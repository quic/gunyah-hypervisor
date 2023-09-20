/*
 * © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define __ASSEMBLER__
#include <hypconstants.h>

#if ((PLATFORM_LMA_BASE & ((1 << 21) - 1)) != 0)
#error PLATFORM_LMA_BASE is not aligned to 2MB
#endif

EXTERN(__entry_el2)
ENTRY(__entry_el2_phys)

PHDRS
{
	text PT_LOAD;
	rodata PT_LOAD;
	dynreloc PT_LOAD;
	dynamic PT_DYNAMIC;
	data PT_LOAD;
	tls PT_TLS;
}


SECTIONS
{
	__entry_el2_phys = ABSOLUTE(__entry_el2 + PLATFORM_LMA_BASE);
	. = 0x0;
	image_virt_start = ADDR(.text);
	image_phys_start = LOADADDR(.text);
	.text : AT (PLATFORM_LMA_BASE) {
		*(.text.boot)
		*(.text.boot.*)
		*(SORT_BY_ALIGNMENT(.text .text.*))
		KEEP(*(.text.debug));
	} : text

	. = ALIGN(4096);
	rodata_base = .;
	.rodata : {
		*(SORT_BY_ALIGNMENT(.rodata .rodata.*))
	} : rodata

	. = ALIGN(8);
	.rela.dyn : { *(.rela.dyn) }

	. = ALIGN(8);
	.gnu.hash : { *(.gnu.hash) }

	. = ALIGN(8);
	.note : { *(.note) *(.note.*) }

	. = ALIGN(4096);
	.dynsym : { *(.dynsym) }
	. = ALIGN(8);
	.dynstr : { *(.dynstr) }

	. = ALIGN(4096);
	.dynamic : { *(.dynamic) } : dynamic : dynreloc

	/* Package data for RootVM */
	. = ALIGN(4096);
#ifdef PLATFORM_ROOTVM_PKG_START_BASE
	image_pkg_start = PLATFORM_ROOTVM_PKG_START_BASE;
#else
	image_pkg_start = . - image_virt_start + image_phys_start;
#endif

	/* align RW sections to the next 2MB page */
	. = ALIGN(0x200000);
	data_base = .;
	.data : {
		*(SORT_BY_ALIGNMENT(.data .data.*))
	} : data

	. = ALIGN(8);
	bss_base = .;
	.bss (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(.bss .bss.*))
		*(COMMON)
	}

#if defined(TRACE_BOOT_ENTRIES)
	. = ALIGN(64);
	.heap.trace (NOLOAD) : {
		trace_boot_buffer = .;
		. = . + (TRACE_BUFFER_ENTRY_SIZE * TRACE_BOOT_ENTRIES);
	} : NONE
#endif

	. = ALIGN(64);
	. = ALIGN(MEMDB_MIN_SIZE);
#if PLATFORM_RW_DATA_SIZE < 0x200000
#error PLATFORM_RW_DATA_SIZE too small
#endif
	.heap.root (NOLOAD) : {
		heap_private_start = .;
		. = data_base + PLATFORM_RW_DATA_SIZE;
		heap_private_end = .;
	} : NONE
	image_virt_last = . - 1;
	image_phys_last = . - 1 - image_virt_start + image_phys_start;
	image_virt_end = .;

	/* Thread local storage - note, we do not support tdata! */
	. = 0;

	/* Force a link error if tdata present */
	.tdata : { *(.tdata) *(.tdata.*) } : tls
	ASSERT(. == 0, ".tdata: initialized tls is not supported")

	.tbss : {
		*(.tbss.current_thread)
		*(SORT_BY_ALIGNMENT(.tbss .tbss.*))
	} : tls
	tbss_size = SIZEOF(.tbss);
	tbss_align = ALIGNOF(.tbss);
}
