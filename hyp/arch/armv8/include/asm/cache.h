// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define CACHE_DO_OP(x, size, op, is_object)                                    \
	do {                                                                   \
		size_t _line_size = 1U << CPU_L1D_LINE_BITS;                   \
                                                                               \
		uintptr_t _base =                                              \
			util_balign_down((uintptr_t)(x), _line_size);          \
		uintptr_t  _end = (uintptr_t)(x) + (size);                     \
		register_t ordering;                                           \
                                                                               \
		assert(!util_add_overflows((uintptr_t)x, (size)-1));           \
                                                                               \
		if (is_object) {                                               \
			__asm__ volatile("" : "=r"(ordering) : "m"(*(x)));     \
		} else {                                                       \
			__asm__ volatile("" : "=r"(ordering) : : "memory");    \
		}                                                              \
                                                                               \
		do {                                                           \
			__asm__ volatile("DC " #op ", %1"                      \
					 : "+r"(ordering)                      \
					 : "r"(_base));                        \
			_base = _base + _line_size;                            \
		} while (_base < _end);                                        \
                                                                               \
		__asm__ volatile("dsb ish" : "+r"(ordering));                  \
		if (is_object) {                                               \
			__asm__ volatile("" : "+r"(ordering), "+m"(*(x)));     \
		} else {                                                       \
			__asm__ volatile("" : "+r"(ordering) : : "memory");    \
		}                                                              \
	} while (0)

#define CACHE_OP_RANGE(x, size, op) CACHE_DO_OP(x, size, op, false)
#define CACHE_OP_OBJECT(x, op)	    CACHE_DO_OP(&(x), sizeof(x), op, true)

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
		} *_x = (void *)x;                                             \
		CACHE_OP_OBJECT(*_x, op);                                      \
	} while (0)

#define CACHE_CLEAN_FIXED_RANGE(x, size) CACHE_OP_FIXED_RANGE(x, size, CVAC)
#define CACHE_INVALIDATE_FIXED_RANGE(x, size)                                  \
	CACHE_OP_FIXED_RANGE(x, size, IVAC)
#define CACHE_CLEAN_INVALIDATE_FIXED_RANGE(x, size)                            \
	CACHE_OP_FIXED_RANGE(x, size, CIVAC)
