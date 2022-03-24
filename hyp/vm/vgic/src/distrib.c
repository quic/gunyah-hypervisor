// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypcontainers.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
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
#include <vic.h>
#include <virq.h>

#include <events/vic.h>
#include <events/virq.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "internal.h"
#include "useraccess.h"
#include "vgic.h"
#include "vic_base.h"

error_t
vgic_handle_object_create_vic(vic_create_t vic_create)
{
	struct vic *vic = vic_create.vic;
	assert(vic != NULL);
	struct partition *partition = vic->header.partition;
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
	GICD_CTLR_DS_set_E1NWF(&ctlr, true);
#endif
	atomic_init(&vic->gicd_ctlr, ctlr);

	return OK;
}

error_t
vic_configure(vic_t *vic, count_t max_vcpus, count_t max_virqs,
	      count_t max_msis)
{
	error_t err = OK;

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

	if (max_msis != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

out:
	return err;
}

bool
vgic_has_lpis(vic_t *vic)
{
	(void)vic;
	return false;
}

error_t
vgic_handle_object_activate_vic(vic_t *vic)
{
	struct partition *partition = vic->header.partition;
	assert(partition != NULL);
	error_t		  err = OK;
	void_ptr_result_t alloc_r;

	assert(vic->sources_count <= GIC_SPI_NUM);
	size_t sources_size = sizeof(vic->sources[0U]) * vic->sources_count;

	assert(vic->gicr_count > 0U);
	assert(vic->gicr_count <= PLATFORM_MAX_CORES);
	size_t vcpus_size = sizeof(vic->gicr_vcpus[0U]) * vic->gicr_count;

	if (sources_size != 0U) {
		alloc_r = partition_alloc(partition, sources_size,
					  alignof(vic->sources[0U]));
		if (alloc_r.e != OK) {
			err = alloc_r.e;
			goto out;
		}
		memset(alloc_r.r, 0, sources_size);
		vic->sources = (virq_source_t *_Atomic *)alloc_r.r;
	}

	alloc_r = partition_alloc(partition, vcpus_size,
				  alignof(vic->gicr_vcpus[0U]));
	if (alloc_r.e != OK) {
		err = alloc_r.e;
		goto out;
	}
	memset(alloc_r.r, 0, vcpus_size);
	vic->gicr_vcpus = (thread_t *_Atomic *)alloc_r.r;

out:
	// We can't free anything here; it will be done in cleanup

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
}

void
vgic_handle_object_cleanup_vic(vic_t *vic)
{
	struct partition *partition = vic->header.partition;

	if (vic->gicr_vcpus != NULL) {
		size_t vcpus_size =
			sizeof(vic->gicr_vcpus[0]) * vic->gicr_count;
		partition_free(partition, vic->gicr_vcpus, vcpus_size);
		vic->gicr_vcpus = NULL;
	}

	if (vic->sources != NULL) {
		size_t sources_size =
			sizeof(vic->sources[0]) * vic->sources_count;
		partition_free(partition, vic->sources, sources_size);
		vic->sources = NULL;
	}
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
		// The sleep flag is initially clear. This has no real effect on
		// guests with GICR_WAKER awareness (like Linux), but allows
		// interrupt delivery to work correctly for guests that assume
		// they have a non-secure view of the GIC (like UEFI).
		atomic_init(&vcpu->vgic_sleep, false);

		vcpu->vgic_ich_hcr = ICH_HCR_EL2_default();

		// Trap changes to the group enable bits.
#if defined(ARCH_ARM_8_6_FGT) && ARCH_ARM_8_6_FGT
		// Use fine-grained traps of the enable registers if they are
		// available, so we don't have to emulate the other registers
		// trapped by TALL[01].
		HFGWTR_EL2_set_ICC_IGRPENn_EL1(&vcpu->vcpu_regs_el2.hfgwtr_el2,
					       true);
#else
		// Trap all accesses for disabled groups. Note that these traps
		// and the group disable maintenance IRQs are toggled every time
		// we update the group enables.
		//
		// We can't use the group enable maintenance IRQs, because their
		// latency is high enough that a VCPU's idle loop might enable
		// the groups and then disable them again before we know they've
		// been enabled, causing it to get stuck in a loop being woken
		// by IRQs that are never delivered.
		ICH_HCR_EL2_set_TALL0(&vcpu->vgic_ich_hcr, true);
		ICH_HCR_EL2_set_TALL1(&vcpu->vgic_ich_hcr, true);
#endif

		// Always set LRENPIE, and keep UIE off. This is because we
		// don't reload active interrupts into the LRs once they've been
		// kicked out; the complexity of doing that outweighs any
		// performance benefit, especially when most VMs are Linux -
		// which uses neither EOImode (in EL1) nor preemption, and
		// therefore will never have multiple active IRQs to trigger
		// this in the first place.
		ICH_HCR_EL2_set_UIE(&vcpu->vgic_ich_hcr, false);
		ICH_HCR_EL2_set_LRENPIE(&vcpu->vgic_ich_hcr, true);
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

static void
vic_set_mpidr_by_index(thread_t *thread, cpu_index_t index)
{
	psci_mpidr_t ret  = platform_cpu_index_to_mpidr(index);
	MPIDR_EL1_t  real = register_MPIDR_EL1_read();

	thread->vcpu_regs_mpidr_el1 = MPIDR_EL1_default();
	MPIDR_EL1_set_Aff0(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff0(&ret));
	MPIDR_EL1_set_Aff1(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff1(&ret));
	MPIDR_EL1_set_Aff2(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff2(&ret));
	MPIDR_EL1_set_Aff3(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff3(&ret));
	MPIDR_EL1_set_MT(&thread->vcpu_regs_mpidr_el1, MPIDR_EL1_get_MT(&real));
}

error_t
vgic_handle_object_activate_thread(thread_t *vcpu)
{
	error_t err = OK;
	vic_t  *vic = vcpu->vgic_vic;

	if (vic != NULL) {
		if (vic->gicr_count > 1U) {
			// When there is no vpm_group (psci) attached, we need
			// to update the vcpu's MPIDR to the vgic
			// configuration.
			// The default MPIDR is flagged as uniprocessor when
			// not initialized by vpm_group.
			MPIDR_EL1_t mpidr_default = MPIDR_EL1_default();
			MPIDR_EL1_set_U(&mpidr_default, true);

			if (MPIDR_EL1_is_equal(mpidr_default,
					       vcpu->vcpu_regs_mpidr_el1)) {
				vic_set_mpidr_by_index(
					vcpu,
					(cpu_index_t)vcpu->vgic_gicr_index);
			}
		}

		spinlock_acquire(&vic->gicd_lock);

		psci_mpidr_t route_id = psci_mpidr_default();
		psci_mpidr_set_Aff0(
			&route_id,
			MPIDR_EL1_get_Aff0(&vcpu->vcpu_regs_mpidr_el1));
		psci_mpidr_set_Aff1(
			&route_id,
			MPIDR_EL1_get_Aff1(&vcpu->vcpu_regs_mpidr_el1));
		psci_mpidr_set_Aff2(
			&route_id,
			MPIDR_EL1_get_Aff2(&vcpu->vcpu_regs_mpidr_el1));
		psci_mpidr_set_Aff3(
			&route_id,
			MPIDR_EL1_get_Aff3(&vcpu->vcpu_regs_mpidr_el1));

		cpu_index_result_t cpu_r =
			platform_cpu_mpidr_to_index(route_id);
		if (cpu_r.e != OK) {
			err = cpu_r.e;
			goto out_locked;
		}
		if (cpu_r.r != vcpu->vgic_gicr_index) {
			err = ERROR_OBJECT_CONFIG;
			goto out_locked;
		}
		assert(cpu_r.r < vic->gicr_count);

		if (atomic_load_relaxed(&vic->gicr_vcpus[cpu_r.r]) != NULL) {
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
		vgic_delivery_state_set_route(&sgi_dstate,
					      vcpu->vgic_gicr_index);
		for (index_t i = 0; i < GIC_SGI_NUM; i++) {
			atomic_init(&vcpu->vgic_private_states[i], sgi_dstate);
		}
		// PPIs are normally level-triggered.
		vgic_delivery_state_t ppi_dstate =
			vgic_delivery_state_default();
		vgic_delivery_state_set_route(&ppi_dstate,
					      vcpu->vgic_gicr_index);
		for (index_t i = 0; i < GIC_PPI_NUM; i++) {
			atomic_init(
				&vcpu->vgic_private_states[GIC_PPI_BASE + i],
				ppi_dstate);
		}

		// Determine the physical interrupt route that should be used
		// for interrupts that target this VCPU.
		scheduler_lock_nopreempt(vcpu);
		cpu_index_t  affinity = scheduler_get_affinity(vcpu);
		psci_mpidr_t mpidr    = platform_cpu_index_to_mpidr(
			   cpulocal_index_valid(affinity) ? affinity : 0U);
		GICD_IROUTER_t phys_route = GICD_IROUTER_default();
		GICD_IROUTER_set_IRM(&phys_route, false);
		GICD_IROUTER_set_Aff0(&phys_route, psci_mpidr_get_Aff0(&mpidr));
		GICD_IROUTER_set_Aff1(&phys_route, psci_mpidr_get_Aff1(&mpidr));
		GICD_IROUTER_set_Aff2(&phys_route, psci_mpidr_get_Aff2(&mpidr));
		GICD_IROUTER_set_Aff3(&phys_route, psci_mpidr_get_Aff3(&mpidr));
		vcpu->vgic_irouter = phys_route;

		// Set the GICD's pointer to the VCPU. This is a store release
		// so we can be sure that all of the thread's initialisation is
		// complete before the VGIC tries to use it.
		atomic_store_release(&vic->gicr_vcpus[cpu_r.r], vcpu);

		scheduler_unlock_nopreempt(vcpu);
	out_locked:
		spinlock_release(&vic->gicd_lock);

		if (err == OK) {
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
	psci_mpidr_t   mpidr	  = platform_cpu_index_to_mpidr(next_cpu);
	GICD_IROUTER_t phys_route = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&phys_route, false);
	GICD_IROUTER_set_Aff0(&phys_route, psci_mpidr_get_Aff0(&mpidr));
	GICD_IROUTER_set_Aff1(&phys_route, psci_mpidr_get_Aff1(&mpidr));
	GICD_IROUTER_set_Aff2(&phys_route, psci_mpidr_get_Aff2(&mpidr));
	GICD_IROUTER_set_Aff3(&phys_route, psci_mpidr_get_Aff3(&mpidr));
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

		spinlock_release(&vic->gicd_lock);
	}
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

#if VGIC_HAS_1N
		// Wake any other threads on the GIC, in case the deferred IRQs
		// can be rerouted.
		vgic_sync_all(vic, true);
#endif

		object_put_vic(vic);
	}
}

void
vgic_handle_rootvm_init(partition_t *root_partition, thread_t *root_thread,
			cspace_t *root_cspace, boot_env_data_t *env_data)
{
	// Create the VIC object for the root VM
	vic_create_t	 vic_params = { 0 };
	vic_ptr_result_t vic_r =
		partition_allocate_vic(root_partition, vic_params);
	if (vic_r.e != OK) {
		goto vic_fail;
	}
	spinlock_acquire(&vic_r.r->header.lock);
#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	count_t max_vcpus = PLATFORM_MAX_CORES;
	count_t max_virqs = GIC_SPI_NUM;
	count_t max_msis = 0U;
#else
	count_t max_vcpus = 1U;
	count_t max_virqs = 64U;
	count_t max_msis  = 0U; // FIXME: for testing
#endif
	env_data->gicd_base   = PLATFORM_GICD_BASE;
	env_data->gicr_base   = PLATFORM_GICR_BASE;
	env_data->gicr_stride = (size_t)util_bit(GICR_STRIDE_SHIFT);

	if (vic_configure(vic_r.r, max_vcpus, max_virqs, max_msis) != OK) {
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
	env_data->vic = cid_r.r;

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	index_t vic_index = root_thread->scheduler_affinity;
#else
	index_t vic_index = 0U;
#endif

	if (vic_attach_vcpu(vic_r.r, root_thread, vic_index) != OK) {
		panic("VIC couldn't attach root VM thread");
	}

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	// Attach all secondary root VM threads to the VIC
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		thread_t *thread;
		if (i == root_thread->scheduler_affinity) {
			continue;
		} else {
			cap_id_t thread_cap = env_data->psci_secondary_vcpus[i];
			object_type_t	    type;
			object_ptr_result_t o = cspace_lookup_object_any(
				root_cspace, thread_cap,
				CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
			if ((o.e != OK) || (type != OBJECT_TYPE_THREAD)) {
				panic("VIC couldn't attach root VM thread");
			}
			thread = o.r.thread;
		}

		if (vic_attach_vcpu(vic_r.r, thread, i) != OK) {
			panic("VIC couldn't attach root VM thread");
		}

		object_put_thread(thread);
	}
#endif

	// Create a HWIRQ object for every SPI
	index_t i;
#if GICV3_EXT_IRQS
#error Extended SPIs and PPIs not handled yet
#endif
	index_t last_spi = util_min((count_t)platform_irq_max(),
				    GIC_SPI_BASE + GIC_SPI_NUM - 1U);
	assert(last_spi < util_array_size(env_data->vic_hwirq));
	for (i = 0; i <= last_spi; i++) {
		hwirq_create_t hwirq_params = {
			.irq = i,
		};

		if (gicv3_get_irq_type(i) == GICV3_IRQ_TYPE_SPI) {
			hwirq_params.action = HWIRQ_ACTION_VGIC_FORWARD_SPI;
		} else if (gicv3_get_irq_type(i) == GICV3_IRQ_TYPE_PPI) {
			hwirq_params.action =
				HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE;
		} else {
			// Don't try to register unhandled interrupt types
			env_data->vic_hwirq[i] = CSPACE_CAP_INVALID;
			continue;
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
				env_data->vic_hwirq[i] = CSPACE_CAP_INVALID;
				object_put_hwirq(hwirq_r.r);
				continue;
			} else {
				panic("Failed to activate HW IRQ object");
			}
		}

		// Create a master cap for the HWIRQ
		object_ptr_t hwirq_optr = { .hwirq = hwirq_r.r };
		cid_r = cspace_create_master_cap(root_cspace, hwirq_optr,
						 OBJECT_TYPE_HWIRQ);
		if (cid_r.e != OK) {
			panic("Unable to create cap to HWIRQ");
		}
		env_data->vic_hwirq[i] = cid_r.r;

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
		if (gicv3_get_irq_type(i) == GICV3_IRQ_TYPE_SPI) {
			// Bind the HW IRQ to the HLOS VIC
			error_t err = vgic_bind_hwirq_spi(vic_r.r, hwirq_r.r,
							  hwirq_params.irq);
			if (err != OK) {
				panic("Unable to bind HW SPI to HLOS VGIC");
			}
		} else if (gicv3_get_irq_type(i) == GICV3_IRQ_TYPE_PPI) {
			// Bind the HW IRQ to the HLOS VIC
			error_t err = vgic_bind_hwirq_forward_private(
				vic_r.r, hwirq_r.r, hwirq_params.irq);
			if (err != OK) {
				panic("Unable to bind HW PPI to HLOS VGIC");
			}
		}
#endif
	}
	for (; i < util_array_size(env_data->vic_hwirq); i++) {
		env_data->vic_hwirq[i] = CSPACE_CAP_INVALID;
	}

	// Fill in the msi source array with invalid caps, and zero the ITS
	// address range. The vgic_its module will write over these if necessary
	// (note that this handler has elevated priority, so vgic_its will run
	// later). They are part of this module's API to avoid an ABI dependency
	// on the presence of the vgic_its module.
	for (i = 0U; i < util_array_size(env_data->vic_msi_source); i++) {
		env_data->vic_msi_source[i] = CSPACE_CAP_INVALID;
	}
	env_data->gits_base   = 0U;
	env_data->gits_stride = 0U;

	return;

vic_fail:
	panic("Unable to create root VM's virtual GIC");
}

error_t
vgic_handle_object_create_hwirq(hwirq_create_t hwirq_create)
{
	hwirq_t *hwirq = hwirq_create.hwirq;
	assert(hwirq != NULL);

	error_t err = OK;

	if (hwirq_create.action == HWIRQ_ACTION_VGIC_FORWARD_SPI) {
		// The physical IRQ must be an SPI.
		if (gicv3_get_irq_type(hwirq_create.irq) !=
		    GICV3_IRQ_TYPE_SPI) {
			err = ERROR_ARGUMENT_INVALID;
		}
	} else if (hwirq_create.action ==
		   HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE) {
		// The physical IRQ must be an PPI.
		if (gicv3_get_irq_type(hwirq_create.irq) !=
		    GICV3_IRQ_TYPE_PPI) {
			err = ERROR_ARGUMENT_INVALID;
		}
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
	gicv3_spi_set_route(hwirq->irq, physical_router);

#if GICV3_HAS_GICD_ICLAR
	if (GICD_IROUTER_get_IRM(&physical_router)) {
		// Set the HW IRQ's 1-of-N routing classes.
		gicv3_spi_set_classes(
			hwirq->irq,
			!vgic_delivery_state_get_nclass0(&current_dstate),
			vgic_delivery_state_get_class1(&current_dstate));
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
			vgic_delivery_state_atomic_intersection(
				dstate, cfg_is_edge, memory_order_relaxed);
		} else {
			vgic_delivery_state_atomic_difference(
				dstate, cfg_is_edge, memory_order_relaxed);
		}
	}

	// Enable the HW IRQ if the virtual enable bit is set (unbound HW IRQs
	// are always disabled).
	if (vgic_delivery_state_get_enabled(&current_dstate)) {
		irq_enable(hwirq);
	}

	hwirq->vgic_enable_hw = true;
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
	irq_disable_sync(hwirq);

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
			irq_enable(hwirq);
		}
	} else {
		irq_disable_nosync(hwirq);
	}

	return true;
}

irq_trigger_result_t
vgic_handle_virq_set_mode_hwirq_spi(virq_source_t *source, irq_trigger_t mode)
{
	hwirq_t *hwirq = hwirq_from_virq_source(source);

	assert(!source->is_private);
	assert(!platform_irq_is_percpu(hwirq->irq));

	return gicv3_irq_set_trigger(hwirq->irq, mode);
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

	if (!GICD_CTLR_DS_is_equal(new_ctlr, old_ctlr)) {
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

void
vgic_gicd_set_irq_router(vic_t *vic, irq_t irq_num, uint8_t aff0, uint8_t aff1,
			 uint8_t aff2, uint8_t aff3, bool is_1n)
{
	assert(vgic_irq_is_spi(irq_num));
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, irq_num);
	assert(dstate != NULL);

	// Find the new target index
	psci_mpidr_t route_id = psci_mpidr_default();
	psci_mpidr_set_Aff0(&route_id, aff0);
	psci_mpidr_set_Aff1(&route_id, aff1);
	psci_mpidr_set_Aff2(&route_id, aff2);
	psci_mpidr_set_Aff3(&route_id, aff3);
	cpu_index_result_t cpu_r = platform_cpu_mpidr_to_index(route_id);
	index_t		   route_index;
	if ((cpu_r.e == OK) && (cpu_r.r < vic->gicr_count)) {
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
		}

		// Set the chosen physical route
		VGIC_TRACE(ROUTE, vic, NULL, "route {:d}: virt {:d} phys {:#x}",
			   irq_num, route_index,
			   GICD_IROUTER_raw(physical_router));
		irq_t irq = hwirq_from_virq_source(source)->irq;
		gicv3_spi_set_route(irq, physical_router);

#if GICV3_HAS_GICD_ICLAR
		if (GICD_IROUTER_get_IRM(&physical_router)) {
			// Set the HW IRQ's 1-of-N routing classes.
			gicv3_spi_set_classes(
				irq,
				!vgic_delivery_state_get_nclass0(&new_dstate),
				vgic_delivery_state_get_class1(&new_dstate));
		}
#endif
	} else {
		VGIC_TRACE(ROUTE, vic, NULL, "route {:d}: virt {:d} phys N/A",
			   irq_num, route_index);
	}

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

void
vgic_gicr_rd_set_control(vic_t *vic, thread_t *gicr_vcpu, GICR_CTLR_t ctlr)
{
	(void)vic;
	(void)gicr_vcpu;
	(void)ctlr;
}

GICR_CTLR_t
vgic_gicr_rd_get_control(vic_t *vic, thread_t *gicr_vcpu)
{
	(void)vic;

	(void)gicr_vcpu;
	GICR_CTLR_t ctlr = GICR_CTLR_default();

	return ctlr;
}

void
vgic_gicr_rd_set_statusr(thread_t *gicr_vcpu, GICR_STATUSR_t statusr, bool set)
{
	if (set) {
		GICR_STATUSR_atomic_union(&gicr_vcpu->vgic_gicr_rd_statusr,
					  statusr, memory_order_relaxed);
	} else {
		GICR_STATUSR_atomic_difference(&gicr_vcpu->vgic_gicr_rd_statusr,
					       statusr, memory_order_relaxed);
	}
}

void
vgic_gicr_sgi_change_sgi_ppi_pending(vic_t *vic, thread_t *gicr_vcpu,
				     irq_t irq_num, bool set)
{
	assert(vgic_irq_is_private(irq_num));

	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, gicr_vcpu, irq_num);
	vgic_change_irq_pending(vic, gicr_vcpu, irq_num, true, source, set,
				false);
	rcu_read_finish();
}

void
vgic_gicr_sgi_change_sgi_ppi_enable(vic_t *vic, thread_t *gicr_vcpu,
				    irq_t irq_num, bool set)
{
	assert(vgic_irq_is_private(irq_num));

	preempt_disable();

	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, gicr_vcpu, irq_num);

	assert((source == NULL) ||
	       (source->trigger != VIRQ_TRIGGER_VGIC_FORWARDED_SPI));

	vgic_change_irq_enable(vic, gicr_vcpu, irq_num, true, source, set);

	rcu_read_finish();

	preempt_enable();
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

	_Atomic vgic_delivery_state_t *dstate =
		&gicr_vcpu->vgic_private_states[irq_num];

	vgic_sync_group_change(vic, irq_num, dstate, is_group_1);
}

void
vgic_gicr_sgi_set_sgi_ppi_priority(vic_t *vic, thread_t *gicr_vcpu,
				   irq_t irq_num, uint8_t priority)
{
	assert(vgic_irq_is_private(irq_num));

	spinlock_acquire(&vic->gicd_lock);

	vgic_set_irq_priority(vic, gicr_vcpu, irq_num, priority);

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
	virq_source_t	      *old_source = atomic_load_acquire(attach_ptr);
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
			vgic_delivery_state_atomic_intersection(
				dstate, cfg_is_edge, memory_order_relaxed);
		} else {
			vgic_delivery_state_atomic_difference(
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
	virq_source_t	      *registered_source = source;
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
		irq_disable_nosync(hwirq);
		deactivate = true;
	}

	return deactivate;
}
