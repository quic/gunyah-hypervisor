// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <vgic.h>
#include <vic.h>

vic_t *
vic_get_vic(const thread_t *vcpu)
{
	return vcpu->vgic_vic;
}

const platform_mpidr_mapping_t *
vgic_get_mpidr_mapping(const vic_t *vic)
{
	return &vic->mpidr_mapping;
}
