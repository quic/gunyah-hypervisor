// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <irq.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <platform_irq.h>
#include <qcbor.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
#include <vcpu.h>
#endif

#include <asm/barrier.h>
#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "gicv3_config.h"
#include "gicv3_its.h"

#if GICV3_HAS_ITS

// The architecture requires that the queue occupies some multiple of 4KB pages
static_assert(util_is_p2aligned(sizeof(gic_its_cmd_t) * GICV3_ITS_QUEUE_LEN,
				12U),
	      "ITS command queue must be a multiple of 4KB");
// We want the queue length to be a power of two, so modulo-size
// operations (to convert sequence numbers to buffer indices) are cheap.
static_assert(util_is_p2(GICV3_ITS_QUEUE_LEN),
	      "ITS command queue should be power-of-two sized");

// ITS driver state.
static gicv3_its_driver_state_t gits[PLATFORM_GITS_COUNT];

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
// True if VMOVP is broadcast in hardware; false if we need to do it ourselves.
static bool gits_vmovp_broadcast;

// Set of ITS numbers to synchronise VMOVP on, if we need to do it in software.
static uint16_t gits_vmovp_itslist;

// Sequence number to correlate software broadcast of VMOVP commands to ITSs.
// This must be serialised with a lock, because the VMOVP commands must be
// queued in the same order across all ITSs in addition to sharing a sequence
// number.
static spinlock_t gits_vmovp_lock;
static uint16_t	  gits_vmovp_sequence PROTECTED_BY(gits_vmovp_lock);

// Level of affinity below which all GICRs must share the LPI configuration
// tables. For GICv4 this also determines the level within which VPEs can
// migrate without VMOVP commands. This a value between 1 and 4, inclusive,
// calculated as 4 - GICR_TYPER.CommonLPIAff. If it is 4, then all affinity
// levels must share tables and VMOVP is never needed.
static count_t gits_common_lpi_affinity;

// VPE ID allocation table, protected by a spinlock.
static spinlock_t gits_vpe_lock;
static BITMAP_DECLARE(GICV3_ITS_VPES, gits_vpe_bitmap)
	PROTECTED_BY(gits_vpe_lock);

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI

// Table of RCU-protected VCPU pointers for allocated VPE IDs.
static thread_t *_Atomic gicv3_its_vpe_table[GICV3_ITS_VPES];

// IRQ range allocated to VPE doorbells.
static irq_range_t gicv3_its_vpe_doorbell_range;
static irq_t	   gicv3_its_vpe_doorbell_base;

#endif // GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI

#endif // defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

// Get a pointer to the ITS command for a given sequence number.
//
// We assume here that the sequence number is somewhere between the allocated
// head and the submitted head, so we can turn it into the buffer index with a
// simple mod. This is not too expensive because GICV3_ITS_QUEUE_LEN is always a
// power of two, so the mod reduces to a shift.
static inline gic_its_cmd_t *
gic_its_cmd_by_seq(platform_msi_controller_id_t its, count_t seq)
{
	assert(its < PLATFORM_GITS_COUNT);

	const index_t index = (index_t)(seq % GICV3_ITS_QUEUE_LEN);
	return &(*gits[its].cmd_queue)[index];
}

// Update and return the cached command queue tail.
//
// The lock for the specified ITS must be held by the caller.
static inline count_result_t
gic_its_cmd_get_tail(platform_msi_controller_id_t its)
{
	assert(its < PLATFORM_GITS_COUNT);

	count_result_t ret;
	GITS_CREADR_t creadr = atomic_load_acquire(&gits[its].regs->ctl.creadr);

	if (GITS_CREADR_get_Stalled(&creadr)) {
		// More specific error reporting is IMP DEF. Either support it
		// or define a better error code. Also, this can be misleading
		// for the caller, since an error here is caused by a previous
		// command, not the one the caller is trying to submit; it may
		// be better to just panic.
		// FIXME: QC Gunyah issue #140
		ret = count_result_error(ERROR_IDLE);
	} else {
		const count_t head	 = gits[its].cmd_queue_head;
		const count_t head_index = head % GICV3_ITS_QUEUE_LEN;
		const count_t tail_index = GITS_CREADR_get_Index(&creadr);

		count_t tail = head - head_index + tail_index;
		if (tail_index > head_index) {
			tail -= GICV3_ITS_QUEUE_LEN;
		}

		gits[its].cmd_queue_cached_tail = tail;
		ret				= count_result_ok(tail);
	}

	return ret;
}

// Allocate a range of entries in the ITS command buffer.
//
// The lock for the specified ITS must be held by the caller, and remain held
// until gic_its_cmd_submit() has been called for the last entry in the
// allocated range (if this function returns success).
static inline count_result_t
gic_its_cmd_alloc(platform_msi_controller_id_t its, count_t num_cmds, bool wait)
{
	assert(its < PLATFORM_GITS_COUNT);

	count_result_t ret;

	bool tail_cache_updated = false;

	if (num_cmds >= GICV3_ITS_QUEUE_LEN) {
		ret = count_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	count_t old_head = gits[its].cmd_queue_head;
	count_t new_head = old_head + num_cmds;

	while ((new_head - gits[its].cmd_queue_cached_tail) >=
	       GICV3_ITS_QUEUE_LEN) {
		// Possibly insufficient space in the command buffer.
		if (wait || !tail_cache_updated) {
			// Re-check the tail in the ITS.
			count_result_t new_tail = gic_its_cmd_get_tail(its);
			if (new_tail.e != OK) {
				// ITS has stalled
				ret = new_tail;
				goto out;
			}
			tail_cache_updated = true;
		} else {
			// Tail is current and we don't want to wait
			ret = count_result_error(ERROR_BUSY);
			goto out;
		}
	}

	gits[its].cmd_queue_head += num_cmds;
	ret = count_result_ok(old_head);

out:
	return ret;
}

// Submit a set of commands to the ITS.
//
// The lock for the specified ITS must be held by the caller, and must have
// been held continuously since a gic_its_cmd_alloc() call that returned a range
// of command buffer entries that included submit_seq.
static inline void
gic_its_cmd_submit(platform_msi_controller_id_t its, count_t submit_seq)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(submit_seq - gits[its].cmd_queue_cached_tail <
	       GICV3_ITS_QUEUE_LEN);

	GITS_CWRITER_t cwriter = GITS_CWRITER_default();

	GITS_CWRITER_set_Index(&cwriter,
			       (submit_seq + 1U) % GICV3_ITS_QUEUE_LEN);

	atomic_store_release(&gits[its].regs->ctl.cwriter, cwriter);
}

static inline count_result_t
gic_its_cmd_enqueue_one(platform_msi_controller_id_t its, gic_its_cmd_t cmd,
			bool wait)
{
	assert(its < PLATFORM_GITS_COUNT);

	TRACE(INFO, INFO, "ITS {:d}: cmd {:#x} {:#x} {:#x} {:#x}", its,
	      cmd.base.bf[0], cmd.base.bf[1], cmd.base.bf[2], cmd.base.bf[3]);

	spinlock_acquire(&gits[its].cmd_queue_lock);

	count_result_t ret = gic_its_cmd_alloc(its, 1U, wait);

	if (ret.e == OK) {
		gic_its_cmd_t *cmd_slot = gic_its_cmd_by_seq(its, ret.r);
		*cmd_slot		= cmd;
		if (gits[its].cmd_queue_flush) {
			CACHE_CLEAN_OBJECT(*cmd_slot);
		}
		gic_its_cmd_submit(its, ret.r);
	}

	spinlock_release(&gits[its].cmd_queue_lock);

	return ret;
}

static void
gicv3_its_check_indirect_table(gicv3_its_indirect_table_t *indirect_table,
			       index_t			   index)
{
	if (indirect_table->first_level == NULL) {
		goto out;
	}

	index_t l1_index = index / indirect_table->entries_l2;
	if (l1_index >= indirect_table->entries_l1) {
		goto out;
	}

	_Atomic GITS_BASER_Indirect_entry_t *entry_ptr =
		&indirect_table->first_level[l1_index];
	GITS_BASER_Indirect_entry_t entry = atomic_load_relaxed(entry_ptr);
	if (GITS_BASER_Indirect_entry_get_Valid(&entry)) {
		goto out;
	}

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_r = partition_alloc(hyp_partition,
						    indirect_table->page_size,
						    indirect_table->page_size);
	if (alloc_r.e != OK) {
		panic("Unable to allocate ITS table");
	}
	(void)memset_s(alloc_r.r, indirect_table->page_size, 0,
		       indirect_table->page_size);
	paddr_t itt_phys =
		partition_virt_to_phys(hyp_partition, (uintptr_t)alloc_r.r);

	GITS_BASER_Indirect_entry_set_Physical_Address(&entry, itt_phys);
	GITS_BASER_Indirect_entry_set_Valid(&entry, true);
	atomic_store_release(entry_ptr, entry);

out:
	return;
}

static void
gicv3_its_allocate_itt(platform_msi_controller_id_t its, size_t itt_entry_size,
		       platform_msi_device_id_t device_id,
		       platform_msi_event_id_t	max_event)
{
	gicv3_its_check_indirect_table(&gits[its].indirect_device_table,
				       device_id);

#if defined(GICV3_ITS_EVENT_BITS)
	count_t event_bits = GICV3_ITS_EVENT_BITS;
	assert(util_bit(event_bits) > max_event);
#else
	// Note: the ITT must be a power-of-two size. Taking the MSB
	// of the maximum ID will effectively round it up. Also, the bit
	// count is offset by 1 in the MAPD command, so the smallest
	// possible table has 2 entries.
	count_t event_bits = (max_event == 0U) ? 1U
					       : (compiler_msb(max_event) + 1U);
#endif
	size_t itt_size	 = itt_entry_size << event_bits;
	size_t itt_align = util_bit(GIC_ITS_CMD_MAPD_ITT_ADDR_PRESHIFT);

	partition_t	 *hyp_partition = partition_get_private();
	void_ptr_result_t alloc_r =
		partition_alloc(hyp_partition, itt_size, itt_align);
	if (alloc_r.e != OK) {
		panic("Unable to allocate ITS table");
	}
	(void)memset_s(alloc_r.r, itt_size, 0, itt_size);
	paddr_t itt_phys =
		partition_virt_to_phys(hyp_partition, (uintptr_t)alloc_r.r);

	gic_its_cmd_t cmd = { .mapd = gic_its_cmd_mapd_default() };
	gic_its_cmd_mapd_set_device_id(&cmd.mapd, device_id);
	gic_its_cmd_mapd_set_size(&cmd.mapd, event_bits - 1U);
	gic_its_cmd_mapd_set_itt_addr(&cmd.mapd, itt_phys);
	gic_its_cmd_mapd_set_valid(&cmd.mapd, true);
	error_t err = gic_its_cmd_enqueue_one(its, cmd, true).e;
	assert(err == OK);
}

static void *
gicv3_its_alloc_table(GITS_BASER_t *baser, count_t entries,
		      gicv3_its_indirect_table_t *indirect_table_ptr,
		      size_t *table_size, count_t page_bits,
		      count_t *page_count, partition_t *hyp_partition)
{
	// An indirect (2-level) table is used if it is implemented by
	// the hardware for this type of base register, supported by
	// the mapping code in this driver (i.e. we have somewhere
	// to store the first level table pointer), and a direct table
	// would occupy more than two pages.
	const bool use_indirect = (indirect_table_ptr != NULL) &&
				  (*page_count > 2U) &&
				  GITS_BASER_get_Indirect(baser);
	if (use_indirect) {
		// Using an indirect table. Calculate the dimensions
		count_t entries_l2 =
			(count_t)((size_t)util_bit(page_bits) /
				  (GITS_BASER_get_Entry_Size(baser) + 1U));
		count_t entries_l1 = ((entries + entries_l2) - 1U) / entries_l2;

		// Recalculate the first-level allocation size
		*table_size =
			entries_l1 * (sizeof(GITS_BASER_Indirect_entry_t) + 1U);
		*table_size = util_p2align_up(*table_size, page_bits);
		assert(page_bits < util_width(*table_size));
		*page_count = (count_t)(*table_size >> page_bits);

		// Record the table dimensions
		indirect_table_ptr->page_size  = util_bit(page_bits);
		indirect_table_ptr->entries_l2 = entries_l2;
		indirect_table_ptr->entries_l1 = entries_l1;
	} else {
		// Using a flat table
		GITS_BASER_set_Indirect(baser, false);
	}

	void_ptr_result_t alloc_r = partition_alloc(
		hyp_partition, *table_size, (size_t)util_bit(page_bits));
	if (alloc_r.e != OK) {
		panic("Unable to allocate ITS table");
	}
	(void)memset_s(alloc_r.r, *table_size, 0, *table_size);

	if (use_indirect) {
		indirect_table_ptr->first_level = alloc_r.r;
	}

	return alloc_r.r;
}

static void
gicv3_its_select_table_type(GITS_TYPER_t		 typer,
			    platform_msi_controller_id_t its,
			    GITS_BASER_t baser, count_t *entries,
			    gicv3_its_indirect_table_t	   **indirect_table_ptr,
			    const platform_msi_controller_t *platform_ctrl)
{
	switch (GITS_BASER_get_Type(&baser)) {
	case GITS_BASER_TYPE_DEVICES: {
		count_t last_device_id = gits[its].first_unused;
		for (index_t i = 0U; i < platform_ctrl->num_devices; i++) {
			last_device_id =
				util_max(last_device_id,
					 platform_ctrl->devices[i].device_id);
		}

		*indirect_table_ptr = &gits[its].indirect_device_table;
		*entries	    = last_device_id + 1U;
		assert(*entries <=
		       (count_t)(util_bit(GITS_TYPER_get_Devbits(&typer) + 1U) -
				 1U));
		break;
	}
	case GITS_BASER_TYPE_COLLECTIONS:
		if (GITS_TYPER_get_HCC(&typer) < PLATFORM_MAX_CORES) {
			// Internal collections aren't enough, we need to
			// allocate a separate collections table.
			*entries = PLATFORM_MAX_CORES;
			if (GITS_TYPER_get_CCT(&typer)) {
				// We can still use the internal collections.
				*entries -= GITS_TYPER_get_HCC(&typer);
			}
		}
		break;
	case GITS_BASER_TYPE_VPES:
#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
		*entries = GICV3_ITS_VPES;
		break;
#else
		// Fall through to unimplemented case.
#endif // GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	case GITS_BASER_TYPE_UNIMPLEMENTED:
	default:
		// Default case
		break;
	}
}

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1 &&    \
	defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
static GITS_BASER_t
gicv3_its_find_vpe_table(platform_msi_controller_id_t new_its,
			 GITS_TYPER_SVPET_t new_svpet, GITS_MPIDR_t new_mpidr)
{
	platform_msi_controller_id_t prev_its;
	GITS_BASER_t		     baser = GITS_BASER_default();

	for (prev_its = 0U; prev_its < new_its; prev_its++) {
		GITS_TYPER_t prev_typer =
			atomic_load_relaxed(&gits[prev_its].regs->ctl.typer);
		if (GITS_TYPER_get_SVPET(&prev_typer) ==
		    GITS_TYPER_SVPET_NOT_SHARED) {
			continue;
		}
		GITS_MPIDR_t prev_mpidr =
			atomic_load_relaxed(&gits[prev_its].regs->ctl.mpidr);
		bool mpidr_match;
		switch (new_svpet) {
		case GITS_TYPER_SVPET_SHARED_AFF3:
			mpidr_match = GITS_MPIDR_get_Aff3(&new_mpidr) ==
				      GITS_MPIDR_get_Aff3(&prev_mpidr);
			break;
		case GITS_TYPER_SVPET_SHARED_AFF2:
			mpidr_match = (GITS_MPIDR_get_Aff3(&new_mpidr) ==
				       GITS_MPIDR_get_Aff3(&prev_mpidr)) &&
				      (GITS_MPIDR_get_Aff2(&new_mpidr) ==
				       GITS_MPIDR_get_Aff2(&prev_mpidr));
			break;
		case GITS_TYPER_SVPET_SHARED_AFF1:
			mpidr_match = (GITS_MPIDR_get_Aff3(&new_mpidr) ==
				       GITS_MPIDR_get_Aff3(&prev_mpidr)) &&
				      (GITS_MPIDR_get_Aff2(&new_mpidr) ==
				       GITS_MPIDR_get_Aff2(&prev_mpidr)) &&
				      (GITS_MPIDR_get_Aff1(&new_mpidr) ==
				       GITS_MPIDR_get_Aff1(&prev_mpidr));
			break;
		case GITS_TYPER_SVPET_NOT_SHARED:
		default:
			mpidr_match = false;
			break;
		}
		if (mpidr_match) {
			// The SVPET fields should be consistent.
			assert(new_svpet == GITS_TYPER_get_SVPET(&prev_typer));
			// GICv4.1 guarantees that the vPE table is in BASER2.
			baser = gits[prev_its].saved_basers[2U];
			assert(GITS_BASER_get_Type(&baser) ==
			       GITS_BASER_TYPE_VPES);
		}
	}

	return baser;
}
#endif

static void
gicv3_its_allocate_tables(platform_msi_controller_id_t its, gits_t *regs,
			  GITS_TYPER_t typer, partition_t *hyp_partition,
			  const platform_msi_controller_t *platform_ctrl)
{
	for (index_t i = 0U; i < util_array_size(regs->ctl.baser); i++) {
		// Write an invalid GITS_BASER specifying our preferred page
		// size (4K) and cache attributes and with the indirect bit set,
		// and then read it back in case those fields are fixed to
		// something else. This also fetches the table type and entry
		// size.
		GITS_BASER_t baser = GITS_BASER_default();
		GITS_BASER_set_Page_Size(&baser, GITS_BASER_PAGE_SIZE_SIZE_4KB);
		GITS_BASER_set_Indirect(&baser, true);
		// Shareability == 1: Inner shareable
		GITS_BASER_set_Shareability(&baser, 1U);
		// OuterCache == 0: Inner and outer attributes are the same
		GITS_BASER_set_OuterCache(&baser, 0U);
		// InnerCache == 7: Inner write back, read + write alloc
		GITS_BASER_set_InnerCache(&baser, 7U);
		atomic_store_relaxed(&regs->ctl.baser[i], baser);
		baser = atomic_load_relaxed(&regs->ctl.baser[i]);

		// VPE tables need special treatment as they may need to be
		// shared between ITSs, and are not needed at all if VPE support
		// is disabled.
		if (GITS_BASER_get_Type(&baser) == GITS_BASER_TYPE_VPES) {
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1 &&    \
	defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
			// GICv4.1 guarantees that the VPE table is in BASER2.
			assert(i == 2U);
			GITS_BASER_t prev_baser = gicv3_its_find_vpe_table(
				its, GITS_TYPER_get_SVPET(&typer),
				atomic_load_relaxed(&regs->ctl.mpidr));
			if (GITS_BASER_get_Valid(&prev_baser)) {
				baser = prev_baser;
				goto baser_valid;
			}
#elif !defined(GICV3_ENABLE_VPE) || !GICV3_ENABLE_VPE
			// No VPE table is needed
			continue;
#endif
		}

		count_t			    entries	       = 0U;
		gicv3_its_indirect_table_t *indirect_table_ptr = NULL;

		gicv3_its_select_table_type(typer, its, baser, &entries,
					    &indirect_table_ptr, platform_ctrl);

		if (entries == 0U) {
			// Nothing to allocate for this base register
			continue;
		}

		// Allocate the first-level table.
		//
		// Allocations must be in multiples of pages; we use whatever
		// page size is already set in the register, which will have
		// been reduced to 4KB (0) if possible when we zeroed it above,
		// but might be fixed to a larger value.
		GITS_BASER_Page_Size_t baser_page_size =
			GITS_BASER_get_Page_Size(&baser);
		size_t table_size =
			entries * (GITS_BASER_get_Entry_Size(&baser) + 1U);
		count_t page_bits =
			(count_t)(12U + (2U * (count_t)baser_page_size));
		table_size = util_p2align_up(table_size, page_bits);
		assert(page_bits < util_width(table_size));
		count_t page_count = (count_t)(table_size >> page_bits);

		void *table_ptr = gicv3_its_alloc_table(
			&baser, entries, indirect_table_ptr, &table_size,
			page_bits, &page_count, hyp_partition);

		assert(table_ptr != NULL);

		// If the table isn't shareable and writeback cacheable, we
		// must clean the cache to PoC after the memset to ensure that
		// the ITS sees the zeros.
#if defined(GICV3_CACHE_INCOHERENT) && GICV3_CACHE_INCOHERENT
		CACHE_CLEAN_RANGE((uint8_t *)alloc_r.r, table_size);
#else
		if ((GITS_BASER_get_Shareability(&baser) != 1U) ||
		    ((GITS_BASER_get_OuterCache(&baser) != 0U) &&
		     (GITS_BASER_get_OuterCache(&baser) != 7U)) ||
		    (GITS_BASER_get_InnerCache(&baser) != 7U)) {
			CACHE_CLEAN_RANGE((uint8_t *)table_ptr, table_size);
		}
#endif

		// Physical_Address: physical address of table base
		paddr_t table_phys = partition_virt_to_phys(
			hyp_partition, (uintptr_t)table_ptr);
		GITS_BASER_set_Physical_Address(&baser, table_phys);

		// Size: table size in pages, minus one
		GITS_BASER_set_Size(&baser, page_count - 1U);

		GITS_BASER_set_Valid(&baser, true);

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1 &&    \
	defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	baser_valid:
#endif
		atomic_store_relaxed(&regs->ctl.baser[i], baser);
		gits[its].saved_basers[i] = baser;

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1 &&    \
	defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
		if ((GITS_TYPER_get_SVPET(&typer) !=
		     GITS_TYPER_SVPET_NOT_SHARED) &&
		    (GITS_BASER_get_Type(&baser) == GITS_BASER_TYPE_VPES)) {
			// Copy all the fields into GICR_VPROPBASER.
			// Unfortunately most of the fields are in different
			// places so we need to use the accessors, we can't just
			// cast the value.
			GICR_VPROPBASER_t vpropbaser =
				GICR_VPROPBASER_default();
			GICR_VPROPBASER_set_Size(&vpropbaser,
						 GITS_BASER_get_Size(&baser));
			GICR_VPROPBASER_set_InnerCache(
				&vpropbaser, GITS_BASER_get_InnerCache(&baser));
			GICR_VPROPBASER_set_Shareability(
				&vpropbaser,
				GITS_BASER_get_Shareability(&baser));
			GICR_VPROPBASER_set_Physical_Address(
				&vpropbaser,
				GITS_BASER_get_Physical_Address(&baser));
			GICR_VPROPBASER_set_Page_Size(
				&vpropbaser, GITS_BASER_get_Page_Size(&baser));
			GICR_VPROPBASER_set_OuterCache(
				&vpropbaser, GITS_BASER_get_OuterCache(&baser));
			GICR_VPROPBASER_set_Z(&vpropbaser, true);
			GICR_VPROPBASER_set_Indirect(&vpropbaser, false);
			GICR_VPROPBASER_set_Valid(&vpropbaser, true);

			gits[its].vpe_table = vpropbaser;
		}
#endif // defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1
       // && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	}
}

static void
gicv3_its_init_one(platform_msi_controller_id_t its, gits_t *regs)
{
	partition_t *hyp_partition = partition_get_private();

	gits[its].regs = regs;

	GITS_TYPER_t typer = atomic_load_relaxed(&regs->ctl.typer);

	assert(GITS_TYPER_get_Physical(&typer));
#if defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	assert(GITS_TYPER_get_Virtual(&typer));
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	if (its == 0U) {
		gits_vmovp_broadcast = GITS_TYPER_get_VMOVP(&typer);
	} else {
		// This should be the same on every ITS!
		assert(gits_vmovp_broadcast == GITS_TYPER_get_VMOVP(&typer));
	}
#endif
#endif // defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	assert(GITS_TYPER_get_VMAPP(&typer) && GITS_TYPER_get_VSGI(&typer));
#endif

	// Ensure that the ITS is disabled and quiescent.
	GITS_CTLR_t ctlr = GITS_CTLR_default();
	atomic_store_relaxed(&regs->ctl.ctlr, ctlr);
	do {
		ctlr = atomic_load_acquire(&regs->ctl.ctlr);
	} while (GITS_CTLR_get_Enabled(&ctlr) ||
		 !GITS_CTLR_get_Quiescent(&ctlr));

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	gits_vmovp_itslist |= util_bit(GITS_CTLR_get_ITS_Number(&ctlr));
#endif
	// Allocate the command queue. Note that this must be 64K-aligned.
	void_ptr_result_t cmd_queue_alloc_r = partition_alloc(
		hyp_partition, sizeof(gic_its_cmd_t) * GICV3_ITS_QUEUE_LEN,
		util_bit(GITS_CBASER_PHYSICAL_ADDRESS_PRESHIFT));
	if (cmd_queue_alloc_r.e != OK) {
		panic("Unable to allocate ITS command queue");
	}
	gic_its_cmd_t(*cmd_queue)[GICV3_ITS_QUEUE_LEN] =
		(gic_its_cmd_t(*)[GICV3_ITS_QUEUE_LEN])cmd_queue_alloc_r.r;

	// Set up the command queue
	spinlock_init(&gits[its].cmd_queue_lock);
	gits[its].cmd_queue	 = cmd_queue;
	gits[its].cmd_queue_phys = partition_virt_to_phys(
		hyp_partition, (uintptr_t)gits[its].cmd_queue);
	gits[its].cmd_queue_head	= 0U;
	gits[its].cmd_queue_cached_tail = 0U;

	atomic_store_relaxed(&regs->ctl.cwriter, GITS_CWRITER_default());

	GITS_CBASER_t cbaser = GITS_CBASER_default();
	// Size: command queue size in 4KB pages, minus one
	const size_t cmd_queue_size = sizeof(*gits[its].cmd_queue);
	GITS_CBASER_set_Size(&cbaser, (count_t)(cmd_queue_size >> 12) - 1U);
	// Shareability == 1: Inner shareable
	GITS_CBASER_set_Shareability(&cbaser, 1U);
	// Physical_Address: physical address of command queue base
	GITS_CBASER_set_Physical_Address(&cbaser, gits[its].cmd_queue_phys);
	// OuterCache == 0: Inner and outer attributes are the same
	GITS_CBASER_set_OuterCache(&cbaser, 0U);
	// InnerCache == 7: Inner write back, read + write alloc
	GITS_CBASER_set_InnerCache(&cbaser, 7U);
	GITS_CBASER_set_Valid(&cbaser, true);

	// cache cbaser, used to rewrite the cbaser on resume.
	gits[its].saved_cbaser = cbaser;

	atomic_store_relaxed(&regs->ctl.cbaser, cbaser);
	cbaser = atomic_load(&regs->ctl.cbaser);

	// Read back the CBASER and check cachability, to determine whether we
	// need to flush commands before submission
#if defined(GICV3_CACHE_INCOHERENT) && GICV3_CACHE_INCOHERENT
	gits[its].cmd_queue_flush = true;
#else
	gits[its].cmd_queue_flush =
		(GITS_CBASER_get_Shareability(&cbaser) != 1U) ||
		((GITS_CBASER_get_OuterCache(&cbaser) != 0U) &&
		 (GITS_CBASER_get_OuterCache(&cbaser) != 7U)) ||
		(GITS_CBASER_get_InnerCache(&cbaser) != 7U);
#endif

	// Determine the lowest device ID that is not associated with a physical
	// device; this is used internally by the virtual ITS implementation.
	const platform_msi_controller_t *platform_ctrl =
		platform_irq_msi_devices(its);
	assert(platform_ctrl != NULL);
	gits[its].first_unused		= 0U;
	gits[its].first_unused_reserved = false;
	gits[its].first_unused_event	= 0U;

	for (index_t i = 0U; i < platform_ctrl->num_devices; i++) {
		if (gits[its].first_unused !=
		    platform_ctrl->devices[i].device_id) {
			break;
		}
		gits[its].first_unused++;
	}

	// Allocate the device, collection and vPE tables
	gicv3_its_allocate_tables(its, regs, typer, hyp_partition,
				  platform_ctrl);

	// Enable the ITS and start processing commands
	GITS_CTLR_set_Enabled(&ctlr, true);
	atomic_store_release(&regs->ctl.ctlr, ctlr);

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	// Wait for the ITS to finish enabling (Quiescent bit clears). Note
	// that Quiescent is UNKNOWN on an enabled ITS before GICv4.1.
	do {
		ctlr = atomic_load_acquire(&regs->ctl.ctlr);
	} while (GITS_CTLR_get_Quiescent(&ctlr));
#endif

	// Allocate an ITT for each of the platform's devices
	size_t itt_entry_size = GITS_TYPER_get_ITT_entry_size(&typer) + 1U;
	for (index_t i = 0U; i < platform_ctrl->num_devices; i++) {
		gicv3_its_allocate_itt(its, itt_entry_size,
				       platform_ctrl->devices[i].device_id,
				       platform_ctrl->devices[i].max_event);
	}
}

platform_msi_id_result_t
gicv3_its_reserve_unused_device(platform_msi_controller_id_t its,
				platform_msi_event_id_t	     max_event)
{
	platform_msi_id_result_t ret;
	platform_msi_id_t	 msi = platform_msi_id_default();

	if (its >= util_array_size(gits)) {
		ret = platform_msi_id_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	if (!gits[its].first_unused_reserved) {
		// Allocate an extra ITT that doesn't belong to any real device,
		// to be used for mapping dummy events in the virtual ITS
		// implementation
		GITS_TYPER_t typer =
			atomic_load_relaxed(&gits[its].regs->ctl.typer);
		size_t itt_entry_size =
			GITS_TYPER_get_ITT_entry_size(&typer) + 1U;
		gicv3_its_allocate_itt(its, itt_entry_size,
				       gits[its].first_unused, max_event);
		gits[its].first_unused_reserved = true;
	}

	platform_msi_id_set_device_id(&msi, gits[its].first_unused);
	platform_msi_id_set_event_id(&msi, gits[its].first_unused_event);
	gits[its].first_unused_event++;
	assert(gits[its].first_unused_event < max_event);
	ret = platform_msi_id_result_ok(msi);
out:

	return ret;
}

void
gicv3_its_init(gits_t *(*regs)[PLATFORM_GITS_COUNT],
	       count_t common_lpi_affinity)
{
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	gits_common_lpi_affinity = common_lpi_affinity;

	spinlock_init(&gits_vmovp_lock);
#else
	(void)common_lpi_affinity;
#endif

	for (count_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gicv3_its_init_one(i, (*regs)[i]);
	}

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	spinlock_init(&gits_vpe_lock);
#endif
}

static void
gicv3_its_init_cpu_one(platform_msi_controller_id_t its, cpu_index_t cpu,
		       paddr_t gicr_phys, index_t gicr_pn)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(cpu < PLATFORM_MAX_CORES);

	// Set RDbase to either the physical redistributor address if PTA=1, or
	// else to the processor number reported by GICR_TYPER.
	GITS_TYPER_t typer = atomic_load_relaxed(&gits[its].regs->ctl.typer);
	if (GITS_TYPER_get_PTA(&typer)) {
		gic_its_rdbase_pta1_set_paddr(&gits[its].rdbases[cpu].pta1,
					      gicr_phys);
	} else {
		gits[its].rdbases[cpu].pta0 = (gic_its_rdbase_pta0_t)gicr_pn;
	}

	// Map a collection with the same ID as the CPU index to the CPU's
	// redistributor. This remains mapped statically. Note that we have
	// to redo this on resume if the collection is internally stored.
	gic_its_cmd_t cmd = { .mapc = gic_its_cmd_mapc_default() };
	gic_its_cmd_mapc_set_icid(&cmd.mapc, cpu);
	gic_its_cmd_mapc_set_rdbase(&cmd.mapc, gits[its].rdbases[cpu].raw);
	gic_its_cmd_mapc_set_valid(&cmd.mapc, true);
	error_t err = gic_its_cmd_enqueue_one(its, cmd, true).e;
	assert(err == OK);
}

void
gicv3_its_init_cpu(cpu_index_t cpu, gicr_t *gicr, paddr_t gicr_phys,
		   index_t gicr_pn)
{
	for (count_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gicv3_its_init_cpu_one(i, cpu, gicr_phys, gicr_pn);
	}

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1 &&    \
	defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	bool	     vpropbase_set = false;
	GICR_TYPER_t gicr_typer	   = atomic_load_relaxed(&gicr->rd.typer);

	for (count_t i = 0U; (i < PLATFORM_GITS_COUNT) && !vpropbase_set; i++) {
		bool	     mpidr_match;
		GITS_TYPER_t gits_typer =
			atomic_load_relaxed(&gits[i].regs->ctl.typer);
		GITS_MPIDR_t mpidr =
			atomic_load_relaxed(&gits[i].regs->ctl.mpidr);
		switch (GITS_TYPER_get_SVPET(&gits_typer)) {
		case GITS_TYPER_SVPET_SHARED_AFF3:
			mpidr_match = GITS_MPIDR_get_Aff3(&mpidr) ==
				      GICR_TYPER_get_Aff3(&gicr_typer);
			break;
		case GITS_TYPER_SVPET_SHARED_AFF2:
			mpidr_match = (GITS_MPIDR_get_Aff3(&mpidr) ==
				       GICR_TYPER_get_Aff3(&gicr_typer)) &&
				      (GITS_MPIDR_get_Aff2(&mpidr) ==
				       GICR_TYPER_get_Aff2(&gicr_typer));
			break;
		case GITS_TYPER_SVPET_SHARED_AFF1:
			mpidr_match = (GITS_MPIDR_get_Aff3(&mpidr) ==
				       GICR_TYPER_get_Aff3(&gicr_typer)) &&
				      (GITS_MPIDR_get_Aff2(&mpidr) ==
				       GICR_TYPER_get_Aff2(&gicr_typer)) &&
				      (GITS_MPIDR_get_Aff1(&mpidr) ==
				       GICR_TYPER_get_Aff1(&gicr_typer));
			break;
		case GITS_TYPER_SVPET_NOT_SHARED:
		default:
			mpidr_match = false;
			break;
		}
		if (mpidr_match) {
			atomic_store_relaxed(&gicr->vlpi.vpropbaser,
					     gits[i].vpe_table);
			vpropbase_set = true;
		}
	}

	if (!vpropbase_set) {
		panic("No ITS had a shared vPE table");
	}
#else
	(void)gicr;
#endif
}

bool_result_t
gicv3_its_is_complete(platform_msi_controller_id_t its, count_t cmd_seq)
{
	bool_result_t ret;

	spinlock_acquire(&gits[its].cmd_queue_lock);
	count_t tail = gits[its].cmd_queue_cached_tail;

	// If cmd_seq was within the range of the queue last time we updated
	// the cached tail, update it again. Note that the current tail is still
	// in the queue; i.e. cmd_seq == tail is not complete.
	if ((cmd_seq - tail) < GICV3_ITS_QUEUE_LEN) {
		count_result_t new_tail = gic_its_cmd_get_tail(its);
		if (new_tail.e != OK) {
			// ITS has stalled
			ret = bool_result_error(new_tail.e);
			goto out;
		}
		tail = new_tail.r;
	}

	// If cmd_seq is still within the range of the queue, the command is
	// not complete yet.
	ret = bool_result_ok((cmd_seq - tail) >= GICV3_ITS_QUEUE_LEN);

out:
	spinlock_release(&gits[its].cmd_queue_lock);
	return ret;
}

error_t
gicv3_its_wait(platform_msi_controller_id_t its, count_t cmd_seq)
{
	error_t err;

	spinlock_acquire(&gits[its].cmd_queue_lock);
	count_t tail = gits[its].cmd_queue_cached_tail;
	spinlock_release(&gits[its].cmd_queue_lock);

	// While cmd_seq remains within the range of the queue, poll the tail.
	// Note that the current tail is still in the queue; i.e. cmd_seq ==
	// tail is not complete.
	while ((cmd_seq - tail) < GICV3_ITS_QUEUE_LEN) {
		asm_yield();
		spinlock_acquire(&gits[its].cmd_queue_lock);
		count_result_t new_tail = gic_its_cmd_get_tail(its);
		if (new_tail.e != OK) {
			// ITS has stalled
			err = new_tail.e;
			spinlock_release(&gits[its].cmd_queue_lock);
			goto out_err;
		}
		tail = new_tail.r;
		spinlock_release(&gits[its].cmd_queue_lock);
	}

	// Checked all items within the range
	err = OK;

out_err:
	return err;
}

error_t
gicv3_its_map(platform_msi_id_t msi_id, cpu_index_t cpu, irq_t lpi)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	assert(cpu < PLATFORM_MAX_CORES);
	gic_its_cmd_t cmd = { .mapti = gic_its_cmd_mapti_default() };
	gic_its_cmd_mapti_set_device_id(&cmd.mapti,
					platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_mapti_set_event_id(&cmd.mapti,
				       platform_msi_id_get_event_id(&msi_id));
	gic_its_cmd_mapti_set_lpi(&cmd.mapti, lpi);
	gic_its_cmd_mapti_set_icid(&cmd.mapti, cpu);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_move(platform_msi_id_t msi_id, cpu_index_t cpu)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	assert(cpu < PLATFORM_MAX_CORES);
	gic_its_cmd_t cmd = { .movi = gic_its_cmd_movi_default() };
	gic_its_cmd_movi_set_device_id(&cmd.movi,
				       platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_movi_set_event_id(&cmd.movi,
				      platform_msi_id_get_event_id(&msi_id));
	gic_its_cmd_movi_set_icid(&cmd.movi, cpu);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_invalidate(platform_msi_id_t msi_id)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	gic_its_cmd_t cmd = { .inv = gic_its_cmd_inv_default() };
	gic_its_cmd_inv_set_device_id(&cmd.inv,
				      platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_inv_set_event_id(&cmd.inv,
				     platform_msi_id_get_event_id(&msi_id));
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_invalidate_all(platform_msi_controller_id_t its, cpu_index_t cpu)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(cpu < PLATFORM_MAX_CORES);
	gic_its_cmd_t cmd = { .invall = gic_its_cmd_invall_default() };
	gic_its_cmd_invall_set_icid(&cmd.invall, cpu);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

count_result_t
gicv3_its_sync(platform_msi_controller_id_t its, cpu_index_t cpu)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(cpu < PLATFORM_MAX_CORES);
	gic_its_cmd_t cmd = { .sync = gic_its_cmd_sync_default() };
	gic_its_cmd_sync_set_rdbase(&cmd.sync, gits[its].rdbases[cpu].raw);
	return gic_its_cmd_enqueue_one(its, cmd, true);
}

error_t
gicv3_its_assert(platform_msi_id_t msi_id)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	gic_its_cmd_t cmd = { .int_ = gic_its_cmd_int_default() };
	gic_its_cmd_int_set_device_id(&cmd.int_,
				      platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_int_set_event_id(&cmd.int_,
				     platform_msi_id_get_event_id(&msi_id));
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_clear(platform_msi_id_t msi_id)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	gic_its_cmd_t cmd = { .clear = gic_its_cmd_clear_default() };
	gic_its_cmd_clear_set_device_id(&cmd.clear,
					platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_clear_set_event_id(&cmd.clear,
				       platform_msi_id_get_event_id(&msi_id));
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_discard(platform_msi_id_t msi_id)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	gic_its_cmd_t cmd = { .discard = gic_its_cmd_discard_default() };
	gic_its_cmd_discard_set_device_id(
		&cmd.discard, platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_discard_set_event_id(&cmd.discard,
					 platform_msi_id_get_event_id(&msi_id));
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
void
gicv3_its_vpe_handle_irq_init(void)
{
	// TODO: for now, assume that vPE doorbells are the only use of LPIs
	// in the hypervisor when GICv4.1 is present. In future we may need to
	// allocate this from somewhere in the available LPI range.
	gicv3_its_vpe_doorbell_base = GIC_LPI_BASE;

	// Allocate a range of doorbells corresponding to the VPE IDs.
	irq_range_add(&gicv3_its_vpe_doorbell_range,
		      IRQ_RANGE_TYPE_GICV3_VPE_DOORBELL,
		      gicv3_its_vpe_doorbell_base, GICV3_ITS_VPES);
}

error_t
gicv3_handle_object_create_thread(thread_create_t thread_create)
{
	assert(thread_create.thread != NULL);
	thread_create.thread->gicv3_its_mapped_cpu = CPU_INDEX_INVALID;
	thread_create.thread->gicv3_its_vpe_id	   = GICV3_VPE_ID_INVALID;
	return OK;
}

error_t
gicv3_its_vpe_activate(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu->gicv3_its_vpe_id >= GICV3_ITS_VPES);
	assert(!cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu));

	error_t err;

	index_t vpe_bit;
	spinlock_acquire(&gits_vpe_lock);
	if (bitmap_ffc(gits_vpe_bitmap, GICV3_ITS_VPES, &vpe_bit)) {
		bitmap_set(gits_vpe_bitmap, vpe_bit);
	} else {
		spinlock_release(&gits_vpe_lock);
		err = ERROR_BUSY;
		goto fail_vpe_id;
	}
	spinlock_release(&gits_vpe_lock);

	err		       = OK;
	vcpu->gicv3_its_vpe_id = (gic_its_vpe_id_t)vpe_bit;

fail_vpe_id:
	return err;
}

static count_result_t
gicv3_its_vpe_unmap_partial(gic_its_vpe_id_t		 vpe_id,
			    platform_msi_controller_id_t last_its);

static error_t
gicv3_its_vpe_map_submit(const thread_t *vcpu, cpu_index_t cpu,
			 count_t virq_bits, paddr_t pending_table,
			 bool pending_zeroed, paddr_t config_table,
			 irq_t db_lpi)
{
	error_t err;

	// After this point the VPE tables may be nonzero, so any GICR that has
	// not been initialised yet will need to read the table.
	for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		GICR_VPROPBASER_set_Z(&gits[i].vpe_table, false);
	}

	// Issue a VMAPP to every ITS. Note that this is required even if the
	// ITSs share vPE tables.
	platform_msi_controller_id_t map_its;
	for (map_its = 0U; map_its < PLATFORM_GITS_COUNT; map_its++) {
		gic_its_cmd_t cmd = { .vmapp = gic_its_cmd_vmapp_default() };
		gic_its_cmd_vmapp_set_alloc(&cmd.vmapp, map_its == 0U);
		gic_its_cmd_vmapp_set_ptz(&cmd.vmapp, pending_zeroed);
		gic_its_cmd_vmapp_set_vconf_addr(&cmd.vmapp, config_table);
		gic_its_cmd_vmapp_set_db_lpi(&cmd.vmapp, db_lpi);
		gic_its_cmd_vmapp_set_vpe_id(&cmd.vmapp,
					     vcpu->gicv3_its_vpe_id);
		gic_its_cmd_vmapp_set_rdbase(&cmd.vmapp,
					     gits[map_its].rdbases[cpu].raw);
		gic_its_cmd_vmapp_set_valid(&cmd.vmapp, true);
		gic_its_cmd_vmapp_set_vpt_size(&cmd.vmapp, virq_bits);
		gic_its_cmd_vmapp_set_vpt_addr(&cmd.vmapp, pending_table);
		count_result_t seq =
			gic_its_cmd_enqueue_one(map_its, cmd, true);
		err = seq.e;

		// If this is the first of multiple ITSs, the spec implies (but
		// does not outright confirm or deny) that we need to wait for
		// the map command with Alloc=1 to complete before sending map
		// commands to the remaining ITSs with Alloc=0.
		if ((err == OK) && (map_its == 0U) &&
		    (PLATFORM_GITS_COUNT > 1U)) {
			err = gicv3_its_wait(map_its, seq.r);
		}
		if (err != OK) {
			goto out_err;
		}
	}

	err = OK;

out_err:
	// If any of the VPE maps failed, we must unmap them all and wait for
	// the unmap to finish.
	if ((err != OK) && (map_its != 0U)) {
		count_result_t count_r = gicv3_its_vpe_unmap_partial(
			vcpu->gicv3_its_vpe_id, map_its - 1U);
		if (count_r.e != OK) {
			panic("Could not revert VPE mapping!");
		} else {
			if (gicv3_its_wait(0U, count_r.r) != OK) {
				panic("Could not revert VPE mapping!");
			}
		}
	}

	return err;
}

error_t
gicv3_its_vpe_map(thread_t *vcpu, count_t virq_bits, paddr_t config_table,
		  size_t config_table_size, paddr_t pending_table,
		  size_t pending_table_size, bool pending_zeroed)
{
	assert(vcpu != NULL);
	assert(!cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu));

	error_t err;

	// The VCPU has LPI support if virq_bits is at least 14.
	//
	// We don't put an explicit upper bound on virq_bits; instead we will
	// apply a per-ITS limit when we perform VMAPP. This allows us to
	// support multiple ITSs that have different implemented ID limits.
	//
	// The GICv4.1 specification only explicitly defines the minimum VIRQ
	// size of 14 in the pseudocode for VMAPP (v4.1), which is confusingly
	// placed in the VMAPP (v4.0) section (5.3.18 in revision F). Note that
	// setting the minimum size to 13, which could conceivably enable vSGI
	// delivery without allocating any vLPIs, is not permitted.
	if (virq_bits < 14U) {
		// No LPI support for this VCPU; nothing to do.
		err = OK;
		goto out;
	}

	// Get the CPU's current affinity, to select the target redistributor.
	scheduler_lock(vcpu);
	cpu_index_t cpu = scheduler_get_affinity(vcpu);
	if (cpu >= PLATFORM_MAX_CORES) {
		err = ERROR_OBJECT_CONFIG;
		goto fail_locked;
	}

	// Ensure the VCPU doesn't migrate while we're mapping it to ITSs.
	scheduler_pin(vcpu);
	scheduler_unlock(vcpu);

	// We can't safely allow this cross-VCPU at present because the physical
	// ITS rdbases are not set until the corresponding PCPU powers on. The
	// spec allows cross-CPU LPI enables but it's unlikely that the VM will
	// try it; CPUs usually control their own redistributors.
	if (vcpu != thread_get_self()) {
		err = ERROR_DENIED;
		goto fail;
	}

	if (vcpu->gicv3_its_vpe_id >= GICV3_ITS_VPES) {
		// This VCPU doesn't have VLPI support
		err = ERROR_OBJECT_CONFIG;
		goto fail;
	}

	irq_t db_lpi = vcpu->gicv3_its_vpe_id + gicv3_its_vpe_doorbell_base;
	assert(db_lpi >= GIC_LPI_BASE);
	atomic_store_relaxed(&gicv3_its_vpe_table[vcpu->gicv3_its_vpe_id],
			     vcpu);
	platform_irq_enable_percpu(db_lpi, cpu);

	if (!util_is_p2aligned(config_table,
			       GIC_ITS_CMD_VMAPP_VCONF_ADDR_PRESHIFT)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
		goto fail;
	}

	if (config_table_size <
	    (((size_t)util_bit(virq_bits)) - GIC_LPI_BASE)) {
		err = ERROR_ARGUMENT_SIZE;
		goto fail;
	}

	if (!util_is_p2aligned(pending_table,
			       GIC_ITS_CMD_VMAPP_VPT_ADDR_PRESHIFT)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
		goto fail;
	}

	if (((pending_table_size * util_width(uint8_t)) < pending_table_size) ||
	    ((pending_table_size * util_width(uint8_t)) <
	     ((size_t)util_bit(virq_bits)))) {
		err = ERROR_ARGUMENT_SIZE;
		goto fail;
	}
	err = gicv3_its_vpe_map_submit(vcpu, cpu, virq_bits, pending_table,
				       pending_zeroed, config_table, db_lpi);

fail:
	if (err == OK) {
		vcpu->gicv3_its_mapped_cpu = cpu;
	}

	// The VCPU can migrate again now.
	scheduler_lock(vcpu);
	scheduler_unpin(vcpu);

fail_locked:
	scheduler_unlock(vcpu);

out:
	return err;
}

// Unmap a VPE from all ITSs up to the one with the specified number. This
// is used either for unmap, or to unwind a failed attempt to map. It must
// only be called on a VCPU that is pinned.
static count_result_t
gicv3_its_vpe_unmap_partial(gic_its_vpe_id_t		 vpe_id,
			    platform_msi_controller_id_t last_its)
{
	platform_msi_controller_id_t unmap_its;
	count_t			     seq_nums[PLATFORM_GITS_COUNT] = { 0 };

	// Send the unmaps in reverse order so we can finish with ITS 0; this is
	// because the count we return for the Alloc=1 unmap is documented as
	// being for ITS 0.
	for (unmap_its = last_its; unmap_its < PLATFORM_GITS_COUNT;
	     unmap_its--) {
		// If this is the last of multiple ITSs, the spec implies (but
		// does not outright confirm or deny) that we need to wait for
		// all preceding unmap commands with Alloc=0 to complete before
		// sending an unmap command to the last ITS with Alloc=1.
		if (unmap_its == 0U) {
			platform_msi_controller_id_t wait_its;
			for (wait_its = last_its; wait_its > unmap_its;
			     wait_its--) {
				if (gicv3_its_wait(wait_its,
						   seq_nums[wait_its]) != OK) {
					panic("ITS: Issue with preceding unmap commands!");
				}
			}
		}

		gic_its_cmd_t cmd = { .vmapp = gic_its_cmd_vmapp_default() };
		gic_its_cmd_vmapp_set_alloc(&cmd.vmapp, unmap_its == last_its);
		gic_its_cmd_vmapp_set_vpe_id(&cmd.vmapp, vpe_id);
		gic_its_cmd_vmapp_set_valid(&cmd.vmapp, false);
		count_result_t seq =
			gic_its_cmd_enqueue_one(unmap_its, cmd, true);
		// We can't reasonably recover from a failure here...
		assert(seq.e == OK);

		seq_nums[unmap_its] = seq.r;
	}

	return count_result_ok(seq_nums[0U]);
}

count_result_t
gicv3_its_vpe_unmap(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu));

	count_result_t ret;

	// Ensure the VCPU doesn't migrate while we're unmapping it.
	scheduler_lock(vcpu);
	scheduler_pin(vcpu);
	scheduler_unlock(vcpu);

	if (vcpu->gicv3_its_vpe_id >= GICV3_ITS_VPES) {
		// This VCPU doesn't have VLPI support
		ret = count_result_error(ERROR_OBJECT_CONFIG);
		goto out;
	}

	ret = gicv3_its_vpe_unmap_partial(vcpu->gicv3_its_vpe_id,
					  PLATFORM_GITS_COUNT - 1U);

	if (ret.e == OK) {
		irq_t db_lpi =
			vcpu->gicv3_its_vpe_id + gicv3_its_vpe_doorbell_base;
		platform_irq_disable_percpu(db_lpi, vcpu->gicv3_its_mapped_cpu);
		atomic_store_relaxed(
			&gicv3_its_vpe_table[vcpu->gicv3_its_vpe_id], NULL);
	}

out:
	if (ret.e == OK) {
		vcpu->gicv3_its_mapped_cpu = CPU_INDEX_INVALID;
	}

	// The VCPU can migrate again now.
	scheduler_lock(vcpu);
	scheduler_unpin(vcpu);
	scheduler_unlock(vcpu);

	return ret;
}

void
gicv3_its_vpe_cleanup(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(!cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu));

	if (vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES) {
		spinlock_acquire(&gits_vpe_lock);
		bitmap_clear(gits_vpe_bitmap, vcpu->gicv3_its_vpe_id);
		spinlock_release(&gits_vpe_lock);
	}
}

static void
gicv3_its_vpe_move_submit(const thread_t *vcpu, cpu_index_t prev_cpu,
			  cpu_index_t new_cpu, irq_t db_lpi)
	REQUIRE_LOCK(preempt_disabled)
{
	gicv3_vpe_sync_deschedule(prev_cpu, true);

	gic_its_cmd_t cmd = { .vmovp = gic_its_cmd_vmovp_default() };
	gic_its_cmd_vmovp_set_vpe_id(&cmd.vmovp, vcpu->gicv3_its_vpe_id);
	gic_its_cmd_vmovp_set_db_lpi(&cmd.vmovp, db_lpi);

	gic_its_cmd_vmovp_set_db(&cmd.vmovp, vcpu_expects_wakeup(vcpu));

	if (gits_vmovp_broadcast) {
		gic_its_cmd_vmovp_set_rdbase(&cmd.vmovp,
					     gits[0U].rdbases[new_cpu].raw);
		count_result_t seq = gic_its_cmd_enqueue_one(0U, cmd, true);
		assert(seq.e == OK);
	} else {
		spinlock_acquire_nopreempt(&gits_vmovp_lock);
		gic_its_cmd_vmovp_set_seqnum(&cmd.vmovp, gits_vmovp_sequence);
		gits_vmovp_sequence++;
		gic_its_cmd_vmovp_set_itslist(&cmd.vmovp, gits_vmovp_itslist);

		for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
			gic_its_cmd_vmovp_set_rdbase(
				&cmd.vmovp, gits[i].rdbases[new_cpu].raw);
			count_result_t seq =
				gic_its_cmd_enqueue_one(i, cmd, true);
			assert(seq.e == OK);
		}
		spinlock_release_nopreempt(&gits_vmovp_lock);
	}
}

void
gicv3_handle_scheduler_affinity_changed(thread_t *thread, cpu_index_t prev_cpu,
					cpu_index_t next_cpu)
{
	thread_t *vcpu = thread;
	assert_debug(vcpu != NULL);

	if (vcpu->gicv3_its_vpe_id >= GICV3_ITS_VPES) {
		// Nothing to do for a VCPU with no VLPI support.
		goto out;
	}
	irq_t db_lpi = vcpu->gicv3_its_vpe_id + gicv3_its_vpe_doorbell_base;
	assert(db_lpi >= GIC_LPI_BASE);

	if (!cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu)) {
		// Nothing to do for an unmapped VCPU.
		goto out;
	}
	assert(!cpulocal_index_valid(prev_cpu) ||
	       (prev_cpu == vcpu->gicv3_its_mapped_cpu));

	if (!cpulocal_index_valid(next_cpu)) {
		// Nothing to do if we don't have a next CPU to map to.
		//
		// In this case we leave the VPE mapped to the old CPU rather
		// than unmapping it. It won't do any harm being mapped, and
		// this allows us to avoid a VMOVP later on if the affinity is
		// set back to a CPU in the same affinity group.
		goto out;
	}

	// Compare affinity values for the old and new physical CPUs. A VMOVP is
	// needed if their GICRs are in different CommonLPIAff groups, which are
	// the groups that must share LPI and VPE configuration tables.
	platform_mpidr_mapping_t mapping = platform_cpu_get_mpidr_mapping();
	MPIDR_EL1_t		 next_mpidr =
		platform_cpu_map_index_to_mpidr(&mapping, next_cpu);
	MPIDR_EL1_t old_mpidr = platform_cpu_map_index_to_mpidr(
		&mapping, vcpu->gicv3_its_mapped_cpu);

	bool need_move = false;

	if ((gits_common_lpi_affinity <= 3U) &&
	    (MPIDR_EL1_get_Aff3(&next_mpidr) !=
	     MPIDR_EL1_get_Aff3(&old_mpidr))) {
		need_move = true;
	}

	if ((gits_common_lpi_affinity <= 2U) &&
	    (MPIDR_EL1_get_Aff2(&next_mpidr) !=
	     MPIDR_EL1_get_Aff2(&old_mpidr))) {
		need_move = true;
	}

	if ((gits_common_lpi_affinity == 1U) &&
	    (MPIDR_EL1_get_Aff1(&next_mpidr) !=
	     MPIDR_EL1_get_Aff1(&old_mpidr))) {
		need_move = true;
	}

	// LPI config is always shared across Aff0, no need to check it

	if (need_move) {
		// If the thread was running when its affinity was changed,
		// the GICR may not have finished descheduling it, in which
		// case we can't safely issue a VMOVP yet. Poll the GICR until
		// its deschedule completes, noting that it is possible that
		// another VCPU has since been scheduled.
		gicv3_its_vpe_move_submit(vcpu, prev_cpu, next_cpu, db_lpi);
	}

	vcpu->gicv3_its_mapped_cpu = next_cpu;

out:
	return;
}

bool
gicv3_its_vpe_handle_irq_dispatch_doorbell(size_t offset)
{
	assert(offset < util_array_size(gicv3_its_vpe_table));

	thread_t *vcpu = atomic_load_consume(&gicv3_its_vpe_table[offset]);

	scheduler_lock(vcpu);
	vcpu->gicv3_its_need_wakeup_check = true;
	vcpu_wakeup(vcpu);
	scheduler_unlock(vcpu);

	return true;
}

error_t
gicv3_its_vmap(platform_msi_id_t msi_id, thread_t *vcpu, irq_t lpi)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	gic_its_cmd_t cmd = { .vmapti = gic_its_cmd_vmapti_default() };
	gic_its_cmd_vmapti_set_device_id(
		&cmd.vmapti, platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_vmapti_set_event_id(&cmd.vmapti,
					platform_msi_id_get_event_id(&msi_id));
	gic_its_cmd_vmapti_set_vlpi(&cmd.vmapti, lpi);
	gic_its_cmd_vmapti_set_vpe_id(&cmd.vmapti, vcpu->gicv3_its_vpe_id);
	gic_its_cmd_vmapti_set_db_lpi(&cmd.vmapti, 1023U);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_vmove(platform_msi_id_t msi_id, thread_t *vcpu)
{
	platform_msi_controller_id_t its =
		platform_msi_id_get_its_index(&msi_id);
	assert(its < PLATFORM_GITS_COUNT);
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	gic_its_cmd_t cmd = { .vmovi = gic_its_cmd_vmovi_default() };
	gic_its_cmd_vmovi_set_device_id(&cmd.vmovi,
					platform_msi_id_get_device_id(&msi_id));
	gic_its_cmd_vmovi_set_event_id(&cmd.vmovi,
				       platform_msi_id_get_event_id(&msi_id));
	gic_its_cmd_vmovi_set_vpe_id(&cmd.vmovi, vcpu->gicv3_its_vpe_id);
	gic_its_cmd_vmovi_set_db(&cmd.vmovi, false);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

error_t
gicv3_its_vinvalidate_all(platform_msi_controller_id_t its, thread_t *vcpu)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	gic_its_cmd_t cmd = { .vinvall = gic_its_cmd_vinvall_default() };
	gic_its_cmd_vinvall_set_vpe_id(&cmd.vinvall, vcpu->gicv3_its_vpe_id);
	return gic_its_cmd_enqueue_one(its, cmd, true).e;
}

count_result_t
gicv3_its_vsync(platform_msi_controller_id_t its, thread_t *vcpu)
{
	assert(its < PLATFORM_GITS_COUNT);
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	gic_its_cmd_t cmd = { .vsync = gic_its_cmd_vsync_default() };
	gic_its_cmd_vsync_set_vpe_id(&cmd.vsync, vcpu->gicv3_its_vpe_id);
	return gic_its_cmd_enqueue_one(its, cmd, true);
}

static const platform_msi_controller_id_t gicv3_default_its =
	(platform_msi_controller_id_t)0U;

error_t
gicv3_its_vsgi_config(thread_t *vcpu, virq_t vsgi, bool enabled, bool group1,
		      uint8_t priority)
{
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	error_t ret = ERROR_IDLE;

	if (cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu)) {
		gic_its_cmd_t cmd = { .vsgi = gic_its_cmd_vsgi_default() };
		gic_its_cmd_vsgi_set_enable(&cmd.vsgi, enabled);
		gic_its_cmd_vsgi_set_group1(&cmd.vsgi, group1);
		gic_its_cmd_vsgi_set_priority(&cmd.vsgi, priority);
		gic_its_cmd_vsgi_set_sgi(&cmd.vsgi, vsgi);
		gic_its_cmd_vsgi_set_vpe_id(&cmd.vsgi, vcpu->gicv3_its_vpe_id);
		ret = gic_its_cmd_enqueue_one(gicv3_default_its, cmd, true).e;
	}

	return ret;
}

error_t
gicv3_its_vsgi_assert(thread_t *vcpu, virq_t vsgi)
{
	assert(cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu));
	GITS_SGIR_t sgir = GITS_SGIR_default();
	GITS_SGIR_set_vINTID(&sgir, vsgi);
	GITS_SGIR_set_vPEID(&sgir, vcpu->gicv3_its_vpe_id);
	atomic_store_relaxed(&gits[gicv3_default_its].regs->vsgi.sgir, sgir);
	return OK;
}

error_t
gicv3_its_vsgi_clear(thread_t *vcpu, virq_t vsgi)
{
	assert(vcpu->gicv3_its_vpe_id < GICV3_ITS_VPES);
	error_t ret = ERROR_IDLE;

	if (cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu)) {
		gic_its_cmd_t cmd = { .vsgi = gic_its_cmd_vsgi_default() };
		gic_its_cmd_vsgi_set_clear(&cmd.vsgi, true);
		gic_its_cmd_vsgi_set_sgi(&cmd.vsgi, vsgi);
		gic_its_cmd_vsgi_set_vpe_id(&cmd.vsgi, vcpu->gicv3_its_vpe_id);
		ret = gic_its_cmd_enqueue_one(gicv3_default_its, cmd, true).e;
	}

	return ret;
}

count_result_t
gicv3_its_vsgi_sync(thread_t *vcpu)
{
	count_result_t ret = count_result_error(ERROR_IDLE);
	if (cpulocal_index_valid(vcpu->gicv3_its_mapped_cpu)) {
		ret = gicv3_its_vsync(gicv3_default_its, vcpu);
	}
	return ret;
}

bool_result_t
gicv3_its_vsgi_is_complete(count_t cmd_seq)
{
	return gicv3_its_is_complete(gicv3_default_its, cmd_seq);
}

#elif defined(GICV3_USE_VLPI) && GICV3_USE_VLPI // && !GICV3_HAS_VLPI_V4_1
#error "GICv4.0 VLPI management is not implemented"
#endif

#endif // defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

size_t
gicv3_its_page_size(GITS_BASER_Page_Size_t page_size)
{
	size_t ret;

	switch (page_size) {
	case GITS_BASER_PAGE_SIZE_SIZE_4KB:
		ret = 4096U;
		break;
	case GITS_BASER_PAGE_SIZE_SIZE_16KB:
		ret = 16384U;
		break;
	case GITS_BASER_PAGE_SIZE_SIZE_64KB:
	default:
		ret = 65536U;
		break;
	}

	return ret;
}

void
gicv3_handle_abort_kernel(void)
{
	if (gits[0].regs == NULL) {
		// Early abort before initialized
		goto out;
	}
	for (count_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		if (gic_its_cmd_get_tail(i).e != OK) {
			TRACE_AND_LOG(ERROR, WARN,
				      "ITS {:d}: command queue stalled", i);
		}
	}
out:
	return;
}

void
gicv3_its_disable_all(void)
{
	GITS_CTLR_t gits_ctlr;
	// Disable all the ITSs before waiting for any of them.
	//
	// If multiple ITSs are present, this will allow them to all flush
	// their caches in parallel.
	for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gits_t *its = gits[i].regs;
		gits_ctlr   = atomic_load_relaxed(&its->ctl.ctlr);

		GITS_CTLR_set_Enabled(&gits_ctlr, false);
		atomic_store_relaxed(&its->ctl.ctlr, gits_ctlr);
	}

	// Poll for quiescent state
	for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gits_t *its = gits[i].regs;

		do {
			gits_ctlr = atomic_load_acquire(&its->ctl.ctlr);
		} while (!GITS_CTLR_get_Quiescent(&gits_ctlr));
	}
}

error_t
gicv3_handle_object_create_gicv3_its(gicv3_its_create_t gicv3_its_create)
{
	error_t	     err;
	gicv3_its_t *gicv3_its = gicv3_its_create.gicv3_its;

	if (gicv3_its_create.phys_index >= PLATFORM_GITS_COUNT) {
		err = ERROR_ARGUMENT_INVALID;
		goto fail;
	}

	gicv3_its->phys_index = gicv3_its_create.phys_index;
	err		      = OK;

fail:
	return err;
}

#if defined(INTERFACE_ROOTVM)
void
gicv3_handle_rootvm_init(partition_t *root_partition, cspace_t *root_cspace,
			 qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	QCBOREncode_OpenArrayInMap(qcbor_enc_ctxt, "its_caps");

	// Create the ITS objects for the root VM
	for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gicv3_its_create_t its_params = {
			.phys_index = i,
		};
		gicv3_its_ptr_result_t its_r = partition_allocate_gicv3_its(
			root_partition, its_params);
		if (its_r.e != OK) {
			panic("Unable to create ITS object");
		}

		error_t err = object_activate_gicv3_its(its_r.r);
		if (err != OK) {
			panic("Failed to activate ITS object");
		}

		// Create a master cap for the gicv3_its
		object_ptr_t	gicv3_its_optr = { .gicv3_its = its_r.r };
		cap_id_result_t cid_r	       = cspace_create_master_cap(
			root_cspace, gicv3_its_optr, OBJECT_TYPE_GICV3_ITS);
		if (cid_r.e != OK) {
			panic("Unable to create cap to gicv3_its");
		}
		QCBOREncode_AddUInt64(qcbor_enc_ctxt, cid_r.r);
	}

	QCBOREncode_CloseArray(qcbor_enc_ctxt);
}
#endif // INTERFACE_ROOTVM

// Ensure that all commands in the command queue are fully processed
// before disabling ITS by clearing the GITS_CTLR.Enabled bit but wait
// until the command queue becomes quiescent.
void
gicv3_its_system_suspend(void)
{
	for (index_t its = 0U; its < PLATFORM_GITS_COUNT; its++) {
		spinlock_acquire(&gits[its].cmd_queue_lock);
		// The last submitted command has a sequence number of
		// `cmd_queue_head - 1`.
		count_t current_submitted_head = gits[its].cmd_queue_head - 1U;
		spinlock_release(&gits[its].cmd_queue_lock);

		// If the queue is not empty, wait for the last submitted
		// command to complete.
		// `gicv3_its_wait` polls the hardware until this specific
		// command is processed, internally updating
		// `gits[its].cmd_queue_cached_tail`.
		if (gicv3_its_wait(its, current_submitted_head) != OK) {
			panic("failed to complete ITS command during suspend");
		}
	}

	// All ITS command queues have been processed and are now empty.
	// It is safe to disable all ITS.
	gicv3_its_disable_all();
}

static void
gicv3_its_system_resume_one(platform_msi_controller_id_t its)
{
	// Ensure that the ITS is disabled and quiescent.
	GITS_CTLR_t ctlr = atomic_load_acquire(&gits[its].regs->ctl.ctlr);
	if (GITS_CTLR_get_Enabled(&ctlr)) {
		// Firmware or suspend path left the ITS enabled!
		panic("gicv3_its: Already enabled");
	}

	// setting command queue pointers to 0
	gits[its].cmd_queue_head	= 0U;
	gits[its].cmd_queue_cached_tail = 0U;
	atomic_store_relaxed(&gits[its].regs->ctl.cwriter,
			     GITS_CWRITER_default());

	// restoring command queue base address
	assert(GITS_CBASER_raw(gits[its].saved_cbaser) != 0U);
	atomic_store_relaxed(&gits[its].regs->ctl.cbaser,
			     gits[its].saved_cbaser);

	// restoring the base addr of device, collection and vPE tables
	GITS_BASER_t *saved_basers = gits[its].saved_basers;
	for (index_t i = 0U; i < util_array_size(gits[its].regs->ctl.baser);
	     i++) {
		atomic_store_relaxed(&gits[its].regs->ctl.baser[i],
				     saved_basers[i]);
	}

	// Enable the ITS and start processing commands
	GITS_CTLR_set_Enabled(&ctlr, true);
	atomic_store_release(&gits[its].regs->ctl.ctlr, ctlr);

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_USE_VLPI) && GICV3_USE_VLPI
	// Wait for the ITS to finish enabling (Quiescent bit clears).
	// Note that Quiescent is UNKNOWN on an enabled ITS before
	// GICv4.1.
	do {
		ctlr = atomic_load_acquire(&gits[its].regs->ctl.ctlr);
	} while (GITS_CTLR_get_Quiescent(&ctlr));
#endif
}

void
gicv3_its_system_resume(void)
{
	for (index_t i = 0U; i < PLATFORM_GITS_COUNT; i++) {
		gicv3_its_system_resume_one(i);
	}
}

#endif // GICV3_HAS_ITS
