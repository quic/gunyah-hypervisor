// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Helper macros (replace with template expansion in autogen)
#define HYPTYPES_DECLARE_RESULT_(name, type)                                   \
	typedef struct name##_result {                                         \
		type	r;                                                     \
		error_t alignas(register_t) e;                                 \
	} name##_result_t;                                                     \
	static inline name##_result_t name##_result_error(error_t err)         \
	{                                                                      \
		return (name##_result_t){ .e = err };                          \
	}                                                                      \
	static inline name##_result_t name##_result_ok(type ret)               \
	{                                                                      \
		return (name##_result_t){ .r = ret, .e = OK };                 \
	}

#define HYPTYPES_DECLARE_RESULT(type) HYPTYPES_DECLARE_RESULT_(type, type##_t)

#define HYPTYPES_DECLARE_RESULT_PTR_(name, type)                               \
	typedef struct name##_ptr_result {                                     \
		type *	r;                                                     \
		error_t alignas(register_t) e;                                 \
	} name##_ptr_result_t;                                                 \
	static inline name##_ptr_result_t name##_ptr_result_error(error_t err) \
	{                                                                      \
		return (name##_ptr_result_t){ .e = err };                      \
	}                                                                      \
	static inline name##_ptr_result_t name##_ptr_result_ok(type *ret)      \
	{                                                                      \
		return (name##_ptr_result_t){ .r = ret, .e = OK };             \
	}

#define HYPTYPES_DECLARE_RESULT_PTR(type)                                      \
	HYPTYPES_DECLARE_RESULT_PTR_(type, type##_t)
