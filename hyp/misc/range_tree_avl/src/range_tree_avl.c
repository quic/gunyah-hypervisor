// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// This is an implementation of range_tree using an AVL tree.
//
// We pick AVL trees over other balanced trees such as RB or WAVL because our
// use cases are read-mostly, rarely updated. AVL trees are more strictly
// balanced, and thus have faster lookups, but have extra storage cost and may
// require more rotations for a single update.
//
// Note that we don't allow ranges to overlap within the tree; lookups always
// return the single range containing the base address, if one exists. Therefore
// a naïve binary tree is sufficient; we don't need an augmented interval tree.

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <panic.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>

#include <events/range_tree.h>

error_t
range_tree_init(range_tree_t *tree, partition_t *partition)
{
	(void)partition;
	atomic_init(&tree->root, NULL);
	spinlock_init(&tree->write_lock);
	atomic_init(&tree->nr_rotations, 0);
	return OK;
}

void
range_tree_lock(range_tree_t *tree)
{
	spinlock_acquire(&tree->write_lock);
}

void
range_tree_lock_nopreempt(range_tree_t *tree)
{
	spinlock_acquire_nopreempt(&tree->write_lock);
}

void
range_tree_unlock(range_tree_t *tree)
{
	spinlock_release(&tree->write_lock);
}

void
range_tree_unlock_nopreempt(range_tree_t *tree)
{
	spinlock_release_nopreempt(&tree->write_lock);
}

static void
range_tree_destroy_node(range_tree_node_t     *node,
			range_tree_node_type_t node_type)
{
	assert(node != NULL);
	range_tree_node_t *left = atomic_load_consume(&node->left);
	if (left != NULL) {
		atomic_store_release(&node->left, NULL);
		range_tree_destroy_node(left, node_type);
	}
	range_tree_node_t *right = atomic_load_consume(&node->right);
	if (right != NULL) {
		atomic_store_release(&node->right, NULL);
		range_tree_destroy_node(right, node_type);
	}
	if (!trigger_range_tree_release_node_event(node_type, node)) {
		panic("Unimplemented or failed release");
	}
}

void
range_tree_destroy(range_tree_t *tree, range_tree_node_type_t node_type)
{
	rcu_read_start();
	range_tree_node_t *root = atomic_load_consume(&tree->root);
	if (root != NULL) {
		range_tree_destroy_node(root, node_type);
	}
	rcu_read_finish();
}

static void
range_tree_rotate_right(range_tree_t *tree, range_tree_node_t *_Atomic *subtree,
			range_tree_node_t *root, range_tree_node_t *left)
{
	// The order is important here: we want to make sure that lookups of
	// present nodes that fail due to a rotation will always observe an
	// increment of the rotation counter.
	//
	// 1. Find the centre subtree, which will move from the right child of
	//    the old left child to the left child of the new right child (which
	//    is currently the root).
	range_tree_node_t *centre = atomic_load_consume(&left->right);
	// 2. Set the left child of the root to itself, so that any reader that
	//    is searching for any left children of the root will become
	//    temporarily stuck in that loop.
	atomic_store_relaxed(&root->left, root);
	// 3. Set the right child of the left node to the root.
	atomic_store_relaxed(&left->right, root);
	// 4. Move the left node to the root of the subtree. Lookups that would
	//    have loaded the root prior to this operation will now load the
	//    left node. There must be release ordering between step 3 and this
	//    operation to prevent lookups of the root incorrectly failing.
	atomic_store_release(subtree, left);
	// 5. Increment the rotation counter. There must be release ordering
	//    between step 2 and this operation, which corresponds to the
	//    acquire ordering between the start counter read and a node read
	//    that could cause an incorrect NULL result in
	//    range_tree_lookup_once().
	(void)atomic_fetch_add_explicit(&tree->nr_rotations, 1,
					memory_order_release);
	// 6. Set the old root's left child to the centre subtree, which will
	//    allow lookups stuck in the left child loop to resume. If this
	//    resulted in a failed lookup, the rotation counter increment will
	//    be observed by range_tree_lookup_once() and the lookup will be
	//    retried. There must be release ordering between step 5 and this
	//    operation, which corresponds to the acquire ordering between
	//    loading the pointer that causes an incorrect NULL result and
	//    reading the rotation counter at the end of the lookup iteration in
	//    range_tree_lookup_once().
	atomic_store_release(&root->left, centre);
}

static void
range_tree_rotate_left(range_tree_t *tree, range_tree_node_t *_Atomic *subtree,
		       range_tree_node_t *root, range_tree_node_t *right)
{
	// Order is important. See comments in range_tree_rotate_right().
	range_tree_node_t *centre = atomic_load_consume(&right->left);
	atomic_store_relaxed(&root->right, root);
	atomic_store_relaxed(&right->left, root);
	atomic_store_release(subtree, right);
	(void)atomic_fetch_add_explicit(&tree->nr_rotations, 1,
					memory_order_release);
	atomic_store_release(&root->right, centre);
}

static bool
range_tree_rebalance_leftwards(range_tree_t		  *tree,
			       range_tree_node_t *_Atomic *subtree,
			       range_tree_node_t	  *root)
{
	bool height_decreased;

	range_tree_node_t *right = atomic_load_consume(&root->right);
	assert(right != NULL);

	if (right->balance <= 0) {
		range_tree_rotate_left(tree, subtree, root, right);

		root->balance = -1 - right->balance;
		right->balance++;
		height_decreased = right->balance == 0;
	} else {
		range_tree_node_t *centre = atomic_load_consume(&right->left);
		assert(centre != NULL);

		range_tree_rotate_right(tree, &root->right, right, centre);
		range_tree_rotate_left(tree, subtree, root, centre);

		root->balance	 = (centre->balance < 0) ? 1 : 0;
		right->balance	 = (centre->balance > 0) ? -1 : 0;
		centre->balance	 = 0;
		height_decreased = true;
	}

	return height_decreased;
}

static bool
range_tree_rebalance_rightwards(range_tree_t		   *tree,
				range_tree_node_t *_Atomic *subtree,
				range_tree_node_t	   *root)
{
	bool height_decreased;

	range_tree_node_t *left = atomic_load_consume(&root->left);
	assert(left != NULL);

	if (left->balance >= 0) {
		range_tree_rotate_right(tree, subtree, root, left);

		root->balance = 1 - left->balance;
		left->balance--;
		height_decreased = left->balance == 0;
	} else {
		range_tree_node_t *centre = atomic_load_consume(&left->right);
		assert(centre != NULL);

		range_tree_rotate_left(tree, &root->left, left, centre);
		range_tree_rotate_right(tree, subtree, root, centre);

		root->balance	 = (centre->balance > 0) ? -1 : 0;
		left->balance	 = (centre->balance < 0) ? 1 : 0;
		centre->balance	 = 0;
		height_decreased = true;
	}

	return height_decreased;
}

static bool_result_t
range_tree_insert_node(range_tree_t *tree, range_tree_node_t *_Atomic *subtree,
		       range_tree_node_t *node)
{
	bool_result_t height_increased;

	range_tree_node_t *subtree_root = atomic_load_consume(subtree);

	if (subtree_root == NULL) {
		// The subtree is empty; insert the node.
		atomic_store_release(subtree, node);
		// The subtree's depth has increased by 1.
		height_increased = bool_result_ok(true);
		// The node has no children, so it is balanced.
		node->balance = 0;
	} else if ((node->base + node->size - 1U) < subtree_root->base) {
		// Node is entirely to the left of the subtree root
		bool_result_t left_height_increased =
			range_tree_insert_node(tree, &subtree_root->left, node);
		if (left_height_increased.e != OK) {
			// Insertion failed; propagate the error.
			height_increased = left_height_increased;
		} else if (left_height_increased.r &&
			   (subtree_root->balance <= 0)) {
			// This subtree was right-heavy and is now balanced, or
			// was balanced and is now left-heavy. Its total height
			// increased if it is no longer balanced.
			subtree_root->balance++;
			height_increased =
				bool_result_ok(subtree_root->balance != 0);
		} else if (left_height_increased.r) {
			// This subtree was left-heavy and now has balance 2;
			// it needs to be rebalanced to the right. The resulting
			// subtree will always be balanced, and its height will
			// always decrease, balancing out the earlier increase.
			bool height_reduced = range_tree_rebalance_rightwards(
				tree, subtree, subtree_root);
			assert(height_reduced);
			height_increased = bool_result_ok(false);
		} else {
			// Child's height was not increased; balance unchanged
			height_increased = bool_result_ok(false);
		}
	} else if (node->base >
		   (subtree_root->base + subtree_root->size - 1U)) {
		// Node is entirely to the right of the subtree root
		bool_result_t right_height_increased = range_tree_insert_node(
			tree, &subtree_root->right, node);
		if (right_height_increased.e != OK) {
			// Insertion failed; propagate the error.
			height_increased = right_height_increased;
		} else if (right_height_increased.r &&
			   (subtree_root->balance >= 0)) {
			// This subtree was left-heavy and is now balanced, or
			// was balanced and is now right-heavy. Its total height
			// increased if it is no longer balanced.
			subtree_root->balance--;
			height_increased =
				bool_result_ok(subtree_root->balance != 0);
		} else if (right_height_increased.r) {
			// This subtree was right-heavy and now has balance -2;
			// it needs to be rebalanced to the left. The resulting
			// subtree will always be balanced, and its height will
			// always decrease, balancing out the earlier increase.
			bool height_reduced = range_tree_rebalance_leftwards(
				tree, subtree, subtree_root);
			assert(height_reduced);
			height_increased = bool_result_ok(false);
		} else {
			// Child's height was not increased; balance unchanged
			height_increased = bool_result_ok(false);
		}
	} else {
		// Node overlaps with the subtree root; insertion fails.
		height_increased = bool_result_error(ERROR_BUSY);
	}

	return height_increased;
}

error_t
range_tree_insert(range_tree_t *tree, range_tree_node_t *node, size_t base,
		  size_t size)
{
	error_t ret;

	if (util_add_overflows(base, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	*node = (range_tree_node_t){
		.base	 = base,
		.size	 = size,
		.left	 = NULL,
		.right	 = NULL,
		.balance = 0,
	};

	ret = range_tree_insert_node(tree, &tree->root, node).e;
out:
	return ret;
}

static bool
range_tree_replace_with_successor(range_tree_t		     *tree,
				  range_tree_node_t *_Atomic *subtree,
				  range_tree_node_t	     *left,
				  range_tree_node_t	     *right,
				  range_tree_node_t *_Atomic *parent_left,
				  range_tree_node_t	     *node)
{
	bool height_reduced;

	range_tree_node_t *succ_left = atomic_load_consume(&node->left);

	if (succ_left != NULL) {
		// We haven't found the successor yet; keep moving left.
		bool left_height_reduced = range_tree_replace_with_successor(
			tree, subtree, left, right, &node->left, succ_left);

		// If the parent is the deleted node, we need to reload it
		// from the subtree root and rebalance its right child;
		// otherwise we rebalance its left child.
		range_tree_node_t *_Atomic *new_subtree =
			(parent_left == NULL)
				? &atomic_load_consume(subtree)->right
				: parent_left;

		// When the successor was found, the left subtree may have
		// reduced in height. If so, we may need to rebalance.
		if (left_height_reduced && (node->balance >= 0)) {
			// This node was left-heavy and is now balanced, or was
			// balanced and is now right-heavy. Its total height
			// decreased if it is now balanced.
			node->balance--;
			height_reduced = node->balance == 0;
		} else if (left_height_reduced) {
			// This node was right-heavy and now has balance 2; it
			// needs to be rebalanced.
			height_reduced = range_tree_rebalance_leftwards(
				tree, new_subtree, node);
		} else {
			// The height has not changed.
			height_reduced = false;
		}
	} else {
		// Attach the removed node's left child to the successor's left.
		atomic_store_relaxed(&node->left, left);

		if (parent_left != NULL) {
			// The successor is not the removed node's right child,
			// so it is on the left of the deleted node's right
			// subtree. We need to rotate the successor into place,
			// attaching the deleted node's right child to its right
			// and then replacing it with its previous right child,
			// while being careful to keep all nodes reachable to a
			// concurrent lookup (though it may traverse the deleted
			// node's right branch more than once).
			range_tree_node_t *succ_right =
				atomic_load_consume(&node->right);
			atomic_store_relaxed(&node->right, right);
			atomic_store_release(subtree, node);
			atomic_store_release(parent_left, succ_right);
		} else {
			assert(node == right);
			// Replace the removed node with its successor.
			atomic_store_release(subtree, right);
		}

		// The height of parent_left decreased, because it was replaced
		// with its own right subtree.
		height_reduced = true;
	}

	return height_reduced;
}

static bool
range_tree_remove_subtree_root(range_tree_t		  *tree,
			       range_tree_node_t *_Atomic *subtree,
			       range_tree_node_t	  *root)
{
	bool height_reduced;

	range_tree_node_t *left	 = atomic_load_consume(&root->left);
	range_tree_node_t *right = atomic_load_consume(&root->right);

	if (left == NULL) {
		// Replace with the right child (which may be NULL)
		atomic_store_relaxed(subtree, right);
		height_reduced = true;
	} else if (right == NULL) {
		// Replace with the left child
		atomic_store_relaxed(subtree, left);
		height_reduced = true;
	} else {
		// Find the successor and replace the deleted root with it.
		// It is the leftmost node of the right subtree, so start the
		// search with the right child.
		bool right_height_reduced = range_tree_replace_with_successor(
			tree, subtree, left, right, NULL, right);

		// When the successor was found, the right subtree may have
		// reduced in height. If so, we may need to rebalance. In any
		// case, the node was replaced, so we need to copy the balance
		// from the old root first.
		range_tree_node_t *node = atomic_load_consume(subtree);
		node->balance		= root->balance;
		if (right_height_reduced && (node->balance <= 0)) {
			// The subtree was right-heavy and is now balanced, or
			// was balanced and is now left-heavy. Its total height
			// decreased if it is now balanced.
			node->balance++;
			height_reduced = node->balance == 0;
		} else if (right_height_reduced) {
			// This node was right-heavy and now has balance -2; it
			// needs to be rebalanced.
			height_reduced = range_tree_rebalance_rightwards(
				tree, subtree, node);
		} else {
			// The height has not changed.
			height_reduced = false;
		}
	}

	return height_reduced;
}

static bool_result_t
range_tree_remove_node(range_tree_t *tree, range_tree_node_t *_Atomic *subtree,
		       range_tree_node_t *removed_node)
{
	bool_result_t height_reduced;

	range_tree_node_t *subtree_root = atomic_load_consume(subtree);

	if (subtree_root == NULL) {
		// Node is not present in the tree. Deletion fails.
		height_reduced = bool_result_error(ERROR_ADDR_INVALID);
	} else if ((removed_node->base + removed_node->size - 1U) <
		   subtree_root->base) {
		// Node is entirely to the left of the subtree root
		bool_result_t left_height_reduced = range_tree_remove_node(
			tree, &subtree_root->left, removed_node);
		if (left_height_reduced.e != OK) {
			height_reduced = left_height_reduced;
		} else if (left_height_reduced.r &&
			   (subtree_root->balance >= 0)) {
			// This subtree was left-heavy and is now balanced, or
			// was balanced and is now right-heavy. Its total height
			// decreased if it is now balanced.
			subtree_root->balance--;
			height_reduced =
				bool_result_ok(subtree_root->balance == 0);
		} else if (left_height_reduced.r) {
			// This subtree was right-heavy and now has balance -2;
			// it needs to be rebalanced. The resulting tree will
			// reduce in height.
			height_reduced = bool_result_ok(
				range_tree_rebalance_leftwards(tree, subtree,
							       subtree_root));
		} else {
			// Child's height was not reduced; balance unchanged
			height_reduced = bool_result_ok(false);
		}
	} else if (removed_node->base >
		   (subtree_root->base + subtree_root->size - 1U)) {
		// Node is entirely to the right of the subtree root
		bool_result_t right_height_reduced = range_tree_remove_node(
			tree, &subtree_root->right, removed_node);
		if (right_height_reduced.e != OK) {
			height_reduced = right_height_reduced;
		} else if (right_height_reduced.r &&
			   (subtree_root->balance <= 0)) {
			// This subtree was right-heavy and is now balanced, or
			// was balanced and is now left-heavy. Its total height
			// decreased if it is now balanced.
			subtree_root->balance++;
			height_reduced =
				bool_result_ok(subtree_root->balance == 0);
		} else if (right_height_reduced.r) {
			// This subtree was left-heavy and now has balance 2; it
			// needs to be rebalanced. The resulting tree will
			// reduce in height.
			height_reduced = bool_result_ok(
				range_tree_rebalance_rightwards(tree, subtree,
								subtree_root));
		} else {
			// Child's height was not reduced; balance unchanged
			height_reduced = bool_result_ok(false);
		}
	} else if (subtree_root != removed_node) {
		// Node is not present in the tree. Deletion fails.
		height_reduced = bool_result_error(ERROR_BUSY);
	} else {
		// Found the node. Remove it and re-attach its children.
		height_reduced = bool_result_ok(range_tree_remove_subtree_root(
			tree, subtree, removed_node));
	}

	return height_reduced;
}

error_t
range_tree_remove(range_tree_t *tree, range_tree_node_t *node)
{
	return range_tree_remove_node(tree, &tree->root, node).e;
}

static range_tree_lookup_result_result_t
range_tree_lookup_once(range_tree_t *tree, size_t base, size_t max_size)
{
	range_tree_lookup_result_result_t ret =
		(range_tree_lookup_result_result_t){ .e = OK };
	// Clamp the max size to the whole address space above the base.
	// Note that we can't return a size that actually covers the entire
	// address space, so we must decrease the limit by 1 if the base is 0.
	size_t size_limit = util_min(
		max_size, (base == 0U) ? SIZE_MAX : (SIZE_MAX - base + 1U));

	// Order of loads is important. See comments in
	// range_tree_rotate_right().
	const size_t nr_rotations_start =
		atomic_load_acquire(&tree->nr_rotations);
	range_tree_node_t *node = atomic_load_consume(&tree->root);

	// This is called without holding the write lock, so we must use
	// a loop to avoid unbounded recursion.
	while (node != NULL) {
		if (base < node->base) {
			// Base is to the left of this node.
			//
			// Since overlap is not allowed in the tree, we
			// can clamp the max size to the distance
			// between the lookup base and this node's base.
			size_limit = util_min(size_limit, node->base - base);

			node = atomic_load_acquire(&node->left);
		} else if (base <= (node->base + node->size - 1U)) {
			// Base is within this node.
			ret.r = (range_tree_lookup_result_t){
				.node = node,
				.size = util_min(size_limit,
						 node->size -
							 (base - node->base)),
			};
			goto out;
		} else {
			// Base is to the right of this node.
			node = atomic_load_acquire(&node->right);
		}
	}

	if (nr_rotations_start != atomic_load_relaxed(&tree->nr_rotations)) {
		ret.e = ERROR_BUSY;
	}
	ret.r = (range_tree_lookup_result_t){
		.node = NULL,
		.size = size_limit,
	};
out:
	return ret;
}

range_tree_lookup_result_t
range_tree_lookup(range_tree_t *tree, size_t base, size_t max_size)
{
	range_tree_lookup_result_result_t res =
		range_tree_lookup_once(tree, base, max_size);
	if (res.e == OK) {
		goto out;
	}
	range_tree_lock(tree);
	res.r = range_tree_lookup_locked(tree, base, max_size);
	range_tree_unlock(tree);
out:
	return res.r;
}

range_tree_lookup_result_t
range_tree_lookup_locked(range_tree_t *tree, size_t base, size_t max_size)
{
	const range_tree_lookup_result_result_t res =
		range_tree_lookup_once(tree, base, max_size);
	assert(res.e == OK);
	return res.r;
}

static bool_result_t
range_tree_propagate_balance_change(range_tree_t	       *tree,
				    range_tree_node_t *_Atomic *subtree,
				    range_tree_node_t	       *node,
				    bool   node_height_increased,
				    int8_t balance_delta)
{
	assert((balance_delta >= -1) && (balance_delta <= 1));
	bool_result_t height_increased;

	range_tree_node_t *subtree_root = atomic_load_consume(subtree);

	if (subtree_root == NULL) {
		// Node is not present in the tree.
		height_increased = bool_result_error(ERROR_ADDR_INVALID);
	} else if ((node->base + node->size - 1U) < subtree_root->base) {
		// Node is entirely to the left of the subtree root
		bool_result_t left_height_increased =
			range_tree_propagate_balance_change(
				tree, &subtree_root->left, node,
				node_height_increased, balance_delta);
		if (left_height_increased.e != OK) {
			// Balance change failed; propagate the error.
			height_increased = left_height_increased;
		} else if (left_height_increased.r &&
			   (subtree_root->balance <= 0)) {
			// This subtree was right-heavy and is now balanced, or
			// was balanced and is now left-heavy. Its total height
			// increased if it is no longer balanced.
			subtree_root->balance++;
			height_increased =
				bool_result_ok(subtree_root->balance != 0);
		} else if (left_height_increased.r) {
			// This subtree was left-heavy and now has balance 2;
			// it needs to be rebalanced to the right.
			const bool height_reduced =
				range_tree_rebalance_rightwards(tree, subtree,
								subtree_root);
			// If the rebalance did not reduce the height, then
			// propagate height increase to caller.
			height_increased = bool_result_ok(!height_reduced);
		} else {
			// Child's height was not increased; balance unchanged
			height_increased = bool_result_ok(false);
		}
	} else if (node->base >
		   (subtree_root->base + subtree_root->size - 1U)) {
		// Node is entirely to the right of the subtree root
		bool_result_t right_height_increased =
			range_tree_propagate_balance_change(
				tree, &subtree_root->right, node,
				node_height_increased, balance_delta);
		if (right_height_increased.e != OK) {
			// Balance change failed; propagate the error.
			height_increased = right_height_increased;
		} else if (right_height_increased.r &&
			   (subtree_root->balance >= 0)) {
			// This subtree was left-heavy and is now balanced, or
			// was balanced and is now right-heavy. Its total height
			// increased if it is no longer balanced.
			subtree_root->balance--;
			height_increased =
				bool_result_ok(subtree_root->balance != 0);
		} else if (right_height_increased.r) {
			// This subtree was right-heavy and now has balance -2;
			// it needs to be rebalanced to the left.
			const bool height_reduced =
				range_tree_rebalance_leftwards(tree, subtree,
							       subtree_root);
			// If the rebalance did not reduce the height, then
			// propagate height increase to caller.
			height_increased = bool_result_ok(!height_reduced);
		} else {
			// Child's height was not increased; balance unchanged
			height_increased = bool_result_ok(false);
		}
	} else if (subtree_root != node) {
		// Node is not present in the tree.
		height_increased = bool_result_error(ERROR_BUSY);
	} else {
		// Found the node. Update its balance and set whether it's
		// height increased.
		const int8_t new_balance =
			subtree_root->balance + balance_delta;
		if (new_balance > 1) {
			// If the new balance is too left heavy (2), we have to
			// rebalance rightwards and it must decrease the height,
			// which means the node's balance will be unchanged.
			const bool height_reduced =
				range_tree_rebalance_rightwards(tree, subtree,
								subtree_root);
			assert(height_reduced);
			height_increased = bool_result_ok(false);
		} else if (new_balance < -1) {
			// If the new balance is too right heavy (-2), we have
			// to rebalance leftwards and it must decrease the
			// height, which means the node's balance will be
			// unchanged.
			const bool height_reduced =
				range_tree_rebalance_leftwards(tree, subtree,
							       subtree_root);
			assert(height_reduced);
			height_increased = bool_result_ok(false);
		} else {
			subtree_root->balance = new_balance;
			height_increased =
				bool_result_ok(node_height_increased);
		}
	}
	return height_increased;
}

error_t
range_tree_split_node(range_tree_t *tree, range_tree_node_t *node,
		      range_tree_node_t *new_lower,
		      range_tree_node_t *new_upper, size_t split_point)
{
	error_t ret;

	if ((split_point <= node->base) ||
	    (split_point >= (node->base + node->size))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	const size_t lower_size = split_point - node->base;
	const size_t upper_size = node->size - lower_size;

	*new_lower = (range_tree_node_t){
		.base	 = node->base,
		.size	 = lower_size,
		.left	 = NULL,
		.right	 = NULL,
		.balance = 0,

	};
	*new_upper = (range_tree_node_t){
		.base	 = split_point,
		.size	 = upper_size,
		.left	 = NULL,
		.right	 = NULL,
		.balance = 0,

	};
	const bool_result_t left_height_increased =
		range_tree_insert_node(tree, &node->left, new_lower);
	assert(left_height_increased.e == OK);
	const bool_result_t right_height_increased =
		range_tree_insert_node(tree, &node->right, new_upper);
	assert(right_height_increased.e == OK);
	const bool left_increased  = left_height_increased.r;
	const bool right_increased = right_height_increased.r;
	if (left_increased || right_increased) {
		bool   node_height_increased;
		int8_t balance_delta;
		if (left_increased && !right_increased) {
			// If only left height increased, then the node's height
			// will only increase if it was balanced (0) or left
			// heavy (1).
			node_height_increased = (node->balance >= 0);
			balance_delta	      = 1;
		} else if (right_increased && !left_increased) {
			// If only right height increased, then the node's
			// height will only increase if it was balanced (0) or
			// right heavy (-1).
			node_height_increased = (node->balance <= 0);
			balance_delta	      = -1;
		} else {
			// If both the left and right heights increased, then
			// the node's height increased.
			node_height_increased = true;
			balance_delta	      = 0;
		}
		const bool_result_t res = range_tree_propagate_balance_change(
			tree, &tree->root, node, node_height_increased,
			balance_delta);
		assert(res.e == OK);
	}
	const bool_result_t res =
		range_tree_remove_node(tree, &tree->root, node);
	assert(res.e == OK);
	ret = OK;
out:
	return ret;
}

static range_tree_node_t *
range_tree_next_subtree_node(range_tree_node_t *root, size_t start)
{
	range_tree_node_t *ret;

	if (root == NULL) {
		ret = NULL;
		goto out;
	}

	if (start > root->base) {
		// This node and everything to the left is excluded.
		// Traverse to the right.
		ret = range_tree_next_subtree_node(
			atomic_load_consume(&root->right), start);
		goto out;
	}

	// If anything to the left is in range, return that. Note that this is
	// not tail-recursive; we must hold the write lock in this function
	// to keep the recursion bounded.
	range_tree_node_t *left_next = range_tree_next_subtree_node(
		atomic_load_consume(&root->left), start);
	if (left_next != NULL) {
		ret = left_next;
		goto out;
	}

	// The root is in range and the left subtree is not, so return it.
	ret = root;

out:
	return ret;
}

range_tree_node_t *
range_tree_next_node(range_tree_t *tree, size_t start)
{
	range_tree_node_t *ret;
	range_tree_lock(tree);
	ret = range_tree_next_subtree_node(atomic_load_consume(&tree->root),
					   start);
	range_tree_unlock(tree);
	return ret;
}
