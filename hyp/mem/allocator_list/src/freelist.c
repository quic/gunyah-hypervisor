// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// The following copyright has been added because the code in this file is
// based on the objmanager_simple allocator from OKL4 3.0

// Australian Public Licence B (OZPLB)
//
// Version 1-0
//
// Copyright (c) 2006-2010, Open Kernel Labs, Inc.
//
// All rights reserved.
//
// Developed by: Embedded, Real-time and Operating Systems Program (ERTOS)
//               National ICT Australia
//               http://www.ertos.nicta.com.au
//
// Permission is granted by Open Kernel Labs, Inc., free of charge, to
// any person obtaining a copy of this software and any associated
// documentation files (the "Software") to deal with the Software without
// restriction, including (without limitation) the rights to use, copy,
// modify, adapt, merge, publish, distribute, communicate to the public,
// sublicense, and/or sell, lend or rent out copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimers.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimers in the documentation and/or other materials provided
//       with the distribution.
//
//     * Neither the name of Open Kernel Labs, Inc., nor the names of its
//       contributors, may be used to endorse or promote products derived
//       from this Software without specific prior written permission.
//
// EXCEPT AS EXPRESSLY STATED IN THIS LICENCE AND TO THE FULL EXTENT
// PERMITTED BY APPLICABLE LAW, THE SOFTWARE IS PROVIDED "AS-IS", AND
// NATIONAL ICT AUSTRALIA AND ITS CONTRIBUTORS MAKE NO REPRESENTATIONS,
// WARRANTIES OR CONDITIONS OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO ANY REPRESENTATIONS, WARRANTIES OR CONDITIONS
// REGARDING THE CONTENTS OR ACCURACY OF THE SOFTWARE, OR OF TITLE,
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NONINFRINGEMENT,
// THE ABSENCE OF LATENT OR OTHER DEFECTS, OR THE PRESENCE OR ABSENCE OF
// ERRORS, WHETHER OR NOT DISCOVERABLE.
//
// TO THE FULL EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT SHALL
// NATIONAL ICT AUSTRALIA OR ITS CONTRIBUTORS BE LIABLE ON ANY LEGAL
// THEORY (INCLUDING, WITHOUT LIMITATION, IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHERWISE) FOR ANY CLAIM, LOSS, DAMAGES OR OTHER
// LIABILITY, INCLUDING (WITHOUT LIMITATION) LOSS OF PRODUCTION OR
// OPERATION TIME, LOSS, DAMAGE OR CORRUPTION OF DATA OR RECORDS; OR LOSS
// OF ANTICIPATED SAVINGS, OPPORTUNITY, REVENUE, PROFIT OR GOODWILL, OR
// OTHER ECONOMIC LOSS; OR ANY SPECIAL, INCIDENTAL, INDIRECT,
// CONSEQUENTIAL, PUNITIVE OR EXEMPLARY DAMAGES, ARISING OUT OF OR IN
// CONNECTION WITH THIS LICENCE, THE SOFTWARE OR THE USE OF OR OTHER
// DEALINGS WITH THE SOFTWARE, EVEN IF NATIONAL ICT AUSTRALIA OR ITS
// CONTRIBUTORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH CLAIM, LOSS,
// DAMAGES OR OTHER LIABILITY.
//
// If applicable legislation implies representations, warranties, or
// conditions, or imposes obligations or liability on Open Kernel Labs, Inc.
// or one of its contributors in respect of the Software that
// cannot be wholly or partly excluded, restricted or modified, the
// liability of Open Kernel Labs, Inc. or the contributor is limited, to
// the full extent permitted by the applicable legislation, at its
// option, to:
// a.  in the case of goods, any one or more of the following:
// i.  the replacement of the goods or the supply of equivalent goods;
// ii.  the repair of the goods;
// iii. the payment of the cost of replacing the goods or of acquiring
//  equivalent goods;
// iv.  the payment of the cost of having the goods repaired; or
// b.  in the case of services:
// i.  the supplying of the services again; or
// ii.  the payment of the cost of having the services supplied again.
//
// The construction, validity and performance of this licence is governed
// by the laws in force in New South Wales, Australia.

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <allocator.h>
#include <attributes.h>
#include <spinlock.h>
#include <util.h>

#include "event_handlers.h"

// Maximum supported heap allocation size or alignment size. We filter out
// really large allocations so we can avoid having to think about corner-cases
// causing overflow.
#define MAX_ALLOC_SIZE	   (256UL * 1024UL * 1024UL)
#define MAX_ALIGNMENT_SIZE (16UL * 1024UL * 1024UL)

#define NODE_HEADER_SIZE (sizeof(allocator_node_t))

// Minimum allocation size from the heap.
#define HEAP_MIN_ALLOC NODE_HEADER_SIZE
#define HEAP_MIN_ALIGN NODE_HEADER_SIZE

// -------------- DEBUGGING --------------
#if defined(ALLOCATOR_DEBUG)
#define OVERFLOW_DEBUG
#define OVERFLOW_REDZONE_SIZE NODE_HEADER_SIZE
// #define DEBUG_PRINT
#endif
// ---------------------------------------

#if defined(ALLOCATOR_DEBUG)
#define CHECK_HEAP(x) check_heap_consistency(x)
#else
#define CHECK_HEAP(x)
#endif

#if defined(ALLOCATOR_DEBUG)
// Checking heap consistency:
// - Previous block should have virtual address before current block.
// - Blocks should not overlap, otherwise they should be merged.
// - Each block should be 8-byte aligned, and have a size of at least 8 bytes.
static void
check_heap_consistency(allocator_node_t *head)
{
	if (head != NULL) {
		allocator_node_t *previous = head;
		allocator_node_t *current  = head->next;

		while (current != NULL) {
#if defined(DEBUG_PRINT)
			printf("[%p : %lx] -> ", current, current->size);
#endif

			assert((uint64_t)previous < (uint64_t)current);
			assert(((uint64_t)previous + previous->size) <=
			       (uint64_t)current);
			assert(((uint64_t)current % NODE_HEADER_SIZE) == 0UL);
			assert(current->size >= NODE_HEADER_SIZE);

			previous = current;
			current	 = current->next;
		}
	}
}
#endif

// Valid cases:		   5 .------------.
//			     |   7 .------V
//   2    3		     |	   |      |
//   .--. .---.            4 .-. 6 .---.  V                8 .---. 9 .----.
//   |  | |   |              | |   |   |                     |   |   |    |
//   |  V |   V--------------+ V   |   V  +------------------+   V   |    V
//   |    |   |              |     |      |                  |       |
//   X    X   |              X     X      |                  |       X
//            +--------------+            +------------------+
//                 head                       last block
static error_t
list_add(allocator_node_t **head, allocator_node_t *node, size_t size)
{
	error_t ret = ERROR_ALLOCATOR_RANGE_OVERLAPPING;

	if (*head == NULL) {
		// 1. Add head to empty list
		*head	      = node;
		(*head)->size = size;
		(*head)->next = NULL;
	} else if (((uint64_t)node + size) < (uint64_t)(*head)) {
		// 2. Prepend to head if address range is before head
		node->next = *head;
		node->size = size;
		*head	   = node;
	} else if (((uint64_t)node + size) == (uint64_t)(*head)) {
		// 3. Merge with head
		node->size = size + (*head)->size;
		node->next = (*head)->next;
		*head	   = node;
	} else {
		allocator_node_t *previous = *head;
		allocator_node_t *current  = (*head)->next;

		while ((current != NULL) &&
		       ((uint64_t)node >= (uint64_t)(current))) {
			previous = current;
			current	 = current->next;
		}

		if (current != NULL) {
			if ((((uint64_t)previous + previous->size) ==
			     (uint64_t)node)) {
				if ((node + size) < current) {
					// 4. Merge with previous
					previous->size += size;
				} else if (((uint64_t)node + size) ==
					   (uint64_t)current) {
					// 5. Merge with previous & current
					previous->size += size + current->size;
					previous->next = current->next;
				} else {
					goto out;
				}
			} else if (((uint64_t)previous + previous->size) <
				   (uint64_t)node) {
				if (((uint64_t)node + size) <
				    (uint64_t)current) {
					// 6. Add between previous & current
					node->next     = current;
					node->size     = size;
					previous->next = node;
				} else if ((uint64_t)(node + size) ==
					   (uint64_t)current) {
					// 7. Merge with current
					node->size     = size + current->size;
					node->next     = current->next;
					previous->next = node;
				} else {
					goto out;
				}
			} else {
				goto out;
			}
		} else {
			if (((uint64_t)previous + previous->size) ==
			    (uint64_t)node) {
				// 8. Merge with previous
				previous->size += size;
			} else if (((uint64_t)previous + previous->size) <
				   (uint64_t)node) {
				// 9. Append node to list
				node->next     = NULL;
				node->size     = size;
				previous->next = node;
			} else {
				goto out;
			}
		}
	}

	ret = OK;

out:
#if defined(DEBUG_PRINT)
	if (ret != OK) {
		printf("ERROR: failed due to addresses overlapping\n");
	}
#endif

	return ret;
}

static error_t NOINLINE
allocator_heap_add_memory(allocator_t *allocator, uintptr_t addr, size_t size)
{
	allocator_node_t *block;
	error_t		  ret = OK;

	assert(addr != 0U);

	// Check input arguments
	if (!util_is_baligned(addr, NODE_HEADER_SIZE)) {
		uintptr_t new_addr = util_balign_up(addr, NODE_HEADER_SIZE);
		size -= (new_addr - addr);
		addr = new_addr;
	}
	if (!util_is_baligned(size, NODE_HEADER_SIZE)) {
		size = util_balign_down(size, NODE_HEADER_SIZE);
	}
	if (util_add_overflows(addr, size)) {
		ret = ERROR_ADDR_OVERFLOW;
	} else if (size < (2UL * NODE_HEADER_SIZE)) {
		ret = ERROR_ARGUMENT_SIZE;
	} else {
		// FIXME: Check if added memory is in kernel address space

		block = (allocator_node_t *)addr;

		// Add memory to the freelist
		spinlock_acquire(&allocator->lock);

		ret = list_add(&allocator->heap, block, size);
		if (ret == OK) {
			allocator->total_size += size;
		}

		spinlock_release(&allocator->lock);
	}

	return ret;
}

error_t
allocator_list_handle_allocator_add_ram_range(partition_t *owner,
					      paddr_t	   phys_base,
					      uintptr_t virt_base, size_t size)
{
	assert(owner != NULL);

	(void)phys_base;

	return allocator_heap_add_memory(&owner->allocator, virt_base, size);
}

// Cases:
//      1 .-----------------------.
//        |                       |
//        |                       V
//      3 |-----. 4 .----.  2 .---.
//        |     |   |    |    |   |
//	  |	V   |	 V    |	  V
//        X         X         X
//        +-----------------------+
//        |         current       |	X = aligned_alloc_start
//        |          node         |	V = aligned_alloc_end
//        +-----------------------+
//        ^                       ^
//    node_start              node_end
static void_ptr_result_t
allocate_from_node(allocator_node_t **head, allocator_node_t **previous,
		   allocator_node_t **current, size_t alloc_size,
		   size_t alloc_alignment)
{
	void_ptr_result_t ret;

	assert(*current != NULL);
	assert(util_is_p2(alloc_alignment));
	assert(alloc_size >= NODE_HEADER_SIZE);
	assert((alloc_size % NODE_HEADER_SIZE) == 0UL);

	uint64_t node_start = (uint64_t)*current;
	uint64_t node_end   = (uint64_t)*current + (*current)->size;
#if defined(OVERFLOW_DEBUG)
	node_start += OVERFLOW_REDZONE_SIZE;
#endif
	uint64_t aligned_alloc_start =
		util_balign_up(node_start, alloc_alignment);
#if defined(OVERFLOW_DEBUG)
	node_start -= OVERFLOW_REDZONE_SIZE;
	aligned_alloc_start -= OVERFLOW_REDZONE_SIZE;
#endif

	uint64_t aligned_alloc_end = aligned_alloc_start + alloc_size;

	if (util_add_overflows(aligned_alloc_start, alloc_size)) {
		ret = void_ptr_result_error(ERROR_ADDR_OVERFLOW);
	} else if ((aligned_alloc_end > node_end) ||
		   (aligned_alloc_start > node_end)) {
		ret = void_ptr_result_error(ERROR_NOMEM);
	} else {
		if (node_end == aligned_alloc_end) {
			if (node_start == aligned_alloc_start) {
				// 1. Allocate from entire node. Remove it from
				// list
				if (*previous != NULL) {
					(*previous)->next = (*current)->next;
				} else {
					*head = (*current)->next;
				}
			} else {
				// 2. Allocate from end of node
				(*current)->size -= alloc_size;
			}
		} else {
			if (node_start == aligned_alloc_start) {
				// 3. Allocate from start of node. Change start
				// addr
				allocator_node_t *next =
					(allocator_node_t *)aligned_alloc_end;

				next->next = (*current)->next;
				next->size = (*current)->size - alloc_size;

				if (*previous != NULL) {
					(*previous)->next = next;
				} else {
					*head = next;
				}
			} else {
				// 4. Allocate from middle of node. Create new
				// node after allocated section
				allocator_node_t *next =
					(allocator_node_t *)aligned_alloc_end;

				next->next	 = (*current)->next;
				next->size	 = node_end - aligned_alloc_end;
				(*current)->next = next;
				(*current)->size =
					aligned_alloc_start - node_start;
			}
		}

		ret = void_ptr_result_ok((void *)aligned_alloc_start);
	}

	return ret;
}

static void_ptr_result_t
allocate_block(allocator_node_t **head, size_t size, size_t alignment)
{
	void_ptr_result_t ret;

	assert(*head != NULL);
	assert((size % NODE_HEADER_SIZE) == 0UL);
	assert(size > 0UL);
	assert(util_is_p2(alignment));
	assert(alignment >= (size_t)sizeof(size_t));

	allocator_node_t *previous = NULL;
	allocator_node_t *current  = *head;

#if defined(DEBUG_PRINT)
	printf("%s: head %p: size %zu (0x%lx), alignment %zu\n", __func__,
	       *head, size, size, alignment);
#endif

	while (current != NULL) {
		void_ptr_result_t result = allocate_from_node(
			head, &previous, &current, size, alignment);

		if (result.e == OK) {
#if defined(DEBUG_PRINT)
			printf("  -- allocated %p\n", result);
#endif
			ret = result;
			goto out;
		}

		previous = current;
		current	 = current->next;
	}

#if defined(DEBUG_PRINT)
	printf("  -- out of memory\n");
#endif
	ret = void_ptr_result_error(ERROR_NOMEM);

out:
	return ret;
}

void_ptr_result_t
allocator_allocate_object(allocator_t *allocator, size_t size,
			  size_t min_alignment)
{
	void_ptr_result_t ret;

	size_t alignment = util_max(min_alignment, alignof(size_t));

	spinlock_acquire(&allocator->lock);

	if (allocator->heap == NULL) {
		ret = void_ptr_result_error(ERROR_NOMEM);
		goto error;
	}

	assert(size > 0UL);
	assert(alignment > 0UL);
	assert((alignment & (alignment - 1UL)) == 0UL);
	assert((alignment >= (size_t)sizeof(size_t)));

#if defined(DEBUG_PRINT)
	printf("%s:\nheap %p: size %zu, alignment %zu\n", __func__,
	       allocator->heap, size, alignment);
#endif
	if ((size > MAX_ALLOC_SIZE) || (alignment > MAX_ALIGNMENT_SIZE)) {
		ret = void_ptr_result_error(ERROR_ARGUMENT_INVALID);
		goto error;
	}

	size = util_balign_up(size, HEAP_MIN_ALLOC);

	if (alignment < HEAP_MIN_ALIGN) {
		alignment = HEAP_MIN_ALIGN;
	}

#if defined(DEBUG_PRINT)
	printf("After alignment. size: %zu alignment: %zu\n", size, alignment);
#endif

#if defined(OVERFLOW_DEBUG)
	size += 2UL * OVERFLOW_REDZONE_SIZE;
#endif

	CHECK_HEAP(allocator->heap);
	ret = allocate_block(&allocator->heap, size, alignment);
	CHECK_HEAP(allocator->heap);

	if (ret.e != OK) {
		goto error;
	}

	allocator->alloc_size += size;

#if defined(ALLOCATOR_DEBUG)
	char  *data  = (char *)ret.r;
	size_t start = 0UL;
	size_t end   = size;

#if defined(OVERFLOW_DEBUG)
	end = OVERFLOW_REDZONE_SIZE;
	memset(&data[start], 0xe7, end - start);
	start = end;
	end   = size - OVERFLOW_REDZONE_SIZE;
#endif
	memset(&data[start], 0xa5, end - start);
#if defined(OVERFLOW_DEBUG)
	start = end;
	end   = size;
	memset(&data[start], 0xe8, end - start);

	// Return address after the overflow check values
	ret.r = (void *)&data[OVERFLOW_REDZONE_SIZE];
#endif
#endif

error:
	spinlock_release(&allocator->lock);
	return ret;
}

// We will probably not be using list_remove() and heap_remove memory()
// functions since we will only have the possibility of adding memory to the
// heap. We will maybe remove when deleting a partition.
static void
list_remove(allocator_node_t **head, allocator_node_t *remove,
	    allocator_node_t *previous)
{
	if (previous == NULL) {
		*head = remove->next;
	} else {
		previous->next = remove->next;
	}
}

// TODO: Exported only for test code currently
error_t
allocator_heap_remove_memory(allocator_t *allocator, void *obj, size_t size);

// Returns -1 if addresses are still being used and therefore cannot be freed.
error_t NOINLINE
allocator_heap_remove_memory(allocator_t *allocator, void *obj, size_t size)
{
	error_t ret = ERROR_ALLOCATOR_MEM_INUSE;

	assert(obj != NULL);
	assert(allocator->heap != NULL);

	allocator_node_t *previous = NULL;
	allocator_node_t *current  = allocator->heap;
	uint64_t	  object_location;
	uint64_t	  current_location;
	uint64_t	  previous_location;
	uint64_t	  aligned_alloc_end;

	spinlock_acquire(&allocator->lock);

	size = util_balign_up(size, HEAP_MIN_ALLOC);

	while (((uint64_t)obj > (uint64_t)current) && (current != NULL)) {
		assert((uint64_t)previous < (uint64_t)obj);
		previous = current;
		current	 = current->next;
	}

	object_location	  = (uint64_t)obj;
	current_location  = (uint64_t)current;
	previous_location = (uint64_t)previous;

	assert(!util_add_overflows(object_location, size));
	aligned_alloc_end = object_location + size;

	assert((object_location <= current_location) ||
	       (current_location == 0UL));
	assert(object_location > previous_location);

	if (current_location == object_location) {
		if ((current_location + current->size) < aligned_alloc_end) {
			goto out;
		} else if ((current_location + current->size) ==
			   aligned_alloc_end) {
			list_remove(&allocator->heap, current, previous);
		} else {
			// Divide current into 2 nodes and remove first one
			allocator_node_t *new;

			new	      = (allocator_node_t *)aligned_alloc_end;
			new->next     = current->next;
			new->size     = current->size - size;
			current->next = new;
			current->size = size;
			list_remove(&allocator->heap, current, previous);
		}
	} else if (previous != NULL) {
		if ((previous_location + previous->size) < aligned_alloc_end) {
			goto out;
		}

		if ((previous_location + previous->size) == aligned_alloc_end) {
			// Reduce size of previous
			previous->size -= size;
		} else {
			// Divide previous into 3 nodes & remove middle one
			allocator_node_t *new;

			new	  = (allocator_node_t *)aligned_alloc_end;
			new->next = current;
			new->size = previous_location + previous->size -
				    aligned_alloc_end;

			previous->next = new;
			previous->size = object_location - previous_location;
		}
	} else {
		goto out;
	}

	ret = OK;
	allocator->total_size -= size;

out:
	spinlock_release(&allocator->lock);
	return ret;
}

static void
deallocate_block(allocator_node_t **head, void *object, size_t size)
{
	assert(object != NULL);
	assert(size >= NODE_HEADER_SIZE);
	assert((size % NODE_HEADER_SIZE) == 0UL);

	allocator_node_t *previous   = NULL;
	allocator_node_t *next	     = NULL;
	allocator_node_t *freed_node = NULL;

	uint64_t object_location;
	uint64_t next_location;
	uint64_t previous_location;

	if (*head == NULL) {
		freed_node	 = object;
		freed_node->size = size;
		freed_node->next = NULL;
		*head		 = freed_node;
		goto out;
	}

	previous = *head;
	next	 = (*head)->next;

	while (((uint64_t)object > (uint64_t)next) && (next != NULL)) {
		assert((uint64_t)previous < (uint64_t)next);
		previous = next;
		next	 = next->next;
	}

	object_location = (uint64_t)object;
	next_location	= (uint64_t)next;
	if (previous != NULL) {
		previous_location = (uint64_t)previous;
	} else {
		previous_location = (uint64_t)~0UL;
	}

	assert((object_location <= next_location) || (next_location == 0UL));

	if ((previous != NULL) &&
	    (previous_location + previous->size) == object_location) {
		// Combine the free memory into the previous node.
		assert(!util_add_overflows(previous->size, size));
		previous->size += size;
		freed_node = previous;

		// If necessary, connect the freed node with the next one.
		if ((next != NULL) && (((uint64_t)freed_node +
					freed_node->size) == next_location)) {
			freed_node->size += next->size;
			freed_node->next = next->next;
		}
	} else if ((previous == NULL) ||
		   (object_location < previous_location)) {
		// Create node as head
		freed_node	 = object;
		freed_node->size = size;
		freed_node->next = previous;
		*head		 = freed_node;

		// If necessary, connect the freed node with the next one.
		if ((previous != NULL) &&
		    (((uint64_t)freed_node + freed_node->size) ==
		     previous_location)) {
			freed_node->size += previous->size;
			freed_node->next = previous->next;
		}
	} else {
		assert(previous != NULL);

		// Create a new header in the object.
		freed_node	 = object;
		freed_node->size = size;
		freed_node->next = next;
		previous->next	 = freed_node;

		// If necessary, connect the freed node with the next one.
		if ((next != NULL) && (((uint64_t)freed_node +
					freed_node->size) == next_location)) {
			freed_node->size += next->size;
			freed_node->next = next->next;
		}
	}

out:
	return;
}

error_t
allocator_deallocate_object(allocator_t *allocator, void *object, size_t size)
{
	assert(object != NULL);
	assert(size > 0UL);

	spinlock_acquire(&allocator->lock);

#if defined(DEBUG_PRINT)
	if (allocator->heap != NULL) {
		printf("%s: heap %p: size %zu / --> %p\n", __func__,
		       allocator->heap, size, object);
	}
#endif

	size = util_balign_up(size, HEAP_MIN_ALLOC);

#if defined(OVERFLOW_DEBUG)
	// Increment size +(2*NODE_HEADER_SIZE) to also free the overflow check
	// values in NODE_HEADER_SIZE addresses before and after the object
	// And go to the address where the overflow check values start
	size += 2UL * OVERFLOW_REDZONE_SIZE;
	object = (void *)((uintptr_t)object - OVERFLOW_REDZONE_SIZE);
#endif

#if defined(ALLOCATOR_DEBUG)
	memset(object, 0xe3, size);
#endif

	CHECK_HEAP(allocator->heap);
	deallocate_block(&allocator->heap, object, size);
	CHECK_HEAP(allocator->heap);

	allocator->alloc_size -= size;

	spinlock_release(&allocator->lock);

	return OK;
}

error_t
allocator_init(allocator_t *allocator)
{
	assert(allocator->heap == NULL);

	allocator->total_size = 0UL;
	allocator->alloc_size = 0UL;

	spinlock_init(&allocator->lock);
	return OK;
}
