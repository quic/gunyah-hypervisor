// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <irq.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_irq.h>
#include <qcbor.h>
#include <range_tree.h>
#include <rcu.h>
#include <refcount.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vgic.h>

#include <events/vgic.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "gicv3_its.h"
#include "internal.h"
#include "vgic.h"

#if VGIC_HAS_LPI && !GICV3_HAS_VLPI_V4_1
static vgic_lpi_range_ptr_result_t
vgic_irq_range_add_vlpis(irq_t base, count_t size, thread_t *vcpu)
	REQUIRE_SPINLOCK(vcpu -> vgic_vic->lpi_range_lock)

{
	error_t			    err;
	vgic_lpi_range_ptr_result_t ret;

	if ((size_t)size > (SIZE_MAX / sizeof(irq_t))) {
		ret = vgic_lpi_range_ptr_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	if (util_add_overflows(base, size - 1U)) {
		ret = vgic_lpi_range_ptr_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	size_t		  alloc_size = sizeof(vgic_lpi_range_t);
	void_ptr_result_t ptr_r	     = partition_alloc(
		partition_get_private(), alloc_size, alignof(vgic_lpi_range_t));
	if (ptr_r.e != OK) {
		ret = vgic_lpi_range_ptr_result_error(ptr_r.e);
		goto out;
	}
	(void)memset_s(ptr_r.r, alloc_size, 0, alloc_size);
	vgic_lpi_range_t *range = (vgic_lpi_range_t *)ptr_r.r;

	atomic_store_relaxed(&range->vcpu, vcpu);

	err = irq_range_add(&range->range, IRQ_RANGE_TYPE_VGIC_LPI, base, size);
	if (err != OK) {
		partition_free(partition_get_private(), range, alloc_size);
		ret = vgic_lpi_range_ptr_result_error(err);
		goto out;
	}

	ret = vgic_lpi_range_ptr_result_ok(range);
out:
	return ret;
}

static irq_result_t
vgic_set_used_bit(vgic_lpi_range_t *lpi_range, index_t lpi_base, virq_t vlpi)
{
	irq_result_t ret;

	BITMAP_FOREACH_CLEAR_BEGIN(offset, lpi_range->used_masks,
				   VGIC_LPI_ENTRY_RANGE_SIZE)
		bitmap_set(lpi_range->used_masks, offset);
		lpi_range->lpis[offset].vlpi = vlpi;
		refcount_init(&lpi_range->lpis[offset].refcount);
		ret = irq_result_ok(lpi_base + offset);
		goto out;
	BITMAP_FOREACH_CLEAR_END

	ret = irq_result_error(ERROR_NORESOURCES);

out:
	return ret;
}

// NOTE: This function assumes that the given vLPI is not assigned
// yet to the given vCPU, therefore, it must not exist in the LPI ranges.
static irq_result_t
vgic_reserve_free_lpi_entry(thread_t *vcpu, virq_t vlpi)
	REQUIRE_SPINLOCK(vcpu -> vgic_vic->lpi_range_lock)
{
	irq_result_t ret;
	index_t	     free_lpi_range = platform_irq_msi_max();

	assert(vcpu != NULL);

	rcu_read_start();

	for (index_t lpi_base = GIC_LPI_BASE; lpi_base < platform_irq_msi_max();
	     lpi_base += VGIC_LPI_ENTRY_RANGE_SIZE) {
		irq_range_t *range = irq_lookup_range(lpi_base);

		if (range == NULL) {
			free_lpi_range = lpi_base;
			break;
		} else if ((range != NULL) &&
			   (range->range_type == IRQ_RANGE_TYPE_VGIC_LPI)) {
			vgic_lpi_range_t *lpi_range =
				vgic_lpi_range_container_of_range(range);
			thread_t *owner_vcpu =
				atomic_load_relaxed(&lpi_range->vcpu);

			if ((NULL != owner_vcpu) && (owner_vcpu != vcpu)) {
				continue;
			} else if (NULL == owner_vcpu) {
				// This only happens if a range was previously
				// used but has become empty due to a vLPI
				// being unmapped. We do not ever free ranges
				// to avoid allowing a guest to cause memory
				// fragmentation by (un)mapping vLPIs.
				atomic_store_relaxed(&lpi_range->vcpu, vcpu);
			}

			if (!bitmap_full(lpi_range->used_masks,
					 VGIC_LPI_ENTRY_RANGE_SIZE)) {
				ret = vgic_set_used_bit(lpi_range, lpi_base,
							vlpi);
				if (ret.e == OK) {
					goto out;
				}
			}
		}
	}

	if (free_lpi_range < platform_irq_msi_max()) {
		vgic_lpi_range_ptr_result_t range_r = vgic_irq_range_add_vlpis(
			free_lpi_range, VGIC_LPI_ENTRY_RANGE_SIZE, vcpu);

		if (range_r.e == OK) {
			ret = vgic_set_used_bit(range_r.r, free_lpi_range,
						vlpi);
		} else {
			ret = irq_result_error(range_r.e);
		}
	} else {
		// No free entry found
		ret = irq_result_error(ERROR_NORESOURCES);
	}

out:
	rcu_read_finish();

	return ret;
}

static void
vgic_try_init_vlpi_dstate(_Atomic vgic_delivery_state_t *dstate)
	REQUIRE_RCU_READ
{
	vgic_delivery_state_t new_dstate = vgic_delivery_state_default();
	vgic_delivery_state_t def_dstate = vgic_delivery_state_default();

	assert(dstate != NULL);

	// LPIs are always edge-triggered.
	vgic_delivery_state_set_cfg_is_edge(&new_dstate, true);
	// LPIs are always group1 interrupts.
	vgic_delivery_state_set_group1(&new_dstate, true);
#if VGIC_HAS_1N
	// 1 of N applies to SPIs only.
	vgic_delivery_state_set_route_1n(&new_dstate, false);
#endif
	// Try updating the dstate. If it was already initialized, e.g. by a
	// MAPTI command mapping the same vLPI, keep its state untouched.
	(void)atomic_compare_exchange_strong_explicit(dstate, &def_dstate,
						      new_dstate,
						      memory_order_relaxed,
						      memory_order_relaxed);
}

vgic_delivery_state_t _Atomic *
vgic_allocate_vlpi_dstate(thread_t *target_vcpu, virq_t vlpi)
{
	_Atomic vgic_delivery_state_t *ret;
	error_t			       err;
	vgic_vlpi_range_t	      *range;
	range_tree_lookup_result_t     vlpi_lookup_r;
	index_t dstate_index = vlpi % VGIC_VLPI_ENTRY_RANGE_SIZE;

	spinlock_acquire(&target_vcpu->vgic_vlpi_lock);

	vlpi_lookup_r =
		range_tree_lookup(&target_vcpu->vgic_vlpi_tree, vlpi, 1u);
	if (NULL == vlpi_lookup_r.node) {
		size_t		  alloc_size = sizeof(vgic_vlpi_range_t);
		size_t		  size;
		size_t		  base;
		void_ptr_result_t ptr_r =
			partition_alloc(target_vcpu->header.partition,
					alloc_size, alignof(vgic_vlpi_range_t));
		if (ptr_r.e != OK) {
			ret = NULL;
			goto out;
		}
		(void)memset_s(ptr_r.r, alloc_size, 0, alloc_size);
		range = (vgic_vlpi_range_t *)ptr_r.r;

		size = VGIC_VLPI_ENTRY_RANGE_SIZE;
		base = (vlpi / size) * (size);
		range_tree_lock_nopreempt(&target_vcpu->vgic_vlpi_tree);
		err = range_tree_insert(&target_vcpu->vgic_vlpi_tree,
					&range->node, base, size);

		if (err != OK) {
			range_tree_unlock_nopreempt(
				&target_vcpu->vgic_vlpi_tree);
			partition_free(target_vcpu->header.partition, range,
				       sizeof(vgic_vlpi_range_t));
			ret = NULL;
			goto out;
		}

		range->partition = object_get_partition_additional(
			target_vcpu->header.partition);
		range_tree_unlock_nopreempt(&target_vcpu->vgic_vlpi_tree);
	} else {
		range = vgic_vlpi_range_container_of_node(vlpi_lookup_r.node);
	}

	// Initialise default values if it has not been done yet.
	vgic_try_init_vlpi_dstate(&range->dstates[dstate_index]);
	ret = &range->dstates[dstate_index];
out:
	spinlock_release(&target_vcpu->vgic_vlpi_lock);

	return ret;
}

irq_result_t
vgic_get_lpi(thread_t *vcpu, virq_t vlpi)
{
	irq_result_t ret;

	for (index_t lpi_base = GIC_LPI_BASE; lpi_base < platform_irq_msi_max();
	     lpi_base += VGIC_LPI_ENTRY_RANGE_SIZE) {
		rcu_read_start();

		irq_range_t *range = irq_lookup_range(lpi_base);
		if ((range == NULL) ||
		    (range->range_type != IRQ_RANGE_TYPE_VGIC_LPI)) {
			rcu_read_finish();
			continue;
		}

		vgic_lpi_range_t *lpi_range =
			vgic_lpi_range_container_of_range(range);

		thread_t *owner_vcpu = atomic_load_relaxed(&lpi_range->vcpu);
		if ((NULL != owner_vcpu) && (vcpu != owner_vcpu)) {
			// The lpi is associated with a different vCPU;
			rcu_read_finish();
			continue;
		} else if (NULL == owner_vcpu) {
			rcu_read_finish();
			continue;
		}

		BITMAP_FOREACH_SET_BEGIN(offset, lpi_range->used_masks,
					 VGIC_LPI_ENTRY_RANGE_SIZE)
			if (vlpi == lpi_range->lpis[offset].vlpi) {
				ret = irq_result_ok(lpi_base + offset);
				rcu_read_finish();
				goto out;
			}
		BITMAP_FOREACH_SET_END

		rcu_read_finish();
	}

	ret = irq_result_error(ERROR_LPI_NOT_ASSIGNED);
out:

	return ret;
}

static void
vgic_get_lpi_additional(thread_t *vcpu, irq_t lpi)
	REQUIRE_SPINLOCK(vcpu -> vgic_vic->lpi_range_lock)
{
	vgic_lpi_range_t *lpi_range;
	index_t		  range_index = lpi % VGIC_LPI_ENTRY_RANGE_SIZE;
	irq_range_t	 *range;

	rcu_read_start();

	range = irq_lookup_range(lpi);
	assert(range != NULL);
	lpi_range = vgic_lpi_range_container_of_range(range);

	// We can be assume that the refcount is non-zero, as the reference
	// count for LPIs is only incremented if they are already marked as
	// in use. In this case, the refcount is guaranteed to be initialized.
	refcount_get_additional(&lpi_range->lpis[range_index].refcount);

	rcu_read_finish();
}

irq_result_t
vgic_map_vlpi(thread_t *vcpu, virq_t vlpi)
{
	irq_result_t lpi_r;

	assert(vcpu != NULL);
	assert(vcpu->vgic_vic != NULL);

	// Grab the lpi_range lock so that in case an existing range is found,
	// we can increment the refcount before it is freed by a parallel
	// unmap. Also protects vgic_reserve_free_lpi_entry().
	spinlock_acquire(&vcpu->vgic_vic->lpi_range_lock);

	lpi_r = vgic_get_lpi(vcpu, vlpi);
	if (lpi_r.e != OK) {
		lpi_r = vgic_reserve_free_lpi_entry(vcpu, vlpi);
	} else {
		vgic_get_lpi_additional(vcpu, lpi_r.r);
	}

	spinlock_release(&vcpu->vgic_vic->lpi_range_lock);

	return lpi_r;
}

cpu_index_t
vgic_try_set_vcpu_lpi_affinity(thread_t *vcpu)
{
	cpu_index_t expected_state = PLATFORM_MAX_CORES;
	cpu_index_t affinity;
	cpu_index_t ret;

	scheduler_lock(vcpu);
	affinity = scheduler_get_affinity(vcpu);
	scheduler_unlock(vcpu);

	if (atomic_compare_exchange_strong_explicit(
		    &vcpu->vgic_lpi_affinity, &expected_state, affinity,
		    memory_order_relaxed, memory_order_relaxed)) {
		ret = affinity;
	} else {
		ret = expected_state;
	}

	return ret;
}

cpu_index_result_t
vgic_get_lpi_affinity(irq_t lpi)
{
	cpu_index_result_t ret;
	irq_range_t	  *range;
	vgic_lpi_range_t  *lpi_range;
	thread_t	  *vcpu;

	rcu_read_start();

	range = irq_lookup_range(lpi);
	if ((range == NULL) || (range->range_type != IRQ_RANGE_TYPE_VGIC_LPI)) {
		ret = cpu_index_result_error(ERROR_LPI_NOT_ASSIGNED);
		goto out_rcu;
	}

	lpi_range = vgic_lpi_range_container_of_range(range);
	vcpu	  = atomic_load_relaxed(&lpi_range->vcpu);
	if (vcpu == NULL) {
		ret = cpu_index_result_error(ERROR_LPI_NOT_ASSIGNED);
		goto out_rcu;
	}

	ret = cpu_index_result_ok(
		atomic_load_relaxed(&vcpu->vgic_lpi_affinity));

out_rcu:
	rcu_read_finish();

	return ret;
}

vgic_delivery_state_t _Atomic *
vgic_find_vlpi_dstate(thread_t *vcpu, virq_t vlpi)
{
	_Atomic vgic_delivery_state_t *ret_dstate;
	range_tree_lookup_result_t     vlpi_lookup_r =
		range_tree_lookup(&vcpu->vgic_vlpi_tree, vlpi, 1U);

	if (vlpi_lookup_r.node != NULL) {
		vgic_vlpi_range_t *vlpi_range =
			vgic_vlpi_range_container_of_node(vlpi_lookup_r.node);
		ret_dstate =
			&vlpi_range->dstates[vlpi % VGIC_VLPI_ENTRY_RANGE_SIZE];
	} else {
		ret_dstate = NULL;
	}

	return ret_dstate;
}

error_t
vlpi_assert(thread_t *vcpu, virq_t vlpi)
{
	error_t			       ret;
	_Atomic vgic_delivery_state_t *dstate;

	rcu_read_start();
	dstate = vgic_find_vlpi_dstate(vcpu, vlpi);

	if (dstate != NULL) {
		vgic_delivery_state_t assert_dstate =
			vgic_delivery_state_default();

		vgic_delivery_state_set_edge(&assert_dstate, true);
		(void)vgic_deliver(vlpi, vcpu->vgic_vic, vcpu, NULL, dstate,
				   assert_dstate, false);
		ret = OK;
	} else {
		ret = ERROR_LPI_NOT_ASSIGNED;
	}
	rcu_read_finish();

	return ret;
}

irq_result_t
vgic_unmap_vlpi(thread_t *vcpu, irq_t vlpi)
{
	irq_result_t	  ret;
	vic_t		 *vic = vcpu->vgic_vic;
	irq_result_t	  plpi_r;
	irq_t		  lpi;
	irq_range_t	 *range;
	vgic_lpi_range_t *lpi_range;
	index_t		  offset;
	thread_t	 *old_vcpu;

	spinlock_acquire(&vic->lpi_range_lock);

	plpi_r = vgic_get_lpi(vcpu, vlpi);
	if (OK != plpi_r.e) {
		ret = irq_result_error(plpi_r.e);
		goto out_locked;
	}

	lpi = plpi_r.r;

	rcu_read_start();

	range = irq_lookup_range(lpi);
	if (NULL == range) {
		ret = irq_result_error(ERROR_LPI_NOT_ASSIGNED);
		goto out_locked_rcu;
	}
	lpi_range = vgic_lpi_range_container_of_range(range);
	offset	  = lpi % VGIC_LPI_ENTRY_RANGE_SIZE;

	old_vcpu = atomic_load_relaxed(&lpi_range->vcpu);
	if (vcpu != old_vcpu) {
		ret = irq_result_error(ERROR_ARGUMENT_INVALID);
		goto out_locked_rcu;
	}

	ret = plpi_r;

	// If there are still references to this pLPI, do not free the entry
	// yet.
	if (!refcount_put(&lpi_range->lpis[offset].refcount)) {
		goto out_locked_rcu;
	}

	bitmap_clear(lpi_range->used_masks, offset);
	lpi_range->lpis[offset].vlpi = 0u;
	if (bitmap_empty(lpi_range->used_masks, VGIC_LPI_ENTRY_RANGE_SIZE)) {
		// No more LPIs allocated in this range, release
		// ownership.
		rcu_enqueue(&lpi_range->rcu_entry,
			    RCU_UPDATE_CLASS_LPI_RELEASE_RANGE);
	}
out_locked_rcu:
	rcu_read_finish();
out_locked:
	spinlock_release(&vic->lpi_range_lock);

	return ret;
}

void
vgic_set_vlpi_pending_sync(thread_t *vcpu, irq_t vlpi)
{
	index_t range_start =
		util_balign_down(vlpi, VGIC_VLPI_ENTRY_RANGE_SIZE);
	range_tree_lookup_result_t vlpi_lookup_r;

	vlpi_lookup_r =
		range_tree_lookup(&vcpu->vgic_vlpi_tree, range_start, 1U);

	if (NULL != vlpi_lookup_r.node) {
		vgic_vlpi_range_t *vlpi_range =
			vgic_vlpi_range_container_of_node(vlpi_lookup_r.node);

		bitmap_atomic_set(vlpi_range->pending_masks,
				  vlpi % VGIC_VLPI_ENTRY_RANGE_SIZE,
				  memory_order_relaxed);
	}
}

void
vgic_clear_vlpi_pending_sync(thread_t *vcpu, irq_t vlpi)
{
	range_tree_lookup_result_t vlpi_lookup_r;

	vlpi_lookup_r = range_tree_lookup(&vcpu->vgic_vlpi_tree, vlpi, 1U);

	if (NULL != vlpi_lookup_r.node) {
		vgic_vlpi_range_t *vlpi_range =
			vgic_vlpi_range_container_of_node(vlpi_lookup_r.node);

		bitmap_atomic_clear(vlpi_range->pending_masks,
				    vlpi % VGIC_VLPI_ENTRY_RANGE_SIZE,
				    memory_order_relaxed);
	}
}

static error_t
vgic_vlpi_deliver_pending(vic_t *vic, _Atomic vgic_delivery_state_t *dstate,
			  irq_t vlpi)
{
	vgic_delivery_state_t old_dstate;
	vgic_delivery_state_t change_dstate;
	bool		      pending;
	bool		      listed;
	bool		      enabled;
	error_t		      ret;

	if (NULL == dstate) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	old_dstate    = atomic_load_relaxed(dstate);
	pending	      = vgic_delivery_state_get_edge(&old_dstate);
	listed	      = vgic_delivery_state_get_listed(&old_dstate);
	enabled	      = vgic_delivery_state_get_enabled(&old_dstate);
	change_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_edge(&change_dstate, true);

	if (!listed && enabled && pending) {
		thread_t *target =
			vgic_get_route_from_state(vic, old_dstate, false);
		(void)vgic_deliver(vlpi, vic, target, NULL, dstate,
				   change_dstate, false);
	}

	ret = OK;

out:
	return ret;
}

void
vgic_vlpi_deliver_pending_sync(thread_t *vcpu, size_t max_lpi)
{
	index_t range_start;

	for (range_start = GIC_LPI_BASE;
	     range_start < (max_lpi - VGIC_VLPI_ENTRY_RANGE_SIZE);
	     range_start += VGIC_VLPI_ENTRY_RANGE_SIZE) {
		index_t			   bit;
		vgic_vlpi_range_t	  *vlpi_range;
		range_tree_lookup_result_t vlpi_lookup_r = range_tree_lookup(
			&vcpu->vgic_vlpi_tree, range_start, 1U);

		if (NULL == vlpi_lookup_r.node) {
			continue;
		}

		vlpi_range =
			vgic_vlpi_range_container_of_node(vlpi_lookup_r.node);

		// Not using bitmap_ffs here to ignore concurrent updates to
		// bits we have already checked.
		for (bit = 0; bit < VGIC_VLPI_ENTRY_RANGE_SIZE; bit++) {
			virq_t vlpi = range_start + bit;

			if (!bitmap_atomic_test_and_clear(
				    vlpi_range->pending_masks, bit,
				    memory_order_relaxed)) {
				continue;
			}

			vgic_vlpi_deliver_pending(vcpu->vgic_vic,
						  &vlpi_range->dstates[bit],
						  vlpi);
		}
	}
}

void
vgic_vlpi_update_route(_Atomic vgic_delivery_state_t *dstate, irq_t vlpi,
		       thread_t *vcpu)
{
	index_t		      route_index;
	vgic_delivery_state_t old_dstate;
	vgic_delivery_state_t new_dstate;

	assert(dstate != NULL);
	assert(vcpu != NULL);

	route_index = vcpu->vgic_gicr_index;

	old_dstate = atomic_load_relaxed(dstate);
	do {
		new_dstate = old_dstate;

		vgic_delivery_state_set_route(&new_dstate, route_index);
		// We might need to reroute a listed LPI, set the sync flag.
		if (vgic_delivery_state_get_listed(&old_dstate)) {
			vgic_delivery_state_set_need_sync(&new_dstate, true);
			vgic_set_vlpi_pending_sync(vcpu, vlpi);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));
}

bool
vgic_handle_dstate_update(thread_t *vcpu, irq_t vlpi,
			  vgic_delivery_state_t new_dstate,
			  vlpi_updated_flags_t	updated)
{
	bool enabled_changed = (vlpi_updated_flags_get_enabled(&updated) ||
				vlpi_updated_flags_get_disabled(&updated));
	bool enabled	     = vlpi_updated_flags_get_enabled(&updated);
	bool sync_needed     = vgic_dstate_is_sync_needed(new_dstate, updated);
	irq_result_t plpi_r;

	// If a sync is needed, set the bit in the level1 table
	// pending bitmask. For LPIs, which are not listed, the
	// SYNC command will have to invoke
	// vgic_change_irq_pending(). For all other LPIs which
	// have changed, it is enough to call vgic_sync_vcpu().
	if (sync_needed) {
		vgic_set_vlpi_pending_sync(vcpu, vlpi);
	}

	// Check if this vLPI has an associated pLPI. If this is the case,
	// update the pLPI configuration. The calling function will take care of
	// triggering the SYNC ITS command.
	plpi_r = vgic_get_lpi(vcpu, vlpi);
	if (OK == plpi_r.e) {
		if (enabled_changed && enabled) {
			gicv3_lpi_enable(plpi_r.r);
		} else if (enabled_changed && !enabled) {
			gicv3_lpi_disable(plpi_r.r);
		}
	}

	return sync_needed;
}

bool_result_t
vgic_inv_vlpi(vic_t *vic, thread_t *vcpu, irq_t vlpi,
	      _Atomic vgic_delivery_state_t *dstate)
{
	vgic_delivery_state_result_t new_dstate_r;
	vgic_delivery_state_t	     new_dstate;
	vlpi_updated_flags_t	     updated;
	bool_result_t		     ret;
	gic_lpi_prop_result_t	     lpi_prop_res;
	bool			     sync_needed;

	assert(vcpu != NULL);

	if (dstate == NULL) {
		dstate = vgic_find_vlpi_dstate(vcpu, vlpi);

		if (NULL == dstate) {
			ret = bool_result_error(ERROR_ARGUMENT_INVALID);
			goto out;
		}
	}

	lpi_prop_res = vgic_gicr_copy_propbase_one(vic, vcpu, vlpi);
	if (OK != lpi_prop_res.e) {
		ret = bool_result_error(lpi_prop_res.e);
		goto out;
	}

	new_dstate_r =
		vgic_update_dstate_from_prop(dstate, lpi_prop_res.r, &updated);

	if (OK != new_dstate_r.e) {
		ret = bool_result_error(new_dstate_r.e);
		goto out;
	}

	new_dstate = new_dstate_r.r;
	sync_needed =
		vgic_handle_dstate_update(vcpu, vlpi, new_dstate, updated);
	ret = bool_result_ok(sync_needed);
out:

	return ret;
}

irq_result_t
vgic_move_vlpi(thread_t *source_vcpu, thread_t *target_vcpu, irq_t vlpi,
	       bool purely_virtual)
{
	irq_result_t		       plpi_old_r;
	irq_result_t		       plpi_new_r;
	irq_result_t		       ret;
	bool			       enabled;
	_Atomic vgic_delivery_state_t *source_dstate =
		vgic_find_vlpi_dstate(source_vcpu, vlpi);
	_Atomic vgic_delivery_state_t *target_dstate;
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(source_dstate);

	if (NULL == target_vcpu) {
		ret = irq_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	// Check if there is already a dstate for this vLPI on the target vCPU.
	// If not, allocate and initialize it.
	target_dstate = vgic_find_vlpi_dstate(target_vcpu, vlpi);
	if (target_dstate == NULL) {
		target_dstate = vgic_allocate_vlpi_dstate(target_vcpu, vlpi);
		if (target_dstate == NULL) {
			ret = irq_result_error(ERROR_NORESOURCES);
			goto out;
		}
	} else {
		// Even if the dstate has already been allocated previously as
		// part of a range, it may stilll have to be initialized.
		// vgic_allocate_vlpi_dstate() only initializes the dstate for
		// the vLPI passed to it, not the entire range.
		vgic_try_init_vlpi_dstate(target_dstate);
	}

	(void)vgic_delivery_state_atomic_union(target_dstate, old_dstate,
					       memory_order_relaxed);

	// Don't bother looking for a pLPI if this is a purely virtual LPI
	// which has no corresponding physical device.
	if (!purely_virtual) {
		// GICR_INVLPIR is only guaranteeed to be implemented on
		// GICv4.1, so we cannot invalidate by LPI here.  Instead, the
		// calling function is responsible for ensuring that all
		// configuration updates are visible to the redistributor.

		plpi_old_r = vgic_unmap_vlpi(source_vcpu, vlpi);
		if (OK != plpi_old_r.e) {
			ret = plpi_old_r;
			goto out;
		}

		// Because we assign pLPI ranges to individual vCPUs, we have
		// to move to a different pLPI range. Disable the old pLPI,
		// remove the previousmapping and create a new one. Then enable
		// the new pLPI and update the dstate with the new route.

		enabled = gicv3_lpi_is_enabled(plpi_old_r.r);
		if (enabled) {
			gicv3_lpi_disable(plpi_old_r.r);
		}

		plpi_new_r = vgic_map_vlpi(target_vcpu, vlpi);
		ret	   = plpi_new_r;

		if (OK != plpi_new_r.e) {
			goto out;
		}

		if (enabled) {
			gicv3_lpi_enable(plpi_new_r.r);
		} else {
			gicv3_lpi_disable(plpi_new_r.r);
		}

	} else {
		ret = irq_result_ok(0);
	}

	// We only reach here if an actual routing change was requested,
	// so no need to double check.
	vgic_vlpi_update_route(target_dstate, vlpi, target_vcpu);

	// Set the pending sync flag to ensure LRs are synchronized.
	vgic_set_vlpi_pending_sync(target_vcpu, vlpi);
	vgic_clear_vlpi_pending_sync(source_vcpu, vlpi);

out:

	return ret;
}

error_t
vgic_clear_vlpi_pending(thread_t *vcpu, virq_t vlpi)
{
	error_t			       err;
	_Atomic vgic_delivery_state_t *dstate;
	vgic_delivery_state_result_t   dstate_r;

	rcu_read_start();
	dstate = vgic_find_vlpi_dstate(vcpu, vlpi);

	if (dstate == NULL) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	dstate_r = vgic_update_dstate_pending(dstate, false);
	err	 = dstate_r.e;
out:
	rcu_read_finish();

	return err;
}

void
vgic_handle_lpi_received(thread_t *vcpu, irq_t vlpi)
{
	error_t err = vlpi_assert(vcpu, vlpi);

	if (err == ERROR_LPI_NOT_ASSIGNED) {
		TRACE(VGIC, WARN, "vLPI received {:d} but it is not mapped.",
		      vlpi);
	} else if (err != OK) {
		TRACE(VGIC, WARN,
		      "vLPI {:d} could not be asserted, error: {:d}.", vlpi,
		      (register_t)err);
	}
}

bool
vgic_lpi_handle_irq_dispatch(irq_t irq, irq_range_t *range, size_t offset)
{
	vgic_lpi_range_t *lpi_range = vgic_lpi_range_container_of_range(range);
	irq_t		  vlpi	    = lpi_range->lpis[offset].vlpi;

	if (!bitmap_isset(lpi_range->used_masks, (index_t)offset)) {
		TRACE(VGIC, WARN, "LPI {:d} received but it is not mapped.",
		      irq);
	} else {
		trigger_vgic_lpi_received_event(
			atomic_load_relaxed(&lpi_range->vcpu), vlpi);
	}

	return false;
}

error_t
vgic_vlpi_handle_object_activate_thread(thread_t *thread)
{
	error_t	     err;
	partition_t *partition = thread->header.partition;

	assert(partition != NULL);

	err = range_tree_init(&thread->vgic_vlpi_tree, partition);
	spinlock_init(&thread->vgic_vlpi_lock);
	atomic_store_relaxed(&thread->vgic_lpi_affinity, PLATFORM_MAX_CORES);

	return err;
}

void
vgic_vlpi_handle_object_deactivate_thread(thread_t *thread)
{
	partition_t *partition = thread->header.partition;

	assert(thread_get_self() != thread);
	assert(partition != NULL);

	range_tree_lock(&thread->vgic_vlpi_tree);
	range_tree_destroy(&thread->vgic_vlpi_tree,
			   RANGE_TREE_NODE_TYPE_VLPI_RANGE);
	range_tree_unlock(&thread->vgic_vlpi_tree);
}

void
vgic_vlpi_unwind_object_activate_thread(thread_t *thread)
{
	vgic_vlpi_handle_object_deactivate_thread(thread);
}

bool
vgic_release_vlpi_range(range_tree_node_t *node)
{
	vgic_vlpi_range_t *range = vgic_vlpi_range_container_of_node(node);
	rcu_enqueue(&range->rcu_entry, RCU_UPDATE_CLASS_VLPI_RELEASE_RANGE);
	return true;
}

rcu_update_status_t
vgic_free_vlpi_range(rcu_entry_t *entry)
{
	vgic_vlpi_range_t *range =
		vgic_vlpi_range_container_of_rcu_entry(entry);
	assert(range != NULL);

	partition_t *partition = range->partition;
	assert(partition != NULL);
	(void)partition_free(partition, range, sizeof(*range));

	object_put_partition(partition);

	return rcu_update_status_default();
}

rcu_update_status_t
vgic_free_lpi_range(rcu_entry_t *entry)
{
	vgic_lpi_range_t *range = vgic_lpi_range_container_of_rcu_entry(entry);
	assert(range != NULL);

	atomic_store_relaxed(&range->vcpu, NULL);

	return rcu_update_status_default();
}
#endif // VGIC_HAS_LPI && !GICV3_HAS_VLPI_V4_1
