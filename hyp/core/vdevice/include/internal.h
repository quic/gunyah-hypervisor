// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Emulate an access to a virtual device backed by physical memory.
//
// This function looks up the given physical address in the memdb, finds the
// corresponding memextent object, and checks whether it is associated with a
// virtual device. If so, it triggers the access event.
//
// This function is intended to be called from a permission fault handler after
// obtaining the physical address from the guest's address space. Since the
// address space might be concurrently modified to unmap the physical address,
// this must be called from an RCU critical section to ensure that the physical
// address is not reused before it has finished.
vcpu_trap_result_t
vdevice_access_phys(paddr_t pa, size_t size, register_t *val, bool is_write)
	REQUIRE_RCU_READ;

// Emulate an access to a virtual device that is not backed by physical memory.
//
// This function looks up the IPA in the current guest address space's virtual
// device mapping. If a virtual device is found, the access event will be
// triggered.
//
// This function is intended to be called from a translation fault handler.
vcpu_trap_result_t
vdevice_access_ipa(vmaddr_t ipa, size_t size, register_t *val, bool is_write);
