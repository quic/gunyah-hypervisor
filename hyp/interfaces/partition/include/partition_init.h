// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if (defined(UNIT_TESTS) && UNIT_TESTS) || defined(ROOTVM_INIT) ||             \
	defined(UEFITZT)

// Get the root VM's partition.
//
// This partition is used for constructing the root VM, and is the initial owner
// of every resource that is not used internally by the hypervisor.
//
// Do not add new calls to this function, except to donate memory to the root
// VM. Callers to partition_alloc() and related functions should always use a
// partition obtained either from an explicit partition capability, or from the
// object header of some first-class object that owns the allocation.
partition_t *
partition_get_root(void);

#endif
