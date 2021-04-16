// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// These functions will both return ERROR_ADDR_INVALID if the guest address
// faults; ERROR_ARGUMENT_SIZE if the source size is zero or the destination
// size is smaller than the source size; or OK if the requested copy was
// completely successful. In case of errors, the data may have been partially
// copied.

error_t
useraccess_copy_from_guest(void *hyp_va, size_t hsize, gvaddr_t guest_va,
			   size_t gsize);

error_t
useraccess_copy_to_guest(gvaddr_t guest_va, size_t gsize, void *hyp_va,
			 size_t hsize);
