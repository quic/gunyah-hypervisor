// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <platform_timer.h>
#include <preempt.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <vic.h>

#include "event_handlers.h"

error_t
vrtc_pl031_handle_object_create_vrtc(vrtc_create_t params)
{
	vrtc_t *vrtc = params.vrtc;
	assert(vrtc != NULL);

	vrtc->ipa	= VMADDR_INVALID;
	vrtc->lr	= 0;
	vrtc->time_base = 0;

	return OK;
}

error_t
vrtc_pl031_handle_object_activate_vrtc(vrtc_t *vrtc)
{
	error_t err = OK;

	assert(vrtc != NULL);

	if (vrtc->ipa == VMADDR_INVALID) {
		// Not configured yet
		err = ERROR_OBJECT_CONFIG;
	}

	return err;
}

void
vrtc_pl031_handle_object_deactivate_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	vrtc_t *vrtc = addrspace->vrtc;

	if (vrtc != NULL) {
		object_put_vrtc(vrtc);
		addrspace->vrtc = NULL;
	}
}

static void
vrtc_pl031_reg_read(vrtc_t *vrtc, size_t offset, register_t *value)
{
	if (offset == offsetof(vrtc_pl031_t, RTCDR)) {
		uint64_t now = platform_timer_get_current_ticks();
		*value = platform_convert_ticks_to_ns(vrtc->time_base + now) /
			 TIMER_NANOSECS_IN_SECOND;
	} else if (offset == offsetof(vrtc_pl031_t, RTCLR)) {
		*value = vrtc->lr;
	} else if (offset == offsetof(vrtc_pl031_t, RTCCR)) {
		// Always enabled
		*value = 1U;
	} else if ((offset >= offsetof(vrtc_pl031_t, RTCPeriphID0)) &&
		   (offset <= offsetof(vrtc_pl031_t, RTCPeriphID3))) {
		// Calculate which byte in the ID register they are after
		uint8_t id = (uint8_t)((offset -
					offsetof(vrtc_pl031_t, RTCPeriphID0)) >>
				       2);
		*value	   = ((register_t)VRTC_PL031_PERIPH_ID >> (id << 3)) &
			 0xffU;
	} else if ((offset >= offsetof(vrtc_pl031_t, RTCPCellID0)) &&
		   (offset <= offsetof(vrtc_pl031_t, RTCPCellID3))) {
		// Calculate which byte in the ID register they are after
		uint8_t id = (uint8_t)((offset -
					offsetof(vrtc_pl031_t, RTCPCellID0)) >>
				       2);
		*value = ((register_t)VRTC_PL031_PCELL_ID >> (id << 3)) & 0xffU;
	} else {
		// All other PL031 registers are treated as RAZ
		*value = 0U;
	}
}

static void
vrtc_pl031_reg_write(vrtc_t *vrtc, size_t offset, register_t *value)
{
	if (offset == offsetof(vrtc_pl031_t, RTCLR)) {
		ticks_t value_ticks = platform_convert_ns_to_ticks(
			*value * TIMER_NANOSECS_IN_SECOND);
		preempt_disable();
		ticks_t now	= platform_timer_get_current_ticks();
		vrtc->time_base = value_ticks - now;
		preempt_enable();
		vrtc->lr = (rtc_seconds_t)(*value);
	}
	// The rest of the registers are WI.
}

vcpu_trap_result_t
vrtc_pl031_handle_vdevice_access_fixed_addr(vmaddr_t ipa, size_t access_size,
					    register_t *value, bool is_write)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	thread_t *thread = thread_get_self();

	vrtc_t *vrtc = thread->addrspace->vrtc;
	if ((vrtc == NULL) || (vrtc->ipa == VMADDR_INVALID)) {
		// vRTC not initialised
		goto out;
	}

	if ((ipa < vrtc->ipa) || util_add_overflows(ipa, access_size) ||
	    ((ipa + access_size) > (vrtc->ipa + VRTC_DEV_SIZE))) {
		// Not vRTC
		goto out;
	}

	// Only 32-bit registers of PL031 are emulated
	if (access_size != sizeof(uint32_t) ||
	    !util_is_baligned(ipa, sizeof(uint32_t))) {
		ret = VCPU_TRAP_RESULT_FAULT;
		goto out;
	}

	size_t offset = (size_t)(ipa - vrtc->ipa);

	if (is_write) {
		vrtc_pl031_reg_write(vrtc, offset, value);
	} else {
		vrtc_pl031_reg_read(vrtc, offset, value);
	}

	ret = VCPU_TRAP_RESULT_EMULATED;

out:
	return ret;
}
