// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// The container_of macros for the tagged types

ALWAYS_INLINE static inline DemoContainer_t *
DemoContainer_container_of_int_var(const uint8_t *ptr)
{
	_Static_assert(
		offsetof(DemoContainer_t, int_var) == 0U,
		"Generated offset for int_var in DemoContainer_t is incorrect");
	return ((DemoContainer_t *)((uintptr_t)(ptr)-0U));
}

ALWAYS_INLINE static inline DemoContainer_t *
DemoContainer_container_of_bit_field_var(const StructBitField_t *ptr)
{
	_Static_assert(
		offsetof(DemoContainer_t, bit_field_var) == 8U,
		"Generated offset for bit_field_var in DemoContainer_t is incorrect");
	return ((DemoContainer_t *)((uintptr_t)(ptr)-8U));
}

ALWAYS_INLINE static inline DemoContainer_t *
DemoContainer_container_of_nested_struct(const DemoSingleField_t *ptr)
{
	_Static_assert(
		offsetof(DemoContainer_t, nested_struct) == 24U,
		"Generated offset for nested_struct in DemoContainer_t is incorrect");
	return ((DemoContainer_t *)((uintptr_t)(ptr)-24U));
}

ALWAYS_INLINE static inline DemoContainer_t *
DemoContainer_container_of_nested_struct2(const DemoFixed_t *ptr)
{
	_Static_assert(
		offsetof(DemoContainer_t, nested_struct2) == 32U,
		"Generated offset for nested_struct2 in DemoContainer_t is incorrect");
	return ((DemoContainer_t *)((uintptr_t)(ptr)-32U));
}
