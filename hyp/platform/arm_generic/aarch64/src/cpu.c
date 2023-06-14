// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <platform_cpu.h>

platform_mpidr_mapping_t
platform_cpu_get_mpidr_mapping(void)
{
	MPIDR_EL1_t real_mpidr = register_MPIDR_EL1_read();

	return (platform_mpidr_mapping_t){
		.aff_shift    = { PLATFORM_MPIDR_AFF0_SHIFT,
				  PLATFORM_MPIDR_AFF1_SHIFT,
				  PLATFORM_MPIDR_AFF2_SHIFT,
				  PLATFORM_MPIDR_AFF3_SHIFT },
		.aff_mask     = { PLATFORM_MPIDR_AFF0_MASK,
				  PLATFORM_MPIDR_AFF1_MASK,
				  PLATFORM_MPIDR_AFF2_MASK,
				  PLATFORM_MPIDR_AFF3_MASK },
		.multi_thread = MPIDR_EL1_get_MT(&real_mpidr),
		.uniprocessor = MPIDR_EL1_get_U(&real_mpidr),
	};
}

MPIDR_EL1_t
platform_cpu_map_index_to_mpidr(const platform_mpidr_mapping_t *mapping,
				index_t				index)
{
	MPIDR_EL1_t mpidr = MPIDR_EL1_default();

	assert(mapping->aff_shift[0] < 32U);
	assert(mapping->aff_shift[1] < 32U);
	assert(mapping->aff_shift[2] < 32U);
	assert(mapping->aff_shift[3] < 32U);

	MPIDR_EL1_set_Aff0(&mpidr, (uint8_t)((index >> mapping->aff_shift[0]) &
					     mapping->aff_mask[0]));
	MPIDR_EL1_set_Aff1(&mpidr, (uint8_t)((index >> mapping->aff_shift[1]) &
					     mapping->aff_mask[1]));
	MPIDR_EL1_set_Aff2(&mpidr, (uint8_t)((index >> mapping->aff_shift[2]) &
					     mapping->aff_mask[2]));
	MPIDR_EL1_set_Aff3(&mpidr, (uint8_t)((index >> mapping->aff_shift[3]) &
					     mapping->aff_mask[3]));
	MPIDR_EL1_set_MT(&mpidr, mapping->multi_thread);
	MPIDR_EL1_set_U(&mpidr, mapping->uniprocessor);

	return mpidr;
}

index_t
platform_cpu_map_mpidr_to_index(const platform_mpidr_mapping_t *mapping,
				MPIDR_EL1_t			mpidr)
{
	index_t index = 0U;

	assert(mapping->aff_shift[0] < 32U);
	assert(mapping->aff_shift[1] < 32U);
	assert(mapping->aff_shift[2] < 32U);
	assert(mapping->aff_shift[3] < 32U);

	index |= ((index_t)MPIDR_EL1_get_Aff0(&mpidr) &
		  (index_t)mapping->aff_mask[0])
		 << mapping->aff_shift[0];
	index |= ((index_t)MPIDR_EL1_get_Aff1(&mpidr) &
		  (index_t)mapping->aff_mask[1])
		 << mapping->aff_shift[1];
	index |= ((index_t)MPIDR_EL1_get_Aff2(&mpidr) &
		  (index_t)mapping->aff_mask[2])
		 << mapping->aff_shift[2];
	index |= ((index_t)MPIDR_EL1_get_Aff3(&mpidr) &
		  (index_t)mapping->aff_mask[3])
		 << mapping->aff_shift[3];

	return index;
}

bool
platform_cpu_map_mpidr_valid(const platform_mpidr_mapping_t *mapping,
			     MPIDR_EL1_t		     mpidr)
{
	bool valid = true;

	assert(mapping->aff_shift[0] < 32U);
	assert(mapping->aff_shift[1] < 32U);
	assert(mapping->aff_shift[2] < 32U);
	assert(mapping->aff_shift[3] < 32U);

	if ((MPIDR_EL1_get_Aff0(&mpidr) & ~mapping->aff_mask[0]) != 0U) {
		valid = false;
	}
	if ((MPIDR_EL1_get_Aff1(&mpidr) & ~mapping->aff_mask[1]) != 0U) {
		valid = false;
	}
	if ((MPIDR_EL1_get_Aff2(&mpidr) & ~mapping->aff_mask[2]) != 0U) {
		valid = false;
	}
	if ((MPIDR_EL1_get_Aff3(&mpidr) & ~mapping->aff_mask[3]) != 0U) {
		valid = false;
	}

	return valid;
}

MPIDR_EL1_t
platform_cpu_index_to_mpidr(index_t index)
{
	platform_mpidr_mapping_t mapping = platform_cpu_get_mpidr_mapping();
	return platform_cpu_map_index_to_mpidr(&mapping, index);
}

index_t
platform_cpu_mpidr_to_index(MPIDR_EL1_t mpidr)
{
	platform_mpidr_mapping_t mapping = platform_cpu_get_mpidr_mapping();
	return platform_cpu_map_mpidr_to_index(&mapping, mpidr);
}

bool
platform_cpu_mpidr_valid(MPIDR_EL1_t mpidr)
{
	platform_mpidr_mapping_t mapping = platform_cpu_get_mpidr_mapping();
	return platform_cpu_map_mpidr_valid(&mapping, mpidr);
}
