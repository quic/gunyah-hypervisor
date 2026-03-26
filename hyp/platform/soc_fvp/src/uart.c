// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <qcbor.h>
#include <spinlock.h>

#include "event_handlers.h"
#include "uart.h"

static fvp_uart_t *uart;
static spinlock_t  uart_lock;

static void
uart_putc(const char c)
{
	while ((atomic_load_relaxed(&uart->fr) & ((uint32_t)1U << 5)) != 0U) {
	}

	atomic_store_relaxed(&uart->dr, c);
}

static char *banner = "[HYP] ";

static void
uart_write(const char *out, size_t size)
{
	size_t	       remain = size;
	static rsize_t blen   = (rsize_t)-1U;
	const char    *pos    = out;

	if (blen == (rsize_t)-1U) {
		blen = util_strnlen(banner, 16U);
	}
	for (size_t i = 0; i < blen; i++) {
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

	uart_putc('\r');
	uart_putc('\n');
}

void
soc_fvp_console_puts(const char *msg)
{
	spinlock_acquire(&uart_lock);
	if (uart != NULL) {
		uart_write(msg, util_strnlen(msg, 256U));
	}
	spinlock_release(&uart_lock);
}

void
soc_fvp_handle_log_message(trace_id_t id, const char *str)
{
#if defined(VERBOSE) && VERBOSE
	(void)id;

	soc_fvp_console_puts(str);
#else
	if ((id == TRACE_ID_WARN) || (id == TRACE_ID_PANIC) ||
	    (id == TRACE_ID_ASSERT_FAILED) ||
#if defined(INTERFACE_TESTS)
	    (id == TRACE_ID_TEST) ||
#endif
	    (id == TRACE_ID_DEBUG)) {
		soc_fvp_console_puts(str);
	}
#endif
}

void
soc_fvp_uart_init(void)
{
	spinlock_init(&uart_lock);

	// Setup UART for HYP to use and UART for PVM/Linux EarlyPrintk to use

	virt_range_result_t range = hyp_aspace_allocate(PLATFORM_HYP_UART_SIZE);
	if (range.e != OK) {
		panic("uart: HYP Address allocation failed.");
	}

	fvp_uart_t	   *pvm_uart;
	virt_range_result_t pvm_range =
		hyp_aspace_allocate(PLATFORM_PVM_UART_SIZE);
	if (pvm_range.e != OK) {
		panic("uart: PVM Address allocation failed.");
	}

	pgtable_hyp_start();

	// Map UART for HYP use
	uart	    = (fvp_uart_t *)range.r.base;
	error_t ret = pgtable_hyp_map(partition_get_private(), (uintptr_t)uart,
				      PLATFORM_HYP_UART_SIZE,
				      PLATFORM_HYP_UART_BASE,
				      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
				      PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("uart: HYP Mapping failed.");
	}

	pgtable_hyp_commit();

	// Set the Baudrate divider
	atomic_store_relaxed(&uart->ibrd, 0x4);
	// Set 8N1 and enable fifo
	atomic_store_relaxed(&uart->lcrh, 0x70);
	// Tx on fifo half full
	atomic_store_relaxed(&uart->ifls, 0x2);
	// Enable Uart, TX and Request to send
	atomic_store_relaxed(&uart->cr, 0x901);

	// Initialize UART for PVM/HLOS use.
	// Needs to be configured by bootloader to enable earlyprintk/earlycom
	// support Hyp does not use this one, just configure it for PVM

	pgtable_hyp_start();

	// Map UART, so HYP can configure for PVM early use
	pvm_uart = (fvp_uart_t *)pvm_range.r.base;
	ret = pgtable_hyp_map(partition_get_private(), (uintptr_t)pvm_uart,
			      PLATFORM_PVM_UART_SIZE, PLATFORM_PVM_UART_BASE,
			      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
			      PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("uart: PVM Mapping failed.");
	}

	pgtable_hyp_commit();

	// Set the Baudrate divider
	atomic_store_relaxed(&pvm_uart->ibrd, 0x4);
	// Set 8N1 and enable fifo
	atomic_store_relaxed(&pvm_uart->lcrh, 0x70);
	// Tx on fifo half full
	atomic_store_relaxed(&pvm_uart->ifls, 0x2);
	// Enable Uart, TX and Request to send
	atomic_store_relaxed(&pvm_uart->cr, 0x901);
}
