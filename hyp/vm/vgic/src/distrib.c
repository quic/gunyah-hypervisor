// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
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
#include "vic_base.h"

// Qualcomm's JEP106 identifier is 0x70, with no continuation bytes. This is
// used in the virtual GICD_IIDR and GICR_IIDR.
#define JEP106_IDENTITY	 0x70U
#define JEP106_CONTCODE	 0x0U
#define IIDR_IMPLEMENTER ((JEP106_CONTCODE << 8U) | JEP106_IDENTITY)
#define IIDR_PRODUCTID	 (uint8_t)'G' /* For "Gunyah" */
#define IIDR_VARIANT	 0U
#define IIDR_REVISION	 0U

// We currently emulate a GICv3 with no legacy support (even if the real GIC
// is a GICv4).
#define PIDR2_VERSION 3U

error_t
vgic_handle_object_create_vic(vic_create_t vic_create)
{
	struct vic *vic = vic_create.vic;
	assert(vic != NULL);
	struct partition *partition = vic->header.partition;
	assert(partition != NULL);
	error_t		  err;
	void_ptr_result_t alloc_r;

	vic->gicr_count	   = 1U;
	vic->sources_count = 0U;

	spinlock_init(&vic->gicd_lock);

	alloc_r = partition_alloc(
		partition, util_max(sizeof(gicd_t), PGTABLE_VM_PAGE_SIZE),
		util_max(alignof(gicd_t), PGTABLE_VM_PAGE_SIZE));
	if (alloc_r.e != OK) {
		err = alloc_r.e;
		goto out;
	}
	memset(alloc_r.r, 0,
	       util_balign_up(sizeof(gicd_t), PGTABLE_VM_PAGE_SIZE));
	gicd_t *gicd = (gicd_t *)alloc_r.r;
	vic->gicd    = gicd;

	// Make sure all the initial routes are invalid, so that the GIC driver
	// startup in the VM will set the routes correctly (if we used a
	// potentially valid value here, like 0, then the driver's writes might
	// be reduced to no-ops).
	GICD_IROUTER_t invalid_router = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&invalid_router, false);
	GICD_IROUTER_set_Aff0(&invalid_router, 0xff);
	GICD_IROUTER_set_Aff1(&invalid_router, 0xff);
	GICD_IROUTER_set_Aff2(&invalid_router, 0xff);
	GICD_IROUTER_set_Aff3(&invalid_router, 0xff);
	for (count_t i = 0U; i < util_array_size(gicd->irouter); i++) {
		atomic_store_relaxed(&gicd->irouter[i], invalid_router);
	}

	ICH_VTR_EL2_t vtr = register_ICH_VTR_EL2_read();

	GICD_TYPER_t typer = GICD_TYPER_default();
	GICD_TYPER_set_ITLinesNumber(&typer,
				     util_balign_up(GIC_SPI_NUM, 32U) / 32U);
	GICD_TYPER_set_MBIS(&typer, true);
#if VGIC_HAS_EXT_IRQS
#error Extended IRQs not yet implemented
#else
	GICD_TYPER_set_ESPI(&typer, false);
#endif
	count_t idbits;
	if (ICH_VTR_EL2_get_IDbits(&vtr) == 0) {
		idbits = 16U;
	} else if (ICH_VTR_EL2_get_IDbits(&vtr) == 1) {
		idbits = 24U;
	} else {
		panic("ICH_VTR_EL2: Unknown IDbits value");
	}

#if !GICV3_HAS_VLPI
	idbits = util_min(VGIC_IDBITS, idbits);
#endif

	GICD_TYPER_set_IDbits(&typer, idbits - 1U);
	GICD_TYPER_set_A3V(&typer, true);
	GICD_TYPER_set_No1N(&typer, VGIC_HAS_1N == 0U);
	atomic_store_relaxed(&gicd->typer, typer);

	GICD_IIDR_t iidr = GICD_IIDR_default();
	GICD_IIDR_set_Implementer(&iidr, IIDR_IMPLEMENTER);
	GICD_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
	GICD_IIDR_set_Variant(&iidr, IIDR_VARIANT);
	GICD_IIDR_set_Revision(&iidr, IIDR_REVISION);
	atomic_store_relaxed(&gicd->iidr, iidr);

	// Use the DS (disable security) version of GICD_CTLR, because we don't
	// implement security states in the virtual GIC. Note that the DS bit is
	// constant true in this bitfield type.
	GICD_CTLR_DS_t ctlr = GICD_CTLR_DS_default();
	// The virtual GIC has no legacy mode support.
	GICD_CTLR_DS_set_ARE(&ctlr, true);
	// temporarily fix the enable bits to 1
	GICD_CTLR_DS_set_EnableGrp0(&ctlr, true);
	GICD_CTLR_DS_set_EnableGrp1(&ctlr, true);
	atomic_store_relaxed(&gicd->ctlr, (GICD_CTLR_t){ .ds = ctlr });

	err = OK;

out:
	return err;
}

error_t
vic_configure(vic_t *vic, count_t max_vcpus, count_t max_virqs)
{
	error_t err;

	if ((max_vcpus == 0U) || (max_vcpus > PLATFORM_MAX_CORES)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (max_virqs > GIC_SPI_NUM) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	err		   = OK;
	vic->gicr_count	   = max_vcpus;
	vic->sources_count = max_virqs;
out:
	return err;
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
		vic->sources = (virq_source_t * _Atomic *)alloc_r.r;
	}

	alloc_r = partition_alloc(partition, vcpus_size,
				  alignof(vic->gicr_vcpus[0U]));
	if (alloc_r.e != OK) {
		err = alloc_r.e;
		goto out;
	}
	memset(alloc_r.r, 0, vcpus_size);
	vic->gicr_vcpus = (thread_t * _Atomic *)alloc_r.r;

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
	}

	if (vic->sources != NULL) {
		size_t sources_size =
			sizeof(vic->sources[0]) * vic->sources_count;
		partition_free(partition, vic->sources, sources_size);
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
	error_t	  err  = OK;
	thread_t *vcpu = thread_create.thread;
	assert(vcpu != NULL);

	atomic_store_relaxed(&vcpu->vgic_lr_owner, CPU_INDEX_INVALID);

	if (vcpu->kind == THREAD_KIND_VCPU) {
		void_ptr_result_t alloc_r;
		partition_t *	  partition = vcpu->header.partition;

		alloc_r = partition_alloc(partition,
					  util_max(sizeof(gicr_rd_base_t),
						   PGTABLE_VM_PAGE_SIZE),
					  util_max(alignof(gicr_rd_base_t),
						   PGTABLE_VM_PAGE_SIZE));
		if (alloc_r.e != OK) {
			err = alloc_r.e;
			goto out;
		}
		memset(alloc_r.r, 0, sizeof(*vcpu->vgic_gicr_rd));
		vcpu->vgic_gicr_rd = alloc_r.r;

		alloc_r = partition_alloc(partition,
					  util_max(sizeof(gicr_sgi_base_t),
						   PGTABLE_VM_PAGE_SIZE),
					  util_max(alignof(gicr_sgi_base_t),
						   PGTABLE_VM_PAGE_SIZE));
		if (alloc_r.e != OK) {
			err = alloc_r.e;
			goto out;
		}
		memset(alloc_r.r, 0, sizeof(*vcpu->vgic_gicr_sgi));
		vcpu->vgic_gicr_sgi = alloc_r.r;

		atomic_store_relaxed(&vcpu->vgic_gicr_rd->ctlr,
				     GICR_CTLR_default());

		GICR_IIDR_t iidr = GICR_IIDR_default();
		GICR_IIDR_set_Implementer(&iidr, IIDR_IMPLEMENTER);
		GICR_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
		GICR_IIDR_set_Variant(&iidr, IIDR_VARIANT);
		GICR_IIDR_set_Revision(&iidr, IIDR_REVISION);
		atomic_store_relaxed(&vcpu->vgic_gicr_rd->iidr, iidr);

		// The virtual GIC is in DS mode and therefore must expose PM
		// support directly to the guest via GICR_WAKER. This register
		// resets to sleep state.
		vcpu->vgic_sleep   = true;
		GICR_WAKER_t waker = GICR_WAKER_default();
		GICR_WAKER_set_ProcessorSleep(&waker, true);
		GICR_WAKER_set_ChildrenAsleep(&waker, true);
		atomic_store_relaxed(&vcpu->vgic_gicr_rd->waker, waker);

		vcpu->vgic_ich_hcr = ICH_HCR_EL2_default();
#if VGIC_HAS_1N
		// Trap the group enable bits so we can check for unrouted
		// 1-of-N VIRQS. Note that these and the corresponding DIE bits
		// are toggled every time we handle a maintenance interrupt.
		ICH_HCR_EL2_set_Vgrp0EIE(&vcpu->vgic_ich_hcr, true);
		ICH_HCR_EL2_set_Vgrp1EIE(&vcpu->vgic_ich_hcr, true);
#endif
		// Always set LRENPIE, and keep UIE off. This is because we
		// don't reload active interrupts into the LRs once they've been
		// kicked out; the complexity of doing that outweighs any
		// performance benefit, especially when most VMs are Linux -
		// which uses neither EOImode (in EL1) nor preemption, and
		// therefore will never have multiple active IRQs to trigger
		// this in the first place.
		ICH_HCR_EL2_set_LRENPIE(&vcpu->vgic_ich_hcr, true);
		// Always trap DIR, so we know which IRQs are being deactivated
		// when the VM uses EOImode=1. We can't rely on LRENPIE/EOIcount
		// in this case (as opposed to EOImode=0, when we can assume the
		// highest priority active interrupts are being deactivated).
		ICH_HCR_EL2_set_TDIR(&vcpu->vgic_ich_hcr, true);
		// Always enable the interface.
		ICH_HCR_EL2_set_En(&vcpu->vgic_ich_hcr, true);

		vcpu->vgic_ich_vmcr = ICH_VMCR_EL2_default();

		// The SGIs are always edge-triggered, so set the edge trigger
		// bit in their dstates.
		vgic_delivery_state_t sgi_dstate =
			vgic_delivery_state_default();
		vgic_delivery_state_set_cfg_is_edge(&sgi_dstate, true);
		for (index_t i = 0; i < GIC_SGI_NUM; i++) {
			atomic_init(&vcpu->vgic_private_states[i], sgi_dstate);
		}
	}

out:
	return err;
}

error_t
vgic_handle_object_activate_thread(thread_t *vcpu)
{
	error_t err = OK;
	vic_t * vic = vcpu->vgic_vic;

	if (vic != NULL) {
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

		// Set the GICD's pointer to the VCPU. This is a store release
		// so we can be sure that all of the thread's initialisation is
		// complete before the VGIC tries to use it.
		atomic_store_release(&vic->gicr_vcpus[cpu_r.r], vcpu);

	out_locked:
		spinlock_release(&vic->gicd_lock);

		// Always apply the wake workaround for now
		// if ((err == OK) && vcpu->vgic_auto_wake) {
		if (err == OK) {
			// Workaround for UEFI: pretend that the VM wrote a 0 to
			// GICR_WAKER to wake up the GICR
			vgic_gicr_rd_set_wake(vic, vcpu, false);
		}
	}

	return err;
}

void
vgic_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread_get_self() != thread);
	assert(!cpulocal_index_valid(
		atomic_load_relaxed(&thread->vgic_lr_owner)));

	vic_t *vic = thread->vgic_vic;
	if (vic != NULL) {
		spinlock_acquire(&vic->gicd_lock);

		assert(thread->vgic_gicr_index < vic->gicr_count);
		if (atomic_load_relaxed(
			    &vic->gicr_vcpus[thread->vgic_gicr_index]) ==
		    thread) {
			atomic_store_relaxed(
				&vic->gicr_vcpus[thread->vgic_gicr_index],
				NULL);

			rcu_read_start();
			for (index_t i = 0; i < vic->sources_count; i++) {
				virq_source_t *source =
					atomic_load_consume(&vic->sources[i]);
				if (source == NULL) {
					continue;
				}
				if (atomic_load_relaxed(&source->vgic_vcpu) ==
				    thread) {
					atomic_store_relaxed(&source->vgic_vcpu,
							     NULL);
				}
			}
			rcu_read_finish();
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
		// Clear out the LRs, rerouting other VIRQs if necessary
		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			if (thread->vgic_lrs[i].dstate != NULL) {
				vgic_defer(vic, thread, i, true);
				assert(thread->vgic_lrs[i].dstate == NULL);
			}
		}

#if VGIC_HAS_1N
		// Wake any other threads on the GIC, in case the deferred IRQs
		// can be rerouted.
		vgic_sync_all(vic, true);
#endif

		object_put_vic(vic);
	}

	if (thread->vgic_gicr_rd != NULL) {
		partition_free(partition, thread->vgic_gicr_rd,
			       sizeof(*thread->vgic_gicr_rd));
	}
	if (thread->vgic_gicr_sgi != NULL) {
		partition_free(partition, thread->vgic_gicr_sgi,
			       sizeof(*thread->vgic_gicr_sgi));
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
#else
	count_t max_vcpus = 1;
	count_t max_virqs = 64;
#endif

	env_data->gicr_base = PLATFORM_GICR_BASE;
	env_data->gicd_base = PLATFORM_GICD_BASE;

	if (vic_configure(vic_r.r, max_vcpus, max_virqs) != OK) {
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

	// UEFI workaround: automatically wake up the root VM's GICRs
	root_thread->vgic_auto_wake = true;

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

		// UEFI workaround: automatically wake up the root VM's GICRs
		thread->vgic_auto_wake = true;

		object_put_thread(thread);
	}
#endif

	// Create a HWIRQ object for every SPI
	index_t i;
	count_t hwirq_count = util_min((count_t)platform_irq_max() + 1U,
				       util_array_size(env_data->vic_hwirq));
	for (i = 0; i < hwirq_count; i++) {
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
		if (hwirq_r.e == OK) {
			if (object_activate_hwirq(hwirq_r.r) != OK) {
				panic("Failed to activate hwirq.");
			}

			// Create a master cap for the HWIRQ
			object_ptr_t hwirq_optr = { .hwirq = hwirq_r.r };
			cid_r			= cspace_create_master_cap(
				  root_cspace, hwirq_optr, OBJECT_TYPE_HWIRQ);
			if (cid_r.e != OK) {
				panic("Unable to create cap to HWIRQ");
			}
			env_data->vic_hwirq[i] = cid_r.r;

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
			if (gicv3_get_irq_type(i) == GICV3_IRQ_TYPE_SPI) {
				// Bind the HW IRQ to the HLOS VIC
				error_t err = vgic_bind_hwirq_spi(
					vic_r.r, hwirq_r.r, hwirq_params.irq);
				if (err != OK) {
					panic("Unable to bind HW SPI to HLOS VGIC");
				}
			} else if (gicv3_get_irq_type(i) ==
				   GICV3_IRQ_TYPE_PPI) {
				// Bind the HW IRQ to the HLOS VIC
				error_t err = vgic_bind_hwirq_forward_private(
					vic_r.r, hwirq_r.r, hwirq_params.irq);
				if (err != OK) {
					panic("Unable to bind HW PPI to HLOS VGIC");
				}
			}
#endif
		} else if ((hwirq_r.e == ERROR_DENIED) ||
			   (hwirq_r.e == ERROR_ARGUMENT_INVALID) ||
			   (hwirq_r.e == ERROR_BUSY)) {
			env_data->vic_hwirq[i] = CSPACE_CAP_INVALID;
		} else {
			panic("Unable to create HW IRQ object");
		}
	}
	for (; i < util_array_size(env_data->vic_hwirq); i++) {
		env_data->vic_hwirq[i] = CSPACE_CAP_INVALID;
	}

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
vgic_unwind_object_create_hwirq(hwirq_create_t hwirq_create)
{
	hwirq_t *hwirq = hwirq_create.hwirq;

	// The interrupt should not have been enabled yet and therefore
	// should still be inactive.
	assert(atomic_load_relaxed(&hwirq->vgic_state) ==
	       VGIC_HWIRQ_STATE_INACTIVE);
}

void
vgic_handle_object_deactivate_hwirq(hwirq_t *hwirq)
{
	if (hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI) {
		vic_unbind(&hwirq->vgic_spi_source);
	}
}

static void
vgic_hwirq_deactivate(hwirq_t *hwirq)
{
	assert(hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI);

	vgic_hwirq_state_t hstate = atomic_load_acquire(&hwirq->vgic_state);

	// The VIRQ should have been delisted by now, because an RCU
	// grace period has elapsed since the vic_unbind() in
	// deactivate above.
	assert(hstate != VGIC_HWIRQ_STATE_LISTED);

	// If it's still active, deactivate it. Nothing else should have
	// a reference to this object by now so we don't need a
	// compare-exchange.
	if (hstate == VGIC_HWIRQ_STATE_ACTIVE) {
		irq_deactivate(hwirq);
	}
}

void
vgic_handle_object_cleanup_hwirq(hwirq_t *hwirq)
{
	if (hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI) {
		vgic_hwirq_deactivate(hwirq);
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

	// Default to an invalid physical route
	GICD_IROUTER_t physical_router = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&physical_router, false);
	GICD_IROUTER_set_Aff0(&physical_router, 0xff);
	GICD_IROUTER_set_Aff1(&physical_router, 0xff);
	GICD_IROUTER_set_Aff2(&physical_router, 0xff);
	GICD_IROUTER_set_Aff3(&physical_router, 0xff);

	// Try to set the physical route based on the virtual route
	GICD_IROUTER_t virtual_router = vgic_get_router(vic, virq);
	if (GICD_IROUTER_get_IRM(&virtual_router)) {
		physical_router = virtual_router;
	} else {
		rcu_read_start();
		thread_t *new_target = vgic_get_route(vic, virq);
		if (new_target != NULL) {
			scheduler_lock(new_target);
			cpu_index_t affinity =
				scheduler_get_affinity(new_target);
			if (cpulocal_index_valid(affinity)) {
				// Fixed affinity; use it as the physical route
				psci_mpidr_t mpidr =
					platform_cpu_index_to_mpidr(affinity);
				GICD_IROUTER_set_Aff0(
					&physical_router,
					psci_mpidr_get_Aff0(&mpidr));
				GICD_IROUTER_set_Aff1(
					&physical_router,
					psci_mpidr_get_Aff1(&mpidr));
				GICD_IROUTER_set_Aff2(
					&physical_router,
					psci_mpidr_get_Aff2(&mpidr));
				GICD_IROUTER_set_Aff3(
					&physical_router,
					psci_mpidr_get_Aff3(&mpidr));
			} else {
				// No fixed affinity; let the GIC choose
				GICD_IROUTER_set_IRM(&physical_router, true);
			}
			scheduler_unlock(new_target);
		}
		rcu_read_finish();
	}

	// Set the chosen physical route
	VGIC_TRACE(VIRQ_CHANGED, vic, NULL,
		   "bind {:d}: route virt {:#x} phys {:#x}", virq,
		   GICD_IROUTER_raw(virtual_router),
		   GICD_IROUTER_raw(physical_router));
	gicv3_spi_set_route(hwirq->irq, physical_router);

	// Attempt to set the HW IRQ's trigger mode based on the virtual ICFGR;
	// if this fails because the HW trigger mode is fixed, then update the
	// virtual ICFGR insted.
	index_t	      i	      = virq / 16U;
	bool	      is_edge = (atomic_load_relaxed(&vic->gicd->icfgr[i]) &
			 util_bit(((virq % 16U) * 2U) + 1U)) != 0U;
	irq_trigger_t mode    = is_edge ? IRQ_TRIGGER_EDGE_RISING
				     : IRQ_TRIGGER_LEVEL_HIGH;
	irq_trigger_result_t new_mode = trigger_virq_set_mode_event(
		VIRQ_TRIGGER_VGIC_FORWARDED_SPI, &hwirq->vgic_spi_source, mode);
	if ((new_mode.e != OK) || (new_mode.r != mode)) {
		// Mode change failed; the hardware config must be fixed to the
		// other mode. Flip the software mode.
		atomic_fetch_xor_explicit(&vic->gicd->icfgr[i],
					  util_bit(((virq % 16U) * 2U) + 1U),
					  memory_order_relaxed);
	}

	// Enable the HW IRQ if the virtual enable bit is set (unbound HW IRQs
	// are always disabled).
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);
	if (vgic_delivery_state_get_enabled(&current_dstate)) {
		irq_enable(hwirq);
	}

	spinlock_release(&vic->gicd_lock);

out:
	return err;
}

error_t
vgic_unbind_hwirq_spi(hwirq_t *hwirq)
{
	assert(hwirq->action == HWIRQ_ACTION_VGIC_FORWARD_SPI);

	// Disable the IRQ, but don't wait for running handlers.
	irq_disable_nosync(hwirq);

	// Remove the VIRQ binding.
	vic_unbind_sync(&hwirq->vgic_spi_source);

	return OK;
}

bool
vgic_handle_virq_set_enabled_hwirq_spi(virq_source_t *source, bool enabled)
{
	hwirq_t *hwirq = hwirq_from_virq_source(source);
	assert(!source->is_private);
	assert(!platform_irq_is_percpu(hwirq->irq));

	if (enabled) {
		irq_enable(hwirq);
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

GICD_IROUTER_t
vgic_get_router(vic_t *vic, virq_t virq)
{
	assert(vgic_irq_is_spi(virq));
	GICD_IROUTER_t result;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SPI:
		result = atomic_load_relaxed(
			&vic->gicd->irouter[virq - GIC_SPI_BASE]);
		break;
	case VGIC_IRQ_TYPE_PPI:
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		result = GICD_IROUTER_default();
		break;
	}
	return result;
}

// Look up the VCPU that the given VIRQ number should be routed to.
//
// The given IRQ number must be an SPI.
//
// Must be called either from an RCU critical section or with the GICD lock
// held, to ensure that the returned thread pointer remains valid. Note that
// the returned value may be NULL.
thread_t *
vgic_get_route(vic_t *vic, virq_t virq)
{
	assert(vgic_irq_is_spi(virq));
	GICD_IROUTER_t route = vgic_get_router(vic, virq);

#if VGIC_HAS_1N
#error not yet implemented: search for a usable VCPU
#else
	assert(!GICD_IROUTER_get_IRM(&route));
#endif
	psci_mpidr_t route_id = psci_mpidr_default();
	psci_mpidr_set_Aff0(&route_id, GICD_IROUTER_get_Aff0(&route));
	psci_mpidr_set_Aff1(&route_id, GICD_IROUTER_get_Aff1(&route));
	psci_mpidr_set_Aff2(&route_id, GICD_IROUTER_get_Aff2(&route));
	psci_mpidr_set_Aff3(&route_id, GICD_IROUTER_get_Aff3(&route));

	thread_t *ret = NULL;

	cpu_index_result_t cpu_r = platform_cpu_mpidr_to_index(route_id);
	if ((cpu_r.e == OK) && (cpu_r.r < vic->gicr_count)) {
		ret = atomic_load_consume(&vic->gicr_vcpus[cpu_r.r]);
	}

	return ret;
}

// Return the route for a shared VIRQ, or the owner for a private VIRQ.
thread_t *
vgic_get_route_or_owner(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	return vgic_irq_is_spi(virq) ? vgic_get_route(vic, virq) : vcpu;
}

static void
vgic_change_irq_pending(vic_t *vic, thread_t *target, irq_t irq_num,
			bool is_private, virq_source_t *source, bool set,
			bool is_msi)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, target, irq_num);
	assert(dstate != NULL);
	assert_preempt_disabled();

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
				   change_dstate, false, is_private);
	} else {
		// Edge-triggered IRQs need to be cleared in hardware as well,
		// in case they have a pending state the hypervisor hasn't seen
		// yet. This has no effect on level-triggered IRQs.
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
		(void)vgic_undeliver(vic, target, dstate, irq_num, false,
				     change_dstate, false);
	}
}

static void
vgic_change_irq_enable(vic_t *vic, thread_t *target, irq_t irq_num,
		       bool is_private, virq_source_t *source, bool set)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, target, irq_num);
	assert(dstate != NULL);
	assert_preempt_disabled();

	if ((source != NULL) && !set) {
		(void)trigger_virq_set_enabled_event(source->trigger, source,
						     set);
	}

	vgic_delivery_state_t change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_enabled(&change_dstate, true);

	if (set) {
		bool is_hw =
			(source != NULL) &&
			(source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

		(void)vgic_deliver(irq_num, vic, target, source, dstate,
				   change_dstate, is_hw, is_private);

	} else {
		// Undeliver and reclaim the VIRQ.
		if (!vgic_undeliver(vic, target, dstate, irq_num, false,
				    change_dstate, true)) {
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

	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// Interrupt is listed; ignore the write.
	} else if (!set) {
		vgic_deactivate(vic, vcpu, irq_num, dstate, old_dstate, false);
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
vgic_sync_group_change(vic_t *vic, _Atomic vgic_delivery_state_t *dstate,
		       bool is_group_1)
{
	assert(dstate != NULL);

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		if (vgic_delivery_state_get_group1(&old_dstate) == is_group_1) {
			// Nothing to do here
			goto out;
		}

		new_dstate = old_dstate;

		vgic_delivery_state_set_group1(&new_dstate, is_group_1);
		if (vgic_delivery_state_get_listed(&old_dstate)) {
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_relaxed));

	// If the VIRQ is listed, it may be on a VCPU that currently has its old
	// group disabled and the new group enabled, and is blocked in a WFI
	// with no other interrupts deliverable (even potentially). If we don't
	// update the list register, that VCPU might wait indefinitely.
	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// To guarantee that the group change will take effect in finite
		// time, sync all VCPUs that might have it listed.
		vgic_sync_all(vic, false);
	}

out:
	(void)0;
}

static void
vgic_set_irq_priority(vic_t *vic, thread_t *vcpu, irq_t irq_num,
		      bool is_private, uint8_t priority, uint8_t old_priority)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, irq_num);
	assert(dstate != NULL);

	assert(!is_private || (vcpu != NULL));

	// We don't bother with any synchronisation if the priority is being
	// lowered (made greater) or unchanged. If it is being raised (made
	// lesser), then there is a possibility that its target VCPU can't
	// receive it at the old priority due to other active IRQs or a manual
	// priority mask, and is blocked in WFI; in this case we must send a
	// sync if the VIRQ is listed or retry delivery at the new priority if
	// it is not listed.
	if (priority < old_priority) {
		vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
		vgic_delivery_state_t new_dstate;
		do {
			new_dstate = old_dstate;
			if (vgic_delivery_state_get_listed(&old_dstate)) {
				vgic_delivery_state_set_need_sync(&new_dstate,
								  true);
			}
			// We do the compare-exchange even if we are not
			// changing the value, because we need the release
			// barrier to ensure that nobody else lists the IRQ
			// after we load the dstate but before we update the
			// register.
		} while (!atomic_compare_exchange_strong_explicit(
			dstate, &old_dstate, new_dstate, memory_order_release,
			memory_order_relaxed));

		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// To guarantee that the priority change will take
			// effect in finite time, sync all VCPUs that might have
			// it listed.
			vgic_sync_all(vic, false);
		} else if (vgic_delivery_state_get_enabled(&old_dstate) &&
			   vgic_delivery_state_is_pending(&old_dstate)) {
			// Retry delivery, in case it previously did not select
			// a LR only because the priority was too low
			virq_source_t *source =
				vgic_find_source(vic, vcpu, irq_num);
			bool is_hw = (source != NULL) &&
				     (source->trigger ==
				      VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

			thread_t *target =
				is_private ? vcpu
					   : vgic_get_route(vic, irq_num);
			(void)vgic_deliver(irq_num, vic, target, source, dstate,
					   vgic_delivery_state_default(), is_hw,
					   is_private);
		} else {
			// Unlisted and not deliverable; nothing to do.
		}
	}
}

void
vgic_gicd_set_control(vic_t *vic, GICD_CTLR_DS_t ctlr)
{
	(void)vic;
	(void)ctlr;
}

void
vgic_gicd_set_statusr(vic_t *vic, GICD_STATUSR_t statusr, bool set)
{
	if (set) {
		GICD_STATUSR_atomic_union(&vic->gicd->statusr, statusr,
					  memory_order_relaxed);
	} else {
		GICD_STATUSR_atomic_difference(&vic->gicd->statusr, statusr,
					       memory_order_relaxed);
	}
}

void
vgic_gicd_change_irq_pending(vic_t *vic, irq_t irq_num, bool set, bool is_msi)
{
	if (vgic_irq_is_spi(irq_num)) {
		// Try to find a thread to deliver to. This might be NULL if the
		// route is invalid or the VCPU isn't attached.
		rcu_read_start();
		virq_source_t *source = vgic_find_source(vic, NULL, irq_num);

		thread_t *target = NULL;
		if (source != NULL) {
			target = atomic_load_consume(&source->vgic_vcpu);
		}
		if (target == NULL) {
			target = vgic_get_route(vic, irq_num);
		}

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

	// Try to find a thread to deliver to. This might be NULL if the
	// route is invalid or the VCPU isn't attached.
	thread_t *target = NULL;
	if (source != NULL) {
		target = atomic_load_consume(&source->vgic_vcpu);
	}
	if (target == NULL) {
		target = vgic_get_route(vic, irq_num);
	}

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

		vgic_sync_group_change(vic, dstate, is_group_1);
	}
}

void
vgic_gicd_set_irq_priority(vic_t *vic, irq_t irq_num, uint8_t priority)
{
	if (vgic_irq_is_spi(irq_num)) {
		uint8_t old_priority = atomic_exchange_explicit(
			&vic->gicd->ipriorityr[irq_num], priority,
			memory_order_relaxed);

		vgic_set_irq_priority(vic, NULL, irq_num, false, priority,
				      old_priority);
	}
}

void
vgic_gicd_set_irq_config(vic_t *vic, irq_t irq_num, bool is_edge)
{
	assert(vgic_irq_is_spi(irq_num));
	assert(vic != NULL);
	index_t i = irq_num / 16U;
	assert((i >= (GIC_SPI_BASE / 16U)) &&
	       (i < util_array_size(vic->gicd->icfgr)));

	// Take the GICD lock to ensure that concurrent writes don't make the
	// HW, dstate and GICD register views of the config inconsistent
	spinlock_acquire(&vic->gicd_lock);

	bool effective_is_edge = is_edge;

	// If there's a source, update its config. Note that this may fail.
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, NULL, irq_num);
	if (source != NULL) {
		irq_trigger_t mode = is_edge ? IRQ_TRIGGER_EDGE_RISING
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

	// Update the config register.
	if (effective_is_edge) {
		(void)atomic_fetch_or_explicit(
			&vic->gicd->icfgr[i],
			util_bit(((irq_num % 16U) * 2U) + 1U),
			memory_order_relaxed);
	} else {
		(void)atomic_fetch_and_explicit(
			&vic->gicd->icfgr[i],
			~util_bit(((irq_num % 16U) * 2U) + 1U),
			memory_order_relaxed);
	}

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

	GICD_IROUTER_t virtual_router = GICD_IROUTER_default();
#if VGIC_HAS_1N
	GICD_IROUTER_set_IRM(&virtual_router, is_1n);
	if (is_1n) {
		// If the M bit is set, the Aff* fields are unknown and we are
		// allowed to make them RO. Give them all fixed nonsense values
		// so changing them while M is set doesn't cause unnecessary
		// re-routing.
		GICD_IROUTER_set_Aff0(&virtual_router, 0xff);
		GICD_IROUTER_set_Aff1(&virtual_router, 0xff);
		GICD_IROUTER_set_Aff2(&virtual_router, 0xff);
		GICD_IROUTER_set_Aff3(&virtual_router, 0xff);
	} else
#else
	// M bit is RAZ/WI; always use the affinity fields
	(void)is_1n;
#endif
	{
		GICD_IROUTER_set_Aff0(&virtual_router, aff0);
		GICD_IROUTER_set_Aff1(&virtual_router, aff1);
		GICD_IROUTER_set_Aff2(&virtual_router, aff2);
		GICD_IROUTER_set_Aff3(&virtual_router, aff3);
	}

	// Take the GICD lock to ensure that concurrent writes don't make the
	// HW, VIRQ source and GICD register views of the route inconsistent
	spinlock_acquire(&vic->gicd_lock);

	GICD_IROUTER_t old_router = atomic_load_relaxed(
		&vic->gicd->irouter[irq_num - GIC_SPI_BASE]);

	if (!GICD_IROUTER_is_equal(old_router, virtual_router)) {
		rcu_read_start();
		// Determine the VIRQ source, if any; also locate the VCPU that
		// is most likely to have this VIRQ listed.
		virq_source_t *source = vgic_find_source(vic, NULL, irq_num);
		thread_t *     old_target = NULL;
		thread_t *     new_target = NULL;
		if (source != NULL) {
			old_target = atomic_load_relaxed(&source->vgic_vcpu);
		}
		if (old_target == NULL) {
			old_target = vgic_get_route(vic, irq_num);
		}

		// Update the route register
		atomic_store_relaxed(
			&vic->gicd->irouter[irq_num - GIC_SPI_BASE],
			virtual_router);

		// Find the new target (if any) and cache it (if possible)
		new_target = vgic_get_route(vic, irq_num);
		if (source != NULL) {
			atomic_store_release(&source->vgic_vcpu, new_target);
		}

		bool is_hw =
			(source != NULL) &&
			(source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);
		if (is_hw) {
			// Default to an invalid physical route
			GICD_IROUTER_t physical_router = GICD_IROUTER_default();
			GICD_IROUTER_set_IRM(&physical_router, false);
			GICD_IROUTER_set_Aff0(&physical_router, 0xff);
			GICD_IROUTER_set_Aff1(&physical_router, 0xff);
			GICD_IROUTER_set_Aff2(&physical_router, 0xff);
			GICD_IROUTER_set_Aff3(&physical_router, 0xff);

			// Try to set the physical route based on the virtual
			// route
			if (GICD_IROUTER_get_IRM(&virtual_router)) {
				physical_router = virtual_router;
			} else if (new_target != NULL) {
				scheduler_lock(new_target);
				cpu_index_t affinity =
					scheduler_get_affinity(new_target);
				if (cpulocal_index_valid(affinity)) {
					// Fixed affinity; use it as the
					// physical route
					psci_mpidr_t mpidr =
						platform_cpu_index_to_mpidr(
							affinity);
					GICD_IROUTER_set_Aff0(
						&physical_router,
						psci_mpidr_get_Aff0(&mpidr));
					GICD_IROUTER_set_Aff1(
						&physical_router,
						psci_mpidr_get_Aff1(&mpidr));
					GICD_IROUTER_set_Aff2(
						&physical_router,
						psci_mpidr_get_Aff2(&mpidr));
					GICD_IROUTER_set_Aff3(
						&physical_router,
						psci_mpidr_get_Aff3(&mpidr));
				} else {
					// No fixed affinity; let the GIC choose
					GICD_IROUTER_set_IRM(&physical_router,
							     true);
				}
				scheduler_unlock(new_target);
			} else {
				// Requested affinity is not valid
			}

			// Set the chosen physical route
			VGIC_TRACE(VIRQ_CHANGED, vic, NULL,
				   "route {:d}: virt {:#x} phys {:#x}", irq_num,
				   GICD_IROUTER_raw(virtual_router),
				   GICD_IROUTER_raw(physical_router));
			gicv3_spi_set_route(hwirq_from_virq_source(source)->irq,
					    physical_router);
		} else {
			VGIC_TRACE(VIRQ_CHANGED, vic, NULL,
				   "route {:d}: virt {:#x} phys N/A", irq_num,
				   GICD_IROUTER_raw(virtual_router));
		}

		spinlock_release(&vic->gicd_lock);

		// Now undeliver the VIRQ from its old target if possible. If
		// the interrupt is currently pending, this will flag it for
		// delivery on its new route.
		if (new_target != old_target) {
			_Atomic vgic_delivery_state_t *dstate =
				vgic_find_dstate(vic, NULL, irq_num);
			(void)vgic_undeliver(vic, old_target, dstate, irq_num,
					     false,
					     vgic_delivery_state_default(),
					     true);
		}
		rcu_read_finish();
	} else {
		VGIC_TRACE(VIRQ_CHANGED, vic, NULL,
			   "route {:d}: virt {:#x} unchanged", irq_num,
			   GICD_IROUTER_raw(virtual_router));
		spinlock_release(&vic->gicd_lock);
	}
}

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

void
vgic_gicr_rd_set_statusr(thread_t *gicr_vcpu, GICR_STATUSR_t statusr, bool set)
{
	gicr_rd_base_t *gicr_rd = gicr_vcpu->vgic_gicr_rd;

	if (set) {
		GICR_STATUSR_atomic_union(&gicr_rd->statusr, statusr,
					  memory_order_relaxed);
	} else {
		GICR_STATUSR_atomic_difference(&gicr_rd->statusr, statusr,
					       memory_order_relaxed);
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

	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, gicr_vcpu, irq_num);

	assert((source == NULL) ||
	       (source->trigger != VIRQ_TRIGGER_VGIC_FORWARDED_SPI));

	preempt_disable();

	vgic_change_irq_enable(vic, gicr_vcpu, irq_num, true, source, set);

	preempt_enable();
	rcu_read_finish();
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

	vgic_sync_group_change(vic, dstate, is_group_1);
}

void
vgic_gicr_sgi_set_sgi_ppi_priority(vic_t *vic, thread_t *gicr_vcpu,
				   irq_t irq_num, uint8_t priority)
{
	assert(vgic_irq_is_private(irq_num));

	uint8_t old_priority = atomic_exchange_explicit(
		&gicr_vcpu->vgic_gicr_sgi->ipriorityr[irq_num], priority,
		memory_order_relaxed);

	vgic_set_irq_priority(vic, gicr_vcpu, irq_num, true, priority,
			      old_priority);
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

	// Update the register view.
	if (is_edge) {
		(void)atomic_fetch_or_explicit(
			&gicr_vcpu->vgic_gicr_sgi->icfgr1,
			util_bit(((irq_num - GIC_PPI_BASE) * 2U) + 1U),
			memory_order_relaxed);
	} else {
		(void)atomic_fetch_and_explicit(
			&gicr_vcpu->vgic_gicr_sgi->icfgr1,
			~util_bit(((irq_num - GIC_PPI_BASE) * 2U) + 1U),
			memory_order_relaxed);
	}

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

	source->virq	   = virq;
	source->trigger	   = trigger;
	source->is_private = false;

	rcu_read_start();
	virq_source_t *_Atomic *attach_ptr = &vic->sources[virq - GIC_SPI_BASE];
	virq_source_t *		old_source = atomic_load_acquire(attach_ptr);
	do {
		if (old_source != NULL) {
			ret = ERROR_BUSY;
			goto out_rcu_release;
		}
		vgic_delivery_state_t current_dstate =
			atomic_load_relaxed(dstate);
		if (vgic_delivery_state_get_hw_detached(&current_dstate)) {
			assert(vgic_delivery_state_get_listed(&current_dstate));
			ret = ERROR_BUSY;
			goto out_rcu_release;
		}
		ret = OK;
	} while (!atomic_compare_exchange_strong_explicit(
		attach_ptr, &old_source, source, memory_order_release,
		memory_order_acquire));

	if (ret == OK) {
		atomic_store_relaxed(&source->vgic_vcpu, NULL);
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

	source->virq	   = virq;
	source->trigger	   = trigger;
	source->is_private = true;

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
		atomic_store_relaxed(&source->vgic_vcpu, vcpu);
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
vic_bind_private_forward_private(vic_forward_private_t *forward_private,
				 vic_t *vic, thread_t *vcpu, virq_t virq,
				 irq_t pirq, cpu_index_t pcpu)
{
	error_t ret;

	assert(forward_private != NULL);
	assert(vic != NULL);
	assert(vcpu != NULL);

	if (vgic_get_irq_type(virq) != VGIC_IRQ_TYPE_PPI) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ret = vic_bind_private_vcpu(&forward_private->source, vcpu, virq,
				    VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE);
	if (ret != OK) {
		goto out;
	}

	// Take the GICD lock to ensure that the vGIC's IRQ config does
	// not change while we are copying it to the hardware GIC
	spinlock_acquire(&vic->gicd_lock);

	bool is_edge = (atomic_load_relaxed(&vcpu->vgic_gicr_sgi->icfgr1) &
			util_bit(((virq - GIC_PPI_BASE) * 2U) + 1U)) != 0U;
	irq_trigger_t mode = is_edge ? IRQ_TRIGGER_EDGE_RISING
				     : IRQ_TRIGGER_LEVEL_HIGH;

	irq_trigger_result_t new_mode = trigger_virq_set_mode_event(
		VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE, &forward_private->source,
		mode);
	if ((new_mode.e != OK) || (new_mode.r != mode)) {
		// Mode change failed; the hardware config must be fixed
		// to the other mode. Flip the software mode.
		atomic_fetch_xor_explicit(
			&vcpu->vgic_gicr_sgi->icfgr1,
			util_bit(((virq - GIC_PPI_BASE) * 2U) + 1U),
			memory_order_relaxed);
	}

	// Enable the HW IRQ if the virtual enable bit is set (unbound
	// HW IRQs are always disabled).
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);
	if (vgic_delivery_state_get_enabled(&current_dstate)) {
		platform_irq_enable_percpu(pirq, pcpu);
	}

	spinlock_release(&vic->gicd_lock);

out:
	return ret;
}

void
vic_unbind(virq_source_t *source)
{
	rcu_read_start();

	vic_t *vic = atomic_exchange_explicit(&source->vic, NULL,
					      memory_order_consume);
	if (vic == NULL) {
		// The VIRQ is not bound
		goto out;
	}

	// Get the current target VCPU
	thread_t *vcpu = atomic_load_consume(&source->vgic_vcpu);

	// It is possible that a shared VIRQ was last delivered to some other
	// VCPU, in which case an undeliver was already started when we changed
	// the route, and all we need to do is send sync IPIs and wait for the
	// VIRQ to sync. The VCPU pointer may be NULL in that case.
	if (source->is_private && (vcpu == NULL)) {
		// Somebody else has already released the VIRQ
		goto out;
	}

	// Clear the level_src bit in the delivery state.
	vgic_delivery_state_t clear_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_level_src(&clear_dstate, true);

	// Undeliver, and force detach if this was a HW source
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	if (!vgic_undeliver(vic, vcpu, dstate, source->virq,
			    source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI,
			    clear_dstate, false)) {
		// The VIRQ is still listed somewhere. For HW sources this can
		// delay both re-registration of the VIRQ and delivery of the
		// HW IRQ (after it is re-registered elsewhere), so start a
		// sync to ensure that delisting happens soon.
		vgic_sync_all(vic, false);
	}

	// Remove the source from the IRQ source array. Note that this must
	// be ordered after the level_src bit is cleared in the undeliver, to
	// ensure that other threads don't see this NULL pointer while the
	// level_src bit is still set.
	virq_source_t *		registered_source = source;
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

out:
	rcu_read_finish();
}

void
vic_unbind_sync(virq_source_t *source)
{
	vic_unbind(source);

	// Ensure that any remote operations affecting the source object and
	// the unbound VIRQ have completed before allowing a new binding.
	rcu_sync();

	// If it's a HW IRQ, ensure that it is delisted and deactivated.
	//
	// The unbind above should have already sent sync IPIs to do this, but
	// they may not have completed yet. In particular, note that RCU may
	// treat a VCPU running in EL1 as quiescent, in which case rcu_sync()
	// might return before the sync IPI has been received by its target.
	if (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI) {
		hwirq_t *hwirq = hwirq_from_virq_source(source);

		vgic_hwirq_state_t hstate =
			atomic_load_relaxed(&hwirq->vgic_state);

		assert_preempt_enabled();
		while (hstate == VGIC_HWIRQ_STATE_LISTED) {
			scheduler_yield();
			hstate = atomic_load_relaxed(&hwirq->vgic_state);
		}

		vgic_hwirq_deactivate(hwirq);
	}

	atomic_store_release(&source->vgic_is_bound, false);
}

// Try to find a GICR for the given VIRQ source, if it doesn't have one already.
//
// Acquires the gicd lock if there is currently no route. The specified source
// must be attached to the specified VIC with a VIRQ number in the SPI range.
//
// Returns the route.
static thread_t *
vgic_try_route_source(vic_t *vic, virq_source_t *source)
{
	assert(vic != NULL);
	assert(source != NULL);
	assert(atomic_load_relaxed(&source->vic) == vic);
	assert(!source->is_private);
	assert(vgic_irq_is_spi(source->virq));

	thread_t *ret = atomic_load_relaxed(&source->vgic_vcpu);

	if (ret == NULL) {
		spinlock_acquire(&vic->gicd_lock);
		if (vgic_find_source(vic, NULL, source->virq) != source) {
			// The source has been detached
			goto out_locked;
		}

		if (atomic_load_relaxed(&source->vgic_vcpu) != NULL) {
			// Someone has beaten us to it
			goto out_locked;
		}

		ret = vgic_get_route(vic, source->virq);
		if (ret != NULL) {
			atomic_store_release(&source->vgic_vcpu, ret);
		}

	out_locked:
		spinlock_release(&vic->gicd_lock);
	}

	return ret;
}

static bool_result_t
virq_do_assert(virq_source_t *source, bool edge_only, bool is_hw)
{
	bool_result_t ret;

	// The source's VIC and VCPU pointers are RCU-protected.
	rcu_read_start();

	// We must have a VIC to deliver to.
	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		ret = bool_result_error(ERROR_VIRQ_NOT_BOUND);
		goto out;
	}

	// We should already have a VCPU to deliver to if the VIRQ is
	// private. Otherwise the VCPU pointer in the source is a cache
	// of the effective route; if it is NULL, try to determine it
	// from the route register, but it might still be NULL if the
	// route register is invalid.
	thread_t *vcpu = atomic_load_consume(&source->vgic_vcpu);
	if (compiler_unexpected(vcpu == NULL)) {
		if (source->is_private) {
			ret = bool_result_error(ERROR_VIRQ_NOT_BOUND);
			goto out;
		} else {
			vcpu = vgic_try_route_source(vic, source);
		}
	}

	// Deliver the interrupt to the target
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	vgic_delivery_state_t assert_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_edge(&assert_dstate, true);
	if (!edge_only) {
		vgic_delivery_state_set_level_src(&assert_dstate, true);
	}

	vgic_delivery_state_t old_dstate =
		vgic_deliver(source->virq, vic, vcpu, source, dstate,
			     assert_dstate, is_hw, source->is_private);

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

	// Unconditionally mark the SPI as active. The recorded state may be out
	// of date because deactivation of listed hardware-sourced VIRQs is
	// forwarded back to the hardware by the ICH.
	atomic_store_relaxed(&hwirq->vgic_state, VGIC_HWIRQ_STATE_ACTIVE);
	VGIC_TRACE(HWSTATE_CHANGED, NULL, NULL, "received {:d}: hw ? -> {:d}",
		   hwirq->irq, VGIC_HWIRQ_STATE_ACTIVE);
	bool deactivate = false;

	// Assert the VIRQ.
	bool_result_t ret =
		virq_do_assert(&hwirq->vgic_spi_source, false, true);
	if (compiler_unexpected(ret.e != OK)) {
		// Delivery failed, so disable the HW IRQ and mark it
		// deactivated (the actual deactivate will occur on return).
		irq_disable_nosync(hwirq);
		vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;
		deactivate = atomic_compare_exchange_strong_explicit(
			&hwirq->vgic_state, &old_hstate,
			VGIC_HWIRQ_STATE_INACTIVE, memory_order_relaxed,
			memory_order_relaxed);
	}

	return deactivate;
}
