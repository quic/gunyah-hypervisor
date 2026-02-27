// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cspace.h>
#include <hyp_aspace.h>
#include <irq.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <platform_smmuv3.h>
#include <qcbor.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <events/smmuv3.h>

#include <asm/barrier.h>
#include <asm/cache.h>
#include <asm/cpu.h>

// Includes generated handlers.
#include "event_handlers.h"
#include "gicv3.h"
#include "smmuv3.h"

#if (PLATFORM_SMMU_V3_TCU_COUNT != 1)
#error "smmuv3: Only support 1 TCU for now"
#endif

#if ((defined(SMMU_V3_PASSTHROUGH) && SMMU_V3_PASSTHROUGH) &&                  \
     (defined(SMMU_V3_GLOBAL_BYPASS) && SMMU_V3_GLOBAL_BYPASS))
#error "smmuv3: Cannot set pass-through and global-bypass at the same time"
#endif

#if ((defined(SMMU_V3_ENABLE) && SMMU_V3_ENABLE) &&                            \
     ((defined(SMMU_V3_PASSTHROUGH) && SMMU_V3_PASSTHROUGH) ||                 \
      (defined(SMMU_V3_GLOBAL_BYPASS) && SMMU_V3_GLOBAL_BYPASS)))
#error "smmuv3: Cannot set pass-through or global-bypass with smmu enable"
#endif

// Support only one SMMUv3 TCU for now
static smmu_v3_instance_t smmu_v3;

#if defined(PLATFORM_SMMUV3_SERIALISE_REGS) && PLATFORM_SMMUV3_SERIALISE_REGS
static inline void
smmuv3_regs_lock(void) ACQUIRE_LOCK(smmu_v3.regs_lock) LOCK_IMPL
{
	spinlock_acquire(&smmu_v3.regs_lock);
}

static inline void
smmuv3_regs_unlock(void) RELEASE_LOCK(smmu_v3.regs_lock) LOCK_IMPL
{
	spinlock_release(&smmu_v3.regs_lock);
}

#define REQUIRE_SMMUV3_REGS_LOCK REQUIRE_SPINLOCK(smmu_v3.regs_lock)
#else
static opaque_lock_t smmuv3_regs_lock_dummy;

static inline void
smmuv3_regs_lock(void) ACQUIRE_LOCK(smmuv3_regs_lock_dummy) LOCK_IMPL
{
	(void)smmuv3_regs_lock_dummy;
}

static inline void
smmuv3_regs_unlock(void) RELEASE_LOCK(smmuv3_regs_lock_dummy) LOCK_IMPL
{
	(void)smmuv3_regs_lock_dummy;
}

#define REQUIRE_SMMUV3_REGS_LOCK REQUIRE_LOCK(smmuv3_regs_lock_dummy)
#endif

void
smmuv3_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	(void)boot_cpu_index;

	// SMMU has at least 2 * 64k pages of core registers
	size_t		    regs_size = sizeof(*smmu_v3.regs);
	virt_range_result_t range     = hyp_aspace_allocate(regs_size);
	if (range.e != OK) {
		panic("smmuv3: Register address allocation failed.");
	}

	pgtable_hyp_start();

	// Map the SMMU TCU pages to HYP
	pgtable_hyp_memtype_t smmuv3_memtype;
#if defined(PLATFORM_SMMUV3_SERIALISE_REGS) && PLATFORM_SMMUV3_SERIALISE_REGS
	smmuv3_memtype = PGTABLE_HYP_MEMTYPE_STRONG;
#else
	smmuv3_memtype = PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE;
#endif

	error_t ret = pgtable_hyp_map(partition_get_private(), range.r.base,
				      regs_size, PLATFORM_SMMU_V3_BASE,
				      smmuv3_memtype, PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("smmuv3: Mapping of register pages failed.");
	}

	pgtable_hyp_commit();

#if defined(PLATFORM_SMMUV3_SERIALISE_REGS) && PLATFORM_SMMUV3_SERIALISE_REGS
	spinlock_init(&smmu_v3.regs_lock);
#endif

	smmu_v3.regs = (smmu_v3_regs_t *)range.r.base;
	smmuv3_regs_lock();

	// Sanity check - verify the HW IDs
	SMMU_V3_IIDR_t iidr =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.iidr);
	assert(SMMU_V3_IIDR_get_Implementer(&iidr) == SMMU_V3_IMPLEMENTER);
#if 0
	assert(SMMU_V3_IIDR_get_ProductID(&iidr) == SMMU_V3_PRODUCT_ID);
#endif

	SMMU_V3_AIDR_t aidr =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.aidr);
	assert(SMMU_V3_AIDR_get_ArchMajorRev(&aidr) == SMMU_V3_MAJOR_REV);
	assert(SMMU_V3_AIDR_get_ArchMinorRev(&aidr) == SMMU_V3_MINOR_REV);
	smmuv3_regs_unlock();

	// Setup the stream ranges map.
	ret = range_tree_init(&smmu_v3.stream_ranges, partition_get_private());
	if (ret != OK) {
		panic("smmuv3: Stream ranges map setup failed");
	}
}

#if defined(MODULE_VM_ROOTVM)
void
smmuv3_handle_rootvm_init(partition_t *root_partition, cspace_t *root_cspace,
			  qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	smmuv3_create_t	    smmu_params = { 0 };
	smmuv3_ptr_result_t smmu_r =
		partition_allocate_smmuv3(root_partition, smmu_params);
	if (smmu_r.e != OK) {
		panic("Unable to create SMMUv3 stub object");
	}
	smmuv3_t *smmu = smmu_r.r;

	error_t err = object_activate_smmuv3(smmu);
	if (err != OK) {
		panic("Failed to activate SMMUv3 object");
	}

	object_ptr_t	smmu_optr = { .smmuv3 = smmu };
	cap_id_result_t cap_r = cspace_create_master_cap(root_cspace, smmu_optr,
							 OBJECT_TYPE_SMMUV3);
	if (cap_r.e != OK) {
		panic("Failed to create SMMUv3 cap");
	}

	// Assume only one SMMUv3 for now.
	smmu->instance = &smmu_v3;
	QCBOREncode_OpenArrayInMap(qcbor_enc_ctxt, "smmuv3_caps");
	QCBOREncode_AddUInt64(qcbor_enc_ctxt, cap_r.r);
	QCBOREncode_CloseArray(qcbor_enc_ctxt);
}
#endif

#if defined(SMMU_V3_ENABLE) && SMMU_V3_ENABLE

static inline void
smmuv3_data_store_sync_fence(void)
{
	// If SMMU_IDR0.COHACC = 0 then a DSB is not sufficient.
	asm_data_outer_shareable_store_sync_fence();
}

static void
smmuv3_disable_stream_event_recording(smmu_v3_stream_id_t stream_id)
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock);

#if defined(SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS) &&                         \
	SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
static error_t
smmuv3_disable_stream(smmu_v3_stream_id_t stream_id)
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock);
#endif

static smmu_v3_stream_table_entry_ptr_result_t
smmuv3_get_ste_slot(smmu_v3_stream_id_t stream_id)
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock);

static void
smmuv3_init_regs(void) REQUIRE_SMMUV3_REGS_LOCK
{
	SMMU_V3_CR0_t  cr0;
	SMMU_V3_CR0_t  cr0ack;
	SMMU_V3_CR1_t  cr1;
	SMMU_V3_CR2_t  cr2;
	SMMU_V3_GBPA_t gbpa;

	cr2 = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr2);
#if defined(SMMU_V3_ENABLE_GLOBAL_BAD_STREAM_REPORTING) &&                     \
	SMMU_V3_ENABLE_GLOBAL_BAD_STREAM_REPORTING
	// Enable C_BAD_STREAMID events to be recorded
	SMMU_V3_CR2_set_RECINVSID(&cr2, true);
#else
	SMMU_V3_CR2_set_RECINVSID(&cr2, false);
#endif

#if defined(ARCH_ARM_FEAT_VHE)
	// SMMU config should match PE for E2H.
	SMMU_V3_CR2_set_E2H(&cr2, true);
#endif

	// For stage 2 VMIDs are always shared between the CPUs and the SMMUs,
	// so we can use DVM if it is available. Currently we rely on this;
	// the pgtable module doesn't support calling back to us to invalidate
	// TLB entries yet.
	SMMU_V3_IDR0_t idr0 =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.idr0);
	if (!SMMU_V3_IDR0_get_BTM(&idr0)) {
		panic("SMMUv3: IDR0.BTM is 0, DVM not supported");
	}
	SMMU_V3_CR2_set_PTM(&cr2, false);

	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr2, cr2);

	cr1 = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr1);
	SMMU_V3_CR1_set_QUEUE_IC(&cr1, SMMU_V3_CR1_CACHEABILITY_WB);
	SMMU_V3_CR1_set_QUEUE_OC(&cr1, SMMU_V3_CR1_CACHEABILITY_WB);
	SMMU_V3_CR1_set_QUEUE_SH(&cr1, SMMU_V3_CR1_SHAREABILITY_OUTER);
	SMMU_V3_CR1_set_TABLE_IC(&cr1, SMMU_V3_CR1_CACHEABILITY_WB);
	SMMU_V3_CR1_set_TABLE_OC(&cr1, SMMU_V3_CR1_CACHEABILITY_WB);
	SMMU_V3_CR1_set_TABLE_SH(&cr1, SMMU_V3_CR1_SHAREABILITY_OUTER);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr1, cr1);

	cr0 = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0);
	// If ATS is supported put it in safe mode.
	SMMU_V3_CR0_set_ATSCHK(&cr0, true);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr0, cr0);
	// Wait for update to complete
	do {
		cr0ack = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0ack);
	} while (!SMMU_V3_CR0_is_equal(cr0, cr0ack));

	gbpa = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gbpa);
	SMMU_V3_GBPA_set_ABORT(&gbpa, true); // Abort incomming transactions
	SMMU_V3_GBPA_set_MTCFG(&gbpa, true); // Use Memattr
	SMMU_V3_GBPA_set_SHCFG(&gbpa, SMMU_V3_GBPA_SHAREABILITY_OUTER);
	SMMU_V3_GBPA_set_MemAttr(&gbpa, 0xf); // Inner/Outer WB Cached
	SMMU_V3_GBPA_set_Update(&gbpa, true); // Check for completion
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.gbpa, gbpa);
	// Wait for update to complete
	do {
		gbpa = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gbpa);
	} while (SMMU_V3_GBPA_get_Update(&gbpa));
}

static void
smmuv3_init_interrupts(void) REQUIRE_SMMUV3_REGS_LOCK
{
	// Disable MSI for event queues
	SMMU_V3_EVENT_IRQ_CFG0_t irq_cfg0 = SMMU_V3_EVENT_IRQ_CFG0_default();
	SMMU_V3_EVENT_IRQ_CFG1_t irq_cfg1 = SMMU_V3_EVENT_IRQ_CFG1_default();
	SMMU_V3_EVENT_IRQ_CFG2_t irq_cfg2 = SMMU_V3_EVENT_IRQ_CFG2_default();

	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.gerror_irq_cgf0,
			     irq_cfg0);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.gerror_irq_cgf1,
			     irq_cfg1);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.gerror_irq_cgf2,
			     irq_cfg2);

	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.eventq_irq_cgf0,
			     irq_cfg0);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.eventq_irq_cgf1,
			     irq_cfg1);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.eventq_irq_cgf2,
			     irq_cfg2);

	// 1 of N setting for physical IRQs
	GICD_IROUTER_t route = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&route, true);

	irq_trigger_result_t irq_trigger_ret;

	if (gicv3_spi_set_route(PLATFORM_SMMU_V3_TCU1_PMU_IRQ, route) != OK) {
		panic("smmuv3: Failed to set TCU1_PMU_IRQ route!");
	}
	irq_trigger_ret = gicv3_irq_set_trigger_shared(
		PLATFORM_SMMU_V3_TCU1_PMU_IRQ,
		PLATFORM_SMMU_V3_TCU1_PMU_IRQ_MODE);
	assert((irq_trigger_ret.e == OK) &&
	       (irq_trigger_ret.r == PLATFORM_SMMU_V3_TCU1_PMU_IRQ_MODE));

	if (gicv3_spi_set_route(PLATFORM_SMMU_V3_TCU1_EVENTQ_IRQ, route) !=
	    OK) {
		panic("smmuv3: Failed to set TCU1_EVENTQ_IRQ route!");
	}
	irq_trigger_ret = gicv3_irq_set_trigger_shared(
		PLATFORM_SMMU_V3_TCU1_EVENTQ_IRQ, IRQ_TRIGGER_EDGE_RISING);
	assert((irq_trigger_ret.e == OK) &&
	       (irq_trigger_ret.r == IRQ_TRIGGER_EDGE_RISING));

	if (gicv3_spi_set_route(PLATFORM_SMMU_V3_TCU1_CMDSYNC_IRQ, route) !=
	    OK) {
		panic("smmuv3: Failed to set TCU1_CMDSYNC_IRQ route!");
	}
	irq_trigger_ret = gicv3_irq_set_trigger_shared(
		PLATFORM_SMMU_V3_TCU1_CMDSYNC_IRQ, IRQ_TRIGGER_EDGE_RISING);
	assert((irq_trigger_ret.e == OK) &&
	       (irq_trigger_ret.r == IRQ_TRIGGER_EDGE_RISING));

	if (gicv3_spi_set_route(PLATFORM_SMMU_V3_TCU1_GLOBAL_IRQ, route) !=
	    OK) {
		panic("smmuv3: Failed to set TCU1_GLOBAL_IRQ route!");
	}
	irq_trigger_ret = gicv3_irq_set_trigger_shared(
		PLATFORM_SMMU_V3_TCU1_GLOBAL_IRQ, IRQ_TRIGGER_EDGE_RISING);
	assert((irq_trigger_ret.e == OK) &&
	       (irq_trigger_ret.r == IRQ_TRIGGER_EDGE_RISING));

	hwirq_ptr_result_t hwirq_ptr_ret;
	hwirq_create_t	   params[] = {
		    {
			    .irq    = PLATFORM_SMMU_V3_TCU1_PMU_IRQ,
			    .action = HWIRQ_ACTION_SMMU_V3_TCU_PMU,
		    },
		    {
			    .irq    = PLATFORM_SMMU_V3_TCU1_EVENTQ_IRQ,
			    .action = HWIRQ_ACTION_SMMU_V3_TCU_EVENTQ,
		    },
		    {
			    .irq    = PLATFORM_SMMU_V3_TCU1_CMDSYNC_IRQ,
			    .action = HWIRQ_ACTION_SMMU_V3_TCU_CMDSYNC,
		    },
		    {
			    .irq    = PLATFORM_SMMU_V3_TCU1_GLOBAL_IRQ,
			    .action = HWIRQ_ACTION_SMMU_V3_TCU_GLOBAL,
		    }
	};

	for (index_t irq = 0; irq < PLATFORM_SMMU_V3_NUM_TCU_IRQS; irq++) {
		hwirq_ptr_ret = partition_allocate_hwirq(
			partition_get_private(), params[irq]);
		if (hwirq_ptr_ret.e != OK) {
			panic("smmuv3: Failed to create SMMU IRQ");
		}
		if (object_activate_hwirq(hwirq_ptr_ret.r) != OK) {
			panic("smmuv3: Failed to activate SMMU IRQ");
		}
		irq_enable_shared(hwirq_ptr_ret.r);
	}

	// Enable IRQs in the HW
	SMMU_V3_IRQ_CTRL_t irq_ctrl;
	SMMU_V3_IRQ_CTRL_t irq_ctrlack;

	irq_ctrl = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.irq_ctrl);

	SMMU_V3_IRQ_CTRL_set_GERROR_IRQEN(&irq_ctrl, true);
	SMMU_V3_IRQ_CTRL_set_EVENTQ_IRQEN(&irq_ctrl, true);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.irq_ctrl, irq_ctrl);

	// Wait for update to complete
	do {
		irq_ctrlack = atomic_load_relaxed(
			&smmu_v3.regs->smmu_page0.irq_ctrlack);
	} while (!SMMU_V3_IRQ_CTRL_is_equal(irq_ctrl, irq_ctrlack));
}

static void
smmu_v3_ste_dump(smmu_v3_stream_id_t stream_id)
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock)
{
	TRACE(DEBUG, INFO, "> ste_dump: stream_id={:#x}", stream_id);

	uint32_t ste_l1slot = stream_id >> smmu_v3.stream_table.split;
	smmu_v3_stream_table_lvl1_entry_t *lvl1_meta =
		&smmu_v3.stream_table.lvl1_entries[ste_l1slot];
	smmu_v3_stream_table_lvl1_desc_t *table_l1_slot =
		&((smmu_v3_stream_table_lvl1_desc_t
			   *)(smmu_v3.stream_table.table))[ste_l1slot];

	TRACE(DEBUG, INFO,
	      "> ste_dump: lvl1_meta span={:#x} l2_vaddr={:#x} l2_phys={:#x} l2_size={:#x}",
	      lvl1_meta->span, lvl1_meta->l2_vaddr, lvl1_meta->l2_phys,
	      lvl1_meta->l2_size);
	TRACE(DEBUG, INFO, "> ste_dump: HW span={:#x} l2_ptr={:#x}",
	      smmu_v3_stream_table_lvl1_desc_get_Span(table_l1_slot),
	      smmu_v3_stream_table_lvl1_desc_get_L2Ptr(table_l1_slot)
		      << SMMU_V3_STREAM_TABLE_LVL1_DESC_L2PTR_SHIFT);

	smmu_v3_stream_table_entry_ptr_result_t ste_r =
		smmuv3_get_ste_slot(stream_id);
	if (ste_r.e == OK) {
		TRACE(DEBUG, INFO,
		      "> ste_dump: HW ptr={:#x} ste[0]={:#x} ste[1]={:#x}",
		      (register_t)ste_r.r, (register_t)ste_r.r->bf[0],
		      (register_t)ste_r.r->bf[1]);
		TRACE(DEBUG, INFO, "> ste_dump: HW CD={:#x}",
		      (register_t)ste_r.r->bf[0]);
	}
}

static void
smmuv3_eventq_report(smmu_v3_event_t *data)
{
	smmu_v3_event_id_t id = smmu_v3_event_base_get_event(&data->base);
	bool		   handled;

	rcu_read_start();

	// Note: StreamID is defined for all standard event types. It may not
	// be defined for implementation-specific events, but we ignore those.
	smmu_v3_stream_id_t stream_id =
		smmu_v3_event_base_get_StreamID(&data->base);
	smmuv3_stream_range_t	  *range;
	range_tree_lookup_result_t range_r =
		range_tree_lookup(&smmu_v3.stream_ranges, stream_id, 1U);
	if (range_r.node != NULL) {
		range = smmuv3_stream_range_container_of_phys_node(
			range_r.node);
		assert((range->viommu != NULL) && (range->smmuv3 != NULL));
	} else {
		range = NULL;
	}

	// If the SID is in a range owned by a VIOMMU, and is not known to be a
	// S2 fault, then give the VIOMMU a chance to handle it directly.
	if ((range != NULL) && !smmu_v3_event_base_get_S2(&data->base) &&
	    trigger_smmuv3_stage1_event_event(range->viommu->type, range,
					      stream_id, data)) {
		handled = true;
		goto out;
	}

	// Hyp handles Stage2.
	//  - Log stage 2 event.
	//  - Disable interrupt/reporting for it. (Mark it invalid with Valid
	//    Reporting disabled?)
	//    Set STE Config to 0x0 -> Report abort to device, no event
	//    recorded.
	//  - Continue.
	//  - Optionally for debug build, can have panic or Stall so drivers are
	//    easier to debug.
	//    STE - [185] S2S Stage 2 fault behavior - Stall

	// Leave the entries valid, just disable Recording S2R
	// Do for
	// • F_TRANSLATION.
	// • F_ACCESS.
	// • F_ADDR_SIZE.
	// • F_PERMISSION

	switch (id) {
	case SMMU_V3_EVENT_ID_C_BAD_STREAMID: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report C_BAD_STREAMID");
		smmu_v3_event_c_bad_streamid_t *entry = &data->c_bad_streamid;

		TRACE(DEBUG, INFO,
		      "smmuv3: C_BAD_STREAMID ssv={:d} ssid={:#x} sid={:#x}",
		      smmu_v3_event_c_bad_streamid_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_c_bad_streamid_get_SubstreamID(entry),
		      stream_id);

#if defined(SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS) &&                         \
	SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
		spinlock_acquire(&smmu_v3.stream_table_lock);
		error_t err = smmuv3_disable_stream(stream_id);
		spinlock_release(&smmu_v3.stream_table_lock);
		if (err != OK) {
			panic("smmuv3: Failed to disable Stream");
		} else {
			TRACE(DEBUG, INFO,
			      "smmuv3: Disabled Stream ssv={:d} ssid={:#x} sid={:#x}",
			      smmu_v3_event_c_bad_streamid_get_SSV(entry) ? 1U
									  : 0U,
			      smmu_v3_event_c_bad_streamid_get_SubstreamID(
				      entry),
			      stream_id);
			handled = true;
		}
#else
		handled = false;
#endif // SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
		break;
	}
	case SMMU_V3_EVENT_ID_F_STE_FETCH: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report F_STE_FETCH");
		smmu_v3_event_f_ste_fetch_t *entry = &data->f_ste_fetch;

		TRACE(DEBUG, INFO,
		      "smmuv3: F_STE_FETCH_1 ssv={:d} ssid={:#x} sid={:#x}",
		      smmu_v3_event_f_ste_fetch_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_f_ste_fetch_get_SubstreamID(entry),
		      stream_id);
		TRACE(DEBUG, INFO,
		      "smmuv3: F_STE_FETCH_2 r={:x} gpcf={:d} addr={:#x}",
		      smmu_v3_event_f_ste_fetch_get_Reason(entry),
		      smmu_v3_event_f_ste_fetch_get_GPCF(entry) ? 1U : 0U,
		      smmu_v3_event_f_ste_fetch_get_FetchAddr(entry) << 3);

		handled = false;
		break;
	}
	case SMMU_V3_EVENT_ID_C_BAD_STE: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report C_BAD_STE");
		smmu_v3_event_c_bad_ste_t *entry = &data->c_bad_ste;

		TRACE(DEBUG, INFO,
		      "smmuv3: C_BAD_STE ssv={:d} ssid={:#x} sid={:#x}",
		      smmu_v3_event_c_bad_ste_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_c_bad_ste_get_SubstreamID(entry),
		      stream_id);

		smmu_v3_stream_id_t sid = stream_id;

		spinlock_acquire(&smmu_v3.stream_table_lock);

#if defined(SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS) &&                         \
	SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
		// This could happen if we programmed an actaul BAD STE or if we
		// allocated memory for this STE as part of a PAGE alloc but it
		// has not been initialized yet. If this is a spurious stream
		// with uninitialized STE we want to program the STE to disable
		// this stream, it is not a 'real' error yet.. If the STE has
		// infact been enabled before, then this is a real error, which
		// should not happen.
		smmu_v3_stream_table_entry_ptr_result_t ste_r =
			smmuv3_get_ste_slot(sid);
		if (ste_r.e != OK) {
			// This should not happen.
			panic("smmuv3: C_BAD_STE STE not found");
		} else {
			// If this is just not initialized yet, do it now
			// else panic
			if (!smmu_v3_stream_table_entry_get_Valid(ste_r.r)) {
				error_t err = smmuv3_disable_stream(sid);
				if (err != OK) {
					panic("smmuv3: Failed to disable Stream");
				} else {
					TRACE(DEBUG, INFO,
					      "smmuv3: Disabled Stream sid={:#x}",
					      sid);
					handled = true;
				}
			} else {
				smmu_v3_ste_dump(sid);
				// Note:
				//  we could have a bunch of these events
				//  already in the Queue by the time we try to
				//  mute the first giving a 'false' panic, can
				//  we enable stall? panic("smmuv3: C_BAD_STE
				//  check STE");
				handled = false;
			}
		}
#else
		smmu_v3_ste_dump(sid);
		handled = false;
#endif // SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
		spinlock_release(&smmu_v3.stream_table_lock);
		break;
	}
	case SMMU_V3_EVENT_ID_F_CD_FETCH: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report F_CD_FETCH");
		smmu_v3_event_f_cd_fetch_t *entry = &data->f_cd_fetch;

		TRACE(DEBUG, INFO,
		      "smmuv3: F_CD_FETCH_1 ssv={:d} ssid={:#x} sid={:#x}",
		      smmu_v3_event_f_cd_fetch_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_f_cd_fetch_get_SubstreamID(entry),
		      stream_id);
		TRACE(DEBUG, INFO,
		      "smmuv3: F_CD_FETCH_2 r={:x} gpcf={:d} addr={:#x}",
		      smmu_v3_event_f_cd_fetch_get_Reason(entry),
		      smmu_v3_event_f_cd_fetch_get_GPCF(entry) ? 1U : 0U,
		      smmu_v3_event_f_cd_fetch_get_FetchAddr(entry) << 3);

		handled = false;
		break;
	}
	case SMMU_V3_EVENT_ID_C_BAD_CD: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report C_BAD_CD");
		smmu_v3_event_c_bad_cd_t *entry = &data->c_bad_cd;

		TRACE(DEBUG, INFO,
		      "smmuv3: C_BAD_CD ssv={:d} ssid={:#x} sid={:#x}",
		      smmu_v3_event_c_bad_cd_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_c_bad_cd_get_SubstreamID(entry), stream_id);

		handled = false;
		break;
	}
	case SMMU_V3_EVENT_ID_F_WALK_EABT: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report F_WALK_EABT");
		smmu_v3_event_f_walk_eabt_t *entry = &data->f_walk_eabt;

		TRACE(DEBUG, INFO,
		      "smmuv3: F_WALK_EABT_1 ssv={:d} ssid={:#x} sid={:#x} s2={:d} class=0x{:x}",
		      smmu_v3_event_f_walk_eabt_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_f_walk_eabt_get_SubstreamID(entry),
		      stream_id,
		      smmu_v3_event_f_walk_eabt_get_S2(entry) ? 1U : 0U,
		      smmu_v3_event_f_walk_eabt_get_CLASS(entry));
		TRACE(DEBUG, INFO,
		      "smmuv3: F_WALK_EABT_2 r={:x} gpcf={:d} iaddr={:#x} faddr={:#x}",
		      smmu_v3_event_f_walk_eabt_get_Reason(entry),
		      smmu_v3_event_f_walk_eabt_get_GPCF(entry) ? 1U : 0U,
		      smmu_v3_event_f_walk_eabt_get_InputAddr(entry),
		      smmu_v3_event_f_walk_eabt_get_FetchAddr(entry) << 3);

		handled = false;
		break;
	}
	case SMMU_V3_EVENT_ID_F_TRANSLATION: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report F_TRANSLATION");
		smmu_v3_event_f_translation_t *entry = &data->f_translation;

		TRACE(DEBUG, INFO,
		      "smmuv3: F_TRANSLATION_1 ssv={:d} ssid={:#x} sid={:#x} s2={:d} class=0x{:x}",
		      smmu_v3_event_f_translation_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_f_translation_get_SubstreamID(entry),
		      stream_id,
		      smmu_v3_event_f_translation_get_S2(entry) ? 1U : 0U,
		      smmu_v3_event_f_translation_get_CLASS(entry));
		TRACE(DEBUG, INFO,
		      "smmuv3: F_TRANSLATION_2 Stall={:d} StallTag={:#x} iaddr={:#x} ipa={:#x}",
		      smmu_v3_event_f_translation_get_Stall(entry) ? 1U : 0U,
		      smmu_v3_event_f_translation_get_StallTag(entry),
		      smmu_v3_event_f_translation_get_InputAddr(entry),
		      smmu_v3_event_f_translation_get_FetchAddr(entry) << 3);

		// Either this is a stage 2 fault, or it is a stage 1 fault that
		// couldn't be handled by the VIOMMU. Either way, we need to
		// disable events for this stream to avoid a flood.
		spinlock_acquire(&smmu_v3.stream_table_lock);
		smmuv3_disable_stream_event_recording(stream_id);
		spinlock_release(&smmu_v3.stream_table_lock);
		handled = true;

#if defined(SMMU_V3_ASSERT_ON_S2_TRANSLATION_FAULT) &&                         \
	SMMU_V3_ASSERT_ON_S2_TRANSLATION_FAULT
		// Crashing here is a debug option.
		assert(!smmu_v3_event_f_translation_get_S2(entry));
#endif

		break;
	}
	case SMMU_V3_EVENT_ID_F_PERMISSION: {
		TRACE(DEBUG, INFO, "smmuv3: eventq_report F_PERMISSION");
		smmu_v3_event_f_permission_t *entry = &data->f_permission;

		TRACE(DEBUG, INFO,
		      "smmuv3: F_PERMISSION_1 ssv={:d} ssid={:#x} sid={:#x} s2={:d} class=0x{:x}",
		      smmu_v3_event_f_permission_get_SSV(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_SubstreamID(entry),
		      stream_id,
		      smmu_v3_event_f_permission_get_S2(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_CLASS(entry));
		TRACE(DEBUG, INFO,
		      "smmuv3: F_PERMISSION_2 Stall={:d} StallTag={:#x} iaddr={:#x} ipa={:#x}",
		      smmu_v3_event_f_permission_get_Stall(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_StallTag(entry),
		      smmu_v3_event_f_permission_get_InputAddr(entry),
		      smmu_v3_event_f_permission_get_FetchAddr(entry) << 3);
		TRACE(DEBUG, INFO,
		      "smmuv3: F_PERMISSION_3 PnU={:d} InD={:d} RnW={:d} TTRnW={:d}",
		      smmu_v3_event_f_permission_get_PnU(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_InD(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_RnW(entry) ? 1U : 0U,
		      smmu_v3_event_f_permission_get_TTRnW(entry) ? 1U : 0U);

		// Either this is a stage 2 fault, or it is a stage 1 fault that
		// couldn't be handled by the VIOMMU. Either way, we need to
		// disable events for this stream to avoid a flood.
		spinlock_acquire(&smmu_v3.stream_table_lock);
		smmuv3_disable_stream_event_recording(stream_id);
		spinlock_release(&smmu_v3.stream_table_lock);
		handled = true;

		break;
	}

	case SMMU_V3_EVENT_ID_F_UUT:
	case SMMU_V3_EVENT_ID_F_BAD_ATS_TREQ:
	case SMMU_V3_EVENT_ID_F_STREAM_DISABLED:
	case SMMU_V3_EVENT_ID_F_TRANSL_FORBIDDEN:
	case SMMU_V3_EVENT_ID_C_BAD_SUBSTREAMID:
	case SMMU_V3_EVENT_ID_F_ADDR_SIZE:
	case SMMU_V3_EVENT_ID_F_ACCESS:
	case SMMU_V3_EVENT_ID_F_TLB_CONFLICT:
	case SMMU_V3_EVENT_ID_F_CFG_CONFLICT:
	case SMMU_V3_EVENT_ID_E_PAGE_REQUEST:
	case SMMU_V3_EVENT_ID_F_VMS_FETCH:
	default:
		handled = false;
		break;
	}

out:
	if (!handled) {
		TRACE(ERROR, WARN, "smmuv3: eventq_report unhandled event");
		for (index_t i = 0U; i < util_array_size(data->base.bf); i++) {
			TRACE(ERROR, WARN, "    event[{:d}] = {:#x}", i,
			      data->base.bf[i]);
		}
	}
	rcu_read_finish();
}

static void
smmuv3_process_eventq(void)
{
	spinlock_acquire(&smmu_v3.event_queue.lock);

	// NOTE:
	//   The Event queue is full if it can be observed that
	//   SMMU_(*_)EVENTQ_PROD.WR == SMMU_(*_)EVENTQ_CONS.RD and WRAP bits
	//   differ.

	smmuv3_regs_lock();
	SMMU_V3_EVENTQ_PROD_t prod =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page1.eventq_prod);
	SMMU_V3_EVENTQ_CONS_t cons =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page1.eventq_cons);
	smmuv3_regs_unlock();

	uint32_t mask = (uint32_t)util_mask(smmu_v3.event_queue.log2len + 1U);
	smmu_v3.event_queue.head = SMMU_V3_EVENTQ_PROD_get_WR(&prod) & mask;

	bool overflow = smmu_v3.event_queue.overflow !=
			SMMU_V3_EVENTQ_PROD_get_OVFLG(&prod);
	if (overflow) {
		// Toggle our flag to match and ack
		smmu_v3.event_queue.overflow =
			SMMU_V3_EVENTQ_PROD_get_OVFLG(&prod);
		SMMU_V3_EVENTQ_CONS_set_OVACKFLG(&cons,
						 smmu_v3.event_queue.overflow);
		// Gets written back to the reg when we ack an event
	}

	// Use HW head and our SW tail for calculations.
	do {
		smmu_v3_event_t *event =
			&(*smmu_v3.event_queue.queue)[smmu_v3.event_queue.tail %
						      smmu_v3.event_queue.len];
		smmuv3_eventq_report(event);

		// Inc our index and update CONS, Let it set the top bit as the
		// wrap bit
		smmu_v3.event_queue.tail = (smmu_v3.event_queue.tail + 1U) &
					   mask;

		// Todo: refactor these into q_inc and q_empty helpers?

		// Acknowledge the events we processed to free up slots
		SMMU_V3_EVENTQ_CONS_set_RD(&cons, smmu_v3.event_queue.tail);
		smmuv3_regs_lock();
		atomic_store_relaxed(&smmu_v3.regs->smmu_page1.eventq_cons,
				     cons);
		smmuv3_regs_unlock();
	} while (smmu_v3.event_queue.head != smmu_v3.event_queue.tail);

	spinlock_release(&smmu_v3.event_queue.lock);
}

static void
smmuv3_process_gerror(void) REQUIRE_PREEMPT_DISABLED
{
	// Event is active if gerror and gerrorn differs.
	// Acknowledge by setting gerrorn the same.
	smmuv3_regs_lock();
	SMMU_V3_GERROR_t gerror =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gerror);
	SMMU_V3_GERROR_t gerrorn =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gerrorn);
	SMMU_V3_GERROR_t active = SMMU_V3_GERROR_difference(gerror, gerrorn);
	smmuv3_regs_unlock();

	// We could have multiple errors at once.
	if (SMMU_V3_GERROR_get_CMDQ_ERR(&active)) {
		SMMU_V3_GERROR_set_CMDQ_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - CMDQ_ERR");
	}
	if (SMMU_V3_GERROR_get_EVENTQ_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_EVENTQ_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - EVENTQ_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_PRIQ_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_PRIQ_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - PRIQ_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_MSI_CMDQ_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_MSI_CMDQ_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - MSI_CMDQ_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_MSI_EVENTQ_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_MSI_EVENTQ_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - MSI_EVENTQ_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_MSI_PRIQ_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_MSI_PRIQ_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - MSI_PRIQ_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_MSI_GERROR_ABT_ERR(&active)) {
		SMMU_V3_GERROR_set_MSI_GERROR_ABT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - MSI_GERROR_ABT_ERR");
	}
	if (SMMU_V3_GERROR_get_SFM_ERR(&active)) {
		SMMU_V3_GERROR_set_SFM_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - SFM_ERR");
	}
	if (SMMU_V3_GERROR_get_CMDQP_ERR(&active)) {
		SMMU_V3_GERROR_set_CMDQP_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - CMDQP_ERR");
	}
	if (SMMU_V3_GERROR_get_DPT_ERR(&active)) {
		SMMU_V3_GERROR_set_DPT_ERR(&active, false);
		TRACE(DEBUG, INFO, "smmuv3: GERROR - DPT_ERR");
	}
	if (!SMMU_V3_GERROR_is_clean(active)) {
		TRACE(DEBUG, INFO, "smmuv3: Unknown global error {:#x}",
		      SMMU_V3_GERROR_raw(active));
	}
}

bool
smmuv3_handle_irq_received_tcu_cmdsync(void)
{
	TRACE(DEBUG, INFO, "smmuv3: HWIRQ_ACTION_SMMU_V3_TCU_CMDSYNC");

	return true;
}

bool NOINLINE
smmuv3_handle_irq_received_tcu_eventq(void)
{
	TRACE(DEBUG, INFO, "smmuv3: HWIRQ_ACTION_SMMU_V3_TCU_EVENTQ");
	smmuv3_process_eventq();

	return true;
}

void
smmuv3_handle_irq_received_tcu_global(void)
{
	TRACE(DEBUG, INFO, "smmuv3: HWIRQ_ACTION_SMMU_V3_TCU_GLOBAL");
	smmuv3_process_gerror();
	// For the moment we do not attempt to resolve these, just panic
	panic("smmuv3: Unresolved global error");
}

bool
smmuv3_handle_irq_received_tcu_pmu(void)
{
	TRACE(DEBUG, INFO, "smmuv3: HWIRQ_ACTION_SMMU_V3_TCU_PMU");

	return true;
}

static void
smmuv3_init_queues(void) REQUIRE_SMMUV3_REGS_LOCK
{
	// Allocate memory for the queues
	// For the moment we only support one CMDQ and one EventQ
	// Note: The effective base address is aligned by the SMMU to the larger
	// of the queue size in bytes or 32 bytes.
	size_t		  cmdq_sz = sizeof(smmu_v3_cmd_t) * SMMU_V3_CMDQ_LEN;
	void_ptr_result_t cmd_ptr_r =
		partition_alloc(partition_get_private(), cmdq_sz,
				util_max(SMMU_V3_QUEUE_MIN_ALIGN, cmdq_sz));
	assert(cmd_ptr_r.e == OK);

	size_t evtq_sz = sizeof(smmu_v3_event_t) * SMMU_V3_EVENTQ_LEN;
	void_ptr_result_t evt_ptr_r =
		partition_alloc(partition_get_private(), evtq_sz,
				util_max(SMMU_V3_QUEUE_MIN_ALIGN, evtq_sz));
	assert(evt_ptr_r.e == OK);

	(void)memset_s(cmd_ptr_r.r, cmdq_sz, 0, cmdq_sz);
	(void)memset_s(evt_ptr_r.r, evtq_sz, 0, evtq_sz);

	// Set up the command queue
	smmu_v3.cmd_queue.queue =
		(smmu_v3_cmd_t(*)[SMMU_V3_CMDQ_LEN])cmd_ptr_r.r;
	spinlock_init(&smmu_v3.cmd_queue.lock);
	smmu_v3.cmd_queue.phys = partition_virt_to_phys(
		partition_get_private(), (uintptr_t)smmu_v3.cmd_queue.queue);
	smmu_v3.cmd_queue.head	  = 0U;
	smmu_v3.cmd_queue.tail	  = 0U;
	smmu_v3.cmd_queue.len	  = SMMU_V3_CMDQ_LEN;
	smmu_v3.cmd_queue.log2len = SMMU_V3_QUEUE_SHIFT;

	// Set up the event queue
	smmu_v3.event_queue.queue =
		(smmu_v3_event_t(*)[SMMU_V3_EVENTQ_LEN])evt_ptr_r.r;
	spinlock_init(&smmu_v3.event_queue.lock);
	smmu_v3.event_queue.phys = partition_virt_to_phys(
		partition_get_private(), (uintptr_t)smmu_v3.event_queue.queue);
	smmu_v3.event_queue.head     = 0U;
	smmu_v3.event_queue.tail     = 0U;
	smmu_v3.event_queue.len	     = SMMU_V3_EVENTQ_LEN;
	smmu_v3.event_queue.log2len  = SMMU_V3_QUEUE_SHIFT;
	smmu_v3.event_queue.overflow = false;

	// Command Queue
	SMMU_V3_CMDQ_BASE_t cmdq_base = SMMU_V3_CMDQ_BASE_default();
	SMMU_V3_CMDQ_BASE_set_LOG2SIZE(&cmdq_base, SMMU_V3_QUEUE_SHIFT);
	SMMU_V3_CMDQ_BASE_set_ADDR(&cmdq_base,
				   smmu_v3.cmd_queue.phys >>
					   SMMU_V3_CMDQ_BASE_ADDR_SHIFT);
	SMMU_V3_CMDQ_BASE_set_RA(&cmdq_base, true);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cmdq_base, cmdq_base);

	SMMU_V3_CMDQ_PROD_t cprod = SMMU_V3_CMDQ_PROD_default();
	SMMU_V3_CMDQ_PROD_set_WR(&cprod, 0U);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cmdq_prod, cprod);

	SMMU_V3_CMDQ_CONS_t ccons = SMMU_V3_CMDQ_CONS_default();
	SMMU_V3_CMDQ_CONS_set_RD(&ccons, 0U);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cmdq_cons, ccons);

	// Event Queue
	SMMU_V3_EVENTQ_BASE_t eventq_base = SMMU_V3_EVENTQ_BASE_default();
	SMMU_V3_EVENTQ_BASE_set_LOG2SIZE(&eventq_base, SMMU_V3_QUEUE_SHIFT);
	SMMU_V3_EVENTQ_BASE_set_ADDR(&eventq_base,
				     smmu_v3.event_queue.phys >>
					     SMMU_V3_EVENTQ_BASE_ADDR_SHIFT);
	SMMU_V3_EVENTQ_BASE_set_WA(&eventq_base, true);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.eventq_base,
			     eventq_base);

	SMMU_V3_EVENTQ_PROD_t eprod = SMMU_V3_EVENTQ_PROD_default();
	SMMU_V3_EVENTQ_PROD_set_WR(&eprod, 0U);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page1.eventq_prod, eprod);

	SMMU_V3_EVENTQ_CONS_t econs = SMMU_V3_EVENTQ_CONS_default();
	SMMU_V3_EVENTQ_CONS_set_RD(&econs, 0U);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page1.eventq_cons, econs);

	// Enable the Queues
	SMMU_V3_CR0_t cr0;
	SMMU_V3_CR0_t cr0ack;

	cr0 = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0);
	SMMU_V3_CR0_set_CMDQEN(&cr0, true);
	SMMU_V3_CR0_set_EVENTQEN(&cr0, true);

	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr0, cr0);

	// Wait for update to complete
	do {
		cr0ack = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0ack);
	} while (!SMMU_V3_CR0_is_equal(cr0, cr0ack));
}

static void
smmuv3_init_stream_table(void) REQUIRE_SMMUV3_REGS_LOCK
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock)
{
	// Use 2lvl with a platform specfic SPLIT. Platform device layout will
	// determine the optimal split. For PCI the RequesterID/StreamID is
	// 16bit PCI BDF Bits: [15:8] Bus, [7:3] Device, [2:0] Function number.
	// Like:
	// SIDSIZE | SPLIT | L1 table size | L2 table size
	//      16 |     6 |           8KB |           4KB
	//      16 |     8 |           2KB |          16KB
	//      16 |    10 |          512B |          64KB
	//
	// Split of 8 should get a PCI bus its own L1 desc.
	// Depending on how devices/SIDs are grouped, the L2 table SPAN can be
	// grown on demand.

	uint8_t log2_size      = 0;
	size_t	l1_num_entries = 0;
	size_t	l1_size	       = 0;

	// Find the minimum LOG2SIZE that will contain all Stream entries
	while (util_bit(log2_size) <= PLATFORM_SMMU_V3_HIGHEST_SID) {
		log2_size++;
	}

	static_assert((PLATFORM_SMMU_V3_SPLIT >=
		       SMMU_V3_STREAM_TABLE_SPLIT_MIN) &&
			      (PLATFORM_SMMU_V3_SPLIT <=
			       SMMU_V3_STREAM_TABLE_SPLIT_MAX),
		      "smmuv3: Stream table split out of range");

	// 6 bit split supports 64 entries per L2
	// (Max entries / 64 per L2) * 8 bytes per L1 desc
	// (4096/64) * 8 = 512 Bytes to cover our devices
	l1_num_entries = util_bit(log2_size) / util_bit(PLATFORM_SMMU_V3_SPLIT);
	l1_size = l1_num_entries * sizeof(smmu_v3_stream_table_lvl1_desc_t);

	SMMU_V3_STRTAB_BASE_CFG_t cfg = SMMU_V3_STRTAB_BASE_CFG_default();
	SMMU_V3_STRTAB_BASE_CFG_set_FMT(&cfg,
					SMMU_V3_STRTAB_BASE_CFG_FMT_TWOLEVEL);
	SMMU_V3_STRTAB_BASE_CFG_set_SPLIT(&cfg, PLATFORM_SMMU_V3_SPLIT);
	SMMU_V3_STRTAB_BASE_CFG_set_LOG2SIZE(&cfg, log2_size);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.strtab_base_cfg, cfg);

	// TODO:
	// Refactor this function to deal with failure of these allocs.
	// Though we will probably panic anyway if we cannot do this setup

	// Alloc the stream table.
	// Aligned to the larger of 64 bytes or the first-level table size
	size_t align = util_max(SMMU_V3_STRTAB_MIN_ALIGN, l1_size);

	// This is the memory installed for the device to use as lvl1
	void_ptr_result_t ptr_r =
		partition_alloc(partition_get_private(), l1_size, align);
	if (ptr_r.e != OK) {
		panic("smmuv3: Unable to allocate Stream table");
	}
	// Zero out the memory to mark entries invalid
	(void)memset_s(ptr_r.r, l1_size, 0, l1_size);

	smmu_v3.stream_table.split    = PLATFORM_SMMU_V3_SPLIT;
	smmu_v3.stream_table.log2size = log2_size;
	assert(l1_size <= UINT32_MAX);
	smmu_v3.stream_table.l1size = (uint32_t)l1_size;
	smmu_v3.stream_table.table  = (vmaddr_t)ptr_r.r;
	smmu_v3.stream_table.phys   = partition_virt_to_phys(
		  partition_get_private(), (uintptr_t)smmu_v3.stream_table.table);

	SMMU_V3_STRTAB_BASE_t base = SMMU_V3_STRTAB_BASE_default();
	SMMU_V3_STRTAB_BASE_set_RA(&base, true);
	SMMU_V3_STRTAB_BASE_set_ADDR(&base,
				     smmu_v3.stream_table.phys >>
					     SMMU_V3_STRTAB_BASE_ADDR_SHIFT);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.strtab_base, base);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	// Allocate for our lvl1 meta data
	// This will let us use a SID to index into the Lvl1 table
	// to find and update STEs.
	// We can then see if an STE has been allocated and where it is.
	size_t lvl1_entries_sz =
		l1_num_entries * sizeof(smmu_v3_stream_table_lvl1_entry_t);

	ptr_r = partition_alloc(partition_get_private(), lvl1_entries_sz,
				alignof(register_t));
	if (ptr_r.e != OK) {
		panic("smmuv3: Unable to allocate lvl1_desc");
	}
	(void)memset_s(ptr_r.r, lvl1_entries_sz, 0, lvl1_entries_sz);

	smmu_v3.stream_table.lvl1_entries = ptr_r.r;
}

static void
smmuv3_get_cmd_queue_vals(count_t log2len, count_t pos, uint32_t *value,
			  uint32_t *wrap)
{
	uint32_t val_mask  = (uint32_t)util_mask(log2len);
	uint32_t wrap_mask = (uint32_t)util_bit(log2len);

	*value = pos & val_mask;
	*wrap  = pos & wrap_mask;
}

static void
smmuv3_enqueue_cmd_wait(smmu_v3_instance_t *smmu, count_t head)
{
	// Keep polling until we are sure the tail pointer is at
	// or passed our expected head pointer.
	// - If the wrap bits are the same and the tail position
	//   is less than the head, keep polling.
	// - If the wrap bits are different and tail position is
	//   greater than head, keep polling.

	// There is a risk here that the SMMU could complete many operations
	// without the vCPU observing them and the vCPU thinking it should still
	// wait. That would need more than CMD_Q_LEN commands to be completed
	// and missed, so quite unlikely given that the vCPU should be much
	// faster than the SMMU.

	// TODO:
	// A more robust solution may be to use the MSI interrupt facility. The
	// SMMU could indicate completion to the vCPU by writing to a vCPU
	// specific mailbox. It does not need to be a MSI, it can be some shared
	// memory address.

	uint32_t wr, wr_wrap;
	uint32_t rd, rd_wrap;

	// The values we are waiting for.
	smmuv3_get_cmd_queue_vals(smmu->cmd_queue.log2len, head, &wr, &wr_wrap);

	do {
		smmuv3_regs_lock();
		// Stop waiting if we detect an error.
		SMMU_V3_GERROR_t gerror =
			atomic_load_relaxed(&smmu->regs->smmu_page0.gerror);
		SMMU_V3_GERROR_t gerrorn =
			atomic_load_relaxed(&smmu->regs->smmu_page0.gerrorn);
		SMMU_V3_CMDQ_CONS_t cons =
			atomic_load_relaxed(&smmu->regs->smmu_page0.cmdq_cons);
		smmuv3_regs_unlock();

		SMMU_V3_GERROR_t active =
			SMMU_V3_GERROR_difference(gerror, gerrorn);
		if (SMMU_V3_GERROR_get_CMDQ_ERR(&active)) {
			TRACE_AND_LOG(DEBUG, INFO,
				      "smmuv3: poll CONS RD={:#x} ERR={:#x}",
				      SMMU_V3_CMDQ_CONS_get_RD(&cons),
				      SMMU_V3_CMDQ_CONS_get_ERR(&cons));
			// Deal with this in a better way.
			// We should attempt to resolve CMDQ errors.
			// For now we panic.
			panic("smmuv3: smmuv3_enqueue_cmd poll CMDQ_ERR");
		}

		// Mask out the position + wrap indicator
		uint32_t mask =
			(uint32_t)util_mask(smmu->cmd_queue.log2len + 1U);
		count_t tail = SMMU_V3_CMDQ_CONS_get_RD(&cons) & mask;

		smmuv3_get_cmd_queue_vals(smmu->cmd_queue.log2len, tail, &rd,
					  &rd_wrap);
	} while (((rd_wrap == wr_wrap) && (rd < wr)) ||
		 ((rd_wrap != wr_wrap) && (rd >= wr)));
}

static void
smmuv3_enqueue_cmd(smmu_v3_cmd_t cmd, bool wait)
{
	// CMDs from different VCPUs may get interleaved with others. This
	// should be fine as long as the threads still block when they need to
	// sync. Only Hyp is writing to the CMDQ, so we expect CMDs to be
	// correct, if the CMDQ returns an error we treat it as fatal.

	spinlock_acquire(&smmu_v3.cmd_queue.lock);

	// Check if we have an active CMDQ error
	smmuv3_regs_lock();
	SMMU_V3_GERROR_t gerror =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gerror);
	SMMU_V3_GERROR_t gerrorn =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gerrorn);
	SMMU_V3_GERROR_t active = SMMU_V3_GERROR_difference(gerror, gerrorn);
	if (SMMU_V3_GERROR_get_CMDQ_ERR(&active)) {
		SMMU_V3_CMDQ_CONS_t cons = atomic_load_relaxed(
			&smmu_v3.regs->smmu_page0.cmdq_cons);
		TRACE_AND_LOG(DEBUG, INFO, "smmuv3: CONS RD={:#x} ERR={:#x}",
			      SMMU_V3_CMDQ_CONS_get_RD(&cons),
			      SMMU_V3_CMDQ_CONS_get_ERR(&cons));
		// Deal with this in a better way.
		// We should attempt to resolve CMDQ errors.
		// For now we panic.
		panic("smmuv3: smmuv3_enqueue_cmd CMDQ_ERR");
	}

	SMMU_V3_CMDQ_PROD_t prod =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cmdq_prod);
	SMMU_V3_CMDQ_CONS_t cons =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cmdq_cons);

	// TODO:
	// The indexes can be max of 19bits, but HW can reduce this, sanity
	// check this. The top bit is used as the wrap bit, so keep that, mask
	// everything else out
	uint32_t mask = (uint32_t)util_mask(smmu_v3.cmd_queue.log2len + 1U);
	smmu_v3.cmd_queue.tail = SMMU_V3_CMDQ_CONS_get_RD(&cons) & mask;

	// Check if there is space in the Queue
	// For now we expect there to be space. We can wait or get a signal when
	// not full. Use our cached head and the HW tail to calculate.
	// PROD.WR == CONS.RD and PROD.WR_WRAP != CONS.RD_WRAP, indicates a
	// full queue.
	uint32_t wr, wr_wrap;
	uint32_t rd, rd_wrap;
	smmuv3_get_cmd_queue_vals(smmu_v3.cmd_queue.log2len,
				  smmu_v3.cmd_queue.head, &wr, &wr_wrap);
	smmuv3_get_cmd_queue_vals(smmu_v3.cmd_queue.log2len,
				  smmu_v3.cmd_queue.tail, &rd, &rd_wrap);
	if ((wr == rd) && (wr_wrap != rd_wrap)) {
		// We should poll with timeout here for new slots.
		// For now we panic after a large number of tries so we dont get
		// stuck
		uint32_t tries = 0;
		do {
			// Cant test this case on the FVP model, looks like SMMU
			// Q never gets full, drains fast
			tries++;
			cons = atomic_load_relaxed(
				&smmu_v3.regs->smmu_page0.cmdq_cons);
			smmu_v3.cmd_queue.tail =
				SMMU_V3_CMDQ_CONS_get_RD(&cons) & mask;
			smmuv3_get_cmd_queue_vals(smmu_v3.cmd_queue.log2len,
						  smmu_v3.cmd_queue.tail, &rd,
						  &rd_wrap);
		} while ((wr == rd) && (wr_wrap != rd_wrap) && (tries < 1000U));

		if (tries >= 1000U) {
			panic("smmuv3: smmuv3_enqueue_cmd CMDQ FULL");
		}
	}

	smmu_v3_cmd_t *cmd_slot =
		&((*smmu_v3.cmd_queue.queue)[smmu_v3.cmd_queue.head %
					     smmu_v3.cmd_queue.len]);

	*cmd_slot = cmd;

	// Make the CMD visible to the SMMU
	CACHE_CLEAN_OBJECT(*cmd_slot);

	// Inc our index and update PROD, Let it set the top bit as the wrap bit
	smmu_v3.cmd_queue.head = (smmu_v3.cmd_queue.head + 1U) & mask;

	// Submit the CMD;
	SMMU_V3_CMDQ_PROD_set_WR(&prod, smmu_v3.cmd_queue.head);
	atomic_store_release(&smmu_v3.regs->smmu_page0.cmdq_prod, prod);

	smmuv3_regs_unlock();
	spinlock_release(&smmu_v3.cmd_queue.lock);

	// If requested, poll until complete
	// Do a busy wait outside of the SMMUv3 critial section.
	if (wait) {
		smmuv3_enqueue_cmd_wait(&smmu_v3, smmu_v3.cmd_queue.head);
	}
}

static void
smmuv3_sync_internal(bool raise_interrupt, bool wait)
{
	// Wait for cmds/invalidations to complete.
	// Sync can send an interrupt on completion
	smmu_v3_cmd_t cmd = { .sync = smmu_v3_cmd_sync_default() };

	// This interrupt does not always fire on the FVP model. Looks like a
	// model bug. It does work sometimes, so the triggers seem ok.
	if (raise_interrupt) {
		smmu_v3_cmd_sync_set_CS(&cmd.sync, SMMU_V3_CMD_SYNC_CS_SIG_IRQ);
	}

	smmuv3_enqueue_cmd(cmd, wait);
}

void
smmuv3_sync(smmuv3_t *smmu, bool raise_interrupt, bool wait)
{
	assert(smmu->instance == &smmu_v3);
	smmuv3_sync_internal(raise_interrupt, wait);
}

void
smmuv3_cfgi_cd_all(smmuv3_stream_range_t *range, smmu_v3_stream_id_t stream_id)
{
	assert((range != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	if ((stream_id >= range->phys_node.base) &&
	    (stream_id <=
	     (range->phys_node.base + range->phys_node.size - 1U))) {
		// Invalidate all CDs for a given stream
		smmu_v3_cmd_t cmd = {
			.cfgi_cd_all = smmu_v3_cmd_cfgi_cd_all_default()
		};
		smmu_v3_cmd_cfgi_cd_all_set_StreamID(&cmd.cfgi_cd_all,
						     stream_id);
		smmuv3_enqueue_cmd(cmd, false);
	} else {
		TRACE(DEBUG, INFO,
		      "{:s}: stream_id {:#x} outside range {:#x}++{:#x}",
		      (register_t) __func__, stream_id, range->phys_node.size,
		      range->phys_node.base);
	}
}

static void
smmuv3_tlbi_el1_all(void)
{
	// Invalidate all Non‑secure EL1 contexts,
	smmu_v3_cmd_t cmd = { .tlbi_nsnh_all =
				      smmu_v3_cmd_tlbi_nsnh_all_default() };
	smmuv3_enqueue_cmd(cmd, false);
}

void
smmuv3_tlbi_el1_vmid(smmuv3_t *smmu, viommu_t *viommu)
{
	assert(smmu->instance == &smmu_v3);
	assert((viommu != NULL) && (viommu->addrspace != NULL));

	// Invalidate Non‑secure EL1 contexts for the VM
	smmu_v3_cmd_t cmd = { .tlbi_nh_all =
				      smmu_v3_cmd_tlbi_nh_all_default() };
	smmu_v3_cmd_tlbi_nh_all_set_VMID(&cmd.tlbi_nh_all,
					 viommu->addrspace->vmid);
	smmuv3_enqueue_cmd(cmd, false);
}

void
smmuv3_tlbi_el1_asid(smmuv3_t *smmu, viommu_t *viommu, smmu_v3_asid_t asid)
{
	assert(smmu->instance == &smmu_v3);
	assert((viommu != NULL) && (viommu->addrspace != NULL));

	// Invalidate Non‑secure EL1 contexts for the ASID
	smmu_v3_cmd_t cmd = { .tlbi_nh_asid =
				      smmu_v3_cmd_tlbi_nh_asid_default() };
	smmu_v3_cmd_tlbi_nh_asid_set_VMID(&cmd.tlbi_nh_asid,
					  viommu->addrspace->vmid);
	smmu_v3_cmd_tlbi_nh_asid_set_ASID(&cmd.tlbi_nh_asid, asid);
	smmuv3_enqueue_cmd(cmd, false);
}

void
smmuv3_tlbi_el1_address_range(smmuv3_t *smmu, viommu_t *viommu, paddr_t address,
			      uint64_t nr_pages, uint8_t page_size, bool leaf)
{
	assert(smmu->instance == &smmu_v3);
	assert((viommu != NULL) && (viommu->addrspace != NULL));

	if (nr_pages == 0U) {
		// Nothing to do, just exit
		goto out;
	}

	// Invalidate Non‑secure EL1 contexts for the ASID address range
	smmu_v3_cmd_t cmd = {
		.tlbi_nh_vaa = smmu_v3_cmd_tlbi_nh_vaa_default(),
	};

	// If Range Based Invalidation is not suported by HW, IDR3.RIL
	// leave num, scale, ttl and tg as RES0.
	// Note: TTL hint is not available from Virtio-iommu interface

	smmu_v3_cmd_tlbi_nh_vaa_set_VMID(&cmd.tlbi_nh_vaa,
					 viommu->addrspace->vmid);
	smmu_v3_cmd_tlbi_nh_vaa_set_Leaf(&cmd.tlbi_nh_vaa, leaf);

	for (uint64_t offset = 0; offset < nr_pages; offset++) {
		paddr_t page = address + (offset << page_size);
		smmu_v3_cmd_tlbi_nh_vaa_set_Address(&cmd.tlbi_nh_vaa, page);

		smmuv3_enqueue_cmd(cmd, false);
	}

out:
	return;
}

void
smmuv3_tlbi_el1_asid_address_range(smmuv3_t *smmu, viommu_t *viommu,
				   smmu_v3_asid_t asid, paddr_t address,
				   uint64_t nr_pages, uint8_t page_size,
				   bool leaf)
{
	assert(smmu->instance == &smmu_v3);
	assert((viommu != NULL) && (viommu->addrspace != NULL));

	if (nr_pages == 0U) {
		// Nothing to do, just exit
		goto out;
	}

	// Invalidate Non‑secure EL1 contexts for the ASID address range
	smmu_v3_cmd_t cmd = { .tlbi_nh_va = smmu_v3_cmd_tlbi_nh_va_default() };

	// If Range Based Invalidation is not suported by HW, IDR3.RIL
	// leave num, scale, ttl and tg as RES0.
	// Note: TTL hint is not available from Virtio-iommu interface

	smmu_v3_cmd_tlbi_nh_va_set_VMID(&cmd.tlbi_nh_va,
					viommu->addrspace->vmid);
	smmu_v3_cmd_tlbi_nh_va_set_ASID(&cmd.tlbi_nh_va, asid);

	smmu_v3_cmd_tlbi_nh_va_set_Leaf(&cmd.tlbi_nh_va, leaf);

	for (uint64_t offset = 0; offset < nr_pages; offset++) {
		paddr_t page = address + (offset << page_size);
		smmu_v3_cmd_tlbi_nh_va_set_Address(&cmd.tlbi_nh_va, page);

		smmuv3_enqueue_cmd(cmd, false);
	}

out:
	return;
}

void
smmu_v3_atc_inv_stream_all(smmuv3_stream_range_t *range,
			   smmu_v3_stream_id_t	  stream_id)
{
	assert((range != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	if ((stream_id >= range->phys_node.base) &&
	    (stream_id <=
	     (range->phys_node.base + range->phys_node.size - 1U))) {
		// Invalidate all the PCI ATS Caches (ATC) for a Stream
		smmu_v3_cmd_t cmd = {
			.atc_inv = smmu_v3_cmd_atc_inv_default(),
		};

		smmu_v3_cmd_atc_inv_set_StreamID(&cmd.atc_inv, stream_id);
		smmu_v3_cmd_atc_inv_set_Size(&cmd.atc_inv,
					     SMMU_V3_CMD_ATC_INV_SPAN_ALL);

		smmuv3_enqueue_cmd(cmd, false);
	} else {
		TRACE(DEBUG, INFO,
		      "{:s}: stream_id {:#x} outside range {:#x}++{:#x}",
		      (register_t) __func__, stream_id, range->phys_node.size,
		      range->phys_node.base);
	}
}

void
smmu_v3_atc_inv_stream_address_range(smmuv3_stream_range_t *range,
				     smmu_v3_stream_id_t    stream_id,
				     paddr_t address, uint8_t sz_bits)
{
	assert((range != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	if ((stream_id >= range->phys_node.base) &&
	    (stream_id <=
	     (range->phys_node.base + range->phys_node.size - 1U))) {
		// Invalidate some of the PCI ATS Caches (ATC) for a Stream
		smmu_v3_cmd_t cmd = {
			.atc_inv = smmu_v3_cmd_atc_inv_default(),
		};

		smmu_v3_cmd_atc_inv_set_StreamID(&cmd.atc_inv, stream_id);
		smmu_v3_cmd_atc_inv_set_Address(&cmd.atc_inv, address);
		smmu_v3_cmd_atc_inv_set_Size(&cmd.atc_inv, sz_bits);

		smmuv3_enqueue_cmd(cmd, false);
	} else {
		TRACE(DEBUG, INFO,
		      "{:s}: stream_id {:#x} outside range {:#x}++{:#x}",
		      (register_t) __func__, stream_id, range->phys_node.size,
		      range->phys_node.base);
	}
}

static void
smmuv3_tlbi_el2_all(void)
{
	smmu_v3_cmd_t cmd = {
		.tlbi_el2_all = smmu_v3_cmd_tlbi_el2_all_default(),
	};
	smmuv3_enqueue_cmd(cmd, false);
}

static void
smmuv3_invalidate_sid_all(void)
{
	smmu_v3_cmd_t cmd = { .cfgi_all = smmu_v3_cmd_cfgi_all_default() };
	smmuv3_enqueue_cmd(cmd, false);
}

static void
smmuv3_invalidate_sid(smmu_v3_stream_id_t stream_id, bool leaf)
{
	smmu_v3_cmd_t cmd = { .cfgi_ste = smmu_v3_cmd_cfgi_ste_default() };

	smmu_v3_cmd_cfgi_ste_set_StreamID(&cmd.cfgi_ste, stream_id);
	smmu_v3_cmd_cfgi_ste_set_Leaf(&cmd.cfgi_ste, leaf);
	smmuv3_enqueue_cmd(cmd, false);
}

static void
smmuv3_prefetch_sid(smmu_v3_stream_id_t stream_id)
{
	smmu_v3_cmd_t cmd = {
		.prefetch_config = smmu_v3_cmd_prefetch_config_default(),
	};

	smmu_v3_cmd_prefetch_config_set_StreamID(&cmd.prefetch_config,
						 stream_id);
	smmuv3_enqueue_cmd(cmd, false);
}

static void
smmuv3_init_cmds(void)
{
	// Invalidate Non‑secure EL1 contexts
	smmuv3_tlbi_el1_all();

	// Invalidate EL2 contexts
	smmuv3_tlbi_el2_all();

	// Invalidate TCU configuration cache and the TBU combined configuration
	// cache and TLB
	smmuv3_invalidate_sid_all();

	// Do TLB invalidations again after CFG incase of prefetch, recomended
	// in ARCH doc Invalidate Non‑secure EL1 contexts
	smmuv3_tlbi_el1_all();
	smmuv3_tlbi_el2_all();

	// Wait for invalidations to complete, no interrupt
	smmuv3_sync_internal(false, true);
}

static void
smmuv3_init(void)
{
	spinlock_init(&smmu_v3.stream_table_lock);

	spinlock_acquire(&smmu_v3.stream_table_lock);
	smmuv3_regs_lock();

	// Some core register configs
	smmuv3_init_regs();

	// Attach Fault IRQ handler?
	smmuv3_init_interrupts();

	// Init Queues
	smmuv3_init_queues();

	// Init Stream Table
	smmuv3_init_stream_table();

	// Invalidations do their own locking.
	smmuv3_regs_unlock();

	// Do initial invalidations etc, wait for SYNC
	smmuv3_init_cmds();

	spinlock_release(&smmu_v3.stream_table_lock);
}

smmuv3_stream_range_ptr_result_t
smmuv3_bind_stream_range(smmuv3_t *smmu, viommu_t *viommu,
			 smmu_v3_stream_id_t base_id, count_t count)
{
	smmuv3_stream_range_ptr_result_t ret;
	error_t				 err;

	if (smmu->instance != &smmu_v3) {
		ret = smmuv3_stream_range_ptr_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	if ((count == 0U) || util_add_overflows(base_id, count - 1U) ||
	    ((base_id + (count - 1U)) > PLATFORM_SMMU_V3_HIGHEST_SID)) {
		ret = smmuv3_stream_range_ptr_result_error(
			ERROR_ARGUMENT_INVALID);
		goto out;
	}

	smmuv3_stream_range_t *range;

	void_ptr_result_t ptr_r = partition_alloc(
		viommu->partition, sizeof(*range), alignof(*range));
	if (ptr_r.e != OK) {
		TRACE(DEBUG, INFO, "smmuv3: Unable to allocate SID range");
		ret = smmuv3_stream_range_ptr_result_error(ptr_r.e);
		goto fail_alloc;
	}

	(void)memset_s(ptr_r.r, sizeof(*range), 0, sizeof(*range));
	range		 = ptr_r.r;
	range->viommu	 = viommu;
	range->smmuv3	 = smmu;
	range->partition = object_get_partition_additional(viommu->partition);

	// Ensure that S2 TLB invalidates are broadcast to the SMMU
	err = pgtable_vm_enable_coherent_iommu(&viommu->addrspace->vm_pgtable);

#if defined(PLATFORM_FVP_RDV1_NO_TLBIOS_SUPPORT) &&                            \
	PLATFORM_FVP_RDV1_NO_TLBIOS_SUPPORT
	// The FVP test platform does not support this TLB maintenance feature.
	// It is OK in this case as it is only used for testing and is not
	// running any Guest VMs at the moment. We could have other platforms
	// with similar limitations, so should address this. A hook should be
	// added to the pgtable code to Queue invalidation CMDs to the SMMUv3
	// CMDQ. The SMMUv3 should first check if the address space needs to do
	// any invalidation. RM, for example, does not use the SMMU and does not
	// need Stage2 invalidates.
	// FIXME: QC Gunyah issue #275
	if (err == ERROR_UNIMPLEMENTED) {
		TRACE_AND_LOG(
			DEBUG, INFO,
			"WARNING: S2 TLB invalidates are NOT broadcast to the SMMU");
		err = OK;
	}
#endif

	if (err != OK) {
		ret = smmuv3_stream_range_ptr_result_error(err);
		goto fail_dvm_enable;
	}

	spinlock_acquire(&smmu_v3.stream_table_lock);
	range_tree_lock_nopreempt(&smmu_v3.stream_ranges);
	err = range_tree_insert(&smmu_v3.stream_ranges, &range->phys_node,
				base_id, count);
	range_tree_unlock_nopreempt(&smmu_v3.stream_ranges);

#if defined(VERBOSE) && VERBOSE
	// All previously unbound streams should be either unallocated, invalid,
	// or disabled; assert that this is the case.
	for (index_t i = 0U; i < count; i++) {
		smmu_v3_stream_id_t stream_id = base_id + i;

		smmu_v3_stream_table_entry_ptr_result_t ste_r =
			smmuv3_get_ste_slot(stream_id);
		if (ste_r.e != OK) {
			continue;
		}

		assert(!smmu_v3_stream_table_entry_get_Valid(ste_r.r) ||
		       (smmu_v3_stream_table_entry_get_Config(ste_r.r) ==
			SMMU_V3_STE_CONFIG_ABORT));
	}
#endif

	spinlock_release(&smmu_v3.stream_table_lock);
	if (err != OK) {
		ret = smmuv3_stream_range_ptr_result_error(err);
		goto fail_range_insert;
	}

	ret = smmuv3_stream_range_ptr_result_ok(range);

fail_range_insert:
	if (ret.e != OK) {
		pgtable_vm_disable_coherent_iommu(
			&viommu->addrspace->vm_pgtable);
	}
fail_dvm_enable:
	if (ret.e != OK) {
		partition_free(viommu->partition, range, sizeof(*range));
		object_put_partition(viommu->partition);
	}
fail_alloc:
out:
	return ret;
}

void
smmuv3_unbind_stream_range(smmuv3_stream_range_t *range)
{
	assert((range != NULL) && (range->viommu != NULL) &&
	       (range->viommu->addrspace != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	spinlock_acquire(&smmu_v3.stream_table_lock);

	bool need_sync = false;

	// Clear the STE valid bits and issue STE invalidate commands.
	for (index_t i = 0U; i < range->phys_node.size; i++) {
		smmu_v3_stream_id_t stream_id =
			(smmu_v3_stream_id_t)range->phys_node.base + i;

		smmu_v3_stream_table_entry_ptr_result_t ste_r =
			smmuv3_get_ste_slot(stream_id);
		if (ste_r.e != OK) {
			continue;
		}

		if (smmu_v3_stream_table_entry_get_Valid(ste_r.r)) {
			need_sync = true;

			// Mark the STE as invalid so the HW will ignore it
			smmu_v3_stream_table_entry_set_Valid(ste_r.r, false);

			// Ensure that written data is observable to the SMMU
			smmuv3_data_store_sync_fence();

			// Ensure SID config is consistent before we update it.
			smmuv3_invalidate_sid(stream_id, true);
		}
	}

	// Wait for any issued STE invalidate commands to complete.
	if (need_sync) {
		smmuv3_sync_internal(false, true);
		need_sync = false;
	}

	// Issue ATC invalidate commands, then zero the STEs entirely.
	for (index_t i = 0U; i < range->phys_node.size; i++) {
		smmu_v3_stream_id_t stream_id =
			(smmu_v3_stream_id_t)range->phys_node.base + i;

		smmu_v3_stream_table_entry_ptr_result_t ste_r =
			smmuv3_get_ste_slot(stream_id);
		if (ste_r.e != OK) {
			continue;
		}

		if (smmu_v3_stream_table_entry_get_EATS(ste_r.r) !=
		    SMMU_V3_STE_EATS_DISABLE) {
			smmu_v3_atc_inv_stream_all(range, stream_id);
			need_sync = true;
		}

		*ste_r.r = smmu_v3_stream_table_entry_default();
	}

	// Wait for any issued ATC invalidate commands to complete.
	if (need_sync) {
		smmuv3_sync_internal(false, true);
	}

	pgtable_vm_disable_coherent_iommu(
		&range->viommu->addrspace->vm_pgtable);

	// Everything is unbound; at this point it is safe to remove the range
	// from the tree so that a new caller can allocate it.
	range_tree_lock_nopreempt(&smmu_v3.stream_ranges);
	(void)range_tree_remove(&smmu_v3.stream_ranges, &range->phys_node);
	range_tree_unlock_nopreempt(&smmu_v3.stream_ranges);

	rcu_enqueue(&range->rcu, RCU_UPDATE_CLASS_SMMUV3_RELEASE_STREAM_RANGE);

	spinlock_release(&smmu_v3.stream_table_lock);
}

rcu_update_status_t
smmuv3_handle_rcu_update(rcu_entry_t *entry)
{
	smmuv3_stream_range_t *range =
		smmuv3_stream_range_container_of_rcu(entry);

	partition_t *partition = range->partition;
	partition_free(partition, range, sizeof(*range));
	object_put_partition(partition);

	return rcu_update_status_default();
}

static smmu_v3_stream_table_entry_ptr_result_t
smmuv3_get_ste_slot(smmu_v3_stream_id_t stream_id)
{
	smmu_v3_stream_table_entry_ptr_result_t ret;

	if (stream_id > PLATFORM_SMMU_V3_HIGHEST_SID) {
		ret = smmu_v3_stream_table_entry_ptr_result_error(
			ERROR_ADDR_INVALID);
		goto out;
	}

	index_t ste_l1slot = stream_id >> smmu_v3.stream_table.split;
	index_t ste_l2slot = stream_id &
			     (index_t)util_mask(smmu_v3.stream_table.split);
	smmu_v3_stream_table_lvl1_entry_t *lvl1_meta =
		&smmu_v3.stream_table.lvl1_entries[ste_l1slot];

	// Check if L2 memory has been alocated for this L1.
	if (lvl1_meta->l2_vaddr == 0U) {
		ret = smmu_v3_stream_table_entry_ptr_result_error(
			ERROR_ADDR_NOTFOUND);
	} else {
		// L2 Array exists.
		// This array grows dynamically, check the SPAN of this L2 array
		assert(((lvl1_meta->span - 1U) >=
			SMMU_V3_STREAM_TABLE_SPLIT_MIN) &&
		       ((lvl1_meta->span - 1U) <=
			SMMU_V3_STREAM_TABLE_SPLIT_MAX));
		if (ste_l2slot >= util_bit(lvl1_meta->span - 1U)) {
			ret = smmu_v3_stream_table_entry_ptr_result_error(
				ERROR_ADDR_NOTFOUND);
		} else {
			ret = smmu_v3_stream_table_entry_ptr_result_ok(
				&((smmu_v3_stream_table_entry_t *)
					  lvl1_meta->l2_vaddr)[ste_l2slot]);
		}
	}

out:
	return ret;
}

static smmu_v3_stream_table_entry_ptr_result_t
smmuv3_alloc_ste_slot(smmu_v3_stream_id_t stream_id)
	REQUIRE_SPINLOCK(smmu_v3.stream_table_lock)
{
	smmu_v3_stream_table_entry_ptr_result_t ret;

	index_t ste_l1slot = stream_id >> smmu_v3.stream_table.split;
	index_t ste_l2slot = stream_id &
			     (index_t)util_mask(smmu_v3.stream_table.split);
	smmu_v3_stream_table_lvl1_entry_t *lvl1_meta =
		&smmu_v3.stream_table.lvl1_entries[ste_l1slot];

	bool new_l2	= false;
	bool replace_l2 = false;

	// Check if we need to alloc new STE array memory for this SID or if
	// we have one ready to update.
	if (lvl1_meta->l2_vaddr == 0U) {
		// New allocation
		new_l2 = true;
	} else {
		// Existing allocation, check if it covers this STE
		// SPAN indicates how many devices are covered by this L2
		assert(((lvl1_meta->span - 1U) >=
			SMMU_V3_STREAM_TABLE_SPLIT_MIN) &&
		       ((lvl1_meta->span - 1U) <=
			SMMU_V3_STREAM_TABLE_SPLIT_MAX));
		if (ste_l2slot >= util_bit(lvl1_meta->span - 1U)) {
			replace_l2 = true;
		}
	}

	if (new_l2 || replace_l2) {
		// Span of 6 is equal to a 4k page. (1 << 6) * 64bytes;
		// Max value allowed here should match the Stream table
		// configuration.
		uint8_t new_span = SMMU_V3_STREAM_TABLE_SPLIT_MIN;
		while (ste_l2slot >= util_bit(new_span)) {
			new_span++;
		}
		assert(new_span <= smmu_v3.stream_table.split);

		size_t ste_lvl2_size = util_bit(new_span) *
				       sizeof(smmu_v3_stream_table_entry_t);

		// This is the memory installed for the device to use as STE at
		// lvl2 The SMMU aligns the lvl2 arrays to its size.
		// We will probably never free these STEs, just mark them as
		// invalid. The structures in here can be updated to point to
		// other VMs as devices move around.
		void_ptr_result_t ptr_r = partition_alloc(
			partition_get_private(), ste_lvl2_size, ste_lvl2_size);
		if (ptr_r.e != OK) {
			TRACE_AND_LOG(DEBUG, INFO,
				      "smmuv3: Unable to allocate STE array");
			ret = smmu_v3_stream_table_entry_ptr_result_error(
				ERROR_NOMEM);
			goto out;
		}

		vmaddr_t old_addr = lvl1_meta->l2_vaddr;
		size_t	 old_sz	  = lvl1_meta->l2_size;

		if (new_l2) {
			// Zero it out to make sure all STEs are 'invalid'.
			(void)memset_s(ptr_r.r, ste_lvl2_size, 0,
				       ste_lvl2_size);
		} else {
			// If replacing the old L2, copy over the old entries
			// and zero out the new.
			(void)memscpy(ptr_r.r, ste_lvl2_size, (void *)old_addr,
				      old_sz);
			(void)memset_s((void *)((vmaddr_t)ptr_r.r + old_sz),
				       ste_lvl2_size - old_sz, 0,
				       ste_lvl2_size - old_sz);
		}

		// Ensure that written data is observable to the SMMU
		smmuv3_data_store_sync_fence();

		// Span: Num STE entries supported as 2^(span-1)
		// The value in the HW needs the '+1', add it here too
		lvl1_meta->span	    = new_span + 1U;
		lvl1_meta->l2_vaddr = (vmaddr_t)ptr_r.r;
		lvl1_meta->l2_phys =
			partition_virt_to_phys(partition_get_private(),
					       (uintptr_t)lvl1_meta->l2_vaddr);
		// Size in bytes of allocated memory
		lvl1_meta->l2_size = (uint32_t)ste_lvl2_size;

		// Update the physical table to use the new STE memory
		// Note the STE is still zero, so not valid and will be ignored
		// by HW
		smmu_v3_stream_table_lvl1_desc_t *table_l1_slot =
			&((smmu_v3_stream_table_lvl1_desc_t
				   *)(smmu_v3.stream_table.table))[ste_l1slot];

		smmu_v3_stream_table_lvl1_desc_t l1_desc =
			smmu_v3_stream_table_lvl1_desc_default();
		smmu_v3_stream_table_lvl1_desc_set_Span(&l1_desc,
							lvl1_meta->span);

		smmu_v3_stream_table_lvl1_desc_set_L2Ptr(
			&l1_desc,
			lvl1_meta->l2_phys >>
				SMMU_V3_STREAM_TABLE_LVL1_DESC_L2PTR_SHIFT);

		// Write the new value, This is 64bit and should be an atomic
		// update.
		*table_l1_slot = l1_desc;

		ret = smmu_v3_stream_table_entry_ptr_result_ok(
			&((smmu_v3_stream_table_entry_t *)
				  lvl1_meta->l2_vaddr)[ste_l2slot]);

		// Ensure that written data is observable to the SMMU
		smmuv3_data_store_sync_fence();

		// Issue a CFGI_STE Leaf=0 after changing the L1STE and
		// sync it before freeing the old L2 to ensure the
		// walker cache is cleaned
		smmuv3_invalidate_sid(stream_id, false);
		smmuv3_sync_internal(false, true);

		if (replace_l2) {
			// Free the old memory;
			partition_free(partition_get_private(),
				       (void *)old_addr, old_sz);
		}
	} else {
		ret = smmu_v3_stream_table_entry_ptr_result_error(
			ERROR_EXISTING_MAPPING);
	}

out:
	return ret;
}

static void
smmuv3_disable_stream_event_recording(smmu_v3_stream_id_t stream_id)
{
	// Disable recording of events for an existing stream configuration.
	// To enable event recording again the stream will need to be attached
	// again.

	smmu_v3_stream_table_entry_ptr_result_t ste_r =
		smmuv3_get_ste_slot(stream_id);
	if (ste_r.e != OK) {
		TRACE(DEBUG, INFO,
		      "smmuv3: disable_stream_event_recording could not find STE");
		goto out;
	}

	// Only disable the event recording, leave everything else as is
	// We may have already disabled this and are just working throught stale
	// entries in the queue
	if (smmu_v3_stream_table_entry_get_S2Record(ste_r.r)) {
		smmu_v3_stream_table_entry_set_S2Record(ste_r.r, false);

		// Ensure that written data is observable to the SMMU
		smmuv3_data_store_sync_fence();

		smmuv3_invalidate_sid(stream_id, true);

		// We dont have to sync() here.
		// The SMMU will pick up the new configuration
	}

out:
	return;
}

#if defined(SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS) &&                         \
	SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS
static error_t
smmuv3_disable_stream(smmu_v3_stream_id_t stream_id)
{
	// We make a STE entry for the Stream and mark it as disabled so we dont
	// get any more events from it. An attach can be used to enable it
	// again. These streams are not part of any domain.

	error_t ret;

	smmu_v3_stream_table_entry_ptr_result_t ste_r =
		smmuv3_get_ste_slot(stream_id);
	if (ste_r.e != OK) {
		ste_r = smmuv3_alloc_ste_slot(stream_id);
		if (ste_r.e != OK) {
			ret = ste_r.e;
			TRACE(DEBUG, INFO,
			      "smmuv3: disable_stream could not allocate STE");
			goto out;
		}
	}

	// Ensure the STE is invalid before we update it.
	smmu_v3_stream_table_entry_set_Valid(ste_r.r, false);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	// Ensure SID config is consistent before we update it.
	smmuv3_invalidate_sid(stream_id, true);
	smmuv3_sync_internal(false, true);

	// Do some initial setup for the STE
	*ste_r.r = smmu_v3_stream_table_entry_default();

	// Mark the stream as disabled and set the Entry as Valid.
	smmu_v3_stream_table_entry_set_Config(ste_r.r,
					      SMMU_V3_STE_CONFIG_ABORT);

	// Mark the STE as valid so the HW can use it
	smmu_v3_stream_table_entry_set_Valid(ste_r.r, true);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	smmuv3_invalidate_sid(stream_id, true);

	// We dont have to sync() here.
	// The SMMU will pick up the new configuration

	ret = OK;

out:
	return ret;
}
#endif // SMMU_V3_DISABLE_INDIVIDUAL_BAD_STREAMS

error_t
smmuv3_attach_stream(smmuv3_stream_range_t	  *range,
		     smmu_v3_stream_id_t	   stream_id,
		     const smmuv3_stream_config_t *s1_cfg, bool prefetch_ste)
{
	error_t ret;

	assert((range != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	if ((stream_id < range->phys_node.base) ||
	    (stream_id >
	     (range->phys_node.base + range->phys_node.size - 1U))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (s1_cfg->s1cdmax > 0U) {
		// Substreams not supported yet, deny
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if ((s1_cfg->eats == SMMU_V3_STE_EATS_FULL) ||
	    (s1_cfg->eats == SMMU_V3_STE_EATS_FULL_DPT)) {
		// FULL ATS not yet supported
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	spinlock_acquire(&smmu_v3.stream_table_lock);

	smmu_v3_stream_table_entry_ptr_result_t ste_r =
		smmuv3_get_ste_slot(stream_id);
	if (ste_r.e != OK) {
		ste_r = smmuv3_alloc_ste_slot(stream_id);
		if (ste_r.e != OK) {
			TRACE_AND_LOG(
				DEBUG, INFO,
				"smmuv3: attach_stream could not allocate STE");
			ret = ste_r.e;
			goto out_unlock;
		}
	}

	if (smmu_v3_stream_table_entry_get_Valid(ste_r.r)) {
		// Ensure the STE is invalid before we update it.
		smmu_v3_stream_table_entry_set_Valid(ste_r.r, false);

		// Ensure that written data is observable to the SMMU
		smmuv3_data_store_sync_fence();

		// Ensure SID config is consistent before we update it.
		smmuv3_invalidate_sid(stream_id, true);
		smmuv3_sync_internal(false, true);
	}

	// Do some initial setup for the STE
	*ste_r.r = smmu_v3_stream_table_entry_default();

	// Setup S2, use CPU tables.
	vmid_t	      vmid    = range->viommu->addrspace->vmid;
	pgtable_vm_t *pgtable = &range->viommu->addrspace->vm_pgtable;

	// Support nested for Guest managed S1
	smmu_v3_stream_table_entry_set_Config(
		ste_r.r, SMMU_V3_STE_CONFIG_S1TRANSLATE_S2TRANSLATE);

	smmu_v3_stream_table_entry_set_S2VMID(ste_r.r, vmid);

	// AA64
	smmu_v3_stream_table_entry_set_S2AA64(ste_r.r, true);

	// These values seem to match the VTCR definitions if S2AA64=1, like we
	// set above
	smmu_v3_stream_table_entry_set_S2T0SZ(
		ste_r.r, VTCR_EL2_get_T0SZ(&pgtable->vtcr_el2));
	smmu_v3_stream_table_entry_set_S2SL0(
		ste_r.r, VTCR_EL2_get_SL0(&pgtable->vtcr_el2));

	tcr_rgn_t s2ir0 = VTCR_EL2_get_IRGN0(&pgtable->vtcr_el2);
	tcr_rgn_t s2or0 = VTCR_EL2_get_ORGN0(&pgtable->vtcr_el2);
	tcr_sh_t  s2sh0 = VTCR_EL2_get_SH0(&pgtable->vtcr_el2);
	tcr_tg0_t s2tg	= VTCR_EL2_get_TG0(&pgtable->vtcr_el2);
	tcr_ps_t  s2ps	= VTCR_EL2_get_PS(&pgtable->vtcr_el2);
	smmu_v3_stream_table_entry_set_S2IR0(ste_r.r, (uint8_t)s2ir0);
	smmu_v3_stream_table_entry_set_S2OR0(ste_r.r, (uint8_t)s2or0);
	smmu_v3_stream_table_entry_set_S2SH0(ste_r.r, (uint8_t)s2sh0);
	smmu_v3_stream_table_entry_set_S2TG(ste_r.r, (uint8_t)s2tg);
	smmu_v3_stream_table_entry_set_S2PS(ste_r.r, (uint8_t)s2ps);

	// Stage2 page table root
	smmu_v3_stream_table_entry_set_S2TTB(
		ste_r.r, pgtable->control.root_pgtable >> 4);

	// Record events, Disable Stall
	smmu_v3_stream_table_entry_set_S2Record(ste_r.r, true);
	smmu_v3_stream_table_entry_set_S2Stall(ste_r.r, false);

	// Disable Stall config in CD stage1, always terminate
	smmu_v3_stream_table_entry_set_S1STALLD(ste_r.r, true);

	// Number of substreams from Guest
	smmu_v3_stream_table_entry_set_S1CDMax(ste_r.r, s1_cfg->s1cdmax);

	// Set ATS enable/disable
	smmu_v3_stream_table_entry_set_EATS(ste_r.r, s1_cfg->eats);

	// Point the STE to the CD (in S1 IPA space; no need to validate)
	smmu_v3_stream_table_entry_set_S1ContextPtr(ste_r.r, s1_cfg->cd_ptr);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	// Ensure SID config is consistent before we enable it.
	smmuv3_invalidate_sid(stream_id, true);
	smmuv3_sync_internal(false, true);

	// Mark the STE as valid so the HW can use it
	smmu_v3_stream_table_entry_set_Valid(ste_r.r, true);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	smmuv3_invalidate_sid(stream_id, true);

	// If EATS is set we need to invalidate the ATS Cache as well.
	if (s1_cfg->eats != SMMU_V3_STE_EATS_DISABLE) {
		smmuv3_sync_internal(false, false);
		smmu_v3_atc_inv_stream_all(range, stream_id);
	}

	if (prefetch_ste) {
		smmuv3_prefetch_sid(stream_id);
	}

	// Wait for SMMU to complete.
	smmuv3_sync_internal(false, true);

	ret = OK;
out_unlock:
	spinlock_release(&smmu_v3.stream_table_lock);
out:
	return ret;
}

void
smmuv3_detach_stream(smmuv3_stream_range_t *range,
		     smmu_v3_stream_id_t    stream_id)
{
	assert((range != NULL) && (range->smmuv3 != NULL) &&
	       (range->smmuv3->instance == &smmu_v3));

	spinlock_acquire(&smmu_v3.stream_table_lock);

	if ((stream_id < range->phys_node.base) ||
	    (stream_id >
	     (range->phys_node.base + range->phys_node.size - 1U))) {
		goto out_unlock;
	}

	smmu_v3_stream_table_entry_ptr_result_t ste_r =
		smmuv3_get_ste_slot(stream_id);
	if (ste_r.e != OK) {
		TRACE(DEBUG, INFO, "smmuv3_detach_stream: no slot (sid {:#x})",
		      stream_id);
		goto out_unlock;
	}

	if (!smmu_v3_stream_table_entry_get_Valid(ste_r.r)) {
		// STE is already invalid; nothing more to do.
		goto out_unlock;
	}

	// Mark the STE as invalid so the HW will ignore it
	smmu_v3_stream_table_entry_set_Valid(ste_r.r, false);

	// Ensure that written data is observable to the SMMU
	smmuv3_data_store_sync_fence();

	smmuv3_invalidate_sid(stream_id, true);

	// If EATS was set we need to invalidate the ATS Cache as well.
	if (smmu_v3_stream_table_entry_get_EATS(ste_r.r) !=
	    SMMU_V3_STE_EATS_DISABLE) {
		smmuv3_sync_internal(false, false);
		smmu_v3_atc_inv_stream_all(range, stream_id);
	}

	// Wait for SMMU to complete.
	smmuv3_sync_internal(false, true);

out_unlock:
	spinlock_release(&smmu_v3.stream_table_lock);
	return;
}
#endif // SMMU_V3_ENABLE

void
smmuv3_handle_boot_hypervisor_start(void)
{
#if defined(SMMU_V3_PASSTHROUGH) && SMMU_V3_PASSTHROUGH

	// Log and exit.
	// Do not configure the SMMU, let HLOS take ownership
	smmuv3_regs_lock();
	SMMU_V3_AIDR_t aidr =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.aidr);
	smmuv3_regs_unlock();
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Found SMMUv3.{:d}",
		      SMMU_V3_AIDR_get_ArchMinorRev(&aidr));
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Pass-through to PVM");
	return;

#else // SMMU_V3_PASSTHROUGH

	smmuv3_regs_lock();
	SMMU_V3_AIDR_t aidr =
		atomic_load_relaxed(&smmu_v3.regs->smmu_page0.aidr);
	smmuv3_regs_unlock();
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Found SMMUv3.{:d}",
		      SMMU_V3_AIDR_get_ArchMinorRev(&aidr));

	SMMU_V3_CR0_t  cr0;
	SMMU_V3_CR0_t  cr0ack;
	SMMU_V3_GBPA_t gbpa;

	error_t err = platform_smmuv3_init();
	assert(err == OK);

	// First check there are no pending updates
	// SMMU should be disabled
	smmuv3_regs_lock();
	cr0    = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0);
	cr0ack = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0ack);
	gbpa   = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gbpa);
	smmuv3_regs_unlock();
	assert(!SMMU_V3_GBPA_get_Update(&gbpa));
	assert(SMMU_V3_CR0_is_equal(cr0, cr0ack));
	assert(!SMMU_V3_CR0_get_SMMUEN(&cr0));

#if defined(SMMU_V3_ENABLE) && SMMU_V3_ENABLE

	smmuv3_init();

	// Enable the SMMU
	// init() could have changed CR0, so fetch it again
	smmuv3_regs_lock();
	cr0 = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0);
	SMMU_V3_CR0_set_SMMUEN(&cr0, true);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr0, cr0);

	// Wait for update to complete
	do {
		cr0ack = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0ack);
	} while (!SMMU_V3_CR0_is_equal(cr0, cr0ack));
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Enabled");
	smmuv3_regs_unlock();

#else // SMMU_V3_ENABLE

	// Disable the SMMU
	// Set the bypass config
	// Set all to preserve incoming attributes
	// SHCFG is non-zero for 'use incoming'
	SMMU_V3_GBPA_set_MTCFG(&gbpa, false);
	SMMU_V3_GBPA_set_ALLOCCFG(&gbpa, false);
	SMMU_V3_GBPA_set_SHCFG(&gbpa, true);
	SMMU_V3_GBPA_set_PRIVCFG(&gbpa, false);
	SMMU_V3_GBPA_set_INSTCFG(&gbpa, false);
#if defined(SMMU_V3_GLOBAL_BYPASS) && SMMU_V3_GLOBAL_BYPASS
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Enable global bypass");
	SMMU_V3_GBPA_set_ABORT(&gbpa, false);
#else
	TRACE_AND_LOG(DEBUG, INFO, "smmuv3: Enable global abort");
	SMMU_V3_GBPA_set_ABORT(&gbpa, true);
#endif
	// Set update flag to check for completion
	SMMU_V3_GBPA_set_Update(&gbpa, true);
	smmuv3_regs_lock();
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.gbpa, gbpa);

	// Wait for update to complete
	do {
		gbpa = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.gbpa);
	} while (SMMU_V3_GBPA_get_Update(&gbpa));

	// Disable the SMMU
	SMMU_V3_CR0_set_SMMUEN(&cr0, false);
	atomic_store_relaxed(&smmu_v3.regs->smmu_page0.cr0, cr0);

	// Wait for update to complete
	do {
		cr0ack = atomic_load_relaxed(&smmu_v3.regs->smmu_page0.cr0ack);
	} while (!SMMU_V3_CR0_is_equal(cr0, cr0ack));
	smmuv3_regs_unlock();
#endif // SMMU_V3_ENABLE

#endif // SMMU_V3_PASSTHROUGH
}
