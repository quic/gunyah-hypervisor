// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// The list implementation consists of a circular double linked list and the
// list type contains a head pointer to the first element of the list.

// All the following functions require the list to be locked if it may be
// accessed by other threads, unless noted otherwise.

#include <atomic.h>
#include <util.h>

void
list_init(list_t *list);

list_node_t *
list_get_head(list_t *list);

bool
list_is_empty(list_t *list);

void
list_insert_at_head(list_t *list, list_node_t *node);

void
list_insert_at_tail(list_t *list, list_node_t *node);

// This function inserts a node in order, where the ordering is defined by the
// caller.
//
// If we want, for example, to insert a node in increasing order, then the
// caller needs to provide a function pointer that returns true if node a is
// smaller than node b, according to the caller's criteria.
//
// Returns true if the new node is placed at the head of the list, or false if
// the new node has been inserted after the head.
bool
list_insert_in_order(list_t *list, list_node_t *node,
		     bool (*compare_fn)(list_node_t *a, list_node_t *b));

void
list_insert_after_node(list_t *list, list_node_t *prev, list_node_t *node);

// The _release variants of insert must be used on any list that is iterated
// with a _consume iterator.
void
list_insert_at_tail_release(list_t *list, list_node_t *node);

// This function returns true if node has been removed from head and the list is
// not empty after the deletion.
//
// If the list is ever iterated by a _consume iterator, then the specified node
// must not be either freed or added to another list until an RCU grace period
// has elapsed; i.e. rcu_enqueue() or rcu_sync() must be called after this
// function returns.
bool
list_delete_node(list_t *list, list_node_t *node);

// Simple iterator. The list must be locked if other threads might modify it,
// and the iterator must not delete nodes.
#define list_foreach(node, list)                                               \
	for ((node) = atomic_load_relaxed(&(list)->head.next);                 \
	     (node) != &(list)->head;                                          \
	     (node) = atomic_load_relaxed(&(node)->next))

#define list__foreach_container(container, list, cname, nname, n)              \
	list_node_t *n = atomic_load_relaxed(&list->head.next);                \
	container      = (n != &list->head) ? cname##_container_of_##nname(n)  \
					    : NULL;                            \
	for (; container != NULL;                                              \
	     n	       = atomic_load_relaxed(&n->next),                        \
	     container = (n != &list->head) ? cname##_container_of_##nname(n)  \
					    : NULL)

// Simple container iterator. The list must be locked if other threads might
// modify it, and the iterator must not delete nodes.
#define list_foreach_container(container, list, cname, nname)                  \
	list__foreach_container((container), (list), cname, nname,             \
				util_cpp_unique_ident(node))

#define list__foreach_container_safe(container, list, cname, nname, n, load)   \
	list_node_t *n = load(&list->head.next);                               \
	container      = (n != &list->head) ? cname##_container_of_##nname(n)  \
					    : NULL;                            \
	n	       = load(&n->next);                                       \
	for (; container != NULL;                                              \
	     container = (n != &list->head) ? cname##_container_of_##nname(n)  \
					    : NULL,                            \
	     n	       = load(&n->next))

// Deletion-safe container iterator. The list must be locked if other threads
// might modify it. The iterator may delete the current node.
#define list_foreach_container_maydelete(container, list, cname, nname)        \
	list__foreach_container_safe((container), (list), cname, nname,        \
				     util_cpp_unique_ident(next),              \
				     atomic_load_relaxed)

// RCU-safe container iterator. Must only be used within an RCU critical
// section. The list need not be locked, but other threads that insert nodes
// must use the _release variants of the insert functions, and any thread that
// deletes a node must allow an RCU grace period to elapse before either freeing
// the memory or adding it to a list again.
#define list_foreach_container_consume(container, list, cname, nname)          \
	list__foreach_container_safe((container), (list), cname, nname,        \
				     util_cpp_unique_ident(next),              \
				     atomic_load_consume)
