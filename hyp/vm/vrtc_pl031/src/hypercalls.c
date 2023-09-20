// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <partition.h>
#include <platform_timer.h>
#include <preempt.h>
#include <spinlock.h>
#include <util.h>

error_t
hypercall_vrtc_configure(cap_id_t vrtc_cap, vmaddr_t ipa)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	if (!util_is_baligned(ipa, VRTC_DEV_SIZE) ||
	    util_add_overflows(ipa, VRTC_DEV_SIZE)) {
		err = ERROR_ADDR_INVALID;
		goto out;
	}

	vrtc_ptr_result_t vrtc_r = cspace_lookup_vrtc_any(
		cspace, vrtc_cap, CAP_RIGHTS_VRTC_CONFIGURE);
	if (compiler_unexpected(vrtc_r.e) != OK) {
		err = vrtc_r.e;
		goto out;
	}
	vrtc_t *vrtc = vrtc_r.r;

	spinlock_acquire(&vrtc->header.lock);
	if (atomic_load_relaxed(&vrtc->header.state) == OBJECT_STATE_INIT) {
		vrtc->ipa = ipa;
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vrtc->header.lock);

out:
	return err;
}

error_t
hypercall_vrtc_set_time_base(cap_id_t vrtc_cap, nanoseconds_t time_base,
			     ticks_t sys_timer_ref)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	vrtc_ptr_result_t vrtc_r = cspace_lookup_vrtc(
		cspace, vrtc_cap, CAP_RIGHTS_VRTC_SET_TIME_BASE);
	if (compiler_unexpected(vrtc_r.e) != OK) {
		err = vrtc_r.e;
		goto out;
	}
	vrtc_t *vrtc = vrtc_r.r;

	if (vrtc->time_base != 0U) {
		// The time base has already been set once
		err = ERROR_BUSY;
		goto out;
	}

	preempt_disable();
	ticks_t now = platform_timer_get_current_ticks();
	if (now < sys_timer_ref) {
		// The snapshot was taken in the future?!
		err = ERROR_ARGUMENT_INVALID;
		goto out_preempt;
	}

	ticks_t time_base_ticks = platform_timer_convert_ns_to_ticks(time_base);

	// Set the time_base to the moment the device was turned on. By using
	// "sys_timer_ref" instead of "now" we account for the time delta
	// between the moment the snapshot was taken and the moment the
	// hypercall is handled in the hypervisor.
	vrtc->time_base = time_base_ticks - sys_timer_ref;
	vrtc->lr	= (rtc_seconds_t)(time_base / TIMER_NANOSECS_IN_SECOND);

out_preempt:
	preempt_enable();
out:
	return err;
}

error_t
hypercall_vrtc_attach_addrspace(cap_id_t vrtc_cap, cap_id_t addrspace_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	vrtc_ptr_result_t vrtc_r = cspace_lookup_vrtc(
		cspace, vrtc_cap, CAP_RIGHTS_VRTC_ATTACH_ADDRSPACE);
	if (compiler_unexpected(vrtc_r.e) != OK) {
		err = vrtc_r.e;
		goto out;
	}
	vrtc_t *vrtc = vrtc_r.r;

	addrspace_ptr_result_t addrspace_r = cspace_lookup_addrspace_any(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(addrspace_r.e != OK)) {
		err = addrspace_r.e;
		goto out_release_vrtc;
	}
	addrspace_t *addrspace = addrspace_r.r;

	spinlock_acquire(&addrspace->header.lock);
	if (atomic_load_relaxed(&addrspace->header.state) !=
	    OBJECT_STATE_ACTIVE) {
		err = ERROR_OBJECT_STATE;
		goto out_release_addrspace;
	}

	err = addrspace_check_range(addrspace, vrtc->ipa, VRTC_DEV_SIZE);
	if (err != OK) {
		goto out_release_addrspace;
	}

	vrtc_t *old_vrtc = addrspace->vrtc;
	if (old_vrtc != NULL) {
		object_put_vrtc(old_vrtc);
	}

	addrspace->vrtc = object_get_vrtc_additional(vrtc);

out_release_addrspace:
	spinlock_release(&addrspace->header.lock);
	object_put_addrspace(addrspace);
out_release_vrtc:
	object_put_vrtc(vrtc);
out:
	return err;
}
