// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(HYPERCALLS)
#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>

// Placeholders for unimplemented objects

// Dynamic creation of partitions is not yet implemented
hypercall_partition_create_partition_result_t
hypercall_partition_create_partition(cap_id_t src_partition_cap,
				     cap_id_t cspace_cap)
{
	(void)src_partition_cap;
	(void)cspace_cap;
	return (hypercall_partition_create_partition_result_t){
		.error	 = ERROR_UNIMPLEMENTED,
		.new_cap = CSPACE_CAP_INVALID,
	};
}

#else
extern int unused;
#endif
