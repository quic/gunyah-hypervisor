// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <list.h>

void
list_init(list_t *list)
{
	assert(list != NULL);

	list_node_t *head = &list->head;

	atomic_store_relaxed(&head->next, head);
	head->prev = head;
}

bool
list_is_empty(list_t *list)
{
	assert(list != NULL);

	return (atomic_load_relaxed(&list->head.next) == &list->head);
}

list_node_t *
list_get_head(list_t *list)
{
	assert(list != NULL);

	list_node_t *node = NULL;

	if (!list_is_empty(list)) {
		node = atomic_load_relaxed(&list->head.next);
	}

	return node;
}

static inline void
list_insert_at_head_explicit(list_t *list, list_node_t *node,
			     memory_order order)
{
	assert(list != NULL);
	assert(node != NULL);

	list_node_t *prev = &list->head;
	list_node_t *next = atomic_load_relaxed(&prev->next);

	node->prev = prev;
	atomic_store_relaxed(&node->next, next);

	atomic_store_explicit(&prev->next, node, order);
	next->prev = node;
}

void
list_insert_at_head(list_t *list, list_node_t *node)
{
	list_insert_at_head_explicit(list, node, memory_order_relaxed);
}

static inline void
list_insert_at_tail_explicit(list_t *list, list_node_t *node,
			     memory_order order)
{
	assert(list != NULL);
	assert(node != NULL);

	list_node_t *next = &list->head;
	list_node_t *prev = next->prev;

	node->prev = prev;
	atomic_store_relaxed(&node->next, next);

	atomic_store_explicit(&prev->next, node, order);
	next->prev = node;
}

void
list_insert_at_tail(list_t *list, list_node_t *node)
{
	list_insert_at_tail_explicit(list, node, memory_order_relaxed);
}

void
list_insert_at_tail_release(list_t *list, list_node_t *node)
{
	list_insert_at_tail_explicit(list, node, memory_order_release);
}

static list_node_t *
find_prev_node_based_on_order(list_node_t *head, list_node_t *new_node,
			      bool (*compare_fn)(list_node_t *a,
						 list_node_t *b))
{
	list_node_t *node = head;
	assert(node != NULL);

	while (atomic_load_relaxed(&node->next) != head) {
		if (compare_fn(new_node, atomic_load_relaxed(&node->next))) {
			break;
		}
		node = atomic_load_relaxed(&node->next);
	}

	return node;
}

static inline bool
list_insert_in_order_explicit(list_t *list, list_node_t *node,
			      bool (*compare_fn)(list_node_t *a,
						 list_node_t *b),
			      memory_order order)
{
	assert(list != NULL);
	assert(node != NULL);

	bool	     new_head = false;
	list_node_t *head     = &list->head;

	list_node_t *prev =
		find_prev_node_based_on_order(head, node, compare_fn);
	list_node_t *next = atomic_load_relaxed(&prev->next);

	if (prev == head) {
		new_head = true;
	}

	node->prev = prev;
	atomic_store_relaxed(&node->next, next);

	atomic_store_explicit(&prev->next, node, order);
	next->prev = node;

	return new_head;
}

bool
list_insert_in_order(list_t *list, list_node_t *node,
		     bool (*compare_fn)(list_node_t *a, list_node_t *b))
{
	return list_insert_in_order_explicit(list, node, compare_fn,
					     memory_order_relaxed);
}

static inline void
list_insert_after_node_explicit(list_t *list, list_node_t *prev,
				list_node_t *node, memory_order order)
{
	assert(node != NULL);
	assert(prev != NULL);

	(void)list;

	list_node_t *next = atomic_load_relaxed(&prev->next);

	node->prev = prev;
	atomic_store_relaxed(&node->next, next);

	atomic_store_explicit(&prev->next, node, order);
	next->prev = node;
}

void
list_insert_after_node(list_t *list, list_node_t *prev, list_node_t *node)
{
	list_insert_after_node_explicit(list, prev, node, memory_order_relaxed);
}

bool
list_delete_node(list_t *list, list_node_t *node)
{
	assert(list != NULL);
	assert(node != NULL);

	bool new_head = false;

	if ((atomic_load_relaxed(&node->next) == NULL) ||
	    (node->prev == NULL)) {
		goto out;
	}

	list_node_t *head = &list->head;
	list_node_t *next = atomic_load_relaxed(&node->next);
	list_node_t *prev = node->prev;

	atomic_store_relaxed(&prev->next, next);
	next->prev = prev;

	if ((prev == head) && (next != head)) {
		new_head = true;
	}

	// Note: we do not zero the node's pointers here, because there might
	// be a list_foreach_container_consume() that still holds a pointer to
	// the node.
out:
	return new_head;
}
