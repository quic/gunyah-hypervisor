// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <log.h>
#include <panic.h>
#include <platform_timer.h>
#include <preempt.h>
#include <sdei_dispatcher.h>
#include <smccc.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

extern void
sdei_client_handle_event_entry(void);

extern void
sdei_client_handle_event(uint64_t event_num, uint64_t ep_argument, uint64_t pc,
			 uint64_t pstate) REQUIRE_PREEMPT_DISABLED;

static bool			sdei_client_enabled;
static sdei_client_event_info_t sdei_client_event_info;

static const uint32_t sdei_client_private_events[] = {
	SDEI_VENDOR_EVENT_EL3_ERROR,
};

static smccc_function_id_t
sdei_client_create_fn_id(sdei_function_t fn)
{
	return smccc_create_fn_id((smccc_function_t)fn, SMCCC_OWNER_ID_STANDARD,
				  true, true);
}

static int64_t
sdei_client_get_version(void)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_VERSION);

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_private_reset(void)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_PRIVATE_RESET);

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_shared_reset(void)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_SHARED_RESET);

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_register_event(uint32_t event_num)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_EVENT_REGISTER);

	sdei_event_register_flags_t flags = sdei_event_register_flags_default();

	args[0] = event_num;
	args[1] = (uintptr_t)&sdei_client_handle_event_entry;
	args[2] = 0x0; // ep_argument
	args[3] = sdei_event_register_flags_raw(flags);
	args[4] = 0x0; // affinity

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_enable_event(uint32_t event_num)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_EVENT_ENABLE);

	args[0] = event_num;

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_disable_event(uint32_t event_num)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_EVENT_DISABLE);

	args[0] = event_num;

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static int64_t
sdei_client_mask_events(void)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_PE_MASK);

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	return (int64_t)ret[0];
}

static void
sdei_client_unmask_events(void)
{
	uint64_t args[SMCCC_1_1_ARGS] = { 0 };
	uint64_t ret[SMCCC_1_1_RETS];

	smccc_function_id_t fn_id =
		sdei_client_create_fn_id(SDEI_FUNCTION_PE_UNMASK);

	smccc_1_1_call(fn_id, &args, &ret, NULL, CLIENT_ID_HYP, false);

	assert((int64_t)ret[0] == SDEI_RET_SUCCESS);
}

static bool
sdei_client_version_is_compatible(sdei_version_t dispatcher_ver)
{
	uint16_t major = sdei_version_get_major(&dispatcher_ver);
	uint16_t minor = sdei_version_get_minor(&dispatcher_ver);

	return (SDEI_CLIENT_VERSION_MAJOR == major) &&
	       (SDEI_CLIENT_VERSION_MINOR <= minor);
}

static void
sdei_client_register_and_enable_private_events(void)
{
	for (index_t i = 0U; i < util_array_size(sdei_client_private_events);
	     i++) {
		uint32_t event_num = sdei_client_private_events[i];

		int64_t err = sdei_client_register_event(event_num);
		if ((err != SDEI_RET_SUCCESS) && (err != SDEI_RET_DENIED)) {
			TRACE_AND_LOG(DEBUG, INFO,
				      "sdei: event {:#x} register failed {:#x}",
				      event_num, (uint64_t)err);
			continue;
		}

		err = sdei_client_enable_event(event_num);
		if (err != SDEI_RET_SUCCESS) {
			TRACE_AND_LOG(DEBUG, INFO,
				      "sdei: event {:#x} enable failed {:#x}",
				      event_num, (uint64_t)err);
			continue;
		}
	}
}

static void
sdei_client_disable_private_events(void)
{
	for (index_t i = 0U; i < util_array_size(sdei_client_private_events);
	     i++) {
		uint32_t event_num = sdei_client_private_events[i];

		(void)sdei_client_disable_event(event_num);
	}
}

void
sdei_client_handle_boot_cold_init(void)
{
	int64_t err = sdei_client_get_version();
	if (err < 0) {
		TRACE_AND_LOG(DEBUG, INFO, "sdei: get version err {:d}",
			      (uint64_t)err);
		goto out;
	}

	sdei_version_t version = sdei_version_cast((uint64_t)err);
	if (!sdei_client_version_is_compatible(version)) {
		TRACE_AND_LOG(DEBUG, INFO, "sdei: version not compatible {:#x}",
			      (uint64_t)err);
		goto out;
	}

	TRACE_AND_LOG(DEBUG, INFO, "sdei: detected version {:#x}",
		      (uint64_t)err);

	err = sdei_client_shared_reset();
	if (err != SDEI_RET_SUCCESS) {
		panic("sdei: shared reset failed");
	}

	sdei_client_enabled = true;
out:
	return;
}

void
sdei_client_handle_boot_cpu_cold_init(void)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	(void)sdei_client_mask_events();

	int64_t err = sdei_client_private_reset();
	if (err != SDEI_RET_SUCCESS) {
		panic("sdei: private reset failed");
	}
out:
	return;
}

void
sdei_client_handle_boot_cpu_warm_init(void)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	sdei_client_register_and_enable_private_events();
out:
	return;
}

void
sdei_client_handle_boot_cpu_start(void)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	// Called after:
	// - cold boot of any cpu
	// - warm boot after cpu is taken offline
	// - warm boot after cpu suspend to powerdown state

	sdei_client_unmask_events();
out:
	return;
}

void
sdei_client_handle_power_cpu_offline(void) REQUIRE_LOCK(preempt_disabled)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	// Called before:
	// - cpu is taken offline
	sdei_client_disable_private_events();
	(void)sdei_client_mask_events();

out:
	return;
}

error_t
sdei_client_handle_power_cpu_suspend(bool may_poweroff)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	// Called before:
	// - cpu suspend to powerdown state
	if (may_poweroff) {
		(void)sdei_client_mask_events();
	}

out:
	return OK;
}

void
sdei_client_unwind_handle_power_cpu_suspend(bool may_poweroff)
{
	if (!sdei_client_enabled) {
		goto out;
	}

	// Called after:
	// - cpu suspend to powerdown state fails
	if (may_poweroff) {
		sdei_client_unmask_events();
	}

out:
	return;
}

void
sdei_client_handle_event(uint64_t event_num, uint64_t ep_argument, uint64_t pc,
			 uint64_t pstate)
{
	// This function runs in the SDEI event handler context. Be very
	// careful of what you do here!
	//
	// E.g. you may not acquire any spinlocks, because the hypervisor may
	// have been interrupted while in a critical section.

	count_t preempt_bits = preempt_enter_nmi();
	(void)ep_argument;

	// We expect only one event to be delivered (the error event). If for
	// some reason we get another, it is ignored.
	static atomic_bool triggered;
	if (atomic_exchange_relaxed(&triggered, true)) {
		goto out;
	}

	sdei_client_event_info_t *info = &sdei_client_event_info;
	info->event_num		       = (uint32_t)event_num;
	info->pc		       = pc;
	info->pstate		       = pstate;
	info->ticks		       = platform_timer_get_current_ticks();

	cpu_index_t cpu = cpulocal_get_index_unsafe();
	if (!cpulocal_index_valid(cpu)) {
		cpu = 0U;
	}

	// Send an IPI to ourselves so we can handle the event safely in a
	// 'normal' interrupt context instead of the SDEI event context.
	ipi_one(IPI_REASON_SDEI_CLIENT_HANDLE_EVENT, cpu);
out:
	preempt_exit_nmi(preempt_bits);
	return;
}

bool
sdei_client_handle_ipi_received(void)
{
	sdei_client_event_info_t *info = &sdei_client_event_info;

	TRACE_AND_LOG(
		ERROR, WARN,
		"sdei: event received: event_num {:x}, ticks {:d}, pc {:#x}, pstate {:#x}",
		info->event_num, info->ticks, info->pc, info->pstate);

	sdei_error_flags_t flags = sdei_error_flags_default();
	sdei_error_flags_set_reason(&flags, SDEI_ERROR_REASON_SENT_BY_EL3);

	sdei_start_system_grace_period("sdei: event received from EL3",
				       ABORT_REASON_PANIC, flags);

	return true;
}
