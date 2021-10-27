// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// TODO:
//
// * Make this code thread-safe. Currently the calling code is required to take
// a lock before using these functions. The level reference counting needs to be
// implemented for modification operations, and RCU used for level deletions.
//
// * Fix misra for pointer type cast.
//
// * add more checks in API level. Like, the size should be multiple of page
// size.
//
// * use the only one category of level instead of two.
//
// * change internal function to use return type (value + error code)
//
// * add test case for contiguous bit.

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypcontainers.h>

#if !defined(HOST_TEST)
#include <hypregisters.h>

#include <attributes.h>
#include <compiler.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <thread.h>
#include <trace.h>
#endif

#include <hyp_aspace.h>
#include <partition.h>
#include <pgtable.h>
#include <spinlock.h>
#include <util.h>

#if !defined(HOST_TEST)
#include <asm/barrier.h>
#endif

#include "event_handlers.h"
#include "events/pgtable.h"

#define SHIFT_4K  (12)
#define SHIFT_16K (14)
#define SHIFT_64K (16)

// mask for [e, s]
#define segment_mask(e, s) (util_mask(e + 1) & (~util_mask(s)))

// FIXME: might be temporary definition for TCR
#define TCR_RGN_NORMAL_NC	 0
#define TCR_RGN_NORMAL_WB_RA_WA	 1
#define TCR_RGN_NORMAL_WT_RA_NWA 2
#define TCR_RGN_NORMAL_WB_RA_NWA 3
#define TCR_SH_NONE		 0
#define TCR_SH_OUTER		 2
#define TCR_SH_INNER		 3
#define TCR_TG0_4KB		 0
#define TCR_TG1_4KB		 2

// Every legal entry type except next level tables
static const pgtable_entry_types_t VMSA_ENTRY_TYPE_LEAF =
	VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE | VMSA_ENTRY_TYPE_INVALID;

#if defined(HOST_TEST)
// Definitions for host test

bool pgtable_op = true;

#define compiler_clrsb(x) __builtin_clrsbl(x)

#define LOG(tclass, log_level, ...)                                            \
	do {                                                                   \
		char log[1024];                                                \
		snprint(log, 1024, __VA_ARGS__);                               \
		puts(log);                                                     \
	} while (0)

#define PGTABLE_VM_PAGE_SIZE 4096
#else
// For target HW

#if defined(NDEBUG)
// pgtable_op is not actually defined for NDEBUG
extern bool pgtable_op;
#else
static _Thread_local bool pgtable_op;
#endif

extern vmsa_general_entry_t aarch64_pt_ttbr1_level1;

#endif

#define PGTABLE_LEVEL_NUM (PGTABLE_LEVEL__MAX + 1)

typedef struct stack_elem {
	paddr_t		    paddr;
	vmsa_level_table_t *table;
	bool		    mapped;
	bool		    need_unmap;
	char		    padding[6];
} stack_elem_t;

typedef struct {
	uint8_t level;
	uint8_t padding[7];
	size_t	size;
} get_start_level_info_ret_t;

#if defined(PLATFORM_PGTABLE_4K_GRANULE)
// Statically support only 4k granule size for now
#define level_conf info_4k_granules

static const pgtable_level_info_t info_4k_granules[PGTABLE_LEVEL_NUM] = {
	// level 0
	{ .msb				      = 47,
	  .lsb				      = 39,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = false,
	  .allowed_types	= VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE,
	  .addr_size		= 1UL << 39,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_0,
	  .contiguous_entry_cnt = 0U },
	// level 1
	{ .msb				      = 38,
	  .lsb				      = 30,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = segment_mask(47, 30),
	  .is_offset			      = false,
	  .allowed_types = VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
			   VMSA_ENTRY_TYPE_BLOCK,
	  .addr_size		= 1UL << 30,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_1,
	  .contiguous_entry_cnt = 16U },
	// level 2
	{ .msb				      = 29,
	  .lsb				      = 21,
	  .table_mask			      = segment_mask(47, 12),
	  .block_and_page_output_address_mask = segment_mask(47, 21),
	  .is_offset			      = false,
	  .allowed_types = VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
			   VMSA_ENTRY_TYPE_BLOCK,
	  .addr_size		= 1UL << 21,
	  .entry_cnt		= 1UL << 9,
	  .level		= PGTABLE_LEVEL_2,
	  .contiguous_entry_cnt = 16U },
	// level 3
	{ .msb				      = 20,
	  .lsb				      = 12,
	  .table_mask			      = 0,
	  .block_and_page_output_address_mask = segment_mask(47, 12),
	  .is_offset			      = false,
	  .allowed_types		      = VMSA_ENTRY_TYPE_PAGE,
	  .addr_size			      = 1UL << 12,
	  .entry_cnt			      = 1UL << 9,
	  .level			      = PGTABLE_LEVEL_3,
	  .contiguous_entry_cnt		      = 16U },
	// offset
	{ .msb				      = 11,
	  .lsb				      = 0,
	  .table_mask			      = 0U,
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = true,
	  .allowed_types		      = VMSA_ENTRY_TYPE_NONE,
	  .addr_size			      = 0U,
	  .entry_cnt			      = 0U,
	  .level			      = PGTABLE_LEVEL_OFFSET,
	  .contiguous_entry_cnt		      = 0U }
};

#elif defined(PLATFORM_PGTABLE_16K_GRANULE)
#define level_conf info_16k_granules

// FIXME: temporarily disable it, enable it for run time configuration
static const pgtable_level_info_t info_16k_granules[PGTABLE_LEVEL_NUM] = {
	// FIXME: level 0 is not permitted for stage-2 (in VTCR_EL2), must use
	// two concatenated level-1 entries.
	// level 0
	{ .msb				      = 47,
	  .lsb				      = 47,
	  .table_mask			      = segment_mask(47, 14),
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = false,
	  .allowed_types	= VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE,
	  .addr_size		= 1UL << 47,
	  .entry_cnt		= 2U,
	  .level		= PGTABLE_LEVEL_0,
	  .contiguous_entry_cnt = 0U },
	// level 1
	{ .msb				      = 46,
	  .lsb				      = 36,
	  .table_mask			      = segment_mask(47, 14),
	  .block_and_page_output_address_mask = segment_mask(47, 36),
	  .is_offset			      = false,
	  .allowed_types	= VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE,
	  .addr_size		= 1UL << 36,
	  .entry_cnt		= 1UL << 11,
	  .level		= PGTABLE_LEVEL_1,
	  .contiguous_entry_cnt = 0U },
	// level 2
	{ .msb				      = 35,
	  .lsb				      = 25,
	  .table_mask			      = segment_mask(47, 14),
	  .block_and_page_output_address_mask = segment_mask(47, 25),
	  .is_offset			      = false,
	  .allowed_types = VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
			   VMSA_ENTRY_TYPE_BLOCK,
	  .addr_size		= 1UL << 25,
	  .entry_cnt		= 1UL << 11,
	  .level		= PGTABLE_LEVEL_2,
	  .contiguous_entry_cnt = 32U },
	// level 3
	{ .msb				      = 24,
	  .lsb				      = 14,
	  .table_mask			      = 0U,
	  .block_and_page_output_address_mask = segment_mask(47, 14),
	  .is_offset			      = false,
	  .allowed_types		      = VMSA_ENTRY_TYPE_PAGE,
	  .addr_size			      = 1UL << 14,
	  .entry_cnt			      = 1UL << 11,
	  .level			      = PGTABLE_LEVEL_3,
	  .contiguous_entry_cnt		      = 128U },
	// offset
	{ .msb				      = 13,
	  .lsb				      = 0,
	  .table_mask			      = 0U,
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = true,
	  .allowed_types		      = VMSA_ENTRY_TYPE_NONE,
	  .addr_size			      = 0U,
	  .entry_cnt			      = 0U,
	  .level			      = PGTABLE_LEVEL_OFFSET,
	  .contiguous_entry_cnt		      = 0U }
};

#elif defined(PLATFORM_PGTABLE_64K_GRANULE)
#define level_conf info_64k_granules

// NOTE: check page 2416, table D5-20 properties of the address lookup levels
// 64kb granule size
static const pgtable_level_info_t info_64k_granules[PGTABLE_LEVEL_NUM] = {
	// level 0
	{ .msb				      = 47,
	  .lsb				      = 42,
	  .table_mask			      = segment_mask(47, 16),
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = false,
	  // No LPA support, so no block type
	  .allowed_types	= VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE,
	  .addr_size		= 1UL << 42,
	  .entry_cnt		= 1UL << 6,
	  .level		= PGTABLE_LEVEL_1,
	  .contiguous_entry_cnt = 0U },
	// level 1
	{ .msb				      = 41,
	  .lsb				      = 29,
	  .table_mask			      = segment_mask(47, 16),
	  .block_and_page_output_address_mask = segment_mask(47, 29),
	  .is_offset			      = false,
	  .allowed_types = VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
			   VMSA_ENTRY_TYPE_BLOCK,
	  .addr_size		= 1UL << 29,
	  .entry_cnt		= 1UL << 13,
	  .level		= PGTABLE_LEVEL_2,
	  .contiguous_entry_cnt = 32U },
	// level 2
	{ .msb				      = 28,
	  .lsb				      = 16,
	  .table_mask			      = 0U,
	  .block_and_page_output_address_mask = segment_mask(47, 16),
	  .is_offset			      = false,
	  .allowed_types		      = VMSA_ENTRY_TYPE_PAGE,
	  .addr_size			      = 1UL << 16,
	  .entry_cnt			      = 1UL << 13,
	  .level			      = PGTABLE_LEVEL_3,
	  .contiguous_entry_cnt		      = 32U },
	// offset
	{ .msb				      = 15,
	  .lsb				      = 0,
	  .table_mask			      = 0U,
	  .block_and_page_output_address_mask = 0U,
	  .is_offset			      = true,
	  .allowed_types		      = VMSA_ENTRY_TYPE_NONE,
	  .addr_size			      = 0U,
	  .entry_cnt			      = 0U,
	  .level			      = PGTABLE_LEVEL_OFFSET,
	  .contiguous_entry_cnt		      = 0U }
};
#else
#error Need to specify page table granule for pgtable module
#endif

static pgtable_hyp_t hyp_pgtable;
static paddr_t	     ttbr0_phys;

#ifndef NDEBUG

// Private type For external modifier, only for debug version
typedef pgtable_modifier_ret_t (*ext_func_t)(
	pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	index_t level, vmsa_entry_type_t type,
	stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data, index_t *next_level,
	vmaddr_t *next_virtual_address, size_t *next_size, paddr_t *next_table);

typedef struct ext_modifier_args {
	ext_func_t func;
	void *	   data;
} ext_modifier_args_t;

// just for debug
void
pgtable_hyp_dump(void);

void
pgtable_hyp_ext(vmaddr_t virtual_address, size_t size,
		pgtable_entry_types_t entry_types, ext_func_t func, void *data);

void
pgtable_vm_dump(pgtable_vm_t *pgtable);

void
pgtable_vm_ext(pgtable_vm_t *pgtable, vmaddr_t virtual_address, size_t size,
	       pgtable_entry_types_t entry_types, ext_func_t func, void *data);
#endif

static void
hyp_tlbi_va(vmaddr_t virtual_address)
{
	// FIXME: we can use more restrictive tlbi if possible
	// FIXME: before invalidate tlb, should we wait for all device/normal
	// memory write operations done.
	vmsa_tlbi_vae2_input_t input;

	vmsa_tlbi_vae2_input_init(&input);
	vmsa_tlbi_vae2_input_set_VA(&input, virtual_address);

#ifndef HOST_TEST
	__asm__ volatile("tlbi VAE2IS, %[VA]	;"
			 : "+m"(asm_ordering)
			 : [VA] "r"(vmsa_tlbi_vae2_input_raw(input)));
#endif
}

static void
vm_tlbi_ipa(vmaddr_t virtual_address)
{
	vmsa_tlbi_ipas2e1is_input_t input;

	vmsa_tlbi_ipas2e1is_input_init(&input);
	vmsa_tlbi_ipas2e1is_input_set_IPA(&input, virtual_address);

#ifndef HOST_TEST
	__asm__ volatile("tlbi IPAS2E1IS, %[VA]	;"
			 : "+m"(asm_ordering)
			 : [VA] "r"(vmsa_tlbi_ipas2e1is_input_raw(input)));
#endif
}

static void
dsb()
{
#ifndef HOST_TEST
	__asm__ volatile("dsb ish" ::: "memory");
#endif
}

static void
vm_tlbi_vmalle1()
{
#ifndef HOST_TEST
	__asm__ volatile("tlbi VMALLE1IS" ::: "memory");
#endif
}

// return true if it's top virt address
static bool
is_hyp_top_virtual_address(vmaddr_t virtual_address);

// check if the virtual address (VA/IPA) bit count is under restriction.
// true if it's right
static bool
addr_check(vmaddr_t virtual_address, size_t bit_count);

#if defined(HOST_TEST)
// Unit test need these helper functions
vmsa_general_entry_t
get_entry(vmsa_level_table_t *table, index_t idx);

vmsa_entry_type_t
get_entry_type(vmsa_general_entry_t *	   entry,
	       const pgtable_level_info_t *level_info);

error_t
get_entry_paddr(const pgtable_level_info_t *level_info,
		vmsa_general_entry_t *entry, vmsa_entry_type_t type,
		paddr_t *paddr);

count_t
get_table_refcount(vmsa_level_table_t *table, index_t idx);
#else
static vmsa_general_entry_t
get_entry(vmsa_level_table_t *table, index_t idx);

static vmsa_entry_type_t
get_entry_type(vmsa_general_entry_t *	   entry,
	       const pgtable_level_info_t *level_info);

static error_t
get_entry_paddr(const pgtable_level_info_t *level_info,
		vmsa_general_entry_t *entry, vmsa_entry_type_t type,
		paddr_t *paddr);

static count_t
get_table_refcount(vmsa_level_table_t *table, index_t idx);
#endif

static void
set_table_refcount(vmsa_level_table_t *table, index_t idx, count_t refcount);

static pgtable_vm_memtype_t
map_stg2_attr_to_memtype(vmsa_lower_attrs_t attrs);

static pgtable_hyp_memtype_t
map_stg1_attr_to_memtype(vmsa_lower_attrs_t attrs);

static vmsa_lower_attrs_t
get_lower_attr(vmsa_general_entry_t entry);

static vmsa_upper_attrs_t
get_upper_attr(vmsa_general_entry_t entry);

static pgtable_access_t
map_stg1_attr_to_access(vmsa_upper_attrs_t upper_attrs,
			vmsa_lower_attrs_t lower_attrs);

static void
map_stg2_attr_to_access(vmsa_upper_attrs_t upper_attrs,
			vmsa_lower_attrs_t lower_attrs,
			pgtable_access_t * kernel_access,
			pgtable_access_t * user_access);

static void
map_stg2_memtype_to_attrs(pgtable_vm_memtype_t	   memtype,
			  vmsa_stg2_lower_attrs_t *lower_attrs);

static void
map_stg1_memtype_to_attrs(pgtable_hyp_memtype_t	   memtype,
			  vmsa_stg1_lower_attrs_t *lower_attrs);

static void
map_stg1_access_to_attrs(pgtable_access_t	  access,
			 vmsa_stg1_upper_attrs_t *upper_attrs,
			 vmsa_stg1_lower_attrs_t *lower_attrs);

static void
map_stg2_access_to_attrs(pgtable_access_t	  kernel_access,
			 pgtable_access_t	  user_access,
			 vmsa_stg2_upper_attrs_t *upper_attrs,
			 vmsa_stg2_lower_attrs_t *lower_attrs);

static void
set_invalid_entry(vmsa_level_table_t *table, index_t idx);

static void
set_table_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
		count_t count, bool fence);

static void
set_page_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
	       vmsa_upper_attrs_t upper_attrs, vmsa_lower_attrs_t lower_attrs,
	       bool contiguous, bool fence);

static void
set_block_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
		vmsa_upper_attrs_t upper_attrs, vmsa_lower_attrs_t lower_attrs,
		bool contiguous, bool fence);

// Helper function for translation table walking. Stop walking if modifier
// returns false
static bool
translation_table_walk(pgtable_t *pgt, vmaddr_t virtual_address,
		       size_t virtual_address_size,
		       pgtable_translation_table_walk_event_t event,
		       pgtable_entry_types_t target_types, void *data);

static error_t
alloc_level_table(partition_t *partition, size_t size, size_t alignment,
		  paddr_t *paddr, vmsa_level_table_t **table);

static void
set_pgtables(vmaddr_t virtual_address, stack_elem_t stack[PGTABLE_LEVEL_NUM],
	     index_t start_level, index_t cur_level, count_t initial_refcount);

static pgtable_modifier_ret_t
map_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	     index_t level, vmsa_entry_type_t type,
	     stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	     index_t *next_level, vmaddr_t *next_virtual_address,
	     size_t *next_size, paddr_t *next_table);

static pgtable_modifier_ret_t
lookup_modifier(pgtable_t *pgt, vmsa_general_entry_t cur_entry, index_t level,
		vmsa_entry_type_t type, void *data);

static void
check_refcount(pgtable_t *pgt, partition_t *partition, vmaddr_t virtual_address,
	       size_t size, index_t upper_level,
	       stack_elem_t stack[PGTABLE_LEVEL_NUM], bool need_dec,
	       size_t preserved_size, index_t *next_level,
	       vmaddr_t *next_virtual_address, size_t *next_size);

#if 0
static bool
map_should_set_cont(vmaddr_t virtual_address, size_t size,
		    vmaddr_t entry_address, index_t level);
#endif

static bool
unmap_should_clear_cont(vmaddr_t virtual_address, size_t size, index_t level);

static void
unmap_clear_cont_bit(vmsa_level_table_t *table, vmaddr_t virtual_address,
		     index_t			       level,
		     vmsa_page_and_block_attrs_entry_t attr_entry,
		     pgtable_unmap_modifier_args_t *   margs);

static bool
unmap_check_start(vmaddr_t virtual_address, vmsa_general_entry_t cur_entry,
		  vmsa_entry_type_t type, index_t level,
		  pgtable_unmap_modifier_args_t *margs);

static bool
unmap_check_end(vmaddr_t virtual_address, size_t size,
		vmsa_general_entry_t cur_entry, vmsa_entry_type_t type,
		index_t level, pgtable_unmap_modifier_args_t *margs);

static pgtable_modifier_ret_t
unmap_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
	       index_t idx, index_t level, vmsa_entry_type_t type,
	       stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	       index_t *next_level, vmaddr_t *next_virtual_address,
	       size_t *next_size, bool only_matching);

static pgtable_modifier_ret_t
prealloc_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		  index_t level, vmsa_entry_type_t type,
		  stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		  index_t *next_level, vmaddr_t *next_virtual_address,
		  size_t *next_size, paddr_t *next_table);

// Return entry idx, it can make sure the returned index is always in the
// range
static inline index_t
get_index(vmaddr_t addr, const pgtable_level_info_t *info)
{
	return (index_t)((addr & segment_mask(info->msb, info->lsb)) >>
			 info->lsb);
}

#ifndef NDEBUG
static inline vmaddr_t
set_index(vmaddr_t addr, const pgtable_level_info_t *info, index_t idx)
{
	// FIXME: double check if it might cause issue to clear [63, 48] bits
	return (addr & (~segment_mask(info->msb, info->lsb))) |
	       (((vmaddr_t)idx << info->lsb) &
		segment_mask(info->msb, info->lsb));
}
#endif

static inline vmaddr_t
step_virtual_address(vmaddr_t virtual_address, const pgtable_level_info_t *info)
{
	// should be fine if it overflows, might need to report error
	return (virtual_address + info->addr_size) & (~util_mask(info->lsb));
}

// Return the actual size of current entry within the specified virtual address
// range.
static inline size_t
size_on_level(vmaddr_t virtual_address, size_t size,
	      const pgtable_level_info_t *level_info)
{
	vmaddr_t v_s = virtual_address, v_e = virtual_address + size - 1;
	vmaddr_t l_s = 0U, l_e = 0U;

	assert(!util_add_overflows(virtual_address, size - 1));

	l_s = (virtual_address >> level_info->lsb) << level_info->lsb;
	// even for the level 0, it will set the bit 49, will not overflow for
	// 64 bit
	l_e = l_s + level_info->addr_size - 1;

	assert(!util_add_overflows(l_s, level_info->addr_size - 1));

	l_s = util_max(l_s, v_s);
	l_e = util_min(v_e, l_e);

	return l_e - l_s + 1;
}

static inline vmaddr_t
entry_start_address(vmaddr_t			virtual_address,
		    const pgtable_level_info_t *level_info)
{
	return (virtual_address >> level_info->lsb) << level_info->lsb;
}

static inline bool
is_preserved_table_entry(size_t			     preserved_size,
			 const pgtable_level_info_t *level_info)
{
	assert(util_is_p2_or_zero(preserved_size));
	return preserved_size < level_info->addr_size;
}

// Helper function to manipulate page table entry
vmsa_general_entry_t
get_entry(vmsa_level_table_t *table, index_t idx)
{
	partition_phys_access_enable(&table[idx]);
	vmsa_general_entry_t entry =
		atomic_load_explicit(&table[idx], memory_order_relaxed);
	partition_phys_access_disable(&table[idx]);
	return entry;
}

bool
is_hyp_top_virtual_address(vmaddr_t virtual_address)
{
	return (virtual_address & hyp_pgtable.top_mask) != 0U;
}

bool
addr_check(vmaddr_t virtual_address, size_t bit_count)
{
#if ARCH_IS_64BIT
	static_assert(sizeof(vmaddr_t) == 8, "vmaddr_t expected to be 64bits");

	int64_t v = (int64_t)virtual_address;
	size_t	count;

	// NOTE: assume LVA is not enabled, and not use va tag
	count = 64U - (compiler_clrsb(v) + 1);
#else
#error unimplemented
#endif

	return count <= bit_count;
}

vmsa_entry_type_t
get_entry_type(vmsa_general_entry_t *	   entry,
	       const pgtable_level_info_t *level_info)
{
	vmsa_entry_type_t ret;

	if (vmsa_general_entry_get_is_valid(entry)) {
		if (vmsa_general_entry_get_is_table(entry)) {
			if (level_info->allowed_types &
			    VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE) {
				ret = VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE;
			} else {
				ret = VMSA_ENTRY_TYPE_PAGE;
			}
		} else {
			if (level_info->allowed_types & VMSA_ENTRY_TYPE_BLOCK) {
				ret = VMSA_ENTRY_TYPE_BLOCK;
			} else {
				ret = VMSA_ENTRY_TYPE_RESERVED;
			}
		}
	} else {
		ret = VMSA_ENTRY_TYPE_INVALID;
	}

	return ret;
}

error_t
get_entry_paddr(const pgtable_level_info_t *level_info,
		vmsa_general_entry_t *entry, vmsa_entry_type_t type,
		paddr_t *paddr)
{
	error_t		    ret = OK;
	vmsa_block_entry_t *blk;
	vmsa_page_entry_t * pg;
	vmsa_table_entry_t *tb;

	*paddr = 0U;
	switch (type) {
	case VMSA_ENTRY_TYPE_BLOCK:
		blk    = (vmsa_block_entry_t *)entry;
		*paddr = vmsa_block_entry_get_OutputAddress(blk) &
			 level_info->block_and_page_output_address_mask;
		break;

	case VMSA_ENTRY_TYPE_PAGE:
		pg     = (vmsa_page_entry_t *)entry;
		*paddr = vmsa_page_entry_get_OutputAddress(pg) &
			 level_info->block_and_page_output_address_mask;
		break;

	case VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE:
		tb     = (vmsa_table_entry_t *)entry;
		*paddr = vmsa_table_entry_get_NextLevelTableAddress(tb) &
			 level_info->table_mask;
		break;

	case VMSA_ENTRY_TYPE_INVALID:
	case VMSA_ENTRY_TYPE_RESERVED:
	case VMSA_ENTRY_TYPE_ERROR:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		break;
	}

	return ret;
}

count_t
get_table_refcount(vmsa_level_table_t *table, index_t idx)
{
	vmsa_general_entry_t g;
	vmsa_table_entry_t * entry = (vmsa_table_entry_t *)&g;

	g = get_entry(table, idx);
	return vmsa_table_entry_get_refcount(entry);
}

static inline void
set_table_refcount(vmsa_level_table_t *table, index_t idx, count_t count)
{
	vmsa_general_entry_t g;
	vmsa_table_entry_t * val = (vmsa_table_entry_t *)&g;

	g = get_entry(table, idx);
	vmsa_table_entry_set_refcount(val, count);
	partition_phys_access_enable(&table[idx]);
	atomic_store_explicit(&table[idx], g, memory_order_relaxed);
	partition_phys_access_disable(&table[idx]);
}

pgtable_vm_memtype_t
map_stg2_attr_to_memtype(vmsa_lower_attrs_t attrs)
{
	vmsa_stg2_lower_attrs_t val = vmsa_stg2_lower_attrs_cast(attrs);
	return vmsa_stg2_lower_attrs_get_mem_attr(&val);
}

pgtable_hyp_memtype_t
map_stg1_attr_to_memtype(vmsa_lower_attrs_t attrs)
{
	vmsa_stg1_lower_attrs_t val = vmsa_stg1_lower_attrs_cast(attrs);
	// only the MAIR index decides the memory type, it's directly map
	return vmsa_stg1_lower_attrs_get_attr_idx(&val);
}

vmsa_lower_attrs_t
get_lower_attr(vmsa_general_entry_t entry)
{
	vmsa_page_and_block_attrs_entry_t *val =
		(vmsa_page_and_block_attrs_entry_t *)&entry;
	return vmsa_page_and_block_attrs_entry_get_lower_attrs(val);
}

vmsa_upper_attrs_t
get_upper_attr(vmsa_general_entry_t entry)
{
	vmsa_page_and_block_attrs_entry_t *val =
		(vmsa_page_and_block_attrs_entry_t *)&entry;
	return vmsa_page_and_block_attrs_entry_get_upper_attrs(val);
}

pgtable_access_t
map_stg1_attr_to_access(vmsa_upper_attrs_t upper_attrs,
			vmsa_lower_attrs_t lower_attrs)
{
	vmsa_stg1_lower_attrs_t l   = vmsa_stg1_lower_attrs_cast(lower_attrs);
	vmsa_stg1_upper_attrs_t u   = vmsa_stg1_upper_attrs_cast(upper_attrs);
	bool			xn  = false;
	vmsa_stg1_ap_t		ap  = VMSA_STG1_AP_EL0_NONE_UPPER_READ_ONLY;
	pgtable_access_t	ret = PGTABLE_ACCESS_NONE;

#if ARCH_AARCH64_USE_VHE
	xn = vmsa_stg1_upper_attrs_get_PXN(&u);
#else
	xn = vmsa_stg1_upper_attrs_get_XN(&u);
#endif
	ap = vmsa_stg1_lower_attrs_get_AP(&l);

	switch (ap) {
#if ARCH_AARCH64_USE_PAN
	case VMSA_STG1_AP_ALL_READ_WRITE:
	case VMSA_STG1_AP_ALL_READ_ONLY:
		// EL0 has access, so no access in EL2 (unless PAN is disabled)
		ret = PGTABLE_ACCESS_NONE;
		break;
#else // !ARCH_AARCH64_USE_PAN
	case VMSA_STG1_AP_ALL_READ_WRITE:
#endif
	case VMSA_STG1_AP_EL0_NONE_UPPER_READ_WRITE:
		// XN is ignored due to SCTLR_EL2.WXN=1; it should be true
		assert(xn);
		ret = PGTABLE_ACCESS_RW;
		break;
#if !ARCH_AARCH64_USE_PAN
	case VMSA_STG1_AP_ALL_READ_ONLY:
#endif
	case VMSA_STG1_AP_EL0_NONE_UPPER_READ_ONLY:
		ret = xn ? PGTABLE_ACCESS_R : PGTABLE_ACCESS_RX;
		break;
	}

	return ret;
}

// Map from Stage 2 XN and S2AP to access, to get access just use:
// s2_xxx_acc[S2AP][XN]
static pgtable_access_t stg2_access[4][2] = {
	// AP 0x0
	{ //      XN 0x0         XN 0x1
	  PGTABLE_ACCESS_X, PGTABLE_ACCESS_NONE },
	// AP 0x1
	{ //      XN 0x0         XN 0x1
	  PGTABLE_ACCESS_RX, PGTABLE_ACCESS_R },
	// AP 0x2
	{ //      XN 0x0         XN 0x1
	  // Note, ACCESS_WX not implemented
	  PGTABLE_ACCESS_NONE, PGTABLE_ACCESS_W },
	// AP 0x3
	{ //      XN 0x0         XN 0x1
	  PGTABLE_ACCESS_RWX, PGTABLE_ACCESS_RW }
};

void
map_stg2_attr_to_access(vmsa_upper_attrs_t upper_attrs,
			vmsa_lower_attrs_t lower_attrs,
			pgtable_access_t * kernel_access,
			pgtable_access_t * user_access)
{
	vmsa_stg2_lower_attrs_t l  = vmsa_stg2_lower_attrs_cast(lower_attrs);
	vmsa_stg2_upper_attrs_t u  = vmsa_stg2_upper_attrs_cast(upper_attrs);
	uint8_t			xn = 0;
	vmsa_s2ap_t		ap;

	xn = vmsa_stg2_upper_attrs_get_XN(&u);
	ap = vmsa_stg2_lower_attrs_get_S2AP(&l);

	*kernel_access = stg2_access[ap][(xn >> 1) & 1U];
#if defined(ARCH_ARM_8_2_TTS2UXN)
	*user_access = stg2_access[ap][(xn >> 0) & 1U];
#else
	*user_access = stg2_access[ap][0U];
#endif
}

void
map_stg2_memtype_to_attrs(pgtable_vm_memtype_t	   memtype,
			  vmsa_stg2_lower_attrs_t *lower_attrs)
{
	vmsa_stg2_lower_attrs_set_mem_attr(lower_attrs, memtype);
	switch (memtype) {
	case PGTABLE_VM_MEMTYPE_NORMAL_NC:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_INC:
	case PGTABLE_VM_MEMTYPE_NORMAL_WT:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_INC:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_WB:
#if SCHEDULER_CAN_MIGRATE
		vmsa_stg2_lower_attrs_set_SH(lower_attrs,
					     VMSA_SHAREABILITY_INNER_SHAREABLE);
#else
		vmsa_stg2_lower_attrs_set_SH(lower_attrs,
					     VMSA_SHAREABILITY_NON_SHAREABLE);
#endif
		break;
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE:
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRE:
	case PGTABLE_VM_MEMTYPE_DEVICE_NGRE:
	case PGTABLE_VM_MEMTYPE_DEVICE_GRE:
	default:
		vmsa_stg2_lower_attrs_set_SH(lower_attrs,
					     VMSA_SHAREABILITY_NON_SHAREABLE);
	}
}

void
map_stg1_memtype_to_attrs(pgtable_hyp_memtype_t	   memtype,
			  vmsa_stg1_lower_attrs_t *lower_attrs)
{
	vmsa_stg1_lower_attrs_set_attr_idx(lower_attrs, memtype);
}

void
map_stg1_access_to_attrs(pgtable_access_t	  access,
			 vmsa_stg1_upper_attrs_t *upper_attrs,
			 vmsa_stg1_lower_attrs_t *lower_attrs)
{
	uint8_t xn, ap;

	switch (access) {
	case PGTABLE_ACCESS_RX:
	case PGTABLE_ACCESS_X:
		xn = false;
		break;
	case PGTABLE_ACCESS_NONE:
	case PGTABLE_ACCESS_W:
	case PGTABLE_ACCESS_R:
	case PGTABLE_ACCESS_RW:
		xn = true;
		break;
	case PGTABLE_ACCESS_RWX:
	default:
		panic("Invalid stg1 access type");
	}

	/* set AP */
	switch (access) {
	case PGTABLE_ACCESS_W:
	case PGTABLE_ACCESS_RW:
		ap = VMSA_STG1_AP_EL0_NONE_UPPER_READ_WRITE;
		break;
	case PGTABLE_ACCESS_NONE:
#if ARCH_AARCH64_USE_PAN
		ap = VMSA_STG1_AP_ALL_READ_WRITE;
		break;
#endif
	case PGTABLE_ACCESS_R:
	case PGTABLE_ACCESS_RX:
	case PGTABLE_ACCESS_X:
		ap = VMSA_STG1_AP_EL0_NONE_UPPER_READ_ONLY;
		break;
	case PGTABLE_ACCESS_RWX:
	default:
		panic("Invalid stg1 access type");
	}

	vmsa_stg1_lower_attrs_set_AP(lower_attrs, ap);
#if ARCH_AARCH64_USE_VHE
	vmsa_stg1_upper_attrs_set_PXN(upper_attrs, xn);
#else
	vmsa_stg1_upper_attrs_set_XN(upper_attrs, xn);
#endif
}

void
map_stg2_access_to_attrs(pgtable_access_t	  kernel_access,
			 pgtable_access_t	  user_access,
			 vmsa_stg2_upper_attrs_t *upper_attrs,
			 vmsa_stg2_lower_attrs_t *lower_attrs)
{
	uint8_t	    xn;
	vmsa_s2ap_t ap;
	bool	    kernel_exec = false, user_exec = false;

	if ((kernel_access & PGTABLE_ACCESS_X) != 0) {
		kernel_exec = true;
	}
	if ((user_access & PGTABLE_ACCESS_X) != 0) {
		user_exec = true;
	}

	xn = (kernel_exec) ? 0 : 2;
#if defined(ARCH_ARM_8_2_TTS2UXN)
	if (kernel_exec != user_exec) {
		xn = (kernel_exec) ? 3 : 1;
	}
#else
	(void)user_exec;
	assert(kernel_access == user_access);
#endif

	// set AP
	// kernel access and user access (RW) should be the same
	static_assert(PGTABLE_ACCESS_X == 1,
		      "expect PGTABLE_ACCESS_X is bit 0");
	assert(((kernel_access ^ kernel_access) >> 1) == 0);
	assert(kernel_access != PGTABLE_ACCESS_X);

	switch (kernel_access) {
	case PGTABLE_ACCESS_R:
	case PGTABLE_ACCESS_RX:
		ap = VMSA_S2AP_READ_ONLY;
		break;
	case PGTABLE_ACCESS_W:
		ap = VMSA_S2AP_WRITE_ONLY;
		break;
	case PGTABLE_ACCESS_RW:
	case PGTABLE_ACCESS_RWX:
		ap = VMSA_S2AP_READ_WRITE;
		break;
	case PGTABLE_ACCESS_NONE:
	case PGTABLE_ACCESS_X:
	default:
		ap = VMSA_S2AP_NONE;
		break;
	}

	vmsa_stg2_lower_attrs_set_S2AP(lower_attrs, ap);
	vmsa_stg2_upper_attrs_set_XN(upper_attrs, xn);
}

void
set_invalid_entry(vmsa_level_table_t *table, index_t idx)
{
	vmsa_general_entry_t entry = { 0 };

	partition_phys_access_enable(&table[idx]);
	atomic_store_explicit(&table[idx], entry, memory_order_relaxed);
	partition_phys_access_disable(&table[idx]);
}

void
set_table_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
		count_t count, bool fence)
{
	vmsa_general_entry_t g;
	vmsa_table_entry_t * entry = (vmsa_table_entry_t *)&g;

	vmsa_table_entry_init(entry);
	vmsa_table_entry_set_NextLevelTableAddress(entry, addr);
	vmsa_table_entry_set_refcount(entry, count);

	partition_phys_access_enable(&table[idx]);
	if (fence) {
		atomic_store_explicit(&table[idx], g, memory_order_release);
	} else {
		atomic_store_explicit(&table[idx], g, memory_order_relaxed);
	}
	partition_phys_access_disable(&table[idx]);
}

void
set_page_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
	       vmsa_upper_attrs_t upper_attrs, vmsa_lower_attrs_t lower_attrs,
	       bool contiguous, bool fence)
{
	vmsa_general_entry_t	  g;
	vmsa_page_entry_t *	  entry = (vmsa_page_entry_t *)&g;
	vmsa_common_upper_attrs_t u;

	vmsa_page_entry_init(entry);

	u = vmsa_common_upper_attrs_cast(upper_attrs);
	vmsa_common_upper_attrs_set_cont(&u, contiguous);

	vmsa_page_entry_set_lower_attrs(entry, lower_attrs);
	vmsa_page_entry_set_upper_attrs(
		entry, (vmsa_upper_attrs_t)vmsa_common_upper_attrs_raw(u));
	vmsa_page_entry_set_OutputAddress(entry, addr);

	partition_phys_access_enable(&table[idx]);
	if (fence) {
		atomic_store_explicit(&table[idx], g, memory_order_release);
	} else {
		atomic_store_explicit(&table[idx], g, memory_order_relaxed);
	}
	partition_phys_access_disable(&table[idx]);
}

void
set_block_entry(vmsa_level_table_t *table, index_t idx, paddr_t addr,
		vmsa_upper_attrs_t upper_attrs, vmsa_lower_attrs_t lower_attrs,
		bool contiguous, bool fence)
{
	vmsa_general_entry_t	  g;
	vmsa_block_entry_t *	  entry = (vmsa_block_entry_t *)&g;
	vmsa_common_upper_attrs_t u;

	vmsa_block_entry_init(entry);

	u = vmsa_common_upper_attrs_cast(upper_attrs);
	vmsa_common_upper_attrs_set_cont(&u, contiguous);

	vmsa_block_entry_set_lower_attrs(entry, lower_attrs);
	vmsa_block_entry_set_upper_attrs(
		entry, (vmsa_upper_attrs_t)vmsa_common_upper_attrs_raw(u));
	vmsa_block_entry_set_OutputAddress(entry, addr);

	partition_phys_access_enable(&table[idx]);
	if (fence) {
		atomic_store_explicit(&table[idx], g, memory_order_release);
	} else {
		atomic_store_explicit(&table[idx], g, memory_order_relaxed);
	}
	partition_phys_access_disable(&table[idx]);
}

error_t
alloc_level_table(partition_t *partition, size_t size, size_t alignment,
		  paddr_t *paddr, vmsa_level_table_t **table)
{
	void_ptr_result_t alloc_ret;

	// actually only used to allocate a page
	alloc_ret = partition_alloc(partition, size, alignment);
	if (alloc_ret.e == OK) {
		memset(alloc_ret.r, 0, size);

		*table = (vmsa_level_table_t *)alloc_ret.r;
		*paddr = partition_virt_to_phys(partition,
						(uintptr_t)alloc_ret.r);
	}
	return alloc_ret.e;
}

// Helper function to map all sub page table/set entry count, following a FIFO
// order, so the last entry to write is the one which actually hook the whole
// new page table levels on the existing page table.
void
set_pgtables(vmaddr_t virtual_address, stack_elem_t stack[PGTABLE_LEVEL_NUM],
	     index_t start_level, index_t cur_level, count_t initial_refcount)
{
	paddr_t			    lower;
	vmsa_level_table_t *	    table;
	const pgtable_level_info_t *level_info = NULL;
	index_t			    idx;
	vmsa_general_entry_t	    g;
	vmsa_entry_type_t	    type;
	count_t			    refcount = initial_refcount;
	index_t			    level    = cur_level;

	while (start_level < level) {
		lower = stack[level].paddr;
		table = stack[level - 1].table;

		assert(stack[level - 1].mapped);

		level_info = &level_conf[level - 1];

		idx  = get_index(virtual_address, level_info);
		g    = get_entry(table, idx);
		type = get_entry_type(&g, level_info);

		switch (type) {
		case VMSA_ENTRY_TYPE_INVALID:
			// only sync with HW when the last page table
			// entry is written
			if (start_level == level - 1) {
				set_table_entry(table, idx, lower, refcount,
						true);
			} else {
				set_table_entry(table, idx, lower, refcount,
						false);
			}

			// the deepest level can be 0/1 for preallocate or
			// normal case. The rest is 1.
			if (refcount == initial_refcount) {
				refcount = 1;
			}

			break;

		case VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE:
			// just need to update the first entry count and exit
			// ignore the levels
			refcount = get_table_refcount(table, idx) + 1;
			set_table_refcount(table, idx, refcount);
			break;

		case VMSA_ENTRY_TYPE_PAGE:
		case VMSA_ENTRY_TYPE_BLOCK:
		case VMSA_ENTRY_TYPE_RESERVED:
		case VMSA_ENTRY_TYPE_ERROR:
		default:
			panic("Unexpected entry type");
		}

		level--;
	}

	return;
}

// Check if only the page access needs to be changed and update it.
static bool
pgtable_maybe_update_access(pgtable_t *	 pgt,
			    stack_elem_t stack[PGTABLE_LEVEL_NUM], index_t idx,
			    vmsa_entry_type_t		 type,
			    pgtable_map_modifier_args_t *margs, index_t level,
			    vmaddr_t virtual_address, size_t size,
			    vmaddr_t *next_virtual_address, size_t *next_size,
			    index_t *next_level)
{
	bool ret = false;

	// If only the entry's access permissions are changing, this can be
	// done without a break before make.

	const pgtable_level_info_t *cur_level_info = &level_conf[level];

	size_t	 addr_size = cur_level_info->addr_size;
	vmaddr_t entry_virtual_address =
		entry_start_address(virtual_address, cur_level_info);

	if ((type == VMSA_ENTRY_TYPE_BLOCK) &&
	    ((virtual_address != entry_virtual_address) ||
	     (size < addr_size))) {
		goto out;
	}
	assert(virtual_address == entry_virtual_address);

	size_t idx_stop = util_min(idx + (size >> cur_level_info->lsb),
				   cur_level_info->entry_cnt);

	paddr_t cur_phys = margs->phys;

	vmsa_level_table_t *table = stack[level].table;
	partition_phys_access_enable(&table[0]);
	while (idx != idx_stop) {
		vmsa_general_entry_t cur_entry =
			atomic_load_explicit(&table[idx], memory_order_relaxed);
		vmsa_upper_attrs_t upper_attrs = get_upper_attr(cur_entry);
		vmsa_lower_attrs_t lower_attrs = get_lower_attr(cur_entry);
		uint64_t	   xn_mask     = VMSA_STG2_UPPER_ATTRS_XN_MASK;
		uint64_t	   s2ap_mask = VMSA_STG2_LOWER_ATTRS_S2AP_MASK;

		paddr_t phys_addr;
		get_entry_paddr(&level_conf[level], &cur_entry, type,
				&phys_addr);

		if (phys_addr != cur_phys) {
			goto out_access;
		}
		vmsa_common_upper_attrs_t upper_attrs_bitfield =
			vmsa_common_upper_attrs_cast(upper_attrs);
		if (vmsa_common_upper_attrs_get_cont(&upper_attrs_bitfield)) {
			goto out_access;
		}
		if ((upper_attrs & ~xn_mask) !=
		    (margs->upper_attrs & ~xn_mask)) {
			goto out_access;
		}
		if ((lower_attrs & ~s2ap_mask) !=
		    (margs->lower_attrs & ~s2ap_mask)) {
			goto out_access;
		}

		vmsa_page_entry_t *entry = (vmsa_page_entry_t *)&cur_entry;

		if ((upper_attrs & xn_mask) != (margs->upper_attrs & xn_mask)) {
			vmsa_page_entry_set_upper_attrs(entry,
							margs->upper_attrs);
		}
		if ((lower_attrs & s2ap_mask) !=
		    (margs->lower_attrs & s2ap_mask)) {
			vmsa_page_entry_set_lower_attrs(entry,
							margs->lower_attrs);
		}

		atomic_store_explicit(&table[idx], cur_entry,
				      memory_order_release);

		idx++;
		cur_phys += cur_level_info->addr_size;
	}
	partition_phys_access_disable(&table[0]);

	ret = true;

	size_t updated_size = cur_phys - margs->phys;
	*next_size	    = size - updated_size;

	virtual_address += updated_size;
	*next_virtual_address = virtual_address;

	// Walk back up the tree if needed
	if (idx == cur_level_info->entry_cnt) {
		idx -= 1; // Last updated index
		while (idx == cur_level_info->entry_cnt - 1) {
			if (level == pgt->start_level) {
				break;
			} else {
				level--;
			}

			cur_level_info = &level_conf[level];
			// virtual_address is already stepped, use previous one
			// to check
			idx = get_index(virtual_address, cur_level_info);
		}
		*next_level = level;
	}

out:
	return ret;
out_access:
	partition_phys_access_disable(&table[0]);
	goto out;
}

static error_t
pgtable_add_table_entry(pgtable_t *pgt, pgtable_map_modifier_args_t *margs,
			index_t cur_level, stack_elem_t *stack,
			vmaddr_t virtual_address, size_t size,
			index_t *next_level, vmaddr_t *next_virtual_address,
			size_t *next_size, paddr_t *next_table,
			bool set_start_level)
{
	error_t		    ret;
	paddr_t		    new_pgtable_paddr;
	vmsa_level_table_t *new_pgt = NULL;
	index_t		    level   = cur_level;

	// allocate page and fill right value first, then update entry
	// to existing table
	ret = alloc_level_table(margs->partition, pgt->granule_size,
				pgt->granule_size, &new_pgtable_paddr,
				&new_pgt);
	if (ret != OK) {
		LOG(ERROR, WARN, "Failed to alloc page table level.\n");
		margs->error = ret;
		goto out;
	}

	if ((margs->new_page_start_level == PGTABLE_INVALID_LEVEL) &&
	    set_start_level) {
		margs->new_page_start_level =
			level > pgt->start_level ? level - 1 : level;
	}

	if (level >= (PGTABLE_LEVEL_NUM - 1)) {
		LOG(ERROR, WARN, "invalid level ({:d}).\n", level);
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// just record the new level in the stack
	stack[level + 1].paddr	= new_pgtable_paddr;
	stack[level + 1].table	= new_pgt;
	stack[level + 1].mapped = true;

	// guide translation_table_walk step into the new sub page table
	// level
	*next_level	      = level + 1;
	*next_virtual_address = virtual_address;
	*next_table	      = new_pgtable_paddr;
	*next_size	      = size;

out:
	return ret;
}

// Splits blocks into pages. Some pages mapped with the old physical address and
// other with the new one.
static pgtable_modifier_ret_t
pgtable_split_block(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		    index_t idx, index_t level, vmsa_entry_type_t type,
		    stack_elem_t		 stack[PGTABLE_LEVEL_NUM],
		    pgtable_map_modifier_args_t *margs, index_t *next_level,
		    vmaddr_t *next_virtual_address, size_t *next_size,
		    paddr_t *next_table)

{
	pgtable_modifier_ret_t vret = PGTABLE_MODIFIER_RET_CONTINUE;
	error_t		       ret;

	assert((level_conf[level].allowed_types & ENUM_VMSA_ENTRY_TYPE_BLOCK) !=
	       0U);

	// Get the values of the block before it is invalidated
	const pgtable_level_info_t *cur_level_info = &level_conf[level];
	size_t			    addr_size	   = cur_level_info->addr_size;
	vmaddr_t		    entry_virtual_address =
		entry_start_address(virtual_address, cur_level_info);
	vmsa_general_entry_t cur_entry = get_entry(stack[level].table, idx);
	vmsa_upper_attrs_t   cur_upper_attrs = get_upper_attr(cur_entry);
	vmsa_lower_attrs_t   cur_lower_attrs = get_lower_attr(cur_entry);
	paddr_t		     phys_addr;
	get_entry_paddr(cur_level_info, &cur_entry, type, &phys_addr);

	// Invalidate entry to then add a new table entry in the current
	// level
	set_invalid_entry(stack[level].table, idx);

	// First, we invalidate entry, we ensure that operation is done
	// by calling dsb, we flush the virtual address stage 2 tlb,
	// dsb, flush entire stage 1 tlb, dsb, and finally the entry
	// will be validated during the mapping operation.
	dsb();
	if (margs->stage == PGTABLE_HYP_STAGE_1) {
		hyp_tlbi_va(entry_virtual_address);
	} else {
		vm_tlbi_ipa(entry_virtual_address);
	}
	if (margs->stage == PGTABLE_VM_STAGE_2) {
		dsb();
		// FIXME: The full stage-1 flushing below is really
		// sub-optimal.
		vm_tlbi_vmalle1();
		dsb();
	}

	ret = pgtable_add_table_entry(pgt, margs, level, stack, virtual_address,
				      size, next_level, next_virtual_address,
				      next_size, next_table, false);
	if (ret != OK) {
		vret = PGTABLE_MODIFIER_RET_ERROR;
		goto out;
	}

	// Update current level and values as now we want to add all the
	// pages to the table just created
	level		= *next_level;
	virtual_address = *next_virtual_address;

	cur_level_info = &level_conf[level];

	size_t	page_size = cur_level_info->addr_size;
	count_t new_pages = (count_t)(addr_size / page_size);
	assert(new_pages == cur_level_info->entry_cnt);

#if 0
	// FIXME: also need to search forwards for occupied entries
	bool contiguous = map_should_set_cont(
		margs->orig_virtual_address, margs->orig_size,
		virtual_address, level);
#else
	bool contiguous = false;
#endif
	bool	page_block_fence;
	index_t start_level;

	if (margs->new_page_start_level != PGTABLE_INVALID_LEVEL) {
		start_level		    = margs->new_page_start_level;
		page_block_fence	    = false;
		margs->new_page_start_level = PGTABLE_INVALID_LEVEL;
	} else {
		start_level	 = level > pgt->start_level ? level - 1 : level;
		page_block_fence = true;
	}

	// Create all pages that cover the old block and hook them to
	// the new table entry
	assert(virtual_address >= entry_virtual_address);

	vmaddr_t cur_virtual_address = entry_virtual_address;

	assert(type == VMSA_ENTRY_TYPE_BLOCK);
	const vmsa_entry_type_t page_or_block_type =
		cur_level_info->allowed_types &
		(VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE);

	paddr_t phys;
	idx = 0;
	for (count_t i = 0; i < new_pages; i++) {
		vmsa_upper_attrs_t upper_attrs;
		vmsa_lower_attrs_t lower_attrs;

		phys	    = phys_addr;
		upper_attrs = cur_upper_attrs;
		lower_attrs = cur_lower_attrs;

		phys_addr += page_size;

		if (page_or_block_type == VMSA_ENTRY_TYPE_BLOCK) {
			set_block_entry(stack[level].table, idx, phys,
					upper_attrs, lower_attrs, contiguous,
					page_block_fence);
		} else {
			set_page_entry(stack[level].table, idx, phys,
				       upper_attrs, lower_attrs, contiguous,
				       page_block_fence);
		}
		cur_virtual_address += page_size;
		assert(!util_add_overflows(margs->phys, (paddr_t)page_size));
		assert(!util_add_overflows(phys_addr, (paddr_t)page_size));
		idx++;
	}

	set_pgtables(entry_virtual_address, stack, start_level, level,
		     new_pages);
out:
	return vret;
}

static pgtable_modifier_ret_t
pgtable_modify_mapping(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		       index_t idx, index_t cur_level, vmsa_entry_type_t type,
		       stack_elem_t		    stack[PGTABLE_LEVEL_NUM],
		       pgtable_map_modifier_args_t *margs, index_t *next_level,
		       vmaddr_t *next_virtual_address, size_t *next_size,
		       paddr_t *next_table)
{
	pgtable_modifier_ret_t vret  = PGTABLE_MODIFIER_RET_CONTINUE;
	index_t		       level = cur_level;

	const pgtable_level_info_t *cur_level_info = &level_conf[level];
	size_t			    addr_size	   = cur_level_info->addr_size;
	vmaddr_t		    entry_virtual_address =
		entry_start_address(virtual_address, cur_level_info);

	if ((type == VMSA_ENTRY_TYPE_BLOCK) &&
	    ((virtual_address != entry_virtual_address) ||
	     (size != addr_size))) {
		// Split the block into pages
		vret = pgtable_split_block(pgt, virtual_address, size, idx,
					   level, type, stack, margs,
					   next_level, next_virtual_address,
					   next_size, next_table);
	} else {
		// The new mapping will cover this entire range, so we need to
		// unmap existing pages or blocks. The new mappings will then
		// be mapped by the caller.

		pgtable_unmap_modifier_args_t margs2;
		memset(&margs2, 0, sizeof(margs2));
		margs2.partition      = margs->partition;
		margs2.preserved_size = PGTABLE_HYP_UNMAP_PRESERVE_NONE;
		margs2.stage	      = margs->stage;
		margs2.remap_regions[0].is_valid = false;
		margs2.remap_regions[1].is_valid = false;

		// it's a page entry
		unmap_modifier(pgt, virtual_address, addr_size, idx, cur_level,
			       type, stack, &margs2, next_level,
			       next_virtual_address, next_size, false);
		dsb();
		if (margs->stage == PGTABLE_VM_STAGE_2) {
			// flush entire stage 1 tlb
			vm_tlbi_vmalle1();
			dsb();
		}
	}

	return vret;
}

// @brief Modify current entry for mapping the specified virt address to the
// physical address.
//
// This modifier simply focuses on just one entry during the mapping procedure.
// Depends on the type of the entry, it may:
// * directly map the physical address as block/page
// * allocate/setup the page table level, and recursively (up to MAX_LEVEL) to
// handle the mapping. After the current page table level entry setup, it will
// drive @see translation_table_walk to the next entry at the same level.
pgtable_modifier_ret_t
map_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size, index_t idx,
	     index_t cur_level, vmsa_entry_type_t type,
	     stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	     index_t *next_level, vmaddr_t *next_virtual_address,
	     size_t *next_size, paddr_t *next_table)
{
	pgtable_map_modifier_args_t *margs =
		(pgtable_map_modifier_args_t *)data;
	pgtable_modifier_ret_t	    vret = PGTABLE_MODIFIER_RET_CONTINUE;
	error_t			    ret	 = OK;
	const pgtable_level_info_t *cur_level_info     = NULL;
	vmsa_level_table_t *	    cur_table	       = NULL;
	uint64_t		    page_or_block_type = 0U;
	size_t			    addr_size = 0U, level_size = 0U;
	uint64_t		    allowed	     = 0U;
	index_t			    start_level	     = 0;
	bool			    page_block_fence = false;
	index_t			    level	     = cur_level;

	// If entry different from ENTRY_INVALID case, it means that the
	// specified range has already been mapped.
	// If try_map is set, we will abort the mapping operation.
	// If is is not set, we will first unmap and then proceed mapping the
	// range anyways
	if ((type == VMSA_ENTRY_TYPE_BLOCK) || (type == VMSA_ENTRY_TYPE_PAGE) ||
	    (type == VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE)) {
		if (margs->try_map) {
			margs->error		     = ERROR_EXISTING_MAPPING;
			margs->partially_mapped_size = margs->orig_size - size;
			vret = PGTABLE_MODIFIER_RET_ERROR;
			goto out;
		} else {
			if (type == VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE) {
				margs->error = ERROR_EXISTING_MAPPING;
				margs->partially_mapped_size =
					margs->orig_size - size;
				vret = PGTABLE_MODIFIER_RET_ERROR;
				goto out;
			}

			bool only_access;
			only_access = pgtable_maybe_update_access(
				pgt, stack, idx, type, margs, level,
				virtual_address, size, next_virtual_address,
				next_size, next_level);
			if (only_access) {
				goto out;
			}

			vret = pgtable_modify_mapping(
				pgt, virtual_address, size, idx, cur_level,
				type, stack, margs, next_level,
				next_virtual_address, next_size, next_table);

			if (vret != PGTABLE_MODIFIER_RET_STOP) {
				// after modified mapping, this case indicates
				// continue, the unmap modifier removes existing
				// pgtable, it might free upper table level as
				// well. So jump out and redo this mapping at
				// the same size, same virtual address
				*next_virtual_address = virtual_address;
				*next_size	      = size;
			}

			goto out;
		}
	}

	assert(data != NULL);
	assert(pgt != NULL);

	// current level should be mapped
	assert(stack[level].mapped);
	cur_table = stack[level].table;

	cur_level_info = &level_conf[level];
	addr_size      = cur_level_info->addr_size;
	allowed	       = cur_level_info->allowed_types;
	level_size     = size_on_level(virtual_address, size, cur_level_info);

	// page/block type is exclusive for each other
	page_or_block_type = allowed &
			     (VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE);
	if ((addr_size <= level_size) && (page_or_block_type != 0) &&
	    (util_is_baligned(margs->phys, addr_size))) {
		if (margs->new_page_start_level != PGTABLE_INVALID_LEVEL) {
			start_level	 = margs->new_page_start_level;
			page_block_fence = false;
			margs->new_page_start_level = PGTABLE_INVALID_LEVEL;
		} else {
			// if current level is start level, no need to update
			// entry count
			start_level = level > pgt->start_level ? level - 1
							       : level;
			page_block_fence = true;
		}

#if 0
		// FIXME: also need to search forwards for occupied entries
		bool contiguous = map_should_set_cont(
			margs->orig_virtual_address, margs->orig_size,
			virtual_address, level);
#else
		bool contiguous = false;
#endif

		// allowed to map a block
		if (page_or_block_type == VMSA_ENTRY_TYPE_BLOCK) {
			set_block_entry(cur_table, idx, margs->phys,
					margs->upper_attrs, margs->lower_attrs,
					contiguous, page_block_fence);
		} else {
			set_page_entry(cur_table, idx, margs->phys,
				       margs->upper_attrs, margs->lower_attrs,
				       contiguous, page_block_fence);
		}

		// check if need to set all page table levels
		set_pgtables(virtual_address, stack, start_level, level, 1U);

		// update the physical address for next mapping
		margs->phys += addr_size;
		assert(!util_add_overflows(margs->phys, addr_size));
	} else if (allowed & VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE) {
		ret = pgtable_add_table_entry(pgt, margs, level, stack,
					      virtual_address, size, next_level,
					      next_virtual_address, next_size,
					      next_table, true);
		if (ret != OK) {
			vret = PGTABLE_MODIFIER_RET_ERROR;
			goto out;
		}
	} else {
		LOG(ERROR, WARN, "Unexpected condition during mapping:\n");
		LOG(ERROR, WARN,
		    "Mapping pa({:x}) to va({:x}), size({:d}), level({:d})",
		    margs->phys, virtual_address, size, (register_t)level);
		// should not be here
		vret	     = PGTABLE_MODIFIER_RET_ERROR;
		margs->error = ERROR_ARGUMENT_INVALID;
		goto out;
	}

out:
	cur_table = NULL;

	// free all pages if something wrong
	if ((vret == PGTABLE_MODIFIER_RET_ERROR) &&
	    (margs->new_page_start_level != 0)) {
		while (margs->new_page_start_level < level) {
			// all new table level, no need to unmap
			assert(!stack[level].need_unmap);
			partition_free(margs->partition, stack[level].table,
				       pgt->granule_size);
			stack[level].paddr  = 0U;
			stack[level].table  = NULL;
			stack[level].mapped = false;
			level--;
		}
	}

	return vret;
}

// @brief Collect information while walking along the virtual address.
//
// This modifier is actually just a visitor, it accumulates the size of the
// entry/entries, and return to the caller. NOTE that the memory type and
// memory attribute should be the same, or else, it only return the attributes
// of the last entry.
pgtable_modifier_ret_t
lookup_modifier(pgtable_t *pgt, vmsa_general_entry_t cur_entry, index_t level,
		vmsa_entry_type_t type, void *data)
{
	pgtable_lookup_modifier_args_t *margs =
		(pgtable_lookup_modifier_args_t *)data;
	const pgtable_level_info_t *cur_level_info = &level_conf[level];
	pgtable_modifier_ret_t	    vret	   = PGTABLE_MODIFIER_RET_STOP;
	error_t			    ret		   = OK;

	// only handle several cases, valid arguments
	if ((type != VMSA_ENTRY_TYPE_PAGE) && (type != VMSA_ENTRY_TYPE_BLOCK)) {
		LOG(ERROR, WARN,
		    "Invalid argument during lookup. Stop lookup now.\n");
		/* shouldn't be here */
		vret = PGTABLE_MODIFIER_RET_ERROR;
		goto out;
	}

	assert(pgt != NULL);

	ret = get_entry_paddr(cur_level_info, &cur_entry, type, &margs->phys);
	if (ret != OK) {
		LOG(ERROR, WARN,
		    "Failed to get physical address, entry type({:d}) ", type);
		LOG(ERROR, WARN, "entry({:x})\n",
		    vmsa_general_entry_raw(cur_entry));
		vret = PGTABLE_MODIFIER_RET_ERROR;
		goto out;
	}

	margs->entry = cur_entry;

	// set size & return for check
	margs->size = cur_level_info->addr_size;

out:
	return vret;
}

// helper to check entry count from the parent page table level,
// free empty upper levels if needed
void
check_refcount(pgtable_t *pgt, partition_t *partition, vmaddr_t virtual_address,
	       size_t size, index_t upper_level,
	       stack_elem_t stack[PGTABLE_LEVEL_NUM], bool need_dec,
	       size_t preserved_size, index_t *next_level,
	       vmaddr_t *next_virtual_address, size_t *next_size)
{
	const pgtable_level_info_t *cur_level_info = NULL;
	vmsa_level_table_t *	    cur_table	   = NULL;
	index_t			    level	   = upper_level;
	index_t			    cur_idx;
	count_t			    refcount;
	bool			    is_preserved = false;
	stack_elem_t *		    free_list[PGTABLE_LEVEL_NUM];
	index_t			    free_idx = 0;
	bool			    dec	     = need_dec;
	size_t			    walked_size;

	while (level >= pgt->start_level) {
		assert(stack[level].mapped);
		cur_table = stack[level].table;

		cur_level_info = &level_conf[level];
		cur_idx	       = get_index(virtual_address, cur_level_info);
		refcount       = get_table_refcount(cur_table, cur_idx);

		if (dec) {
			// decrease entry count
			refcount--;
			set_table_refcount(cur_table, cur_idx, refcount);
			dec = false;
		}

		if (refcount == 0) {
			is_preserved = is_preserved_table_entry(preserved_size,
								cur_level_info);

			if (is_preserved) {
				break;
			}

			// Make sure the general page table walk does not step
			// into the level that is being freed. The correct step
			// might either be forward one entry (if there are more
			// entries in the current level) or up one level (if
			// this is the last entry in the current level).
			// The following diagram shows the edge case:
			//
			//         Next Entry
			//            +
			//            |
			//            |
			//+-----------v-------------+
			//|           T T           |    *next_level
			//+-----------+-+-----------+
			//            | |
			//      +-----+ +--+
			//      |          |
			//  +---v----+  +--v-+--------+
			//  |      |B|  |  T |      |B|  level
			//  +--------+  +-+--+--------+
			//                |
			//                v
			//              +-+-+------+
			//              | T |      |   freed
			//              +-+-+------+
			//                |
			//                v
			//              +----------+
			//              |P|        |   freed
			//              +----------+
			// Here two levels are freed, the *next entry* is the
			// entry for next iteration.
			*next_level = util_min(*next_level, level);
			// bigger virtual address, further
			*next_virtual_address =
				util_max(*next_virtual_address,
					 step_virtual_address(virtual_address,
							      cur_level_info));
			walked_size = (*next_virtual_address - virtual_address);
			*next_size  = util_max(size, walked_size) - walked_size;

			free_list[free_idx] = &stack[level + 1];
			free_idx++;
			// invalidate current entry
			set_invalid_entry(cur_table, cur_idx);

			dec = true;
		}

		if ((refcount == 0) && (level > 0)) {
			// will decrease it's parent level entry count
			level--;
		} else {
			// break if current level entry count is non-zero
			break;
		}

		cur_table = NULL;
	}

	// free the page table levels at one time, the free will do the fence
	while (free_idx > 0) {
		free_idx--;

		if (free_list[free_idx]->need_unmap) {
			// Only used by unmap, should always need unamp
			partition_phys_unmap(free_list[free_idx]->table,
					     free_list[free_idx]->paddr,
					     pgt->granule_size);
			free_list[free_idx]->need_unmap = false;
		}

		partition_free_phys(partition, free_list[free_idx]->paddr,
				    pgt->granule_size);
		free_list[free_idx]->table  = NULL;
		free_list[free_idx]->paddr  = 0U;
		free_list[free_idx]->mapped = false;
	}

	return;
}

#if 0
static bool
map_should_set_cont(vmaddr_t virtual_address, size_t size,
		    vmaddr_t entry_address, index_t level)
{
	const pgtable_level_info_t *info = &level_conf[level];

	assert(info->contiguous_entry_cnt != 0U);

	size_t	 cont_size  = info->addr_size * info->contiguous_entry_cnt;
	vmaddr_t cont_start = util_balign_down(entry_address, cont_size);

	assert(!util_add_overflows(cont_start, cont_size - 1U));
	vmaddr_t cont_end = cont_start + cont_size - 1U;

	assert(!util_add_overflows(virtual_address, size - 1U));
	vmaddr_t virtual_end = virtual_address + size - 1U;

	return (cont_start >= virtual_address) && (cont_end &= virtual_end);
}
#endif

static bool
unmap_should_clear_cont(vmaddr_t virtual_address, size_t size, index_t level)
{
	const pgtable_level_info_t *info = &level_conf[level];

	assert(info->contiguous_entry_cnt != 0U);

	size_t	 cont_size  = info->addr_size * info->contiguous_entry_cnt;
	vmaddr_t cont_start = util_balign_down(virtual_address, cont_size);

	assert(!util_add_overflows(cont_start, cont_size - 1U));
	vmaddr_t cont_end = cont_start + cont_size - 1U;

	assert(!util_add_overflows(virtual_address, size - 1U));
	vmaddr_t virtual_end = virtual_address + size - 1U;

	return (cont_start < virtual_address) || (cont_end > virtual_end);
}

static void
unmap_clear_cont_bit(vmsa_level_table_t *table, vmaddr_t virtual_address,
		     index_t			       level,
		     vmsa_page_and_block_attrs_entry_t attr_entry,
		     pgtable_unmap_modifier_args_t *   margs)
{
	const pgtable_level_info_t *info = &level_conf[level];

	assert(info->contiguous_entry_cnt != 0U);

	// get index range in current table to clear cont bit
	index_t cur_idx = get_index(virtual_address, info);
	index_t idx_start =
		util_balign_down(cur_idx, info->contiguous_entry_cnt);
	index_t idx_end = (index_t)(idx_start + info->contiguous_entry_cnt - 1);

	// start break-before-make sequence: clear all contiguous entries
	for (index_t idx = idx_start; idx <= idx_end; idx++) {
		set_invalid_entry(table, idx);
	}
	dsb();

	// flush all contiguous entries from TLB (note that the CPU may not
	// implement the contiguous bit at this level, so we are required to
	// flush addresses in all entries)
	vmaddr_t vaddr =
		virtual_address &
		~((util_bit(info->lsb) * info->contiguous_entry_cnt) - 1U);
	for (index_t i = 0; i < info->contiguous_entry_cnt; i++) {
		if (margs->stage == PGTABLE_HYP_STAGE_1) {
			hyp_tlbi_va(vaddr);
		} else {
			vm_tlbi_ipa(vaddr);
		}
		vaddr += info->addr_size;
	}

	// Restore the entries other than cur_idx, with the cont bit cleared
	vmsa_upper_attrs_t upper_attrs =
		vmsa_page_and_block_attrs_entry_get_upper_attrs(&attr_entry);
	vmsa_lower_attrs_t lower_attrs =
		vmsa_page_and_block_attrs_entry_get_lower_attrs(&attr_entry);
	vmsa_common_upper_attrs_t upper_attrs_bitfield =
		vmsa_common_upper_attrs_cast(upper_attrs);
	assert(vmsa_common_upper_attrs_get_cont(&upper_attrs_bitfield));
	vmsa_common_upper_attrs_set_cont(&upper_attrs_bitfield, false);
	upper_attrs = vmsa_common_upper_attrs_raw(upper_attrs_bitfield);
	vmsa_page_and_block_attrs_entry_set_upper_attrs(&attr_entry,
							upper_attrs);

	vmsa_general_entry_t entry = vmsa_general_entry_cast(
		vmsa_page_and_block_attrs_entry_raw(attr_entry));

	const vmsa_entry_type_t page_or_block_type =
		info->allowed_types &
		(VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE);
	paddr_t entry_phys;
	(void)get_entry_paddr(info, &entry, page_or_block_type, &entry_phys);
	entry_phys &=
		~((util_bit(info->lsb) * info->contiguous_entry_cnt) - 1U);

	for (index_t idx = idx_start; idx <= idx_end; idx++) {
		if (idx == cur_idx) {
			// This should be left invalid
		} else if (page_or_block_type == VMSA_ENTRY_TYPE_BLOCK) {
			set_block_entry(table, idx, entry_phys, upper_attrs,
					lower_attrs, false, false);
		} else {
			set_page_entry(table, idx, entry_phys, upper_attrs,
				       lower_attrs, false, false);
		}
		entry_phys += info->addr_size;
	}
}

bool
unmap_check_start(vmaddr_t virtual_address, vmsa_general_entry_t cur_entry,
		  vmsa_entry_type_t type, index_t level,
		  pgtable_unmap_modifier_args_t *margs)
{
	const pgtable_level_info_t *	  cur_level_info = NULL;
	vmaddr_t			  entry_address;
	vmsa_page_and_block_attrs_entry_t attr_entry;
	vmsa_lower_attrs_t		  lower_attrs;
	vmsa_upper_attrs_t		  upper_attrs;
	paddr_t				  entry_phys;
	bool				  ret = false;

	assert((type == VMSA_ENTRY_TYPE_BLOCK) ||
	       (type == VMSA_ENTRY_TYPE_PAGE));

	cur_level_info = &level_conf[level];
	entry_address  = entry_start_address(virtual_address, cur_level_info);

	// only handle the case when the start is in the block entry range
	if (virtual_address <= entry_address) {
		ret = false;
		goto out;
	}

	attr_entry = vmsa_page_and_block_attrs_entry_cast(
		vmsa_general_entry_raw(cur_entry));
	(void)get_entry_paddr(cur_level_info, &cur_entry, type, &entry_phys);
	lower_attrs =
		vmsa_page_and_block_attrs_entry_get_lower_attrs(&attr_entry);
	upper_attrs =
		vmsa_page_and_block_attrs_entry_get_upper_attrs(&attr_entry);

	// handle block size change, only apply for block entry
	if (type == VMSA_ENTRY_TYPE_BLOCK) {
		// record address range we should keep after BBM
		margs->remap_regions[0].is_valid	= true;
		margs->remap_regions[0].virtual_address = entry_address;
		margs->remap_regions[0].phys		= entry_phys;
		margs->remap_regions[0].size = virtual_address - entry_address;
		margs->remap_regions[0].lower_attrs = lower_attrs;
		margs->remap_regions[0].upper_attrs = upper_attrs;

		ret = true;
	}
out:
	return ret;
}

bool
unmap_check_end(vmaddr_t virtual_address, size_t size,
		vmsa_general_entry_t cur_entry, vmsa_entry_type_t type,
		index_t level, pgtable_unmap_modifier_args_t *margs)
{
	const pgtable_level_info_t *	  cur_level_info = NULL;
	vmaddr_t			  entry_address;
	size_t				  level_size;
	vmsa_page_and_block_attrs_entry_t attr_entry;
	vmsa_lower_attrs_t		  lower_attrs;
	vmsa_upper_attrs_t		  upper_attrs;
	paddr_t				  entry_phys;
	bool				  ret = false;

	assert((type == VMSA_ENTRY_TYPE_BLOCK) ||
	       (type == VMSA_ENTRY_TYPE_PAGE));

	cur_level_info = &level_conf[level];
	level_size     = size_on_level(virtual_address, size, cur_level_info);
	entry_address  = entry_start_address(virtual_address, cur_level_info);

	// only handle the case when the end is in the block entry range
	if (util_add_overflows(virtual_address, level_size) ||
	    ((virtual_address + level_size - 1U) >=
	     (entry_address + cur_level_info->addr_size - 1U))) {
		ret = false;
		goto out;
	}

	attr_entry = vmsa_page_and_block_attrs_entry_cast(
		vmsa_general_entry_raw(cur_entry));
	(void)get_entry_paddr(cur_level_info, &cur_entry, type, &entry_phys);
	lower_attrs =
		vmsa_page_and_block_attrs_entry_get_lower_attrs(&attr_entry);
	upper_attrs =
		vmsa_page_and_block_attrs_entry_get_upper_attrs(&attr_entry);

	// handle block size change, only apply for block entry
	if (type == VMSA_ENTRY_TYPE_BLOCK) {
		margs->remap_regions[1].is_valid = true;
		margs->remap_regions[1].virtual_address =
			virtual_address + level_size;

		assert(!util_add_overflows(entry_phys, virtual_address -
							       entry_address +
							       level_size));

		margs->remap_regions[1].phys =
			entry_phys +
			(virtual_address - entry_address + level_size);
		margs->remap_regions[1].size = entry_address - virtual_address +
					       cur_level_info->addr_size -
					       level_size;
		margs->remap_regions[1].lower_attrs = lower_attrs;
		margs->remap_regions[1].upper_attrs = upper_attrs;

		ret = true;
	}
out:
	return ret;
}

// @brief Unmap the current entry if possible.
//
// This modifier will try to:
// * Decrease the reference count (entry count) of current entry. If it's
// allowed (not preserved) and possible (ref count == 0), it will free the
// next page table level. In this case, It will guide @see
// translation_table_walk to step onto the next entry at the same level, and
// update the size as well.
// * Invalidate current entry.
pgtable_modifier_ret_t
unmap_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
	       index_t idx, index_t level, vmsa_entry_type_t type,
	       stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
	       index_t *next_level, vmaddr_t *next_virtual_address,
	       size_t *next_size, bool only_matching)
{
	const pgtable_level_info_t *   cur_level_info = NULL;
	pgtable_unmap_modifier_args_t *margs =
		(pgtable_unmap_modifier_args_t *)data;
	pgtable_modifier_ret_t vret	 = PGTABLE_MODIFIER_RET_CONTINUE;
	vmsa_level_table_t *   cur_table = NULL;
	vmsa_general_entry_t   cur_entry;
	bool		       need_dec = false;

	assert(pgt != NULL);

	// current level should be mapped
	assert(stack[level].mapped);
	cur_table = stack[level].table;

	cur_level_info = &level_conf[level];
	// FIXME: if cur_entry is not used, remove it
	cur_entry = get_entry(cur_table, idx);

	// Set invalid entry and unmap/free the page level
	// NOTE: it's possible to forecast if we can free the whole sub page
	// table levels when we got a page table level entry, but it's just for
	// certain cases (especially the last second page)

	// No need to decrease entry count in upper page table level by default,
	// for INVALID entry.
	need_dec = false;

	if (only_matching && ((type == VMSA_ENTRY_TYPE_BLOCK) ||
			      (type == VMSA_ENTRY_TYPE_PAGE))) {
		// Check if it is mapped to different phys_addr than expected
		// If so, do not unmap this address
		paddr_t phys_addr;

		(void)get_entry_paddr(cur_level_info, &cur_entry, type,
				      &phys_addr);
		if ((phys_addr < margs->phys) ||
		    (phys_addr > (margs->phys + margs->size - 1U))) {
			goto out;
		}
	}

	if (type == VMSA_ENTRY_TYPE_BLOCK) {
		unmap_check_start(virtual_address, cur_entry, type, level,
				  margs);

		unmap_check_end(virtual_address, size, cur_entry, type, level,
				margs);
	}

	if ((type == VMSA_ENTRY_TYPE_BLOCK) || (type == VMSA_ENTRY_TYPE_PAGE)) {
		vmsa_upper_attrs_t upper_attrs = get_upper_attr(cur_entry);
		vmsa_common_upper_attrs_t upper_attrs_bitfield =
			vmsa_common_upper_attrs_cast(upper_attrs);

		// clear contiguous bit if needed
		if (vmsa_common_upper_attrs_get_cont(&upper_attrs_bitfield) &&
		    unmap_should_clear_cont(virtual_address, size, level)) {
			vmsa_page_and_block_attrs_entry_t attr_entry =
				vmsa_page_and_block_attrs_entry_cast(
					vmsa_general_entry_raw(cur_entry));
			unmap_clear_cont_bit(cur_table, virtual_address, level,
					     attr_entry, margs);
		} else {
			set_invalid_entry(cur_table, idx);

			// need to decrease entry count for this table level
			need_dec = true;

			dsb();
			if (margs->stage == PGTABLE_HYP_STAGE_1) {
				hyp_tlbi_va(virtual_address);
			} else {
				vm_tlbi_ipa(virtual_address);
			}
		}
	} else {
		assert(type == VMSA_ENTRY_TYPE_INVALID);
	}

	if (level != pgt->start_level) {
		check_refcount(pgt, margs->partition, virtual_address, size,
			       level - 1, stack, need_dec,
			       margs->preserved_size, next_level,
			       next_virtual_address, next_size);
	}

out:
	cur_table = NULL;

	return vret;
}

// @brief Pre-allocate specified page table level for certain virtual address
// range.
//
// This modifier just allocate/adds some page table level using the specified
// partition. The usage of this call is to guarantee the currently used page
// table level is still valid after release certain partition.
pgtable_modifier_ret_t
prealloc_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		  index_t level, vmsa_entry_type_t type,
		  stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		  index_t *next_level, vmaddr_t *next_virtual_address,
		  size_t *next_size, paddr_t *next_table)
{
	pgtable_prealloc_modifier_args_t *margs =
		(pgtable_prealloc_modifier_args_t *)data;
	pgtable_modifier_ret_t	    vret = PGTABLE_MODIFIER_RET_CONTINUE;
	error_t			    ret	 = OK;
	const pgtable_level_info_t *cur_level_info = NULL;
	paddr_t			    new_pgt_paddr;
	size_t			    addr_size = 0U, level_size = 0U;
	vmsa_level_table_t *	    new_pgt = NULL;

	assert(type == VMSA_ENTRY_TYPE_INVALID);
	assert(data != NULL);
	assert(pgt != NULL);

	assert(stack[level].mapped);

	cur_level_info = &level_conf[level];
	addr_size      = cur_level_info->addr_size;
	level_size     = size_on_level(virtual_address, size, cur_level_info);

	// FIXME: since all size are at least page aligned, level_size should also
	// be page aligned, add assert here

	if (addr_size <= level_size) {
		// hook pages to the existing page table levels
		// for the case that the root level is the level need to
		// preserve, it just return since new_page_start_level is not
		// set
		if (margs->new_page_start_level != PGTABLE_INVALID_LEVEL) {
			set_pgtables(virtual_address, stack,
				     margs->new_page_start_level, level, 0U);

			margs->new_page_start_level = PGTABLE_INVALID_LEVEL;
		}

		// go to next entry at the same level
		goto out;
	} else {
		// if (addr_size > level_size)
		ret = alloc_level_table(margs->partition, pgt->granule_size,
					pgt->granule_size, &new_pgt_paddr,
					&new_pgt);
		if (ret != OK) {
			LOG(ERROR, WARN, "Failed to allocate page.\n");
			vret	     = PGTABLE_MODIFIER_RET_ERROR;
			margs->error = ret;
			goto out;
		}

		if (margs->new_page_start_level == PGTABLE_INVALID_LEVEL) {
			margs->new_page_start_level =
				level > pgt->start_level ? level - 1 : level;
		}

		stack[level + 1].paddr	= new_pgt_paddr;
		stack[level + 1].table	= new_pgt;
		stack[level + 1].mapped = true;

		// step into the next sub level, with nothing stepped
		*next_virtual_address = virtual_address;
		*next_size	      = size;
		*next_level	      = level + 1;
		*next_table	      = new_pgt_paddr;
	}

out:
	return vret;
}

#ifndef NDEBUG
static pgtable_modifier_ret_t
dump_modifier(vmaddr_t virtual_address, size_t size,
	      stack_elem_t stack[PGTABLE_LEVEL_NUM], index_t idx, index_t level,
	      vmsa_entry_type_t type)
{
	const pgtable_level_info_t *cur_level_info = NULL;
	vmsa_level_table_t *	    cur_table	   = NULL;
	vmsa_general_entry_t	    cur_entry;
	uint64_t *		    entry_val = (uint64_t *)&cur_entry;
	paddr_t			    p;
	count_t			    refcount;
	vmaddr_t		    cur_virtual_address;
	const char *		    msg_type = "[X]";
	char			    indent[16];
	index_t			    i;
	pgtable_modifier_ret_t	    vret      = PGTABLE_MODIFIER_RET_CONTINUE;
	size_t			    addr_size = 0U;

	if (size == 0L) {
		vret = PGTABLE_MODIFIER_RET_STOP;
		goto out;
	}

	assert(stack[level].mapped);
	cur_table = stack[level].table;

	cur_level_info = &level_conf[level];
	addr_size      = cur_level_info->addr_size;
	cur_entry      = get_entry(cur_table, idx);
	refcount       = get_table_refcount(cur_table, idx);

	// No need to care if paddr is wrong
	(void)get_entry_paddr(cur_level_info, &cur_entry, type, &p);

	// FIXME: check if cur_virtual_address is right
	cur_virtual_address = set_index(virtual_address, cur_level_info, idx) &
			      (~util_mask(cur_level_info->lsb));

	// FIXME: add assert for sizeof(indent) and level
	indent[0] = '|';
	for (i = 0; i < level; i++) {
		indent[i + 1] = '\t';
	}
	indent[i + 1] = 0;

	switch (type) {
	case VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE:
		msg_type = "[Table]";
		break;
	case VMSA_ENTRY_TYPE_BLOCK:
		msg_type = "[Block]";
		break;
	case VMSA_ENTRY_TYPE_PAGE:
		msg_type = "[Page]";
		break;
	case VMSA_ENTRY_TYPE_RESERVED:
		msg_type = "[Reserved]";
		break;
	case VMSA_ENTRY_TYPE_ERROR:
		msg_type = "[Error]";
		break;
	case VMSA_ENTRY_TYPE_INVALID:
	default:
		msg_type = "[Invalid]";
		break;
	}

	switch (type) {
	case VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE:
		LOG(DEBUG, INFO,
		    "{:s}->{:s} entry[{:#x}] virtual_address({:#x})",
		    (register_t)indent, (register_t)msg_type, *entry_val,
		    cur_virtual_address);
		LOG(DEBUG, INFO,
		    "{:s}phys({:#x}) idx({:d}) cnt({:d}) level({:d})",
		    (register_t)indent, (register_t)p, (register_t)idx,
		    (register_t)refcount, (register_t)cur_level_info->level);
		LOG(DEBUG, INFO, "{:s}addr_size({:#x})", (register_t)indent,
		    (register_t)addr_size);
		break;
	case VMSA_ENTRY_TYPE_BLOCK:
	case VMSA_ENTRY_TYPE_PAGE:
		LOG(DEBUG, INFO,
		    "{:s}->{:s} entry[{:#x}] virtual_address({:#x})",
		    (register_t)indent, (register_t)msg_type,
		    (register_t)*entry_val, (register_t)cur_virtual_address);
		LOG(DEBUG, INFO, "{:s}phys({:#x}) idx({:d}) level({:d})",
		    (register_t)indent, (register_t)p, (register_t)idx,
		    (register_t)cur_level_info->level);
		LOG(DEBUG, INFO, "{:s}addr_size({:#x})", (register_t)indent,
		    (register_t)addr_size);
		break;
	case VMSA_ENTRY_TYPE_INVALID:
		break;
	case VMSA_ENTRY_TYPE_RESERVED:
	case VMSA_ENTRY_TYPE_ERROR:
	default:
		LOG(DEBUG, INFO, "{:s}->{:s} virtual_address({:#x}) idx({:d})",
		    (register_t)indent, (register_t)msg_type,
		    (register_t)cur_virtual_address, (register_t)idx);
		break;
	}

out:
	cur_table = NULL;

	return vret;
}

static pgtable_modifier_ret_t
external_modifier(pgtable_t *pgt, vmaddr_t virtual_address, size_t size,
		  index_t idx, index_t level, vmsa_entry_type_t type,
		  stack_elem_t stack[PGTABLE_LEVEL_NUM], void *data,
		  index_t *next_level, vmaddr_t *next_virtual_address,
		  size_t *next_size, paddr_t *next_table)
{
	ext_modifier_args_t *  margs	 = (ext_modifier_args_t *)data;
	void *		       func_data = margs->data;
	pgtable_modifier_ret_t ret	 = PGTABLE_MODIFIER_RET_STOP;

	if (margs->func != NULL) {
		ret = margs->func(pgt, virtual_address, size, idx, level, type,
				  stack, func_data, next_level,
				  next_virtual_address, next_size, next_table);
	}

	return ret;
}

#endif

// @brief Generic code to walk through translation table.
//
// This function is generic for stage 1 and stage 2 translation table walking.
// Depends on the specified event, it triggers proper function (modifier) to
// handle current table entry.
// When the modifier function is called, it can get the following information:
// * Current start virtual address
// * Current entry type
// * Current page table level physical address
// * Current page table level
//   There are two kinds of level. One level is 0 based. The other concept of
//   level is based on ARM reference manual. It's defined @see level_type.
//   For example, the 64K granule page table with LPA feature does not have
//   the first level.
//   The previous level is used to control the loop, and the second level is
//   used to manipulate page table level stack.
// * Remaining size for the walking
//   If the address range specified by current start virtual address and
//   remaining size is fully visited, this function will return.
// * The current page table control structure
// * The page table level physical address stack
//   This stack can be used to get back to upper level.
// * A private pointer which modifier can interpret by itself, it remains the
//   same during the walking procedure.
//
// The modifier function can also control the walking by set:
// * Next page table physical address
// * Next level
// * Next start virtual address
// * Next remaining size
// And these four variables can fully control the flow of walking. Also, the
// return value of the modifier function provides a simple way to
// stop/continue, or report errors.
//
// The walking process is surely ended:
// * Finished visiting the specified address range.
// * Reach the maximum virtual address.
// But when the modifier changed internal variables, it's modifier's
// responsibility to make sure it can ended.
//
// @param pgt page table control structure.
// @param root_pa the physical address of the page table we started to walk.
// It's allowed to use the mid level page tables to do the walking.
// @param root the virtual address of page table.
// @param virtual_address the start virtual address.
// @param size the size of virtual address it needs to visit.
// @param event specifies the modifier to call.
// @param expected specifies the table entry type the modifier needs.
// @param data specifier an opaque data structure specific for modifier.
// @return true if the finished the walking without error. Or false indicates
// the failure.
bool
translation_table_walk(pgtable_t *pgt, vmaddr_t virtual_address,
		       size_t virtual_address_size,
		       pgtable_translation_table_walk_event_t event,
		       pgtable_entry_types_t expected, void *data)
{
	paddr_t		     root_pa = pgt->root_pgtable;
	vmsa_level_table_t * root    = pgt->root;
	index_t		     level   = pgt->start_level;
	index_t		     prev_level;
	index_t		     prev_idx;
	vmaddr_t	     prev_virtual_address;
	size_t		     prev_size;
	vmsa_general_entry_t prev_entry;
	vmsa_entry_type_t    prev_type;

	// loop control variable
	index_t	 cur_level	     = level;
	paddr_t	 cur_table_paddr     = 0U;
	vmaddr_t cur_virtual_address = virtual_address;

	const pgtable_level_info_t *cur_level_info = NULL;
	index_t			    cur_idx;
	stack_elem_t		    stack[PGTABLE_LEVEL_NUM];
	vmsa_level_table_t *	    cur_table = NULL;
	vmsa_general_entry_t	    cur_entry;
	vmsa_entry_type_t	    cur_type;
	size_t			    cur_size = virtual_address_size;
	// ret: indicates whether walking is successful or not.
	// done: indicates the walking got a stop sign and need to return. It
	// can be changed by modifier.
	// ignores the modifier.
	bool ret = false, done = false;

	memset(stack, 0, sizeof(stack));
	stack[level].paddr  = root_pa;
	stack[level].table  = root;
	stack[level].mapped = true;

	for (cur_level = level;
	     cur_level < (index_t)util_array_size(level_conf);) {
		cur_level_info = &level_conf[cur_level];
		cur_idx	       = get_index(cur_virtual_address, cur_level_info);

		if (cur_level_info->is_offset) {
			// Arrived offset segment, mapping is supposed to
			// be finished
			LOG(ERROR, WARN,
			    "Stepped into the leaf, shouldn't be here.\n");
			ret = true;
			goto out;
		}

		cur_table_paddr = stack[cur_level].paddr;
		if (stack[cur_level].mapped) {
			cur_table = stack[cur_level].table;
		} else {
			cur_table = (vmsa_level_table_t *)partition_phys_map(
				cur_table_paddr, pgt->granule_size);
			if (cur_table == NULL) {
				LOG(ERROR, WARN, "Failed to map{:#x}.\n",
				    cur_table_paddr);
				ret = false;
				goto out;
			}

			stack[cur_level].table	    = cur_table;
			stack[cur_level].mapped	    = true;
			stack[cur_level].need_unmap = true;
		}

		cur_entry = get_entry(cur_table, cur_idx);
		cur_type  = get_entry_type(&cur_entry, cur_level_info);

		// record the argument for modifier
		prev_virtual_address = cur_virtual_address;
		prev_level	     = cur_level;
		prev_idx	     = cur_idx;
		prev_entry	     = cur_entry;
		prev_type	     = cur_type;
		prev_size	     = cur_size;

		switch (cur_type) {
		case VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE:
			cur_level++;
			assert(cur_level < PGTABLE_LEVEL_NUM);
			// FIXME: should we simplify the signature of this call?
			if (OK != get_entry_paddr(cur_level_info, &cur_entry,
						  cur_type, &cur_table_paddr)) {
				// something wrong
				LOG(ERROR, WARN,
				    "Failed to get physical address: ");
				LOG(ERROR, WARN, "entry({:#x})\n",
				    vmsa_general_entry_raw(cur_entry));
				ret = false;
				goto out;
			}
			stack[cur_level].paddr	= cur_table_paddr;
			stack[cur_level].mapped = false;
			stack[cur_level].table	= NULL;
			break;

		case VMSA_ENTRY_TYPE_INVALID:
			// for invalid entry, it must be handled by modifier.
			// Also by default, the next entry for the walking
			// is simply set to the next entry. The modifier should
			// guide the walking to go to the proper next entry.
			// Unless the modifier asks for continue the walking,
			// the walking process must stopped by default.
		case VMSA_ENTRY_TYPE_PAGE:
		case VMSA_ENTRY_TYPE_BLOCK:
			// update virt address to visit next entry
			cur_virtual_address = step_virtual_address(
				cur_virtual_address, cur_level_info);
			size_t step_size =
				(cur_virtual_address - prev_virtual_address);

			if (cur_size >= step_size) {
				cur_size -= step_size;
			} else {
				cur_size = 0U;
			}

			// If we're at the lowest level
			if (cur_level_info->allowed_types ==
			    VMSA_ENTRY_TYPE_PAGE) {
				if (prev_size < cur_level_info->addr_size) {
					// wrong, size must be at least multiple
					// of page
					ret = false;
					break;
				}
			}

			if (cur_size == 0) {
				// the whole walk is done, but modifier can
				// still ask for loop by changing the size
				done = true;
				ret  = true;
			} else {
				done = false;
				ret  = true;
			}

			while (cur_idx == cur_level_info->entry_cnt - 1) {
				if (cur_level == pgt->start_level) {
					done = true;
					break;
				} else {
					cur_level--;
				}

				cur_level_info = &level_conf[cur_level];
				// cur_virtual_address is already stepped, use
				// previous one to check
				cur_idx = get_index(prev_virtual_address,
						    cur_level_info);
			}
			break;

		case VMSA_ENTRY_TYPE_ERROR:
		case VMSA_ENTRY_TYPE_RESERVED:
		default:
			// shouldn't be here
			ret = false;
			goto out;
		}

		cur_table = NULL;

		if ((prev_type & expected) != 0U) {
			pgtable_modifier_ret_t vret;

			switch (event) {
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_MMAP:
				vret = map_modifier(
					pgt, prev_virtual_address, prev_size,
					prev_idx, prev_level, prev_type, stack,
					data, &cur_level, &cur_virtual_address,
					&cur_size, &cur_table_paddr);
				break;
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_UNMAP:
				vret = unmap_modifier(pgt, prev_virtual_address,
						      prev_size, prev_idx,
						      prev_level, prev_type,
						      stack, data, &cur_level,
						      &cur_virtual_address,
						      &cur_size, false);
				break;
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_UNMAP_MATCH:
				vret = unmap_modifier(pgt, prev_virtual_address,
						      prev_size, prev_idx,
						      prev_level, prev_type,
						      stack, data, &cur_level,
						      &cur_virtual_address,
						      &cur_size, true);
				break;
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_LOOKUP:
				vret = lookup_modifier(pgt, prev_entry,
						       prev_level, prev_type,
						       data);
				break;
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_PREALLOC:
				vret = prealloc_modifier(
					pgt, prev_virtual_address, prev_size,
					prev_level, prev_type, stack, data,
					&cur_level, &cur_virtual_address,
					&cur_size, &cur_table_paddr);
				break;
#ifndef NDEBUG
			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_DUMP:
				vret = dump_modifier(prev_virtual_address,
						     prev_size, stack, prev_idx,
						     prev_level, prev_type);
				break;

			case PGTABLE_TRANSLATION_TABLE_WALK_EVENT_EXTERNAL:
				vret = external_modifier(
					pgt, prev_virtual_address, prev_size,
					prev_idx, prev_level, prev_type, stack,
					data, &cur_level, &cur_virtual_address,
					&cur_size, &cur_table_paddr);
				break;
#endif
			default:
				vret = PGTABLE_MODIFIER_RET_ERROR;
				break;
			}

			if (vret == PGTABLE_MODIFIER_RET_STOP) {
				ret  = true;
				done = true;
			} else if (vret == PGTABLE_MODIFIER_RET_ERROR) {
				ret  = false;
				done = true;
			} else if (vret == PGTABLE_MODIFIER_RET_CONTINUE) {
				// It's modifier's responsibility to work around
				// the walk error if it wishes to continue
				ret  = true;
				done = false;
			} else {
				// unknown return, just stop
				done = true;
				ret  = false;
			}
		}

		while (prev_level > cur_level) {
			// Discard page table level which is not used. If
			// modifier changes stack, it's modifier's
			// responsibility to unmap & maintain correct stack
			// status.
			// Since it's possible for modifier to unmap the table
			// level, need to double check if need to unmap the
			// levels here.
			if (!stack[prev_level].mapped) {
				prev_level--;
				continue;
			}

			if (stack[prev_level].need_unmap) {
				partition_phys_unmap(stack[prev_level].table,
						     stack[prev_level].paddr,
						     pgt->granule_size);
				stack[prev_level].need_unmap = false;
			}
			stack[prev_level].table	 = NULL;
			stack[prev_level].paddr	 = 0U;
			stack[prev_level].mapped = false;
			prev_level--;
		}

		// only next table should continue the loop
		if (done || (cur_size == 0L)) {
			break;
		}
	}
out:
	while (cur_level > pgt->start_level) {
		if (stack[cur_level].mapped && stack[cur_level].need_unmap) {
			partition_phys_unmap(stack[cur_level].table,
					     stack[cur_level].paddr,
					     pgt->granule_size);
			stack[cur_level].need_unmap = false;
		}
		stack[cur_level].mapped = false;
		stack[cur_level].table	= NULL;
		cur_level--;
	}

	return ret;
}

static get_start_level_info_ret_t
get_start_level_info(const pgtable_level_info_t *infos, index_t msb)
{
	get_start_level_info_ret_t ret = { .level = 0U, .size = 0UL };

	uint8_t			    level      = 0;
	const pgtable_level_info_t *level_info = infos;

	for (level = 0; level < PGTABLE_LEVEL_NUM; level++) {
		if ((msb <= level_info->msb) && (msb >= level_info->lsb)) {
			break;
		}
		level_info++;
	}
	assert(level < PGTABLE_LEVEL_NUM);

	size_t entry_cnt = 1UL << (msb - level_info->lsb + 1);

	ret.level = level;
	ret.size  = sizeof(vmsa_general_entry_t) * entry_cnt;

	return ret;
}

void
pgtable_handle_boot_cold_init(void)
{
	error_t	      ret = OK;
	index_t	      top_msb;
	index_t	      bottom_msb;
	const count_t page_shift     = SHIFT_4K;
	const size_t  max_va_bit_cnt = 48;
	partition_t * partition	     = partition_get_private();

#if !ARCH_AARCH64_USE_VHE
#error VHE is currently assumed
#endif
	spinlock_init(&hyp_pgtable.lock);

	// FIXME: refine with more configurable code
	hyp_pgtable.top_control.granule_size = 1UL << page_shift;
	hyp_pgtable.top_control.address_bits = HYP_ASPACE_HIGH_BITS;
	top_msb				     = HYP_ASPACE_HIGH_BITS - 1;
	// FIXME: change to static check (with constant?)??
	// Might be better to use hyp_pgtable.top_control.address_bits
	assert((HYP_ASPACE_HIGH_BITS != level_conf[0].msb + 1) ||
	       (HYP_ASPACE_HIGH_BITS != level_conf[1].msb + 1) ||
	       (HYP_ASPACE_HIGH_BITS != level_conf[2].msb + 1) ||
	       (HYP_ASPACE_HIGH_BITS != level_conf[3].msb + 1));

	hyp_pgtable.bottom_control.granule_size = 1UL << page_shift;
	hyp_pgtable.bottom_control.address_bits = HYP_ASPACE_LOW_BITS;
	bottom_msb				= HYP_ASPACE_LOW_BITS - 1;

	assert((HYP_ASPACE_LOW_BITS != level_conf[0].msb + 1) ||
	       (HYP_ASPACE_LOW_BITS != level_conf[1].msb + 1) ||
	       (HYP_ASPACE_LOW_BITS != level_conf[2].msb + 1) ||
	       (HYP_ASPACE_LOW_BITS != level_conf[3].msb + 1));

	// NOTE: assume LVA is not enabled, and not use va tag
	hyp_pgtable.top_mask = ~segment_mask(max_va_bit_cnt, 0);

	// update level info based on virtual_address bits
	get_start_level_info_ret_t top_info =
		get_start_level_info(level_conf, top_msb);
	hyp_pgtable.top_control.start_level	 = top_info.level;
	hyp_pgtable.top_control.start_level_size = top_info.size;

	get_start_level_info_ret_t bottom_info =
		get_start_level_info(level_conf, bottom_msb);
	hyp_pgtable.bottom_control.start_level	    = bottom_info.level;
	hyp_pgtable.bottom_control.start_level_size = bottom_info.size;

#if defined(HOST_TEST)
	// allocate the top page table
	ret = alloc_level_table(partition, top_info.size,
				util_max(top_info.size, VMSA_TABLE_MIN_ALIGN),
				&hyp_pgtable.top_control.root_pgtable,
				&hyp_pgtable.top_control.root);
	if (ret != OK) {
		LOG(ERROR, WARN, "Failed to allocate high page table level.\n");
		goto out;
	}
#else
	hyp_pgtable.top_control.root =
		(vmsa_level_table_t *)&aarch64_pt_ttbr1_level1;
	hyp_pgtable.top_control.root_pgtable = partition_virt_to_phys(
		partition, (uintptr_t)hyp_pgtable.top_control.root);
#endif

	// allocate the root page table
	ret = alloc_level_table(partition, bottom_info.size,
				util_max(bottom_info.size,
					 VMSA_TABLE_MIN_ALIGN),
				&hyp_pgtable.bottom_control.root_pgtable,
				&hyp_pgtable.bottom_control.root);
	if (ret != OK) {
		LOG(ERROR, WARN,
		    "Failed to allocate bottom page table level.\n");
		goto out;
	}

	ttbr0_phys = hyp_pgtable.bottom_control.root_pgtable;

	// activate the lower address space now for cold-boot
	pgtable_handle_boot_runtime_warm_init();

out:
	if (ret != OK) {
#if defined(HOST_TEST)
		if (hyp_pgtable.top_control.root_pgtable != 0U) {
			partition_free(partition, hyp_pgtable.top_control.root,
				       hyp_pgtable.top_control.granule_size);
			hyp_pgtable.top_control.root = NULL;
		}
#endif

		if (hyp_pgtable.bottom_control.root_pgtable != 0U) {
			partition_free(partition,
				       hyp_pgtable.bottom_control.root,
				       hyp_pgtable.bottom_control.granule_size);
			hyp_pgtable.bottom_control.root = NULL;
		}

		panic("Failed to initialize hypervisor root page-table");
	}
}

#if !defined(HOST_TEST)
void
pgtable_handle_boot_runtime_warm_init()
{
	TTBR0_EL2_t ttbr0_val = TTBR0_EL2_default();
	TTBR0_EL2_set_BADDR(&ttbr0_val, ttbr0_phys);
	TTBR0_EL2_set_CnP(&ttbr0_val, true);

	TCR_EL2_E2H1_t tcr_val = register_TCR_EL2_E2H1_read();
	TCR_EL2_E2H1_set_T0SZ(&tcr_val, 64U - HYP_ASPACE_LOW_BITS);
	TCR_EL2_E2H1_set_EPD0(&tcr_val, false);
	TCR_EL2_E2H1_set_ORGN0(&tcr_val, TCR_RGN_NORMAL_WB_RA_WA);
	TCR_EL2_E2H1_set_IRGN0(&tcr_val, TCR_RGN_NORMAL_WB_RA_WA);
	TCR_EL2_E2H1_set_SH0(&tcr_val, TCR_SH_INNER);
	TCR_EL2_E2H1_set_TG0(&tcr_val, TCR_TG0_4KB);

	register_TTBR0_EL2_write_barrier(ttbr0_val);
	register_TCR_EL2_E2H1_write_barrier(tcr_val);

	asm_context_sync_fence();
}
#endif

#if defined(HOST_TEST)
void
pgtable_hyp_destroy(partition_t *partition)
{
	vmaddr_t virtual_address = 0x0U;
	size_t	 size		 = 0x0U;

	assert(partition != NULL);

	// we should unmap everything
	virtual_address = 0x0U;
	size		= 1UL << hyp_pgtable.bottom_control.address_bits;
	pgtable_hyp_unmap(partition, virtual_address, size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	virtual_address = ~util_mask(hyp_pgtable.top_control.address_bits);
	size		= 1UL << hyp_pgtable.top_control.address_bits;
	pgtable_hyp_unmap(partition, virtual_address, size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	// free top level page table
	partition_free(partition, hyp_pgtable.top_control.root,
		       hyp_pgtable.top_control.granule_size);
	hyp_pgtable.top_control.root = NULL;
	partition_free(partition, hyp_pgtable.bottom_control.root,
		       hyp_pgtable.bottom_control.granule_size);
	hyp_pgtable.bottom_control.root = NULL;

	memset(&hyp_pgtable, 0, sizeof(hyp_pgtable));
}
#endif

bool
pgtable_hyp_lookup(uintptr_t virtual_address, paddr_t *mapped_base,
		   size_t *mapped_size, pgtable_hyp_memtype_t *mapped_memtype,
		   pgtable_access_t *mapped_access)
{
	bool			       walk_ret = false;
	pgtable_lookup_modifier_args_t margs;
	pgtable_entry_types_t	       entry_types;
	vmsa_upper_attrs_t	       upper_attrs;
	vmsa_lower_attrs_t	       lower_attrs;
	pgtable_t *		       pgt = NULL;

	assert(mapped_base != NULL);
	assert(mapped_size != NULL);
	assert(mapped_memtype != NULL);
	assert(mapped_access != NULL);

	if (is_hyp_top_virtual_address(virtual_address)) {
		pgt = &hyp_pgtable.top_control;
	} else {
		pgt = &hyp_pgtable.bottom_control;
	}

	assert(addr_check(virtual_address, pgt->address_bits));

	memset(&margs, 0, sizeof(margs));
	entry_types = VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE;
	// just try to lookup a page, but if it's a block, the modifier will
	// stop the walk and return success
	walk_ret = translation_table_walk(
		pgt, virtual_address, pgt->granule_size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_LOOKUP, entry_types,
		&margs);

	if (margs.size == 0U) {
		// Return error (not-mapped) if lookup found no pages.
		walk_ret = false;
	}

	if (walk_ret) {
		*mapped_base = margs.phys;
		*mapped_size = margs.size;

		// FIXME: we can simplify below 4 line
		lower_attrs	= get_lower_attr(margs.entry);
		upper_attrs	= get_upper_attr(margs.entry);
		*mapped_memtype = map_stg1_attr_to_memtype(lower_attrs);
		*mapped_access =
			map_stg1_attr_to_access(upper_attrs, lower_attrs);
	} else {
		*mapped_base	= 0U;
		*mapped_size	= 0U;
		*mapped_memtype = PGTABLE_HYP_MEMTYPE_WRITEBACK;
		*mapped_access	= PGTABLE_ACCESS_NONE;
	}

	return walk_ret;
}

bool
pgtable_hyp_lookup_range(uintptr_t  virtual_address_base,
			 size_t	    virtual_address_size,
			 uintptr_t *mapped_virtual_address,
			 paddr_t *mapped_phys, size_t *mapped_size,
			 pgtable_hyp_memtype_t *mapped_memtype,
			 pgtable_access_t *	mapped_access,
			 bool *			remainder_unmapped)
{
	(void)virtual_address_base;
	(void)virtual_address_size;
	(void)mapped_virtual_address;
	(void)mapped_phys;
	(void)mapped_size;
	(void)mapped_memtype;
	(void)mapped_access;
	(void)remainder_unmapped;

	return false;
}

error_t
pgtable_hyp_preallocate(partition_t *partition, uintptr_t virtual_address,
			size_t size)
{
	pgtable_prealloc_modifier_args_t margs;
	pgtable_t *			 pgt = NULL;

	assert(partition != NULL);
	assert((size & (size - 1)) == 0U);
	assert((virtual_address & (size - 1)) == 0);

	if (is_hyp_top_virtual_address(virtual_address)) {
		pgt = &hyp_pgtable.top_control;
	} else {
		pgt = &hyp_pgtable.bottom_control;
	}

	assert(!util_add_overflows(virtual_address, size - 1));

	assert(addr_check(virtual_address, pgt->address_bits) &&
	       addr_check(virtual_address + size - 1, pgt->address_bits));

	memset(&margs, 0, sizeof(margs));
	margs.partition		   = partition;
	margs.new_page_start_level = PGTABLE_INVALID_LEVEL;
	margs.error		   = OK;

	bool walk_ret = translation_table_walk(
		pgt, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_PREALLOC,
		VMSA_ENTRY_TYPE_INVALID, &margs);

	if (!walk_ret && (margs.error == OK)) {
		margs.error = ERROR_FAILURE;
	}

	return margs.error;
}

// FIXME: right now assume the virt address with size is free,
// no need to retry
// FIXME: assume the size must be single page size or available block
// size, or else, just map it as one single page.
static error_t
pgtable_do_hyp_map(partition_t *partition, uintptr_t virtual_address,
		   size_t size, paddr_t phys, pgtable_hyp_memtype_t memtype,
		   pgtable_access_t access, vmsa_shareability_t shareability,
		   bool try_map)
{
	pgtable_map_modifier_args_t margs;
	vmsa_stg1_lower_attrs_t	    l;
	vmsa_stg1_upper_attrs_t	    u;
	pgtable_t *		    pgt = NULL;

	assert(pgtable_op == true);

	assert(partition != NULL);

	if (is_hyp_top_virtual_address(virtual_address)) {
		pgt = &hyp_pgtable.top_control;
	} else {
		pgt = &hyp_pgtable.bottom_control;
	}

	if (util_add_overflows(virtual_address, size - 1)) {
		margs.error = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	if (!util_is_baligned(virtual_address, pgt->granule_size)) {
		margs.error = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}
	if (!util_is_baligned(phys, pgt->granule_size)) {
		margs.error = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}
	if (!util_is_baligned(size, pgt->granule_size)) {
		margs.error = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (!addr_check(virtual_address, pgt->address_bits) ||
	    !addr_check(virtual_address + size - 1, pgt->address_bits)) {
		margs.error = ERROR_ADDR_INVALID;
		goto out;
	}

	memset(&margs, 0, sizeof(margs));
	margs.orig_virtual_address = virtual_address;
	margs.orig_size		   = size;
	margs.phys		   = phys;
	margs.partition		   = partition;
	vmsa_stg1_lower_attrs_init(&l);
	vmsa_stg1_upper_attrs_init(&u);

	map_stg1_memtype_to_attrs(memtype, &l);
	map_stg1_access_to_attrs(access, &u, &l);
	vmsa_stg1_lower_attrs_set_SH(&l, shareability);
	margs.lower_attrs	   = vmsa_stg1_lower_attrs_raw(l);
	margs.upper_attrs	   = vmsa_stg1_upper_attrs_raw(u);
	margs.new_page_start_level = PGTABLE_INVALID_LEVEL;
	margs.error		   = OK;
	margs.try_map		   = try_map;
	margs.stage		   = PGTABLE_HYP_STAGE_1;

	// FIXME: try to unify the level number, just use one kind of level
	bool walk_ret = translation_table_walk(
		pgt, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_MMAP, VMSA_ENTRY_TYPE_LEAF,
		&margs);

	if (!walk_ret && (margs.error == OK)) {
		margs.error = ERROR_FAILURE;
	}
	if ((margs.error != OK) && (margs.partially_mapped_size != 0)) {
		pgtable_hyp_unmap(partition, virtual_address,
				  margs.partially_mapped_size,
				  PGTABLE_HYP_UNMAP_PRESERVE_ALL);
	}
out:
	return margs.error;
}

error_t
pgtable_hyp_map(partition_t *partition, uintptr_t virtual_address, size_t size,
		paddr_t phys, pgtable_hyp_memtype_t memtype,
		pgtable_access_t access, vmsa_shareability_t shareability)
{
	return pgtable_do_hyp_map(partition, virtual_address, size, phys,
				  memtype, access, shareability, true);
}

error_t
pgtable_hyp_remap(partition_t *partition, uintptr_t virtual_address,
		  size_t size, paddr_t phys, pgtable_hyp_memtype_t memtype,
		  pgtable_access_t access, vmsa_shareability_t shareability)
{
	return pgtable_do_hyp_map(partition, virtual_address, size, phys,
				  memtype, access, shareability, false);
}

static void
pgtable_remapping(pgtable_t *pgt, partition_t *partition,
		  pgtable_unmap_modifier_args_t margs)
{
	pgtable_map_modifier_args_t mremap_args;

	// If we had to do a break-before-make to split large pages at the start
	// and/or end for partial unmapping, remap them now.
	for (index_t i = 0; i < util_array_size(margs.remap_regions); i++) {
		if (margs.remap_regions[i].is_valid) {
			// Ensure that the flushes are complete
			dsb();
			break;
		}
	}

	for (index_t i = 0; i < util_array_size(margs.remap_regions); i++) {
		if (!margs.remap_regions[i].is_valid) {
			continue;
		}

		memset(&mremap_args, 0, sizeof(mremap_args));
		mremap_args.phys	= margs.remap_regions[i].phys;
		mremap_args.partition	= partition;
		mremap_args.lower_attrs = margs.remap_regions[i].lower_attrs;
		mremap_args.upper_attrs = margs.remap_regions[i].upper_attrs;
		mremap_args.new_page_start_level = PGTABLE_INVALID_LEVEL;
		mremap_args.try_map		 = true;
		mremap_args.stage		 = margs.stage;

		// FIXME: Handle error
		bool walk_ret = translation_table_walk(
			pgt, margs.remap_regions[i].virtual_address,
			margs.remap_regions[i].size,
			PGTABLE_TRANSLATION_TABLE_WALK_EVENT_MMAP,
			VMSA_ENTRY_TYPE_INVALID, &mremap_args);
		if (!walk_ret) {
			panic("Error in pgtable_remapping");
		}
	}
}

// FIXME: assume the size must be multiple of single page size or available
// block size, or else, just unmap it with page size aligned range.
// May be something like some blocks + several pages.
//
// Also, will unmap the vaddress without considering the memtype and access
// permission.
//
// It's caller's responsibility to make sure the virt address is already fully
// mapped. There's no roll back, so any failure will cause partially unmap
// operation.
void
pgtable_hyp_unmap(partition_t *partition, uintptr_t virtual_address,
		  size_t size, size_t preserved_prealloc)
{
	pgtable_unmap_modifier_args_t margs;
	pgtable_t *		      pgt = NULL;

	assert(pgtable_op == true);

	assert(partition != NULL);
	assert(util_is_p2_or_zero(preserved_prealloc));

	if (is_hyp_top_virtual_address(virtual_address)) {
		pgt = &hyp_pgtable.top_control;
	} else {
		pgt = &hyp_pgtable.bottom_control;
	}

	assert(!util_add_overflows(virtual_address, size - 1));

	assert(addr_check(virtual_address, pgt->address_bits));
	assert(addr_check(virtual_address + size - 1, pgt->address_bits));

	assert(util_is_baligned(virtual_address, pgt->granule_size));
	assert(util_is_baligned(size, pgt->granule_size));

	memset(&margs, 0, sizeof(margs));
	margs.partition			= partition;
	margs.preserved_size		= preserved_prealloc;
	margs.stage			= PGTABLE_HYP_STAGE_1;
	margs.remap_regions[0].is_valid = false;
	margs.remap_regions[1].is_valid = false;

	bool walk_ret = translation_table_walk(
		pgt, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_UNMAP,
		VMSA_ENTRY_TYPE_LEAF, &margs);
	if (!walk_ret) {
		panic("Error in pgtable_hyp_unmap");
	}

	pgtable_remapping(pgt, partition, margs);

	return;
}

void
pgtable_hyp_start(void)
{
	// Nothing to do here.

	// The pgtable_hyp code has to run with a lock and preempt disabled to
	// ensure forward progress and because the code is not thread safe.
	spinlock_acquire(&hyp_pgtable.lock);
#if !defined(NDEBUG)
	assert(pgtable_op == false);
	pgtable_op = true;
#endif
}

void
pgtable_hyp_commit(void)
{
#ifndef HOST_TEST
	__asm__ volatile("dsb ish" ::: "memory");
#endif
#if !defined(NDEBUG)
	assert(pgtable_op == true);
	pgtable_op = false;
#endif
	spinlock_release(&hyp_pgtable.lock);
}

#ifndef NDEBUG
void
pgtable_hyp_dump(void)
{
	pgtable_entry_types_t entry_types;
	vmaddr_t	      virtual_address = 0U;
	size_t		      size	      = 0U;

	LOG(DEBUG, INFO, "+---------------- page table ----------------\n");
	LOG(DEBUG, INFO, "| TTBR1[{:#x}]:\n",
	    hyp_pgtable.top_control.root_pgtable);
	entry_types = VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE |
		      VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
		      VMSA_ENTRY_TYPE_INVALID | VMSA_ENTRY_TYPE_RESERVED |
		      VMSA_ENTRY_TYPE_ERROR | VMSA_ENTRY_TYPE_NONE;
	virtual_address = ~util_mask(hyp_pgtable.top_control.address_bits);
	size		= 1UL << hyp_pgtable.top_control.address_bits;
	(void)translation_table_walk(&hyp_pgtable.top_control, virtual_address,
				     size,
				     PGTABLE_TRANSLATION_TABLE_WALK_EVENT_DUMP,
				     entry_types, NULL);
	LOG(DEBUG, INFO, "\n");
	LOG(DEBUG, INFO, "| TTBR0[{:#x}]:\n",
	    hyp_pgtable.bottom_control.root_pgtable);
	entry_types = VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE |
		      VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
		      VMSA_ENTRY_TYPE_INVALID | VMSA_ENTRY_TYPE_RESERVED |
		      VMSA_ENTRY_TYPE_ERROR | VMSA_ENTRY_TYPE_NONE;
	virtual_address = 0U;
	size		= 1UL << hyp_pgtable.bottom_control.address_bits;
	(void)translation_table_walk(&hyp_pgtable.bottom_control,
				     virtual_address, size,
				     PGTABLE_TRANSLATION_TABLE_WALK_EVENT_DUMP,
				     entry_types, NULL);
	LOG(DEBUG, INFO, "+--------------------------------------------\n\n");
}

void
pgtable_hyp_ext(vmaddr_t virtual_address, size_t size,
		pgtable_entry_types_t entry_types, ext_func_t func, void *data)
{
	ext_modifier_args_t margs;
	pgtable_t *	    pgt = NULL;

	if (is_hyp_top_virtual_address(virtual_address)) {
		pgt = &hyp_pgtable.top_control;
	} else {
		pgt = &hyp_pgtable.bottom_control;
	}

	assert(addr_check(virtual_address, pgt->address_bits));
	assert(addr_check(virtual_address + size - 1, pgt->address_bits));

	memset(&margs, 0, sizeof(margs));
	margs.func = func;
	margs.data = data;

	(void)translation_table_walk(
		pgt, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_EXTERNAL, entry_types,
		&margs);
}

void
pgtable_vm_dump(pgtable_vm_t *pgt)
{
	assert(pgt != NULL);

	pgtable_entry_types_t entry_types;

	size_t size = util_bit(pgt->control.address_bits);

	LOG(DEBUG, INFO, "+---------------- page table ----------------\n");
	LOG(DEBUG, INFO, "| TTBR({:#x}):\n", pgt->control.root_pgtable);
	entry_types = VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE |
		      VMSA_ENTRY_TYPE_NEXT_LEVEL_TABLE |
		      VMSA_ENTRY_TYPE_INVALID | VMSA_ENTRY_TYPE_RESERVED |
		      VMSA_ENTRY_TYPE_ERROR | VMSA_ENTRY_TYPE_NONE;
	(void)translation_table_walk(&pgt->control, 0L, size,
				     PGTABLE_TRANSLATION_TABLE_WALK_EVENT_DUMP,
				     entry_types, NULL);
	LOG(DEBUG, INFO, "+--------------------------------------------\n\n");
}

void
pgtable_vm_ext(pgtable_vm_t *pgt, vmaddr_t virtual_address, size_t size,
	       pgtable_entry_types_t entry_types, ext_func_t func, void *data)
{
	ext_modifier_args_t margs;

	assert(pgt != NULL);
	assert(addr_check(virtual_address, pgt->control.address_bits));
	assert(addr_check(virtual_address + size - 1,
			  pgt->control.address_bits));

	memset(&margs, 0, sizeof(margs));
	margs.func = func;
	margs.data = data;

	(void)translation_table_walk(
		&pgt->control, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_EXTERNAL, entry_types,
		&margs);
}
#endif

static tcr_tg_t
vtcr_get_tg0_code(size_t granule_size)
{
	tcr_tg_t tg0 = 0U;

	switch (granule_size) {
	case 1UL << SHIFT_4K:
		tg0 = TCR_TG_GRANULE_SIZE_4KB;
		break;
	case 1UL << SHIFT_16K:
		tg0 = TCR_TG_GRANULE_SIZE_16KB;
		break;
	case 1UL << SHIFT_64K:
		tg0 = TCR_TG_GRANULE_SIZE_64KB;
		break;
	default:
		panic("Invalid granule size");
	}

	return tg0;
}

#if !defined(HOST_TEST)
static void
pgtable_vm_init_regs(pgtable_vm_t *vm_pgtable)
{
	assert(vm_pgtable != NULL);

	// Init Virtualization Translation Control Register

	VTCR_EL2_init(&vm_pgtable->vtcr_el2);

	uint8_t t0sz = (uint8_t)(64U - vm_pgtable->control.address_bits);

	VTCR_EL2_set_T0SZ(&vm_pgtable->vtcr_el2, t0sz);

	if (vm_pgtable->control.granule_size == 4096) {
		switch (vm_pgtable->control.start_level) {
		case 0:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x2);
			break;
		case 1:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x1);
			break;
		case 2:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x0);
			break;
		default:
			panic("Invalid SL0");
		}
	} else {
		switch (vm_pgtable->control.start_level) {
		case 1:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x2);
			break;
		case 2:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x1);
			break;
		case 3:
			VTCR_EL2_set_SL0(&vm_pgtable->vtcr_el2, 0x0);
			break;
		case 0:
		default:
			panic("Invalid SL0");
		}
	}
	VTCR_EL2_set_IRGN0(&vm_pgtable->vtcr_el2, 1);
	VTCR_EL2_set_ORGN0(&vm_pgtable->vtcr_el2, 1);
	VTCR_EL2_set_SH0(&vm_pgtable->vtcr_el2, TCR_SH_INNER);

	tcr_tg_t tg0 = vtcr_get_tg0_code(vm_pgtable->control.granule_size);
	VTCR_EL2_set_TG0(&vm_pgtable->vtcr_el2, tg0);

	ID_AA64MMFR0_EL1_t id_aa64mmfro = register_ID_AA64MMFR0_EL1_read();
	VTCR_EL2_set_PS(&vm_pgtable->vtcr_el2,
			ID_AA64MMFR0_EL1_get_PARange(&id_aa64mmfro));

	// The stage-2 input address must be equal or smaller than the CPU's
	// reported physical address space size.
	switch (VTCR_EL2_get_PS(&vm_pgtable->vtcr_el2)) {
	case TCR_PS_SIZE_32BITS:
		assert(vm_pgtable->control.address_bits <= 32);
		break;
	case TCR_PS_SIZE_36BITS:
		assert(vm_pgtable->control.address_bits <= 36);
		break;
	case TCR_PS_SIZE_40BITS:
		assert(vm_pgtable->control.address_bits <= 40);
		break;
	case TCR_PS_SIZE_42BITS:
		assert(vm_pgtable->control.address_bits <= 42);
		break;
	case TCR_PS_SIZE_44BITS:
		assert(vm_pgtable->control.address_bits <= 44);
		break;
	case TCR_PS_SIZE_48BITS:
		assert(vm_pgtable->control.address_bits <= 48);
		break;
	case TCR_PS_SIZE_52BITS:
		assert(vm_pgtable->control.address_bits <= 52);
		break;
	default:
		panic("bad PARange");
	}

#if defined(ARCH_ARM_8_1_VMID16)
	VTCR_EL2_set_VS(&vm_pgtable->vtcr_el2, true);
#endif

#if defined(ARCH_ARM_8_1_TTHM)
	VTCR_EL2_set_HA(&vm_pgtable->vtcr_el2, true);
#if defined(ARCH_ARM_8_1_TTHM_HD)
	VTCR_EL2_set_HD(&vm_pgtable->vtcr_el2, true);
#endif
#endif

#if defined(ARCH_ARM_8_2_TTPBHA)
	VTCR_EL2_set_HWU059(&vm_pgtable->vtcr_el2, false);
	VTCR_EL2_set_HWU060(&vm_pgtable->vtcr_el2, false);
	VTCR_EL2_set_HWU061(&vm_pgtable->vtcr_el2, false);
	VTCR_EL2_set_HWU062(&vm_pgtable->vtcr_el2, false);
#endif

#if (ARCH_ARM_VER >= 84)
	VTCR_EL2_set_NSW(&vm_pgtable->vtcr_el2, true);
	VTCR_EL2_set_NSA(&vm_pgtable->vtcr_el2, true);
#endif

	// Init Virtualization Translation Table Base Register

	VTTBR_EL2_init(&vm_pgtable->vttbr_el2);
	VTTBR_EL2_set_CnP(&vm_pgtable->vttbr_el2, true);
	VTTBR_EL2_set_BADDR(&vm_pgtable->vttbr_el2,
			    vm_pgtable->control.root_pgtable);
#if defined(ARCH_ARM_8_1_VMID16)
	VTTBR_EL2_set_VMID(&vm_pgtable->vttbr_el2, vm_pgtable->control.vmid);
#else
	VTTBR_EL2_set_VMID(&vm_pgtable->vttbr_el2,
			   (uint8_t)vm_pgtable->control.vmid);
#endif
}

void
pgtable_vm_load_regs(pgtable_vm_t *vm_pgtable)
{
	register_VTCR_EL2_write(vm_pgtable->vtcr_el2);
	register_VTTBR_EL2_write(vm_pgtable->vttbr_el2);
}
#endif

error_t
pgtable_vm_init(partition_t *partition, pgtable_vm_t *pgtable, vmid_t vmid)
{
	error_t ret = OK;
	index_t msb;

	if (pgtable->control.root != NULL) {
		// Address already setup by another module
		assert(pgtable->control.vmid == vmid);
		goto out;
	}

	// FIXME: refine with more configurable code
	pgtable->control.granule_size = PGTABLE_VM_PAGE_SIZE;
	pgtable->control.address_bits = PLATFORM_VM_ADDRESS_SPACE_BITS;
	msb			      = PLATFORM_VM_ADDRESS_SPACE_BITS - 1;
	pgtable->control.vmid	      = vmid;

	get_start_level_info_ret_t info = get_start_level_info(level_conf, msb);
	pgtable->control.start_level	= info.level;
	pgtable->control.start_level_size = info.size;

	// allocate the level 0 page table
	ret = alloc_level_table(partition, info.size,
				util_max(info.size, VMSA_TABLE_MIN_ALIGN),
				&pgtable->control.root_pgtable,
				&pgtable->control.root);
	if (ret != OK) {
		goto out;
	}

#if !defined(HOST_TEST)
	pgtable_vm_init_regs(pgtable);
#endif

out:
	return ret;
}

void
pgtable_vm_destroy(partition_t *partition, pgtable_vm_t *pgtable)
{
	vmaddr_t virtual_address = 0x0U;
	size_t	 size		 = 0x0U;

	assert(partition != NULL);
	assert(pgtable != NULL);
	assert(pgtable->control.root != NULL);

	virtual_address = 0x0U;
	size		= 1UL << pgtable->control.address_bits;
	// we should unmap everything
	pgtable_vm_unmap(partition, pgtable, virtual_address, size);

	// free top level page table
	partition_free(partition, pgtable->control.root,
		       pgtable->control.start_level_size);
	pgtable->control.root = NULL;
}

bool
pgtable_vm_lookup(pgtable_vm_t *pgtable, vmaddr_t virtual_address,
		  paddr_t *mapped_base, size_t *mapped_size,
		  pgtable_vm_memtype_t *mapped_memtype,
		  pgtable_access_t *	mapped_vm_kernel_access,
		  pgtable_access_t *	mapped_vm_user_access)
{
	bool			       walk_ret;
	pgtable_lookup_modifier_args_t margs;
	pgtable_entry_types_t	       entry_types;
	vmsa_upper_attrs_t	       upper_attrs;
	vmsa_lower_attrs_t	       lower_attrs;

	assert(pgtable != NULL);
	assert(mapped_base != NULL);
	assert(mapped_size != NULL);
	assert(mapped_memtype != NULL);
	assert(mapped_vm_kernel_access != NULL);
	assert(mapped_vm_user_access != NULL);

	assert(addr_check(virtual_address, pgtable->control.address_bits));

	memset(&margs, 0, sizeof(margs));
	entry_types = VMSA_ENTRY_TYPE_BLOCK | VMSA_ENTRY_TYPE_PAGE;
	// just try to lookup a page, but if it's a block, the modifier will
	// stop the walk and return success
	walk_ret = translation_table_walk(
		&pgtable->control, virtual_address,
		pgtable->control.granule_size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_LOOKUP, entry_types,
		&margs);

	if (margs.size == 0U) {
		// Return error (not-mapped) if lookup found no pages.
		walk_ret = false;
	}

	if (walk_ret) {
		*mapped_base = margs.phys;
		*mapped_size = margs.size;

		lower_attrs	= get_lower_attr(margs.entry);
		upper_attrs	= get_upper_attr(margs.entry);
		*mapped_memtype = map_stg2_attr_to_memtype(lower_attrs);
		map_stg2_attr_to_access(upper_attrs, lower_attrs,
					mapped_vm_kernel_access,
					mapped_vm_user_access);

	} else {
		*mapped_base		 = 0U;
		*mapped_size		 = 0U;
		*mapped_memtype		 = PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE;
		*mapped_vm_kernel_access = PGTABLE_ACCESS_NONE;
		*mapped_vm_user_access	 = PGTABLE_ACCESS_NONE;
	}

	return walk_ret;
}

bool
pgtable_vm_lookup_range(pgtable_vm_t *pgtable, vmaddr_t virtual_address_base,
			size_t	  virtual_address_size,
			vmaddr_t *mapped_virtual_address, paddr_t *mapped_phys,
			size_t *	      mapped_size,
			pgtable_vm_memtype_t *mapped_memtype,
			pgtable_access_t *    mapped_vm_kernel_access,
			pgtable_access_t *    mapped_vm_user_access,
			bool *		      remainder_unmapped)
{
	(void)pgtable;
	(void)virtual_address_base;
	(void)virtual_address_size;
	(void)mapped_virtual_address;
	(void)mapped_phys;
	(void)mapped_size;
	(void)mapped_memtype;
	(void)mapped_vm_kernel_access;
	(void)mapped_vm_user_access;
	(void)remainder_unmapped;

	return false;
}

// FIXME: right now assume the virt address with size is free,
// no need to retry
// FIXME: assume the size must be single page size or available block
// size, or else, just map it as one single page.
error_t
pgtable_vm_map(partition_t *partition, pgtable_vm_t *pgtable,
	       vmaddr_t virtual_address, size_t size, paddr_t phys,
	       pgtable_vm_memtype_t memtype, pgtable_access_t vm_kernel_access,
	       pgtable_access_t vm_user_access, bool try_map)
{
	pgtable_map_modifier_args_t margs;
	vmsa_stg2_lower_attrs_t	    l;
	vmsa_stg2_upper_attrs_t	    u;

	assert(pgtable_op == true);

	assert(pgtable != NULL);
	assert(partition != NULL);

	if (!addr_check(virtual_address, pgtable->control.address_bits)) {
		margs.error = ERROR_ADDR_INVALID;
		goto fail;
	}

	if (util_add_overflows(virtual_address, size - 1) ||
	    !addr_check(virtual_address + size - 1,
			pgtable->control.address_bits)) {
		margs.error = ERROR_ADDR_OVERFLOW;
		goto fail;
	}

	// FIXME: Supporting different granule sizes will need support and
	// additional checking to be added to memextent code.

	if (!util_is_baligned(virtual_address, pgtable->control.granule_size) ||
	    !util_is_baligned(phys, pgtable->control.granule_size) ||
	    !util_is_baligned(size, pgtable->control.granule_size)) {
		margs.error = ERROR_ARGUMENT_ALIGNMENT;
		goto fail;
	}

	// FIXME: how to check phys, read tcr in init?
	// FIXME: no need to to check vm memtype, right?

	memset(&margs, 0, sizeof(margs));
	margs.orig_virtual_address = virtual_address;
	margs.orig_size		   = size;
	margs.phys		   = phys;
	margs.partition		   = partition;
	vmsa_stg2_lower_attrs_init(&l);
	vmsa_stg2_upper_attrs_init(&u);
	map_stg2_memtype_to_attrs(memtype, &l);
	map_stg2_access_to_attrs(vm_kernel_access, vm_user_access, &u, &l);
	margs.lower_attrs	   = vmsa_stg2_lower_attrs_raw(l);
	margs.upper_attrs	   = vmsa_stg2_upper_attrs_raw(u);
	margs.new_page_start_level = PGTABLE_INVALID_LEVEL;
	margs.error		   = OK;
	margs.try_map		   = try_map;
	margs.stage		   = PGTABLE_VM_STAGE_2;

	// FIXME: try to unify the level number, just use one kind of level
	bool walk_ret = translation_table_walk(
		&pgtable->control, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_MMAP, VMSA_ENTRY_TYPE_LEAF,
		&margs);

	if (((margs.error != OK) || !walk_ret) &&
	    (margs.partially_mapped_size != 0)) {
		pgtable_vm_unmap(partition, pgtable, virtual_address,
				 margs.partially_mapped_size);
	}

fail:
	return margs.error;
}

void
pgtable_vm_unmap(partition_t *partition, pgtable_vm_t *pgtable,
		 vmaddr_t virtual_address, size_t size)
{
	pgtable_unmap_modifier_args_t margs;

	assert(pgtable_op == true);

	assert(pgtable != NULL);
	assert(partition != NULL);

	assert(!util_add_overflows(virtual_address, size - 1));

	assert(addr_check(virtual_address, pgtable->control.address_bits));
	assert(addr_check(virtual_address + size - 1,
			  pgtable->control.address_bits));

	assert(util_is_baligned(virtual_address,
				pgtable->control.granule_size));
	assert(util_is_baligned(size, pgtable->control.granule_size));

	memset(&margs, 0, sizeof(margs));
	margs.partition = partition;
	// no need to preserve table levels here
	margs.preserved_size		= PGTABLE_HYP_UNMAP_PRESERVE_NONE;
	margs.stage			= PGTABLE_VM_STAGE_2;
	margs.remap_regions[0].is_valid = false;
	margs.remap_regions[1].is_valid = false;

	bool walk_ret = translation_table_walk(
		&pgtable->control, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_UNMAP,
		VMSA_ENTRY_TYPE_LEAF, &margs);
	if (!walk_ret) {
		panic("Error in pgtable_hyp_unmap");
	}

	pgtable_remapping(&pgtable->control, partition, margs);

	return;
}

void
pgtable_vm_unmap_matching(partition_t *partition, pgtable_vm_t *pgtable,
			  vmaddr_t virtual_address, paddr_t phys, size_t size)
{
	pgtable_unmap_modifier_args_t margs;

	assert(pgtable_op == true);

	assert(pgtable != NULL);
	assert(partition != NULL);

	assert(!util_add_overflows(virtual_address, size - 1));

	assert(addr_check(virtual_address, pgtable->control.address_bits));
	assert(addr_check(virtual_address + size - 1,
			  pgtable->control.address_bits));

	memset(&margs, 0, sizeof(margs));
	margs.partition = partition;
	// no need to preserve table levels here
	margs.preserved_size = PGTABLE_HYP_UNMAP_PRESERVE_NONE;
	margs.stage	     = PGTABLE_VM_STAGE_2;
	margs.phys	     = phys;
	margs.size	     = size;
	(void)translation_table_walk(
		&pgtable->control, virtual_address, size,
		PGTABLE_TRANSLATION_TABLE_WALK_EVENT_UNMAP_MATCH,
		VMSA_ENTRY_TYPE_LEAF, &margs);

	pgtable_remapping(&pgtable->control, partition, margs);

	return;
}

// FIXME: remove pragmas when start/complete implemented
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
void
pgtable_vm_start(pgtable_vm_t *pgtable)
{
	assert(pgtable != NULL);
#ifndef HOST_TEST
	// FIXME: We need to to run VM pagetable code with preempt disable due
	// to TLB flushes.
	preempt_disable();
#if !defined(NDEBUG)
	assert(pgtable_op == false);
	pgtable_op = true;
#endif

	thread_t *thread = thread_get_self();

	// Since the pagetable code may need to flush the target VMID, we need
	// to ensure that it is current for the pagetable operations.
	// We set the VMID which is in the VTTBR register. Note, no need to set
	// VTCR - so ensure no TLB walks take place!.  This also assumes that
	// preempt is disabled otherwise a context-switch would restore the
	// original registers.
	if ((thread->addrspace == NULL) ||
	    (&thread->addrspace->vm_pgtable != pgtable)) {
		register_VTTBR_EL2_write(pgtable->vttbr_el2);
	}
#endif
}

void
pgtable_vm_commit(pgtable_vm_t *pgtable)
{
#ifndef HOST_TEST
#if !defined(NDEBUG)
	assert(pgtable_op == true);
	pgtable_op = false;
#endif

	__asm__ volatile("dsb ish" ::: "memory");
	// This is only needed when unmapping. Consider some flags to
	// track to flush requirements.
	__asm__ volatile("tlbi VMALLE1IS; dsb ish" : "+m"(asm_ordering));

	thread_t *thread = thread_get_self();

	// Since the pagetable code flushes the target VMID, we set it as the
	// current VMID for the pagetable operations. We need to restore the
	// original VMID (in VTTBR_EL2) here.
	if ((thread->addrspace != NULL) &&
	    (&thread->addrspace->vm_pgtable != pgtable)) {
		register_VTTBR_EL2_write(
			thread->addrspace->vm_pgtable.vttbr_el2);
	}

	preempt_enable();
#endif
	trigger_pgtable_vm_commit_event(pgtable);
}
#pragma clang diagnostic pop
