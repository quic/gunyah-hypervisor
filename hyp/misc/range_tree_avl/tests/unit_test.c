// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#undef register_t

#include <string.h>

#include <atomic.h>
#include <range_tree.h>
#include <util.h>

#define TEST_BEGIN(name)                                                       \
	do {                                                                   \
		test_failed = false;                                           \
		printf("TEST: %s...\n", name);                                 \
	} while (0)

#define TEST_ASSERT(condition, message)                                        \
	do {                                                                   \
		if (condition) {                                               \
			printf("✓ PASS: %s\n", message);                       \
			++asserts_passed;                                      \
		} else {                                                       \
			printf("✗ FAIL: %s\n", message);                       \
			++asserts_failed;                                      \
			test_failed = true;                                    \
		}                                                              \
	} while (0)

#define TEST_END()                                                             \
	do {                                                                   \
		if (test_failed) {                                             \
			printf("FAILED\n\n");                                  \
			++tests_failed;                                        \
		} else {                                                       \
			printf("PASSED\n\n");                                  \
			++tests_passed;                                        \
		}                                                              \
	} while (0)

static bool   test_failed    = false;
static size_t tests_passed   = 0;
static size_t tests_failed   = 0;
static size_t asserts_passed = 0;
static size_t asserts_failed = 0;

void
trigger_range_tree_release_node_event(range_tree_node_type_t type,
				      range_tree_node_t	    *node)
{
	(void)type;
	(void)node;
}

// Test: Rotation counters should increment correctly
static void
test_rotation_counters_increment(void)
{
	TEST_BEGIN("rotation_counters_increment");

	const size_t size      = 0x1;
	const size_t low_addr  = 0x10;
	const size_t med_addr  = 0x20;
	const size_t high_addr = 0x30;

	struct param {
		size_t		  base;
		size_t		  size;
		range_tree_node_t node;
		size_t		  nr_rotations_expected;
	} params[][3] = {
		// Left rotation case
		{
			{ .base			 = low_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = med_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = high_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 1 },
		},
		// Right rotation case
		{
			{ .base			 = high_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = med_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = low_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 1 },
		},
		// Left-Right rotation case
		{
			{ .base			 = high_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = low_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = med_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 2 },
		},
		// Right-Left rotation case
		{
			{ .base			 = low_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = high_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 0 },
			{ .base			 = med_addr,
			  .size			 = size,
			  .node			 = { 0 },
			  .nr_rotations_expected = 2 },
		},
	};

	for (size_t test = 0; test < util_array_size(params); ++test) {
		range_tree_t tree;
		error_t	     err = range_tree_init(&tree, NULL);
		TEST_ASSERT(err == OK,
			    "Range tree initialization should succeed");
		TEST_ASSERT(atomic_load_relaxed(&tree.nr_rotations) == 0,
			    "Rotations should be 0");
		for (size_t i = 0; i < util_array_size(params[test]); ++i) {
			const size_t nr_rotations_start =
				atomic_load_relaxed(&tree.nr_rotations);
			range_tree_lock(&tree);
			err = range_tree_insert(&tree, &params[test][i].node,
						params[test][i].base,
						params[test][i].size);
			range_tree_unlock(&tree);
			TEST_ASSERT(err == OK,
				    "Range tree insertion should succeed");
			TEST_ASSERT(
				atomic_load_relaxed(&tree.nr_rotations) ==
					nr_rotations_start +
						params[test][i]
							.nr_rotations_expected,
				"Check number of rotations");
		}
	}

	TEST_END();
}

int
main(void)
{
	printf("=== Range Tree Unit Tests ===\n\n");

	test_rotation_counters_increment();

	printf("=== Asserts Summary ===\n");
	printf("Asserts passed: %zu\n", asserts_passed);
	printf("Asserts failed: %zu\n", asserts_failed);
	printf("Total:  %zu\n\n", asserts_passed + asserts_failed);

	printf("=== Tests Summary ===\n");
	printf("Tests passed: %zu\n", tests_passed);
	printf("Tests failed: %zu\n", tests_failed);
	printf("Total:  %zu\n\n", tests_passed + tests_failed);

	if (asserts_failed == 0) {
		printf("SUCCESS: All unit tests completed successfully!\n");
		return EXIT_SUCCESS;
	} else {
		printf("FAILURE: %zu asserts(s) and %zu test(s) failed.\n",
		       asserts_failed, tests_failed);
		return EXIT_FAILURE;
	}
}
