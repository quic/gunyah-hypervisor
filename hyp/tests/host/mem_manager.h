// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

typedef struct partition_s partition_t;

struct mem_node {
	partition_t *const part;
	const uintptr_t	   paddr;
	void *const	   vaddr;
	const size_t	   sz;
	size_t		   map_cnt;
};

void
alloc_dict_init(void);

void
alloc_dict_cleanup(void);

bool
alloc_dict_add(partition_t *part, uintptr_t paddr, void *virt, size_t sz);

struct mem_node *
alloc_dict_find_paddr(uintptr_t paddr);

struct mem_node *
alloc_dict_find_vaddr(void *vaddr);

void
alloc_dict_remove(struct mem_node *t);

size_t
alloc_dict_paddr_allocated_count(void);

void
alloc_dict_dump(void);
