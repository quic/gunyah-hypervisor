// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>

#include <atomic.h>
#include <compiler.h>
#include <log.h>
#include <trace.h>

#include <events/tests.h>

#include "event_handlers.h"

extern const char  hypervisor_version[];
static const char *msg_ptr;

void
test_print_hyp_version_init(void)
{
	msg_ptr = hypervisor_version;
	return;
}

error_t
test_print_hyp_version(tests_run_id_t test_id)

{
	if (test_id == TESTS_RUN_ID_SMC_0) {
		LOG(USER, TEST, "{:s}", (register_t)msg_ptr);
		return OK;
	} else {
		return ERROR_UNIMPLEMENTED;
	}
}
