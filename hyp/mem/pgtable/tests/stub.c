// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdio.h>
#include <stdlib.h>
#undef register_t

#include "mem_manager.h"
#include "stub.h"

// Assume test is for 64 bit
// Fake physical memory region up to 48 bit, each time, the allocated physical
// address will not return, so when it's used up all 48 bit physical memory,
// allocation pmem will be failed
#define MAX_PMEM_ADDR (0xFFFFFFFFFFFF)

// assume test is for 4K
#define P4K_SHIFT (12)
#define P4K_MASK  ((1 << 12) - 1)

#define CUR_MASK  P4K_MASK
#define CUR_SHIFT P4K_SHIFT
#define CUR_PG_SZ (1 << CUR_SHIFT)

#define ROUND_UP_SZ(sz) ((sz + CUR_PG_SZ - 1) & (~CUR_MASK))

// use a count to indicate current start of available physical memory
uint64_t    pmem_avail = 0UL;
static bool debug      = false;

partition_t *
partition_get_private(void)
{
	return (partition_t *)0xFFUL;
}

paddr_t
partition_virt_to_phys(partition_t *part, uintptr_t vaddr)
{
	struct mem_node *n   = NULL;
	paddr_t		 ret = 0UL;

	n = alloc_dict_find_vaddr((void *)vaddr);
	if (n != NULL) {
		ret = n->paddr;
	}

	return ret;
}

void_ptr_result_t
partition_alloc(partition_t *partition, size_t bytes, size_t min_alignment)
{
	void		 *vaddr;
	paddr_t		  paddr, orig_pmem_avail;
	void_ptr_result_t ret;

	ret.e = OK;

	// aligned start address first
	orig_pmem_avail = pmem_avail;
	pmem_avail	= (pmem_avail + min_alignment - 1) / min_alignment *
			  min_alignment;

	// create a page aligned address, and take it as physical address
	paddr = pmem_avail;
	pmem_avail += ROUND_UP_SZ(bytes);
	if (pmem_avail > MAX_PMEM_ADDR) {
		printf("WARNING: used up all physical memory, allocated too"
		       " much in test case, refine physical memory"
		       " code in test case!!\n");
		ret.e = ERROR_NOMEM;
		goto out;
	}

	if (paddr & CUR_MASK) {
		pmem_avail = orig_pmem_avail;
		ret.e	   = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vaddr = malloc(bytes);
	if (vaddr == NULL) {
		pmem_avail = orig_pmem_avail;
		ret.r	   = NULL;
		ret.e	   = ERROR_NOMEM;
		goto out;
	}

	if (alloc_dict_add(partition, paddr, vaddr, ROUND_UP_SZ(bytes))) {
		pmem_avail = orig_pmem_avail;
		free(vaddr);
		ret.e = ERROR_NOMEM;
		goto out;
	}

	if (debug) {
		printf("alloc va(%lx) pa(%lx) sz(%zu)\n", (uint64_t)vaddr,
		       paddr, bytes);
	}

	ret.r = vaddr;
out:
	return ret;
}

error_t
partition_free(partition_t *partition, void *mem, size_t bytes)
{
	struct mem_node *t   = alloc_dict_find_vaddr(mem);
	error_t		 ret = OK;

	if (t == NULL) {
		// something wrong
		printf("ERROR: cannot find %p, something wrong\n", mem);
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	} else {
		if (debug) {
			printf("free va(%lx) pa(%lx) cnt(%zu)\n",
			       (uint64_t)t->vaddr, (uint64_t)t->paddr,
			       t->map_cnt);
		}

		if (t->map_cnt != 0) {
			printf("ERROR: free[%p] with it mapped(%zu)\n", mem,
			       t->map_cnt);
		}

		free(t->vaddr);
		alloc_dict_remove(t);
	}
out:
	return ret;
}

error_t
partition_free_phys(partition_t *partition, paddr_t mem, size_t bytes)
{
	struct mem_node *t   = alloc_dict_find_paddr(mem);
	error_t		 ret = OK;

	if (t == NULL) {
		// something wrong
		printf("ERROR: cannot find %lx, something wrong\n", mem);
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	} else {
		if (debug) {
			printf("free va(%lx) pa(%lx) cnt(%zu)\n",
			       (uint64_t)t->vaddr, mem, t->map_cnt);
		}

		if (t->map_cnt != 0) {
			printf("ERROR: free[%lx] with it mapped(%zu)\n", mem,
			       t->map_cnt);
		}

		free(t->vaddr);
		alloc_dict_remove(t);
	}
out:
	return ret;
}

void *
partition_phys_map(paddr_t paddr, size_t size)
{
	void		*ret = NULL;
	struct mem_node *t   = alloc_dict_find_paddr(paddr);

	if (t != NULL) {
		ret = t->vaddr;
		t->map_cnt += 1;

		if (debug) {
			printf("map va(%lx) pa(%lx) sz(%zu) cnt(%zu)\n",
			       (uint64_t)ret, paddr, size, t->map_cnt);
		}
	}

	return ret;
}

void
partition_phys_unmap(void *vaddr, paddr_t paddr, size_t size)
{
	struct mem_node *t = alloc_dict_find_paddr(paddr);
	if (t != NULL) {
		t->map_cnt -= 1;

		if (debug) {
			printf("unmap va(%lx) pa(%lx) sz(%zu) cnt(%zu)\n",
			       (uint64_t)vaddr, paddr, size, t->map_cnt);
		}
	}

	return;
}

void
partition_phys_access_enable(void *ptr)
{
	return;
}

void
partition_phys_access_disable(void *ptr)
{
	return;
}

void
pgtable_handle_boot_runtime_warm_init()
{
	return;
}

void
init(void)
{
	alloc_dict_init();
}

void
cleanup(void)
{
	alloc_dict_cleanup();
}

// Reset pmem mapping
void
reset_pmem_allocation()
{
	cleanup();
	pmem_avail = 0UL;
	init();
}

// Check if there's physical memory still allocated
bool
has_pmem_allocated(void)
{
	return alloc_dict_paddr_allocated_count() != 0;
}

#ifndef NDEBUG
void
dump_memory_allocation()
{
	alloc_dict_dump();
}
#endif

#if !defined(NDEBUG)
void
assert_failed(const char *file, int line, const char *func, const char *err)
{
	(void)file;
	(void)line;
	(void)func;
	(void)err;
}
#endif

void
panic(const char *msg)
{
	printf("PANIC: %s\n", msg);
	exit(-1);
}

void
pgtable_vm_init_regs(pgtable_vm_t *vm_pgtable)
{
}

void
spinlock_init(spinlock_t *unused)
{
	(void)unused;
}

void
spinlock_acquire(spinlock_t *unused)
{
	(void)unused;
}

void
spinlock_release(spinlock_t *unused)
{
	(void)unused;
}
