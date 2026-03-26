// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// @file
// Random test will randomly call all APIs following the designed rules.
// The test is replayable, and contains facility to assist debug.
// All the input arguments should be regulated (valid per convention).
// It can be the supplimentary of the code coverage calculation.

// Randomize input of all operations:
// * map: partition, va, pa, size
// * unmap: partition, va, size (identical to one of the map ops), preserve_size
// * lookup: virtual_address
// * mem access op: get pa from virtual_address by directly read memory
// (independent func)
//
// The following calls will just be called one time per random test
// * init: bottom bit size
// * destroy
//
// The rules:
// * all inputs should be valid
// * map & unmap must be paired & reversed

// TODO:
// * randomize test for access/memtype
// * randomize test for lookup
// * simulate HW access pattern
// * refine printf with MSG macro
// * add command to check contiguous bit.

#include <assert.h>
#include <hyptypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hypconstants.h>

#include <getopt.h>
#include <libgen.h>
#include <pgtable.h>
#include <sys/queue.h>

#include "stub.h"

// FIXME: duplicate from pgtable.c, since need to keep info_4k_granules as static for
// namespace consideration
#define util_bit(n) ((uint64_t)1U << (n))

#define util_mask(n) (util_bit(n) - 1)
// mask for [e, s]
#define segment_mask(e, s) (util_mask(e + 1) & (~util_mask(s)))

#define PGTABLE_LEVEL_NUM (PGTABLE_LEVEL__MAX + 1)

// INTERFACE
typedef enum action {
	INIT		  = 0,
	MAP		  = 1,
	UNMAP		  = 2,
	LOOKUP		  = 3,
	MEM_ACCESS	  = 4,
	DEINIT		  = 5,
	CHECK_TABLE_LEVEL = 6,
	PREALLOC	  = 7,
	CHECK_ENTRY_CNT	  = 8,
	REMAP		  = 9,
} action_t;

typedef enum status {
	FAILED = 0,
	SUCCESS,
} status_t;

typedef struct mem_access_args {
	bool	found;
	paddr_t paddr;
} mem_access_args_t;

typedef struct check_table_level_args {
	index_t level;
	bool	found;
} check_table_level_args_t;

typedef struct check_table_entry_cnt_args {
	bool is_correct;
	// indicates the level entry which contains incorrect entry cnt
	index_t failure_level;
	size_t	expected_cnt;
	size_t	actual_cnt;
} check_table_entry_cnt_args_t;

typedef struct stack_elem {
	paddr_t		    paddr;
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
pgtable_handle_boot_cold_init(void);

// access internal function
extern vmsa_entry_t
get_entry(vmsa_level_table_t *table, index_t idx);

extern size_t
get_table_refcount(vmsa_level_table_t *table, index_t idx);

extern pgtable_entry_types_t
get_entry_type(vmsa_entry_t *entry, const pgtable_level_info_t *level_info);

extern void
get_entry_paddr(const pgtable_level_info_t *level_info, vmsa_entry_t *entry,
		pgtable_entry_types_t type, paddr_t *paddr);
extern void
pgtable_hyp_ext(vmaddr_t virtual_address, size_t size,
		pgtable_entry_types_t entry_types, ext_func_t func, void *data);

extern void
pgtable_hyp_destroy(partition_t *partition);

#ifndef NDEBUG
// FIXME: mixed debug function with normal one, fix it.
extern void
pgtable_hyp_dump(void);
#endif

// Should only have one root
static bool dump  = false;
static bool debug = false;
// easy debug
static size_t line_cnt = 0;

static size_t granule_size = 0;

// FIXME: duplicated from pgtable.c
#define level_conf info_4k_granules
static const pgtable_level_info_t info_4k_granules[] = {
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

pgtable_modifier_ret_t
mem_access(pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	   index_t level, pgtable_entry_types_t type,
	   stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	   index_t *next_level, vmaddr_t *next_virtual_address,
	   size_t *next_size, paddr_t *next_table);

bool
run_init(const char *args);

bool
run_map(const char *args);

bool
run_remap(const char *args);

bool
run_unmap(const char *args);

bool
run_lookup(const char *args);

bool
run_memaccess(const char *args);

bool
run_prealloc(const char *args);

bool
run_check_table_level(const char *args);

bool
run_check_entry_cnt(const char *args);

bool
run_deinit(const char *args);

static void
help(char *app_name)
{
	printf("Usage: %s -f input\n", basename(app_name));
	printf("Run test case from input file\n\n");

	return;
}

static const char *
action_name(action_t a)
{
	const char *name = "Unknown";
	switch (a) {
	case INIT:
		name = "init";
		break;

	case MAP:
		name = "map";
		break;

	case UNMAP:
		name = "unmap";
		break;

	case LOOKUP:
		name = "lookup";
		break;

	case MEM_ACCESS:
		name = "mem_access";
		break;

	case DEINIT:
		name = "destroy";
		break;

	case CHECK_TABLE_LEVEL:
		name = "check_table_level";
		break;

	case CHECK_ENTRY_CNT:
		name = "check_entry_cnt";
		break;

	case PREALLOC:
		name = "prealloc";
		break;

	case REMAP:
		name = "remap";
		break;

	default:
		break;
	}

	return name;
}

bool
run_init(const char *args)
{
	pgtable_handle_boot_cold_init();

	return true;
}

bool
run_map(const char *args)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	action_t     action;
	uintptr_t    virtual_address;
	size_t	     size;
	paddr_t	     pa;
	size_t	     merge_limit;

	sscanf(args, "%u %lx %zu %lx %zi", &action, &virtual_address, &size,
	       &pa, &merge_limit);

	error_t err = pgtable_hyp_map_merge(partition, virtual_address, size,
					    pa, PGTABLE_HYP_MEMTYPE_STRONG,
					    PGTABLE_ACCESS_RW,
					    VMSA_SHAREABILITY_INNER_SHAREABLE,
					    merge_limit);

	if (err != OK) {
		printf("map failed: va(%#lx) size(%zu) pa(%#lx) ml(%zu)-> err(%d)\n",
		       virtual_address, size, pa, merge_limit, err);
#ifndef NDEBUG
		dump_memory_allocation();
		pgtable_hyp_dump();
#endif
		return false;
	}

#ifndef NDEBUG
	if (dump) {
		printf("after map: virtual_address(%#lx) size(%zu) pa(%#lx) ml(%zu)\n",
		       virtual_address, size, pa, merge_limit);
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	return true;
}

bool
run_remap(const char *args)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	action_t     action;
	uintptr_t    virtual_address;
	size_t	     size;
	paddr_t	     pa;
	size_t	     merge_limit;

	sscanf(args, "%u %lx %zu %lx %zi", &action, &virtual_address, &size,
	       &pa, &merge_limit);

	error_t err = pgtable_hyp_remap_merge(partition, virtual_address, size,
					      pa, PGTABLE_HYP_MEMTYPE_STRONG,
					      PGTABLE_ACCESS_RW,
					      VMSA_SHAREABILITY_INNER_SHAREABLE,
					      merge_limit);

	if (err != OK) {
		printf("remap failed: va(%#lx) size(%zu) pa(%#lx) ml(%zu)-> err(%d)\n",
		       virtual_address, size, pa, merge_limit, err);
#ifndef NDEBUG
		dump_memory_allocation();
		pgtable_hyp_dump();
#endif
		return false;
	}

#ifndef NDEBUG
	if (dump) {
		printf("after remap: va(%#lx) size(%zu) pa(%#lx) ml(%zu)\n",
		       virtual_address, size, pa, merge_limit);
		dump_memory_allocation();
		pgtable_hyp_dump();
	}
#endif

	return true;
}

bool
run_unmap(const char *args)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	action_t     action;
	uintptr_t    virtual_address;
	size_t	     size, preserved_prealloc;

	sscanf(args, "%u %lx %lu %lu", &action, &virtual_address, &size,
	       &preserved_prealloc);
	pgtable_hyp_unmap(partition, virtual_address, size, preserved_prealloc);

	return true;
}

bool
run_lookup(const char *args)
{
	paddr_t		      mapped_base;
	size_t		      mapped_size;
	pgtable_hyp_memtype_t mapped_memtype;
	pgtable_access_t      mapped_access;
	uintptr_t	      virtual_address;
	size_t		      expect_size, expect_pa;
	status_t	      expect_status;
	bool		      l_ret = false;
	bool		      match = false;
	bool		      ret   = false;
	action_t	      action;

	sscanf(args, "%u %lx %lu %lx %u", &action, &virtual_address,
	       &expect_size, &expect_pa, &expect_status);
	l_ret = pgtable_hyp_lookup(virtual_address, &mapped_base, &mapped_size,
				   &mapped_memtype, &mapped_access);
	if (l_ret) {
		// found pa
		if ((mapped_base == expect_pa) &&
		    (mapped_size == expect_size)) {
			match = true;
		}
	}

	if ((match && (expect_status == SUCCESS)) ||
	    ((!match) && (expect_status == FAILED))) {
		ret = true;
	} else {
		// FIXME: should we just consider it's a failure when there's
		// something there.
		ret = false;

#ifndef NDEBUG
		dump_memory_allocation();
		pgtable_hyp_dump();
#endif
	}

	return ret;
}

bool
run_prealloc(const char *args)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	action_t     action;
	uintptr_t    virtual_address;
	size_t	     size;

	sscanf(args, "%u %lx %lu", &action, &virtual_address, &size);
	pgtable_hyp_preallocate(partition, virtual_address, size);

	return true;
}

// check whether there is an table entry point to a table
pgtable_modifier_ret_t
check_table_level(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		  index_t idx, index_t level, pgtable_entry_types_t type,
		  stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		  index_t *next_level, vmaddr_t *next_virtual_address,
		  size_t *next_size, paddr_t *next_table)
{
	check_table_level_args_t *args = (check_table_level_args_t *)data;
	pgtable_modifier_ret_t	  ret  = PGTABLE_MODIFIER_RET_CONTINUE;

	if (level == args->level &&
	    pgtable_entry_types_get_next_level_table(&type)) {
		args->found = true;
		ret	    = PGTABLE_MODIFIER_RET_STOP;
	}

	return ret;
}

bool
run_check_table_level(const char *args)
{
	check_table_level_args_t margs;
	pgtable_entry_types_t	 types = pgtable_entry_types_default();
	action_t		 action;
	uintptr_t		 virtual_address;
	index_t			 level;
	int			 level_val;
	const size_t		 page_size = 4096UL;

	sscanf(args, "%u %lx %u", &action, &virtual_address, &level_val);
	assert(level_val > 0);
	assert(level_val < PGTABLE_LEVEL_NUM);
	level = (index_t)level_val;

	margs.found = false;
	// FIXME: note we can not check root table, since it's always there,
	// testcase should avoid that
	margs.level = level - 1;
	pgtable_entry_types_set_next_level_table(&types, true);
	(void)pgtable_hyp_ext(virtual_address, page_size, types,
			      check_table_level, &margs);

	return margs.found;
}

pgtable_modifier_ret_t
check_table_entry_cnt(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		      index_t idx, index_t level, pgtable_entry_types_t type,
		      stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		      index_t *next_level, vmaddr_t *next_virtual_address,
		      size_t *next_size, paddr_t *next_table)
{
	check_table_entry_cnt_args_t *args =
		(check_table_entry_cnt_args_t *)data;
	pgtable_modifier_ret_t	    vret = PGTABLE_MODIFIER_RET_CONTINUE;
	vmsa_entry_t		    cur_entry, sub_entry;
	vmsa_level_table_t	   *cur_table = NULL, *sub_table = NULL;
	const pgtable_level_info_t *info      = NULL;
	size_t			    entry_cnt = 0, check_cnt = 0;
	error_t			    ret = OK;
	paddr_t			    sub_table_phys;
	index_t			    sub_idx;
	pgtable_entry_types_t	    sub_type;

	assert(pgtable_entry_types_get_next_level_table(&type));

	// get the entry count of current table entry
	info = &level_conf[level];
	assert(stack[level].mapped);
	cur_table = stack[level].table;
	cur_entry = get_entry(cur_table, idx);
	entry_cnt = get_table_refcount(cur_table, idx);

	// get physical address of next level table
	get_entry_paddr(info, &cur_entry, type, &sub_table_phys);

	sub_table = (vmsa_level_table_t *)partition_phys_map(
		sub_table_phys, util_bit(pgt->granule_shift));
	if (sub_table == NULL) {
		vret		 = PGTABLE_MODIFIER_RET_ERROR;
		args->is_correct = false;
		goto out;
	}

	// loop
	info	  = &level_conf[level + 1];
	check_cnt = 0;
	for (sub_idx = 0; sub_idx < info->entry_cnt; sub_idx++) {
		sub_entry = get_entry(sub_table, sub_idx);
		sub_type  = get_entry_type(&sub_entry, info);

		// count any entry which is valid as table/block/page
		// this check ignores the check about whether this type of entry
		// should be there
		if (pgtable_entry_types_get_next_level_table(&sub_type) ||
		    pgtable_entry_types_get_block(&sub_type) ||
		    pgtable_entry_types_get_page(&sub_type)) {
			check_cnt++;
		}
	}

	if (check_cnt == entry_cnt) {
		args->is_correct = true;
		vret		 = PGTABLE_MODIFIER_RET_CONTINUE;
	} else {
		args->is_correct    = false;
		args->failure_level = level;
		args->expected_cnt  = entry_cnt;
		args->actual_cnt    = check_cnt;
		vret		    = PGTABLE_MODIFIER_RET_STOP;
	}

out:
	if (sub_table != NULL) {
		partition_phys_unmap(sub_table, sub_table_phys,
				     util_bit(pgt->granule_shift));
	}

	return vret;
}

bool
run_check_entry_cnt(const char *args)
{
	check_table_entry_cnt_args_t margs;
	pgtable_entry_types_t	     types = pgtable_entry_types_default();
	action_t		     action;
	uintptr_t		     virtual_address;
	size_t			     size;

	sscanf(args, "%u %lx %lu", &action, &virtual_address, &size);

	// in case there is no such address, should let it pass
	margs.is_correct    = true;
	margs.failure_level = 0;
	margs.expected_cnt  = 0;
	margs.actual_cnt    = 0;
	pgtable_entry_types_set_next_level_table(&types, true);
	(void)pgtable_hyp_ext(virtual_address, size, types,
			      check_table_entry_cnt, &margs);

	// if (dump && (!margs.is_correct)) {
	if ((!margs.is_correct)) {
		printf("Entry cnt check failure: level(%u), expect(%zu) ",
		       margs.failure_level, margs.expected_cnt);
		printf(" actual(%zu)\n", margs.actual_cnt);
		pgtable_hyp_dump();
	}

	return margs.is_correct;
}

pgtable_modifier_ret_t
mem_access(pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	   index_t level, pgtable_entry_types_t type,
	   stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	   index_t *next_level, vmaddr_t *next_virtual_address,
	   size_t *next_size, paddr_t *next_table)
{
	mem_access_args_t	   *args	= (mem_access_args_t *)data;
	pgtable_modifier_ret_t	    ret		= PGTABLE_MODIFIER_RET_STOP;
	const pgtable_level_info_t *level_info	= NULL;
	vmaddr_t		    offset_mask = 0UL;
	paddr_t			    offset	= 0UL;
	vmsa_entry_t		    cur_entry;
	vmsa_level_table_t	   *cur_table = NULL;
	paddr_t			    p;

	// get the offset based on virtual_address
	level_info  = &level_conf[level];
	offset_mask = level_info->addr_size - 1;
	offset	    = virtual_address & offset_mask;

	// current level should be mapped
	assert(stack[level].mapped);
	cur_table = stack[level].table;
	cur_entry = get_entry(cur_table, idx);

	get_entry_paddr(level_info, &cur_entry, type, &p);

	args->paddr = p + offset;
	args->found = true;

	return ret;
}

// FIXME: could directly read memory to check the address translation.
bool
run_memaccess(const char *args)
{
	mem_access_args_t     margs;
	pgtable_entry_types_t types = pgtable_entry_types_default();
	action_t	      action;
	uintptr_t	      virtual_address;
	size_t		      size;
	bool		      ret;
	paddr_t		      expect_paddr;
	uint32_t	      expect_failure;

	// address (va/pa) must to be hex format
	sscanf(args, "%u %lx %lu %lx %u", &action, &virtual_address, &size,
	       &expect_paddr, &expect_failure);

	margs.found = false;
	pgtable_entry_types_set_block(&types, true);
	pgtable_entry_types_set_page(&types, true);
	if (size < granule_size) {
		size = granule_size;
	}
	if (virtual_address & (granule_size - 1U)) {
		uint64_t ofs = virtual_address & (granule_size - 1U);
		virtual_address -= ofs;
		expect_paddr -= ofs;
	}
	(void)pgtable_hyp_ext(virtual_address, size, types, mem_access, &margs);

	if ((margs.found && (margs.paddr == expect_paddr)) ||
	    ((!margs.found) && (expect_failure == 1))) {
		ret = true;
	} else {
		printf("Failed to access mem: va(%lx), size(%zu) ",
		       virtual_address, size);
		if (margs.found) {
			printf(" found pa(%lx)", margs.paddr);
		} else {
			printf(" found unmapped");
		}
		if (expect_failure == 1) {
			printf(" expect failure\n");
		} else {
			printf(" expect success pa(%lx)\n", expect_paddr);
		}
		ret = false;

#ifndef NDEBUG
		dump_memory_allocation();
		pgtable_hyp_dump();
#endif
	}

	return ret;
}

bool
run_deinit(const char *args)
{
	partition_t  partition_normal;
	partition_t *partition = &partition_normal;
	bool	     ret       = true;

	pgtable_hyp_destroy(partition);

#ifndef NDEBUG
	if (dump) {
		dump_memory_allocation();
	}
#endif

	if (has_pmem_allocated()) {
		printf("Memory leak detected\n");
		ret = false;
	}
	reset_pmem_allocation();

	return ret;
}

// NOTE: could take input from outside or organise test by python.
int
main(int argc, char *argv[])
{
	const size_t buf_size = 256;
	char	     buf[buf_size];
	char	    *fn = NULL;
	FILE	    *fp = NULL;
	action_t     action;
	int	     opt;
	bool	     pgtable_initialised = false;
	bool	     ret;

	while ((opt = getopt(argc, argv, "f:h")) != -1) {
		switch (opt) {
		case 'f':
			fn = optarg;
			break;
		default:
			help(argv[0]);
			exit(-1);
		}
	}

	if (fn == NULL) {
		printf("Need input file for this test.\n");
		help(argv[0]);
		exit(-1);
	}

	fp = fopen(fn, "r");
	if (fp == NULL) {
		printf("Failed to open file\n");
		exit(-1);
	}

	init();

	setbuf(stdout, NULL);
	printf("testcase (%s)... ", fn);

	// Calc granule size
	for (index_t i = 0;; i++) {
		if (pgtable_entry_types_get_page(
			    &level_conf[i].allowed_types)) {
			granule_size = level_conf[i].addr_size;
			break;
		}
		if (level_conf[i].lsb == 0) {
			printf("granule_size calc failed\n");
			exit(-1);
		}
	}

	// load specific input from input file
	for (line_cnt = 1; NULL != fgets(buf, buf_size, fp); line_cnt++) {
		// Progress indication
		if ((line_cnt % 100) == 0) {
			int chars = printf("%zd", line_cnt);
			for (int j = 0; j < chars; j++) {
				putc('\b', stdout);
			}
		}

		// skip the commented line, must be at the first char of the
		// line
		// FIXME: could make it more flexible to allow just be the first
		// char after indentation
		if (buf[0] == '#' || buf[0] == '\n') {
			continue;
		}

		sscanf(buf, "%u", &action);

		// perform tests
		if (debug) {
			printf(">> begin %s:%zu\n", action_name(action),
			       line_cnt);
		}

		switch (action) {
		case INIT:
			assert(!pgtable_initialised);
			pgtable_initialised = true;
			ret		    = run_init(buf);
			break;

		case MAP:
			ret = run_map(buf);
			break;

		case REMAP:
			ret = run_remap(buf);
			break;

		case UNMAP:
			ret = run_unmap(buf);
			break;

		case LOOKUP:
			ret = run_lookup(buf);
			break;

		case MEM_ACCESS:
			ret = run_memaccess(buf);
			break;

		case DEINIT:
			assert(pgtable_initialised);
			pgtable_initialised = false;
			ret		    = run_deinit(buf);
			break;

		case CHECK_TABLE_LEVEL:
			ret = run_check_table_level(buf);
			break;

		case CHECK_ENTRY_CNT:
			ret = run_check_entry_cnt(buf);
			break;

		case PREALLOC:
			ret = run_prealloc(buf);
			break;

		default:
			printf("Warning: unknown action %u on line %zu\n",
			       action, line_cnt);
		}

		if (debug) {
			printf("<< end %s:%zu\n", action_name(action),
			       line_cnt);
		}

		if (!ret) {
			printf("Line %zu operation failed.\n", line_cnt);
			break;
		}
	}

	if (ret) {
		puts("PASSED");
		return 0;
	} else {
		puts("FAILED");
		return -1;
	}
}
