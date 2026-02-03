// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#undef register_t

#include <string.h>

#include <atomic.h>
#include <range_tree.h>
#include <util.h>

#include "plookups.h"
#include "stub.h"

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

#define TEST_FAIL(message) TEST_ASSERT(false, message)

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

#define NR_MAX_NODES	0xFFFFF
#define NR_BASE_OFFSETS 3

typedef struct plookups_fail_counts {
	size_t nr_base_failures;
	size_t nr_size_failures;
	size_t nr_node_size_failures;
	size_t nr_null_failures;
} plookups_fail_counts_t;

typedef struct plookups_args {
	range_tree_lookup_result_t (*lookup_fn)(range_tree_t *, size_t, size_t);
	range_tree_t	      *tree;
	size_t		       base;
	size_t		       size;
	plookups_fail_counts_t counters;
} plookups_args_t;

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

static bool
has_plookups_passed(plookups_args_t *args)
{
	// Checks failure counter array contains only zeros.
	static const plookups_fail_counts_t zero_counters = { 0 };
	return memcmp(&args->counters, &zero_counters,
		      sizeof(args->counters)) == 0;
}

static void
print_plookups_results(plookups_args_t *args)
{
	size_t total_failures = 0;
	printf("parallel lookup failures:\n");
	printf("\tBase mismatches:      %zu\n",
	       args->counters.nr_base_failures);
	printf("\tSize mismatches:      %zu\n",
	       args->counters.nr_size_failures);
	printf("\tNode size mismatches: %zu\n",
	       args->counters.nr_node_size_failures);
	printf("\tNULL results:         %zu\n",
	       args->counters.nr_null_failures);
	total_failures += args->counters.nr_base_failures +
			  args->counters.nr_size_failures +
			  args->counters.nr_node_size_failures +
			  args->counters.nr_null_failures;
	printf("total failures: %zu\n", total_failures);
}

static void *
plookup_thread_fn(void *arg)
{
	plookups_state_t *state	    = arg;
	plookups_args_t	 *args	    = state->args;
	size_t		  iteration = 0;
	while (plookups_thread_exists(state)) {
		lock(&state->mutex);
		while (state->thread_asleep) {
			wait(&state->cond, &state->mutex);
			iteration = 0;
		}
		// Immediately check thread is supposed to exist because it
		// could be the destroy function waking us up.
		if (!plookups_thread_exists(state)) {
			unlock(&state->mutex);
			break;
		}
		++iteration;
		range_tree_lookup_result_t ret =
			args->lookup_fn(args->tree, args->base, args->size);
		if (ret.node) {
			if (ret.node->base != args->base) {
				printf("\nparallel lookup failed: base(%#zx), size(%#zx), iteration(%zu) -> bad base(%#zx)\n",
				       args->base, args->size, iteration,
				       ret.node->base);

				++args->counters.nr_base_failures;
			}
			if (ret.size != args->size) {
				printf("\nparallel lookup failed: base(%#zx), size(%#zx), iteration(%zu) -> bad size(%#zx)\n",
				       args->base, args->size, iteration,
				       ret.size);
				++args->counters.nr_size_failures;
			}
			if (ret.node->size != args->size) {
				printf("\nparallel lookup failed: base(%#zx), size(%#zx), iteration(%zu) -> bad node size(%#zx)\n",
				       args->base, args->size, iteration,
				       ret.node->size);
				++args->counters.nr_node_size_failures;
			}
		} else {
			printf("\nparallel lookup failed: base(%#zx), size(%#zx) iteration(%zu) -> NULL\n",
			       args->base, args->size, iteration);
			++args->counters.nr_null_failures;
		}
		unlock(&state->mutex);
	}
	return NULL;
}

static range_tree_lookup_result_t
range_tree_lookup_with_write_lock(range_tree_t *tree, size_t base,
				  size_t max_size)
{
	rcu_read_start();
	range_tree_lock(tree);
	const range_tree_lookup_result_t res =
		range_tree_lookup_locked(tree, base, max_size);
	range_tree_unlock(tree);
	rcu_read_finish();
	return res;
}

static const struct lookup_fn {
	range_tree_lookup_result_t (*fn)(range_tree_t *, size_t, size_t);
	const char *desc;
} lookup_fns[] = { { .fn = range_tree_lookup, .desc = "range_tree_lookup" },
		   { .fn   = range_tree_lookup_with_write_lock,
		     .desc = "range_tree_lookup_locked" } };

static void
test_parallel_lookup_during_left_or_right_rotation(void)
{
	TEST_BEGIN("parallel_lookup_during_left_or_right_rotation");

	const size_t		 size		       = 0x1;
	const size_t		 min_addr	       = 0x0;
	const size_t		 max_addr	       = NR_MAX_NODES - 1;
	const size_t		 nr_rotations_expected = 1048555;
	static range_tree_node_t nodes[NR_MAX_NODES];

	static plookups_state_t state;
	static plookups_args_t	args = (plookups_args_t){ .size = size };
	plookups_create_thread(&state, &args, plookup_thread_fn);

	const struct param {
		const char *desc;
		size_t	    start_base;
		size_t	    end_base;
		int	    increment;
		size_t	    nr_rotations_expected;
		bool	    test_rotations_overflow;
	} params[] = {
		// Inserts: 0, 1, 2, ...
		// Parallel lookups: 0 during 1's insert, 1 during 2's insert,
		// ...
		{ .desc			   = "left rotation case",
		  .start_base		   = min_addr,
		  .end_base		   = max_addr,
		  .increment		   = size,
		  .nr_rotations_expected   = nr_rotations_expected,
		  .test_rotations_overflow = false },
		// Inserts: NR_MAX_NODES - 1, NR_MAX_NODES - 2, ...
		// Parallel lookups: NR_MAX_NODES - 1 during NR_MAX_NODES - 2's
		// insert, ...
		{ .desc			   = "right rotation case",
		  .start_base		   = max_addr,
		  .end_base		   = min_addr,
		  .increment		   = -1 * (int)size,
		  .nr_rotations_expected   = nr_rotations_expected,
		  .test_rotations_overflow = false },
		{ .desc = "rotation counter overflow with left rotation case",
		  .start_base		   = min_addr,
		  .end_base		   = max_addr,
		  .increment		   = size,
		  .nr_rotations_expected   = SIZE_MAX,
		  .test_rotations_overflow = true }
	};

	for (size_t f = 0; f < util_array_size(lookup_fns); ++f) {
		args.lookup_fn = lookup_fns[f].fn;
		for (size_t p = 0; p < util_array_size(params); ++p) {
			printf("PARAMS: lookup_fn=%s, insert_params=%s\n",
			       lookup_fns[f].desc, params[p].desc);
			static range_tree_t tree;
			error_t		    err = range_tree_init(&tree, NULL);
			args.tree		= &tree;
			args.counters		= (plookups_fail_counts_t){ 0 };
			TEST_ASSERT(err == OK,
				    "Range tree initialization should succeed");
			TEST_ASSERT(atomic_load_relaxed(&tree.nr_rotations) ==
					    0,
				    "Rotations should be 0");
			size_t i = 0;
			for (size_t base = params[p].start_base;
			     base != (params[p].end_base + params[p].increment);
			     base += params[p].increment) {
				range_tree_lock(&tree);
				err = range_tree_insert(&tree, &nodes[i], base,
							size);
				range_tree_unlock(&tree);
				if (err != OK) {
					TEST_FAIL(
						"Range tree insertion should succeed");
				}
				if (params[p].test_rotations_overflow) {
					if ((i % 3 == 2) &&
					    (atomic_load_relaxed(
						     &tree.nr_rotations) !=
					     0)) {
						TEST_FAIL(
							"Every third insert should always overflow rotation counter to 0");
					}
				}
				plookups_sleep_thread(&state);
				args.base = base;
				if (params[p].test_rotations_overflow) {
					atomic_store_relaxed(&tree.nr_rotations,
							     SIZE_MAX);
				}
				plookups_wake_thread(&state);
				++i;
			}
			plookups_sleep_thread(&state);
			TEST_ASSERT(atomic_load_relaxed(&tree.nr_rotations) ==
					    params[p].nr_rotations_expected,
				    "Check number of rotations");

			if (!has_plookups_passed(&args)) {
				print_plookups_results(&args);
				TEST_FAIL(
					"Parallel lookups during left or right rotations should succeed");
			}
		}
	}

	plookups_destroy_thread(&state);

	TEST_END();
}

static void
test_parallel_lookup_during_left_and_right_rotation(void)
{
	TEST_BEGIN("parallel_lookup_during_left_and_right_rotation");

	const size_t size	 = 0x1;
	const size_t low_offset	 = 0x0;
	const size_t med_offset	 = 0x1;
	const size_t high_offset = 0x2;

	static range_tree_node_t nodes[NR_MAX_NODES];

	static plookups_state_t state;
	static plookups_args_t	args = (plookups_args_t){ .size = size };
	plookups_create_thread(&state, &args, plookup_thread_fn);

	const struct param {
		const char *desc;
		size_t	    base_offsets[NR_BASE_OFFSETS];
		size_t	    nr_rotations_expected;
	} params[] = {
		// Inserts: 0, 2, 1, then, 3, 5, 4, then, ...
		// Parallel lookups: 2 during 1's insert, 5 during 4's insert,
		// ...
		{ .desc		= "right-left rotation case",
		  .base_offsets = { low_offset, high_offset, med_offset },
		  .nr_rotations_expected = 1048556 },
		// Inserts: 2, 0, 1, then, 5, 3, 4, then, ...
		// Parallel lookups: 0 during 1's insert, 3 during 4's insert,
		// ...
		{ .desc		= "left-right rotation case",
		  .base_offsets = { high_offset, low_offset, med_offset },
		  .nr_rotations_expected = 1223318 }
	};

	for (size_t f = 0; f < util_array_size(lookup_fns); ++f) {
		args.lookup_fn = lookup_fns[f].fn;
		for (size_t p = 0; p < util_array_size(params); ++p) {
			printf("PARAMS: lookup_fn=%s, insert_params=%s\n",
			       lookup_fns[f].desc, params[p].desc);
			static range_tree_t tree;
			error_t		    err = range_tree_init(&tree, NULL);
			args.tree		= &tree;
			args.counters		= (plookups_fail_counts_t){ 0 };
			TEST_ASSERT(err == OK,
				    "Range tree initialization should succeed");
			TEST_ASSERT(atomic_load_relaxed(&tree.nr_rotations) ==
					    0,
				    "Rotations should be 0");
			for (size_t i = 0; i < NR_MAX_NODES;
			     i += NR_BASE_OFFSETS) {
				for (size_t j = 0; j < NR_BASE_OFFSETS; ++j) {
					const size_t base =
						i + params[p].base_offsets[j];
					range_tree_lock(&tree);
					err = range_tree_insert(&tree,
								&nodes[i + j],
								base, size);
					range_tree_unlock(&tree);
					if (err != OK) {
						TEST_FAIL(
							"Range tree insertion should succeed");
					}
					if (j == NR_BASE_OFFSETS - 2) {
						// Repeatedly look up the
						// second-last node in the group
						// while the third and final
						// node is inserted in the next
						// loop iteration, which
						// triggers left/right
						// rotations.
						args.base = base;
						plookups_wake_thread(&state);
					}
				}
				plookups_sleep_thread(&state);
			}
			TEST_ASSERT(atomic_load_relaxed(&tree.nr_rotations) ==
					    params[p].nr_rotations_expected,
				    "Check number of rotations");

			if (!has_plookups_passed(&args)) {
				print_plookups_results(&args);
				TEST_FAIL(
					"Parallel lookups during left and right rotations should succeed");
			}
		}
	}

	plookups_destroy_thread(&state);

	TEST_END();
}

static void
test_split_at_invalid_point(void)
{
	TEST_BEGIN("split_at_invalid_point");

	range_tree_t tree;
	error_t	     err = range_tree_init(&tree, NULL);
	TEST_ASSERT(err == OK, "Range tree init should succeed");

	range_tree_node_t node	    = { 0 };
	range_tree_node_t new_lower = { 0 };
	range_tree_node_t new_upper = { 0 };

	const size_t base = 0x1000;
	const size_t size = 0x10;

	range_tree_lock(&tree);
	err = range_tree_insert(&tree, &node, base, size);
	range_tree_unlock(&tree);
	TEST_ASSERT(err == OK, "Node insert should succeed");

	const struct param {
		size_t bad_split_point;
		char  *msg;
	} params[] = {
		{ .bad_split_point = base,
		  .msg		   = "Split at start should fail" },
		{ .bad_split_point = base + size,
		  .msg		   = "Split at end should fail" },
		{ .bad_split_point = base - 1,
		  .msg		   = "Split before start should fail" },
		{ .bad_split_point = base + size + 1,
		  .msg		   = "Split after end should fail" },
	};

	for (size_t i = 0; i < util_array_size(params); ++i) {
		range_tree_lock(&tree);
		err = range_tree_split_node(&tree, &node, &new_lower,
					    &new_upper,
					    params[i].bad_split_point);
		range_tree_unlock(&tree);
		TEST_ASSERT(err == ERROR_ARGUMENT_INVALID, params[i].msg);
		TEST_ASSERT(node.base == base,
			    "Node base unchanged after failed split");
		TEST_ASSERT(node.size == size,
			    "Node size unchanged after failed split");
		TEST_ASSERT(new_lower.base == 0,
			    "New lower node base unchanged after failed split");
		TEST_ASSERT(new_lower.size == 0,
			    "New lower node size unchanged after failed split");
		TEST_ASSERT(new_upper.base == 0,
			    "New upper node base unchanged after failed split");
		TEST_ASSERT(new_upper.size == 0,
			    "New upper node size unchanged after failed split");
	}

	TEST_END();
}

int
main(void)
{
	printf("=== Range Tree Unit Tests ===\n\n");

	test_rotation_counters_increment();
	test_parallel_lookup_during_left_or_right_rotation();
	test_parallel_lookup_during_left_and_right_rotation();
	test_split_at_invalid_point();

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
