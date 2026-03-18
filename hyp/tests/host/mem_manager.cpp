// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <map>

extern "C" {
#include "mem_manager.h"
};

std::map<uintptr_t, mem_node> *alloc_dict;

void
alloc_dict_init(void)
{
	alloc_dict = new std::map<uintptr_t, mem_node>;
}

void
alloc_dict_cleanup(void)
{
	delete alloc_dict;
}

bool
alloc_dict_add(partition_t *part, uintptr_t paddr, void *virt, size_t sz)
{
	auto result = alloc_dict->emplace(paddr, (mem_node){ .part    = part,
							     .paddr   = paddr,
							     .vaddr   = virt,
							     .sz      = sz,
							     .map_cnt = 0 });

	return !result.second;
}

mem_node *
alloc_dict_find_paddr(uintptr_t paddr)
{
	auto result = alloc_dict->find(paddr);
	return (result != alloc_dict->end()) ? &result->second : NULL;
}

mem_node *
alloc_dict_find_vaddr(void *vaddr)
{
	for (auto &pair: *alloc_dict) {
		if (pair.second.vaddr == vaddr) {
			return &pair.second;
		}
	}
	return NULL;
}

void
alloc_dict_remove(mem_node *t)
{
	alloc_dict->erase(t->paddr);
}

size_t
alloc_dict_paddr_allocated_count(void)
{
	return alloc_dict->size();
}

void
alloc_dict_dump(void)
{
	printf("+---------------- MEM INFO ----------------\n");
	for (auto &pair: *alloc_dict) {
		auto &node = pair.second;
		printf("| part[%p]:%lx -> %p [%ld]\n", node.part, node.paddr,
		       node.vaddr, node.sz);
	}
	printf("+------------------------------------------\n\n");
}
