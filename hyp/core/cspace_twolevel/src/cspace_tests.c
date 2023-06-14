// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>

#include <hyprights.h>

#include <atomic.h>
#include <cpulocal.h>
#include <cspace.h>
#include <object.h>
#include <partition.h>
#include <partition_alloc.h>
#include <spinlock.h>

#include <asm/event.h>

#include "event_handlers.h"

static cspace_t	      *test_cspace;
static cap_id_t	       test_cspace_master_cap;
static _Atomic count_t test_cspace_wait_count;
static _Atomic count_t test_cspace_finish_count;
static _Atomic uint8_t test_cspace_revoke_flag;

#define TEST_CAP_COPIES	     20U
#define TEST_CSPACE_MAX_CAPS ((PLATFORM_MAX_CORES * TEST_CAP_COPIES) + 1U)

CPULOCAL_DECLARE_STATIC(cap_id_t, test_caps)[TEST_CAP_COPIES];

void
tests_cspace_init(void)
{
	cspace_ptr_result_t cspace_ret;
	object_ptr_t	    obj;
	cap_id_result_t	    cap_ret;
	error_t		    err;

	cspace_create_t params = { NULL };

	cspace_ret = partition_allocate_cspace(partition_get_private(), params);
	assert(cspace_ret.e == OK);
	test_cspace = cspace_ret.r;
	spinlock_acquire(&test_cspace->header.lock);
	err = cspace_configure(test_cspace, TEST_CSPACE_MAX_CAPS);
	spinlock_release(&test_cspace->header.lock);
	assert(err == OK);
	err = object_activate_cspace(test_cspace);
	assert(err == OK);

	obj.cspace = test_cspace;
	cap_ret =
		cspace_create_master_cap(test_cspace, obj, OBJECT_TYPE_CSPACE);
	assert(cap_ret.e == OK);
	test_cspace_master_cap = cap_ret.r;

	atomic_init(&test_cspace_wait_count, 0U);
	atomic_init(&test_cspace_finish_count, PLATFORM_MAX_CORES);
	atomic_init(&test_cspace_revoke_flag, 0U);
}

static error_t
tests_cspace_cap_lookup(cap_id_t cap, cap_rights_t rights)
{
	object_ptr_result_t ret = cspace_lookup_object(
		test_cspace, cap, OBJECT_TYPE_CSPACE, rights, true);

	if (ret.e == OK) {
		assert(ret.r.cspace == test_cspace);
		object_put_cspace(ret.r.cspace);
	}

	return ret.e;
}

bool
tests_cspace_start(void)
{
	cap_id_result_t	    cap_ret;
	cap_id_t	   *cap		  = CPULOCAL(test_caps);
	cap_rights_cspace_t cspace_rights = cap_rights_cspace_default();
	cap_rights_t	    rights;
	error_t		    err;
	const count_t	    num_delete = TEST_CAP_COPIES / 2U;

	object_get_cspace_additional(test_cspace);

	cap_rights_cspace_set_test(&cspace_rights, true);
	rights = cap_rights_cspace_raw(cspace_rights);

	// Sync with other cores to maximise concurrent accesses
	(void)atomic_fetch_add_explicit(&test_cspace_wait_count, 1U,
					memory_order_relaxed);
	while (asm_event_load_before_wait(&test_cspace_wait_count) !=
	       PLATFORM_MAX_CORES) {
		asm_event_wait(&test_cspace_wait_count);
	}

	for (count_t i = 0U; i < TEST_CAP_COPIES; i++) {
		cap_ret = cspace_copy_cap(test_cspace, test_cspace,
					  test_cspace_master_cap, rights);
		assert(cap_ret.e == OK);
		cap[i] = cap_ret.r;
	}

	err = tests_cspace_cap_lookup(test_cspace_master_cap, rights);
	assert(err == OK);

	for (count_t i = 0U; i < TEST_CAP_COPIES; i++) {
		err = tests_cspace_cap_lookup(cap[i], rights);
		assert(err == OK);
	}

	cspace_rights = CAP_RIGHTS_CSPACE_ALL;
	rights	      = cap_rights_cspace_raw(cspace_rights);

	err = tests_cspace_cap_lookup(test_cspace_master_cap, rights);
	assert(err == OK);

	for (count_t i = 0U; i < TEST_CAP_COPIES; i++) {
		err = tests_cspace_cap_lookup(cap[i], rights);
		assert(err == ERROR_CSPACE_INSUFFICIENT_RIGHTS);
	}

	for (count_t i = 0U; i < num_delete; i++) {
		err = cspace_delete_cap(test_cspace, cap[i]);
		assert(err == OK);
	}

	if (atomic_fetch_sub_explicit(&test_cspace_finish_count, 1U,
				      memory_order_release) == 1U) {
		err = cspace_revoke_caps(test_cspace, test_cspace_master_cap);
		assert(err == OK);

		err = cspace_delete_cap(test_cspace, test_cspace_master_cap);
		assert(err == OK);

		asm_event_store_and_wake(&test_cspace_revoke_flag, 1U);
	} else {
		while (asm_event_load_before_wait(&test_cspace_revoke_flag) ==
		       0U) {
			asm_event_wait(&test_cspace_revoke_flag);
		}
	}

	// Delete the revoked caps
	for (count_t i = num_delete; i < TEST_CAP_COPIES; i++) {
		err = cspace_delete_cap(test_cspace, cap[i]);
		assert(err == OK);
	}

	object_put_cspace(test_cspace);

	return false;
}
#else

extern char unused;

#endif
