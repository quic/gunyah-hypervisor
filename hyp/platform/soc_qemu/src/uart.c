// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <hyp_aspace.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <preempt.h>

#include "event_handlers.h"
#include "uart.h"

static soc_qemu_uart_t *uart;

static void
uart_putc(const char c)
{
	while ((atomic_load_relaxed(&uart->tfr) & ((uint32_t)1U << 5)) != 0U)
		;

	atomic_store_relaxed(&uart->dr, c);
}

static char *banner = "[HYP] ";

static void
uart_write(const char *out, size_t size)
{
	size_t	    remain = size;
	const char *pos	   = out;

	for (size_t i = 0; i < strlen(banner); i++) {
		uart_putc(banner[i]);
	}

	while (remain > 0) {
		char c;

		if (*pos == '\n') {
			c = '\r';
			uart_putc(c);
		}

		c = *pos;
		uart_putc(c);
		pos++;
		remain--;
	}

	uart_putc('\n');
}

void
soc_qemu_console_puts(const char *msg)
{
	preempt_disable();
	if (uart != NULL) {
		uart_write(msg, strlen(msg));
	}
	preempt_enable();
}

void
soc_qemu_handle_log_message(trace_id_t id, const char *str)
{
#if defined(VERBOSE) && VERBOSE
	(void)id;

	soc_qemu_console_puts(str);
#else
	if ((id == TRACE_ID_WARN) || (id == TRACE_ID_PANIC) ||
	    (id == TRACE_ID_ASSERT_FAILED) ||
#if defined(INTERFACE_TESTS)
	    (id == TRACE_ID_TEST) ||
#endif
	    (id == TRACE_ID_DEBUG)) {
		soc_qemu_console_puts(str);
	}
#endif
}

void
soc_qemu_uart_init(void)
{
	virt_range_result_t range = hyp_aspace_allocate(PLATFORM_UART_SIZE);
	if (range.e != OK) {
		panic("uart: Address allocation failed.");
	}

	pgtable_hyp_start();

	// Map UART
	uart	    = (soc_qemu_uart_t *)range.r.base;
	error_t ret = pgtable_hyp_map(partition_get_private(), (uintptr_t)uart,
				      PLATFORM_UART_SIZE, PLATFORM_UART_BASE,
				      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
				      PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("uart: Mapping failed.");
	}

	pgtable_hyp_commit();
}
