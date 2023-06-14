// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define CACHE_BARRIER_OBJECT_LOAD(x, order)                                    \
	__asm__ volatile("" : "=r"(order) : "m"(*(x)))
#define CACHE_BARRIER_OBJECT_STORE(x, order)                                   \
	__asm__ volatile("" : "+r"(order), "+m"(*(x)))
#define CACHE_BARRIER_MEMORY_LOAD(x, order)                                    \
	__asm__ volatile("" : "=r"(order) : : "memory")
#define CACHE_BARRIER_MEMORY_STORE(x, order)                                   \
	__asm__ volatile("" : "+r"(order) : : "memory")

#define CACHE_DO_OP(x, size, op, CACHE_BARRIER)                                \
	do {                                                                   \
		const size_t line_size_ = 1U << CPU_L1D_LINE_BITS;             \
                                                                               \
		uintptr_t line_base_ =                                         \
			util_balign_down((uintptr_t)(x), line_size_);          \
		uintptr_t  end_ = (uintptr_t)(x) + (size);                     \
		register_t ordering;                                           \
                                                                               \
		assert(!util_add_overflows((uintptr_t)x, (size)-1U));          \
                                                                               \
		CACHE_BARRIER##_LOAD(x, ordering);                             \
                                                                               \
		do {                                                           \
			__asm__ volatile("DC " #op ", %1"                      \
					 : "+r"(ordering)                      \
					 : "r"(line_base_));                   \
			line_base_ = line_base_ + line_size_;                  \
		} while (line_base_ < end_);                                   \
                                                                               \
		__asm__ volatile("dsb ish" : "+r"(ordering));                  \
		CACHE_BARRIER##_STORE(x, ordering);                            \
	} while (0)

#define CACHE_OP_RANGE(x, size, op)                                            \
	CACHE_DO_OP(x, size, op, CACHE_BARRIER_MEMORY)
#define CACHE_OP_OBJECT(x, op)                                                 \
	CACHE_DO_OP(&(x), sizeof(x), op, CACHE_BARRIER_OBJECT)

#define CACHE_CLEAN_RANGE(x, size)	      CACHE_OP_RANGE(x, size, CVAC)
#define CACHE_INVALIDATE_RANGE(x, size)	      CACHE_OP_RANGE(x, size, IVAC)
#define CACHE_CLEAN_INVALIDATE_RANGE(x, size) CACHE_OP_RANGE(x, size, CIVAC)

#define CACHE_CLEAN_OBJECT(x)		 CACHE_OP_OBJECT(x, CVAC)
#define CACHE_INVALIDATE_OBJECT(x)	 CACHE_OP_OBJECT(x, IVAC)
#define CACHE_CLEAN_INVALIDATE_OBJECT(x) CACHE_OP_OBJECT(x, CIVAC)

#define CACHE_OP_FIXED_RANGE(x, size, op)                                      \
	do {                                                                   \
		struct {                                                       \
			char p[size];                                          \
		} *x_ = (void *)x;                                             \
		CACHE_OP_OBJECT(*x_, op);                                      \
	} while (0)

#define CACHE_CLEAN_FIXED_RANGE(x, size) CACHE_OP_FIXED_RANGE(x, size, CVAC)
#define CACHE_INVALIDATE_FIXED_RANGE(x, size)                                  \
	CACHE_OP_FIXED_RANGE(x, size, IVAC)
#define CACHE_CLEAN_INVALIDATE_FIXED_RANGE(x, size)                            \
	CACHE_OP_FIXED_RANGE(x, size, CIVAC)
