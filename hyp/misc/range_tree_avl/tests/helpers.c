// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#include <stdint.h>
#include <stdio.h>

#include <util.h>

#include "helpers.h"

uint32_result_t
check_subtree(range_tree_node_t *node, uint32_t *visited)
{
	if (node == NULL) {
		return uint32_result_ok(0U);
	}

	if (visited != NULL) {
		(*visited)++;
	}

	uint32_result_t left_r = check_subtree(node->left, visited);
	if (left_r.e != OK) {
		return left_r;
	}

	uint32_result_t right_r = check_subtree(node->right, visited);
	if (right_r.e != OK) {
		return right_r;
	}

	int32_t expected_balance = (int32_t)left_r.r - (int32_t)right_r.r;
	if ((int32_t)node->balance != expected_balance) {
		printf("check failed: node @ %#lx balance %d, expected %d\n",
		       node->base, node->balance, expected_balance);
		return uint32_result_error(ERROR_FAILURE);
	}

	return uint32_result_ok(util_max(left_r.r, right_r.r) + 1U);
}
