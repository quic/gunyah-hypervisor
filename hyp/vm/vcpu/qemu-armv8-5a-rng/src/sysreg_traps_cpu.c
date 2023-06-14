// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <thread.h>
#include <vcpu.h>

#include <asm/system_registers.h>
// #include <asm/system_registers_cpu.h>

#include "event_handlers.h"

// There are no implementation specific EL1 registers to emulate for QEMU.

vcpu_trap_result_t
sysreg_read_cpu(ESR_EL2_ISS_MSR_MRS_t iss)
{
	(void)iss;
	return VCPU_TRAP_RESULT_UNHANDLED;
}

vcpu_trap_result_t
sysreg_write_cpu(ESR_EL2_ISS_MSR_MRS_t iss)
{
	(void)iss;
	return VCPU_TRAP_RESULT_UNHANDLED;
}
