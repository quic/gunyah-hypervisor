// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <compiler.h>
#include <platform_prng.h>
#include <thread.h>
#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"

// FIXME: ABI checks disabled since Linux driver is non-compliant.
#define LINUX_TRNG_WORKAROUND

static void NOINLINE
arm_trng_fi_read(vcpu_gpr_t *regs, uint64_t bits, bool smc64)
{
	// TRNG_RND requires x1-x3 to be zero on error
	regs->x[1] = 0x0U;
	regs->x[2] = 0x0U;
	regs->x[3] = 0x0U;

	if ((bits == 0U) || (bits > (smc64 ? 192U : 96U))) {
		regs->x[0] = (uint64_t)ARM_TRNG_RET_INVALID_PARAMETERS;
	} else {
		uint32_t data[192 / 32] = { 0 };
		count_t	 remain		= (count_t)bits;
		int32_t	 i		= (int32_t)util_array_size(data) - 1;

		assert(bits <= 192);

		// Read N-bits of entropy
		while (remain != 0U) {
			assert(i >= 0);

			error_t err = platform_get_random32(&data[i]);
			if (err != OK) {
				break;
			}
			if (remain < 32U) {
				// Mask any unrequested bits
				data[i] &= (uint32_t)util_mask(remain);
				remain = 0U;
			} else {
				remain -= 32U;
			}
			i--;
		}
		if (remain != 0U) {
			regs->x[0] = (uint64_t)ARM_TRNG_RET_NO_ENTROPY;
			goto out;
		}
		// Copy out entropy
		if (smc64) {
			regs->x[3] = data[5] | ((uint64_t)data[4] << 32);
			regs->x[2] = data[3] | ((uint64_t)data[2] << 32);
			regs->x[1] = data[1] | ((uint64_t)data[0] << 32);
		} else {
			regs->x[3] = data[5];
			regs->x[2] = data[4];
			regs->x[1] = data[3];
		}
		// Erase entropy from the stack
		(void)memset_s(data, sizeof(data), 0, sizeof(data));
		CACHE_CLEAN_INVALIDATE_OBJECT(data);

		regs->x[0] = (uint64_t)ARM_TRNG_RET_SUCCESS;
	}
out:
	return;
}

static bool
arm_trng_fi_check_mbz(index_t ra, index_t rb, bool smc64)
{
	assert(rb > ra);
#if defined(LINUX_TRNG_WORKAROUND)

	(void)ra;
	(void)rb;
	(void)smc64;
	return true;
#else
	thread_t *current = thread_get_self();
	bool	  failed  = false;

	for (index_t i = ra; i <= rb; i++) {
		register_t r = current->vcpu_regs_gpr.x[i];
		if (!smc64) {
			r = r & 0xffffffffU;
		}
		if (r != 0x0U) {
			failed = true;
		}
	}

	return !failed;
#endif
}

static bool
arm_trng_fi_handle_call(void)
{
	bool		    handled = false;
	thread_t	   *current = thread_get_self();
	smccc_function_id_t function_id =
		smccc_function_id_cast((uint32_t)current->vcpu_regs_gpr.x[0]);
	smccc_owner_id_t owner_id =
		smccc_function_id_get_owner_id(&function_id);
	smccc_function_t function =
		smccc_function_id_get_function(&function_id);

	if (compiler_expected((owner_id != SMCCC_OWNER_ID_STANDARD) ||
			      (!smccc_function_id_get_is_fast(&function_id)))) {
		goto out;
	}
	if ((function < (smccc_function_t)ARM_TRNG_FUNCTION__MIN) ||
	    (function > (smccc_function_t)ARM_TRNG_FUNCTION__MAX)) {
		goto out;
	}

	arm_trng_function_t f = (arm_trng_function_t)function;
	bool is_smc64	      = smccc_function_id_get_is_smc64(&function_id);

	// Setup the default return
	handled = true;

	// Default return is Unknown Function
	if (is_smc64) {
		current->vcpu_regs_gpr.x[0] = SMCCC_UNKNOWN_FUNCTION64;
	} else {
		current->vcpu_regs_gpr.x[0] = SMCCC_UNKNOWN_FUNCTION32;
	}

	if (is_smc64) {
		switch (f) {
		case ARM_TRNG_FUNCTION_TRNG_RNG:
			if (!arm_trng_fi_check_mbz(2, 7, is_smc64)) {
				current->vcpu_regs_gpr.x[0] = (uint64_t)
					ARM_TRNG_RET_INVALID_PARAMETERS;
				break;
			}

			arm_trng_fi_read(&current->vcpu_regs_gpr,
					 current->vcpu_regs_gpr.x[1], is_smc64);
			break;
		case ARM_TRNG_FUNCTION_TRNG_VERSION:
		case ARM_TRNG_FUNCTION_TRNG_FEATURES:
		case ARM_TRNG_FUNCTION_TRNG_GET_UUID:
		case ARM_TRNG_FUNCTION_LAST_ID:
		default:
			// Unimplemented
			break;
		}
	} else {
		switch (f) {
		case ARM_TRNG_FUNCTION_TRNG_VERSION:
			if (!arm_trng_fi_check_mbz(1, 7, is_smc64)) {
				current->vcpu_regs_gpr.x[0] = (uint64_t)
					ARM_TRNG_RET_INVALID_PARAMETERS;
				break;
			}

			current->vcpu_regs_gpr.x[0] = 0x10000;
			current->vcpu_regs_gpr.x[1] = 0x0;
			current->vcpu_regs_gpr.x[2] = 0x0;
			current->vcpu_regs_gpr.x[3] = 0x0;

			break;
		case ARM_TRNG_FUNCTION_TRNG_FEATURES: {
			if (!arm_trng_fi_check_mbz(2, 7, is_smc64)) {
				current->vcpu_regs_gpr.x[0] = (uint64_t)
					ARM_TRNG_RET_INVALID_PARAMETERS;
				break;
			}

			current->vcpu_regs_gpr.x[0] =
				(uint64_t)ARM_TRNG_RET_NOT_SUPPORTED;

			smccc_function_id_t fid = smccc_function_id_cast(
				(uint32_t)current->vcpu_regs_gpr.x[1]);
			if ((smccc_function_id_get_owner_id(&fid) !=
			     SMCCC_OWNER_ID_STANDARD) ||
			    !smccc_function_id_get_is_fast(&fid) ||
			    (smccc_function_id_get_res0(&fid) != 0U)) {
				break;
			}
			smccc_function_t fn =
				smccc_function_id_get_function(&fid);
			switch ((arm_trng_function_t)fn) {
			case ARM_TRNG_FUNCTION_TRNG_VERSION:
			case ARM_TRNG_FUNCTION_TRNG_FEATURES:
			case ARM_TRNG_FUNCTION_TRNG_GET_UUID:
				if (!smccc_function_id_get_is_smc64(&fid)) {
					current->vcpu_regs_gpr.x[0] =
						(uint64_t)ARM_TRNG_RET_SUCCESS;
				}
				break;
			case ARM_TRNG_FUNCTION_TRNG_RNG:
				current->vcpu_regs_gpr.x[0] =
					(uint64_t)ARM_TRNG_RET_SUCCESS;
				break;
			case ARM_TRNG_FUNCTION_LAST_ID:
			default:
				// Nothing to do
				break;
			}
			break;
		}
		case ARM_TRNG_FUNCTION_TRNG_GET_UUID:
			if (!arm_trng_fi_check_mbz(1, 7, is_smc64)) {
				current->vcpu_regs_gpr.x[0] = (uint64_t)
					ARM_TRNG_RET_INVALID_PARAMETERS;
				break;
			}
			uint32_t uuid[4] = { 0xffffffffU, 0U, 0U, 0U };

			if (platform_get_rng_uuid(uuid) != OK) {
				current->vcpu_regs_gpr.x[0] =
					(uint64_t)ARM_TRNG_RET_NOT_SUPPORTED;
				break;
			}
			assert(uuid[0] != 0xffffffffU);

			current->vcpu_regs_gpr.x[0] = uuid[0];
			current->vcpu_regs_gpr.x[1] = uuid[1];
			current->vcpu_regs_gpr.x[2] = uuid[2];
			current->vcpu_regs_gpr.x[3] = uuid[3];

			break;
		case ARM_TRNG_FUNCTION_TRNG_RNG:
			if (!arm_trng_fi_check_mbz(2, 7, is_smc64)) {
				current->vcpu_regs_gpr.x[0] = (uint64_t)
					ARM_TRNG_RET_INVALID_PARAMETERS;
				break;
			}

			arm_trng_fi_read(
				&current->vcpu_regs_gpr,
				(uint64_t)(uint32_t)current->vcpu_regs_gpr.x[1],
				is_smc64);
			break;
		case ARM_TRNG_FUNCTION_LAST_ID:
		default:
			// Nothing to do
			break;
		}
	}

out:
	return handled;
}

bool
arm_trng_fi_handle_vcpu_trap_smc64(ESR_EL2_ISS_SMC64_t iss)
{
	bool handled = false;

	if (compiler_unexpected(ESR_EL2_ISS_SMC64_get_imm16(&iss) ==
				(uint16_t)0U)) {
		handled = arm_trng_fi_handle_call();
	}

	return handled;
}

bool
arm_trng_fi_handle_vcpu_trap_hvc64(ESR_EL2_ISS_HVC_t iss)
{
	bool handled = false;

	if (compiler_unexpected(ESR_EL2_ISS_HVC_get_imm16(&iss) ==
				(uint16_t)0U)) {
		handled = arm_trng_fi_handle_call();
	}

	return handled;
}
