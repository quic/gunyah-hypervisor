// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypcontainers.h>
#include <hypregisters.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <qcbor.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vic.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "internal.h"
#include "vgic_gicr.h"

// Qualcomm's JEP106 identifier is 0x70, with no continuation bytes. This is
// used in the virtual GICD_IIDR and GICR_IIDR.
#define JEP106_IDENTITY 0x70U
#define JEP106_CONTCODE 0x0U
#define IIDR_PRODUCTID	(uint8_t)'G' /* For "Gunyah" */
#define IIDR_VARIANT	0U
#define IIDR_REVISION	0U

static void
vgic_update_irqbits_flag(vic_t *vic, const thread_t *vcpu, size_t base_offset,
			 count_t range_base, count_t range_size, uint32_t *bits)
{
	for (index_t i = 0; i < vic->gicr_count; i++) {
		rcu_read_start();
		thread_t *check_vcpu = atomic_load_consume(&vic->gicr_vcpus[i]);
		if (check_vcpu == NULL) {
			goto next_vcpu;
		}

		// If it's the private range, make sure we only look at the
		// targeted VCPU.
		if (vgic_irq_is_private(range_base) && (check_vcpu != vcpu)) {
			goto next_vcpu;
		}

		cpu_index_t remote_cpu = vgic_lr_owner_lock(check_vcpu);

		// If it's remotely running, we can't check its LRs. If any of
		// the range is listed in this VCPU, we're out of luck.
		if (cpulocal_index_valid(remote_cpu)) {
			goto next_vcpu_locked;
		}

		for (count_t lr = 0U; lr < CPU_GICH_LR_COUNT; lr++) {
			vgic_lr_status_t *status = &check_vcpu->vgic_lrs[lr];
			if (status->dstate == NULL) {
				// LR is not in use
				continue;
			}

			virq_t virq =
				ICH_LR_EL2_base_get_vINTID(&status->lr.base);
			if ((virq < range_base) ||
			    (virq >= (range_base + range_size))) {
				// LR's VIRQ is not in this range
				continue;
			}

			uint32_t bit = (uint32_t)util_bit(virq - range_base);
			ICH_LR_EL2_State_t state =
				ICH_LR_EL2_base_get_State(&status->lr.base);

			switch (base_offset) {
			case offsetof(gicd_t, ispendr):
			case offsetof(gicd_t, icpendr):
			case offsetof(gicd_t, ispendr_e):
			case offsetof(gicd_t, icpendr_e):
			case offsetof(gicr_t, sgi.ispendr_e):
			case offsetof(gicr_t, sgi.icpendr_e):
				if ((state == ICH_LR_EL2_STATE_PENDING) ||
				    (state ==
				     ICH_LR_EL2_STATE_PENDING_ACTIVE)) {
					(*bits) |= bit;
				} else {
					(*bits) &= ~bit;
				}
				break;
			case offsetof(gicd_t, isactiver):
			case offsetof(gicd_t, icactiver):
			case offsetof(gicd_t, isactiver_e):
			case offsetof(gicd_t, icactiver_e):
			case offsetof(gicr_t, sgi.isactiver_e):
			case offsetof(gicr_t, sgi.icactiver_e):
				if ((state == ICH_LR_EL2_STATE_ACTIVE) ||
				    (state ==
				     ICH_LR_EL2_STATE_PENDING_ACTIVE)) {
					(*bits) |= bit;
				} else {
					(*bits) &= ~bit;
				}
				break;
			default:
				panic("vgic_read_irqbits: Bad base_offset");
			}
		}

	next_vcpu_locked:
		vgic_lr_owner_unlock(check_vcpu);
	next_vcpu:
		rcu_read_finish();
	}
}

static uint32_t
vgic_read_gicd_irqbits(count_t			      range_size,
		       _Atomic vgic_delivery_state_t *dstates,
		       size_t base_offset, bool *listed)
{
	uint32_t bits = 0U;
	for (count_t i = 0; i < range_size; i++) {
		vgic_delivery_state_t this_dstate =
			atomic_load_relaxed(&dstates[i]);
		bool bit;

		// Note: the GICR base offsets are the same as the GICD offsets
		// (for IGROUPR, IS/ICENABLER, IS/ICPENDR, IS/ICACTIVER, ICFGR,
		// IGRPMODR) so we don't need to duplicate them here.
		// The extended registers don't overlap and can just be included
		// as additional cases.
		switch (base_offset) {
		case offsetof(gicd_t, igroupr):
		case offsetof(gicd_t, igroupr_e):
		case offsetof(gicr_t, sgi.igroupr_e):
			bit = vgic_delivery_state_get_group1(&this_dstate);
			break;
		case offsetof(gicd_t, isenabler):
		case offsetof(gicd_t, icenabler):
		case offsetof(gicd_t, isenabler_e):
		case offsetof(gicd_t, icenabler_e):
		case offsetof(gicr_t, sgi.isenabler_e):
		case offsetof(gicr_t, sgi.icenabler_e):
			bit = vgic_delivery_state_get_enabled(&this_dstate);
			break;
		case offsetof(gicd_t, ispendr):
		case offsetof(gicd_t, icpendr):
		case offsetof(gicd_t, ispendr_e):
		case offsetof(gicd_t, icpendr_e):
		case offsetof(gicr_t, sgi.ispendr_e):
		case offsetof(gicr_t, sgi.icpendr_e):
			bit = vgic_delivery_state_is_pending(&this_dstate);
			if (vgic_delivery_state_get_listed(&this_dstate)) {
				*listed = true;
			}
			break;
		case offsetof(gicd_t, isactiver):
		case offsetof(gicd_t, icactiver):
		case offsetof(gicd_t, isactiver_e):
		case offsetof(gicd_t, icactiver_e):
		case offsetof(gicr_t, sgi.isactiver_e):
		case offsetof(gicr_t, sgi.icactiver_e):
			bit = vgic_delivery_state_get_active(&this_dstate);
			if (vgic_delivery_state_get_listed(&this_dstate)) {
				*listed = true;
			}
			break;
		default:
			panic("vgic_read_irqbits: Bad base_offset");
		}

		if (bit) {
			bits |= (uint32_t)util_bit(i);
		}
	}

	return bits;
}

static uint32_t
vgic_read_irqbits(vic_t *vic, thread_t *vcpu, size_t base_offset, size_t offset)
{
	assert(vic != NULL);
	assert(vcpu != NULL);
	assert(offset >= base_offset);
#if VGIC_HAS_EXT_SPIS
	assert(offset <= base_offset + (32U * sizeof(uint32_t)));
#else
	assert(offset <= base_offset + (31U * sizeof(uint32_t)));
#endif

	uint32_t bits = 0U;
	// virq number at position 0 of the register to be read
	count_t range_base;
	// Actual readable states (bits) in register, it is not necessarily 32.
	count_t range_size;

#if VGIC_HAS_EXT_PPIS
	// Check whether vgic_read_irqbits is being called on extended irq
	// registers.
	if ((base_offset == offsetof(gicr_t, sgi.igroupr_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.isenabler_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.icenabler_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.ispendr_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.icpendr_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.isactiver_e)) ||
	    (base_offset == offsetof(gicr_t, sgi.icactiver_e))) {
		range_base =
			((count_t)((offset - base_offset) / sizeof(uint32_t)) *
			 32U) +
			GIC_PPI_EXT_BASE;
	} else
#endif
#if VGIC_HAS_EXT_SPIS
		if ((base_offset == offsetof(gicd_t, igroupr_e)) ||
		    (base_offset == offsetof(gicd_t, isenabler_e)) ||
		    (base_offset == offsetof(gicd_t, icenabler_e)) ||
		    (base_offset == offsetof(gicd_t, ispendr_e)) ||
		    (base_offset == offsetof(gicd_t, icpendr_e)) ||
		    (base_offset == offsetof(gicd_t, isactiver_e)) ||
		    (base_offset == offsetof(gicd_t, icactiver_e))) {
		// Check assumes vgic_read_irqbits is never read on GICD_IROUTER
		// and GICD_ICLAR.
		range_base =
			((count_t)((offset - base_offset) / sizeof(uint32_t)) *
			 32U) +
			GIC_SPI_EXT_BASE;
	} else
#endif
	{
		range_base =
			(count_t)((offset - base_offset) / sizeof(uint32_t)) *
			32U;
	}

	range_size = (range_base < GIC_SPECIAL_INTIDS_BASE)
			     ? util_min(32U, (count_t)(GIC_SPECIAL_INTIDS_BASE -
						       range_base))
			     : 32U;

	rcu_read_start();
	_Atomic vgic_delivery_state_t *dstates =
		vgic_find_dstate(vic, vcpu, range_base);
	if (dstates == NULL) {
		rcu_read_finish();
		goto out;
	}
	assert(compiler_sizeof_object(dstates) >=
	       (range_size * sizeof(*dstates)));

	bool listed = false;

	bits = vgic_read_gicd_irqbits(range_size, dstates, base_offset,
				      &listed);
	rcu_read_finish();

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	if ((range_base == GIC_SGI_BASE) &&
	    ((base_offset == offsetof(gicd_t, ispendr)) ||
	     (base_offset == offsetof(gicd_t, icpendr)) ||
	     (base_offset == offsetof(gicd_t, ispendr_e)) ||
	     (base_offset == offsetof(gicd_t, icpendr_e)) ||
	     (base_offset == offsetof(gicr_t, sgi.ispendr_e)) ||
	     (base_offset == offsetof(gicr_t, sgi.icpendr_e)))) {
		// Query the hardware for the vSGI pending state
		uint32_result_t bits_r = gicv3_vpe_vsgi_query(vcpu);
		if (bits_r.e == OK) {
			bits |= bits_r.r;
		}
	}
#endif // GICV3_HAS_VLPI_V4_1 && GICV3_ENABLE_VPE

	if (compiler_expected(!listed)) {
		// We didn't try to read the pending or active state of a VIRQ
		// that is in list register, so the value we've read is
		// accurate.
		goto out;
	}

	// Read back from the current VCPU's physical LRs.
	preempt_disable();
	for (count_t lr = 0U; lr < CPU_GICH_LR_COUNT; lr++) {
		vgic_read_lr_state(lr);
	}
	preempt_enable();

	// Try to update the flags for listed vIRQs, based on the state of
	// every VCPU's list registers.
	vgic_update_irqbits_flag(vic, vcpu, base_offset, range_base, range_size,
				 &bits);
out:
	return bits;
}

static register_t
vgic_read_priority(vic_t *vic, thread_t *vcpu, size_t offset,
		   size_t access_size)
{
	register_t bits = 0U;

	rcu_read_start();
	_Atomic vgic_delivery_state_t *dstates =
		vgic_find_dstate(vic, vcpu, (count_t)offset);
	if (dstates == NULL) {
		goto out;
	}
	assert(compiler_sizeof_object(dstates) >=
	       access_size * sizeof(*dstates));

	for (count_t i = 0; i < access_size; i++) {
		vgic_delivery_state_t this_dstate =
			atomic_load_relaxed(&dstates[i]);

		bits |= (register_t)vgic_delivery_state_get_priority(
				&this_dstate)
			<< (i * util_width(uint8_t));
	}

out:
	rcu_read_finish();

	return bits;
}

static register_t
vgic_read_config(vic_t *vic, thread_t *vcpu, size_t base_offset, size_t offset)
{
	assert(vic != NULL);
	assert(vcpu != NULL);
	assert(offset >= base_offset);
	assert(offset <= base_offset + (64U * sizeof(uint32_t)));

	register_t bits = 0U;
	// virq number at position [0:1] of the register to be read
	count_t range_base;
	// Actual readable states (2 bits) in register, it is not
	// necessarily 16.
	count_t range_size;

#if VGIC_HAS_EXT_PPIS
	// Check whether extended irq configs are being read.
	if (base_offset == offsetof(gicr_t, sgi.icfgr_e)) {
		range_base =
			((count_t)((offset - base_offset) / sizeof(uint32_t)) *
			 16U) +
			GIC_PPI_EXT_BASE;
	} else
#endif
#if VGIC_HAS_EXT_SPIS
		if (base_offset == offsetof(gicd_t, icfgr_e)) {
		range_base =
			((count_t)((offset - base_offset) / sizeof(uint32_t)) *
			 16U) +
			GIC_SPI_EXT_BASE;
	} else
#endif
	{
		range_base =
			(count_t)((offset - base_offset) / sizeof(uint32_t)) *
			16U;
	}

	range_size = (range_base < GIC_SPECIAL_INTIDS_BASE)
			     ? util_min(16U, (count_t)(GIC_SPECIAL_INTIDS_BASE -
						       range_base))
			     : 16U;

	rcu_read_start();
	_Atomic vgic_delivery_state_t *dstates =
		vgic_find_dstate(vic, vcpu, range_base);
	if (dstates == NULL) {
		goto out;
	}
	assert(compiler_sizeof_object(dstates) >=
	       range_size * sizeof(*dstates));

	for (count_t i = 0; i < range_size; i++) {
		vgic_delivery_state_t this_dstate =
			atomic_load_relaxed(&dstates[i]);

		if (vgic_delivery_state_get_cfg_is_edge(&this_dstate)) {
			bits |= util_bit((i * 2U) + 1U);
		}
	}

out:
	rcu_read_finish();

	return bits;
}

static bool
gicd_vdevice_read(vic_t *vic, size_t offset, register_t *val,
		  size_t access_size)
{
	bool	  ret;
	thread_t *thread = thread_get_self();

	uint32_t read_val = 0U;

	assert(vic != NULL);

	switch (offset) {
	case offsetof(gicd_t, setspi_nsr):
	case offsetof(gicd_t, clrspi_nsr):
	case offsetof(gicd_t, setspi_sr):
	case offsetof(gicd_t, clrspi_sr):
	case offsetof(gicd_t, sgir): {
		// WO registers, RAZ
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_RWOD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		read_val = 0U;
		break;
	}
	case offsetof(gicd_t, ctlr):
		read_val =
			GICD_CTLR_DS_raw(atomic_load_relaxed(&vic->gicd_ctlr));
		break;
	case offsetof(gicd_t, statusr):
		read_val = GICD_STATUSR_raw(vic->gicd_statusr);
		break;
	case offsetof(gicd_t, typer): {
		GICD_TYPER_t typer = GICD_TYPER_default();
		GICD_TYPER_set_ITLinesNumber(
			&typer,
			(count_t)util_balign_up(GIC_SPI_NUM, 32U) / 32U);
		GICD_TYPER_set_MBIS(&typer, true);
#if VGIC_HAS_EXT_SPIS
		GICD_TYPER_set_ESPI(&typer, true);
		// # SPIs supported = (32 * (ESPI_Range + 1))
		GICD_TYPER_set_ESPI_range(&typer,
					  (VGIC_SPI_EXT_NUM / 32U) - 1U);
#else
		GICD_TYPER_set_ESPI(&typer, false);
#endif

#if VGIC_HAS_LPI
		GICD_TYPER_set_LPIS(&typer, vgic_has_lpis(vic));
		GICD_TYPER_set_IDbits(&typer, vic->gicd_idbits - 1U);
#else
		GICD_TYPER_set_IDbits(&typer, VGIC_IDBITS - 1U);
#endif
		GICD_TYPER_set_A3V(&typer, true);
		GICD_TYPER_set_No1N(&typer, VGIC_HAS_1N == 0);
		read_val = GICD_TYPER_raw(typer);
		break;
	}
	case offsetof(gicd_t, iidr): {
		GICD_IIDR_t iidr = GICD_IIDR_default();
		GICD_IIDR_set_Implementer_Identity(&iidr, JEP106_IDENTITY);
		GICD_IIDR_set_Implementer_ContCode(&iidr, JEP106_CONTCODE);
		GICD_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
		GICD_IIDR_set_Variant(&iidr, IIDR_VARIANT);
		GICD_IIDR_set_Revision(&iidr, IIDR_REVISION);
		read_val = GICD_IIDR_raw(iidr);
		break;
	}
	case offsetof(gicd_t, typer2): {
		GICD_TYPER2_t typer2 = GICD_TYPER2_default();
#if GICV3_HAS_VLPI_V4_1
		GICD_TYPER2_set_nASSGIcap(&typer2, vgic_has_lpis(vic));
#endif
		read_val = GICD_TYPER2_raw(typer2);
		break;
	}
	case OFS_GICD_PIDR2:
		read_val = VGIC_PIDR2;
		break;
	case util_offset_case_range(gicd_t, igroupr):
		read_val = vgic_read_irqbits(vic, thread,
					     offsetof(gicd_t, igroupr), offset);
		break;
	case util_offset_case_range(gicd_t, isenabler):
		read_val = vgic_read_irqbits(vic, thread,
					     OFS_GICD_ISENABLER(0U), offset);
		break;
	case util_offset_case_range(gicd_t, icenabler):
		read_val = vgic_read_irqbits(vic, thread,
					     OFS_GICD_ICENABLER(0U), offset);
		break;
	case util_offset_case_range(gicd_t, ispendr):
		read_val = vgic_read_irqbits(vic, thread, OFS_GICD_ISPENDR(0U),
					     offset);
		break;
	case util_offset_case_range(gicd_t, icpendr):
		read_val = vgic_read_irqbits(vic, thread, OFS_GICD_ICPENDR(0U),
					     offset);
		break;
	case util_offset_case_range(gicd_t, isactiver):
		read_val = vgic_read_irqbits(vic, thread,
					     OFS_GICD_ISACTIVER(0U), offset);
		break;
	case util_offset_case_range(gicd_t, icactiver):
		read_val = vgic_read_irqbits(vic, thread,
					     OFS_GICD_ICACTIVER(0U), offset);
		break;
	case util_offset_case_range(gicd_t, ipriorityr):
		read_val = (uint32_t)vgic_read_priority(
			vic, thread, offset - offsetof(gicd_t, ipriorityr),
			access_size);
		break;
	case util_offset_case_range(gicd_t, icfgr):
		read_val = (uint32_t)vgic_read_config(
			vic, thread, offsetof(gicd_t, icfgr), offset);
		break;
	case util_offset_case_range(gicd_t, itargetsr):
	case util_offset_case_range(gicd_t, igrpmodr):
	case util_offset_case_range(gicd_t, nsacr):
		// RAZ ranges
		read_val = 0U;
		break;
#if VGIC_HAS_EXT_SPIS
	case util_offset_case_range(gicd_t, igroupr_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, igroupr_e), offset);
		break;
	case util_offset_case_range(gicd_t, isenabler_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, isenabler_e), offset);
		break;
	case util_offset_case_range(gicd_t, icenabler_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, icenabler_e), offset);
		break;
	case util_offset_case_range(gicd_t, ispendr_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, ispendr_e), offset);
		break;
	case util_offset_case_range(gicd_t, icpendr_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, icpendr_e), offset);
		break;
	case util_offset_case_range(gicd_t, isactiver_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, isactiver_e), offset);
		break;
	case util_offset_case_range(gicd_t, icactiver_e):
		read_val = vgic_read_irqbits(
			vic, thread, offsetof(gicd_t, icactiver_e), offset);
		break;
	case util_offset_case_range(gicd_t, ipriorityr_e):
		read_val = (uint32_t)vgic_read_priority(
			vic, thread,
			offset - offsetof(gicd_t, ipriorityr_e) +
				GIC_SPI_EXT_BASE,
			access_size);
		break;
	case util_offset_case_range(gicd_t, icfgr_e):
		read_val = (uint32_t)vgic_read_config(
			vic, thread, offsetof(gicd_t, icfgr_e), offset);
		break;
	case util_offset_case_range(gicd_t, igrpmodr_e):
	case util_offset_case_range(gicd_t, nsacr_e):
		// RAZ ranges
		read_val = 0U;
		break;
#endif // VGIC_HAS_EXT_SPIS
	default: {
		// Unknown register
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_RRD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		read_val = 0U;
		break;
	}
	}

	*val = read_val;
	ret  = true;

	return ret;
}

static void
gicd_vdevice_enabler_write(vic_t *vic, size_t offset, register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n = (index_t)((offset - (set ? offsetof(gicd_t, isenabler)
					     : offsetof(gicd_t, icenabler))) /
			      sizeof(uint32_t));
	// Ignore writes to the SGI and PPI bits (ISENABLER0)
	if (n != 0U) {
		uint32_t bits = (uint32_t)val;
		if (n == 31U) {
			// Ignore the bits for IRQs 1020-1023
			bits &= ~0xf0000000U;
		}
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicd_change_irq_enable(vic, (n * 32U) + i, set);
		}
	}
}

static void
gicd_vdevice_pendr_write(vic_t *vic, size_t offset, register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n = (index_t)((offset - (set ? offsetof(gicd_t, ispendr)
					     : offsetof(gicd_t, icpendr))) /
			      sizeof(uint32_t));
	// Ignore writes to the SGI and PPI bits (ISENABLER0)
	if (n != 0U) {
		uint32_t bits = (uint32_t)val;
		if (n == 31U) {
			// Ignore the bits for IRQs 1020-1023
			bits &= ~0xf0000000U;
		}
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicd_change_irq_pending(vic, (n * 32U) + i, set,
						     false);
		}
	}
}

static void
gicd_vdevice_activer_write(vic_t *vic, size_t offset, register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n = (index_t)((offset - (set ? offsetof(gicd_t, isactiver)
					     : offsetof(gicd_t, icactiver))) /
			      sizeof(uint32_t));
	// Ignore writes to the SGI and PPI bits (ISENABLER0)
	if (n != 0U) {
		uint32_t bits = (uint32_t)val;
		if (n == 31U) {
			// Ignore the bits for IRQs 1020-1023
			bits &= ~0xf0000000U;
		}
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicd_change_irq_active(vic, (n * 32U) + i, set);
		}
	}
}

#if VGIC_HAS_EXT_SPIS
static void
gicd_vdevice_enabler_e_write(vic_t *vic, size_t offset, register_t val,
			     bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n = (index_t)((offset - (set ? offsetof(gicd_t, isenabler_e)
					     : offsetof(gicd_t, icenabler_e))) /
			      sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	if (n >= (VGIC_SPI_EXT_NUM / 32U)) {
		bits = 0U;
	}
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicd_change_irq_enable(
			vic, GIC_SPI_EXT_BASE + (n * 32U) + i, set);
	}
}

static void
gicd_vdevice_pendr_e_write(vic_t *vic, size_t offset, register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t	 n = (index_t)((offset - (set ? offsetof(gicd_t, ispendr_e)
					      : offsetof(gicd_t, icpendr_e))) /
			       sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	if (n >= (VGIC_SPI_EXT_NUM / 32U)) {
		bits = 0U;
	}
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicd_change_irq_pending(
			vic, GIC_SPI_EXT_BASE + (n * 32U) + i, set, false);
	}
}

static void
gicd_vdevice_activer_e_write(vic_t *vic, size_t offset, register_t val,
			     bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n = (index_t)((offset - (set ? offsetof(gicd_t, isactiver_e)
					     : offsetof(gicd_t, icactiver_e))) /
			      sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	if (n >= (VGIC_SPI_EXT_NUM / 32U)) {
		bits = 0U;
	}
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicd_change_irq_active(
			vic, GIC_SPI_EXT_BASE + (n * 32U) + i, set);
	}
}
#endif

static bool
gicd_vdevice_write(vic_t *vic, size_t offset, register_t val,
		   size_t access_size)
{
	bool ret = true;

	assert(vic != NULL);
	VGIC_TRACE(GICD_WRITE, vic, NULL, "GICD_WRITE reg = {:x}, val = {:#x}",
		   offset, val);

	switch (offset) {
	case util_offset_case_range(gicd_t, ctlr):
		vgic_gicd_set_control(vic, GICD_CTLR_DS_cast((uint32_t)val));
		break;
	case util_offset_case_range(gicd_t, typer):
	case util_offset_case_range(gicd_t, iidr):
	case OFS_GICD_PIDR2:
	case util_offset_case_range(gicd_t, typer2): {
		// RO registers
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_WROD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		break;
	}
	case util_offset_case_range(gicd_t, statusr): {
		GICD_STATUSR_t statusr = GICD_STATUSR_cast((uint32_t)val);
		vgic_gicd_set_statusr(vic, statusr, false);
		break;
	}
	case offsetof(gicd_t, setspi_nsr):
	case offsetof(gicd_t, clrspi_nsr): {
		vgic_gicd_change_irq_pending(
			vic,
			GICD_CLRSPI_SETSPI_NSR_SR_get_INTID(
				&GICD_CLRSPI_SETSPI_NSR_SR_cast((uint32_t)val)),
			(offset == offsetof(gicd_t, setspi_nsr)), true);
		break;
	}
	case offsetof(gicd_t, setspi_sr):
	case offsetof(gicd_t, clrspi_sr):
		// WI
		break;
	case util_offset_case_range(gicd_t, igroupr): {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_IGROUPR(0U)) /
				      sizeof(uint32_t));
		for (index_t i = util_max(n * 32U, GIC_SPI_BASE);
		     i < util_min((n + 1U) * 32U, 1020U); i++) {
			vgic_gicd_set_irq_group(
				vic, i, (val & util_bit(i % 32U)) != 0U);
		}
		break;
	}
	case util_offset_case_range(gicd_t, isenabler):
		gicd_vdevice_enabler_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icenabler):
		gicd_vdevice_enabler_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, ispendr):
		gicd_vdevice_pendr_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icpendr):
		gicd_vdevice_pendr_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, isactiver):
		gicd_vdevice_activer_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icactiver):
		gicd_vdevice_activer_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, ipriorityr): {
		// 32-bit registers, byte or 32-bit accessible

		index_t n = (index_t)(offset - OFS_GICD_IPRIORITYR(0U));
		// Loop through every byte
		uint32_t shifted_val = (uint32_t)val;
		for (index_t i = util_max(n, GIC_SPI_BASE);
		     i < (n + access_size); i++) {
			vgic_gicd_set_irq_priority(vic, i,
						   (uint8_t)shifted_val);
			shifted_val >>= util_width(uint8_t);
		}
		break;
	}
	case util_offset_case_range(gicd_t, itargetsr):
		// WI
		break;
	case util_offset_case_range(gicd_t, icfgr): {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ICFGR(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits
		for (index_t i = util_max(n * 16U, GIC_SPI_BASE);
		     i < util_min((n + 1U) * 16U, 1020U); i++) {
			vgic_gicd_set_irq_config(
				vic, i,
				(val & util_bit(((i % 16U) * 2U) + 1U)) != 0U);
		}
		break;
	}
	case util_offset_case_range(gicd_t, igrpmodr):
	case util_offset_case_range(gicd_t, nsacr):
	case offsetof(gicd_t, sgir):
	case util_offset_case_range(gicd_t, cpendsgir):
	case util_offset_case_range(gicd_t, spendsgir):
		// WI
		break;
	case util_case_range(OFS_GICD_IROUTER(0U),
			     OFS_GICD_IROUTER(GIC_SPI_NUM - 1)): {
		// 64-bit registers with 64-bit access only

		index_t	       spi = GIC_SPI_BASE +
				     (index_t)((offset - OFS_GICD_IROUTER(0U)) /
					       sizeof(uint64_t));
		GICD_IROUTER_t irouter = GICD_IROUTER_cast(val);
		vgic_gicd_set_irq_router(vic, spi,
					 GICD_IROUTER_get_Aff0(&irouter),
					 GICD_IROUTER_get_Aff1(&irouter),
					 GICD_IROUTER_get_Aff2(&irouter),
					 GICD_IROUTER_get_Aff3(&irouter),
					 GICD_IROUTER_get_IRM(&irouter));
		break;
	}
#if GICV3_HAS_GICD_ICLAR
	case OFS_GICD_SETCLASSR: {
		GICD_SETCLASSR_t setclassr = GICD_SETCLASSR_cast((uint32_t)val);
		virq_t		 virq	   = GICD_SETCLASSR_get_SPI(&setclassr);
		if (vgic_irq_is_spi(virq)) {
			vgic_gicd_set_irq_classes(
				vic, virq,
				GICD_SETCLASSR_get_Class0(&setclassr),
				GICD_SETCLASSR_get_Class1(&setclassr));
		}
		break;
	}
#endif

#if VGIC_HAS_EXT_SPIS
	case util_offset_case_range(gicd_t, igroupr_e): {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - offsetof(gicd_t, igroupr_e)) /
				      sizeof(uint32_t));
		for (index_t i = GIC_SPI_EXT_BASE + (n * 32U);
		     i < (GIC_SPI_EXT_BASE + ((n + 1U) * 32U)); i++) {
			vgic_gicd_set_irq_group(
				vic, i, (val & util_bit(i % 32U)) != 0U);
		}
		break;
	}
	case util_offset_case_range(gicd_t, isenabler_e):
		gicd_vdevice_enabler_e_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icenabler_e):
		gicd_vdevice_enabler_e_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, ispendr_e):
		gicd_vdevice_pendr_e_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icpendr_e):
		gicd_vdevice_pendr_e_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, isactiver_e):
		gicd_vdevice_activer_e_write(vic, offset, val, true);
		break;
	case util_offset_case_range(gicd_t, icactiver_e):
		gicd_vdevice_activer_e_write(vic, offset, val, false);
		break;
	case util_offset_case_range(gicd_t, ipriorityr_e): {
		// 32-bit registers, byte or 32-bit accessible

		index_t n = (index_t)(offset - offsetof(gicd_t, ipriorityr_e));
		if (n >= VGIC_SPI_EXT_NUM) {
			break;
		}
		// Loop through every byte
		uint32_t shifted_val = (uint32_t)val;
		for (index_t i = n; i < (n + access_size); i++) {
			vgic_gicd_set_irq_priority(vic, GIC_SPI_EXT_BASE + i,
						   (uint8_t)shifted_val);
			shifted_val >>= util_width(uint8_t);
		}
		break;
	}
	case util_offset_case_range(gicd_t, icfgr_e): {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - offsetof(gicd_t, icfgr_e)) /
				      sizeof(uint32_t));
		if (n >= (VGIC_SPI_EXT_NUM / 16U)) {
			break;
		}
		for (index_t i = n * 16U; i < ((n + 1U) * 16U); i++) {
			vgic_gicd_set_irq_config(
				vic, GIC_SPI_EXT_BASE + i,
				(val & util_bit(((i % 16U) * 2U) + 1U)) != 0U);
		}
		break;
	}
	case util_offset_case_range(gicd_t, igrpmodr_e):
	case util_offset_case_range(gicd_t, nsacr_e):
		// WI
		break;
	case util_offset_case_range(gicd_t, irouter_e): {
		// 64-bit registers with 64-bit access only

		index_t spi = GIC_SPI_EXT_BASE +
			      (index_t)((offset - offsetof(gicd_t, irouter_e)) /
					sizeof(uint64_t));
		if (spi >= (GIC_SPI_EXT_BASE + VGIC_SPI_EXT_NUM)) {
			break;
		}
		GICD_IROUTER_t irouter_e = GICD_IROUTER_cast(val);
		vgic_gicd_set_irq_router(vic, spi,
					 GICD_IROUTER_get_Aff0(&irouter_e),
					 GICD_IROUTER_get_Aff1(&irouter_e),
					 GICD_IROUTER_get_Aff2(&irouter_e),
					 GICD_IROUTER_get_Aff3(&irouter_e),
					 GICD_IROUTER_get_IRM(&irouter_e));
		break;
	}
#endif // VGIC_HAS_EXT_SPIS

#if VGIC_IGNORE_ARRAY_OVERFLOWS
	case util_case_range(OFS_GICD_IPRIORITYR(1020U),
			     OFS_GICD_IPRIORITYR(1023U)):
		// Ignore priority writes for special IRQs
		break;
	case util_case_range(OFS_GICD_IROUTER(GIC_SPI_NUM),
			     OFS_GICD_IROUTER(991U)):
		// Ignore route writes for special IRQs
		break;
#endif
	default: {
		// Unknown register
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_WRD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		ret = false;
		break;
	}
	}

	return ret;
}

static bool
gicd_access_allowed(size_t size, size_t offset)
{
	bool ret;

	// First check if the access is size-aligned
	if ((offset & (size - 1U)) != 0UL) {
		ret = false;
	} else if (size == sizeof(uint64_t)) {
		// Doubleword accesses are only allowed for routing
		// registers
		ret = ((offset >= OFS_GICD_IROUTER(0U)) &&
		       (offset <= OFS_GICD_IROUTER(GIC_SPI_NUM - 1U)));
#if VGIC_IGNORE_ARRAY_OVERFLOWS
		// Ignore route accesses for special IRQs
		if ((offset >= OFS_GICD_IROUTER(0U)) &&
		    (offset <= OFS_GICD_IROUTER(1023U))) {
			ret = true;
		}
#endif
#if VGIC_HAS_EXT_SPIS
		ret = ret || util_offset_in_range(offset, gicd_t, irouter_e);
#endif
	} else if (size == sizeof(uint32_t)) {
		// Word accesses, always allowed
		ret = true;
	} else if (size == sizeof(uint16_t)) {
		// Half-word accesses are only allowed for the SETSPI
		// and CLRSPI registers
		ret = ((offset == offsetof(gicd_t, setspi_nsr)) ||
		       (offset == offsetof(gicd_t, clrspi_nsr)));
	} else if (size == sizeof(uint8_t)) {
		// Byte accesses are only allowed for priority, target
		// and SGI pending registers
		ret = (((offset >= OFS_GICD_IPRIORITYR(0U)) &&
			(offset <= OFS_GICD_IPRIORITYR(1019U))) ||
		       ((offset >= OFS_GICD_ITARGETSR(0U)) &&
			(offset <= OFS_GICD_ITARGETSR(1019U))) ||
		       ((offset >= OFS_GICD_CPENDSGIR(0U)) &&
			(offset <= OFS_GICD_CPENDSGIR(15U))) ||
		       ((offset >= OFS_GICD_SPENDSGIR(0U)) &&
			(offset <= OFS_GICD_SPENDSGIR(15U))));
#if VGIC_IGNORE_ARRAY_OVERFLOWS
		// Ignore priority accesses for special IRQs
		if ((offset >= OFS_GICD_IPRIORITYR(0U)) &&
		    (offset <= OFS_GICD_IPRIORITYR(1023U))) {
			ret = true;
		}
#endif
#if VGIC_HAS_EXT_SPIS
		ret = ret || util_offset_in_range(offset, gicd_t, ipriorityr_e);
#endif
	} else {
		// Invalid access size
		ret = false;
	}

	return ret;
}

static bool
gicr_vdevice_read(vic_t *vic, thread_t *gicr_vcpu, index_t gicr_num,
		  size_t offset, register_t *val, size_t access_size,
		  bool last_gicr)
{
	bool ret = true;

	(void)vic;

	switch (offset) {
	case offsetof(gicr_t, rd.setlpir):
	case offsetof(gicr_t, rd.clrlpir):
	case offsetof(gicr_t, rd.invlpir):
	case offsetof(gicr_t, rd.invallr): {
		// WO registers, RAZ
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_RWOD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		*val = 0U;
		break;
	}
	case util_offset_case_range(gicr_t, rd.typer): {
		GICR_TYPER_t typer = GICR_TYPER_default();
		GICR_TYPER_set_Aff0(
			&typer,
			MPIDR_EL1_get_Aff0(&gicr_vcpu->vcpu_regs_mpidr_el1));
		GICR_TYPER_set_Aff1(
			&typer,
			MPIDR_EL1_get_Aff1(&gicr_vcpu->vcpu_regs_mpidr_el1));
		GICR_TYPER_set_Aff2(
			&typer,
			MPIDR_EL1_get_Aff2(&gicr_vcpu->vcpu_regs_mpidr_el1));
		GICR_TYPER_set_Aff3(
			&typer,
			MPIDR_EL1_get_Aff3(&gicr_vcpu->vcpu_regs_mpidr_el1));
		GICR_TYPER_set_Last(&typer, last_gicr);

		// The Processor Number is used only to select the
		// target GICR in ITS commands. When ARE is disabled, it
		// also determines the CPU's bit in ITARGETSR, but we
		// don't support that. So it is safe for this to be the
		// logical VCPU index.
		GICR_TYPER_set_Processor_Num(&typer, gicr_num);
#if VGIC_HAS_LPI
		GICR_TYPER_set_PLPIS(&typer, vgic_has_lpis(vic));
#endif

#if VGIC_HAS_EXT_PPIS
		GICR_TYPER_set_PPInum(&typer, GICR_TYPER_PPINUM_MAX_1119);
#endif

		*val = GICR_TYPER_raw(typer);

		if (offset != offsetof(gicr_t, rd.typer)) {
			// Must be a 32-bit access to the big end
			assert(offset ==
			       ((size_t)OFS_GICR_RD_TYPER + sizeof(uint32_t)));
			*val >>= 32U;
		}
		break;
	}
	case util_offset_case_range(gicr_t, rd.iidr): {
		GICR_IIDR_t iidr = GICR_IIDR_default();
		GICR_IIDR_set_Implementer_Identity(&iidr, JEP106_IDENTITY);
		GICR_IIDR_set_Implementer_ContCode(&iidr, JEP106_CONTCODE);
		GICR_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
		GICR_IIDR_set_Variant(&iidr, IIDR_VARIANT);
		GICR_IIDR_set_Revision(&iidr, IIDR_REVISION);
		*val = GICR_IIDR_raw(iidr);
		break;
	}
	case util_offset_case_range(gicr_t, PIDR2):
		*val = VGIC_PIDR2;
		break;
	case util_offset_case_range(gicr_t, rd.ctlr):
		*val = GICR_CTLR_raw(vgic_gicr_rd_get_control(vic, gicr_vcpu));
		break;
	case util_offset_case_range(gicr_t, rd.statusr):
		*val = GICR_STATUSR_raw(
			atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_statusr));
		break;
	case util_offset_case_range(gicr_t, rd.waker): {
		GICR_WAKER_t gicr_waker = GICR_WAKER_default();
		GICR_WAKER_set_ProcessorSleep(
			&gicr_waker,
			atomic_load_relaxed(&gicr_vcpu->vgic_sleep) !=
				VGIC_SLEEP_STATE_AWAKE);
		GICR_WAKER_set_ChildrenAsleep(
			&gicr_waker, vgic_gicr_rd_check_sleep(gicr_vcpu));

		*val = GICR_WAKER_raw(gicr_waker);
		break;
	}
	case util_offset_case_range(gicr_t, rd.propbaser):
#if VGIC_HAS_LPI
		*val = GICR_PROPBASER_raw(
			atomic_load_relaxed(&vic->gicr_rd_propbaser));
#else
		*val = 0U;
#endif
		break;
	case util_offset_case_range(gicr_t, rd.pendbaser): {
#if VGIC_HAS_LPI
		GICR_PENDBASER_t pendbase =
			atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_pendbaser);
		// The PTZ bit is specified as WO/RAZ, but we use it to
		// cache the written value which is used when EnableLPIs
		// is set to 1. Therefore we must clear it here.
		GICR_PENDBASER_set_PTZ(&pendbase, false);
		*val = GICR_PENDBASER_raw(pendbase);
#else
		*val = 0U;
#endif
		break;
	}
	case util_offset_case_range(gicr_t, rd.syncr): {
#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
		GICR_SYNCR_t syncr = GICR_SYNCR_default();
		GICR_SYNCR_set_Busy(&syncr,
				    vgic_gicr_get_inv_pending(vic, gicr_vcpu));
		*val = GICR_SYNCR_raw(syncr);
#else
		*val = 0U;
#endif
		break;
	}
	case offsetof(gicr_t, sgi.igroupr0):
	case offsetof(gicr_t, sgi.isenabler0):
	case offsetof(gicr_t, sgi.icenabler0):
	case offsetof(gicr_t, sgi.ispendr0):
	case offsetof(gicr_t, sgi.icpendr0):
	case offsetof(gicr_t, sgi.isactiver0):
	case offsetof(gicr_t, sgi.icactiver0):
		*val = (uint32_t)vgic_read_irqbits(
			vic, gicr_vcpu, offset - offsetof(gicr_t, sgi),
			offset - offsetof(gicr_t, sgi));
		break;
	case offsetof(gicr_t, sgi.igrpmodr0):
	case offsetof(gicr_t, sgi.nsacr):
		// RAZ/WI because GICD_CTLR.DS==1
		*val = 0U;
		break;
	case util_offset_case_range(gicr_t, sgi.ipriorityr):
		*val = vgic_read_priority(
			vic, gicr_vcpu,
			offset - offsetof(gicr_t, sgi.ipriorityr), access_size);
		break;
	case util_offset_case_range(gicr_t, sgi.icfgr):
		*val = vgic_read_config(vic, gicr_vcpu,
					offsetof(gicr_t, sgi.icfgr), offset);
		break;
#if VGIC_HAS_EXT_PPIS
	case util_offset_case_range(gicr_t, sgi.igroupr_e):
	case util_offset_case_range(gicr_t, sgi.isenabler_e):
	case util_offset_case_range(gicr_t, sgi.icenabler_e):
	case util_offset_case_range(gicr_t, sgi.ispendr_e):
	case util_offset_case_range(gicr_t, sgi.icpendr_e):
	case util_offset_case_range(gicr_t, sgi.isactiver_e):
	case util_offset_case_range(gicr_t, sgi.icactiver_e):
		*val = (uint32_t)vgic_read_irqbits(
			vic, gicr_vcpu, offset - offsetof(gicr_t, sgi),
			offset - offsetof(gicr_t, sgi));
		break;
	case util_offset_case_range(gicr_t, sgi.igrpmodr_e):
		// RAZ/WI because GICD_CTLR.DS==1
		*val = 0U;
		break;
	case util_offset_case_range(gicr_t, sgi.ipriorityr_e):
		*val = vgic_read_priority(
			vic, gicr_vcpu,
			offset - offsetof(gicr_t, sgi.ipriorityr_e) +
				GIC_PPI_EXT_BASE,
			access_size);
		break;
	case util_offset_case_range(gicr_t, sgi.icfgr_e):
		*val = vgic_read_config(vic, gicr_vcpu,
					offsetof(gicr_t, sgi.icfgr_e), offset);
		break;
#endif // VGIC_HAS_EXT_PPIS

	default: {
		// Unknown register
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_RRD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		*val = 0U;
		break;
	}
	}

	return ret;
}

static void
gicr_vdevice_icfgr_write(vic_t *vic, thread_t *gicr_vcpu, register_t val)
{
	// 32-bit register, 32-bit access only
	for (index_t i = 0U; i < GIC_PPI_NUM; i++) {
		vgic_gicr_sgi_set_ppi_config(vic, gicr_vcpu, i + GIC_PPI_BASE,
					     (val & util_bit((i * 2U) + 1U)) !=
						     0U);
	}
}

static void
gicr_vdevice_ipriorityr_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			      register_t val, size_t access_size)
{
	// 32-bit registers, byte or 32-bit accessible
	index_t n = (index_t)(offset - OFS_GICR_SGI_IPRIORITYR(0U));
	// Loop through every byte
	uint32_t shifted_val = (uint32_t)val;
	for (index_t i = 0U; i < access_size; i++) {
		vgic_gicr_sgi_set_sgi_ppi_priority(vic, gicr_vcpu, n + i,
						   (uint8_t)shifted_val);
		shifted_val >>= util_width(uint8_t);
	}
}

static void
gicr_vdevice_activer0_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			    register_t val)
{
	// 32-bit registers, 32-bit access only
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_active(
			vic, gicr_vcpu, i,
			(offset == offsetof(gicr_t, sgi.isactiver0)));
	}
}

static void
gicr_vdevice_pendr0_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			  register_t val)
{
	// 32-bit registers, 32-bit access only
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_pending(
			vic, gicr_vcpu, i,
			(offset == offsetof(gicr_t, sgi.ispendr0)));
	}
}

static void
gicr_vdevice_enabler0_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			    register_t val)
{
	// 32-bit registers, 32-bit access only
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_enable(
			vic, gicr_vcpu, i,
			(offset == offsetof(gicr_t, sgi.isenabler0)));
	}
}

static void
gicr_vdevice_igroupr0_write(vic_t *vic, thread_t *gicr_vcpu, register_t val)
{
	// 32-bit register, 32-bit access only
	for (index_t i = 0U; i < 32U; i++) {
		vgic_gicr_sgi_set_sgi_ppi_group(vic, gicr_vcpu, i,
						(val & util_bit(i)) != 0U);
	}
}

#if VGIC_HAS_EXT_PPIS
static void
gicr_vdevice_icfgr_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			   register_t val)
{
	// 32-bit register, 32-bit access only
	index_t n = (index_t)((offset - offsetof(gicr_t, sgi.icfgr_e)) /
			      sizeof(uint32_t));
	for (index_t i = (n * 16U); i < ((n + 1U) * 16U); i++) {
		vgic_gicr_sgi_set_ppi_config(
			vic, gicr_vcpu, GIC_PPI_EXT_BASE + i,
			(val & util_bit((i * 2U) + 1U)) != 0U);
	}
}

static void
gicr_vdevice_ipriorityr_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
				register_t val, size_t access_size)
{
	// 32-bit registers, byte or 32-bit accessible
	index_t n = (index_t)(offset - offsetof(gicr_t, sgi.ipriorityr_e));
	// Loop through every byte
	uint32_t shifted_val = (uint32_t)val;
	for (index_t i = n; i < (n + access_size); i++) {
		vgic_gicr_sgi_set_sgi_ppi_priority(vic, gicr_vcpu,
						   GIC_PPI_EXT_BASE + i,
						   (uint8_t)shifted_val);
		shifted_val >>= util_width(uint8_t);
	}
}

static void
gicr_vdevice_activer_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			     register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n =
		(index_t)((offset - (set ? offsetof(gicr_t, sgi.isactiver_e)
					 : offsetof(gicr_t, sgi.icactiver_e))) /
			  sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_active(
			vic, gicr_vcpu, GIC_PPI_EXT_BASE + (n * 32U) + i, set);
	}
}

static void
gicr_vdevice_pendr_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			   register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n =
		(index_t)((offset - (set ? offsetof(gicr_t, sgi.ispendr_e)
					 : offsetof(gicr_t, sgi.icpendr_e))) /
			  sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_pending(
			vic, gicr_vcpu, GIC_PPI_EXT_BASE + (n * 32U) + i, set);
	}
}

static void
gicr_vdevice_enabler_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			     register_t val, bool set)
{
	// 32-bit registers, 32-bit access only
	index_t n =
		(index_t)((offset - (set ? offsetof(gicr_t, sgi.isenabler_e)
					 : offsetof(gicr_t, sgi.icenabler_e))) /
			  sizeof(uint32_t));
	uint32_t bits = (uint32_t)val;
	while (bits != 0U) {
		index_t i = compiler_ctz(bits);
		bits &= ~((index_t)util_bit(i));

		vgic_gicr_sgi_change_sgi_ppi_enable(
			vic, gicr_vcpu, GIC_PPI_EXT_BASE + (n * 32U) + i, set);
	}
}

static void
gicr_vdevice_igroupr_e_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
			     register_t val)
{
	// 32-bit register, 32-bit access only
	index_t n = (index_t)((offset - offsetof(gicr_t, sgi.igroupr_e)) /
			      sizeof(uint32_t));
	for (index_t i = 0U; i < 32U; i++) {
		vgic_gicr_sgi_set_sgi_ppi_group(
			vic, gicr_vcpu, GIC_PPI_EXT_BASE + (n * 32U) + i,
			(val & util_bit(i)) != 0U);
	}
}
#endif // VGIC_HAS_EXT_PPIS

#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
static void
gicr_vdevice_invallr_write(vic_t *vic, thread_t *gicr_vcpu, register_t val)
{
	GICR_INVALLR_t invallr = GICR_INVALLR_cast(val);
	// WI if the virtual bit is set
	if (!GICR_INVALLR_get_V(&invallr)) {
		vgic_gicr_rd_invall(vic, gicr_vcpu);
	}
}

static void
gicr_vdevice_invlpir_write(vic_t *vic, thread_t *gicr_vcpu, register_t val)
{
	GICR_INVLPIR_t invlpir = GICR_INVLPIR_cast(val);
	// WI if the virtual bit is set
	if (!GICR_INVLPIR_get_V(&invlpir)) {
		vgic_gicr_rd_invlpi(vic, gicr_vcpu,
				    GICR_INVLPIR_get_pINTID(&invlpir));
	}
}
#endif

static bool
gicr_vdevice_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
		   register_t val, size_t access_size)
{
	bool ret = true;

	VGIC_TRACE(GICR_WRITE, vic, gicr_vcpu,
		   "GICR_WRITE reg = {:x}, val = {:#x}", offset, val);

	switch (offset) {
	case offsetof(gicr_t, rd.ctlr):
		vgic_gicr_rd_set_control(vic, gicr_vcpu,
					 GICR_CTLR_cast((uint32_t)val));
		break;
	case offsetof(gicr_t, rd.iidr):
	case offsetof(gicr_t, rd.typer):
	case offsetof(gicr_t, rd.syncr):
	case offsetof(gicr_t, PIDR2): {
		// RO registers
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_WROD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		break;
	}
	case offsetof(gicr_t, rd.statusr): {
		GICR_STATUSR_t statusr = GICR_STATUSR_cast((uint32_t)val);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, false);
		break;
	}
	case offsetof(gicr_t, rd.waker):
		vgic_gicr_rd_set_sleep(
			vic, gicr_vcpu,
			GICR_WAKER_get_ProcessorSleep(
				&GICR_WAKER_cast((uint32_t)val)));
		break;
	case offsetof(gicr_t, rd.setlpir):
	case offsetof(gicr_t, rd.clrlpir):
		// Direct LPIs not implemented, WI
		//
		// Implementing these is strictly required by the GICv3 spec
		// when the VCPU has LPI support but no ITS. We define that to
		// be a configuration error in VM provisioning.
		break;
#if VGIC_HAS_LPI
	case offsetof(gicr_t, rd.propbaser):
		vgic_gicr_rd_set_propbase(vic, GICR_PROPBASER_cast(val));
		break;
	case offsetof(gicr_t, rd.pendbaser):
		vgic_gicr_rd_set_pendbase(vic, gicr_vcpu,
					  GICR_PENDBASER_cast(val));
		break;
	case offsetof(gicr_t, rd.invlpir):
#if GICV3_HAS_VLPI_V4_1
		gicr_vdevice_invlpir_write(vic, gicr_vcpu, val);
#elif GICV3_HAS_ITS // && !GICV3_HAS_VLPI_V4_1
		// WI for GICv3 with ITS
#else		    // !GICV3_HAS_VLPI_V4_1 && !GICV3_HAS_ITS
#error GICv3 with LPIs but without ITS not supported.
#endif // !GICV3_HAS_VLPI_V4_1 && !GICV3_HAS_ITS
		break;
	case offsetof(gicr_t, rd.invallr):
#if GICV3_HAS_VLPI_V4_1
		gicr_vdevice_invallr_write(vic, gicr_vcpu, val);
#elif GICV3_HAS_ITS // && !GICV3_HAS_VLPI_V4_1
		// WI for GICv3 with ITS
#else		    // !GICV3_HAS_VLPI_V4_1 && !GICV3_HAS_ITS
#error GICv3 with LPIs but without ITS not supported.
#endif // !GICV3_HAS_VLPI_V4_1 && !GICV3_HAS_ITS
		break;
#endif // VGIC_HAS_LPI
	case offsetof(gicr_t, sgi.igroupr0):
		gicr_vdevice_igroupr0_write(vic, gicr_vcpu, val);
		break;
	case offsetof(gicr_t, sgi.isenabler0):
	case offsetof(gicr_t, sgi.icenabler0):
		gicr_vdevice_enabler0_write(vic, gicr_vcpu, offset, val);
		break;
	case offsetof(gicr_t, sgi.ispendr0):
	case offsetof(gicr_t, sgi.icpendr0):
		gicr_vdevice_pendr0_write(vic, gicr_vcpu, offset, val);
		break;
	case offsetof(gicr_t, sgi.isactiver0):
	case offsetof(gicr_t, sgi.icactiver0):
		gicr_vdevice_activer0_write(vic, gicr_vcpu, offset, val);
		break;
	case util_case_range(
		OFS_GICR_SGI_IPRIORITYR(0U),
		OFS_GICR_SGI_IPRIORITYR(GIC_PPI_BASE + GIC_PPI_NUM - 1)):
		gicr_vdevice_ipriorityr_write(vic, gicr_vcpu, offset, val,
					      access_size);
		break;
	case OFS_GICR_SGI_ICFGR(0U):
		// All interrupts in this register are SGIs, which are always
		// edge-triggered, so it is entirely WI
		break;
	case OFS_GICR_SGI_ICFGR(1U):
		gicr_vdevice_icfgr_write(vic, gicr_vcpu, val);
		break;
	case offsetof(gicr_t, sgi.igrpmodr0):
	case offsetof(gicr_t, sgi.nsacr):
		// WI
		break;
#if VGIC_HAS_EXT_PPIS
	case util_offset_case_range(gicr_t, sgi.igroupr_e):
		gicr_vdevice_igroupr_e_write(vic, gicr_vcpu, offset, val);
		break;
	case util_offset_case_range(gicr_t, sgi.isenabler_e):
		gicr_vdevice_enabler_e_write(vic, gicr_vcpu, offset, val, true);
		break;
	case util_offset_case_range(gicr_t, sgi.icenabler_e):
		gicr_vdevice_enabler_e_write(vic, gicr_vcpu, offset, val,
					     false);
		break;
	case util_offset_case_range(gicr_t, sgi.ispendr_e):
		gicr_vdevice_pendr_e_write(vic, gicr_vcpu, offset, val, true);
		break;
	case util_offset_case_range(gicr_t, sgi.icpendr_e):
		gicr_vdevice_pendr_e_write(vic, gicr_vcpu, offset, val, false);
		break;
	case util_offset_case_range(gicr_t, sgi.isactiver_e):
		gicr_vdevice_activer_e_write(vic, gicr_vcpu, offset, val, true);
		break;
	case util_offset_case_range(gicr_t, sgi.icactiver_e):
		gicr_vdevice_activer_e_write(vic, gicr_vcpu, offset, val,
					     false);
		break;
	case util_offset_case_range(gicr_t, sgi.ipriorityr_e):
		gicr_vdevice_ipriorityr_e_write(vic, gicr_vcpu, offset, val,
						access_size);
		break;
	case util_offset_case_range(gicr_t, sgi.icfgr_e):
		gicr_vdevice_icfgr_e_write(vic, gicr_vcpu, offset, val);
		break;
	case util_offset_case_range(gicr_t, sgi.igrpmodr_e):
		// WI
		break;
#endif
	default: {
		// Unknown register
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_WRD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		ret = false;
		break;
	}
	}

	return ret;
}

static bool
gicr_access_allowed(size_t size, size_t offset)
{
	bool ret;

	// First check if the access is size-aligned
	if ((offset & (size - 1U)) != 0UL) {
		ret = false;
	} else if (size == sizeof(uint64_t)) {
		ret = ((offset == offsetof(gicr_t, rd.invallr)) ||
		       (offset == offsetof(gicr_t, rd.invlpir)) ||
		       (offset == offsetof(gicr_t, rd.pendbaser)) ||
		       (offset == offsetof(gicr_t, rd.propbaser)) ||
		       (offset == offsetof(gicr_t, rd.setlpir)) ||
		       (offset == offsetof(gicr_t, rd.clrlpir)) ||
		       (offset == offsetof(gicr_t, rd.typer)));
	} else if (size == sizeof(uint32_t)) {
		// Word accesses, always allowed
		ret = true;
	} else if (size == sizeof(uint16_t)) {
		// Half-word accesses are not allowed for GICR registers
		ret = false;
	} else if (size == sizeof(uint8_t)) {
		// Byte accesses are only allowed for priority registers
		ret = (((offset >= OFS_GICR_SGI_IPRIORITYR(0U)) &&
			(offset <= OFS_GICR_SGI_IPRIORITYR(31U))));
#if VGIC_HAS_EXT_PPIS
		ret = ret ||
		      util_offset_in_range(offset, gicr_t, sgi.ipriorityr_e);
#endif
	} else {
		// Invalid access size
		ret = false;
	}

	return ret;
}

static vcpu_trap_result_t
vgic_handle_gicd_access(vic_t *vic, size_t offset, size_t access_size,
			register_t *value, bool is_write)
{
	bool access_ok = false;

	if (gicd_access_allowed(access_size, offset)) {
		if (is_write) {
			access_ok = gicd_vdevice_write(vic, offset, *value,
						       access_size);
		} else {
			access_ok = gicd_vdevice_read(vic, offset, value,
						      access_size);
		}
	}
	return access_ok ? VCPU_TRAP_RESULT_EMULATED : VCPU_TRAP_RESULT_FAULT;
}

static vcpu_trap_result_t
vgic_handle_gicr_access(vic_t *vic, thread_t *thread, size_t offset,
			size_t access_size, register_t *value, bool is_write,
			bool last_gicr)
{
	bool access_ok = false;

	if (gicr_access_allowed(access_size, offset)) {
		if (is_write) {
			access_ok = gicr_vdevice_write(vic, thread, offset,
						       *value, access_size);
		} else {
			access_ok = gicr_vdevice_read(vic, thread,
						      thread->vgic_gicr_index,
						      offset, value,
						      access_size, last_gicr);
		}
	}

	return access_ok ? VCPU_TRAP_RESULT_EMULATED : VCPU_TRAP_RESULT_FAULT;
}

vcpu_trap_result_t
vgic_handle_vdevice_access(vdevice_type_t type, vdevice_t *vdevice,
			   size_t offset, size_t access_size, register_t *value,
			   bool is_write)
{
	assert(vdevice != NULL);

	vcpu_trap_result_t ret;

	if (type == VDEVICE_TYPE_VGIC_GICD) {
		vic_t *vic = vic_container_of_gicd_device(vdevice);
		ret = vgic_handle_gicd_access(vic, offset, access_size, value,
					      is_write);
	} else {
		assert(type == VDEVICE_TYPE_VGIC_GICR);
		thread_t *gicr_vcpu =
			thread_container_of_vgic_gicr_device(vdevice);
		vic_t *vic = gicr_vcpu->vgic_vic;
		assert(vic != NULL);
		ret = vgic_handle_gicr_access(vic, gicr_vcpu, offset,
					      access_size, value, is_write,
					      gicr_vcpu->vgic_gicr_device_last);
	}

	return ret;
}

vcpu_trap_result_t
vgic_handle_vdevice_access_fixed_addr(vmaddr_t ipa, size_t access_size,
				      register_t *value, bool is_write)
{
	vcpu_trap_result_t ret;

	thread_t *thread = thread_get_self();
	vic_t	 *vic	 = thread->vgic_vic;

	if ((vic == NULL) || !vic->allow_fixed_vmaddr) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if ((ipa >= PLATFORM_GICD_BASE) &&
		   (ipa < (PLATFORM_GICD_BASE + 0x10000U))) {
		size_t offset = (size_t)(ipa - PLATFORM_GICD_BASE);
		ret = vgic_handle_gicd_access(vic, offset, access_size, value,
					      is_write);
	} else if ((ipa >= PLATFORM_GICR_BASE) &&
		   (ipa < (PLATFORM_GICR_BASE + ((vmaddr_t)PLATFORM_MAX_CORES
						 << GICR_STRIDE_SHIFT)))) {
		index_t gicr_num = (index_t)((ipa - PLATFORM_GICR_BASE) >>
					     GICR_STRIDE_SHIFT);
		if ((vic != NULL) && (gicr_num < vic->gicr_count)) {
			rcu_read_start();

			thread_t *gicr_vcpu =
				vgic_get_thread_by_gicr_index(vic, gicr_num);

			if (gicr_vcpu != NULL) {
				bool is_last =
					(gicr_num == (vic->gicr_count - 1U)) ||
					(atomic_load_relaxed(
						 &vic->gicr_vcpus[gicr_num +
								  1U]) == NULL);
				vmaddr_t gicr_base =
					((vmaddr_t)PLATFORM_GICR_BASE +
					 ((vmaddr_t)gicr_num
					  << GICR_STRIDE_SHIFT));
				size_t offset = (size_t)(ipa - gicr_base);
				ret	      = vgic_handle_gicr_access(
					vic, gicr_vcpu, offset, access_size,
					value, is_write, is_last);
			} else {
				ret = VCPU_TRAP_RESULT_UNHANDLED;
			}

			rcu_read_finish();
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
	} else {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	return ret;
}

error_t
vgic_handle_vdevice_trigger_msi(vdevice_t *vdevice, size_t offset,
				uint32_t message)
{
	assert(vdevice != NULL);
	vic_t *vic = vic_container_of_gicd_device(vdevice);

	error_t ret;

	if (offset == offsetof(gicd_t, setspi_nsr)) {
		vgic_gicd_change_irq_pending(
			vic,
			GICD_CLRSPI_SETSPI_NSR_SR_get_INTID(
				&GICD_CLRSPI_SETSPI_NSR_SR_cast(message)),
			true, true);
		ret = OK;
	} else {
		ret = ERROR_ADDR_INVALID;
	}

	return ret;
}
