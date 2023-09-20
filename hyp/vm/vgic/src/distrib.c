// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypconstants.h>
#include <hypcontainers.h>
#include <hypregisters.h>
#include <hyprights.h>

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
#include <pgtable.h>
#include <platform_cpu.h>
#include <platform_irq.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vdevice.h>
#include <vic.h>
#include <virq.h>

#include <events/vic.h>
#include <events/virq.h>

#include <asm/nospec_checks.h>

#if defined(ARCH_ARM_FEAT_FGT) && ARCH_ARM_FEAT_FGT
#include <arm_fgt.h>
#endif

#include "event_handlers.h"
#include "gicv3.h"
#include "internal.h"
#include "useraccess.h"
#include "vgic.h"
#include "vic_base.h"

error_t
vgic_handle_object_create_vic(vic_create_t vic_create)
{
	vic_t *vic = vic_create.vic;
	assert(vic != NULL);
	partition_t *partition = vic->header.partition;
	assert(partition != NULL);

	vic->gicr_count	   = 1U;
	vic->sources_count = 0U;

	spinlock_init(&vic->gicd_lock);
	spinlock_init(&vic->search_lock);

	// Use the DS (disable security) version of GICD_CTLR, because we don't
	// implement security states in the virtual GIC. Note that the DS bit is
	// constant true in this bitfield type.
	GICD_CTLR_DS_t ctlr = GICD_CTLR_DS_default();
	// The virtual GIC has no legacy mode support.
	GICD_CTLR_DS_set_ARE(&ctlr, true);
#if VGIC_HAS_1N
	// We currently don't implement E1NWF=0.
	// FIXME:
	GICD_CTLR_DS_set_E1NWF(&ctlr, true);
#endif
	atomic_init(&vic->gicd_ctlr, ctlr);

	// If not configured otherwise, default to using the same MPIDR mapping
	// as the hardware
	vic->mpidr_mapping = platform_cpu_get_mpidr_mapping();

	return OK;
}

error_t
vic_configure(vic_t *vic, count_t max_vcpus, count_t max_virqs,
	      count_t max_msis, bool allow_fixed_vmaddr)
{
	error_t err = OK;

	vic->allow_fixed_vmaddr = allow_fixed_vmaddr;

	if ((max_vcpus == 0U) || (max_vcpus > PLATFORM_MAX_CORES)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	vic->gicr_count = max_vcpus;

	if (max_virqs > GIC_SPI_NUM) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	vic->sources_count = max_virqs;

#if VGIC_HAS_LPI
	if ((max_msis + GIC_LPI_BASE) >= util_bit(VGIC_IDBITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	vic->gicd_idbits = compiler_msb(max_msis + GIC_LPI_BASE - 1U) + 1U;
#else
	if (max_msis != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}
#endif

out:
	return err;
}

bool
vgic_has_lpis(vic_t *vic)
{
#if VGIC_HAS_LPI
	return vic->gicd_idbits >= 14U;
#else
	(void)vic;
	return false;
#endif
}

error_t
vgic_handle_object_activate_vic(vic_t *vic)
{
	partition_t *partition = vic->header.partition;
	assert(partition != NULL);
	error_t		  err = OK;
	void_ptr_result_t alloc_r;

	assert(vic->sources_count <= GIC_SPI_NUM);
	size_t sources_size = sizeof(vic->sources[0U]) * vic->sources_count;

	assert(vic->gicr_count > 0U);
	assert(vic->gicr_count <= PLATFORM_MAX_CORES);
	size_t vcpus_size = sizeof(vic->gicr_vcpus[0U]) * vic->gicr_count;

#if VGIC_HAS_LPI
	if (vgic_has_lpis(vic)) {
		size_t vlpi_propbase_size =
			util_bit(vic->gicd_idbits) - GIC_LPI_BASE;
		size_t vlpi_propbase_align =
			util_bit(GIC_ITS_CMD_VMAPP_VCONF_ADDR_PRESHIFT);
		alloc_r = partition_alloc(vic->header.partition,
					  vlpi_propbase_size,
					  vlpi_propbase_align);
		if (alloc_r.e != OK) {
			err = alloc_r.e;
			goto out;
		}
		// No need for a memset here; the first time a VM enables LPIs
		// we will memcpy the table from VM memory (and zero the rest
		// of the table if necessary) before sending a VMAPP command.
		// The vlpi_config_valid flag indicates that this has been done
		vic->vlpi_config_table = alloc_r.r;
	}
#endif

	if (sources_size != 0U) {
		alloc_r = partition_alloc(partition, sources_size,
					  alignof(vic->sources[0U]));
		if (alloc_r.e != OK) {
			err = alloc_r.e;
			goto out;
		}
		(void)memset_s(alloc_r.r, sources_size, 0, sources_size);
		vic->sources = (virq_source_t *_Atomic *)alloc_r.r;
	}

	alloc_r = partition_alloc(partition, vcpus_size,
				  alignof(vic->gicr_vcpus[0U]));
	if (alloc_r.e != OK) {
		err = alloc_r.e;
		goto out;
	}
	(void)memset_s(alloc_r.r, vcpus_size, 0, vcpus_size);

	vic->gicr_vcpus = (thread_t *_Atomic *)alloc_r.r;

out:
	// We can't free anything here; it will be done in cleanup

	return err;
}

error_t
vgic_handle_addrspace_attach_vdevice(addrspace_t *addrspace,
				     cap_id_t vdevice_object_cap, index_t index,
				     vmaddr_t vbase, size_t size,
				     addrspace_attach_vdevice_flags_t flags)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vic_ptr_result_t vic_r = cspace_lookup_vic(
		cspace, vdevice_object_cap, CAP_RIGHTS_VIC_ATTACH_VDEVICE);
	if (compiler_unexpected(vic_r.e) != OK) {
		err = vic_r.e;
		goto out;
	}

	index_result_t index_r =
		nospec_range_check(index, vic_r.r->gicr_count + 1U);
	if (index_r.e != OK) {
		err = ERROR_ARGUMENT_INVALID;
		goto out_ref;
	}

	spinlock_acquire(&vic_r.r->gicd_lock);

	if (index_r.r == 0U) {
		// Attaching the GICD registers.
		if (flags.raw != 0U) {
			err = ERROR_ARGUMENT_INVALID;
			goto out_locked;
		}

		if (vic_r.r->gicd_device.type != VDEVICE_TYPE_NONE) {
			err = ERROR_BUSY;
			goto out_locked;
		}
		vic_r.r->gicd_device.type = VDEVICE_TYPE_VGIC_GICD;

		err = vdevice_attach_vmaddr(&vic_r.r->gicd_device, addrspace,
					    vbase, size);
		if (err != OK) {
			vic_r.r->gicd_device.type = VDEVICE_TYPE_NONE;
		}
	} else {
		// Attaching GICR registers for a specific VCPU.
		if (!vgic_gicr_attach_flags_is_clean(flags.vgic_gicr)) {
			err = ERROR_ARGUMENT_INVALID;
			goto out_locked;
		}

		rcu_read_start();
		thread_t *gicr_vcpu = atomic_load_consume(
			&vic_r.r->gicr_vcpus[index_r.r - 1U]);

		if (gicr_vcpu == NULL) {
			err = ERROR_IDLE;
			goto out_gicr_rcu;
		}

		if (gicr_vcpu->vgic_gicr_device.type != VDEVICE_TYPE_NONE) {
			err = ERROR_BUSY;
			goto out_gicr_rcu;
		}

		if (vgic_gicr_attach_flags_get_last_valid(&flags.vgic_gicr)) {
			gicr_vcpu->vgic_gicr_device_last =
				vgic_gicr_attach_flags_get_last(
					&flags.vgic_gicr);
		} else {
			// Last flag is unspecified; set it by default if this
			// is the highest-numbered GICR, which matches the old
			// behaviour.
			gicr_vcpu->vgic_gicr_device_last =
				(index_r.r == vic_r.r->gicr_count);
		}

		gicr_vcpu->vgic_gicr_device.type = VDEVICE_TYPE_VGIC_GICR;
		err = vdevice_attach_vmaddr(&gicr_vcpu->vgic_gicr_device,
					    addrspace, vbase, size);
		if (err != OK) {
			gicr_vcpu->vgic_gicr_device.type = VDEVICE_TYPE_NONE;
		}

	out_gicr_rcu:
		rcu_read_finish();
	}

out_locked:
	spinlock_release(&vic_r.r->gicd_lock);
out_ref:
	object_put_vic(vic_r.r);
out:
	return err;
}

void
vgic_handle_object_deactivate_vic(vic_t *vic)
{
	// We shouldn't be here if there are any GICRs attached
	for (index_t i = 0; i < vic->gicr_count; i++) {
		assert(atomic_load_relaxed(&vic->gicr_vcpus[i]) == NULL);
	}

	rcu_read_start();
	for (index_t i = 0; i < vic->sources_count; i++) {
		virq_source_t *virq_source =
			atomic_load_consume(&vic->sources[i]);

		if (virq_source == NULL) {
			continue;
		}

		vic_unbind(virq_source);
	}
	rcu_read_finish();

	if (vic->gicd_device.type != VDEVICE_TYPE_NONE) {
		vdevice_detach_vmaddr(&vic->gicd_device);
	}
}

void
vgic_handle_object_cleanup_vic(vic_t *vic)
{
	partition_t *partition = vic->header.partition;

	if (vic->gicr_vcpus != NULL) {
		size_t vcpus_size =
			sizeof(vic->gicr_vcpus[0]) * vic->gicr_count;
		(void)partition_free(partition, vic->gicr_vcpus, vcpus_size);
		vic->gicr_vcpus = NULL;
	}

	if (vic->sources != NULL) {
		size_t sources_size =
			sizeof(vic->sources[0]) * vic->sources_count;
		(void)partition_free(partition, vic->sources, sources_size);
		vic->sources = NULL;
	}

#if VGIC_HAS_LPI
	if (vic->vlpi_config_table != NULL) {
		size_t vlpi_propbase_size =
			util_bit(vic->gicd_idbits) - GIC_LPI_BASE;
		(void)partition_free(vic->header.partition,
				     vic->vlpi_config_table,
				     vlpi_propbase_size);
		vic->vlpi_config_table = NULL;
	}
#endif
}

error_t
vic_attach_vcpu(vic_t *vic, thread_t *vcpu, index_t index)
{
	assert(atomic_load_relaxed(&vcpu->header.state) == OBJECT_STATE_INIT);
	assert(atomic_load_relaxed(&vic->header.state) == OBJECT_STATE_ACTIVE);

	error_t err;

	if (vcpu->kind != THREAD_KIND_VCPU) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (index >= vic->gicr_count) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	err = OK;

	if (vcpu->vgic_vic != NULL) {
		object_put_vic(vcpu->vgic_vic);
	}

	vcpu->vgic_vic	      = object_get_vic_additional(vic);
	vcpu->vgic_gicr_index = index;

out:
	return err;
}

error_t
vgic_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *vcpu = thread_create.thread;
	assert(vcpu != NULL);

	spinlock_init(&vcpu->vgic_lr_owner_lock.lock);
	atomic_store_relaxed(&vcpu->vgic_lr_owner_lock.owner,
			     CPU_INDEX_INVALID);

	if (vcpu->kind == THREAD_KIND_VCPU) {
#if VGIC_HAS_LPI
		GICR_CTLR_t ctlr = GICR_CTLR_default();
		GICR_CTLR_set_IR(&ctlr, true);
		atomic_store_relaxed(&vcpu->vgic_gicr_rd_ctlr, ctlr);
#endif

		// The sleep flag is initially clear. This has no real effect on
		// guests with GICR_WAKER awareness (like Linux), but allows
		// interrupt delivery to work correctly for guests that assume
		// they have a non-secure view of the GIC (like UEFI).
		atomic_init(&vcpu->vgic_sleep, false);

		vcpu->vgic_ich_hcr = ICH_HCR_EL2_default();

		// Trap changes to the group enable bits.
#if defined(ARCH_ARM_FEAT_FGT) && ARCH_ARM_FEAT_FGT
		if (arm_fgt_is_allowed()) {
			// Use fine-grained traps of the enable registers if
			// they are available, so we don't have to emulate the
			// other registers trapped by TALL[01].
			HFGWTR_EL2_set_ICC_IGRPENn_EL1(
				&vcpu->vcpu_regs_el2.hfgwtr_el2, true);
		} else
#endif
		{
			// Trap all accesses for disabled groups. Note that
			// these traps and the group disable maintenance IRQs
			// are toggled every time we update the group enables.
			//
			// We can't use the group enable maintenance IRQs,
			// because their latency is high enough that a VCPU's
			// idle loop might enable the groups and then disable
			// them again before we know they've been enabled,
			// causing it to get stuck in a loop being woken by IRQs
			// that are never delivered.
			ICH_HCR_EL2_set_TALL0(&vcpu->vgic_ich_hcr, true);
			ICH_HCR_EL2_set_TALL1(&vcpu->vgic_ich_hcr, true);
		}

		// Always set LRENPIE, and keep UIE off. This is because we
		// don't reload active interrupts into the LRs once they've been
		// kicked out; the complexity of doing that outweighs any
		// performance benefit, especially when most VMs are Linux -
		// which uses neither EOImode (in EL1) nor preemption, and
		// therefore will never have multiple active IRQs to trigger
		// this in the first place.
		ICH_HCR_EL2_set_UIE(&vcpu->vgic_ich_hcr, false);
		ICH_HCR_EL2_set_LRENPIE(&vcpu->vgic_ich_hcr, true);
#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
		// We don't know whether to set vSGIEOICount until the VM
		// enables groups in GICD_CTLR, at which point we must propagate
		// the nASSGIreq bit from the same register to all the vCPUs.
		// That is done in vgic_gicr_update_group_enables().
		ICH_HCR_EL2_set_vSGIEOICount(&vcpu->vgic_ich_hcr, false);
#endif
		// Always trap DIR, so we know which IRQs are being deactivated
		// when the VM uses EOImode=1. We can't rely on LRENPIE/EOIcount
		// in this case (as opposed to EOImode=0, when we can assume the
		// highest priority active interrupts are being deactivated).
		ICH_HCR_EL2_set_TDIR(&vcpu->vgic_ich_hcr, true);
		// Always enable the interface.
		ICH_HCR_EL2_set_En(&vcpu->vgic_ich_hcr, true);

		vcpu->vgic_ich_vmcr = ICH_VMCR_EL2_default();
	}

	return OK;
}

index_result_t
vgic_get_index_for_mpidr(vic_t *vic, uint8_t aff0, uint8_t aff1, uint8_t aff2,
			 uint8_t aff3)
{
	platform_mpidr_mapping_t mapping = vic->mpidr_mapping;
	index_result_t		 ret;

	if (compiler_unexpected(((~mapping.aff_mask[0] & aff0) != 0U) ||
				((~mapping.aff_mask[1] & aff1) != 0U) ||
				((~mapping.aff_mask[2] & aff2) != 0U) ||
				((~mapping.aff_mask[3] & aff3) != 0U))) {
		ret = index_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	index_t index = 0U;
	index |= ((index_t)aff0 << mapping.aff_shift[0]);
	index |= ((index_t)aff1 << mapping.aff_shift[1]);
	index |= ((index_t)aff2 << mapping.aff_shift[2]);
	index |= ((index_t)aff3 << mapping.aff_shift[3]);

	if (compiler_unexpected(index >= vic->gicr_count)) {
		ret = index_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	ret = index_result_ok(index);
out:
	return ret;
}

error_t
vgic_handle_object_activate_thread(thread_t *vcpu)
{
	error_t err = OK;
	vic_t  *vic = vcpu->vgic_vic;

	if (vic != NULL) {
		spinlock_acquire(&vic->gicd_lock);

		index_t index = vcpu->vgic_gicr_index;

		if (atomic_load_relaxed(&vic->gicr_vcpus[index]) != NULL) {
			err = ERROR_BUSY;
			goto out_locked;
		}

		// Initialise the local IRQ delivery states, including their
		// route fields which are fixed to this CPU's index to simplify
		// the routing logic elsewhere.
		//
		// The SGIs are always edge-triggered, so set the edge trigger
		// bit in their dstates.
		vgic_delivery_state_t sgi_dstate =
			vgic_delivery_state_default();
		vgic_delivery_state_set_cfg_is_edge(&sgi_dstate, true);
		vgic_delivery_state_set_route(&sgi_dstate, index);
		for (index_t i = 0; i < GIC_SGI_NUM; i++) {
			atomic_init(&vcpu->vgic_private_states[i], sgi_dstate);
		}
		// PPIs are normally level-triggered.
		vgic_delivery_state_t ppi_dstate =
			vgic_delivery_state_default();
		vgic_delivery_state_set_route(&ppi_dstate, index);
		for (index_t i = 0; i < GIC_PPI_NUM; i++) {
			atomic_init(
				&vcpu->vgic_private_states[GIC_PPI_BASE + i],
				ppi_dstate);
		}

		// Determine the physical interrupt route that should be used
		// for interrupts that target this VCPU.
		scheduler_lock_nopreempt(vcpu);
		cpu_index_t affinity = scheduler_get_affinity(vcpu);
		MPIDR_EL1_t mpidr    = platform_cpu_index_to_mpidr(
			   cpulocal_index_valid(affinity) ? affinity : 0U);
		GICD_IROUTER_t phys_route = GICD_IROUTER_default();
		GICD_IROUTER_set_IRM(&phys_route, false);
		GICD_IROUTER_set_Aff0(&phys_route, MPIDR_EL1_get_Aff0(&mpidr));
		GICD_IROUTER_set_Aff1(&phys_route, MPIDR_EL1_get_Aff1(&mpidr));
		GICD_IROUTER_set_Aff2(&phys_route, MPIDR_EL1_get_Aff2(&mpidr));
		GICD_IROUTER_set_Aff3(&phys_route, MPIDR_EL1_get_Aff3(&mpidr));
		vcpu->vgic_irouter = phys_route;

#if VGIC_HAS_LPI && GICV3_HAS_VLPI
#if GICV3_HAS_VLPI_V4_1
		// VSGI setup has not been done yet; set the sequence
		// number to one that will never be complete.
		atomic_init(&vcpu->vgic_vsgi_setup_seq, ~(count_t)0U);
#endif

		if (vgic_has_lpis(vic)) {
			size_t vlpi_pendbase_size =
				BITMAP_NUM_WORDS(util_bit(vic->gicd_idbits)) *
				sizeof(register_t);
			size_t vlpi_pendbase_align =
				util_bit(GIC_ITS_CMD_VMAPP_VPT_ADDR_PRESHIFT);
			void_ptr_result_t alloc_r = partition_alloc(
				vcpu->header.partition, vlpi_pendbase_size,
				vlpi_pendbase_align);
			if (alloc_r.e != OK) {
				err = alloc_r.e;
				goto out_vcpu_locked;
			}

			// Call the ITS driver to allocate a vPE ID and a
			// doorbell LPI for this VCPU. We do this before we
			// save the pending table pointer so the cleanup
			// function can use the pointer to decide whether
			// to call gicv3_its_vpe_cleanup(vcpu).
			err = gicv3_its_vpe_activate(vcpu);
			if (err != OK) {
				(void)partition_free(vcpu->header.partition,
						     alloc_r.r,
						     vlpi_pendbase_size);
				goto out_vcpu_locked;
			}

			// No need to memset here; it will be done (with a
			// possible partial memcpy from the VM) before we issue
			// a VMAPP, when the VM writes 1 to EnableLPIs.
			vcpu->vgic_vlpi_pending_table = alloc_r.r;
		}
#endif

		// Set the GICD's pointer to the VCPU. This is a store release
		// so we can be sure that all of the thread's initialisation is
		// complete before the VGIC tries to use it.
		atomic_store_release(&vic->gicr_vcpus[index], vcpu);

#if VGIC_HAS_LPI && GICV3_HAS_VLPI
	out_vcpu_locked:
#endif
		scheduler_unlock_nopreempt(vcpu);
	out_locked:
		spinlock_release(&vic->gicd_lock);

		if (err == OK) {
			vcpu->vcpu_regs_mpidr_el1 =
				platform_cpu_map_index_to_mpidr(
					&vic->mpidr_mapping, index);

			// Check for IRQs that were routed to this CPU and
			// delivered before it was attached, to make sure they
			// are flagged locally.
			vgic_retry_unrouted(vic);
		}
	}

	return err;
}

void
vgic_handle_scheduler_affinity_changed(thread_t *vcpu, cpu_index_t next_cpu)
{
	MPIDR_EL1_t    mpidr	  = platform_cpu_index_to_mpidr(next_cpu);
	GICD_IROUTER_t phys_route = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&phys_route, false);
	GICD_IROUTER_set_Aff0(&phys_route, MPIDR_EL1_get_Aff0(&mpidr));
	GICD_IROUTER_set_Aff1(&phys_route, MPIDR_EL1_get_Aff1(&mpidr));
	GICD_IROUTER_set_Aff2(&phys_route, MPIDR_EL1_get_Aff2(&mpidr));
	GICD_IROUTER_set_Aff3(&phys_route, MPIDR_EL1_get_Aff3(&mpidr));
	vcpu->vgic_irouter = phys_route;
}

void
vgic_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread_get_self() != thread);
	assert(!cpulocal_index_valid(
		atomic_load_relaxed(&thread->vgic_lr_owner_lock.owner)));

	vic_t *vic = thread->vgic_vic;
	if (vic != NULL) {
		rcu_read_start();
		for (index_t i = 0; i < GIC_PPI_NUM; i++) {
			virq_source_t *virq_source =
				atomic_load_consume(&thread->vgic_sources[i]);

			if (virq_source == NULL) {
				continue;
			}

			vic_unbind(virq_source);
		}
		rcu_read_finish();

		spinlock_acquire(&vic->gicd_lock);

		assert(thread->vgic_gicr_index < vic->gicr_count);
		if (atomic_load_relaxed(
			    &vic->gicr_vcpus[thread->vgic_gicr_index]) ==
		    thread) {
			atomic_store_relaxed(
				&vic->gicr_vcpus[thread->vgic_gicr_index],
				NULL);
		}

#if VGIC_HAS_LPI
		if (vgic_has_lpis(vic) &&
		    (thread->vgic_vlpi_pending_table != NULL)) {
			// Ensure that any outstanding unmap has finished
			GICR_CTLR_t old_ctlr =
				atomic_load_relaxed(&thread->vgic_gicr_rd_ctlr);
			if (GICR_CTLR_get_Enable_LPIs(&old_ctlr)) {
				count_result_t count_r =
					gicv3_its_vpe_unmap(thread);
				assert(count_r.e == OK);
				thread->vgic_vlpi_unmap_seq = count_r.r;
			}
		}
#endif

		if (thread->vgic_gicr_device.type != VDEVICE_TYPE_NONE) {
			vdevice_detach_vmaddr(&thread->vgic_gicr_device);
		}

		spinlock_release(&vic->gicd_lock);
	}
}

void
vgic_unwind_object_activate_thread(thread_t *thread)
{
	vgic_handle_object_deactivate_thread(thread);
}

void
vgic_handle_object_cleanup_thread(thread_t *thread)
{
	partition_t *partition = thread->header.partition;
	assert(partition != NULL);

	vic_t *vic = thread->vgic_vic;
	if (vic != NULL) {
		// Ensure that the VIRQ groups are disabled
		thread->vgic_group0_enabled = false;
		thread->vgic_group1_enabled = false;

		// Clear out all LRs and re-route all pending IRQs
		vgic_undeliver_all(vic, thread);

#if VGIC_HAS_LPI && GICV3_HAS_VLPI
		if (vgic_has_lpis(vic) &&
		    (thread->vgic_vlpi_pending_table != NULL)) {
			// Ensure that any outstanding unmap has finished
			GICR_CTLR_t old_ctlr =
				atomic_load_relaxed(&thread->vgic_gicr_rd_ctlr);
			if (GICR_CTLR_get_Enable_LPIs(&old_ctlr)) {
				(void)gicv3_its_wait(
					0U, thread->vgic_vlpi_unmap_seq);
			}

			// Discard the pending table
			size_t vlpi_pendbase_size =
				BITMAP_NUM_WORDS(util_bit(vic->gicd_idbits)) *
				sizeof(register_t);
			(void)partition_free(thread->header.partition,
					     thread->vgic_vlpi_pending_table,
					     vlpi_pendbase_size);
			thread->vgic_vlpi_pending_table = NULL;

			// Tell the ITS driver to release the allocated vPE ID
			// and doorbell IRQ.
			gicv3_its_vpe_cleanup(thread);
		} else {
			assert(thread->vgic_vlpi_pending_table == NULL);
		}
#endif

#if VGIC_HAS_1N
		// Wake any other threads on the GIC, in case the deferred IRQs
		// can be rerouted.
		vgic_sync_all(vic, true);
#endif

		object_put_vic(vic);
	}
}

static void
vgic_handle_rootvm_create_hwirq(partition_t	 *root_partition,
				cspace_t	 *root_cspace,
				qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	index_t i = 0U;
#if GICV3_EXT_IRQS
	index_t last_spi = util_min((count_t)platform_irq_max(),
				    GIC_SPI_EXT_BASE + GIC_SPI_EXT_NUM - 1U);
#else
	index_t last_spi = util_min((count_t)platform_irq_max(),
				    GIC_SPI_BASE + GIC_SPI_NUM - 1U);
#endif

	QCBOREncode_OpenArrayInMap(qcbor_enc_ctxt, "vic_hwirq");
	while (i <= last_spi) {
		hwirq_create_t hwirq_params = {
			.irq = i,
		};

		gicv3_irq_type_t irq_type = gicv3_get_irq_type(i);

		if (irq_type == GICV3_IRQ_TYPE_SPI) {
			hwirq_params.action = HWIRQ_ACTION_VGIC_FORWARD_SPI;
		} else if (irq_type == GICV3_IRQ_TYPE_PPI) {
			hwirq_params.action =
				HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE;
#if GICV3_EXT_IRQS
		} else if (irq_type == GICV3_IRQ_TYPE_SPI_EXT) {
			hwirq_params.action = HWIRQ_ACTION_VGIC_FORWARD_SPI;
		} else if (irq_type == GICV3_IRQ_TYPE_PPI_EXT) {
			hwirq_params.action =
				HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE;
#endif
		} else {
			QCBOREncode_AddUInt64(qcbor_enc_ctxt,
					      CSPACE_CAP_INVALID);
			goto next_index;
		}

		hwirq_ptr_result_t hwirq_r =
			partition_allocate_hwirq(root_partition, hwirq_params);
		if (hwirq_r.e != OK) {
			panic("Unable to create HW IRQ object");
		}

		error_t err = object_activate_hwirq(hwirq_r.r);
		if (err != OK) {
			if ((err == ERROR_DENIED) ||
			    (err == ERROR_ARGUMENT_INVALID) ||
			    (err == ERROR_BUSY)) {
				QCBOREncode_AddUInt64(qcbor_enc_ctxt,
						      CSPACE_CAP_INVALID);
				object_put_hwirq(hwirq_r.r);
				goto next_index;
			} else {
				panic("Failed to activate HW IRQ object");
			}
		}

		// Create a master cap for the HWIRQ
		object_ptr_t	hwirq_optr = { .hwirq = hwirq_r.r };
		cap_id_result_t cid_r	   = cspace_create_master_cap(
			     root_cspace, hwirq_optr, OBJECT_TYPE_HWIRQ);
		if (cid_r.e != OK) {
			panic("Unable to create cap to HWIRQ");
		}
		QCBOREncode_AddUInt64(qcbor_enc_ctxt, cid_r.r);

	next_index:
		i++;
#if GICV3_EXT_IRQS
		// Skip large range between end of non-extended PPIs and start
		// of extended SPIs to optimize encoding
		if (i == GIC_PPI_EXT_BASE + GIC_PPI_EXT_NUM) {
			i = GIC_SPI_EXT_BASE;
		}
#endif
	}
	QCBOREncode_CloseArray(qcbor_enc_ctxt);
}

void
vgic_handle_rootvm_init(partition_t *root_partition, thread_t *root_thread,
			cspace_t *root_cspace, hyp_env_data_t *hyp_env,
			qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	// Create the VIC object for the root VM
	vic_create_t	 vic_params = { 0 };
	vic_ptr_result_t vic_r =
		partition_allocate_vic(root_partition, vic_params);
	if (vic_r.e != OK) {
		goto vic_fail;
	}
	spinlock_acquire(&vic_r.r->header.lock);
	count_t max_vcpus = 1U;
	count_t max_virqs = 64U;
	count_t max_msis  = 0U;

	assert(qcbor_enc_ctxt != NULL);

	hyp_env->gicd_base   = PLATFORM_GICD_BASE;
	hyp_env->gicr_base   = PLATFORM_GICR_BASE;
	hyp_env->gicr_stride = (size_t)util_bit(GICR_STRIDE_SHIFT);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "gicd_base",
				   PLATFORM_GICD_BASE);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "gicr_stride",
				   (size_t)util_bit(GICR_STRIDE_SHIFT));
	// Array of tuples of base address and number of GICRs for each
	// contiguous GICR range. Currently only one range is supported.
	QCBOREncode_OpenArrayInMap(qcbor_enc_ctxt, "gicr_ranges");
	QCBOREncode_OpenArray(qcbor_enc_ctxt);
	QCBOREncode_AddUInt64(qcbor_enc_ctxt, PLATFORM_GICR_BASE);
	QCBOREncode_AddUInt64(qcbor_enc_ctxt, PLATFORM_GICR_COUNT);
	QCBOREncode_CloseArray(qcbor_enc_ctxt);
	QCBOREncode_CloseArray(qcbor_enc_ctxt);

	if (vic_configure(vic_r.r, max_vcpus, max_virqs, max_msis, false) !=
	    OK) {
		spinlock_release(&vic_r.r->header.lock);
		goto vic_fail;
	}
	spinlock_release(&vic_r.r->header.lock);

	if (object_activate_vic(vic_r.r) != OK) {
		goto vic_fail;
	}

	// Create a master cap for the VIC
	object_ptr_t	vic_optr = { .vic = vic_r.r };
	cap_id_result_t cid_r = cspace_create_master_cap(root_cspace, vic_optr,
							 OBJECT_TYPE_VIC);
	if (cid_r.e != OK) {
		goto vic_fail;
	}
	hyp_env->vic = cid_r.r;
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "vic", cid_r.r);

	index_t vic_index = 0U;

	if (vic_attach_vcpu(vic_r.r, root_thread, vic_index) != OK) {
		panic("VIC couldn't attach root VM thread");
	}

	// Create a HWIRQ object for every SPI
	vgic_handle_rootvm_create_hwirq(root_partition, root_cspace,
					qcbor_enc_ctxt);
	hyp_env->gits_base   = 0U;
	hyp_env->gits_stride = 0U;

	return;

vic_fail:
	panic("Unable to create root VM's virtual GIC");
}

void
vgic_handle_rootvm_init_late(thread_t		  *root_thread,
			     const hyp_env_data_t *hyp_env)
{
	assert(root_thread != NULL);
	assert(hyp_env != NULL);

	addrspace_t *root_addrspace = root_thread->addrspace;
	if (root_addrspace == NULL) {
		panic("vgic rootvm_init_late: addrspace not yet created\n");
	}

	vic_t *root_vic = root_thread->vgic_vic;
	spinlock_acquire(&root_vic->gicd_lock);

	root_vic->gicd_device.type = VDEVICE_TYPE_VGIC_GICD;
	if (vdevice_attach_vmaddr(&root_vic->gicd_device, root_addrspace,
				  hyp_env->gicd_base, sizeof(gicd_t)) != OK) {
		panic("vgic rootvm_init_late: unable to map GICD\n");
	}

	rcu_read_start();
	for (index_t i = 0U; i < root_vic->gicr_count; i++) {
		thread_t *gicr_vcpu =
			atomic_load_consume(&root_vic->gicr_vcpus[i]);
		if (gicr_vcpu == NULL) {
			continue;
		}
		gicr_vcpu->vgic_gicr_device.type = VDEVICE_TYPE_VGIC_GICR;
		if (vdevice_attach_vmaddr(
			    &gicr_vcpu->vgic_gicr_device, root_addrspace,
			    hyp_env->gicr_base + (i * hyp_env->gicr_stride),
			    hyp_env->gicr_stride) != OK) {
			panic("vgic rootvm_init_late: unable to map GICR\n");
		}
	}
	rcu_read_finish();
	spinlock_release(&root_vic->gicd_lock);
}

error_t
vgic_handle_object_create_hwirq(hwirq_create_t hwirq_create)
{
	hwirq_t *hwirq = hwirq_create.hwirq;
	assert(hwirq != NULL);

	error_t err = ERROR_ARGUMENT_INVALID;

	if (hwirq_create.action == HWIRQ_ACTION_VGIC_FORWARD_SPI) {
		gicv3_irq_type_t irq_type =
			gicv3_get_irq_type(hwirq_create.irq);
		// The physical IRQ must be an SPI.
		if (irq_type == GICV3_IRQ_TYPE_SPI) {
			err = OK;
#if GICV3_EXT_IRQS
		} else if (irq_type == GICV3_IRQ_TYPE_SPI_EXT) {
			err = OK;
#endif
		}
	} else if (hwirq_create.action ==
		   HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE) {
		gicv3_irq_type_t irq_type =
			gicv3_get_irq_type(hwirq_create.irq);
		// The physical IRQ must be an PPI.
		if (irq_type == GICV3_IRQ_TYPE_PPI) {
			err = OK;
#if GICV3_EXT_IRQS
		} else if (irq_type == GICV3_IRQ_TYPE_PPI_EXT) {
			err = OK;
#endif
		}
	} else {
		// Not a forwarded IRQ
		err = OK;
	}

	return err;
}

void
vgic_handle_object_deactivate_hwirq(hwirq_t *hwirq)
{
	if (hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI) {
		vic_unbind(&hwirq->vgic_spi_source);
	}
}

error_t
vgic_bind_hwirq_spi(vic_t *vic, hwirq_t *hwirq, virq_t virq)
{
	error_t err;

	assert(hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI);

	if (vgic_get_irq_type(virq) != VGIC_IRQ_TYPE_SPI) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	err = vic_bind_shared(&hwirq->vgic_spi_source, vic, virq,
			      VIRQ_TRIGGER_VGIC_FORWARDED_SPI);
	if (err != OK) {
		goto out;
	}

	// Take the GICD lock to ensure that the vGIC's IRQ config does not
	// change while we are copying it to the hardware GIC
	spinlock_acquire(&vic->gicd_lock);

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);

	// Default to an invalid physical route
	GICD_IROUTER_t physical_router = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&physical_router, false);
	GICD_IROUTER_set_Aff0(&physical_router, 0xff);
	GICD_IROUTER_set_Aff1(&physical_router, 0xff);
	GICD_IROUTER_set_Aff2(&physical_router, 0xff);
	GICD_IROUTER_set_Aff3(&physical_router, 0xff);

	// Try to set the physical route based on the virtual route
	rcu_read_start();
	thread_t *new_target = vgic_find_target(vic, &hwirq->vgic_spi_source);
	if (new_target != NULL) {
		physical_router = new_target->vgic_irouter;

		VGIC_TRACE(ROUTE, vic, NULL,
			   "bind {:d}: route virt {:d} phys {:#x}", virq,
			   new_target->vgic_gicr_index,
			   GICD_IROUTER_raw(physical_router));
	} else {
#if GICV3_HAS_1N
		// No direct target, so let the physical GIC choose
		GICD_IROUTER_set_IRM(&physical_router, true);
#endif

		VGIC_TRACE(ROUTE, vic, NULL,
			   "bind {:d}: route virt none phys {:#x}", virq,
			   GICD_IROUTER_raw(physical_router));
	}
	rcu_read_finish();

	// Set the chosen physical route
	err = gicv3_spi_set_route(hwirq->irq, physical_router);
	if (err != OK) {
		goto release_lock;
	}

#if GICV3_HAS_GICD_ICLAR
	if (GICD_IROUTER_get_IRM(&physical_router)) {
		// Set the HW IRQ's 1-of-N routing classes.
		err = gicv3_spi_set_classes(
			hwirq->irq,
			!vgic_delivery_state_get_nclass0(&current_dstate),
			vgic_delivery_state_get_class1(&current_dstate));

		if (err != OK) {
			goto release_lock;
		}
	}
#endif

	// Attempt to set the HW IRQ's trigger mode based on the virtual ICFGR;
	// if this fails because the HW trigger mode is fixed, then update the
	// virtual ICFGR insted.
	bool is_edge = vgic_delivery_state_get_cfg_is_edge(&current_dstate);
	irq_trigger_t	     mode     = is_edge ? IRQ_TRIGGER_EDGE_RISING
						: IRQ_TRIGGER_LEVEL_HIGH;
	irq_trigger_result_t new_mode = trigger_virq_set_mode_event(
		VIRQ_TRIGGER_VGIC_FORWARDED_SPI, &hwirq->vgic_spi_source, mode);
	if ((new_mode.e != OK) || (new_mode.r != mode)) {
		vgic_delivery_state_t cfg_is_edge =
			vgic_delivery_state_default();
		vgic_delivery_state_set_cfg_is_edge(&cfg_is_edge, true);
		// Mode change failed; the hardware config must be fixed to the
		// other mode. Flip the software mode.
		if (is_edge) {
			(void)vgic_delivery_state_atomic_intersection(
				dstate, cfg_is_edge, memory_order_relaxed);
		} else {
			(void)vgic_delivery_state_atomic_difference(
				dstate, cfg_is_edge, memory_order_relaxed);
		}
	}

	// Enable the HW IRQ if the virtual enable bit is set (unbound HW IRQs
	// are always disabled).
	if (vgic_delivery_state_get_enabled(&current_dstate)) {
		irq_enable_shared(hwirq);
	}

	hwirq->vgic_enable_hw = true;

release_lock:
	spinlock_release(&vic->gicd_lock);

out:
	return err;
}

error_t
vgic_unbind_hwirq_spi(hwirq_t *hwirq)
{
	error_t err;

	assert(hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI);

	rcu_read_start();
	vic_t *vic = atomic_load_consume(&hwirq->vgic_spi_source.vic);
	if (vic == NULL) {
		rcu_read_finish();
		err = ERROR_VIRQ_NOT_BOUND;
		goto out;
	}

	// Ensure that no other thread can concurrently enable the HW IRQ by
	// enabling the bound VIRQ.
	spinlock_acquire(&vic->gicd_lock);
	hwirq->vgic_enable_hw = false;
	spinlock_release(&vic->gicd_lock);
	rcu_read_finish();

	// Disable the IRQ, and wait for running handlers to complete.
	irq_disable_shared_sync(hwirq);

	// Remove the VIRQ binding, and wait until the source can be reused.
	vic_unbind_sync(&hwirq->vgic_spi_source);

	err = OK;
out:
	return err;
}

bool
vgic_handle_virq_set_enabled_hwirq_spi(virq_source_t *source, bool enabled)
{
	hwirq_t *hwirq = hwirq_from_virq_source(source);
	assert(!source->is_private);
	assert(!platform_irq_is_percpu(hwirq->irq));

	if (enabled) {
		if (compiler_expected(hwirq->vgic_enable_hw)) {
			irq_enable_shared(hwirq);
		}
	} else {
		irq_disable_shared_nosync(hwirq);
	}

	return true;
}

irq_trigger_result_t
vgic_handle_virq_set_mode_hwirq_spi(virq_source_t *source, irq_trigger_t mode)
{
	hwirq_t *hwirq = hwirq_from_virq_source(source);

	assert(!source->is_private);
	assert(!platform_irq_is_percpu(hwirq->irq));

	return gicv3_irq_set_trigger_shared(hwirq->irq, mode);
}

static void
vgic_change_irq_pending(vic_t *vic, thread_t *target, irq_t irq_num,
			bool is_private, virq_source_t *source, bool set,
			bool is_msi)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, target, irq_num);
	assert(dstate != NULL);

	preempt_disable();

	// Determine the pending flags to change.
	vgic_delivery_state_t change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_edge(&change_dstate, true);
	if (is_msi) {
		vgic_delivery_state_set_level_msg(&change_dstate, true);
	} else {
		vgic_delivery_state_set_level_sw(&change_dstate, true);
	}

	if (set) {
		(void)vgic_deliver(irq_num, vic, target, source, dstate,
				   change_dstate, is_private);
	} else {
		// Forwarded SPIs must be deactivated; otherwise they will
		// become undeliverable until asserted in software. This has no
		// effect on IRQs that are not forwarded SPIs.
		vgic_delivery_state_set_hw_active(&change_dstate, true);

		// Edge-triggered forwarded SPIs need to be cleared in hardware
		// as well, in case they have a pending state the hypervisor
		// hasn't seen yet. This has no effect on level-triggered IRQs.
		bool is_hw =
			(source != NULL) &&
			(source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);
		if (is_hw) {
			hwirq_t *hwirq = hwirq_from_virq_source(source);
			gicv3_irq_cancel_nowait(hwirq->irq);
		}

		// Undeliver the IRQ.
		//
		// We don't forcibly reclaim the VIRQ because it might still be
		// pending from a level-triggered hardware source. This means we
		// don't know whether to trigger a sync if the VIRQ is still
		// remotely listed.
		//
		// It is strictly ok not to sync, because the GIC specification
		// implicitly permits this operation to take an arbitrarily long
		// time to be effective (it can't be polled like ICENABLER, and
		// there is no finite-time guarantee of completion like there is
		// for IPRIORITYR etc.). Still, this might cause problems for
		// drivers that assume that ICPENDR works.
		(void)vgic_undeliver(vic, target, dstate, irq_num,
				     change_dstate, false);
	}

	preempt_enable();
}

static void
vgic_change_irq_enable(vic_t *vic, thread_t *target, irq_t irq_num,
		       bool is_private, virq_source_t *source, bool set)
	REQUIRE_PREEMPT_DISABLED
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, target, irq_num);
	assert(dstate != NULL);

	if ((source != NULL) && !set) {
		(void)trigger_virq_set_enabled_event(source->trigger, source,
						     set);
	}

	vgic_delivery_state_t change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_enabled(&change_dstate, true);

	if (set) {
		(void)vgic_deliver(irq_num, vic, target, source, dstate,
				   change_dstate, is_private);

	} else {
		// Undeliver and reclaim the VIRQ.
		if (!vgic_undeliver(vic, target, dstate, irq_num, change_dstate,
				    false)) {
			vgic_sync_all(vic, false);
		}
	}

	if ((source != NULL) && set) {
		(void)trigger_virq_set_enabled_event(source->trigger, source,
						     set);
	}
}

static void
vgic_change_irq_active(vic_t *vic, thread_t *vcpu, irq_t irq_num, bool set)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, irq_num);
	assert(dstate != NULL);

	// Accurately virtualising ISACTIVER / ICACTIVER, even for reads, is
	// challenging due to the list register model; we would have to be
	// able to simultaneously block all attached VCPUs (including those that
	// are running remotely) and read and write their LRs to do it
	// accurately.
	//
	// This doesn't matter much, though, since they are only really useful
	// for power management (typically at EL3, no not in our VMs) and
	// debugging the GIC driver (which shouldn't be happening in a VM).
	//
	// We take the easy approach here, and simply ignore any writes to
	// currently listed VIRQs.

	// Don't let context switches delist the VIRQ out from under us
	preempt_disable();

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// Interrupt is listed; ignore the write.
	} else if (!set) {
		vgic_deactivate(vic, vcpu, irq_num, dstate, old_dstate, false,
				false);
	} else {
		vgic_delivery_state_t new_dstate;
		do {
			if (vgic_delivery_state_get_listed(&old_dstate)) {
				break;
			}
			new_dstate = old_dstate;
			vgic_delivery_state_set_active(&new_dstate, set);
		} while (!atomic_compare_exchange_weak_explicit(
			dstate, &old_dstate, new_dstate, memory_order_relaxed,
			memory_order_relaxed));
	}

	preempt_enable();
}

static void
vgic_sync_group_change(vic_t *vic, virq_t irq_num,
		       _Atomic vgic_delivery_state_t *dstate, bool is_group_1)
{
	assert(dstate != NULL);

	// Atomically update the group bit and obtain the current state.
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		new_dstate = old_dstate;
		vgic_delivery_state_set_group1(&new_dstate, is_group_1);
		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// To guarantee that the group change takes effect in
			// finite time, request a sync of the listed VIRQ.
			vgic_delivery_state_set_need_sync(&new_dstate,
							  is_group_1);
		}
	} while (!atomic_compare_exchange_weak_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// We requested a sync above; notify the VCPUs.
		vgic_sync_all(vic, false);
	} else {
		// Retry delivery, in case the group change made the IRQ
		// deliverable.
		rcu_read_start();
		thread_t *target =
			vgic_get_route_from_state(vic, new_dstate, false);
		if (target != NULL) {
			virq_source_t *source =
				vgic_find_source(vic, target, irq_num);
			(void)vgic_deliver(irq_num, vic, target, source, dstate,
					   vgic_delivery_state_default(),
					   vgic_irq_is_private(irq_num));
		}
		rcu_read_finish();
	}

	(void)0;
}

static void
vgic_set_irq_priority(vic_t *vic, thread_t *vcpu, irq_t irq_num,
		      uint8_t priority)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, irq_num);
	assert(dstate != NULL);

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		new_dstate = old_dstate;

		vgic_delivery_state_set_priority(&new_dstate, priority);

		// If the priority is being raised (made lesser), then there is
		// a possibility that its target VCPU can't receive it at the
		// old priority due to other active IRQs or a manual priority
		// mask, and is blocked in WFI; in this case we must send a sync
		// if the VIRQ is listed, or retry delivery at the new priority
		// if it is not listed (below).
		if ((priority <
		     vgic_delivery_state_get_priority(&old_dstate)) &&
		    vgic_delivery_state_get_listed(&old_dstate)) {
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	if (priority < vgic_delivery_state_get_priority(&old_dstate)) {
		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// To guarantee that the priority change will take
			// effect in finite time, sync all VCPUs that might have
			// it listed.
			vgic_sync_all(vic, false);
		} else if (vgic_delivery_state_get_enabled(&old_dstate) &&
			   vgic_delivery_state_is_pending(&old_dstate)) {
			// Retry delivery, in case it previously did not select
			// a LR only because the priority was too low
			rcu_read_start();
			thread_t *target = vgic_get_route_from_state(
				vic, new_dstate, false);
			if (target != NULL) {
				virq_source_t *source =
					vgic_find_source(vic, target, irq_num);
				(void)vgic_deliver(
					irq_num, vic, target, source, dstate,
					vgic_delivery_state_default(),
					vgic_irq_is_private(irq_num));
			}
			rcu_read_finish();
		} else {
			// Unlisted and not deliverable; nothing to do.
		}
	}
}

void
vgic_gicd_set_control(vic_t *vic, GICD_CTLR_DS_t ctlr)
{
	spinlock_acquire(&vic->gicd_lock);
	GICD_CTLR_DS_t old_ctlr = atomic_load_relaxed(&vic->gicd_ctlr);
	GICD_CTLR_DS_t new_ctlr = old_ctlr;

	GICD_CTLR_DS_copy_EnableGrp0(&new_ctlr, &ctlr);
	GICD_CTLR_DS_copy_EnableGrp1(&new_ctlr, &ctlr);
#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	if (!GICD_CTLR_DS_get_EnableGrp0(&old_ctlr) &&
	    !GICD_CTLR_DS_get_EnableGrp1(&old_ctlr)) {
		GICD_CTLR_DS_copy_nASSGIreq(&new_ctlr, &ctlr);
	}
#endif

	if (!GICD_CTLR_DS_is_equal(new_ctlr, old_ctlr)) {
#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
		vic->vsgis_enabled = GICD_CTLR_DS_get_nASSGIreq(&new_ctlr);
#endif
		atomic_store_relaxed(&vic->gicd_ctlr, new_ctlr);
		vgic_update_enables(vic, new_ctlr);
	}

	spinlock_release(&vic->gicd_lock);
}

void
vgic_gicd_set_statusr(vic_t *vic, GICD_STATUSR_t statusr, bool set)
{
	spinlock_acquire(&vic->gicd_lock);
	if (set) {
		vic->gicd_statusr =
			GICD_STATUSR_union(vic->gicd_statusr, statusr);
	} else {
		vic->gicd_statusr =
			GICD_STATUSR_difference(vic->gicd_statusr, statusr);
	}
	spinlock_release(&vic->gicd_lock);
}

void
vgic_gicd_change_irq_pending(vic_t *vic, irq_t irq_num, bool set, bool is_msi)
{
	if (vgic_irq_is_spi(irq_num)) {
		rcu_read_start();
		virq_source_t *source = vgic_find_source(vic, NULL, irq_num);

		// Try to find a thread to deliver to if we're setting the
		// pending bit. This might be NULL if the route is invalid
		// or the VCPU isn't attached.
		thread_t *target =
			set ? vgic_get_route_for_spi(vic, irq_num, false)
			    : NULL;

		vgic_change_irq_pending(vic, target, irq_num, false, source,
					set, is_msi);
		rcu_read_finish();
	} else {
		assert(is_msi);
		// Ignore attempts to message-signal non SPI IRQs
	}
}

void
vgic_gicd_change_irq_enable(vic_t *vic, irq_t irq_num, bool set)
{
	assert(vgic_irq_is_spi(irq_num));

	// Take the GICD lock and locate the source. We must do this
	// with the lock held to ensure that HW IRQs are correctly
	// enabled and disabled.
	spinlock_acquire(&vic->gicd_lock);
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, NULL, irq_num);

	// Try to find a thread to deliver to if we're setting the enable bit.
	// This might be NULL if the route is invalid or the VCPU isn't
	// attached.
	thread_t *target = set ? vgic_get_route_for_spi(vic, irq_num, false)
			       : NULL;

	vgic_change_irq_enable(vic, target, irq_num, false, source, set);
	rcu_read_finish();

	spinlock_release(&vic->gicd_lock);
}

void
vgic_gicd_change_irq_active(vic_t *vic, irq_t irq_num, bool set)
{
	if (vgic_irq_is_spi(irq_num)) {
		vgic_change_irq_active(vic, NULL, irq_num, set);
	}
}

void
vgic_gicd_set_irq_group(vic_t *vic, irq_t irq_num, bool is_group_1)
{
	if (vgic_irq_is_spi(irq_num)) {
		_Atomic vgic_delivery_state_t *dstate =
			&vic->spi_states[irq_num - GIC_SPI_BASE];

		vgic_sync_group_change(vic, irq_num, dstate, is_group_1);
	}
}

void
vgic_gicd_set_irq_priority(vic_t *vic, irq_t irq_num, uint8_t priority)
{
	vgic_set_irq_priority(vic, thread_get_self(), irq_num, priority);
}

void
vgic_gicd_set_irq_config(vic_t *vic, irq_t irq_num, bool is_edge)
{
	assert(vgic_irq_is_spi(irq_num));
	assert(vic != NULL);

	// Take the GICD lock to ensure that concurrent writes don't make the
	// HW and dstate views of the config inconsistent
	spinlock_acquire(&vic->gicd_lock);

	bool effective_is_edge = is_edge;

	// If there's a source, update its config. Note that this may fail.
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, NULL, irq_num);
	if (source != NULL) {
		irq_trigger_t	     mode = is_edge ? IRQ_TRIGGER_EDGE_RISING
						    : IRQ_TRIGGER_LEVEL_HIGH;
		irq_trigger_result_t new_mode = trigger_virq_set_mode_event(
			source->trigger, source, mode);
		if (new_mode.e != OK) {
			// Unable to set the requested mode; bail out
			rcu_read_finish();
			goto out;
		}
		effective_is_edge = new_mode.r == IRQ_TRIGGER_EDGE_RISING;
	}
	rcu_read_finish();

	// Update the delivery state.
	//
	// There is no need to synchronise: changing this configuration while
	// the interrupt is enabled and pending has an UNKNOWN effect on the
	// interrupt's pending state.
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, irq_num);
	vgic_delivery_state_t change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_cfg_is_edge(&change_dstate, true);
	if (effective_is_edge) {
		(void)vgic_delivery_state_atomic_union(dstate, change_dstate,
						       memory_order_relaxed);
	} else {
		// Also clear any leftover software level assertions.
		vgic_delivery_state_set_level_sw(&change_dstate, true);
		vgic_delivery_state_set_level_msg(&change_dstate, true);
		(void)vgic_delivery_state_atomic_difference(
			dstate, change_dstate, memory_order_relaxed);
	}

out:
	spinlock_release(&vic->gicd_lock);
}

static void
vgic_gicd_set_irq_hardware_router(vic_t *vic, irq_t irq_num,
				  vgic_delivery_state_t new_dstate,
				  const thread_t       *new_target,
				  index_t		route_index)
{
	virq_source_t *source = vgic_find_source(vic, NULL, irq_num);
	bool	       is_hw  = (source != NULL) &&
		     (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);
	if (is_hw) {
		// Default to an invalid physical route
		GICD_IROUTER_t physical_router = GICD_IROUTER_default();
		GICD_IROUTER_set_IRM(&physical_router, false);
		GICD_IROUTER_set_Aff0(&physical_router, 0xff);
		GICD_IROUTER_set_Aff1(&physical_router, 0xff);
		GICD_IROUTER_set_Aff2(&physical_router, 0xff);
		GICD_IROUTER_set_Aff3(&physical_router, 0xff);

		// Try to set the physical route based on the virtual target
#if VGIC_HAS_1N && GICV3_HAS_1N
		if (vgic_delivery_state_get_route_1n(&new_dstate)) {
			GICD_IROUTER_set_IRM(&physical_router, true);
		} else
#endif
			if (new_target != NULL) {
			physical_router = new_target->vgic_irouter;
		} else {
			// No valid target
		}

		// Set the chosen physical route
		VGIC_TRACE(ROUTE, vic, NULL, "route {:d}: virt {:d} phys {:#x}",
			   irq_num, route_index,
			   GICD_IROUTER_raw(physical_router));
		irq_t irq = hwirq_from_virq_source(source)->irq;
		(void)gicv3_spi_set_route(irq, physical_router);

#if GICV3_HAS_GICD_ICLAR
		if (GICD_IROUTER_get_IRM(&physical_router)) {
			// Set the HW IRQ's 1-of-N routing classes.
			(void)gicv3_spi_set_classes(
				irq,
				!vgic_delivery_state_get_nclass0(&new_dstate),
				vgic_delivery_state_get_class1(&new_dstate));
		}
#endif
	} else {
		VGIC_TRACE(ROUTE, vic, NULL, "route {:d}: virt {:d} phys N/A",
			   irq_num, route_index);
	}
#if !(VGIC_HAS_1N && GICV3_HAS_1N) && !GICV3_HAS_GICD_ICLAR
	(void)new_dstate;
#endif
}

void
vgic_gicd_set_irq_router(vic_t *vic, irq_t irq_num, uint8_t aff0, uint8_t aff1,
			 uint8_t aff2, uint8_t aff3, bool is_1n)
{
	assert(vgic_irq_is_spi(irq_num));
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, irq_num);
	assert(dstate != NULL);

	// Find the new target index
	index_result_t cpu_r =
		vgic_get_index_for_mpidr(vic, aff0, aff1, aff2, aff3);
	index_t route_index;
	if (cpu_r.e == OK) {
		assert(cpu_r.r < vic->gicr_count);
		route_index = cpu_r.r;
	} else {
		// Use an out-of-range value to indicate an invalid route.
		route_index = PLATFORM_MAX_CORES;
	}

	// Take the GICD lock to ensure that concurrent writes don't make the
	// HW, VIRQ source and GICD register views of the route inconsistent
	spinlock_acquire(&vic->gicd_lock);

	// Update the route in the delivery state
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		new_dstate = old_dstate;

		vgic_delivery_state_set_route(&new_dstate, route_index);
#if VGIC_HAS_1N
		vgic_delivery_state_set_route_1n(&new_dstate, is_1n);
#else
		(void)is_1n;
#endif

		// We might need to reroute a listed IRQ, so send a sync.
		if (vgic_delivery_state_get_listed(&old_dstate)) {
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	// Find the new target.
	rcu_read_start();
	thread_t *new_target =
		(route_index < vic->gicr_count)
			? atomic_load_consume(&vic->gicr_vcpus[route_index])
			: NULL;

	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// To guarantee that the route change will take effect in finite
		// time, sync all VCPUs that might have it listed.
		vgic_sync_all(vic, false);
	} else if (vgic_delivery_state_get_enabled(&old_dstate) &&
		   vgic_delivery_state_is_pending(&old_dstate)) {
		// Retry delivery, in case it previously did not select a LR
		// only because the priority was too low.
		(void)vgic_deliver(irq_num, vic, new_target, NULL, dstate,
				   vgic_delivery_state_default(),
				   vgic_irq_is_private(irq_num));
	} else {
		// Unlisted and not deliverable; nothing to do.
	}

	// For hardware sourced IRQs, pass the change through to the hardware.
	vgic_gicd_set_irq_hardware_router(vic, irq_num, new_dstate, new_target,
					  route_index);

	spinlock_release(&vic->gicd_lock);
	rcu_read_finish();
}

#if GICV3_HAS_GICD_ICLAR
void
vgic_gicd_set_irq_classes(vic_t *vic, irq_t irq_num, bool class0, bool class1)
{
	assert(vgic_irq_is_spi(irq_num));
	assert(vic != NULL);

	// Take the GICD lock to ensure that concurrent writes don't make the
	// HW and dstate views of the config inconsistent
	spinlock_acquire(&vic->gicd_lock);

	// If there's a source, update its config. Note that this may fail, and
	// it will have no effect if the IRQ is not currently 1-of-N routed.
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, NULL, irq_num);
	if ((source != NULL) &&
	    (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI)) {
		hwirq_t *hwirq = hwirq_from_virq_source(source);
		error_t err = gicv3_spi_set_classes(hwirq->irq, class0, class1);
		if (err != OK) {
			rcu_read_finish();
			goto out;
		}
	}
	rcu_read_finish();

	// Update the delivery state.
	//
	// There is no need to synchronise: changing this configuration while
	// the interrupt is enabled and pending has an UNKNOWN effect on the
	// interrupt's pending state.
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, irq_num);
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		new_dstate = old_dstate;
		vgic_delivery_state_set_nclass0(&new_dstate, !class0);
		vgic_delivery_state_set_class1(&new_dstate, class1);
	} while (!atomic_compare_exchange_weak_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

out:
	spinlock_release(&vic->gicd_lock);
}
#endif

// GICR
thread_t *
vgic_get_thread_by_gicr_index(vic_t *vic, index_t gicr_num)
{
	assert(gicr_num < vic->gicr_count);
	return atomic_load_consume(&vic->gicr_vcpus[gicr_num]);
}

#if VGIC_HAS_LPI
// Copy part or all of an LPI config or pending table from VM memory.
static void
vgic_gicr_copy_in(addrspace_t *addrspace, uint8_t *hyp_table,
		  size_t hyp_table_size, vmaddr_t vm_table_ipa, size_t offset,
		  size_t vm_table_size)
{
	error_t err = OK;

	if (util_add_overflows((uintptr_t)hyp_table, offset) ||
	    util_add_overflows(vm_table_ipa, offset)) {
		err = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	if ((offset >= hyp_table_size) || (offset >= vm_table_size)) {
		err = ERROR_ADDR_UNDERFLOW;
		goto out;
	}

	err = useraccess_copy_from_guest_ipa(addrspace, hyp_table + offset,
					     hyp_table_size - offset,
					     vm_table_ipa + offset,
					     vm_table_size - offset, false,
					     false)
		      .e;

out:
	if (err != OK) {
		// Copy failed.
		//
		// Note that GICv4.1 deprecates implementation of SError
		// generation in the GICR & CPU interface (as opposed to the
		// ITS), and recent CPUs don't implement it. So there is no way
		// to report this to the VM. We just log it and continue.
		TRACE_AND_LOG(ERROR, WARN,
			      "vgicr: LPI table copy-in failed: {:d}",
			      (register_t)err);
	}
}

static bool
vgic_gicr_copy_pendbase(vic_t *vic, count_t idbits, thread_t *gicr_vcpu)
{
	assert(vic != NULL);
	assert(gicr_vcpu != NULL);

	GICR_PENDBASER_t pendbaser =
		atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_pendbaser);
	bool   ptz = GICR_PENDBASER_get_PTZ(&pendbaser);
	size_t pending_table_size =
		BITMAP_NUM_WORDS(util_bit(vic->gicd_idbits)) *
		sizeof(register_t);
	const size_t pending_table_reserved =
		BITMAP_NUM_WORDS(GIC_LPI_BASE) * sizeof(register_t);

	assert(gicr_vcpu->vgic_vlpi_pending_table != NULL);
	assert(pending_table_size > pending_table_reserved);

	if (ptz) {
		errno_t err_mem = memset_s(gicr_vcpu->vgic_vlpi_pending_table,
					   pending_table_size, 0,
					   pending_table_size);
		if (err_mem != 0) {
			panic("Error in memset_s operation!");
		}
	} else {
		// Zero the reserved part of the pending table
		errno_t err_mem = memset_s(gicr_vcpu->vgic_vlpi_pending_table,
					   pending_table_reserved, 0,
					   pending_table_reserved);
		if (err_mem != 0) {
			panic("Error in memset_s operation!");
		}

		// Look up the physical address of the IPA range specified in
		// the GICR_PENDBASER, and copy it into the pending table. If
		// the lookup fails, or the permissions are wrong, copy zeros.
		vmaddr_t base = GICR_PENDBASER_get_PA(&pendbaser);
		size_t	 vm_table_size =
			BITMAP_NUM_WORDS(util_bit(idbits)) * sizeof(register_t);
		assert(vm_table_size <= pending_table_size);

		vgic_gicr_copy_in(gicr_vcpu->addrspace,
				  gicr_vcpu->vgic_vlpi_pending_table,
				  pending_table_size, base,
				  pending_table_reserved, vm_table_size);

		// Zero the remainder of the pending table
		if (vm_table_size < pending_table_size) {
			err_mem = memset_s(gicr_vcpu->vgic_vlpi_pending_table +
						   vm_table_size,
					   pending_table_size - vm_table_size,
					   0,
					   pending_table_size - vm_table_size);
			if (err_mem != 0) {
				panic("Error in memset_s operation!");
			}
		}
	}
	return ptz;
}

static void
vgic_gicr_copy_propbase_all(vic_t *vic, thread_t *gicr_vcpu,
			    bool zero_remainder)
{
	assert(vic != NULL);

	GICR_PROPBASER_t propbaser =
		atomic_load_relaxed(&vic->gicr_rd_propbaser);
	size_t config_table_size = util_bit(vic->gicd_idbits) - GIC_LPI_BASE;

	count_t	 idbits = util_min(GICR_PROPBASER_get_IDbits(&propbaser) + 1U,
				   vic->gicd_idbits);
	vmaddr_t base	= GICR_PROPBASER_get_PA(&propbaser);
	size_t	 vm_table_size = (util_bit(idbits) >= GIC_LPI_BASE)
					 ? (util_bit(idbits) - GIC_LPI_BASE)
					 : 0U;
	assert(vm_table_size <= config_table_size);

	vgic_gicr_copy_in(gicr_vcpu->addrspace, vic->vlpi_config_table,
			  config_table_size, base, 0U, vm_table_size);

	// Zero the remainder of the pending table
	if (zero_remainder && (vm_table_size < config_table_size)) {
		errno_t err_mem =
			memset_s(vic->vlpi_config_table + vm_table_size,
				 config_table_size - vm_table_size, 0,
				 config_table_size - vm_table_size);
		if (err_mem != 0) {
			panic("Error in memset_s operation!");
		}
	}
}

void
vgic_gicr_copy_propbase_one(vic_t *vic, thread_t *gicr_vcpu, irq_t vlpi)
{
	GICR_PROPBASER_t propbaser =
		atomic_load_relaxed(&vic->gicr_rd_propbaser);
	size_t config_table_size = util_bit(vic->gicd_idbits) - GIC_LPI_BASE;

	count_t idbits = util_min(GICR_PROPBASER_get_IDbits(&propbaser) + 1U,
				  vic->gicd_idbits);
	// Note that we only ever read these mappings (as writing back to them
	// is strictly optional in the spec) so we don't require write access.
	vmaddr_t base = GICR_PROPBASER_get_PA(&propbaser);

	// Ignore requests for out-of-range vLPI numbers
	if ((vlpi >= GIC_LPI_BASE) && (vlpi < util_bit(idbits))) {
		// Copy in a single byte
		vgic_gicr_copy_in(gicr_vcpu->addrspace, vic->vlpi_config_table,
				  config_table_size, base,
				  ((size_t)vlpi - (size_t)GIC_LPI_BASE),
				  ((size_t)vlpi - (size_t)GIC_LPI_BASE + 1U));
	}
}

#if GICV3_HAS_VLPI_V4_1
static void
vgic_update_vsgi(thread_t *gicr_vcpu, irq_t irq_num)
{
	// Note: we don't check whether vSGI delivery is enabled here; that is
	// only done when sending an SGI.
	_Atomic vgic_delivery_state_t *dstate =
		&gicr_vcpu->vgic_private_states[irq_num];
	vgic_delivery_state_t new_dstate = atomic_load_relaxed(dstate);

	// Note: as per the spec, this is a no-op if the vPE is not mapped.
	// The gicv3 driver may ignore the call in that case.
	(void)gicv3_its_vsgi_config(
		gicr_vcpu, irq_num,
		vgic_delivery_state_get_enabled(&new_dstate),
		vgic_delivery_state_get_group1(&new_dstate),
		vgic_delivery_state_get_priority(&new_dstate));
}

static void
vgic_setup_vcpu_vsgis(thread_t *vcpu)
{
	for (virq_t sgi = GIC_SGI_BASE; sgi < GIC_SGI_BASE + GIC_SGI_NUM;
	     sgi++) {
		vgic_update_vsgi(vcpu, sgi);
	}

	count_result_t sync_r = gicv3_its_vsgi_sync(vcpu);
	assert(sync_r.e == OK);
	atomic_store_release(&vcpu->vgic_vsgi_setup_seq, sync_r.r);
}

error_t
vgic_vsgi_assert(thread_t *gicr_vcpu, irq_t irq_num)
{
	error_t err;

	count_t setup_seq =
		atomic_load_acquire(&gicr_vcpu->vgic_vsgi_setup_seq);

	if (setup_seq == ~(count_t)0U) {
		// VSGI setup not queued yet
		err = ERROR_DENIED;
		goto out;
	}

	if (compiler_unexpected(setup_seq != 0U)) {
		bool_result_t complete_r =
			gicv3_its_vsgi_is_complete(setup_seq);
		assert(complete_r.e == OK);
		if (!complete_r.r) {
			// VSGI setup queued but VSYNC not complete yet
			err = ERROR_BUSY;
			goto out;
		}
		atomic_store_release(&gicr_vcpu->vgic_vsgi_setup_seq, 0U);
	}

	VGIC_TRACE(VIRQ_CHANGED, gicr_vcpu->vgic_vic, gicr_vcpu,
		   "sgi {:d}: send vsgi", irq_num);
	err = gicv3_its_vsgi_assert(gicr_vcpu, irq_num);

out:
	return err;
}
#endif

static error_t
vgic_gicr_enable_lpis(vic_t *vic, thread_t *gicr_vcpu)
{
	assert(vic != NULL);
	assert(vgic_has_lpis(vic));
	assert(vic->vlpi_config_table != NULL);
	assert(gicr_vcpu != NULL);
	assert(gicr_vcpu->vgic_vlpi_pending_table != NULL);

	GICR_PROPBASER_t propbaser =
		atomic_load_relaxed(&vic->gicr_rd_propbaser);
	count_t idbits = util_min(GICR_PROPBASER_get_IDbits(&propbaser) + 1U,
				  vic->gicd_idbits);

	// If this is the first VCPU to enable LPIs, we need to copy the
	// LPI configurations from the virtual GICR_PROPBASER. This is not
	// done for subsequent enables; LPI configuration changes must raise
	// explicit invalidates after that point.
	spinlock_acquire(&vic->gicd_lock);
	if (!vic->vlpi_config_valid) {
		vgic_gicr_copy_propbase_all(vic, gicr_vcpu, true);
		vic->vlpi_config_valid = true;
	}
	spinlock_release(&vic->gicd_lock);

	// If the virtual GICR_PENDBASER has the PTZ bit clear when LPIs are
	// enabled, we need to copy the VCPU's VLPI pending states from the
	// virtual GICR_PENDBASER. Otherwise we just zero the VLPI pending
	// states and ignore the GICR_PENDBASER PA entirely.
	//
	// Note that the spec does not require us to ever write back to the
	// pending table.
	bool pending_zeroed = vgic_gicr_copy_pendbase(vic, idbits, gicr_vcpu);

	// Call the ITS driver to map the VCPU into the VPE table.
	paddr_t config_table_phys = partition_virt_to_phys(
		vic->header.partition, (uintptr_t)vic->vlpi_config_table);
	assert(config_table_phys != PADDR_INVALID);
	size_t	config_table_size  = util_bit(vic->gicd_idbits) - GIC_LPI_BASE;
	paddr_t pending_table_phys = partition_virt_to_phys(
		gicr_vcpu->header.partition,
		(uintptr_t)gicr_vcpu->vgic_vlpi_pending_table);
	assert(pending_table_phys != PADDR_INVALID);
	size_t pending_table_size =
		BITMAP_NUM_WORDS(util_bit(vic->gicd_idbits)) *
		sizeof(register_t);
	error_t err = gicv3_its_vpe_map(gicr_vcpu, vic->gicd_idbits,
					config_table_phys, config_table_size,
					pending_table_phys, pending_table_size,
					pending_zeroed);

#if GICV3_HAS_VLPI_V4_1
	if (err == OK) {
		// Tell the ITS about the vPE's vSGI configuration.
		spinlock_acquire(&vic->gicd_lock);
		vgic_setup_vcpu_vsgis(gicr_vcpu);
		spinlock_release(&vic->gicd_lock);
	}
#endif

	if (gicr_vcpu == thread_get_self()) {
		preempt_disable();
		vgic_vpe_schedule_current();
		preempt_enable();
	}

	return err;
}
#endif // VGIC_HAS_LPI

void
vgic_gicr_rd_set_control(vic_t *vic, thread_t *gicr_vcpu, GICR_CTLR_t ctlr)
{
#if VGIC_HAS_LPI
	bool enable_lpis = GICR_CTLR_get_Enable_LPIs(&ctlr) &&
			   vgic_has_lpis(vic);

	if (enable_lpis) {
		GICR_CTLR_t ctlr_enable_lpis = GICR_CTLR_default();
		GICR_CTLR_set_Enable_LPIs(&ctlr_enable_lpis, true);
		GICR_CTLR_t old_ctlr = GICR_CTLR_atomic_union(
			&gicr_vcpu->vgic_gicr_rd_ctlr, ctlr_enable_lpis,
			memory_order_acquire);
		bool old_enable_lpis = GICR_CTLR_get_Enable_LPIs(&old_ctlr);

		if (!old_enable_lpis) {
			error_t err = vgic_gicr_enable_lpis(vic, gicr_vcpu);
			if (err != OK) {
				// LPI enable failed; clear the enable bit.
				TRACE_AND_LOG(ERROR, WARN,
					      "vgicr: LPI enable failed: {:d}",
					      (register_t)err);
				(void)GICR_CTLR_atomic_difference(
					&gicr_vcpu->vgic_gicr_rd_ctlr,
					ctlr_enable_lpis, memory_order_release);
			}
		}
	}
#else
	(void)vic;
	(void)gicr_vcpu;
	(void)ctlr;
#endif
}

GICR_CTLR_t
vgic_gicr_rd_get_control(vic_t *vic, thread_t *gicr_vcpu)
{
	(void)vic;

#if VGIC_HAS_LPI
	GICR_CTLR_t ctlr = atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd_ctlr);
#if GICV3_HAS_VLPI_V4_1
	bool_result_t disabled_r =
		gicv3_its_vsgi_is_complete(gicr_vcpu->vgic_vsgi_disable_seq);
	if ((disabled_r.e == OK) && !disabled_r.r) {
		GICR_CTLR_set_RWP(&ctlr, true);
	}
#endif
#else
	(void)gicr_vcpu;
	GICR_CTLR_t ctlr = GICR_CTLR_default();
#endif

	return ctlr;
}

void
vgic_gicr_rd_set_statusr(thread_t *gicr_vcpu, GICR_STATUSR_t statusr, bool set)
{
	if (set) {
		(void)GICR_STATUSR_atomic_union(
			&gicr_vcpu->vgic_gicr_rd_statusr, statusr,
			memory_order_relaxed);
	} else {
		(void)GICR_STATUSR_atomic_difference(
			&gicr_vcpu->vgic_gicr_rd_statusr, statusr,
			memory_order_relaxed);
	}
}

#if VGIC_HAS_LPI
void
vgic_gicr_rd_set_propbase(vic_t *vic, GICR_PROPBASER_t propbase)
{
	GICR_PROPBASER_t new_propbase = GICR_PROPBASER_default();

	// We implement the cache and shareability fields as read-only to
	// reflect the fact that the hypervisor always accesses the table
	// through its own shared cacheable mapping.
	GICR_PROPBASER_set_OuterCache(&new_propbase, 0U);
	GICR_PROPBASER_set_InnerCache(&new_propbase, 7U);
	GICR_PROPBASER_set_Shareability(&new_propbase, 1U);

	// Use the physical address and size provided by the VM.
	GICR_PROPBASER_copy_PA(&new_propbase, &propbase);
	GICR_PROPBASER_copy_IDbits(&new_propbase, &propbase);

	// There is no need to synchronise or update anything else here. This
	// value is only used when EnableLPIs changes to 1 or an explicit
	// invalidate is processed.
	atomic_store_relaxed(&vic->gicr_rd_propbaser, new_propbase);
}

void
vgic_gicr_rd_set_pendbase(vic_t *vic, thread_t *gicr_vcpu,
			  GICR_PENDBASER_t pendbase)
{
	(void)vic;

	GICR_PENDBASER_t new_pendbase = GICR_PENDBASER_default();

	// We implement the cache and shareability fields as read-only to
	// reflect the fact that the hypervisor always accesses the table
	// through its own shared cacheable mapping.
	GICR_PENDBASER_set_OuterCache(&new_pendbase, 0U);
	GICR_PENDBASER_set_InnerCache(&new_pendbase, 7U);
	GICR_PENDBASER_set_Shareability(&new_pendbase, 1U);

	// Use the physical address provided by the VM.
	GICR_PENDBASER_set_PA(&new_pendbase, GICR_PENDBASER_get_PA(&pendbase));

	// Copy the PTZ bit. When the VM sets EnableLPIs to 1, this will
	// determine the cache update behaviour and the VMAPP command's PTZ bit.
	// However, the read trap will always zero this.
	GICR_PENDBASER_set_PTZ(&new_pendbase,
			       GICR_PENDBASER_get_PTZ(&pendbase));

	// There is no need to synchronise or update anything else here. This
	// value is only used when EnableLPIs changes to 1 or an explicit
	// invalidate is processed.
	atomic_store_relaxed(&gicr_vcpu->vgic_gicr_rd_pendbaser, new_pendbase);
}

void
vgic_gicr_rd_invlpi(vic_t *vic, thread_t *gicr_vcpu, virq_t vlpi_num)
{
	if (vic->vlpi_config_valid) {
		vgic_gicr_copy_propbase_one(vic, gicr_vcpu, vlpi_num);
		gicv3_vlpi_inv_by_id(gicr_vcpu, vlpi_num);
	}
}

void
vgic_gicr_rd_invall(vic_t *vic, thread_t *gicr_vcpu)
{
	if (vic->vlpi_config_valid) {
		vgic_gicr_copy_propbase_all(vic, gicr_vcpu, false);
		gicv3_vlpi_inv_all(gicr_vcpu);
	}
}

bool
vgic_gicr_get_inv_pending(vic_t *vic, thread_t *gicr_vcpu)
{
	return vic->vlpi_config_valid && gicv3_vlpi_inv_pending(gicr_vcpu);
}
#endif

void
vgic_gicr_sgi_change_sgi_ppi_pending(vic_t *vic, thread_t *gicr_vcpu,
				     irq_t irq_num, bool set)
{
	assert(vgic_irq_is_private(irq_num));

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	if (!vgic_irq_is_ppi(irq_num) && vic->vsgis_enabled) {
		if (set) {
			if (vgic_vsgi_assert(gicr_vcpu, irq_num) == OK) {
				// Delivered by ITS
				goto out;
			}
			// Need to deliver in software instead; fall through
		} else {
			(void)gicv3_its_vsgi_clear(gicr_vcpu, irq_num);
			// Might be pending in software too; fall through
		}
	}
#endif

	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, gicr_vcpu, irq_num);
	vgic_change_irq_pending(vic, gicr_vcpu, irq_num, true, source, set,
				false);
	rcu_read_finish();

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
out:
	return;
#endif
}

void
vgic_gicr_sgi_change_sgi_ppi_enable(vic_t *vic, thread_t *gicr_vcpu,
				    irq_t irq_num, bool set)
{
	assert(vgic_irq_is_private(irq_num));

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	// Take the distributor lock for SGIs to ensure that vSGI config changes
	// by different CPUs don't end up out of order in the ITS.
	spinlock_acquire(&vic->gicd_lock);
#else
	preempt_disable();
#endif

	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, gicr_vcpu, irq_num);

	assert((source == NULL) ||
	       (source->trigger != VIRQ_TRIGGER_VGIC_FORWARDED_SPI));

	vgic_change_irq_enable(vic, gicr_vcpu, irq_num, true, source, set);

	rcu_read_finish();

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	if (!vgic_irq_is_ppi(irq_num) && vgic_has_lpis(vic)) {
		vgic_update_vsgi(gicr_vcpu, irq_num);
		if (!set) {
			count_result_t seq_r = gicv3_its_vsgi_sync(gicr_vcpu);
			if (seq_r.e == OK) {
				gicr_vcpu->vgic_vsgi_disable_seq = seq_r.r;
			}
		}
	}
	spinlock_release(&vic->gicd_lock);
#else
	preempt_enable();
#endif
}

void
vgic_gicr_sgi_change_sgi_ppi_active(vic_t *vic, thread_t *gicr_vcpu,
				    irq_t irq_num, bool set)
{
	assert(vgic_irq_is_private(irq_num));

	vgic_change_irq_active(vic, gicr_vcpu, irq_num, set);
}

void
vgic_gicr_sgi_set_sgi_ppi_group(vic_t *vic, thread_t *gicr_vcpu, irq_t irq_num,
				bool is_group_1)
{
	assert(vgic_irq_is_private(irq_num));

#if GICV3_HAS_VLPI_V4_1
	// Take the distributor lock for SGIs to ensure that two config changes
	// by different CPUs don't end up out of order in the ITS.
	spinlock_acquire(&vic->gicd_lock);
#endif

	_Atomic vgic_delivery_state_t *dstate =
		&gicr_vcpu->vgic_private_states[irq_num];

	vgic_sync_group_change(vic, irq_num, dstate, is_group_1);

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	if (!vgic_irq_is_ppi(irq_num) && vgic_has_lpis(vic)) {
		vgic_update_vsgi(gicr_vcpu, irq_num);
	}
	spinlock_release(&vic->gicd_lock);
#endif
}

void
vgic_gicr_sgi_set_sgi_ppi_priority(vic_t *vic, thread_t *gicr_vcpu,
				   irq_t irq_num, uint8_t priority)
{
	assert(vgic_irq_is_private(irq_num));

	spinlock_acquire(&vic->gicd_lock);

	vgic_set_irq_priority(vic, gicr_vcpu, irq_num, priority);

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	if (!vgic_irq_is_ppi(irq_num) && vgic_has_lpis(vic)) {
		vgic_update_vsgi(gicr_vcpu, irq_num);
	}
#endif

	spinlock_release(&vic->gicd_lock);
}

void
vgic_gicr_sgi_set_ppi_config(vic_t *vic, thread_t *gicr_vcpu, irq_t irq_num,
			     bool is_edge)
{
	assert(vgic_irq_is_ppi(irq_num));
	assert(vic != NULL);
	assert(gicr_vcpu != NULL);

	// Take the GICD lock to ensure that concurrent writes don't make the
	// dstate and GICR register views of the config inconsistent
	spinlock_acquire(&vic->gicd_lock);

	// Update the delivery state.
	//
	// There is no need to synchronise: changing this configuration while
	// the interrupt is enabled and pending has an UNKNOWN effect on the
	// interrupt's pending state.
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, gicr_vcpu, irq_num);
	vgic_delivery_state_t change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_cfg_is_edge(&change_dstate, true);
	if (is_edge) {
		(void)vgic_delivery_state_atomic_union(dstate, change_dstate,
						       memory_order_relaxed);
	} else {
		// Also clear any leftover software level assertions.
		vgic_delivery_state_set_level_sw(&change_dstate, true);
		vgic_delivery_state_set_level_msg(&change_dstate, true);
		(void)vgic_delivery_state_atomic_difference(
			dstate, change_dstate, memory_order_relaxed);
	}

	spinlock_release(&vic->gicd_lock);
}

error_t
vic_bind_shared(virq_source_t *source, vic_t *vic, virq_t virq,
		virq_trigger_t trigger)
{
	error_t ret;

	if (atomic_fetch_or_explicit(&source->vgic_is_bound, true,
				     memory_order_acquire)) {
		ret = ERROR_VIRQ_BOUND;
		goto out;
	}
	assert(atomic_load_relaxed(&source->vic) == NULL);

	if (vgic_get_irq_type(virq) != VGIC_IRQ_TYPE_SPI) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_release;
	}

	if ((virq - GIC_SPI_BASE) >= vic->sources_count) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_release;
	}

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);

	source->virq		= virq;
	source->trigger		= trigger;
	source->is_private	= false;
	source->vgic_gicr_index = CPU_INDEX_INVALID;

	rcu_read_start();
	virq_source_t *_Atomic *attach_ptr = &vic->sources[virq - GIC_SPI_BASE];
	virq_source_t	       *old_source = atomic_load_acquire(attach_ptr);
	do {
		// If there is already a source bound, we can't bind another.
		if (old_source != NULL) {
			ret = ERROR_BUSY;
			goto out_rcu_release;
		}

		// If the previous source for this VIRQ was a forwarded SPI,
		// we can't bind a new forwarded SPI until the old one has been
		// removed from the LRs and deactivated, to avoid any ambiguity
		// in the meanings of the hw_active and hw_deactivated bits in
		// the delivery state. In that case, ask the caller to try
		// again.
		if (trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI) {
			vgic_delivery_state_t current_dstate =
				atomic_load_relaxed(dstate);
			if (vgic_delivery_state_get_hw_detached(
				    &current_dstate)) {
				assert(vgic_delivery_state_get_listed(
					&current_dstate));
				ret = ERROR_RETRY;
				goto out_rcu_release;
			}
		}

		ret = OK;
	} while (!atomic_compare_exchange_strong_explicit(
		attach_ptr, &old_source, source, memory_order_acq_rel,
		memory_order_acquire));

	if (ret == OK) {
		atomic_store_release(&source->vic, vic);
	}
out_rcu_release:
	rcu_read_finish();

out_release:
	if (ret != OK) {
		atomic_store_release(&source->vgic_is_bound, false);
	}

out:
	return ret;
}

static error_t
vic_bind_private(virq_source_t *source, vic_t *vic, thread_t *vcpu, virq_t virq,
		 virq_trigger_t trigger)
{
	error_t ret;

	if (vgic_get_irq_type(virq) != VGIC_IRQ_TYPE_PPI) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	assert(vic != NULL);
	assert(atomic_load_relaxed(&vic->header.state) == OBJECT_STATE_ACTIVE);

	if (atomic_fetch_or_explicit(&source->vgic_is_bound, true,
				     memory_order_acquire)) {
		ret = ERROR_VIRQ_BOUND;
		goto out_release;
	}
	assert(atomic_load_relaxed(&source->vic) == NULL);

	source->virq		= virq;
	source->trigger		= trigger;
	source->is_private	= true;
	source->vgic_gicr_index = vcpu->vgic_gicr_index;

	spinlock_acquire(&vic->gicd_lock);
	if (atomic_load_relaxed(&vic->gicr_vcpus[vcpu->vgic_gicr_index]) !=
	    vcpu) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_locked;
	}

	virq_source_t *old_source = NULL;
	if (!atomic_compare_exchange_strong_explicit(
		    &vcpu->vgic_sources[virq - GIC_PPI_BASE], &old_source,
		    source, memory_order_release, memory_order_relaxed)) {
		ret = ERROR_BUSY;
	} else {
		atomic_store_release(&source->vic, vic);
		ret = OK;
	}

out_locked:
	spinlock_release(&vic->gicd_lock);

out_release:
	if (ret != OK) {
		atomic_store_release(&source->vgic_is_bound, false);
	}
out:
	return ret;
}

error_t
vic_bind_private_vcpu(virq_source_t *source, thread_t *vcpu, virq_t virq,
		      virq_trigger_t trigger)
{
	error_t ret;

	assert(source != NULL);
	assert(vcpu != NULL);

	vic_t *vic = vcpu->vgic_vic;
	if (vic == NULL) {
		ret = ERROR_ARGUMENT_INVALID;
	} else {
		ret = vic_bind_private(source, vic, vcpu, virq, trigger);
	}

	return ret;
}

error_t
vic_bind_private_index(virq_source_t *source, vic_t *vic, index_t index,
		       virq_t virq, virq_trigger_t trigger)
{
	error_t ret;

	assert(source != NULL);
	assert(vic != NULL);

	if (index >= vic->gicr_count) {
		ret = ERROR_ARGUMENT_INVALID;
	} else {
		rcu_read_start();

		thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[index]);

		if (vcpu == NULL) {
			ret = ERROR_OBJECT_CONFIG;
		} else {
			ret = vic_bind_private(source, vic, vcpu, virq,
					       trigger);
		}

		rcu_read_finish();
	}

	return ret;
}

error_t
vic_bind_private_forward_private(virq_source_t *source, vic_t *vic,
				 thread_t *vcpu, virq_t virq, irq_t pirq,
				 cpu_index_t pcpu)
{
	error_t ret;

	assert(source != NULL);
	assert(vic != NULL);
	assert(vcpu != NULL);

	if (vgic_get_irq_type(virq) != VGIC_IRQ_TYPE_PPI) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ret = vic_bind_private_vcpu(source, vcpu, virq,
				    VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE);
	if (ret != OK) {
		goto out;
	}

	// Take the GICD lock to ensure that the vGIC's IRQ config does
	// not change while we are copying it to the hardware GIC
	spinlock_acquire(&vic->gicd_lock);

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);

	bool is_edge = vgic_delivery_state_get_cfg_is_edge(&current_dstate);
	irq_trigger_t mode = is_edge ? IRQ_TRIGGER_EDGE_RISING
				     : IRQ_TRIGGER_LEVEL_HIGH;

	irq_trigger_result_t new_mode = trigger_virq_set_mode_event(
		VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE, source, mode);
	if ((new_mode.e != OK) || (new_mode.r != mode)) {
		vgic_delivery_state_t cfg_is_edge =
			vgic_delivery_state_default();
		vgic_delivery_state_set_cfg_is_edge(&cfg_is_edge, true);
		// Mode change failed; the hardware config must be fixed to the
		// other mode. Flip the software mode.
		if (is_edge) {
			(void)vgic_delivery_state_atomic_intersection(
				dstate, cfg_is_edge, memory_order_relaxed);
		} else {
			(void)vgic_delivery_state_atomic_difference(
				dstate, cfg_is_edge, memory_order_relaxed);
		}
	}

	// Enable the HW IRQ if the virtual enable bit is set (unbound
	// HW IRQs are always disabled).
	if (vgic_delivery_state_get_enabled(&current_dstate)) {
		platform_irq_enable_percpu(pirq, pcpu);
	}

	spinlock_release(&vic->gicd_lock);

out:
	return ret;
}

static error_t
vic_do_unbind(virq_source_t *source)
{
	error_t err = ERROR_VIRQ_NOT_BOUND;

	rcu_read_start();

	vic_t *vic = atomic_exchange_explicit(&source->vic, NULL,
					      memory_order_consume);
	if (vic == NULL) {
		// The VIRQ is not bound
		goto out;
	}

	// Try to find the current target VCPU. This may be inaccurate or NULL
	// for a shared IRQ, but must be correct for a private IRQ.
	thread_t *vcpu = vgic_find_target(vic, source);
	if (source->is_private && (vcpu == NULL)) {
		// The VIRQ has been concurrently unbound.
		goto out;
	}

	// Clear the level_src and hw_active bits in the delivery state.
	// The latter bit will implicitly detach and deactivate the physical
	// IRQ, if there is one.
	vgic_delivery_state_t clear_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_level_src(&clear_dstate, true);
	vgic_delivery_state_set_hw_active(&clear_dstate, true);

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	if (!vgic_undeliver(vic, vcpu, dstate, source->virq, clear_dstate,
			    false)) {
		// The VIRQ is still listed somewhere. For HW sources this can
		// delay both re-registration of the VIRQ and delivery of the
		// HW IRQ (after it is re-registered elsewhere), so start a
		// sync to ensure that delisting happens soon.
		vgic_sync_all(vic, false);
	}

	// Remove the source from the IRQ source array. Note that this must
	// be ordered after the level_src bit is cleared in the undeliver, to
	// ensure that other threads don't see this NULL pointer while the
	// level_src or hw_active bits are still set.
	virq_source_t	       *registered_source = source;
	virq_source_t *_Atomic *registered_source_ptr =
		source->is_private
			? &vcpu->vgic_sources[source->virq - GIC_PPI_BASE]
			: &vic->sources[source->virq - GIC_SPI_BASE];
	if (!atomic_compare_exchange_strong_explicit(
		    registered_source_ptr, &registered_source, NULL,
		    memory_order_release, memory_order_relaxed)) {
		// Somebody else has already released the VIRQ
		goto out;
	}

	err = OK;
out:
	rcu_read_finish();
	return err;
}

void
vic_unbind(virq_source_t *source)
{
	(void)vic_do_unbind(source);
}

void
vic_unbind_sync(virq_source_t *source)
{
	if (vic_do_unbind(source) == OK) {
		// Ensure that any remote operations affecting the source object
		// and the unbound VIRQ have completed.
		rcu_sync();

		// Mark the source as no longer bound.
		atomic_store_release(&source->vgic_is_bound, false);
	}
}

static bool_result_t
virq_do_assert(virq_source_t *source, bool edge_only, bool is_hw)
{
	bool_result_t ret;

	// The source's VIC pointer and the target VCPU are RCU-protected.
	rcu_read_start();

	// We must have a VIC to deliver to. Note that we use load-acquire here
	// rather than the usual load-consume, to ensure that we only read the
	// other fields in the source after they have been set.
	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		ret = bool_result_error(ERROR_VIRQ_NOT_BOUND);
		goto out;
	}

	// Choose a target VCPU to deliver to.
#if VGIC_HAS_1N
	thread_t *vcpu = NULL;

	if (source->is_private) {
		vcpu = vgic_find_target(vic, source);
		if (vcpu == NULL) {
			// The VIRQ has been concurrently unbound.
			ret = bool_result_error(ERROR_VIRQ_NOT_BOUND);
			goto out;
		}
	} else {
		// A shared VIRQ might be 1-of-N, and vgic_find_target() will
		// return NULL in that case, so we can't use it.
		vcpu = vgic_get_route_for_spi(vic, source->virq, is_hw);
	}
#else  // !VGIC_HAS_1N
	thread_t *vcpu = vgic_find_target(vic, source);
	if (source->is_private && (vcpu == NULL)) {
		// The VIRQ has been concurrently unbound.
		ret = bool_result_error(ERROR_VIRQ_NOT_BOUND);
		goto out;
	}
#endif // VGIC_HAS_1N

	// Deliver the interrupt to the target
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	vgic_delivery_state_t assert_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_edge(&assert_dstate, true);
	if (!edge_only) {
		vgic_delivery_state_set_level_src(&assert_dstate, true);
	}
	if (is_hw) {
		vgic_delivery_state_set_hw_active(&assert_dstate, true);
	}

	vgic_delivery_state_t old_dstate =
		vgic_deliver(source->virq, vic, vcpu, source, dstate,
			     assert_dstate, source->is_private);

	ret = bool_result_ok(vgic_delivery_state_get_cfg_is_edge(&old_dstate));
out:
	rcu_read_finish();

	return ret;
}

bool_result_t
virq_assert(virq_source_t *source, bool edge_only)
{
	return virq_do_assert(source, edge_only, false);
}

// Handle a hardware SPI that is forwarded as a VIRQ.
bool
vgic_handle_irq_received_forward_spi(hwirq_t *hwirq)
{
	assert(hwirq != NULL);
	assert(hwirq->vgic_spi_source.trigger ==
	       VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

	bool deactivate = false;

	bool_result_t ret =
		virq_do_assert(&hwirq->vgic_spi_source, false, true);

	if (compiler_unexpected(ret.e != OK)) {
		// Delivery failed, so disable the HW IRQ.
		irq_disable_shared_nosync(hwirq);
		deactivate = true;
	}

	return deactivate;
}

static error_t
vgic_set_mpidr_mapping(vic_t *vic, MPIDR_EL1_t mask, count_t aff0_shift,
		       count_t aff1_shift, count_t aff2_shift,
		       count_t aff3_shift, bool mt)
{
	uint64_t      cpuindex_mask = 0U;
	const count_t shifts[4]	    = { aff0_shift, aff1_shift, aff2_shift,
					aff3_shift };
	const uint8_t masks[4]	    = { MPIDR_EL1_get_Aff0(&mask),
					MPIDR_EL1_get_Aff1(&mask),
					MPIDR_EL1_get_Aff2(&mask),
					MPIDR_EL1_get_Aff3(&mask) };
	error_t	      err;

	for (index_t i = 0U; i < 4U; i++) {
		// Since there are only 32 significant affinity bits, a shift of
		// more than 32 can't be useful, so don't allow it.
		if (shifts[i] >= 32U) {
			err = ERROR_ARGUMENT_INVALID;
			goto out;
		}

		// Collect the output bits, checking that there's no overlap.
		uint64_t field_mask = (uint64_t)masks[i] << shifts[i];
		if ((cpuindex_mask & field_mask) != 0U) {
			err = ERROR_ARGUMENT_INVALID;
			goto out;
		}
		cpuindex_mask |= field_mask;
	}

	// We don't allow sparse mappings, so check that the output bits are
	// contiguous and start from the least significant bit. This is true if
	// the mask is one less than a power of two.
	//
	// Also, the mask has to fit in cpu_index_t, and must not be able to
	// produce CPU_INDEX_INVALID, which currently limits it to 15 bits.
	if (!util_is_p2(cpuindex_mask + 1U) ||
	    (cpuindex_mask >= CPU_INDEX_INVALID)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// Note: we currently don't check that the mapping can assign unique
	// MPIDR values to all VCPUs. If it doesn't, the VM will probably fail
	// to boot or at least fail to start the VCPUs with duplicated values,
	// but the hypervisor itself will not fail.

	// Construct and set the mapping.
	vic->mpidr_mapping = (platform_mpidr_mapping_t){
		.aff_shift    = { shifts[0], shifts[1], shifts[2], shifts[3] },
		.aff_mask     = { masks[0], masks[1], masks[2], masks[3] },
		.multi_thread = mt,
		.uniprocessor = (cpuindex_mask == 0U),
	};
	err = OK;

out:
	return err;
}

error_t
hypercall_vgic_set_mpidr_mapping(cap_id_t vic_cap, uint64_t mask,
				 count_t aff0_shift, count_t aff1_shift,
				 count_t aff2_shift, count_t aff3_shift,
				 bool mt)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, vic_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_released;
	}
	if (type != OBJECT_TYPE_VIC) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_unlocked;
	}
	vic_t *vic = o.r.vic;

	spinlock_acquire(&vic->header.lock);
	if (atomic_load_relaxed(&vic->header.state) == OBJECT_STATE_INIT) {
		err = vgic_set_mpidr_mapping(vic, MPIDR_EL1_cast(mask),
					     aff0_shift, aff1_shift, aff2_shift,
					     aff3_shift, mt);
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vic->header.lock);

out_unlocked:
	object_put(type, o.r);
out_released:

	return err;
}
