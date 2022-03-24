// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// This function attaches an address space to a thread.
//
// An address space can only be attached to a thread before it starts running
// and the address space should already be in an activate state.
//
// The function returns an error if the thread is not vcpu kind, if it belongs
// to a HLOS VM and if it the address space has not been activated.
error_t
addrspace_attach_thread(addrspace_t *addrspace, thread_t *thread);

// Configure the address space.
//
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
addrspace_configure(addrspace_t *addrspace, vmid_t vmid);

// Translate a VA to PA in the current guest address space.
paddr_result_t
addrspace_va_to_pa_read(gvaddr_t addr);

// Translate a VA to IPA in the current guest address space.
vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr);
