// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Return true if vcpu_run is enabled for the specified VCPU.
//
// Returns false if vcpu_run is disabled or the specified thread is not a VCPU.
//
// Note that this is all-or-nothing for any given VCPU: either it is scheduled
// exclusively by calling the vcpu_run hypercall, or else it is scheduled by the
// EL2 scheduler and any attempt to call vcpu_run on it will fail.
//
// The caller must hold the specified thread's scheduler lock.
bool
vcpu_run_is_enabled(const thread_t *vcpu) REQUIRE_SCHEDULER_LOCK(vcpu);
