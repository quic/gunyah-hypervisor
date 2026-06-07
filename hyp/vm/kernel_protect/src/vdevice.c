// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <preempt.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>
#include <vcpu.h>
#include <vdevice.h>

#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "event_handlers.h"
#include "kernel_protect_internal.h"

// kp_check_fixmap_access - common helper for all three vdevice fault handlers.
//
// Computes the absolute IPA of the faulting access from the vdevice's IPA base
// and the intra-device byte offset, then delegates to
// kernel_protect_ipa_in_assets_region() to decide whether the address falls
// inside the FIXMAP_TEXT_POKE0 exclusion range.
//
// Returns:
//   VCPU_TRAP_RESULT_EMULATED  - IPA is within the fixmap exclusion range.
//                                NOTE: this is NOT a true emulation — no
//                                instruction decoding or register writeback is
//                                performed. Returning EMULATED is a NOP: it
//                                tells the vdevice layer to advance the PC and
//                                return to the guest as if the access
//                                succeeded, silently allowing the kernel
//                                text-poke write to proceed without injecting a
//                                fault.
//   VCPU_TRAP_RESULT_FAULT     - IPA is outside the fixmap range; the access
//                                is a genuine protection violation and must
//                                be injected as a fault into the guest.
static vcpu_trap_result_t
kp_check_fixmap_access(vdevice_t *vdevice, size_t offset)
{
	thread_t	  *current	= thread_get_self();
	addrspace_t	  *vm_addrspace = current->addrspace;
	vmid_t		   vmid		= vm_addrspace->vmid;
	vcpu_trap_result_t result;

	// Reconstruct the absolute IPA: vdevice base + intra-device offset,
	// then page-align it to match the granularity used by the fixmap check.
	uint64_t ipa	  = (uint64_t)vdevice->range.base + (uint64_t)offset;
	uint64_t ipa_page = util_balign_down(ipa, PGTABLE_VM_PAGE_SIZE);

	if (kernel_protect_ipa_in_assets_region(vmid, ipa_page)) {
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			DEBUG, INFO,
			"GH_KERNEL_PROTECT: fixmap poke in section IPA %llx vmid %lu — NOP (allowing)",
			ipa_page, (uint64_t)vmid);
#endif
		// IPA is within the FIXMAP_TEXT_POKE0 exclusion range.
		// Return EMULATED as a NOP: PC is advanced and control returns
		// to the guest without injecting a fault, silently permitting
		// the kernel live-patch text-poke write to proceed.
		result = VCPU_TRAP_RESULT_EMULATED;
	} else {
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			DEBUG, INFO,
			"GH_KERNEL_PROTECT: illegal write to section IPA %llx vmid %lu — faulting",
			ipa_page, (uint64_t)vmid);
#endif
		// IPA is not in the fixmap exclusion range: this is a genuine
		// protection violation; inject a fault into the guest.
		result = VCPU_TRAP_RESULT_FAULT;
	}

	return result;
}

vcpu_trap_result_t
kernel_protect_handle_vdevice_access_rx(vdevice_type_t type, vdevice_t *vdevice,
					size_t offset, size_t access_size,
					register_t *value, bool is_write,
					vdevice_access_arch_data_t arch_data)
{
	(void)type;
	(void)access_size;
	(void)value;
	(void)arch_data;

	vcpu_trap_result_t result;

	if (!is_write) {
		// Read faults on an RX region are unexpected; leave unhandled
		// so the core vdevice layer can decide what to do.
		result = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// A write fault on an RX (text) region: check whether the faulting
	// IPA belongs to the FIXMAP_TEXT_POKE0 exclusion range.
	result = kp_check_fixmap_access(vdevice, offset);

out:
	return result;
}

vcpu_trap_result_t
kernel_protect_handle_vdevice_access_ro(vdevice_type_t type, vdevice_t *vdevice,
					size_t offset, size_t access_size,
					register_t *value, bool is_write,
					vdevice_access_arch_data_t arch_data)
{
	(void)type;
	(void)access_size;
	(void)value;
	(void)arch_data;

	vcpu_trap_result_t result;

	if (!is_write) {
		// Read faults on an RO region are unexpected; leave unhandled.
		result = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// A write fault on an RO (rodata) region: check whether the faulting
	// IPA belongs to the FIXMAP_TEXT_POKE0 exclusion range.
	result = kp_check_fixmap_access(vdevice, offset);

out:
	return result;
}

vcpu_trap_result_t
kernel_protect_handle_vdevice_access_rw(vdevice_type_t type, vdevice_t *vdevice,
					size_t offset, size_t access_size,
					register_t *value, bool is_write,
					vdevice_access_arch_data_t arch_data)
{
	(void)type;
	(void)access_size;
	(void)value;
	(void)arch_data;

	vcpu_trap_result_t result;

	if (!is_write) {
		// Read faults on an RW region are unexpected; leave unhandled.
		result = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// A write fault on an RW (data/bss) region: check whether the faulting
	// IPA belongs to the FIXMAP_TEXT_POKE0 exclusion range.
	result = kp_check_fixmap_access(vdevice, offset);

out:
	return result;
}
