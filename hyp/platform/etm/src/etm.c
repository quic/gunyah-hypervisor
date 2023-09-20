// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_security.h>
#include <platform_timer.h>
#include <trace.h>
#include <util.h>

#include "etm.h"
#include "event_handlers.h"

#if defined(PLATFORM_ETM_REG_WRITE_WORKAROUND)
// this work around is for context save, since we are writing lots of registers
// back to back, it could block other master on NOC.
#define CTX_WRITE_WORKAROUND platform_timer_ndelay(20000)
#else
#define CTX_WRITE_WORKAROUND
#endif

#if !defined(ETM_USE_SOFTWARE_LOCK)
// Using or implementing TRCLAR is deprecated. Linux doesn't use it.
#define ETM_USE_SOFTWARE_LOCK 0
#endif

typedef struct {
	size_t reg_offset;
	size_t access_size;
	size_t count;
	size_t stride;
} context_register_info_t;

static etm_t *mapped_etms[PLATFORM_MAX_CORES];

static register_t *etm_contexts[PLATFORM_MAX_CORES];

static uint32_t etm_claim_tag[PLATFORM_MAX_CORES];

static uint32_t etm_cprgctlr[PLATFORM_MAX_CORES];

#define ETM_REGISTER(name)                                                     \
	{                                                                      \
		.reg_offset  = offsetof(etm_t, name),                          \
		.access_size = util_sizeof_member(etm_t, name), .count = 1,    \
		.stride = 0                                                    \
	}

#define ETM_REGISTER_ARRAY(name)                                               \
	{                                                                      \
		.reg_offset  = offsetof(etm_t, name),                          \
		.access_size = util_sizeof_member(etm_t, name[0]),             \
		.count	     = util_sizeof_member(etm_t, name) /               \
			 util_sizeof_member(etm_t, name[0]),                   \
		.stride = util_sizeof_member(etm_t, name[0])                   \
	}

#define ETM_REGISTER_SPARSE_ARRAY(name)                                        \
	{                                                                      \
		.reg_offset  = offsetof(etm_t, name),                          \
		.access_size = util_sizeof_member(etm_t, name[0].value),       \
		.count	     = util_sizeof_member(etm_t, name) /               \
			 util_sizeof_member(etm_t, name[0]),                   \
		.stride = util_sizeof_member(etm_t, name[0])                   \
	}

// NOTE: registers are saved in the context memory region based on their
// index in context_register_list. Make sure the alignment is correct
static const context_register_info_t context_register_list[] = {
	// main control & configuration regsters
	ETM_REGISTER(trcprocselr),
	ETM_REGISTER(trcconfigr),
	ETM_REGISTER(trcauxctlr),
	ETM_REGISTER(trceventctl0r),
	ETM_REGISTER(trceventctl1r),
	ETM_REGISTER(trcstallctlr),
	ETM_REGISTER(trctsctlr),
	ETM_REGISTER(trcsyncpr),
	ETM_REGISTER(trcccctlr),
	ETM_REGISTER(trcbbctlr),
	ETM_REGISTER(trctraceidr),
	ETM_REGISTER(trcqctlr),

	// filtering control registers
	ETM_REGISTER(trcvictlr),
	ETM_REGISTER(trcviiectlr),
	ETM_REGISTER(trcvissctlr),
	ETM_REGISTER(trcvipcssctlr),
	ETM_REGISTER(trcvdctlr),
	ETM_REGISTER(trcvdsacctlr),
	ETM_REGISTER(trcvdarcctlr),

	// derived resources registers
	ETM_REGISTER_ARRAY(trcseqevr),
	ETM_REGISTER(trcseqrstevr),
	ETM_REGISTER(trcseqstr),
	ETM_REGISTER(trcextinselr),
	ETM_REGISTER_ARRAY(trccntrldvr),
	ETM_REGISTER_ARRAY(trccntctlr),
	ETM_REGISTER_ARRAY(trccntvr),

	// resource selection registers
	ETM_REGISTER_ARRAY(trcrsctlr2),

	// comparator registers
	ETM_REGISTER_ARRAY(trcacvr),
	ETM_REGISTER_ARRAY(trcacatr),

	ETM_REGISTER_SPARSE_ARRAY(trcdvcvr),
	ETM_REGISTER_SPARSE_ARRAY(trcdvcmr),

	ETM_REGISTER_ARRAY(trccidcvr),
	ETM_REGISTER_ARRAY(trccidcctlr),

	ETM_REGISTER_ARRAY(trcvmidcvr),
	ETM_REGISTER_ARRAY(trcvmidcctlr),

	// single shot comparator registers
	ETM_REGISTER_ARRAY(trcssccr),
	ETM_REGISTER_ARRAY(trcsscsr),
	ETM_REGISTER_ARRAY(trcsspcicr),
};

static size_t
etm_get_context_size_percpu(void)
{
	size_t ret = 0UL;

	// can be optimized by read last entry offset + size, but need to
	// restrict the timing to call this context
	for (index_t i = 0; i < util_array_size(context_register_list); i++) {
		const context_register_info_t *info = &context_register_list[i];
		ret += sizeof(uint64_t) * info->count;
	}
	return ret;
}

void
etm_handle_boot_hypervisor_start(void)
{
	if (compiler_expected(platform_security_state_debug_disabled())) {
		goto out;
	}

	partition_t *hyp_partition = partition_get_private();

	// FIXME: remove when read from device tree
	paddr_t etm_base   = PLATFORM_ETM_BASE;
	size_t	etm_stride = PLATFORM_ETM_STRIDE;

	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		virt_range_result_t range =
			hyp_aspace_allocate(PLATFORM_ETM_SIZE_PERCPU);
		if (range.e != OK) {
			panic("ETM: Address allocation failed.");
		}

		paddr_t cur_base = etm_base + i * etm_stride;

		pgtable_hyp_start();

		error_t ret = pgtable_hyp_map(
			hyp_partition, range.r.base, PLATFORM_ETM_SIZE_PERCPU,
			cur_base, PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
			PGTABLE_ACCESS_RW, VMSA_SHAREABILITY_NON_SHAREABLE);
		if (ret != OK) {
			panic("ETM: Mapping of etm register failed.");
		}
		mapped_etms[i] = (etm_t *)range.r.base;

		pgtable_hyp_commit();
	}

	// allocate contexts
	size_t context_size = etm_get_context_size_percpu();
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		void_ptr_result_t alloc_r = partition_alloc(
			hyp_partition, context_size, alignof(uint64_t));
		if (alloc_r.e != OK) {
			panic("failed to allocate ETM context memory");
		}

		etm_contexts[i] = (register_t *)alloc_r.r;
		(void)memset_s(etm_contexts[i], context_size, 0, context_size);
	}

out:
	return;
}

void
etm_set_reg(cpu_index_t cpu, size_t offset, register_t val, size_t access_size)
{
	(void)access_size;

	assert(cpulocal_index_valid(cpu));

	assert(offset < (sizeof(*mapped_etms[cpu]) - access_size));
	uintptr_t base = (uintptr_t)mapped_etms[cpu];

	if (access_size == sizeof(uint32_t)) {
		_Atomic uint32_t *reg = (_Atomic uint32_t *)(base + offset);
		atomic_store_relaxed(reg, val);
	} else if (access_size == sizeof(uint64_t)) {
		_Atomic uint64_t *reg = (_Atomic uint64_t *)(base + offset);
		atomic_store_relaxed(reg, val);
	} else {
		panic("ETM: invalid access size");
	}
}

void
etm_get_reg(cpu_index_t cpu, size_t offset, register_t *val, size_t access_size)
{
	(void)access_size;

	assert(cpulocal_index_valid(cpu));

	uintptr_t base = (uintptr_t)mapped_etms[cpu];

	if (access_size == sizeof(uint32_t)) {
		// Regards the ETM v4 doc: HW implementation supports 32-bit
		// accesses to access 32-bit registers or either half of a
		// 64-bit register.
		_Atomic uint32_t *reg = (_Atomic uint32_t *)(base + offset);

		*val = atomic_load_relaxed(reg);
	} else if (access_size == sizeof(uint64_t)) {
		_Atomic uint64_t *reg = (_Atomic uint64_t *)(base + offset);

		*val = atomic_load_relaxed(reg);
	} else {
		panic("ETM: invalid access size");
	}
}

static void
etm_unlock_percpu(cpu_index_t cpu)
{
#if ETM_USE_SOFTWARE_LOCK
	atomic_store_relaxed(&mapped_etms[cpu]->trclar, ETM_TRCLAR_UNLOCK);
	CTX_WRITE_WORKAROUND;
#else
	(void)cpu;
#endif
}

#if ETM_USE_SOFTWARE_LOCK
static void
etm_lock_percpu(cpu_index_t cpu)
{
	atomic_store_relaxed(&mapped_etms[cpu]->trclar, ETM_TRCLAR_LOCK);
	CTX_WRITE_WORKAROUND;
}
#endif

static void
etm_os_unlock_percpu(cpu_index_t cpu)
{
	atomic_store_relaxed(&mapped_etms[cpu]->trcoslar, ETM_TRCOSLAR_UNLOCK);

	// Note: no write delay workaround for this register, to avoid delaying
	// resume when the ETM is not being used. It is always written last
	// in the sequence anyway, so a delay after it is useless.
}

static void
etm_os_lock_percpu(cpu_index_t cpu)
{
	atomic_store_relaxed(&mapped_etms[cpu]->trcoslar, ETM_TRCOSLAR_LOCK);

	// Note: no write delay workaround for this register, to avoid delaying
	// suspend when the ETM is not being used. The suspend sequence should
	// start with a conditional CTX_WRITE_WORKAROUND as a substitute.
}

static index_t
etm_save_context_registers(cpu_index_t cpu, const context_register_info_t *info,
			   index_t context_register_index)
{
	register_t *context = etm_contexts[cpu];

	index_t cur_register_index = context_register_index;

	for (index_t i = 0; i < info->count; i++, cur_register_index++) {
		size_t reg_offset = info->reg_offset + i * info->stride;

		register_t *reg = context + cur_register_index;

		etm_get_reg(cpu, reg_offset, reg, info->access_size);
	}

	return cur_register_index;
}

void
etm_save_context_percpu(cpu_index_t cpu)
{
	// dsb isb
	__asm__ volatile("dsb ish; isb" ::: "memory");

	// Delay after taking the OS lock in the caller
	CTX_WRITE_WORKAROUND;

	// pull trcstatr.pmstable until it's stable
	// wait up to 100us
	ticks_t start = platform_timer_get_current_ticks();
	ticks_t timeout =
		start + platform_timer_convert_ns_to_ticks(100U * 1000U);
	do {
		ETM_TRCSTATR_t trcstatr =
			atomic_load_relaxed(&mapped_etms[cpu]->trcstatr);
		if (ETM_TRCSTATR_get_pmstable(&trcstatr)) {
			break;
		}

		if (platform_timer_get_current_ticks() > timeout) {
			TRACE_AND_LOG(ERROR, INFO,
				      "ETM: programmers model is not stable");
			break;
		}
	} while (1);

	etm_cprgctlr[cpu] = atomic_load_relaxed(&mapped_etms[cpu]->trcprgctlr);
	if ((etm_cprgctlr[cpu] & ETM_TRCPRGCTLR_ENABLE) != 0U) {
		// save all context registers
		index_t context_register_index = 0U;
		for (index_t i = 0; i < util_array_size(context_register_list);
		     i++) {
			const context_register_info_t *info =
				&context_register_list[i];

			context_register_index = etm_save_context_registers(
				cpu, info, context_register_index);
		}

		etm_claim_tag[cpu] =
			atomic_load_relaxed(&mapped_etms[cpu]->trcclaimclr);

		// poll until trcstatr.idle
		count_t idle_count = 100U;
		bool	idle	   = false;
		while (idle_count > 0U) {
			// should be a volatile read
			ETM_TRCSTATR_t trcstatr = atomic_load_relaxed(
				&mapped_etms[cpu]->trcstatr);
			idle = ETM_TRCSTATR_get_idle(&trcstatr);

			if (idle) {
				break;
			}

			idle_count--;
			platform_timer_ndelay(1000);
		}

		if ((!idle) && (idle_count == 0)) {
			LOG(ERROR, WARN,
			    "ETM: waiting idle timeout for context save");
		}
	}
	return;
}

static index_t
etm_restore_context_registers(cpu_index_t		     cpu,
			      const context_register_info_t *info,
			      index_t context_register_index)
{
	register_t *context = etm_contexts[cpu];

	index_t cur_register_index = context_register_index;

	for (index_t i = 0; i < info->count; i++, cur_register_index++) {
		size_t reg_offset = info->reg_offset + i * info->stride;

		register_t *reg = context + cur_register_index;

		etm_set_reg(cpu, reg_offset, *reg, info->access_size);
		CTX_WRITE_WORKAROUND;
	}

	return cur_register_index;
}

void
etm_restore_context_percpu(cpu_index_t cpu)
{
	if (((etm_cprgctlr[cpu]) & ETM_TRCPRGCTLR_ENABLE) != 0U) {
		// restore all context registers
		index_t context_register_index = 0U;
		for (index_t i = 0; i < util_array_size(context_register_list);
		     i++) {
			const context_register_info_t *info =
				&context_register_list[i];

			context_register_index = etm_restore_context_registers(
				cpu, info, context_register_index);
		}

		// set claim tag
		atomic_store_relaxed(&mapped_etms[cpu]->trcclaimset,
				     etm_claim_tag[cpu]);
		CTX_WRITE_WORKAROUND;

		atomic_store_relaxed(&mapped_etms[cpu]->trcprgctlr,
				     ETM_TRCPRGCTLR_ENABLE);
	}

	return;
}

void
etm_handle_power_cpu_online(void)
{
	if (compiler_unexpected(!platform_security_state_debug_disabled())) {
		cpu_index_t cpu = cpulocal_get_index();
		etm_unlock_percpu(cpu);
		etm_os_unlock_percpu(cpu);
	}
}

void
etm_handle_power_cpu_offline(void)
{
	(void)etm_handle_power_cpu_suspend(true);
}

error_t
etm_handle_power_cpu_suspend(bool may_poweroff)
{
	if (may_poweroff &&
	    compiler_unexpected(!platform_security_state_debug_disabled())) {
		cpu_index_t cpu = cpulocal_get_index();

		etm_unlock_percpu(cpu);
		etm_os_lock_percpu(cpu);

		etm_save_context_percpu(cpu);
	}

	return OK;
}

void
etm_unwind_power_cpu_suspend(bool may_poweroff)
{
	if (may_poweroff &&
	    compiler_unexpected(!platform_security_state_debug_disabled())) {
		cpu_index_t cpu = cpulocal_get_index();
		etm_os_unlock_percpu(cpu);

#if ETM_USE_SOFTWARE_LOCK
#error Restore software lock from before suspend (don't lock unconditionally)
#endif
	}
}

void
etm_handle_power_cpu_resume(bool was_poweroff)
{
	if (compiler_expected(platform_security_state_debug_disabled())) {
		goto out;
	}

	cpu_index_t cpu = cpulocal_get_index();

	if (was_poweroff) {
		etm_unlock_percpu(cpu);

		// check lock os los with trcoslsr.oslk == 1
		ETM_TRCOSLSR_t trcoslsr =
			atomic_load_relaxed(&mapped_etms[cpu]->trcoslsr);
		if (!ETM_TRCOSLSR_get_oslk(&trcoslsr)) {
			LOG(ERROR, WARN, "etm: os is not locked");
			etm_os_lock_percpu(cpu);
		}

		etm_restore_context_percpu(cpu);
	}

	etm_os_unlock_percpu(cpu);

#if ETM_USE_SOFTWARE_LOCK
#error Restore software lock from before suspend (don't lock unconditionally)
#endif

out:
	return;
}
