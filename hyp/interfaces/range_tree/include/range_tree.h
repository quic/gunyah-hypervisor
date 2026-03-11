// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// The range tree is RCU-protected for read operations, and locked with a
// spinlock for write operations.

// Initialise the range tree.
//
// The provided partition will be used for memory allocation, if required by the
// tree implementation.
error_t
range_tree_init(range_tree_t *tree, partition_t *partition);

// Lock / unlock the range tree for an atomic sequence of writes.
//
// In the existing implementation these are trivial spinlock wrappers. They are
// provided in case a future implementation has different locking requirements.
void
range_tree_lock(range_tree_t *tree) ACQUIRE_SPINLOCK(tree -> write_lock);
void
range_tree_lock_nopreempt(range_tree_t *tree)
	ACQUIRE_SPINLOCK_NP(tree -> write_lock);
void
range_tree_unlock(range_tree_t *tree) RELEASE_SPINLOCK(tree -> write_lock);
void
range_tree_unlock_nopreempt(range_tree_t *tree)
	RELEASE_SPINLOCK_NP(tree -> write_lock);

// Destroy a range tree.
//
// For any existing nodes in the tree, the range_tree_release_node event
// will be triggered with the specified node_type argument as its selector.
// This may be RANGE_TREE_NODE_TYPE_NONE if the tree is known to be empty.
//
// This must not be called until all readers are known to have completed.
void
range_tree_destroy(range_tree_t *tree, range_tree_node_type_t node_type)
	REQUIRE_SPINLOCK(tree -> write_lock);

// Insert a node into the range tree, covering a specified range.
//
// The specified range is required to be empty, and the specified node is
// required not to already be inserted in any range tree. If the node was
// previously inserted, an RCU grace period must have elapsed since it was
// removed.
error_t
range_tree_insert(range_tree_t *tree, range_tree_node_t *node, size_t base,
		  size_t size) REQUIRE_SPINLOCK(tree -> write_lock);

// Remove a node from the range tree.
//
// Note that this does not trigger the range_tree_release_node event. The caller
// is responsible for arranging for the node to be freed.
//
// The node must not be freed or reused before an RCU grace period has elapsed.
error_t
range_tree_remove(range_tree_t *tree, range_tree_node_t *node)
	REQUIRE_SPINLOCK(tree -> write_lock);

// Lookup a range in the range tree.
//
// This function returns the entry found at the given base, and returns the size
// of this entry, which will be capped at max_size.
//
// This function will retry failed lookups that coincide with a tree rotation,
// after acquiring the write lock to guarantee that no spurious failures occur
// during the retry. Therefore, callers already holding the write lock must
// use range_tree_lookup_locked() instead.
range_tree_lookup_result_t
range_tree_lookup(range_tree_t *tree, size_t base, size_t max_size)
	EXCLUDE_SPINLOCK(tree->write_lock) REQUIRE_RCU_READ;

// Lookup a range in the range tree.
//
// This function returns the same as range_tree_lookup(), but does not retry
// failed lookups, since concurrent tree rotations are not possible when the
// caller holds the write lock.
range_tree_lookup_result_t
range_tree_lookup_locked(range_tree_t *tree, size_t base, size_t max_size)
	REQUIRE_SPINLOCK(tree -> write_lock) REQUIRE_RCU_READ;

// Range tree iterator.
//
// Returns the range with the lowest base address that is not less than the
// specified start address. Returns NULL if there are no such nodes. Node sizes
// are not checked by this lookup.
//
// This can be used to iterate over a balanced range tree without being required
// to persistently hold the write lock or the RCU read critical section, or
// store state apart from the base address of the previously visited node. An
// iteration over the entire tree will complete in O(n log n) time.
//
// This function is required to acquire the write lock to avoid a potentially
// unbounded recursion. This is because it cannot be tail-recursive, and a
// concurrent write might transiently construct loops while rebalancing the
// tree. However, it is not necessary to hold the write lock between calls.
//
// Note that an O(n) time iteration is possible for any tree structure, but
// generally it must hold the write lock to avoid races with rebalancing; we
// don't currently have any use cases for that.
range_tree_node_t *
range_tree_next_node(range_tree_t *tree, size_t start)
	EXCLUDE_SPINLOCK(tree->write_lock) REQUIRE_RCU_READ;

// Simple iterator for a range tree. The RCU read lock must be held at the start
// of each iteration, though it is permitted to release and re-acquire it as
// long as the node is not accessed after releasing it. The tree's write lock
// must not be held at the start of each iteration.
//
// Node removal during iteration is permitted (after acquiring the write lock),
// but should be avoided as it will cause a rebalance of the tree.
#define range_tree_foreach(node, tree)                                         \
	for ((node) = range_tree_next_node((tree), 0U); (node) != NULL;        \
	     (node) = util_add_overflows((node)->base, 1U)                     \
			      ? NULL                                           \
			      : range_tree_next_node((tree),                   \
						     (node)->base + 1U))

// Simple container iterator. Same restrictions as for range_tree_foreach().
#define range_tree__foreach_container(container, tree, cname, nname, n)          \
	range_tree_node_t *n = range_tree_next_node((tree), 0U);                 \
	container = ((n) != NULL) ? cname##_container_of_##nname(n) : NULL;      \
	for (; (container) != NULL;                                              \
	     (n)       = util_add_overflows((n)->base, 1U)                       \
				 ? NULL                                          \
				 : range_tree_next_node((tree), (n)->base + 1U), \
	     container = ((n) != NULL) ? cname##_container_of_##nname(n)         \
				       : NULL)

#define range_tree_foreach_container(container, tree, cname, nname)            \
	range_tree__foreach_container(container, tree, cname, nname,           \
				      util_cpp_unique_ident(node))
