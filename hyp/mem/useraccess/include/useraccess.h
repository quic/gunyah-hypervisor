// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Copy in or out of VM memory by VA or IPA.
//
// These functions will all return ERROR_ADDR_INVALID if the specified address
// is not mapped in stage 2; ERROR_DENIED if the address is mapped but the
// requested access will permission-fault; ERROR_ARGUMENT_SIZE if the source
// size is zero or the destination size is smaller than the source size; or OK
// if the requested copy was completely successful. In case of errors, the data
// may have been partially copied.
//
// If the force_access argument is true, the copy will ignore the write access
// bit in both stages.
//
// The _va functions will return ERROR_ARGUMENT_INVALID if the access faults in
// stage 1. This includes permission faults.
//
// The _va functions will automatically perform cache maintenance if necessary
// to ensure that the access is coherent with the EL1 view of memory at the
// specified address (though aliases with different stage 1 attributes may
// remain incoherent).
//
// The _ipa functions will perform cache maintenance if the
// force_coherent argument is false and the stage 2 mapping forces a
// non-writeback cache attribute, or if the force_coherent argument is true and
// the stage 2 mapping does not force a writeback attribute; the latter
// behaviour is mostly useful for emulating I/O devices using read-only memory.

size_result_t
useraccess_copy_from_guest_va(void *hyp_va, size_t hsize, gvaddr_t guest_va,
			      size_t gsize);

size_result_t
useraccess_copy_to_guest_va(gvaddr_t guest_va, size_t gsize, const void *hyp_va,
			    size_t hsize, bool force_access);

size_result_t
useraccess_copy_from_guest_ipa(addrspace_t *addrspace, void *hyp_va,
			       size_t hsize, vmaddr_t guest_ipa, size_t gsize,
			       bool force_access, bool force_coherent);

size_result_t
useraccess_copy_to_guest_ipa(addrspace_t *addrspace, vmaddr_t guest_ipa,
			     size_t gsize, const void *hyp_va, size_t hsize,
			     bool force_access, bool force_coherent);
