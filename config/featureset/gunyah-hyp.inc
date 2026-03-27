# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

module core/api
module core/base
module core/boot
module core/cpulocal
module core/cspace_twolevel
module core/globals
module core/idle
module core/ipi
module core/irq
module core/object_standard
module core/partition_standard
module core/power
module core/preempt
module core/rcu_bitmap
module core/scheduler_fprr
module core/spinlock_ticket
module core/task_queue
module core/thread_standard
module core/timer
module core/util
module core/vdevice
module core/wait_queue_broadcast
module debug/object_lists
module debug/symbol_version
module ipc/doorbell
module ipc/msgqueue
module mem/addrspace
module mem/allocator_boot
module mem/allocator_list
module mem/hyp_aspace
module mem/memdb_bitmap
module mem/memextent_sparse
module mem/pgtable
module misc/abort
module misc/elf
module misc/log_standard
module misc/prng_hw
module misc/prng_simple
module misc/qcbor
module misc/range_map_gpt
module misc/range_tree_avl
module misc/root_env
module misc/trace_standard
module vm/rootvm
module vm/rootvm_package
module vm/slat
module vm/vcpu
module vm/vcpu_power
module vm/virtio_backend
module vm/virtio_mmio
arch_module aarch64 misc/spectre_arm
arch_module armv8 misc/smc_trace
arch_module armv8 platform/arm_fgt
arch_module armv8 platform/arm_smccc
arch_module armv8 platform/arm_trng_fi
arch_module armv8 vm/arm_pv_time
arch_module armv8 vm/arm_vm_lor_null
arch_module armv8 vm/arm_vm_mpam_direct
arch_module armv8 vm/arm_vm_mte
arch_module armv8 vm/arm_vm_pmu
arch_module armv8 vm/arm_vm_spe
arch_module armv8 vm/arm_vm_ras
arch_module armv8 vm/arm_vm_smmuv2
arch_module armv8 vm/arm_vm_timer
arch_module armv8 vm/smccc
arch_module armv8 vm/vdebug
arch_module armv8 vm/vgic
arch_module cortex-a-v8_0 vm/vetm_null
arch_module cortex-a-v8_2 vm/vetm_null
arch_module cortex-a-v9 misc/vet
arch_module cortex-a-v9 platform/ete
arch_module cortex-a-v9 platform/trbe
arch_module cortex-a-v9 vm/arm_vm_amu
arch_module cortex-a-v9 vm/vete
arch_module cortex-a-v9 vm/vtrbe
arch_module gic-600-lpi vm/vgic_its_v3
arch_module gic-700-vlpi vm/vgic_its_v4
arch_module gic-arm-aem vm/vgic_its_v3
arch_module neoverse-v1 vm/arm_vm_amu
arch_module neoverse-v1 vm/vetm_null
arch_module arm-aem vm/arm_vm_amu
arch_module arm-aem vm/vetm_null
arch_module oryon-ncc-gen1 vm/arm_vm_amu
arch_module oryon-ncc-gen1 vm/vetm_null
arch_module smmuv3 vm/virtio_iommu
