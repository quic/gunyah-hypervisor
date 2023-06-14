// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Configure a vdevice that is backed by a physical memory extent.
//
// The given memextent is presumed to be mapped (either before or after this
// call) with reduced permissions, typically read-only, in the guest's address
// space. Any permission fault received for this memextent will be forwarded to
// the access handler for the vdevice. The vdevice's type must be set before
// calling this function.
//
// The caller should ensure that the memextent meets any requirements it has for
// size, memory type / cache attributes, permissions, etc. Normally this would
// be done by calling memextent_attach().
error_t
vdevice_attach_phys(vdevice_t *vdevice, memextent_t *memextent);

// Tear down a vdevice's attachment to a physical memory extent. This must only
// be called after receiving an OK result from vdevice_attach_phys().
//
// Note that calls to the access handler are not guaranteed to be complete until
// an RCU grace period has elapsed after calling this function. If the access
// handler makes use of a pointer to or mapping of the memextent, the caller
// should not release or unmap the memextent until a grace period has elapsed.
void
vdevice_detach_phys(vdevice_t *vdevice, memextent_t *memextent);

// Configure a vdevice that is not backed by physical memory.
//
// After this call succeeds, any translation faults in the specified range will
// be forwarded to the access handler for the vdevice. The vdevice's type must
// be set before calling this function.
//
// The given address range in the addrspace is presumed to not be mapped to any
// physical memextent. If such a mapping exists or is created later, it may
// shadow the device.
//
// The caller is responsible for ensuring that calls to this function are
// serialised for each device. Note that multiple calls are not generally useful
// because only one attachment is allowed.
//
// This function will retain a reference to the specified address space.
error_t
vdevice_attach_vmaddr(vdevice_t *vdevice, addrspace_t *addrspace, vmaddr_t ipa,
		      size_t size);

// Tear down a vdevice's attachment to a guest address range. This must only
// be called after receiving an OK result from vdevice_attach_vmaddr().
//
// Note that calls to the access handler are not guaranteed to be complete and
// it is not safe to call vdevice_attach_vmaddr() again until an RCU grace
// period has elapsed after calling this function.
void
vdevice_detach_vmaddr(vdevice_t *vdevice);
