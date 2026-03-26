// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#include <hypconstants.h>

#define register_t std_register_t
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <libgen.h>
#include <sys/queue.h>
#include <unistd.h>
#undef register_t

#include "pgtable.h"
#include "stub.h"
#include "util.h"

// TODO:
// * use assert for test failure checking.
//
// * add test case for memory type combine API function.
//
// * check if it can work under unitest environment.

// FIXME: duplicate from pgtable.c, since need to keep g4k_levels as static for
// namespace consideration

// mask for [e, s]
#define segment_mask(e, s) (util_mask(e + 1) & (~util_mask(s)))

#define PGTABLE_LEVEL_NUM (PGTABLE_LEVEL__MAX + 1)

static bool dump_table = false;

typedef struct clean_args {
	bool failed;
} clean_args_t;

typedef struct table_entry_check_args {
	bool failed;
} table_entry_args_t;

typedef struct contiguous_bit_check_args {
	bool failed;
	bool cont;
} contiguous_bit_check_args_t;

// FIXME: duplicated from pgtable.c
static const pgtable_level_info_t g4k_levels[] = {
	// level 0
	{ .msb				      = 47,
	  .lsb				      = 39,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = 0UL,
	  .is_offset			      = false,
	  .allowed_types		      = pgtable_entry_types_cast(
		  PGTABLE_ENTRY_TYPES_NEXT_LEVEL_TABLE_MASK),
	  .addr_size		= 1UL << 39,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_0,
	  .contiguous_entry_cnt = 0UL },
	// level 1
	{ .msb				      = 38,
	  .lsb				      = 30,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = segment_mask(47, 30),
	  .is_offset			      = false,
	  .allowed_types		      = pgtable_entry_types_cast(
		  PGTABLE_ENTRY_TYPES_NEXT_LEVEL_TABLE_MASK |
		  PGTABLE_ENTRY_TYPES_BLOCK_MASK),
	  .addr_size		= 1UL << 30,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_1,
	  .contiguous_entry_cnt = 16UL },
	// level 2
	{ .msb				      = 29,
	  .lsb				      = 21,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = segment_mask(47, 21),
	  .is_offset			      = false,
	  .allowed_types		      = pgtable_entry_types_cast(
		  PGTABLE_ENTRY_TYPES_NEXT_LEVEL_TABLE_MASK |
		  PGTABLE_ENTRY_TYPES_BLOCK_MASK),
	  .addr_size		= 1UL << 21,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_2,
	  .contiguous_entry_cnt = 16UL },
	// level 3
	{ .msb				      = 20,
	  .lsb				      = 12,
	  .table_mask			      = 0UL,
	  .block_and_page_output_address_mask = segment_mask(47, 12),
	  .is_offset			      = false,
	  .allowed_types =
		  pgtable_entry_types_cast(PGTABLE_ENTRY_TYPES_PAGE_MASK),
	  .addr_size		= 1UL << 12,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_3,
	  .contiguous_entry_cnt = 16UL },
	// offset
	{ .msb				      = 11,
	  .lsb				      = 0,
	  .table_mask			      = 0UL,
	  .block_and_page_output_address_mask = 0UL,
	  .is_offset			      = true,
	  .allowed_types		      = pgtable_entry_types_cast(0U),
	  .addr_size			      = 0UL,
	  .entry_cnt			      = 0UL,
	  .level			      = PGTABLE_LEVEL_OFFSET,
	  .contiguous_entry_cnt		      = 0UL }
};

#ifndef NDEBUG
// just for debug
extern void
pgtable_hyp_dump(void);

extern void
pgtable_vm_dump(pgtable_vm_t *pgtable);

// NOTE: either hide the types with void or just copy definition here.
// Agree external modifier for debug? Better ideas?
typedef struct stack_elem {
	paddr_t		    pa;
	vmsa_level_table_t *table;
	bool		    mapped;
	bool		    need_unmap;
	char		    padding[6];
} stack_elem_t;

typedef pgtable_modifier_ret_t (*ext_func_t)(
	pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	index_t level, pgtable_entry_types_t type,
	stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data, index_t *next_level,
	vmaddr_t *next_virtual_address, size_t *next_size, paddr_t *next_table);

extern void
pgtable_hyp_ext(vmaddr_t virtual_address, size_t size,
		pgtable_entry_types_t entry_types, ext_func_t func, void *data);

extern void
pgtable_vm_ext(pgtable_vm_t *pgtable, vmaddr_t virtual_address, size_t size,
	       pgtable_entry_types_t entry_types, ext_func_t func, void *data);
#endif

extern void
pgtable_handle_boot_cold_init(void);

// access internal function
extern vmsa_entry_t
get_entry(vmsa_level_table_t *table, index_t idx);

extern vmsa_upper_attrs_t
get_upper_attr(vmsa_entry_t entry);

extern void
pgtable_hyp_destroy(partition_t *partition);

count_t
get_table_refcount(vmsa_level_table_t *table, index_t idx);

// hyp test helper, will make sure the VA region is mapped with specified PA.
static bool
hyp_check_mapped(paddr_t pa, uintptr_t virtual_address, size_t size);

// make sure only invalid entry in the specified region.
static bool
hyp_check_cleaned(paddr_t pa, uintptr_t virtual_address, size_t size);

static bool
hyp_check_table_entry(uintptr_t virtual_address, size_t size);

// hyp test function
static bool
hyp_test_single_page_map(uintptr_t virtual_address);

static bool
hyp_test_page_aligned_map(uintptr_t virtual_address, size_t blk_size);

static bool
hyp_test_single_block_map(uintptr_t virtual_address, size_t blk_size);

static bool
hyp_test_partial_unmap(uintptr_t virtual_address, size_t blk_size,
		       uintptr_t unmap_address_offset, size_t unmap_size);

static bool
hyp_test_preserve_page_level(uintptr_t virtual_address, size_t size);

static bool
hyp_test_multiple_page(uintptr_t virtual_address, size_t size);

static bool
hyp_test_contiguous_group_map(uintptr_t virtual_address, index_t level);

// vm test function
static bool
vm_test_single_page_map(uintptr_t virtual_address, bool try_map,
			bool allow_merge);

static bool
vm_test_single_block_map(uintptr_t virtual_address, size_t blk_size,
			 bool try_map, bool allow_merge);

static bool
vm_test_overlapped_map(uintptr_t virtual_address, size_t size, size_t alignment,
		       uintptr_t overlapped_virtual_address,
		       size_t overlapped_size, bool try_map, bool allow_merge);

static bool
vm_test_change_access(uintptr_t virtual_address, size_t size, size_t alignment,
		      bool try_map, bool allow_merge);

void
help(char *app_name)
{
	printf("Usage: %s [OPTION]...\n", basename(app_name));
	printf("Run the page table API test case from host development PC\n\n");

	return;
}

bool
hyp_check_mapped(paddr_t phy_addr, uintptr_t virt_addr, size_t size)
{
	size_t		      cur_size	      = size;
	paddr_t		      pa	      = phy_addr, mapped_base;
	uintptr_t	      virtual_address = virt_addr;
	bool		      l_ret = false, ret = false;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;

	while (cur_size > 0) {
		l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base,
					   &mapped_size, &mapped_memtype,
					   &mapped_access);
		if (l_ret && (cur_size >= mapped_size)) {
			cur_size -= mapped_size;
		} else {
			break;
		}

		if (mapped_base != pa) {
			break;
		}

		// FIXME: need to check if they shares the same mapped_memtype
		// and the access

		virtual_address += mapped_size;
		pa += mapped_size;
	}

	if (cur_size == 0) {
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}

// Only guarantee there's no page/block mapped
pgtable_modifier_ret_t
check_clean(pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	    index_t level, pgtable_entry_types_t type,
	    stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	    index_t *next_level, vmaddr_t *next_virtual_address,
	    size_t *next_size, paddr_t *next_table)
{
	clean_args_t	      *args = (clean_args_t *)data;
	pgtable_modifier_ret_t ret  = PGTABLE_MODIFIER_RET_CONTINUE;

	if (pgtable_entry_types_get_invalid(&type)) {
		args->failed = false;
		ret	     = PGTABLE_MODIFIER_RET_CONTINUE;
	} else if (pgtable_entry_types_get_next_level_table(&type)) {
		// let it to continue step into next level
		ret = PGTABLE_MODIFIER_RET_CONTINUE;
	} else {
		args->failed = true;
		ret	     = PGTABLE_MODIFIER_RET_STOP;
	}

	return ret;
}

bool
hyp_check_cleaned(paddr_t pa, uintptr_t virtual_address, size_t size)
{
	clean_args_t	      args;
	pgtable_entry_types_t types =
		pgtable_entry_types_inverse(pgtable_entry_types_default());

	args.failed = false;

	(void)pa;
	(void)pgtable_hyp_ext(virtual_address, size, types, check_clean, &args);

	return !args.failed;
}

// Iterate the table entry with 0 valid enties
pgtable_modifier_ret_t
count_table_entry(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		  index_t idx, index_t level, pgtable_entry_types_t type,
		  stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		  index_t *next_level, vmaddr_t *next_virtual_address,
		  size_t *next_size, paddr_t *next_table)
{
	table_entry_args_t	   *args = (table_entry_args_t *)data;
	vmsa_entry_t		    entry;
	const pgtable_level_info_t *next_level_info = NULL;
	vmsa_level_table_t	   *table;
	count_t			    entry_cnt = 0;
	pgtable_modifier_ret_t	    ret	      = PGTABLE_MODIFIER_RET_CONTINUE;

	// check current table's entry count, if it's 0, then accumulate the
	// address size of current level
	assert(stack[level].mapped);
	table		= stack[level].table;
	next_level_info = &g4k_levels[level + 1];
	entry		= get_entry(table, idx);
	entry_cnt	= get_table_refcount(table, idx);

	// the rule for preserve is: if the current block size is larger than
	// the preserved size, then next table level is needed. Else, it steps
	// out to the next entry without allocate new table level.

	if (entry_cnt == 0) {
		if ((size < next_level_info->addr_size) &&
		    pgtable_entry_types_get_next_level_table(
			    &next_level_info->allowed_types)) {
			args->failed = true;
			ret	     = PGTABLE_MODIFIER_RET_STOP;
		} else {
			args->failed = false;
			ret	     = PGTABLE_MODIFIER_RET_CONTINUE;
		}
	}

	return ret;
}

static bool
hyp_check_table_entry(uintptr_t virtual_address, size_t size)
{
	table_entry_args_t    args;
	pgtable_entry_types_t types = pgtable_entry_types_default();

	pgtable_entry_types_set_next_level_table(&types, true);

	args.failed = true;
	(void)pgtable_hyp_ext(virtual_address, size, types, count_table_entry,
			      &args);

	return !args.failed;
}

static pgtable_modifier_ret_t
check_contiguous_bit(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		     index_t idx, index_t level, pgtable_entry_types_t type,
		     stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		     index_t *next_level, vmaddr_t *next_virtual_address,
		     size_t *next_size, paddr_t *next_table)
{
	contiguous_bit_check_args_t *args = (contiguous_bit_check_args_t *)data;
	pgtable_modifier_ret_t	     ret  = PGTABLE_MODIFIER_RET_CONTINUE;

	if (pgtable_entry_types_get_page(&type) ||
	    pgtable_entry_types_get_block(&type)) {
		assert(stack[level].mapped);

		vmsa_entry_t	   entry = get_entry(stack[level].table, idx);
		vmsa_upper_attrs_t upper_attrs = get_upper_attr(entry);
		vmsa_common_upper_attrs_t upper_attrs_bitfield =
			vmsa_common_upper_attrs_cast(upper_attrs);

		if (vmsa_common_upper_attrs_get_cont(&upper_attrs_bitfield) ==
		    args->cont) {
			args->failed = false;
			ret	     = PGTABLE_MODIFIER_RET_CONTINUE;
		} else {
			args->failed = true;
			ret	     = PGTABLE_MODIFIER_RET_STOP;
		}
	} else if (pgtable_entry_types_get_next_level_table(&type)) {
		// let it to continue step into next level
		ret = PGTABLE_MODIFIER_RET_CONTINUE;
	} else {
		args->failed = true;
		ret	     = PGTABLE_MODIFIER_RET_STOP;
	}

	return ret;
}

static bool
hyp_check_contiguous_bit(uintptr_t virtual_address, size_t size, bool cont)
{
	contiguous_bit_check_args_t args;
	pgtable_entry_types_t	    types =
		pgtable_entry_types_inverse(pgtable_entry_types_default());

	args.failed = true;
	args.cont   = cont;

	(void)pgtable_hyp_ext(virtual_address, size, types,
			      check_contiguous_bit, &args);

	return !args.failed;
}

void
test_memallocator()
{
	paddr_t		  paddr;
	partition_t	  partition;
	void_ptr_result_t ret;

	ret   = partition_alloc(&partition, 4096, 4096);
	paddr = partition_virt_to_phys(&partition, (uintptr_t)ret.r);
	printf("got vaddr(%p) paddr(%lx)\n", ret.r, paddr);
	partition_free(&partition, ret.r, 4096);

	ret   = partition_alloc(&partition, 4096, 4096);
	paddr = partition_virt_to_phys(&partition, (uintptr_t)ret.r);
	printf("got vaddr(%p) paddr(%lx)\n", ret.r, paddr);
	partition_free(&partition, ret.r, 4096);

	ret   = partition_alloc(&partition, 4096, 4096);
	paddr = partition_virt_to_phys(&partition, (uintptr_t)ret.r);
	printf("got vaddr(%p) paddr(%lx)\n", ret.r, paddr);
	partition_free(&partition, ret.r, 4096);

	ret   = partition_alloc(&partition, 4096, 4096);
	paddr = partition_virt_to_phys(&partition, (uintptr_t)ret.r);
	printf("got vaddr(%p) paddr(%lx)\n", ret.r, paddr);
	partition_free(&partition, ret.r, 4096);
}

void
hyp_test_pgtable()
{
	bool	     ret;
	const size_t page_size	  = 4096;
	const size_t blk_2mb_size = 1L << 21;
	const size_t blk_1g_size  = 1L << 30;

	printf("\n\n================ HYP API TEST ================\n");

	// map a page to [0x245000, 0x246000)
	ret = hyp_test_single_page_map(0x245000);
	if (!ret) {
		printf("Failed for test single page map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// map 2M to [0x30000000, 0x30200000)
	ret = hyp_test_page_aligned_map(0x30000000, 0x200000);
	if (!ret) {
		printf("Failed for test page aligned map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 2MB physical address to [0x30000000, 0x30200000)
	ret = hyp_test_single_block_map(0x30000000, blk_2mb_size);
	if (!ret) {
		printf("Failed for test single 2 Mega block map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 2MB physical address to [0x30000000, 0x30200000)
	// and then unmap 40k from [0x30100000, 0x3010a000)
	ret = hyp_test_partial_unmap(0x30000000, blk_2mb_size, 0x100000,
				     0xa000);
	if (!ret) {
		printf("Failed for test partial unmap test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 8MB physical address to [0x50000000, 0x50800000)
	// and then unmap 40k from [0x50300000, 0x30500000)
	ret = hyp_test_partial_unmap(0x50000000, 4 * blk_2mb_size, 0x300000,
				     0x200000);
	if (!ret) {
		printf("Failed for test partial unmap test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 1GB physical address to [0x40000000, 0x80000000)
	ret = hyp_test_single_block_map(0x40000000, blk_1g_size);
	if (!ret) {
		printf("Failed for test single 1 G block map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// preserve one page level
	ret = hyp_test_preserve_page_level(0x35200000, blk_2mb_size);
	if (!ret) {
		printf("Failed to preserve page level\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// preserve several page level
	ret = hyp_test_preserve_page_level(0x13e000000, 16 * blk_2mb_size);
	if (!ret) {
		printf("Failed to preserve page level\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// map two pages at lowest level
	// two pages at [13FFFF000] and [140000000]
	// This test has one entry on the end of one page table level, and
	// another on the start of one page table level. looks like:
	//           +----+---+---+-----------+
	//           |    | E | E |           |
	//           +----+-+-+-+-+-----------+
	//                  |   |
	//                  |   |
	//             +----+   +----+
	//             |             |
	//             v             v
	// +----------++-+         +-+-+----------+
	// |          | E|         | E |          |
	// +----------+-++         +-+-+----------+
	//              |            |
	//              v            v
	// +-----------++-+         ++--+---------+
	// |           | E|         | E |         |
	// +-----------++-+         +-+-+---------+
	//              |             |
	//              |             |
	//              v             v
	//     +--------+---+       +-+----------+
	//     | PAGE       |       | PAGE       |
	//     +------------+       +------------+
	ret = hyp_test_multiple_page(0x13FFFF000, page_size * 2);
	if (!ret) {
		printf("Failed to map multiple page level\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}

	// try to map 16 4KB pages to [0x12340000U, 0x12350000U) with cont bit
	// set and then unmap one or more pages in it, the cont bit should be
	// cleared automatically.
	ret = hyp_test_contiguous_group_map(0x12340000U, 3U);
	if (!ret) {
		printf("Failed for test contiguous group test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map 16 2MB blocks to [0x12000000U, 0x14000000U) with cont bit
	// set and then unmap one or more pages in it, the cont bit should be
	// cleared automatically.
	ret = hyp_test_contiguous_group_map(0x12000000U, 2U);
	if (!ret) {
		printf("Failed for test contiguous group test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

out:
	reset_pmem_allocation();
	return;
}

void
vm_test_pgtable()
{
	bool	     ret;
	const size_t page_4k_size = 1L << 12;
	const size_t blk_2mb_size = 1L << 21;
	const size_t blk_1g_size  = 1L << 30;

	printf("\n\n================ VM API TEST ================\n");

	// map a page to [0x245000, 0x246000)
	ret = vm_test_single_page_map(0x245000, true, false);
	if (!ret) {
		printf("Failed for test single page map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 2MB physical address to [0x30000000, 0x30200000)
	ret = vm_test_single_block_map(0x30000000, blk_2mb_size, true, false);
	if (!ret) {
		printf("Failed for test single 2 Mega block map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map a 1GB physical address to [0x40000000, 0x80000000)
	ret = vm_test_single_block_map(0x40000000, blk_1g_size, true, false);
	if (!ret) {
		printf("Failed for test single 1 G block map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map one block entry at [0x40000000, 0x40200000), then an
	// overlapped range at [0x3ff00000, 0x40100000)
	//
	// This test is successful if the mapping of the overlapping range
	// fails, since we only map if there are no existing mappings.
	// (try_map is true)
	ret = vm_test_overlapped_map(0x40000000, blk_2mb_size, blk_2mb_size,
				     0x3ff00000, blk_2mb_size, true, false);
	if (!ret) {
		printf("Failed for partially map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// try to map one block entry at [0x40000000, 0x40200000), then an
	// overlapped range at [0x3ff00000, 0x40100000)
	//
	// This test is successful if the mapping of the overlapping range
	// fails, since we only map if there are no existing mappings.
	// (try_map is true)
	ret = vm_test_overlapped_map(0x40000000,
				     blk_2mb_size + 10 * page_4k_size,
				     blk_2mb_size, 0x40100000, blk_2mb_size,
				     true, false);
	if (!ret) {
		printf("Failed for partially map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// map one block entry at [0x40000000, 0x40200000), then an overlapped
	// range at [0x3ff00000, 0x40100000)
	//
	// This test is successful if:
	// - The overlapping range is mapped, since we map regardless of a
	// mapping already existing in the range. (try_map is false)
	// - Unmapping the first mapped range [0x40000000, 0x40200000) only
	// unmaps [0x40100000, 0x40200000) since the range [0x40000000,
	// 0x40100000) is now owned by the second mapping done.
	ret = vm_test_overlapped_map(0x40000000, blk_2mb_size, blk_2mb_size,
				     0x3ff00000, blk_2mb_size, false, false);
	if (!ret) {
		printf("Failed for partially map test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}
	reset_pmem_allocation();

	// map one block entry at [0x40000000, 0x40200000), then change access
	// values
	//
	// This test is successful if:
	// - The range is mapped and then the access of that mapping is changed
	// without remapping. We verify that the access has changed by doing a
	// pgtable lookup and checking that access values are updated.
	ret = vm_test_change_access(0x40000000, blk_2mb_size, blk_2mb_size,
				    false, false);
	if (!ret) {
		printf("Failed for change access of mapping test\n");
		goto out;
	}
	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		goto out;
	}

out:
	reset_pmem_allocation();
	return;
}

int
main(int argc, char *argv[])
{
	int opt;
	int test_memory_allocator = 0;

	while ((opt = getopt(argc, argv, "s:f:hm")) != -1) {
		switch (opt) {
		case 'h':
			help(argv[0]);
			break;
		case 'm':
			test_memory_allocator = 1;
			break;
		default:
			help(argv[0]);
			exit(-1);
		}
	}

	init();

	if (test_memory_allocator != 0) {
		test_memallocator();
		return 0;
	}

	hyp_test_pgtable();

	vm_test_pgtable();

	// clean all physical memory allocated
	cleanup();

	return 0;
}

bool
hyp_test_single_page_map(uintptr_t virtual_address)
{
	partition_t	      partition_normal;
	partition_t	     *partition = &partition_normal;
	size_t		      page_size = 4096;
	paddr_t		      mapped_base, p;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;
	bool		      l_ret;
	bool		      f_ret	 = true;
	void		     *tested_mem = NULL;
	void_ptr_result_t     ret;

	printf("===> begin test single page mapping:\n");

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a page
	ret = partition_alloc(partition, page_size, page_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical page for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_hyp_map(partition, virtual_address, page_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if ((!l_ret) || (mapped_base != p) || (mapped_size != page_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

	pgtable_hyp_unmap(partition, virtual_address, page_size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	partition_free(partition, tested_mem, page_size);

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

	pgtable_hyp_destroy(partition);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
	}
#endif

	printf("^^^^ test single page mapping done\n");
out:
	return f_ret;
}

bool
hyp_test_page_aligned_map(uintptr_t virtual_address, size_t blk_size)
{
	const size_t	      page_size = 4096;
	partition_t	      partition_normal;
	partition_t	     *partition = &partition_normal;
	paddr_t		      mapped_base, p;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;
	bool		      l_ret;
	bool		      f_ret	 = true;
	void		     *tested_mem = NULL;
	void_ptr_result_t     ret;
	size_t		      allocated_size = blk_size + page_size;

	printf("===> begin test page aligned mapping: virtual_address(%lx)"
	       " blk_size(%ld)\n",
	       virtual_address, blk_size);

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a block, page aligned
	ret = partition_alloc(partition, allocated_size, page_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	// only need page aligned
	if (((p & (page_size - 1)) == 0) && ((p & (page_size * 2 - 1)) == 0)) {
		p += page_size;
	}

	pgtable_hyp_map(partition, virtual_address, blk_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

#ifndef NDEBUG
	if (dump_table) {
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	// should only find a page
	if ((!l_ret) || (mapped_base != p) || (mapped_size != page_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	pgtable_hyp_unmap(partition, virtual_address, blk_size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);
	partition_free(partition, tested_mem, allocated_size);

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	pgtable_hyp_destroy(partition);

	printf("^^^^ test single block mapping done\n");
out:
	return f_ret;
}

bool
hyp_test_single_block_map(uintptr_t virtual_address, size_t blk_size)
{
	partition_t	      partition_normal;
	partition_t	     *partition = &partition_normal;
	paddr_t		      mapped_base, p;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;
	bool		      l_ret;
	bool		      f_ret	 = true;
	void		     *tested_mem = NULL;
	void_ptr_result_t     ret;

	printf("===> begin test single block mapping: virtual_address(%lx) blk_size(%ld)\n",
	       virtual_address, blk_size);

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a page
	ret = partition_alloc(partition, blk_size, blk_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_hyp_map(partition, virtual_address, blk_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if ((!l_ret) || (mapped_base != p) || (mapped_size != blk_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	pgtable_hyp_unmap(partition, virtual_address, blk_size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);
	partition_free(partition, tested_mem, blk_size);

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	pgtable_hyp_destroy(partition);

	printf("^^^^ test single block mapping done\n");
out:
	return f_ret;
}

bool
hyp_test_partial_unmap(uintptr_t virtual_address, size_t blk_size,
		       uintptr_t unmap_address_offset, size_t unmap_size)
{
	partition_t	      partition_normal;
	partition_t	     *partition = &partition_normal;
	size_t		      page_size = 4096;
	paddr_t		      mapped_base, p;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;
	bool		      l_ret;
	bool		      f_ret	 = true;
	void		     *tested_mem = NULL;
	void_ptr_result_t     ret;

	printf("===> begin test partial unmapping: virtual_address(%lx) "
	       "blk_size(%ld), unmap_address_offset(%ld) unmap_size(%ld)\n",
	       virtual_address, blk_size, unmap_address_offset, unmap_size);

	assert(blk_size >= unmap_size + unmap_address_offset);
	// make sure unmap parameters are page aligned
	assert((unmap_address_offset & (page_size - 1)) == 0);
	assert((unmap_size & (page_size - 1)) == 0);

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a page
	ret = partition_alloc(partition, blk_size, blk_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_hyp_map(partition, virtual_address, blk_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if ((!l_ret) || (mapped_base != p)) {
		printf("Failed to map for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	// do partially unmap
	pgtable_hyp_unmap(partition, virtual_address + unmap_address_offset,
			  unmap_size, PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	// check if it's partially unmapped
	l_ret = pgtable_hyp_lookup(virtual_address + unmap_address_offset,
				   &mapped_base, &mapped_size, &mapped_memtype,
				   &mapped_access);
	if (l_ret) {
		printf("Failed to partially unmap %lx (still exist)\n",
		       virtual_address + unmap_address_offset);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n",
		       virtual_address + unmap_address_offset);
	}

	// check head
	if (unmap_address_offset != 0) {
		l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base,
					   &mapped_size, &mapped_memtype,
					   &mapped_access);
		if (l_ret && mapped_base == p) {
			printf("Successfully find head %lx.\n",
			       virtual_address);
		} else {
			printf("%lx (head does not exist)\n", virtual_address);
			f_ret = false;
			goto out;
		}
	}

	// check tail
	if (blk_size > unmap_address_offset + unmap_size) {
		l_ret = pgtable_hyp_lookup(
			virtual_address + unmap_address_offset + unmap_size,
			&mapped_base, &mapped_size, &mapped_memtype,
			&mapped_access);
		if (l_ret &&
		    mapped_base == p + unmap_address_offset + unmap_size) {
			printf("Successfully find tail %lx\n",
			       virtual_address + unmap_address_offset +
				       unmap_size);
		} else {
			printf("%lx (tail does not exist)\n",
			       virtual_address + unmap_address_offset +
				       unmap_size);
			f_ret = false;
			goto out;
		}
	}

out:
#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	partition_free(partition, tested_mem, blk_size);

	pgtable_hyp_destroy(partition);

	printf("^^^^ test single block mapping done\n");
	return f_ret;
}

bool
hyp_test_multiple_page(uintptr_t virtual_address, size_t size)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	size_t	     page_size = 4096;
	size_t	rounded_size = ((size + page_size - 1) / page_size) * page_size;
	paddr_t p;
	bool	f_ret	   = true;
	void   *tested_mem = NULL;
	void_ptr_result_t ret;

	printf("===> begin test multiple pages mapping: virtual_address(%lx) size(%zu)\n",
	       virtual_address, size);

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a page
	ret = partition_alloc(partition, rounded_size, page_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical page for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_hyp_map(partition, virtual_address, rounded_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	if (!hyp_check_mapped(p, virtual_address, rounded_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

	pgtable_hyp_unmap(partition, virtual_address, rounded_size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	if (hyp_check_cleaned(p, virtual_address, rounded_size)) {
		printf("Success to unmap %lx.\n", virtual_address);
	} else {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	}

	partition_free(partition, tested_mem, rounded_size);

	pgtable_hyp_destroy(partition);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
	}
#endif

	printf("^^^^ test multiple pages mapping done\n");
out:
	return f_ret;
}

bool
hyp_test_preserve_page_level(uintptr_t virtual_address, size_t size)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	bool	     f_ret     = true;

	printf("===> begin test preserve: virtual_address(%lx) size(%ld)\n",
	       virtual_address, size);

	// init pgtable
	pgtable_handle_boot_cold_init();

	pgtable_hyp_preallocate(partition, virtual_address, size);

	if (!hyp_check_table_entry(virtual_address, size)) {
		printf("Check failure for preserve table entries\n");
		f_ret = false;
		goto out;
	}

	if (dump_table) {
		printf("\nprealloc, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}

	pgtable_hyp_unmap(partition, virtual_address, size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	pgtable_hyp_destroy(partition);

	if (dump_table) {
		printf("\nafter all, page table dump:\n");
		dump_memory_allocation();
	}

	printf("^^^^ test preserve done\n");
out:
	return f_ret;
}

static bool
hyp_test_contiguous_group_map(uintptr_t virtual_address, index_t level)
{
	partition_t		    partition_normal;
	partition_t		   *partition = &partition_normal;
	paddr_t			    p;
	bool			    f_ret      = true;
	void			   *tested_mem = NULL;
	void_ptr_result_t	    ret;
	const pgtable_level_info_t *info = &g4k_levels[level];

	printf("===> begin test contiguous group mapping: virtual_address(%lx) size(%lu * %lu)\n",
	       virtual_address, info->contiguous_entry_cnt, info->addr_size);

	if ((level > 3U) || (g4k_levels[level].contiguous_entry_cnt == 0U)) {
		printf("Invalid level(%d) index\n", level);
		f_ret = false;
		goto out;
	}

	size_t cont_size = info->contiguous_entry_cnt * info->addr_size;

	if (!util_is_baligned(virtual_address, cont_size)) {
		printf("%lx should be aligned with %lx\n", virtual_address,
		       cont_size);
		f_ret = false;
		goto out;
	}

	// init pgtable
	pgtable_handle_boot_cold_init();

	// allocate a page
	ret = partition_alloc(partition, cont_size, cont_size);
	if (ret.e != OK) {
		printf("Failed to allocate contiguous group pages for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_hyp_map(partition, virtual_address, cont_size, p,
			PGTABLE_HYP_MEMTYPE_STRONG, PGTABLE_ACCESS_RW,
			VMSA_SHAREABILITY_INNER_SHAREABLE);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	if (!hyp_check_mapped(p, virtual_address, cont_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

	if (!hyp_check_contiguous_bit(virtual_address, cont_size, true)) {
		printf("Failed to set cont bit for %lx++%lx.\n",
		       virtual_address, cont_size);
		f_ret = false;
		goto out;
	}

	// Better to set unmap_from and unmap_to randomly from 0~15
	const index_t unmap_from = 2U;
	const index_t unmap_to	 = 4U;
	uintptr_t unmap_start = virtual_address + unmap_from * info->addr_size;
	uintptr_t unmap_size  = info->addr_size * (unmap_to - unmap_from + 1U);

	pgtable_hyp_unmap(partition, unmap_start, unmap_size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	if (!hyp_check_cleaned(p, unmap_start, unmap_size)) {
		printf("Failed to unmap %lx++%lx(still exist)\n", unmap_start,
		       unmap_size);
		f_ret = false;
		goto out;
	}

	uintptr_t check_cont_start = virtual_address;
	size_t	  check_cont_size  = info->addr_size * unmap_from;
	if (!hyp_check_contiguous_bit(check_cont_start, check_cont_size,
				      false)) {
		printf("Failed to clear cont bit for %lx++%lx.\n",
		       check_cont_start, check_cont_size);
		f_ret = false;
		goto out;
	}

	check_cont_start = unmap_start + unmap_size;
	check_cont_size	 = cont_size - ((unmap_to + 1U) * info->addr_size);
	if (!hyp_check_contiguous_bit(check_cont_start, check_cont_size,
				      false)) {
		printf("Failed to clear cont bit for %lx++%lx.\n",
		       check_cont_start, check_cont_size);
		f_ret = false;
		goto out;
	}

	partition_free(partition, tested_mem, cont_size);

	pgtable_hyp_destroy(partition);

	printf("^^^^ test contiguous group mapping done\n");
out:
	return f_ret;
}

bool
vm_test_single_page_map(uintptr_t virtual_address, bool try_map,
			bool allow_merge)
{
	partition_t	     partition_normal;
	partition_t	    *partition = &partition_normal;
	size_t		     page_size = 4096;
	paddr_t		     mapped_base, p;
	size_t		     mapped_size;
	pgtable_vm_memtype_t mapped_memtype;
	pgtable_access_t     mapped_kernel_access, mapped_user_access;
	bool		     l_ret;
	pgtable_vm_t	     page_table, *pgtable = &page_table;
	bool		     f_ret	= true;
	void		    *tested_mem = NULL;
	void_ptr_result_t    ret;

	printf("===> begin test single page mapping:\n");
	// init pgtable
	memset(pgtable, 0, sizeof(*pgtable));
	if (pgtable_vm_init(partition, pgtable, 65U) != OK) {
		printf("Failed to create hyp pgtable.\n");
		f_ret = false;
		goto out;
	}

	// allocate a page
	ret = partition_alloc(partition, page_size, page_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical page for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_vm_map(partition, pgtable, virtual_address, page_size, p,
		       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_RW,
		       PGTABLE_ACCESS_RW, try_map, allow_merge);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if ((!l_ret) || (mapped_base != p) || (mapped_size != page_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

	pgtable_vm_unmap(partition, pgtable, virtual_address, page_size);

	partition_free(partition, tested_mem, page_size);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

	pgtable_vm_destroy(partition, pgtable);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
	}
#endif

	printf("^^^^ test single page mapping done\n");
out:
	return f_ret;
}

bool
vm_test_single_block_map(uintptr_t virtual_address, size_t blk_size,
			 bool try_map, bool allow_merge)
{
	partition_t	     partition_normal;
	partition_t	    *partition = &partition_normal;
	paddr_t		     mapped_base, p;
	size_t		     mapped_size;
	pgtable_vm_memtype_t mapped_memtype;
	pgtable_access_t     mapped_kernel_access, mapped_user_access;
	bool		     l_ret;
	pgtable_vm_t	     page_table, *pgtable = &page_table;
	bool		     f_ret	= true;
	void		    *tested_mem = NULL;
	void_ptr_result_t    ret;

	printf("===> begin test single block mapping: virtual_address(%lx) blk_size(%ld)\n",
	       virtual_address, blk_size);
	// init pgtable
	memset(pgtable, 0, sizeof(*pgtable));
	if (pgtable_vm_init(partition, pgtable, 65U) != OK) {
		printf("Failed to create hyp pgtable.\n");
		f_ret = false;
		goto out;
	}

	// allocate a page
	ret = partition_alloc(partition, blk_size, blk_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	pgtable_vm_map(partition, pgtable, virtual_address, blk_size, p,
		       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_RW,
		       PGTABLE_ACCESS_RW, try_map, allow_merge);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if ((!l_ret) || (mapped_base != p) || (mapped_size != blk_size)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	pgtable_vm_unmap(partition, pgtable, virtual_address, blk_size);
	partition_free(partition, tested_mem, blk_size);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	pgtable_vm_destroy(partition, pgtable);

	printf("^^^^ test single block mapping done\n");
out:
	return f_ret;
}

bool
vm_test_overlapped_map(uintptr_t virtual_address, size_t size, size_t alignment,
		       uintptr_t overlapped_virtual_address,
		       size_t overlapped_size, bool try_map, bool allow_merge)
{
	partition_t	     partition_normal;
	partition_t	    *partition = &partition_normal;
	paddr_t		     mapped_base, p, p2;
	size_t		     mapped_size;
	pgtable_vm_memtype_t mapped_memtype;
	pgtable_access_t     mapped_kernel_access, mapped_user_access;
	bool		     l_ret;
	pgtable_vm_t	     page_table, *pgtable = &page_table;
	bool		     f_ret	    = true;
	void		    *tested_mem	    = NULL;
	void		    *overlapped_mem = NULL;
	void_ptr_result_t    ret;

	printf("===> begin test partially mapping: virtual_address(%lx) "
	       "size(%ld) overlapped address(%lx) size(%ld) - ",
	       virtual_address, size, overlapped_virtual_address,
	       overlapped_size);

	if (try_map) {
		printf("Try_map TRUE\n");
	} else {
		printf("Try_map FALSE\n");
	}

	// init pgtable
	memset(pgtable, 0, sizeof(*pgtable));
	if (pgtable_vm_init(partition, pgtable, 65U) != OK) {
		printf("Failed to create hyp pgtable.\n");
		f_ret = false;
		goto out;
	}

	// allocate a page
	ret = partition_alloc(partition, size, alignment);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	// Map first range
	pgtable_vm_map(partition, pgtable, virtual_address, size, p,
		       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_RW,
		       PGTABLE_ACCESS_RW, try_map, allow_merge);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if ((!l_ret) || (mapped_base != p)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	// try to map the overlapped memory
	ret = partition_alloc(partition, overlapped_size, overlapped_size);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	overlapped_mem = ret.r;
	p2 = partition_virt_to_phys(partition, (uintptr_t)overlapped_mem);

	// Map overlapping range
	ret.e = pgtable_vm_map(partition, pgtable, overlapped_virtual_address,
			       overlapped_size, p2,
			       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_RW,
			       PGTABLE_ACCESS_RW, try_map, allow_merge);
	if ((ret.e != OK) && !try_map) {
		printf("Failed to map overlapping range.\n");
		f_ret = false;
		goto out;
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter overlapped map, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	// If the overlapping range was mapped with try_map set to true then the
	// lookup will not find it as it could not be mapped because the first
	// range was already mapped. If set to false, the lookup should be able
	// to find it and not the first range in its base address anymore.
	l_ret = pgtable_vm_lookup(pgtable, overlapped_virtual_address,
				  &mapped_base, &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if (l_ret && mapped_base == p2) {
		if (try_map) {
			printf("Overlapped mapping of %lx should fail.\n",
			       overlapped_virtual_address);
			f_ret = false;
			goto out;
		}
	} else if (!try_map) {
		printf("Overlapped mapping of %lx should succeed.\n",
		       overlapped_virtual_address);
		f_ret = false;
		goto out;
	}

	pgtable_vm_unmap_matching(partition, pgtable, virtual_address, p, size);
	partition_free(partition, tested_mem, size);

	paddr_t v_base;

	if (try_map) {
		v_base = virtual_address;
	} else {
		v_base = overlapped_virtual_address + overlapped_size;
	}

	l_ret = pgtable_vm_lookup(pgtable, v_base, &mapped_base, &mapped_size,
				  &mapped_memtype, &mapped_kernel_access,
				  &mapped_user_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", v_base);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", v_base);
	}

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	if (!try_map) {
		l_ret = pgtable_vm_lookup(pgtable, virtual_address,
					  &mapped_base, &mapped_size,
					  &mapped_memtype,
					  &mapped_kernel_access,
					  &mapped_user_access);
		if (!l_ret) {
			printf("Failed to remap %lx\n", virtual_address);
			f_ret = false;
			goto out;
		}

		l_ret = pgtable_vm_lookup(pgtable, overlapped_virtual_address,
					  &mapped_base, &mapped_size,
					  &mapped_memtype,
					  &mapped_kernel_access,
					  &mapped_user_access);
		if (!l_ret) {
			printf("Failed to unmap %lx ", virtual_address);
			printf("(%lx has been unmapped as well)\n",
			       overlapped_virtual_address);
			f_ret = false;
			goto out;
		}

		pgtable_vm_unmap_matching(partition, pgtable,
					  overlapped_virtual_address, p2,
					  overlapped_size);

		l_ret = pgtable_vm_lookup(pgtable, overlapped_virtual_address,
					  &mapped_base, &mapped_size,
					  &mapped_memtype,
					  &mapped_kernel_access,
					  &mapped_user_access);
		if (l_ret) {
			printf("Failed to unmap %lx (still exist)\n",
			       overlapped_virtual_address);
			f_ret = false;
			goto out;
		} else {
			printf("Success to unmap %lx.\n",
			       overlapped_virtual_address);
		}
	}
	partition_free(partition, overlapped_mem, overlapped_size);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter overlapped unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	pgtable_vm_destroy(partition, pgtable);

	printf("^^^^ test partially mapping done\n");
out:
	return f_ret;
}

bool
vm_test_change_access(uintptr_t virtual_address, size_t size, size_t alignment,
		      bool try_map, bool allow_merge)
{
	partition_t	     partition_normal;
	partition_t	    *partition = &partition_normal;
	paddr_t		     mapped_base, p;
	size_t		     mapped_size;
	pgtable_vm_memtype_t mapped_memtype;
	pgtable_access_t     mapped_kernel_access, mapped_user_access;
	bool		     l_ret;
	pgtable_vm_t	     page_table, *pgtable = &page_table;
	bool		     f_ret	= true;
	void		    *tested_mem = NULL;
	void_ptr_result_t    ret;

	printf("===> begin test mapping access change: virtual_address(%lx) "
	       "size(%ld) - ",
	       virtual_address, size);

	if (try_map) {
		printf("Try_map TRUE\n");
	} else {
		printf("Try_map FALSE\n");
	}

	// init pgtable
	memset(pgtable, 0, sizeof(*pgtable));
	if (pgtable_vm_init(partition, pgtable, 65U) != OK) {
		printf("Failed to create hyp pgtable.\n");
		f_ret = false;
		goto out;
	}

	// allocate a page
	ret = partition_alloc(partition, size, alignment);
	if (ret.e != OK) {
		printf("Failed to allocate a physical block for testing.\n");
		f_ret = false;
		goto out;
	}

	tested_mem = ret.r;
	p	   = partition_virt_to_phys(partition, (uintptr_t)tested_mem);

	// Map first range
	pgtable_vm_map(partition, pgtable, virtual_address, size, p,
		       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_RW,
		       PGTABLE_ACCESS_RW, try_map, allow_merge);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if ((!l_ret) || (mapped_base != p)) {
		printf("Failed to lookup for %lx.\n", virtual_address);
		f_ret = false;
		goto out;
	}

	printf("Mapping found - mapped_kernel_access: %d, mapped_user_access: %d\n",
	       mapped_kernel_access, mapped_user_access);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter map, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	// Change access of mapping
	pgtable_vm_map(partition, pgtable, virtual_address, size, p,
		       PGTABLE_VM_MEMTYPE_NORMAL_WB, PGTABLE_ACCESS_R,
		       PGTABLE_ACCESS_R, try_map, allow_merge);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);

	if ((!l_ret) || (mapped_base != p) ||
	    (mapped_kernel_access != PGTABLE_ACCESS_R) ||
	    (mapped_user_access != PGTABLE_ACCESS_R)) {
		printf("Failed to lookup for %lx \n", virtual_address);
		if ((mapped_kernel_access != PGTABLE_ACCESS_R) ||
		    (mapped_user_access != PGTABLE_ACCESS_R)) {
			printf("Mapping with kernel access (%d) and user access(%d) ",
			       PGTABLE_ACCESS_R, PGTABLE_ACCESS_R);
			printf("and got kernel access (%d) and user access(%d)\n",
			       mapped_kernel_access, mapped_user_access);
		}
		f_ret = false;
		goto out;
	}

	printf("Mapping found - mapped_kernel_access: %d, mapped_user_access: %d\n",
	       mapped_kernel_access, mapped_user_access);

	pgtable_vm_unmap_matching(partition, pgtable, virtual_address, p, size);

	l_ret = pgtable_vm_lookup(pgtable, virtual_address, &mapped_base,
				  &mapped_size, &mapped_memtype,
				  &mapped_kernel_access, &mapped_user_access);
	if (l_ret) {
		printf("Failed to unmap %lx (still exist)\n", virtual_address);
		f_ret = false;
		goto out;
	} else {
		printf("Success to unmap %lx.\n", virtual_address);
	}

	partition_free(partition, tested_mem, size);

#ifndef NDEBUG
	if (dump_table) {
		printf("\nafter unmap, page table dump:\n");
		dump_memory_allocation();
		pgtable_vm_dump(pgtable);
	}
#endif

	pgtable_vm_destroy(partition, pgtable);

	printf("^^^^ test mapping access change done\n");
out:
	return f_ret;
}
