// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>

#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <gpt.h>
#include <log.h>
#include <partition_init.h>
#include <preempt.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

static gpt_t gpt;

static gpt_entry_t
test_entry_init(gpt_type_t type, uint64_t value)
{
	return (gpt_entry_t){
		.type  = type,
		.value = { .raw = value },
	};
}

void
gpt_tests_add_offset(gpt_type_t type, gpt_value_t *value, size_t offset)
{
	value->raw += (type != GPT_TYPE_TEST_C) ? offset : (offset * 2);
}

bool
gpt_tests_values_equal(gpt_value_t x, gpt_value_t y)
{
	return x.raw == y.raw;
}

error_t
gpt_tests_callback(gpt_entry_t entry, size_t base, size_t size, gpt_arg_t arg)
{
	LOG(DEBUG, INFO,
	    "GPT callback: t {:d}, v {:#x}, [{:#x}, {:#x}], arg {:#x}",
	    entry.type, entry.value.raw, base, size, arg.test);

	return OK;
}

void
gpt_handle_tests_init(void)
{
	partition_t *partition = partition_get_root();
	assert(partition != NULL);

	gpt_config_t config = gpt_config_default();
	gpt_config_set_max_bits(&config, GPT_MAX_SIZE_BITS);

	register_t types = 0U;
	bitmap_set(&types, GPT_TYPE_TEST_A);
	bitmap_set(&types, GPT_TYPE_TEST_B);
	bitmap_set(&types, GPT_TYPE_TEST_C);

	error_t err = gpt_init(&gpt, partition, config, types);
	assert(err == OK);
}

bool
gpt_handle_tests_start(void)
{
	error_t err;

	preempt_disable();

	if (cpulocal_get_index() != 0U) {
		goto out;
	}

	assert(gpt_is_empty(&gpt));

	size_t	    base, size;
	gpt_entry_t e1, e2;

	base = 0x80000000U;
	size = 0x70000;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);

	err = gpt_insert(&gpt, base, size, e1, true);
	assert(err == OK);

	base = 0x80001000U;
	size = 0x4500;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);
	e2   = test_entry_init(GPT_TYPE_TEST_A, 0x900000);

	err = gpt_update(&gpt, base, size, e1, e2);
	assert(err == OK);

	base = 0x80020010U;
	size = 0x3;
	e2   = test_entry_init(GPT_TYPE_TEST_B, base);

	err = gpt_insert(&gpt, base, size, e2, false);
	assert(err == OK);

	base = 0x80040400U;
	size = 3;
	e1   = test_entry_init(GPT_TYPE_TEST_A, 0x80040400U);
	e2   = test_entry_init(GPT_TYPE_TEST_C, 0x400);

	err = gpt_update(&gpt, base, size, e1, e2);
	assert(err == OK);

	base = 0x80055555;
	size = 1234;
	e1   = test_entry_init(GPT_TYPE_TEST_A, 0x80055555);

	err = gpt_remove(&gpt, base, size, e1);
	assert(err == OK);

	gpt_dump_ranges(&gpt);

	base = 0x80000050;
	size = 0x20;
	e1   = test_entry_init(GPT_TYPE_TEST_A, 0x80000050);

	assert(!gpt_is_empty(&gpt));

	bool ret = gpt_is_contiguous(&gpt, base, size, e1);
	assert(ret);

	gpt_lookup_result_t lookup = gpt_lookup(&gpt, 0x8, 1U);
	LOG(DEBUG, INFO, "Lookup returned: {:d} {:#x} ({:#x})",
	    lookup.entry.type, lookup.entry.value.raw, lookup.size);

	lookup = gpt_lookup(&gpt, 0x80040001, 2);
	LOG(DEBUG, INFO, "Lookup returned: {:d} {:#x} ({:#x})",
	    lookup.entry.type, lookup.entry.value.raw, lookup.size);

	lookup = gpt_lookup(&gpt, 0x80050006, 0x20000);
	LOG(DEBUG, INFO, "Lookup returned: {:d} {:#x} ({:#x})",
	    lookup.entry.type, lookup.entry.value.raw, lookup.size);

	gpt_arg_t arg;

	base	 = 0x80000001;
	size	 = 0x6f000;
	arg.test = 0xfeed;

	err = gpt_walk(&gpt, base, size, GPT_TYPE_TEST_A, GPT_CALLBACK_TEST,
		       arg);
	assert(err == OK);

	base	 = 0x80040200U;
	size	 = 0x800;
	arg.test = 0xbeef;

	err = gpt_walk(&gpt, base, size, GPT_TYPE_TEST_C, GPT_CALLBACK_TEST,
		       arg);
	assert(err == OK);

	base = 0x100100U;
	size = 0x1;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);

	err = gpt_insert(&gpt, base, size, e1, false);
	assert(err == OK);

	base = 0x100300U;
	size = 0x1;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);

	err = gpt_insert(&gpt, base, size, e1, false);
	assert(err == OK);

	base = 0x100100U;
	size = 0x1;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);

	err = gpt_remove(&gpt, base, size, e1);
	assert(err == OK);

	gpt_dump_ranges(&gpt);

	// Partially invalid update.
	base = 0x80030000U;
	size = 0x50000;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);
	e2   = test_entry_init(GPT_TYPE_TEST_B, base);

	err = gpt_update(&gpt, base, size, e1, e2);
	assert(err != OK);

	size = 0x10;

	err = gpt_update(&gpt, base, size, e1, e2);
	assert(err == OK);

	gpt_dump_ranges(&gpt);

	// Attempt to insert invalid type.
	e1 = test_entry_init(GPT_TYPE_LEVEL, 0x213123123123);

	err = gpt_insert(&gpt, 0x919100123f23, 0x1012301230, e1, false);
	assert(err != OK);

	base = 0x70000000U;
	size = 0x20000000U;
	e1   = test_entry_init(GPT_TYPE_TEST_B, 0x50000000U);

	err = gpt_insert(&gpt, base, size, e1, false);
	assert(err == OK);

	gpt_dump_levels(&gpt);

	err = gpt_clear(&gpt, 0U, 0x100000000U);
	assert(err == OK);

	assert(gpt_is_empty(&gpt));

	base = 0U;
	size = 1U;
	e1   = test_entry_init(GPT_TYPE_TEST_A, base);

	err = gpt_insert(&gpt, base, size, e1, false);
	assert(err == OK);

	for (index_t i = 0U; i < GPT_MAX_SIZE_BITS; i += GPT_LEVEL_BITS) {
		base = util_bit(i);
		size = 1U;
		e1   = test_entry_init(GPT_TYPE_TEST_A, base);

		err = gpt_insert(&gpt, base, size, e1, false);
		assert(err == OK);
	}

	gpt_dump_ranges(&gpt);

	for (index_t i = 0U; i < GPT_MAX_SIZE_BITS; i += GPT_LEVEL_BITS) {
		base = util_bit(i);
		size = 1U;
		e1   = test_entry_init(GPT_TYPE_TEST_A, base);

		err = gpt_remove(&gpt, base, size, e1);
		assert(err == OK);
	}

	gpt_clear_all(&gpt);

	for (index_t i = 0U; i < GPT_LEVEL_ENTRIES; i++) {
		base = 0xffff00000000 + (i << 8);
		size = 1U << 8;
		e1   = test_entry_init(GPT_TYPE_TEST_A, base);

		err = gpt_insert(&gpt, base, size, e1, true);
		assert(err == OK);
	}

	gpt_dump_levels(&gpt);

	e1 = test_entry_init(GPT_TYPE_TEST_A, 1U);

	err = gpt_insert(&gpt, 0x1000, 1U, e1, false);
	assert(err == OK);

	err = gpt_insert(&gpt, 0x1002, 1U, e1, false);
	assert(err == OK);

	err = gpt_insert(&gpt, 0x1002, 1U, e1, false);
	assert(err == OK);

	e2 = test_entry_init(GPT_TYPE_TEST_B, 1U);

	err = gpt_insert(&gpt, 0x1010, 1U, e2, false);
	assert(err == OK);

	gpt_dump_levels(&gpt);

	e1 = test_entry_init(GPT_TYPE_TEST_C, 2U);

	err = gpt_insert(&gpt, 0U, 0x2000, e1, true);
	assert(err != OK);

	gpt_clear(&gpt, 1U, 0x10000U);

	gpt_dump_levels(&gpt);

	gpt_clear_all(&gpt);

	e1 = test_entry_init(GPT_TYPE_TEST_A, 0xdeadbeefbeadfeedU);

	// Base and size cause overflow
	err = gpt_insert(&gpt, 0xffffffffffffff00, 0x0, e1, true);
	assert(err == ERROR_ARGUMENT_INVALID);

	err = gpt_insert(&gpt, 0xffffffffffffff00, 0x100, e1, true);
	assert(err == ERROR_ARGUMENT_SIZE);

	err = gpt_insert(&gpt, 0xffffffffffffffff, 0x33333333, e1, true);
	assert(err == ERROR_ARGUMENT_INVALID);

	// Larger than max size supported
	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS), 0x1, e1, true);
	assert(err == ERROR_ARGUMENT_SIZE);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS + 1), 0x33333333, e1,
			 true);
	assert(err == ERROR_ARGUMENT_SIZE);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1, e1, true);
	assert(err == OK);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1, e1, true);
	assert(err == ERROR_BUSY);

	err = gpt_clear(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1U);
	assert(err == OK);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 2, 0x2, e1, true);
	assert(err == OK);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1, e1, true);
	assert(err == ERROR_BUSY);

	err = gpt_walk(&gpt, 0, util_bit(GPT_MAX_SIZE_BITS), GPT_TYPE_TEST_A,
		       GPT_CALLBACK_TEST, arg);
	assert(err == OK);

	err = gpt_clear(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1U);
	assert(err == OK);

	err = gpt_walk(&gpt, 0, util_bit(GPT_MAX_SIZE_BITS), GPT_TYPE_TEST_A,
		       GPT_CALLBACK_TEST, arg);
	assert(err == OK);

	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 2, 0x1, e1, true);
	assert(err == ERROR_BUSY);

	e2 = e1;
	e2.value.raw += 1;
	err = gpt_insert(&gpt, util_bit(GPT_MAX_SIZE_BITS) - 1, 0x1, e2, true);
	assert(err == OK);

	err = gpt_walk(&gpt, 0, util_bit(GPT_MAX_SIZE_BITS), GPT_TYPE_TEST_A,
		       GPT_CALLBACK_TEST, arg);
	assert(err == OK);

	err = gpt_insert(&gpt, 0x4000000000000, 0x33333333, e1, true);
	assert(err == OK);

	e1 = test_entry_init(GPT_TYPE_TEST_A, 0x3333444455550000U);

	err = gpt_insert(&gpt, 0x234000000000, 0x679823213, e1, true);
	assert(err == OK);

	// Attempt to insert range with overlap at end
	err = gpt_insert(&gpt, 0x233cdf123000, 0x82681e3f2, e1, true);
	assert(err != OK);

	e1 = test_entry_init(GPT_TYPE_TEST_A, 0x3333444455556666U);
	e2 = test_entry_init(GPT_TYPE_TEST_B, 0x777788889999);

	// Attempt to update range that doesn't match at the end
	err = gpt_update(&gpt, 0x234000006666, 0x73f8c532ab, e1, e2);
	assert(err != OK);

	err = gpt_update(&gpt, 0x234000006666, 0x123e34, e1, e2);
	assert(err == OK);

	gpt_dump_ranges(&gpt);

	base = 0x7ab2348e293;
	size = 0x123809193;

	e1 = test_entry_init(GPT_TYPE_TEST_A, 0x183741ea175);

	err = gpt_insert(&gpt, 0U, base, e1, false);
	assert(err == OK);

	e1.value.raw += base + size;

	err = gpt_insert(&gpt, base + size, GPT_MAX_SIZE - base - size, e1,
			 false);
	assert(err == OK);

	e1.value.raw -= size;

	err = gpt_insert(&gpt, base, size, e1, false);
	assert(err == OK);

	e1.value.raw -= base;

	// GPT should now have a single contiguous entry
	assert(gpt_is_contiguous(&gpt, 0U, GPT_MAX_SIZE, e1));

	gpt_dump_ranges(&gpt);

	gpt_destroy(&gpt);
out:
	preempt_enable();

	return false;
}

#else

extern char unused;

#endif
