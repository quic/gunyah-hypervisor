// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "internal.h"
#include "vgic.h"

// Qualcomm's JEP106 identifier is 0x70, with no continuation bytes. This is
// used in the virtual GICD_IIDR and GICR_IIDR.
#define JEP106_IDENTITY 0x70U
#define JEP106_CONTCODE 0x0U
#define IIDR_IMPLEMENTER                                                       \
	(((uint16_t)JEP106_CONTCODE << 8U) | (uint16_t)JEP106_IDENTITY)
#define IIDR_PRODUCTID (uint8_t)'G' /* For "Gunyah" */
#define IIDR_VARIANT   0U
#define IIDR_REVISION  0U

static register_t
vgic_read_irqbits(vic_t *vic, thread_t *vcpu, size_t base_offset, size_t offset)
{
	assert(vic != NULL);
	assert(vcpu != NULL);
	assert(offset >= base_offset);
	assert(offset <= base_offset + (31 * sizeof(uint32_t)));

	register_t bits = 0U;
	count_t	   range_base =
		(count_t)((offset - base_offset) / sizeof(uint32_t)) * 32U;
	count_t range_size =
		util_min(32U, GIC_SPECIAL_INTIDS_BASE - range_base);

	_Atomic vgic_delivery_state_t *dstates =
		vgic_find_dstate(vic, vcpu, range_base);
	if (dstates == NULL) {
		goto out;
	}
	assert(compiler_sizeof_object(dstates) >=
	       range_size * sizeof(*dstates));

	bool listed = false;

	for (count_t i = 0; i < range_size; i++) {
		vgic_delivery_state_t this_dstate =
			atomic_load_relaxed(&dstates[i]);
		bool bit;

		// Note: the GICR base offsets are the same as the GICD offsets,
		// so we don't need to duplicate them here.
		switch (base_offset) {
		case OFS_GICD_IGROUPR(0U):
			bit = vgic_delivery_state_get_group1(&this_dstate);
			break;
		case OFS_GICD_ISENABLER(0U):
		case OFS_GICD_ICENABLER(0U):
			bit = vgic_delivery_state_get_enabled(&this_dstate);
			break;
		case OFS_GICD_ISPENDR(0U):
		case OFS_GICD_ICPENDR(0U):
			bit = vgic_delivery_state_is_pending(&this_dstate);
			if (vgic_delivery_state_get_listed(&this_dstate)) {
				listed = true;
			}
			break;
		case OFS_GICD_ISACTIVER(0U):
		case OFS_GICD_ICACTIVER(0U):
			bit = vgic_delivery_state_get_active(&this_dstate);
			if (vgic_delivery_state_get_listed(&this_dstate)) {
				listed = true;
			}
			break;
		default:
			panic("vgic_read_irqbits: Bad base_offset");
		}

		if (bit) {
			bits |= util_bit(i);
		}
	}

#if GICV3_HAS_VLPI_V4_1 && defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	if ((range_base == GIC_SGI_BASE) &&
	    ((base_offset == offsetof(gicd_t, ispendr)) ||
	     (base_offset == offsetof(gicd_t, icpendr)))) {
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
			case OFS_GICD_ISPENDR(0U):
			case OFS_GICD_ICPENDR(0U):
				if ((state == ICH_LR_EL2_STATE_PENDING) ||
				    (state ==
				     ICH_LR_EL2_STATE_PENDING_ACTIVE)) {
					bits |= bit;
				} else {
					bits &= ~bit;
				}
				break;
			case OFS_GICD_ISACTIVER(0U):
			case OFS_GICD_ICACTIVER(0U):
				if ((state == ICH_LR_EL2_STATE_ACTIVE) ||
				    (state ==
				     ICH_LR_EL2_STATE_PENDING_ACTIVE)) {
					bits |= bit;
				} else {
					bits &= ~bit;
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

out:
	return bits;
}

static register_t
vgic_read_priority(vic_t *vic, thread_t *vcpu, size_t offset,
		   size_t access_size)
{
	register_t bits = 0U;

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
			<< (i * 8U);
	}

out:
	return bits;
}

static register_t
vgic_read_config(vic_t *vic, thread_t *vcpu, size_t offset)
{
	assert(vic != NULL);
	assert(vcpu != NULL);
	assert(offset <= (63 * sizeof(uint32_t)));

	register_t bits	      = 0U;
	count_t	   range_base = (count_t)(offset / sizeof(uint32_t)) * 16U;
	count_t	   range_size =
		util_min(16U, GIC_SPECIAL_INTIDS_BASE - range_base);

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
	return bits;
}

static bool
gicd_vdevice_read(vic_t *vic, size_t offset, register_t *val,
		  size_t access_size)
{
	bool	  ret	 = true;
	thread_t *thread = thread_get_self();

	assert(vic != NULL);

	if ((offset == offsetof(gicd_t, setspi_nsr)) ||
	    (offset == offsetof(gicd_t, clrspi_nsr)) ||
	    (offset == offsetof(gicd_t, setspi_sr)) ||
	    (offset == offsetof(gicd_t, clrspi_sr)) ||
	    (offset == offsetof(gicd_t, sgir))) {
		// WO registers, RAZ
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_RWOD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		*val = 0U;

	} else if (offset == offsetof(gicd_t, ctlr)) {
		*val = GICD_CTLR_DS_raw(atomic_load_relaxed(&vic->gicd_ctlr));

	} else if (offset == offsetof(gicd_t, statusr)) {
		*val = GICD_STATUSR_raw(vic->gicd_statusr);

	} else if (offset == offsetof(gicd_t, typer)) {
		GICD_TYPER_t typer = GICD_TYPER_default();
		GICD_TYPER_set_ITLinesNumber(
			&typer,
			(count_t)util_balign_up(GIC_SPI_NUM, 32U) / 32U);
		GICD_TYPER_set_MBIS(&typer, true);
#if VGIC_HAS_EXT_IRQS
#error Extended IRQs not yet implemented
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
		*val = GICD_TYPER_raw(typer);

	} else if (offset == offsetof(gicd_t, iidr)) {
		GICD_IIDR_t iidr = GICD_IIDR_default();
		GICD_IIDR_set_Implementer(&iidr, IIDR_IMPLEMENTER);
		GICD_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
		GICD_IIDR_set_Variant(&iidr, IIDR_VARIANT);
		GICD_IIDR_set_Revision(&iidr, IIDR_REVISION);
		*val = GICD_IIDR_raw(iidr);

	} else if (offset == offsetof(gicd_t, typer2)) {
		GICD_TYPER2_t typer2 = GICD_TYPER2_default();
#if GICV3_HAS_VLPI_V4_1
		GICD_TYPER2_set_nASSGIcap(&typer2, vgic_has_lpis(vic));
#endif
		*val = GICD_TYPER2_raw(typer2);

	} else if (offset == (size_t)OFS_GICD_PIDR2) {
		*val = VGIC_PIDR2;

	} else if ((offset >= OFS_GICD_IGROUPR(0U)) &&
		   (offset <= OFS_GICD_IGROUPR(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_IGROUPR(0),
					 offset);

	} else if ((offset >= OFS_GICD_ISENABLER(0U)) &&
		   (offset <= OFS_GICD_ISENABLER(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ISENABLER(0U),
					 offset);

	} else if ((offset >= OFS_GICD_ICENABLER(0U)) &&
		   (offset <= OFS_GICD_ICENABLER(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ICENABLER(0U),
					 offset);

	} else if ((offset >= OFS_GICD_ISPENDR(0U)) &&
		   (offset <= OFS_GICD_ISPENDR(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ISPENDR(0U),
					 offset);

	} else if ((offset >= OFS_GICD_ICPENDR(0U)) &&
		   (offset <= OFS_GICD_ICPENDR(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ICPENDR(0U),
					 offset);

	} else if ((offset >= OFS_GICD_ISACTIVER(0U)) &&
		   (offset <= OFS_GICD_ISACTIVER(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ISACTIVER(0U),
					 offset);

	} else if ((offset >= OFS_GICD_ICACTIVER(0U)) &&
		   (offset <= OFS_GICD_ICACTIVER(31U))) {
		*val = vgic_read_irqbits(vic, thread, OFS_GICD_ICACTIVER(0U),
					 offset);

	} else if (util_offset_in_range(offset, gicd_t, ipriorityr)) {
		*val = vgic_read_priority(vic, thread,
					  offset - offsetof(gicd_t, ipriorityr),
					  access_size);

	} else if (util_offset_in_range(offset, gicd_t, icfgr)) {
		*val = vgic_read_config(vic, thread,
					offset - offsetof(gicd_t, icfgr));

	} else if (util_offset_in_range(offset, gicd_t, itargetsr) ||
		   util_offset_in_range(offset, gicd_t, igrpmodr) ||
		   util_offset_in_range(offset, gicd_t, nsacr)) {
		// RAZ ranges
		*val = 0U;

	} else {
		// Unknown register
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_RRD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		*val = 0U;
	}

	return ret;
}

static bool
gicd_vdevice_write(vic_t *vic, size_t offset, register_t val,
		   size_t access_size)
{
	bool ret = true;

	assert(vic != NULL);
	VGIC_TRACE(GICD_WRITE, vic, NULL, "GICD_WRITE reg = {:x}, val = {:#x}",
		   offset, val);

	if (offset == offsetof(gicd_t, ctlr)) {
		vgic_gicd_set_control(vic, GICD_CTLR_DS_cast((uint32_t)val));

	} else if ((offset == offsetof(gicd_t, typer)) ||
		   (offset == offsetof(gicd_t, iidr)) ||
		   (offset == (size_t)OFS_GICD_PIDR2) ||
		   (offset == offsetof(gicd_t, typer2))) {
		// RO registers
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_WROD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);

	} else if (offset == offsetof(gicd_t, statusr)) {
		GICD_STATUSR_t statusr = GICD_STATUSR_cast((uint32_t)val);
		vgic_gicd_set_statusr(vic, statusr, false);

	} else if ((offset == offsetof(gicd_t, setspi_nsr)) ||
		   (offset == offsetof(gicd_t, clrspi_nsr))) {
		vgic_gicd_change_irq_pending(
			vic,
			GICD_CLRSPI_SETSPI_NSR_SR_get_INTID(
				&GICD_CLRSPI_SETSPI_NSR_SR_cast((uint32_t)val)),
			(offset == offsetof(gicd_t, setspi_nsr)), true);

	} else if ((offset == offsetof(gicd_t, setspi_sr)) ||
		   (offset == offsetof(gicd_t, clrspi_sr))) {
		// WI

	} else if ((offset >= OFS_GICD_IGROUPR(0U)) &&
		   (offset <= OFS_GICD_IGROUPR(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_IGROUPR(0U)) /
				      sizeof(uint32_t));
		for (index_t i = util_max(n * 32U, GIC_SPI_BASE);
		     i < util_min((n + 1U) * 32U, 1020U); i++) {
			vgic_gicd_set_irq_group(
				vic, i, (val & util_bit(i % 32U)) != 0U);
		}

	} else if ((offset >= OFS_GICD_ISENABLER(0U)) &&
		   (offset <= OFS_GICD_ISENABLER(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ISENABLER(0U)) /
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

				vgic_gicd_change_irq_enable(vic, (n * 32U) + i,
							    true);
			}
		}

	} else if ((offset >= OFS_GICD_ICENABLER(0U)) &&
		   (offset <= OFS_GICD_ICENABLER(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ICENABLER(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits (ICENABLER0)
		if (n != 0U) {
			uint32_t bits = (uint32_t)val;
			if (n == 31U) {
				// Ignore the bits for IRQs 1020-1023
				bits &= ~0xf0000000U;
			}
			while (bits != 0U) {
				index_t i = compiler_ctz(bits);
				bits &= ~((index_t)util_bit(i));

				vgic_gicd_change_irq_enable(vic, (n * 32U) + i,
							    false);
			}
		}

	} else if ((offset >= OFS_GICD_ISPENDR(0U)) &&
		   (offset <= OFS_GICD_ISPENDR(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ISPENDR(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits (ISPENDR0)
		if (n != 0U) {
			uint32_t bits = (uint32_t)val;
			if (n == 31U) {
				// Ignore the bits for IRQs 1020-1023
				bits &= ~0xf0000000U;
			}
			while (bits != 0U) {
				index_t i = compiler_ctz(bits);
				bits &= ~((index_t)util_bit(i));

				vgic_gicd_change_irq_pending(vic, (n * 32U) + i,
							     true, false);
			}
		}

	} else if ((offset >= OFS_GICD_ICPENDR(0U)) &&
		   (offset <= OFS_GICD_ICPENDR(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ICPENDR(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits (ICPENDR0)
		if (n != 0U) {
			uint32_t bits = (uint32_t)val;
			if (n == 31U) {
				// Ignore the bits for IRQs 1020-1023
				bits &= ~0xf0000000U;
			}
			while (bits != 0U) {
				index_t i = compiler_ctz(bits);
				bits &= ~((index_t)util_bit(i));

				vgic_gicd_change_irq_pending(vic, (n * 32U) + i,
							     false, false);
			}
		}

	} else if ((offset >= OFS_GICD_ISACTIVER(0U)) &&
		   (offset <= OFS_GICD_ISACTIVER(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ISACTIVER(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits (ISACTIVER0)
		if (n != 0U) {
			uint32_t bits = (uint32_t)val;
			if (n == 31U) {
				// Ignore the bits for IRQs 1020-1023
				bits &= ~0xf0000000U;
			}
			while (bits != 0U) {
				index_t i = compiler_ctz(bits);
				bits &= ~((index_t)util_bit(i));

				vgic_gicd_change_irq_active(vic, (n * 32U) + i,
							    true);
			}
		}

	} else if ((offset >= OFS_GICD_ICACTIVER(0U)) &&
		   (offset <= OFS_GICD_ICACTIVER(31U))) {
		// 32-bit registers, 32-bit access only

		index_t n = (index_t)((offset - OFS_GICD_ICACTIVER(0U)) /
				      sizeof(uint32_t));
		// Ignore writes to the SGI and PPI bits (ICACTIVER0)
		if (n != 0U) {
			uint32_t bits = (uint32_t)val;
			if (n == 31U) {
				// Ignore the bits for IRQs 1020-1023
				bits &= ~0xf0000000U;
			}
			while (bits != 0U) {
				index_t i = compiler_ctz(bits);
				bits &= ~((index_t)util_bit(i));

				vgic_gicd_change_irq_active(vic, (n * 32U) + i,
							    false);
			}
		}

	} else if ((offset >= OFS_GICD_IPRIORITYR(0U)) &&
		   (offset <= OFS_GICD_IPRIORITYR(1019U))) {
		// 32-bit registers, byte or 32-bit accessible

		index_t n = (index_t)(offset - OFS_GICD_IPRIORITYR(0U));
		// Loop through every byte
		uint32_t shifted_val = (uint32_t)val;
		for (index_t i = util_max(n, GIC_SPI_BASE); i < n + access_size;
		     i++) {
			vgic_gicd_set_irq_priority(vic, i,
						   (uint8_t)shifted_val);
			shifted_val >>= 8U;
		}
	} else if ((offset >= OFS_GICD_ITARGETSR(0U)) &&
		   (offset <= OFS_GICD_ITARGETSR(1019U))) {
		// WI

	} else if ((offset >= OFS_GICD_ICFGR(0U)) &&
		   (offset <= OFS_GICD_ICFGR(63U))) {
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

	} else if ((offset >= OFS_GICD_IGRPMODR(0U)) &&
		   (offset <= OFS_GICD_IGRPMODR(31U))) {
		// WI

	} else if ((offset >= OFS_GICD_NSACR(0U)) &&
		   (offset <= OFS_GICD_NSACR(63U))) {
		// WI

	} else if (offset == offsetof(gicd_t, sgir)) {
		// WI

	} else if ((offset >= OFS_GICD_CPENDSGIR(0U)) &&
		   (offset <= OFS_GICD_CPENDSGIR(15U))) {
		// WI

	} else if ((offset >= OFS_GICD_SPENDSGIR(0U)) &&
		   (offset <= OFS_GICD_SPENDSGIR(15U))) {
		// WI

	} else if ((offset >= OFS_GICD_IROUTER(0U)) &&
		   (offset <= OFS_GICD_IROUTER(GIC_SPI_NUM - 1))) {
		// 64-bit registers with 64-bit access only

		index_t spi = GIC_SPI_BASE +
			      (index_t)((offset - OFS_GICD_IROUTER(0U)) /
					sizeof(uint64_t));
		GICD_IROUTER_t irouter = GICD_IROUTER_cast(val);
		vgic_gicd_set_irq_router(vic, spi,
					 GICD_IROUTER_get_Aff0(&irouter),
					 GICD_IROUTER_get_Aff1(&irouter),
					 GICD_IROUTER_get_Aff2(&irouter),
					 GICD_IROUTER_get_Aff3(&irouter),
					 GICD_IROUTER_get_IRM(&irouter));

	}
#if GICV3_HAS_GICD_ICLAR
	else if (offset == OFS_GICD_SETCLASSR) {
		GICD_SETCLASSR_t setclassr = GICD_SETCLASSR_cast((uint32_t)val);
		virq_t		 virq	   = GICD_SETCLASSR_get_SPI(&setclassr);
		if (vgic_irq_is_spi(virq)) {
			vgic_gicd_set_irq_classes(
				vic, virq,
				GICD_SETCLASSR_get_Class0(&setclassr),
				GICD_SETCLASSR_get_Class1(&setclassr));
		}
	}
#endif
#if VGIC_HAS_EXT_IRQS
#error extended SPI support not implemented
#endif
#if VGIC_IGNORE_ARRAY_OVERFLOWS
	else if ((offset >= OFS_GICD_IPRIORITYR(1020U)) &&
		 (offset <= OFS_GICD_IPRIORITYR(1023U))) {
		// Ignore priority writes for special IRQs
	} else if ((offset >= OFS_GICD_IROUTER(GIC_SPI_NUM)) &&
		   (offset <= OFS_GICD_IROUTER(1023U))) {
		// Ignore route writes for special IRQs
	}
#endif
	else {
		// Unknown register
		GICD_STATUSR_t statusr;
		GICD_STATUSR_init(&statusr);
		GICD_STATUSR_set_WRD(&statusr, true);
		vgic_gicd_set_statusr(vic, statusr, true);
		ret = false;
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
		// Doubleword accesses are only allowed for routing registers
		ret = ((offset >= OFS_GICD_IROUTER(0U)) &&
		       (offset <= OFS_GICD_IROUTER(GIC_SPI_NUM - 1U)));
#if VGIC_IGNORE_ARRAY_OVERFLOWS
		// Ignore route accesses for special IRQs
		if ((offset >= OFS_GICD_IROUTER(0U)) &&
		    (offset <= OFS_GICD_IROUTER(1023U))) {
			ret = true;
		}
#endif
	} else if (size == sizeof(uint32_t)) {
		// Word accesses, always allowed
		ret = true;
	} else if (size == sizeof(uint16_t)) {
		// Half-word accesses are only allowed for the SETSPI and CLRSPI
		// registers
		ret = ((offset == offsetof(gicd_t, setspi_nsr)) ||
		       (offset == offsetof(gicd_t, clrspi_nsr)));
	} else if (size == sizeof(uint8_t)) {
		// Byte accesses are only allowed for priority, target and
		// SGI pending registers
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
		if ((offset >= OFS_GICD_IROUTER(0U)) &&
		    (offset <= OFS_GICD_IROUTER(1023U))) {
			ret = true;
		}
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

	if ((offset == offsetof(gicr_t, rd.setlpir)) ||
	    (offset == offsetof(gicr_t, rd.clrlpir)) ||
	    (offset == offsetof(gicr_t, rd.invlpir)) ||
	    (offset == offsetof(gicr_t, rd.invallr))) {
		// WO registers, RAZ
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_RWOD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		*val = 0U;

	} else if (util_balign_down(offset, sizeof(GICR_TYPER_t)) ==
		   offsetof(gicr_t, rd.typer)) {
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

		// The Processor Number is used only to select the target GICR
		// in ITS commands. When ARE is disabled, it also determines the
		// CPU's bit in ITARGETSR, but we don't support that. So it is
		// safe for this to be the logical VCPU index.
		GICR_TYPER_set_Processor_Num(&typer, gicr_num);
#if VGIC_HAS_LPI
		GICR_TYPER_set_PLPIS(&typer, vgic_has_lpis(vic));
#endif
		*val = GICR_TYPER_raw(typer);

		if (offset != offsetof(gicr_t, rd.typer)) {
			// Must be a 32-bit access to the big end
			assert(offset == OFS_GICR_RD_TYPER + sizeof(uint32_t));
			*val >>= 32U;
		}

	} else if (offset == offsetof(gicr_t, rd.iidr)) {
		GICR_IIDR_t iidr = GICR_IIDR_default();
		GICR_IIDR_set_Implementer(&iidr, IIDR_IMPLEMENTER);
		GICR_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
		GICR_IIDR_set_Variant(&iidr, IIDR_VARIANT);
		GICR_IIDR_set_Revision(&iidr, IIDR_REVISION);
		*val = GICR_IIDR_raw(iidr);

	} else if (offset == offsetof(gicr_t, PIDR2)) {
		*val = VGIC_PIDR2;

	} else if (offset == offsetof(gicr_t, rd.ctlr)) {
		*val = GICR_CTLR_raw(vgic_gicr_rd_get_control(vic, gicr_vcpu));

	} else if (offset == offsetof(gicr_t, rd.statusr)) {
		*val = GICR_STATUSR_raw(
			atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_statusr));

	} else if (offset == offsetof(gicr_t, rd.waker)) {
		GICR_WAKER_t gicr_waker = GICR_WAKER_default();
		GICR_WAKER_set_ProcessorSleep(
			&gicr_waker,
			atomic_load_relaxed(&gicr_vcpu->vgic_sleep));
		GICR_WAKER_set_ChildrenAsleep(
			&gicr_waker, vgic_gicr_rd_check_sleep(gicr_vcpu));

		*val = GICR_WAKER_raw(gicr_waker);

	} else if (offset == offsetof(gicr_t, rd.propbaser)) {
#if VGIC_HAS_LPI
		*val = GICR_PROPBASER_raw(
			atomic_load_relaxed(&vic->gicr_rd_propbaser));
#else
		*val = 0U;
#endif

	} else if (offset == offsetof(gicr_t, rd.pendbaser)) {
#if VGIC_HAS_LPI
		GICR_PENDBASER_t pendbase =
			atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_pendbaser);
		// The PTZ bit is specified as WO/RAZ, but we use it to cache
		// the written value which is used when EnableLPIs is set to 1.
		// Therefore we must clear it here.
		GICR_PENDBASER_set_PTZ(&pendbase, false);
		*val = GICR_PENDBASER_raw(pendbase);
#else
		*val = 0U;
#endif

	} else if (offset == offsetof(gicr_t, rd.syncr)) {
#if VGIC_HAS_LPI
		GICR_SYNCR_t syncr = GICR_SYNCR_default();
		GICR_SYNCR_set_Busy(&syncr,
				    vgic_gicr_get_inv_pending(vic, gicr_vcpu));
		*val = GICR_SYNCR_raw(syncr);
#else
		*val = 0U;
#endif

	} else if ((offset == offsetof(gicr_t, sgi.igroupr0)) ||
		   (offset == offsetof(gicr_t, sgi.isenabler0)) ||
		   (offset == offsetof(gicr_t, sgi.icenabler0)) ||
		   (offset == offsetof(gicr_t, sgi.ispendr0)) ||
		   (offset == offsetof(gicr_t, sgi.icpendr0)) ||
		   (offset == offsetof(gicr_t, sgi.isactiver0)) ||
		   (offset == offsetof(gicr_t, sgi.icactiver0))) {
		*val = vgic_read_irqbits(vic, gicr_vcpu,
					 offset - offsetof(gicr_t, sgi),
					 offset - offsetof(gicr_t, sgi));

	} else if ((offset == offsetof(gicr_t, sgi.igrpmodr0)) ||
		   (offset == offsetof(gicr_t, sgi.nsacr))) {
		// RAZ/WI because GICD_CTLR.DS==1
		*val = 0U;

	} else if (util_offset_in_range(offset, gicr_t, sgi.ipriorityr)) {
		*val = vgic_read_priority(
			vic, gicr_vcpu,
			offset - offsetof(gicr_t, sgi.ipriorityr), access_size);

	} else if (util_offset_in_range(offset, gicr_t, sgi.icfgr)) {
		*val = vgic_read_config(vic, gicr_vcpu,
					offset - offsetof(gicr_t, sgi.icfgr));

	} else {
		// Unknown register
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_RRD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		*val = 0U;
	}

	return ret;
}

static bool
gicr_vdevice_write(vic_t *vic, thread_t *gicr_vcpu, size_t offset,
		   register_t val, size_t access_size)
{
	bool ret = true;

	VGIC_TRACE(GICR_WRITE, vic, gicr_vcpu,
		   "GICR_WRITE reg = {:x}, val = {:#x}", offset, val);

	if (offset == offsetof(gicr_t, rd.ctlr)) {
		vgic_gicr_rd_set_control(vic, gicr_vcpu,
					 GICR_CTLR_cast((uint32_t)val));

	} else if ((offset == offsetof(gicr_t, rd.iidr)) ||
		   (offset == offsetof(gicr_t, rd.typer)) ||
		   (offset == offsetof(gicr_t, rd.syncr)) ||
		   (offset == offsetof(gicr_t, PIDR2))) {
		// RO registers
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_WROD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);

	} else if (offset == offsetof(gicr_t, rd.statusr)) {
		GICR_STATUSR_t statusr = GICR_STATUSR_cast((uint32_t)val);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, false);

	} else if (offset == offsetof(gicr_t, rd.waker)) {
		bool new_sleep = GICR_WAKER_get_ProcessorSleep(
			&GICR_WAKER_cast((uint32_t)val));
#if VGIC_HAS_1N
		bool old_sleep = atomic_exchange_explicit(
			&gicr_vcpu->vgic_sleep, new_sleep,
			memory_order_relaxed);
		if (old_sleep && !new_sleep) {
			// Leaving sleep, so clear any pending 1-of-N wakeup.
			scheduler_lock(gicr_vcpu);
			gicr_vcpu->vgic_wakeup_1n = false;
			scheduler_unlock(gicr_vcpu);
		}
#else
		atomic_store_relaxed(&gicr_vcpu->vgic_sleep, new_sleep);
#endif

	} else if ((offset == offsetof(gicr_t, rd.setlpir)) ||
		   (offset == offsetof(gicr_t, rd.clrlpir))) {
		// Direct LPIs not implemented, WI
		//
		// Implementing these is strictly required by the GICv3 spec
		// when the VCPU has LPI support but no ITS. We define that to
		// be a configuration error in VM provisioning.

#if VGIC_HAS_LPI
	} else if (offset == offsetof(gicr_t, rd.propbaser)) {
		vgic_gicr_rd_set_propbase(vic, GICR_PROPBASER_cast(val));

	} else if (offset == offsetof(gicr_t, rd.pendbaser)) {
		vgic_gicr_rd_set_pendbase(vic, gicr_vcpu,
					  GICR_PENDBASER_cast(val));

	} else if (offset == offsetof(gicr_t, rd.invlpir)) {
		GICR_INVLPIR_t invlpir = GICR_INVLPIR_cast(val);
		// WI if the virtual bit is set
		if (!GICR_INVLPIR_get_V(&invlpir)) {
			vgic_gicr_rd_invlpi(vic, gicr_vcpu,
					    GICR_INVLPIR_get_pINTID(&invlpir));
		}

	} else if (offset == offsetof(gicr_t, rd.invallr)) {
		GICR_INVALLR_t invallr = GICR_INVALLR_cast(val);
		// WI if the virtual bit is set
		if (!GICR_INVALLR_get_V(&invallr)) {
			vgic_gicr_rd_invall(vic, gicr_vcpu);
		}
#endif // VGIC_HAS_LPI

	} else if (offset == offsetof(gicr_t, sgi.igroupr0)) {
		// 32-bit register, 32-bit access only
		for (index_t i = 0U; i < 32U; i++) {
			vgic_gicr_sgi_set_sgi_ppi_group(
				vic, gicr_vcpu, i, (val & util_bit(i)) != 0U);
		}

	} else if ((offset == offsetof(gicr_t, sgi.isenabler0)) ||
		   (offset == offsetof(gicr_t, sgi.icenabler0))) {
		// 32-bit registers, 32-bit access only
		uint32_t bits = (uint32_t)val;
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicr_sgi_change_sgi_ppi_enable(
				vic, gicr_vcpu, i,
				(offset == offsetof(gicr_t, sgi.isenabler0)));
		}

	} else if ((offset == offsetof(gicr_t, sgi.ispendr0)) ||
		   (offset == offsetof(gicr_t, sgi.icpendr0))) {
		// 32-bit registers, 32-bit access only
		uint32_t bits = (uint32_t)val;
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicr_sgi_change_sgi_ppi_pending(
				vic, gicr_vcpu, i,
				(offset == offsetof(gicr_t, sgi.ispendr0)));
		}

	} else if ((offset == offsetof(gicr_t, sgi.isactiver0)) ||
		   (offset == offsetof(gicr_t, sgi.icactiver0))) {
		// 32-bit registers, 32-bit access only
		uint32_t bits = (uint32_t)val;
		while (bits != 0U) {
			index_t i = compiler_ctz(bits);
			bits &= ~((index_t)util_bit(i));

			vgic_gicr_sgi_change_sgi_ppi_active(
				vic, gicr_vcpu, i,
				(offset == offsetof(gicr_t, sgi.isactiver0)));
		}

	} else if ((offset >= OFS_GICR_SGI_IPRIORITYR(0U)) &&
		   (offset <=
		    OFS_GICR_SGI_IPRIORITYR(GIC_PPI_BASE + GIC_PPI_NUM - 1))) {
		// 32-bit registers, byte or 32-bit accessible
		index_t n = (index_t)(offset - OFS_GICR_SGI_IPRIORITYR(0U));
		// Loop through every byte
		uint32_t shifted_val = (uint32_t)val;
		for (index_t i = 0U; i < access_size; i++) {
			vgic_gicr_sgi_set_sgi_ppi_priority(
				vic, gicr_vcpu, n + i, (uint8_t)shifted_val);
			shifted_val >>= 8U;
		}

	} else if (offset == OFS_GICR_SGI_ICFGR(0U)) {
		// All interrupts in this register are SGIs, which are always
		// edge-triggered, so it is entirely WI

	} else if (offset == OFS_GICR_SGI_ICFGR(1U)) {
		// 32-bit register, 32-bit access only
		for (index_t i = 0U; i < GIC_PPI_NUM; i++) {
			vgic_gicr_sgi_set_ppi_config(
				vic, gicr_vcpu, i + GIC_PPI_BASE,
				(val & util_bit((i * 2U) + 1U)) != 0U);
		}

	} else if (offset == offsetof(gicr_t, sgi.igrpmodr0)) {
		// WI

	} else if (offset == offsetof(gicr_t, sgi.nsacr)) {
		// WI

	}
#if VGIC_HAS_EXT_IRQS
#error extended PPI support not implemented
#endif
	else {
		// Unknown register
		GICR_STATUSR_t statusr;
		GICR_STATUSR_init(&statusr);
		GICR_STATUSR_set_WRD(&statusr, true);
		vgic_gicr_rd_set_statusr(gicr_vcpu, statusr, true);
		ret = false;
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
		       (offset <= offsetof(gicr_t, rd.invlpir)) ||
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
		   (ipa < PLATFORM_GICD_BASE + 0x10000U)) {
		size_t offset = (size_t)(ipa - PLATFORM_GICD_BASE);
		ret = vgic_handle_gicd_access(vic, offset, access_size, value,
					      is_write);
	} else if ((ipa >= PLATFORM_GICR_BASE) &&
		   (ipa < PLATFORM_GICR_BASE + ((vmaddr_t)PLATFORM_MAX_CORES
						<< GICR_STRIDE_SHIFT))) {
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
