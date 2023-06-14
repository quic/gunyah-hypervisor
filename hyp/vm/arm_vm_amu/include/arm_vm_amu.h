// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(ARCH_ARM_FEAT_AMUv1) || defined(ARCH_ARM_FEAT_AMUv1p1)
typedef uint64_t arm_vm_amu_offsets_t[PLATFORM_AMU_CNT_NUM];
typedef uint64_t arm_vm_amu_aux_offsets_t[PLATFORM_AMU_AUX_CNT_NUM];

uint64_t
arm_vm_amu_get_counter(index_t index);

uint64_t
arm_vm_amu_get_aux_counter(index_t index);

uint64_t
arm_vm_amu_get_event_type(index_t index);

uint64_t
arm_vm_amu_get_aux_event_type(index_t index);

void
arm_vm_amu_add_counters(arm_vm_amu_offsets_t *offsets);

void
arm_vm_amu_subtract_counters(arm_vm_amu_offsets_t *offsets);

void
arm_vm_amu_add_aux_counters(arm_vm_amu_aux_offsets_t *offsets);

void
arm_vm_amu_subtract_aux_counters(arm_vm_amu_aux_offsets_t *offsets);
#endif
