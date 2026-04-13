// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <hyp_aspace.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_mem.h>
#include <rcu.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "ffa_private.h"

static_assert(HYP_FFA_PAGE_SIZE == 4096U,
	      "FFA RXTX size must be 1 page whose size is 4096U");

// RXTX buffers between Hypervisor and TZ
static uint8_t *ffa_hyp_rx_buffer;
static uint8_t *ffa_hyp_tx_buffer;
static size_t	ffa_hyp_rxtx_buffer_size;
// These atomic booleans are used to acquire and release the hyp buffers above.
// We don't use a spinlock since that would cause other CPUs making calls that
// require the buffer to spin.
static _Atomic bool ffa_hyp_is_tx_busy;	 // True if in use
static _Atomic bool ffa_hyp_is_rx_owner; // True if Hyp is the owner

bool
ffa_create_hyp_rxtx_buffer(count_t page_count)
{
	bool success;

	assert((page_count > 0U) &&
	       (page_count <= HYP_FFA_RXTX_MAX_BUFFER_PAGE_COUNT));

	size_t	 buffer_size = (size_t)page_count * 4096U;
	uint8_t *rx_buffer;
	uint8_t *tx_buffer;
	paddr_t	 rx_phys;
	paddr_t	 tx_phys;

	// Allocate the RX/TX buffer from the heap,
	// the corresponding physical pages are contiguous
	partition_t *hyp_partition = partition_get_private();

	assert_debug(ffa_hyp_rxtx_buffer_size == 0U);

	size_t min_alignment = ffa_get_ffa_rxtx_min_alignment();

	void_ptr_result_t alloc_ret =
		partition_alloc(hyp_partition, buffer_size * 2U, min_alignment);
	if (alloc_ret.e != OK) {
		success = false;
		goto out_err;
	}
	rx_buffer = (uint8_t *)alloc_ret.r;
	rx_phys	  = partition_virt_to_phys(hyp_partition, (uintptr_t)rx_buffer);

	tx_buffer = (uint8_t *)((uintptr_t)rx_buffer + buffer_size);
	tx_phys	  = rx_phys + buffer_size;

	// Send the FF-A request to TZ
	register_t args[SMCCC_1_2_ARGS] = { 0 };
	register_t ret[SMCCC_1_2_RETS]	= { 0 };

	args[0] = (register_t)tx_phys;
	args[1] = (register_t)rx_phys;
	args[2] = (register_t)page_count;

	smccc_function_id_t fn_id = smccc_create_fn_id(
		FFA_FUNCTION_FFA_RXTX_MAP, SMCCC_OWNER_ID_STANDARD, true, true);

	// Send the request to the TZ to create HYP to TZ RXTX buffer
	ffa_smccc_call(fn_id, &args, &ret);

	smccc_function_id_t ret_fn_id =
		smccc_function_id_cast((uint32_t)ret[0]);
	smccc_function_t ffa_ret_func =
		smccc_function_id_get_function(&ret_fn_id);
	if (ffa_ret_func != FFA_FUNCTION_FFA_SUCCESS) {
		panic("FFA: RXTX buffer creation failed.");
	}
	success = true;

	ffa_hyp_rx_buffer	 = rx_buffer;
	ffa_hyp_tx_buffer	 = tx_buffer;
	ffa_hyp_rxtx_buffer_size = buffer_size;
	atomic_init(&ffa_hyp_is_tx_busy, false);
	atomic_init(&ffa_hyp_is_rx_owner, true);

out_err:
	if (!success && (alloc_ret.r != NULL)) {
		partition_free(hyp_partition, alloc_ret.r, buffer_size * 2U);
	}

	return success;
}

void
ffa_hyp_tx_buffer_get_tx(uint8_t **ffa_hyp_tx_buffer_ptr,
			 size_t	  *ffa_hyp_rxtx_buffer_size_ptr)
{
	assert(ffa_hyp_tx_buffer_ptr != NULL);
	assert(ffa_hyp_rxtx_buffer_size_ptr != NULL);

	*ffa_hyp_tx_buffer_ptr	      = ffa_hyp_tx_buffer;
	*ffa_hyp_rxtx_buffer_size_ptr = ffa_hyp_rxtx_buffer_size;
}

bool
ffa_acquire_hyp_tx_buffer(void)
{
	bool ret;

	if (ffa_hyp_rx_buffer == NULL) {
		ret = false;
		goto out;
	}

	bool ffa_hyp_is_tx_busy_expect = false;
	ret = atomic_compare_exchange_strong_explicit(
		&ffa_hyp_is_tx_busy, &ffa_hyp_is_tx_busy_expect, true,
		memory_order_acquire, memory_order_relaxed);

out:
	return ret;
}

void
ffa_release_hyp_tx_buffer(void)
{
	atomic_store_release(&ffa_hyp_is_tx_busy, false);
}

static void
ffa_rxtx_buffer_init(ffa_rxtx_buffer_t *rxtx_buffer, virt_range_t map_range,
		     size_t buffer_size, memextent_t *rx_me, memextent_t *tx_me,
		     uint8_t *tx_buffer_hyp_private)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock)
{
	ffa_rxtx_buffer_info_t *info = &rxtx_buffer->info;

	assert_debug(info->buffer_size == 0U);
	assert_debug(buffer_size != 0U);

	info->map_range	  = map_range;
	info->rx_me	  = rx_me;
	info->tx_me	  = tx_me;
	info->buffer_size = buffer_size;

	info->is_rx_owner = true;
	info->is_tx_busy  = false;

	info->tx_buffer_hyp_private = tx_buffer_hyp_private;
}

static void
ffa_rxtx_buffer_reset(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock)
{
	rxtx_buffer->info = (ffa_rxtx_buffer_info_t){ 0U };
}

size_t
ffa_rxtx_buffer_size(ffa_rxtx_buffer_t *rxtx_buffer)
{
	return rxtx_buffer->info.buffer_size;
}

uint8_t *
ffa_rxtx_buffer_get_rx(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock)
{
	return (uint8_t *)rxtx_buffer->info.map_range.base;
}

uint8_t *
ffa_rxtx_buffer_get_tx(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock)
{
	return (uint8_t *)(rxtx_buffer->info.map_range.base +
			   rxtx_buffer->info.buffer_size + HYP_FFA_PAGE_SIZE);
}

uint8_t *
ffa_rxtx_buffer_get_hyp_private(ffa_rxtx_buffer_t *rxtx_buffer)
{
	return (uint8_t *)rxtx_buffer->info.tx_buffer_hyp_private;
}

bool
ffa_rxtx_buffer_is_valid(ffa_rxtx_buffer_t *rxtx_buffer)
{
	return rxtx_buffer->info.buffer_size != 0U;
}

bool
ffa_rxtx_buffer_hyp_is_rx_owner(ffa_rxtx_buffer_t *rxtx_buffer)
{
	return rxtx_buffer->info.is_rx_owner;
}

void
ffa_rxtx_buffer_set_hyp_is_rx_owner(ffa_rxtx_buffer_t *rxtx_buffer,
				    bool	       hyp_owned)
{
	rxtx_buffer->info.is_rx_owner = hyp_owned;
}

bool
ffa_rxtx_buffer_is_tx_busy(ffa_rxtx_buffer_t *rxtx_buffer)
{
	return rxtx_buffer->info.is_tx_busy;
}

void
ffa_rxtx_buffer_set_is_tx_busy(ffa_rxtx_buffer_t *rxtx_buffer, bool tx_busy)
{
	rxtx_buffer->info.is_tx_busy = tx_busy;
}

static error_t
ffa_rxtx_buffer_validate_acl(memextent_t *me, vmid_t vmid,
			     pgtable_access_t access)
{
	error_t ret;

	spinlock_acquire(&me->lock);

	count_t acl_len = me->mem_acl_len;
	if (!me->mem_acl_registered || (acl_len != 1U)) {
		// ACL must be explicitly registered for SP sharing.
		// Only two valid scenarios exist:
		// 1. ACL represents the owner VM of the memextent
		// 2. ACL represents the sole VM that was lent the memory region
		ret = ERROR_DENIED;
		goto out;
	}

	mem_acl_info_t *entry = &me->mem_acl_info[0];

	vmid_t	entry_vmid = mem_acl_info_get_vmid(entry);
	uint8_t entry_perm = mem_acl_info_get_perm(entry);

	if (vmid != entry_vmid) {
		// Only the registered VM is allowed for FFA use cases.
		ret = ERROR_DENIED;
		goto out;
	}

	pgtable_access_result_t access_res =
		pgtable_access_cast_safe(entry_perm);
	if (access_res.e != OK) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!pgtable_access_check(access_res.r, access)) {
		ret = ERROR_DENIED;
		goto out;
	}

	ret = OK;

out:
	spinlock_release(&me->lock);
	return ret;
}

static error_t
ffa_rxtx_buffer_map(addrspace_t *addrspace, vmaddr_t page_ipa,
		    uintptr_t va_base, memextent_t **page_me, size_t size)
{
	error_t err;

	assert(addrspace != NULL);
	partition_t *partition = addrspace->header.partition;

	addrspace_lookup_result_t lookup_ret =
		addrspace_lookup(addrspace, page_ipa, size);
	if (lookup_ret.e != OK) {
		err = lookup_ret.e;
		LOG(ERROR, WARN,
		    "FF-A: Mapping of VM RX/TX failed, lookup ipa {:#x} size {:#x} error {:d}",
		    page_ipa, size, (register_t)err);
		goto out;
	}
	if (lookup_ret.r.size != size) {
		err = ERROR_ARGUMENT_INVALID;
		LOG(ERROR, WARN, "FF-A: Discontiguous RX/TX is not supported");
		goto out;
	}

	pgtable_vm_memtype_t mapped_memtype = lookup_ret.r.memtype;
	if ((mapped_memtype == PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE) ||
	    (mapped_memtype == PGTABLE_VM_MEMTYPE_DEVICE_NGNRE) ||
	    (mapped_memtype == PGTABLE_VM_MEMTYPE_DEVICE_NGRE) ||
	    (mapped_memtype == PGTABLE_VM_MEMTYPE_DEVICE_GRE)) {
		err = ERROR_MEMEXTENT_TYPE;
		LOG(ERROR, WARN,
		    "FF-A: Mapping of VM RX/TX failed, lookup type {:#x}",
		    (register_t)mapped_memtype);
		goto out;
	}

	// For RX buffer, hypervisor should have RW permission, guest OS should
	// have at least R. For TX buffer, hypervisor should have R permission,
	// guest OS should have at least RW. And the permission that guest owns
	// should be high permissive than that we want to map to hypervisor.
	pgtable_access_t access = PGTABLE_ACCESS_RW;

	if (!pgtable_access_check(lookup_ret.r.kernel_access, access)) {
		err = ERROR_DENIED;
		LOG(ERROR, WARN,
		    "FF-A: Mapping of VM RX/TX failed, access {:#x}",
		    (register_t)access);
		goto out;
	}

	paddr_t page_pa = lookup_ret.r.phys;

	// Derive the memory to prevent the VM from donating, sharing, or
	// lending it. This will result in a NOT_OWNER error if the VM attempts
	// to do so.
	rcu_read_start();
	memdb_obj_type_result_t res = memdb_lookup(page_pa);
	if ((res.e != OK) || (res.r.type != MEMDB_TYPE_EXTENT)) {
		err = res.e;
		LOG(ERROR, WARN,
		    "FF-A: Page's ownership isn't set properly {:d}",
		    (register_t)err);
		goto out_rcu_read_finish;
	}

	memextent_t *owner_me = (memextent_t *)res.r.object;
	assert(owner_me->memtype != MEMEXTENT_MEMTYPE_DEVICE);

	if (!object_get_memextent_safe(owner_me)) {
		LOG(ERROR, WARN, "FF-A: RX/TX get me failed");
		err = ERROR_FAILURE;
		goto out_rcu_read_finish;
	}

	err = ffa_rxtx_buffer_validate_acl(owner_me, addrspace->vmid, access);
	if (err != OK) {
		LOG(ERROR, WARN, "FF-A: RX/TX acl validation failed");
		goto out_put_owner_me;
	}

	memextent_ptr_result_t me_ret = memextent_derive(
		owner_me, page_pa - owner_me->phys_base, size,
		owner_me->memtype, owner_me->access, MEMEXTENT_TYPE_BASIC);
	if (me_ret.e != OK) {
		err = me_ret.e;
		LOG(ERROR, WARN,
		    "FF-A: Deriving of VM RX/TX failed, map error {:d}",
		    (register_t)me_ret.e);
		goto out_put_owner_me;
	}

	// Map VM's RXTX buffer in Hypervisor
	pgtable_hyp_start();
	err = pgtable_hyp_map(partition, va_base, size, page_pa,
			      PGTABLE_HYP_MEMTYPE_WRITEBACK, access,
			      VMSA_SHAREABILITY_INNER_SHAREABLE);
	pgtable_hyp_commit();
	if (err != OK) {
		LOG(ERROR, WARN,
		    "FF-A: Mapping of VM RX/TX failed, map error {:d}",
		    (register_t)err);
		goto delete_me;
	}

	if (page_me != NULL) {
		*page_me = me_ret.r;
	}

	err = OK;
	goto out_put_owner_me;

delete_me:
	object_put_memextent(me_ret.r);
out_put_owner_me:
	object_put_memextent(owner_me);
out_rcu_read_finish:
	rcu_read_finish();
out:
	return err;
}

static bool
ffa_validate_rxtx_buffer(paddr_t base, size_t size, size_t min_alignment,
			 bool is_smc64)
{
	bool success;

	if (!is_smc64) {
		size = size & util_mask(32);
		base = base & util_mask(32);
	}
	if (size == 0U) {
		success = false;
		goto out;
	}
	size_t end_offset = size - 1U;

	if (!util_is_baligned(base, min_alignment) ||
	    !util_is_baligned(size, min_alignment)) {
		success = false;
		goto out;
	}

	if (is_smc64) {
		success = !util_add_overflows(base, end_offset);
	} else {
		// Ensure the whole buffer is 32-bit
		success = (base + end_offset) <= UINT32_MAX;
	}

out:
	return success;
}

void
ffa_call_handle_ffa_rxtx_map(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	assert_debug(smccc_function_id_get_function(&fn_id) ==
		     FFA_FUNCTION_FFA_RXTX_MAP);

	partition_t *partition = addrspace->header.partition;

	bool	 is_smc64   = smccc_function_id_get_is_smc64(&fn_id);
	uint64_t tx_address = is_smc64 ? (*args)[0]
				       : ((*args)[0] & util_mask(32U));
	uint64_t rx_address = is_smc64 ? (*args)[1]
				       : ((*args)[1] & util_mask(32U));

	ffa_rxtx_page_count_t ffa_rxtx_page_count =
		ffa_rxtx_page_count_cast((uint32_t)(*args)[2]);

	spinlock_acquire(&addrspace->ffa_rxtx_buffer.lock);

	if (ffa_rxtx_page_count_get_reserved(&ffa_rxtx_page_count) != 0U) {
		LOG(ERROR, WARN, "FF-A: FFA_RXTX_MAP page count is invalid");
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		goto out_unlock;
	}

	uint32_t page_count =
		ffa_rxtx_page_count_get_count(&ffa_rxtx_page_count);
	size_t buffer_size = (size_t)page_count * 4096U;

	if (ffa_rxtx_buffer_is_valid(&addrspace->ffa_rxtx_buffer)) {
		LOG(ERROR, WARN, "FF-A: RXTX buffer exists");
		ffa_set_error(FFA_RET_DENIED, ret);
		goto out_unlock;
	}

	LOG(DEBUG, INFO, "FF-A: RXTX map, tx {:x} rx {:x} size {:x}.",
	    tx_address, rx_address, buffer_size);

	size_t rxtx_min_alignment = ffa_get_ffa_rxtx_min_alignment();

	if ((tx_address == 0U) || (rx_address == 0U) ||
	    !ffa_validate_rxtx_buffer(tx_address, buffer_size,
				      rxtx_min_alignment, is_smc64) ||
	    !ffa_validate_rxtx_buffer(rx_address, buffer_size,
				      rxtx_min_alignment, is_smc64)) {
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		goto out_unlock;
	}

	if (buffer_size != HYP_FFA_PAGE_SIZE) {
		// Currently we enforce that only a single page can be used
		// for RXTX.
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		goto out_unlock;
	}

	virt_range_result_t buffer_area =
		hyp_aspace_allocate((buffer_size * 2U) + HYP_FFA_PAGE_SIZE);
	if (buffer_area.e != OK) {
		LOG(ERROR, WARN, "FF-A: aspace alloc failed");
		ffa_set_error(FFA_RET_NO_MEMORY, ret);
		goto out_unlock;
	}

	memextent_t *rx_me = NULL;
	memextent_t *tx_me = NULL;

	// Map RX and TX buffers in the hypervisor address space

	if (OK != ffa_rxtx_buffer_map(addrspace, (vmaddr_t)rx_address,
				      buffer_area.r.base, &rx_me,
				      buffer_size)) {
		LOG(ERROR, WARN, "FF-A: RXTX buffer mapping failed");
		ffa_set_error(FFA_RET_DENIED, ret);
		goto out_unmap;
	}
	// Leave a HYP_FFA_PAGE_SIZE guard region between RX and TX mappings
	if (OK != ffa_rxtx_buffer_map(addrspace, (vmaddr_t)tx_address,
				      buffer_area.r.base + buffer_size +
					      HYP_FFA_PAGE_SIZE,
				      &tx_me, buffer_size)) {
		LOG(ERROR, WARN, "FF-A: RXTX buffer mapping failed");
		ffa_set_error(FFA_RET_DENIED, ret);
		goto out_unmap;
	}

	// Allocate a private buffer used when Hypervisor needs its own private
	// copy of the VM's TX buffer contents.
	void_ptr_result_t alloc_ret = partition_alloc(
		addrspace->header.partition, buffer_size, rxtx_min_alignment);
	if (alloc_ret.e != OK) {
		LOG(ERROR, WARN, "FF-A: alloc TX private buffer failed");
		ffa_set_error(FFA_RET_NO_MEMORY, ret);
		goto out_unmap;
	}

	ffa_rxtx_buffer_init(&addrspace->ffa_rxtx_buffer, buffer_area.r,
			     buffer_size, rx_me, tx_me, (uint8_t *)alloc_ret.r);

	if (smccc_function_id_get_is_smc64(&fn_id)) {
		ffa_success64(ret);
	} else {
		ffa_success(ret);
	}

	goto out_unlock;

out_unmap:
	hyp_aspace_unmap_and_deallocate(partition, buffer_area.r);
	if (tx_me != NULL) {
		object_put_memextent(tx_me);
	}
	if (rx_me != NULL) {
		object_put_memextent(rx_me);
	}

out_unlock:
	spinlock_release(&addrspace->ffa_rxtx_buffer.lock);
}

void
ffa_call_handle_ffa_rxtx_unmap(smccc_function_id_t fn_id,
			       const register_t (*args)[SMCCC_1_2_ARGS],
			       register_t (*ret)[SMCCC_1_2_RETS])
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	smccc_function_t ffa_func = smccc_function_id_get_function(&fn_id);
	assert_debug(ffa_func == FFA_FUNCTION_FFA_RXTX_UNMAP);

	if ((uint32_t)(*args)[0] != 0U) {
		// The ID is only valid at the Non-secure physical FF-A
		// instance. MBZ Otherwise.
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		goto out;
	}

	error_t err = ffa_free_vm_rxtx_buffer(addrspace, false);
	if (err == ERROR_BUSY) {
		ffa_set_error(FFA_RET_BUSY, ret);
		goto out;
	} else if (err == ERROR_ARGUMENT_INVALID) {
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		goto out;
	} else {
		// Other error reponses are not expected.
		assert(err == OK);
	}

	// We need RCU sync to complete memextent deletion
	rcu_sync();

	ffa_success(ret);

out:
	return;
}

error_t
ffa_free_vm_rxtx_buffer(addrspace_t *addrspace, bool is_shutdown)
{
	error_t ret;
	assert(addrspace != NULL);

	ffa_rxtx_buffer_t *rxtx_buffer = &addrspace->ffa_rxtx_buffer;

	spinlock_acquire(&rxtx_buffer->lock);

	if (ffa_rxtx_buffer_is_valid(rxtx_buffer)) {
		if (!ffa_rxtx_buffer_is_tx_busy(rxtx_buffer)) {
			partition_t *partition = addrspace->header.partition;

			// Although only the initial portion of the allocated
			// memory was explicitly mapped (2 * rxtx_buffer->size),
			// we unmap the entire original allocation to ensure
			// proper resource release.
			hyp_aspace_unmap_and_deallocate(
				partition, rxtx_buffer->info.map_range);

			// Release the derived extents.
			object_put_memextent(rxtx_buffer->info.rx_me);
			object_put_memextent(rxtx_buffer->info.tx_me);

			// Free the copy-buffer.
			partition_free(addrspace->header.partition,
				       rxtx_buffer->info.tx_buffer_hyp_private,
				       rxtx_buffer->info.buffer_size);

			ffa_rxtx_buffer_reset(rxtx_buffer);
			ret = OK;
		} else {
			assert_debug(!is_shutdown);
			LOG(ERROR, WARN, "FF-A: RXTX tx busy");
			ret = ERROR_BUSY;
		}
	} else {
		if (is_shutdown) {
			// When it is teardown, the VM may not have a RXTX
			// buffer allocated.
			ret = OK;
		} else {
			LOG(ERROR, WARN, "FF-A: RXTX not setup");
			ret = ERROR_ARGUMENT_INVALID;
		}
	}

	spinlock_release(&rxtx_buffer->lock);

	return ret;
}

void
ffa_call_handle_ffa_rx_release(smccc_function_id_t fn_id,
			       const register_t (*args)[SMCCC_1_2_ARGS],
			       register_t (*ret)[SMCCC_1_2_RETS])
{
	bool valid;

	smccc_function_t ffa_func = smccc_function_id_get_function(&fn_id);
	assert_debug(ffa_func == FFA_FUNCTION_FFA_RX_RELEASE);

#if defined(PLATFORM_FFA_UEFI_RX_RELEASE_WORKAROUND) &&                        \
	PLATFORM_FFA_UEFI_RX_RELEASE_WORKAROUND
	// FIXME: QC Gunyah issue #284
	// Skip W1 MBZ check.
	(void)args;
	valid = true;
#else
	// W1 is only valid at the Non-secure physical FF-A instance.
	// MBZ otherwise.
	valid = ((uint32_t)(*args)[0] == 0U);
#endif

	if (valid) {
		thread_t *current = thread_get_self();
		assert(current != NULL);
		addrspace_t *addrspace = current->addrspace;
		assert(addrspace != NULL);

		ffa_rxtx_buffer_t *vm_rxtx_buffer = &addrspace->ffa_rxtx_buffer;
		spinlock_acquire(&vm_rxtx_buffer->lock);
		if (ffa_rxtx_buffer_is_valid(vm_rxtx_buffer)) {
			if (ffa_rxtx_buffer_hyp_is_rx_owner(vm_rxtx_buffer)) {
				// Caller did not have ownership of the RX
				// buffer.
				ffa_set_error(FFA_RET_DENIED, ret);
			} else {
				ffa_rxtx_buffer_set_hyp_is_rx_owner(
					vm_rxtx_buffer, true);
				ffa_success(ret);
			}
		} else {
			ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
		}
		spinlock_release(&vm_rxtx_buffer->lock);
	} else {
		LOG(ERROR, WARN, "FF-A: FFA_RX_RELEASE w1 is non-zero");
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
	}
}

// Copies data from a TX buffer into the hypervisor's internal buffer.
// Copying directly from the TX buffer after validating the memory might
// create a TOCTOU vulnerability. The buffer must be copied out before
// any validation.
bool
ffa_acquire_tx_buffer(ffa_rxtx_buffer_t *rxtx_buffer, size_t len)
{
	bool ret;

	assert(len <= ffa_hyp_rxtx_buffer_size);

	spinlock_acquire(&rxtx_buffer->lock);
	if (!ffa_rxtx_buffer_is_valid(rxtx_buffer)) {
		ret = false;
		goto out;
	}

	if (ffa_rxtx_buffer_is_tx_busy(rxtx_buffer)) {
		ret = false;
	} else {
		assert_debug(rxtx_buffer->info.tx_buffer_hyp_private != NULL);
		ffa_rxtx_buffer_set_is_tx_busy(rxtx_buffer, true);

		uint8_t *tx_buffer = ffa_rxtx_buffer_get_tx(rxtx_buffer);

		size_t copied_size =
			memscpy(rxtx_buffer->info.tx_buffer_hyp_private,
				rxtx_buffer->info.buffer_size, tx_buffer, len);
		ret = copied_size == len;
	}
out:
	spinlock_release(&rxtx_buffer->lock);

	return ret;
}

void
ffa_release_tx_buffer(ffa_rxtx_buffer_t *rxtx_buffer, size_t len)
{
	spinlock_acquire(&rxtx_buffer->lock);
	if (ffa_rxtx_buffer_is_valid(rxtx_buffer)) {
		assert_debug(rxtx_buffer->info.tx_buffer_hyp_private != NULL);
		ffa_rxtx_buffer_set_is_tx_busy(rxtx_buffer, false);

		(void)memset_s(rxtx_buffer->info.tx_buffer_hyp_private,
			       rxtx_buffer->info.buffer_size, 0, len);
	}
	spinlock_release(&rxtx_buffer->lock);
}
