// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#undef register_t

#include <hypconstants.h>

#include <getopt.h>
#include <libgen.h>
#include <range_tree.h>
#include <unistd.h>
#include <util.h>

#include "helpers.h"
#include "stub.h"

// INTERFACE
typedef enum action {
	INIT	  = 0,
	INSERT	  = 1,
	REMOVE	  = 2,
	LOOKUP	  = 3,
	DEINIT	  = 4,
	CHECK	  = 5,
	NEXT_NODE = 6,
	SPLIT	  = 7,
} action_t;

typedef enum status {
	FAILED = 0,
	SUCCESS,
} status_t;

static range_tree_t	  tree;
static uint32_t		  node_count;
static range_tree_node_t *node_array;

static bool debug = false;
static bool dump  = false;
// easy debug
static size_t line_cnt = 0;

bool
run_init(const char *args);

bool
run_insert(const char *args);

bool
run_remove(const char *args);

bool
run_lookup(const char *args);

bool
run_next_node(const char *args);

bool
run_split(const char *args);

bool
run_deinit(const char *args);

static void
help(char *app_name)
{
	printf("Usage: %s -f input\n", basename(app_name));
	printf("Run test case from input file\n\n");

	return;
}

void
trigger_range_tree_release_node_event(range_tree_node_type_t type,
				      range_tree_node_t	    *node)
{
}

static const char *
action_name(action_t a)
{
	const char *name = "Unknown";
	switch (a) {
	case INIT:
		name = "init";
		break;

	case INSERT:
		name = "insert";
		break;

	case REMOVE:
		name = "remove";
		break;

	case LOOKUP:
		name = "lookup";
		break;

	case DEINIT:
		name = "deinit";
		break;

	case CHECK:
		name = "check";
		break;

	case NEXT_NODE:
		name = "next_node";
		break;

	case SPLIT:
		name = "split";
		break;

	default:
		break;
	}

	return name;
}

bool
run_init(const char *args)
{
	action_t action;

	error_t err = range_tree_init(&tree, NULL);
	if (err != OK) {
		return false;
	}

	sscanf(args, "%u %u", &action, &node_count);

	node_array = calloc(node_count, sizeof(node_array[0]));
	if (node_array == NULL) {
		range_tree_lock(&tree);
		range_tree_destroy(&tree, RANGE_TREE_NODE_TYPE_NONE);
		range_tree_unlock(&tree);
		return false;
	}

	return true;
}

bool
run_insert(const char *args)
{
	action_t action;
	size_t	 base, size;
	uint32_t node_index, expect_success;

	sscanf(args, "%u %zx %zx %u %u", &action, &base, &size, &node_index,
	       &expect_success);

	assert(node_index < node_count);
	range_tree_node_t *node = &node_array[node_index];

	range_tree_lock(&tree);
	error_t err = range_tree_insert(&tree, node, base, size);
	range_tree_unlock(&tree);
	if (err != OK) {
		if (expect_success) {
			printf("insert failed: base(%#lx) size(%zu) -> err(%d)\n",
			       base, size, err);
		}
		return !expect_success;
	}

	if (!expect_success) {
		printf("inserted with overlap: base(%#lx) size(%zu)\n", base,
		       size);
	}
	return expect_success;
}

bool
run_remove(const char *args)
{
	action_t action;
	uint32_t node_index;

	sscanf(args, "%u %u", &action, &node_index);

	assert(node_index < node_count);
	range_tree_node_t *node = &node_array[node_index];

	range_tree_lock(&tree);
	error_t err = range_tree_remove(&tree, node);
	range_tree_unlock(&tree);
	if (err != OK) {
		printf("remove failed: err(%d)\n", err);
		return false;
	}

	return true;
}

bool
run_lookup(const char *args)
{
	action_t action;
	size_t	 base, expect_size;
	int32_t	 expect_index;

	sscanf(args, "%u %zx %zx %i", &action, &base, &expect_size,
	       &expect_index);

	rcu_read_start();
	range_tree_lookup_result_t ret =
		range_tree_lookup(&tree, base, SIZE_MAX);
	rcu_read_finish();

	if ((ret.node == NULL) && (expect_index != -1)) {
		printf("lookup failed: base(%#lx), size %#zx -> NULL, size %#zx\n",
		       base, expect_size, ret.size);
		return false;
	}

	if (ret.node != NULL) {
		ptrdiff_t node_index = ret.node - node_array;
		assert((node_index >= 0) && (node_index < node_count));
		if (node_index != expect_index) {
			printf("lookup failed: base(%#lx), size %#zx -> node %td (not %d), size %#zx\n",
			       base, expect_size, node_index, expect_index,
			       ret.size);
			return false;
		}
	}

	if (ret.size != expect_size) {
		printf("lookup failed: base(%#lx), size %#zx -> node %d, size %#zx\n",
		       base, expect_size, expect_index, ret.size);
		return false;
	}

	return true;
}

bool
run_next_node(const char *args)
{
	action_t action;
	size_t	 start;
	int32_t	 expect_index;

	sscanf(args, "%u %zx %i", &action, &start, &expect_index);

	rcu_read_start();
	range_tree_node_t *ret = range_tree_next_node(&tree, start);
	rcu_read_finish();

	if ((ret == NULL) && (expect_index != -1)) {
		printf("next_node failed: start(%#lx) -> NULL\n", start);
		return false;
	}

	if (ret != NULL) {
		ptrdiff_t node_index = ret - node_array;
		assert((node_index >= 0) && (node_index < node_count));
		if (node_index != expect_index) {
			printf("lookup failed: start(%#lx) -> node %td (not %d)\n",
			       start, node_index, expect_index);
			return false;
		}
	}

	return true;
}

bool
run_split(const char *args)
{
	action_t action;
	uint32_t node_index, new_lower_index, new_upper_index, expect_success;
	size_t	 split_point;

	sscanf(args, "%u %u %u %u %zx %u", &action, &node_index,
	       &new_lower_index, &new_upper_index, &split_point,
	       &expect_success);

	assert(node_index < node_count);
	assert(new_lower_index < node_count);

	assert(new_upper_index < node_count);
	assert(node_index != new_lower_index);

	range_tree_node_t *node	     = &node_array[node_index];
	range_tree_node_t *new_lower = &node_array[new_lower_index];
	range_tree_node_t *new_upper = &node_array[new_upper_index];

	const size_t node_base = node->base;
	const size_t node_size = node->size;

	range_tree_lock(&tree);
	error_t err = range_tree_split_node(&tree, node, new_lower, new_upper,
					    split_point);

	range_tree_unlock(&tree);

	if (err != OK) {
		if (expect_success) {
			printf("split failed: node_idx(%u) split_point(%#zx) -> err(%d)\n",
			       node_index, split_point, err);
		}
		return !expect_success;
	}
	if (!expect_success) {
		printf("split succeeded unexpectedly: node_idx(%u) split_point(%#zx)\n",
		       node_index, split_point);
		return false;
	}

	if (new_lower->base != node_base) {
		printf("split validation failed: new_lower base is %#zx, expected %#zx\n",
		       new_lower->base, node_base);
		return false;
	}
	const size_t expected_new_lower_size = split_point - node_base;
	if (new_lower->size != expected_new_lower_size) {
		printf("split validation failed: new_lower size is %#zx, expected %#zx\n",
		       new_lower->size, expected_new_lower_size);
		return false;
	}
	if (new_upper->base != split_point) {
		printf("split validation failed: new_upper base is %#zx, expected %#zx\n",
		       new_upper->base, split_point);
		return false;
	}
	const size_t expected_new_upper_size =
		node_base + node_size - split_point;
	if (new_upper->size != expected_new_upper_size) {
		printf("split validation failed: new_upper size is %#zx, expected %#zx\n",
		       new_upper->size, expected_new_upper_size);
		return false;
	}

	return true;
}

bool
run_check(const char *args)
{
	action_t action;
	uint32_t inserted_count;

	sscanf(args, "%u %u", &action, &inserted_count);

	uint32_t	visited_count = 0U;
	uint32_result_t check_r = check_subtree(tree.root, &visited_count);
	if (check_r.e != OK) {
		return false;
	}

	if (visited_count != inserted_count) {
		printf("check failed: count %d, expected %d\n", visited_count,
		       inserted_count);
		return false;
	}

	return true;
}

bool
run_deinit(const char *args)
{
	// Not implemented yet
	(void)args;
	return true;
}

void
dump_subtree(range_tree_node_t *node, uint32_t indent)
{
	if (node == NULL) {
		return;
	}
	dump_subtree(node->left, indent + 1U);
	for (uint32_t i = 0U; i < indent; i++) {
		printf("  ");
	}
	printf("%#lx..%#lx (sz %#lx) bal %d id %td\n", node->base,
	       node->base + node->size - 1U, node->size, node->balance,
	       node - node_array);
	dump_subtree(node->right, indent + 1U);
}

// NOTE: could take input from outside or organise test by python.
int
main(int argc, char *argv[])
{
	const size_t buf_size = 256;
	char	     buf[buf_size];
	char	    *fn = NULL;
	FILE	    *fp = NULL;
	action_t     action;
	int	     opt;
	bool	     pgtable_initialised = false;
	bool	     ret;

	while ((opt = getopt(argc, argv, "f:hvd")) != -1) {
		switch (opt) {
		case 'f':
			fn = optarg;
			break;
		case 'v':
			debug = true;
			break;
		case 'd':
			dump = true;
			break;
		default:
			help(argv[0]);
			exit(-1);
		}
	}

	if (fn == NULL) {
		printf("Need input file for this test.\n");
		help(argv[0]);
		exit(-1);
	}

	fp = fopen(fn, "r");
	if (fp == NULL) {
		printf("Failed to open file\n");
		exit(-1);
	}

	setbuf(stdout, NULL);
	printf("testcase (%s)... ", fn);

	bool show_progress = isatty(STDOUT_FILENO);

	// load specific input from input file
	for (line_cnt = 1; NULL != fgets(buf, buf_size, fp); line_cnt++) {
		// Progress indication
		if (show_progress && ((line_cnt % 100) == 0)) {
			int chars = printf("%zd", line_cnt);
			for (int j = 0; j < chars; j++) {
				putc('\b', stdout);
			}
		}

		// skip the commented line, must be at the first char of the
		// line
		// FIXME: could make it more flexible to allow just be the first
		// char after indentation
		if (buf[0] == '#' || buf[0] == '\n') {
			continue;
		}

		sscanf(buf, "%u", &action);

		// perform tests
		if (debug) {
			printf(">> begin %s:%zu\n", action_name(action),
			       line_cnt);
		}

		switch (action) {
		case INIT:
			assert(!pgtable_initialised);
			pgtable_initialised = true;
			ret		    = run_init(buf);
			break;

		case INSERT:
			ret = run_insert(buf);
			break;

		case REMOVE:
			ret = run_remove(buf);
			break;

		case LOOKUP:
			ret = run_lookup(buf);
			break;

		case CHECK:
			ret = run_check(buf);
			break;

		case NEXT_NODE:
			ret = run_next_node(buf);
			break;

		case SPLIT:
			ret = run_split(buf);
			break;

		default:
			printf("Warning: unknown action %u on line %zu\n",
			       action, line_cnt);
		}

		if (dump) {
			printf("== dump ==\n");
			dump_subtree(tree.root, 0);
		}

		if (debug) {
			printf("<< end %s:%zu\n", action_name(action),
			       line_cnt);
			assert(check_subtree(tree.root, NULL).e == OK);
		}

		if (!ret) {
			printf("Line %zu operation failed.\n", line_cnt);
			break;
		}
	}

	if (ret) {
		puts("PASSED");
		return 0;
	} else {
		puts("FAILED");
		return -1;
	}
}
