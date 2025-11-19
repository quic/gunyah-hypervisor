// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef HYPTYPES_H_
#define HYPTYPES_H_

#include <limits.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct Back_s			     Back_t;
typedef struct Default_s		     Default_t;
typedef union Demo2U_u			     Demo2U_t;
typedef struct DemoContainer_s		     DemoContainer_t;
typedef struct DemoFixed_s		     DemoFixed_t;
typedef struct DemoPacked_s		     DemoPacked_t;
typedef struct DemoPackedInObject_s	     DemoPackedInObject_t;
typedef struct DemoSingleField_s	     DemoSingleField_t;
typedef struct DemoUStruct_s		     DemoUStruct_t;
typedef struct ExecContext_s		     ExecContext_t;
typedef struct NestUI_s			     NestUI_t;
typedef struct Standalone_s		     Standalone_t;
typedef struct StructBitField_b		     StructBitField_t;
typedef struct TEST_COMB_feat_var_VAR1_b     TEST_COMB_feat_var_VAR1_t;
typedef struct TEST_COMB_feat_var_VAR2_b     TEST_COMB_feat_var_VAR2_t;
typedef struct TEST_COMB_multi_over_VAR1_b   TEST_COMB_multi_over_VAR1_t;
typedef struct TEST_COMB_multi_over_VAR2_b   TEST_COMB_multi_over_VAR2_t;
typedef struct TEST_DEFAULT_feat_b	     TEST_DEFAULT_feat_t;
typedef struct TEST_DEFAULT_plain_b	     TEST_DEFAULT_plain_t;
typedef struct TestBitField_b		     TestBitField_t;
typedef struct UBitField_b		     UBitField_t;
typedef struct auto_layout_register_b	     auto_layout_register_t;
typedef struct device_descriptor_s	     device_descriptor_t;
typedef struct initialization_register_b     initialization_register_t;
typedef struct interrupt_control_register_b  interrupt_control_register_t;
typedef struct memory_config_register_b	     memory_config_register_t;
typedef struct module_config_register_b	     module_config_register_t;
typedef struct peripheral_control_register_b peripheral_control_register_t;
typedef struct sizeof_test_s		     sizeof_test_t;
typedef struct system_control_register_b     system_control_register_t;
typedef struct test_struct_s		     test_struct_t;

#define ARRAY_SIZE 10

struct Back_s {
	int8_t	k;
	int8_t	p;
	int8_t	TRI_tj;
	uint8_t TRI_tp;
};

#define CSZ 2

struct Default_s {
	uint32_t x;
};

// Bitfield: UBitField <uint64_t x2>
typedef struct UBitField_b {
	// 31:0      uint32_t test
	// 42:32     uint16_t test1
	// 58:51     uint8_t test5
	uint64_t bf[2];
} UBitField_t;

#define UBitField_default()                                                    \
	(UBitField_t)                                                          \
	{                                                                      \
		.bf = { 0x0U, 0x0U }                                           \
	}

#define UBitField_cast(val_0, val_1)                                           \
	(UBitField_t)                                                          \
	{                                                                      \
		.bf = {(val_0), (val_1) }                                      \
	}

void
UBitField_init(UBitField_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
UBitField_t
UBitField_clean(UBitField_t bit_field);

bool
UBitField_is_equal(UBitField_t b1, UBitField_t b2);

bool
UBitField_is_empty(UBitField_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
UBitField_is_clean(UBitField_t bit_field);

union Demo2U_u {
	uint32_t    int_var;
	UBitField_t bit_var;
};

struct DemoFixed_s {
	uint8_t	 start_var;
	uint8_t	 pad_to_cacheline_var_[63];
	uint8_t	 cacheline_var;
	uint8_t	 pad_to_next_cacheline_var_[63];
	uint64_t next_cacheline_var;
};

struct DemoSingleField_s {
	uint32_t int_var;
};

// Bitfield: StructBitField <uint64_t x2>
typedef struct StructBitField_b {
	// 31:0      uint32_t test
	// 42:32     uint16_t test1
	// 58:51     uint8_t test5
	uint64_t bf[2];
} StructBitField_t;

#define StructBitField_default()                                               \
	(StructBitField_t)                                                     \
	{                                                                      \
		.bf = { 0x0U, 0x0U }                                           \
	}

#define StructBitField_cast(val_0, val_1)                                      \
	(StructBitField_t)                                                     \
	{                                                                      \
		.bf = {(val_0), (val_1) }                                      \
	}

void
StructBitField_init(StructBitField_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
StructBitField_t
StructBitField_clean(StructBitField_t bit_field);

bool
StructBitField_is_equal(StructBitField_t b1, StructBitField_t b2);

bool
StructBitField_is_empty(StructBitField_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
StructBitField_is_clean(StructBitField_t bit_field);

struct DemoContainer_s {
	uint8_t		  int_var;
	uint8_t		  pad_to_bit_field_var_[7];
	StructBitField_t  bit_field_var;
	DemoSingleField_t nested_struct;
	uint8_t		  pad_to_nested_struct2_[4];
	DemoFixed_t	  nested_struct2;
};

struct __attribute__((packed)) DemoPacked_s {
	uint8_t		  int_var;
	StructBitField_t  bit_field_var;
	DemoSingleField_t nested_struct;
};

// Bitfield: TestBitField <uint64_t x2>
typedef struct TestBitField_b {
	// 31:0      uint32_t test
	// 42:32     uint16_t test1
	// 50:43     uint8_t test5
	uint64_t bf[2];
} TestBitField_t;

#define TestBitField_default()                                                 \
	(TestBitField_t)                                                       \
	{                                                                      \
		.bf = { 0x0U, 0x0U }                                           \
	}

#define TestBitField_cast(val_0, val_1)                                        \
	(TestBitField_t)                                                       \
	{                                                                      \
		.bf = {(val_0), (val_1) }                                      \
	}

void
TestBitField_init(TestBitField_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TestBitField_t
TestBitField_clean(TestBitField_t bit_field);

bool
TestBitField_is_equal(TestBitField_t b1, TestBitField_t b2);

bool
TestBitField_is_empty(TestBitField_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TestBitField_is_clean(TestBitField_t bit_field);

struct __attribute__((packed)) DemoPackedInObject_s {
	uint8_t	       int_var;
	TestBitField_t bit_field_var;
	uint32_t       tt;
};

struct DemoUStruct_s {
	uint8_t	    int_var;
	uint8_t	    pad_to_bit_field_var_[7];
	UBitField_t bit_field_var;
	Demo2U_t    embedded_union;
};

struct NestUI_s {
	uint32_t t;
	uint32_t uy;
};

struct ExecContext_s {
	char		     O_Machine_e;
	char		     O_Machine_f;
	char		     Machine_e;
	char		     Machine_f;
	uint8_t		     pad_to_O_bit_field_in_obj_[4];
	TestBitField_t	     O_bit_field_in_obj;
	Back_t		    *tt;
	int32_t		     a;
	int32_t		     b;
	int32_t		     O_o;
	int32_t		     O_p;
	uint32_t	     O_j[10];
	NestUI_t	     O_JJ[10];
	uint32_t	     O_N_t;
	uint32_t	     O_N_uy;
	uint32_t	     O_OO_t;
	uint32_t	     O_OO_uy;
	uint32_t	     t;
	uint32_t	     uy;
	uint8_t		     pad_to_permitted_offset_[1856];
	uint32_t	     permitted_offset;
	DemoPackedInObject_t O_ds;
	Back_t		     yy[8];
	Back_t		     jj[10];
	uint8_t		     pad_end_[7];
};

#define LOGICAL_AND_TEST    (bool)1U	  // 0x1
#define LOGICAL_OR_TEST	    (bool)0U	  // 0x0
#define PAGE_SIZE	    (size_t)4096U // 0x1000
#define SIZEOF_MINUSES_TEST (size_t)4092U // 0xffc
#define SIZEOF_MINUS_TEST   (size_t)4092U // 0xffc

struct Standalone_s {
	uint8_t t;
	uint8_t u;
};

// Bitfield: TEST_COMB_feat_var_VAR1 <uint64_t>
typedef struct TEST_COMB_feat_var_VAR1_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_COMB_feat_var_VAR1_t;

#define TEST_COMB_feat_var_VAR1_default()                                      \
	(TEST_COMB_feat_var_VAR1_t)                                            \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_COMB_feat_var_VAR1_cast(val_0)                                    \
	(TEST_COMB_feat_var_VAR1_t)                                            \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_COMB_feat_var_VAR1_raw(TEST_COMB_feat_var_VAR1_t bit_field);

void
TEST_COMB_feat_var_VAR1_init(TEST_COMB_feat_var_VAR1_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_COMB_feat_var_VAR1_t
TEST_COMB_feat_var_VAR1_clean(TEST_COMB_feat_var_VAR1_t bit_field);

bool
TEST_COMB_feat_var_VAR1_is_equal(TEST_COMB_feat_var_VAR1_t b1,
				 TEST_COMB_feat_var_VAR1_t b2);

bool
TEST_COMB_feat_var_VAR1_is_empty(TEST_COMB_feat_var_VAR1_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_COMB_feat_var_VAR1_is_clean(TEST_COMB_feat_var_VAR1_t bit_field);

// Bitfield: TEST_COMB_feat_var_VAR2 <uint64_t>
typedef struct TEST_COMB_feat_var_VAR2_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_COMB_feat_var_VAR2_t;

#define TEST_COMB_feat_var_VAR2_default()                                      \
	(TEST_COMB_feat_var_VAR2_t)                                            \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_COMB_feat_var_VAR2_cast(val_0)                                    \
	(TEST_COMB_feat_var_VAR2_t)                                            \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_COMB_feat_var_VAR2_raw(TEST_COMB_feat_var_VAR2_t bit_field);

void
TEST_COMB_feat_var_VAR2_init(TEST_COMB_feat_var_VAR2_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_COMB_feat_var_VAR2_t
TEST_COMB_feat_var_VAR2_clean(TEST_COMB_feat_var_VAR2_t bit_field);

bool
TEST_COMB_feat_var_VAR2_is_equal(TEST_COMB_feat_var_VAR2_t b1,
				 TEST_COMB_feat_var_VAR2_t b2);

bool
TEST_COMB_feat_var_VAR2_is_empty(TEST_COMB_feat_var_VAR2_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_COMB_feat_var_VAR2_is_clean(TEST_COMB_feat_var_VAR2_t bit_field);

// Bitfield: TEST_COMB_multi_over_VAR1 <uint64_t>
typedef struct TEST_COMB_multi_over_VAR1_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_COMB_multi_over_VAR1_t;

#define TEST_COMB_multi_over_VAR1_default()                                    \
	(TEST_COMB_multi_over_VAR1_t)                                          \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_COMB_multi_over_VAR1_cast(val_0)                                  \
	(TEST_COMB_multi_over_VAR1_t)                                          \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_COMB_multi_over_VAR1_raw(TEST_COMB_multi_over_VAR1_t bit_field);

void
TEST_COMB_multi_over_VAR1_init(TEST_COMB_multi_over_VAR1_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_COMB_multi_over_VAR1_t
TEST_COMB_multi_over_VAR1_clean(TEST_COMB_multi_over_VAR1_t bit_field);

bool
TEST_COMB_multi_over_VAR1_is_equal(TEST_COMB_multi_over_VAR1_t b1,
				   TEST_COMB_multi_over_VAR1_t b2);

bool
TEST_COMB_multi_over_VAR1_is_empty(TEST_COMB_multi_over_VAR1_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_COMB_multi_over_VAR1_is_clean(TEST_COMB_multi_over_VAR1_t bit_field);

// Bitfield: TEST_COMB_multi_over_VAR2 <uint64_t>
typedef struct TEST_COMB_multi_over_VAR2_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_COMB_multi_over_VAR2_t;

#define TEST_COMB_multi_over_VAR2_default()                                    \
	(TEST_COMB_multi_over_VAR2_t)                                          \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_COMB_multi_over_VAR2_cast(val_0)                                  \
	(TEST_COMB_multi_over_VAR2_t)                                          \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_COMB_multi_over_VAR2_raw(TEST_COMB_multi_over_VAR2_t bit_field);

void
TEST_COMB_multi_over_VAR2_init(TEST_COMB_multi_over_VAR2_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_COMB_multi_over_VAR2_t
TEST_COMB_multi_over_VAR2_clean(TEST_COMB_multi_over_VAR2_t bit_field);

bool
TEST_COMB_multi_over_VAR2_is_equal(TEST_COMB_multi_over_VAR2_t b1,
				   TEST_COMB_multi_over_VAR2_t b2);

bool
TEST_COMB_multi_over_VAR2_is_empty(TEST_COMB_multi_over_VAR2_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_COMB_multi_over_VAR2_is_clean(TEST_COMB_multi_over_VAR2_t bit_field);

// Bitfield: TEST_DEFAULT_feat <uint64_t>
typedef struct TEST_DEFAULT_feat_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_DEFAULT_feat_t;

#define TEST_DEFAULT_feat_default()                                            \
	(TEST_DEFAULT_feat_t)                                                  \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_DEFAULT_feat_cast(val_0)                                          \
	(TEST_DEFAULT_feat_t)                                                  \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_DEFAULT_feat_raw(TEST_DEFAULT_feat_t bit_field);

void
TEST_DEFAULT_feat_init(TEST_DEFAULT_feat_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_DEFAULT_feat_t
TEST_DEFAULT_feat_clean(TEST_DEFAULT_feat_t bit_field);

bool
TEST_DEFAULT_feat_is_equal(TEST_DEFAULT_feat_t b1, TEST_DEFAULT_feat_t b2);

bool
TEST_DEFAULT_feat_is_empty(TEST_DEFAULT_feat_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_DEFAULT_feat_is_clean(TEST_DEFAULT_feat_t bit_field);

// Bitfield: TEST_DEFAULT_plain <uint64_t>
typedef struct TEST_DEFAULT_plain_b {
	// 63:0      uint64_t value
	uint64_t bf[1];
} TEST_DEFAULT_plain_t;

#define TEST_DEFAULT_plain_default()                                           \
	(TEST_DEFAULT_plain_t)                                                 \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define TEST_DEFAULT_plain_cast(val_0)                                         \
	(TEST_DEFAULT_plain_t)                                                 \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
TEST_DEFAULT_plain_raw(TEST_DEFAULT_plain_t bit_field);

void
TEST_DEFAULT_plain_init(TEST_DEFAULT_plain_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
TEST_DEFAULT_plain_t
TEST_DEFAULT_plain_clean(TEST_DEFAULT_plain_t bit_field);

bool
TEST_DEFAULT_plain_is_equal(TEST_DEFAULT_plain_t b1, TEST_DEFAULT_plain_t b2);

bool
TEST_DEFAULT_plain_is_empty(TEST_DEFAULT_plain_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
TEST_DEFAULT_plain_is_clean(TEST_DEFAULT_plain_t bit_field);

#define UNARY_MINUS_TEST (int32_t)-4096 // -0x1000

typedef enum color_code_e { RED = 1, BLUE = 2 } color_code_t;

#define COLOR_CODE__MAX BLUE
#define COLOR_CODE__MIN RED

// Bitfield: auto_layout_register <uint64_t>
typedef struct auto_layout_register_b {
	// 4:0       uint8_t auto_field_5bit
	// 6:5       color_code_t auto_field_enum
	// 15:8      uint16_t replacement_field
	// 23:16     uint8_t additional_auto_field
	uint64_t bf[1];
} auto_layout_register_t;

#define auto_layout_register_default()                                         \
	(auto_layout_register_t)                                               \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define auto_layout_register_cast(val_0)                                       \
	(auto_layout_register_t)                                               \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
auto_layout_register_raw(auto_layout_register_t bit_field);

void
auto_layout_register_init(auto_layout_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
auto_layout_register_t
auto_layout_register_clean(auto_layout_register_t bit_field);

bool
auto_layout_register_is_equal(auto_layout_register_t b1,
			      auto_layout_register_t b2);

bool
auto_layout_register_is_empty(auto_layout_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
auto_layout_register_is_clean(auto_layout_register_t bit_field);

typedef uint8_t	 byte_t;
typedef uint32_t count_t;

// Bitfield: peripheral_control_register <uint64_t>
typedef struct peripheral_control_register_b {
	// 7:0       uint16_t peripheral_id
	// 23:16     const uint16_t version
	// 63:24     uint64_t config_data
	uint64_t bf[1];
} peripheral_control_register_t;

#define peripheral_control_register_default()                                  \
	(peripheral_control_register_t)                                        \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define peripheral_control_register_cast(val_0)                                \
	(peripheral_control_register_t)                                        \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
peripheral_control_register_raw(peripheral_control_register_t bit_field);

void
peripheral_control_register_init(peripheral_control_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
peripheral_control_register_t
peripheral_control_register_clean(peripheral_control_register_t bit_field);

bool
peripheral_control_register_is_equal(peripheral_control_register_t b1,
				     peripheral_control_register_t b2);

bool
peripheral_control_register_is_empty(peripheral_control_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
peripheral_control_register_is_clean(peripheral_control_register_t bit_field);

struct device_descriptor_s {
	color_code_t		      color;
	uint8_t			      pad_to_control_[4];
	peripheral_control_register_t control;
};

typedef uint32_t dword_t;

typedef enum error_code_e {
	ERROR_CODE_ERROR_TIMEOUT	   = -3,
	ERROR_CODE_ERROR_RESOURCE_BUSY	   = -2,
	ERROR_CODE_ERROR_NOT_FOUND	   = -1,
	ERROR_CODE_ERROR_OK		   = 0,
	ERROR_CODE_ERROR_RETRY		   = 1,
	ERROR_CODE_ERROR_INVALID_ARG	   = 2,
	ERROR_CODE_ERROR_FATAL		   = 10,
	ERROR_CODE_ERROR_PERMISSION_DENIED = 11,
	ERROR_CODE_ERROR_OUT_OF_MEMORY	   = 12
} error_code_t;

#define ERROR_CODE__MAX ERROR_CODE_ERROR_OUT_OF_MEMORY
#define ERROR_CODE__MIN ERROR_CODE_ERROR_TIMEOUT

typedef uint16_t half_t;
typedef uint32_t index_t;

// Bitfield: initialization_register <uint64_t>
typedef struct initialization_register_b {
	// 7         uint16_t reset_flag
	// 15:8       uint16_t magic_byte_b
	// 23:16     const uint16_t magic_byte_c
	// 47:40     uint16_t extended_config
	uint64_t bf[1];
} initialization_register_t;

#define initialization_register_default()                                      \
	(initialization_register_t)                                            \
	{                                                                      \
		.bf = { 0x5555035555bead80U }                                  \
	}

#define initialization_register_cast(val_0)                                    \
	(initialization_register_t)                                            \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
initialization_register_raw(initialization_register_t bit_field);

void
initialization_register_init(initialization_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
initialization_register_t
initialization_register_clean(initialization_register_t bit_field);

bool
initialization_register_is_equal(initialization_register_t b1,
				 initialization_register_t b2);

bool
initialization_register_is_empty(initialization_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
initialization_register_is_clean(initialization_register_t bit_field);

typedef int8_t (*intarray_t)[8];

// Bitfield: interrupt_control_register <uint64_t>
typedef struct interrupt_control_register_b {
	// 7:0       color_code_t interrupt_id
	// 15:8      int16_t priority_level
	// 23:16     const uint16_t handler_index
	// 35:32     uint16_t interrupt_flags
	uint64_t bf[1];
} interrupt_control_register_t;

#define interrupt_control_register_default()                                   \
	(interrupt_control_register_t)                                         \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define interrupt_control_register_cast(val_0)                                 \
	(interrupt_control_register_t)                                         \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
interrupt_control_register_raw(interrupt_control_register_t bit_field);

void
interrupt_control_register_init(interrupt_control_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
interrupt_control_register_t
interrupt_control_register_clean(interrupt_control_register_t bit_field);

bool
interrupt_control_register_is_equal(interrupt_control_register_t b1,
				    interrupt_control_register_t b2);

bool
interrupt_control_register_is_empty(interrupt_control_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
interrupt_control_register_is_clean(interrupt_control_register_t bit_field);

// Bitfield: memory_config_register <uint64_t>
typedef struct memory_config_register_b {
	// 7:0       uint16_t base_address
	// 23:16     const uint16_t read_only_id
	// 63:24     uint64_t extended_address
	uint64_t bf[1];
} memory_config_register_t;

#define memory_config_register_default()                                       \
	(memory_config_register_t)                                             \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define memory_config_register_cast(val_0)                                     \
	(memory_config_register_t)                                             \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
memory_config_register_raw(memory_config_register_t bit_field);

void
memory_config_register_init(memory_config_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
memory_config_register_t
memory_config_register_clean(memory_config_register_t bit_field);

bool
memory_config_register_is_equal(memory_config_register_t b1,
				memory_config_register_t b2);

bool
memory_config_register_is_empty(memory_config_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
memory_config_register_is_clean(memory_config_register_t bit_field);

// Bitfield: module_config_register <uint64_t>
typedef struct module_config_register_b {
	// 7:0       uint16_t extended_module_id
	// 15:8      uint16_t module_version
	// 20:16     uint8_t feature_flags
	// 52:21     uint32_t capability_bits
	// 60:53     uint8_t additional_capability
	uint64_t bf[1];
} module_config_register_t;

#define module_config_register_default()                                       \
	(module_config_register_t)                                             \
	{                                                                      \
		.bf = { 0x0U }                                                 \
	}

#define module_config_register_cast(val_0)                                     \
	(module_config_register_t)                                             \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
module_config_register_raw(module_config_register_t bit_field);

void
module_config_register_init(module_config_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
module_config_register_t
module_config_register_clean(module_config_register_t bit_field);

bool
module_config_register_is_equal(module_config_register_t b1,
				module_config_register_t b2);

bool
module_config_register_is_empty(module_config_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
module_config_register_is_clean(module_config_register_t bit_field);

typedef enum priority_level_e {
	PRIORITY_LEVEL_LOW    = 0,
	PRIORITY_LEVEL_MEDIUM = 1,
	PRIORITY_LEVEL_HIGH   = 2
} priority_level_t;

#define PRIORITY_LEVEL__MAX PRIORITY_LEVEL_HIGH
#define PRIORITY_LEVEL__MIN PRIORITY_LEVEL_LOW

typedef enum result_code_e {
	RESULT_SUCCESS = 1,
	RESULT_FAILURE = 2
} result_code_t;

#define RESULT_CODE__MAX RESULT_FAILURE
#define RESULT_CODE__MIN RESULT_SUCCESS

struct sizeof_test_s {
	uint32_t test;
};

typedef enum status_flag_e {
	INACTIVE	    = 0,
	STATUS_FLAG_ACTIVE  = 1,
	STATUS_FLAG_PENDING = 2
} status_flag_t;

#define STATUS_FLAG__MAX STATUS_FLAG_PENDING
#define STATUS_FLAG__MIN INACTIVE

// Bitfield: system_control_register <uint64_t>
typedef struct system_control_register_b {
	// 7:0       uint16_t device_id
	// 15:8      int16_t priority
	// 19:16     int16_t status_code
	// 25:20     uint8_t flags
	// 31:26     uint8_t mode
	// 48         bool enable
	// 49        const uint8_t lock_bit
	// 50        int8_t sign_bit
	// 43:40     uint32_t address_offset
	uint64_t bf[1];
} system_control_register_t;

#define system_control_register_default()                                      \
	(system_control_register_t)                                            \
	{                                                                      \
		.bf = { 0xa00000500U }                                         \
	}

#define system_control_register_cast(val_0)                                    \
	(system_control_register_t)                                            \
	{                                                                      \
		.bf = {(val_0) }                                               \
	}

uint64_t
system_control_register_raw(system_control_register_t bit_field);

void
system_control_register_init(system_control_register_t *bit_field);

// Set all unknown/unnamed fields to their expected default values.
// Note, this does NOT clean const named fields to default values.
system_control_register_t
system_control_register_clean(system_control_register_t bit_field);

bool
system_control_register_is_equal(system_control_register_t b1,
				 system_control_register_t b2);

bool
system_control_register_is_empty(system_control_register_t bit_field);

// Check all unknown/unnamed fields have expected default values.
// Note, this does NOT check:
// - whether const named fields have their default values,
// - whether named fields with enumerated types are in-range, or
// - the values of named writeonly fields.
bool
system_control_register_is_clean(system_control_register_t bit_field);

typedef enum temperature_offset_e {
	TEMP_BELOW = -1,
	TEMP_ZERO  = 0,
	TEMP_ABOVE = 1
} temperature_offset_t;

#define TEMPERATURE_OFFSET__MAX TEMP_ABOVE
#define TEMPERATURE_OFFSET__MIN TEMP_BELOW

struct test_struct_s {
	uint64_t test_bitmap_512[8];
	uint64_t test_bitmap_768[12];
	uint64_t test_bitmap_1[1];
};

typedef uintptr_t vaddr_t;
typedef uint32_t  word_t;

#include <hypresult.h>

ALWAYS_INLINE void
UBitField_set_test(UBitField_t *bit_field, uint32_t val);

ALWAYS_INLINE uint32_t
UBitField_get_test(const UBitField_t *bit_field);

ALWAYS_INLINE void
UBitField_copy_test(UBitField_t	      *bit_field_dst,
		    const UBitField_t *bit_field_src);

ALWAYS_INLINE void
UBitField_set_test1(UBitField_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
UBitField_get_test1(const UBitField_t *bit_field);

ALWAYS_INLINE void
UBitField_copy_test1(UBitField_t       *bit_field_dst,
		     const UBitField_t *bit_field_src);

ALWAYS_INLINE void
UBitField_set_test5(UBitField_t *bit_field, uint8_t val);

ALWAYS_INLINE uint8_t
UBitField_get_test5(const UBitField_t *bit_field);

ALWAYS_INLINE void
UBitField_copy_test5(UBitField_t       *bit_field_dst,
		     const UBitField_t *bit_field_src);

ALWAYS_INLINE void
StructBitField_set_test(StructBitField_t *bit_field, uint32_t val);

ALWAYS_INLINE uint32_t
StructBitField_get_test(const StructBitField_t *bit_field);

ALWAYS_INLINE void
StructBitField_copy_test(StructBitField_t	*bit_field_dst,
			 const StructBitField_t *bit_field_src);

ALWAYS_INLINE void
StructBitField_set_test1(StructBitField_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
StructBitField_get_test1(const StructBitField_t *bit_field);

ALWAYS_INLINE void
StructBitField_copy_test1(StructBitField_t	 *bit_field_dst,
			  const StructBitField_t *bit_field_src);

ALWAYS_INLINE void
StructBitField_set_test5(StructBitField_t *bit_field, uint8_t val);

ALWAYS_INLINE uint8_t
StructBitField_get_test5(const StructBitField_t *bit_field);

ALWAYS_INLINE void
StructBitField_copy_test5(StructBitField_t	 *bit_field_dst,
			  const StructBitField_t *bit_field_src);

ALWAYS_INLINE void
TestBitField_set_test(TestBitField_t *bit_field, uint32_t val);

ALWAYS_INLINE uint32_t
TestBitField_get_test(const TestBitField_t *bit_field);

ALWAYS_INLINE void
TestBitField_copy_test(TestBitField_t	    *bit_field_dst,
		       const TestBitField_t *bit_field_src);

ALWAYS_INLINE void
TestBitField_set_test1(TestBitField_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
TestBitField_get_test1(const TestBitField_t *bit_field);

ALWAYS_INLINE void
TestBitField_copy_test1(TestBitField_t	     *bit_field_dst,
			const TestBitField_t *bit_field_src);

ALWAYS_INLINE void
TestBitField_set_test5(TestBitField_t *bit_field, uint8_t val);

ALWAYS_INLINE uint8_t
TestBitField_get_test5(const TestBitField_t *bit_field);

ALWAYS_INLINE void
TestBitField_copy_test5(TestBitField_t	     *bit_field_dst,
			const TestBitField_t *bit_field_src);

ALWAYS_INLINE void
TEST_COMB_feat_var_VAR1_set_value(TEST_COMB_feat_var_VAR1_t *bit_field,
				  uint64_t		     val);

ALWAYS_INLINE uint64_t
TEST_COMB_feat_var_VAR1_get_value(const TEST_COMB_feat_var_VAR1_t *bit_field);

ALWAYS_INLINE void
TEST_COMB_feat_var_VAR1_copy_value(
	TEST_COMB_feat_var_VAR1_t	*bit_field_dst,
	const TEST_COMB_feat_var_VAR1_t *bit_field_src);

ALWAYS_INLINE void
TEST_COMB_feat_var_VAR2_set_value(TEST_COMB_feat_var_VAR2_t *bit_field,
				  uint64_t		     val);

ALWAYS_INLINE uint64_t
TEST_COMB_feat_var_VAR2_get_value(const TEST_COMB_feat_var_VAR2_t *bit_field);

ALWAYS_INLINE void
TEST_COMB_feat_var_VAR2_copy_value(
	TEST_COMB_feat_var_VAR2_t	*bit_field_dst,
	const TEST_COMB_feat_var_VAR2_t *bit_field_src);

ALWAYS_INLINE void
TEST_COMB_multi_over_VAR1_set_value(TEST_COMB_multi_over_VAR1_t *bit_field,
				    uint64_t			 val);

ALWAYS_INLINE uint64_t
TEST_COMB_multi_over_VAR1_get_value(
	const TEST_COMB_multi_over_VAR1_t *bit_field);

ALWAYS_INLINE void
TEST_COMB_multi_over_VAR1_copy_value(
	TEST_COMB_multi_over_VAR1_t	  *bit_field_dst,
	const TEST_COMB_multi_over_VAR1_t *bit_field_src);

ALWAYS_INLINE void
TEST_COMB_multi_over_VAR2_set_value(TEST_COMB_multi_over_VAR2_t *bit_field,
				    uint64_t			 val);

ALWAYS_INLINE uint64_t
TEST_COMB_multi_over_VAR2_get_value(
	const TEST_COMB_multi_over_VAR2_t *bit_field);

ALWAYS_INLINE void
TEST_COMB_multi_over_VAR2_copy_value(
	TEST_COMB_multi_over_VAR2_t	  *bit_field_dst,
	const TEST_COMB_multi_over_VAR2_t *bit_field_src);

ALWAYS_INLINE void
TEST_DEFAULT_feat_set_value(TEST_DEFAULT_feat_t *bit_field, uint64_t val);

ALWAYS_INLINE uint64_t
TEST_DEFAULT_feat_get_value(const TEST_DEFAULT_feat_t *bit_field);

ALWAYS_INLINE void
TEST_DEFAULT_feat_copy_value(TEST_DEFAULT_feat_t       *bit_field_dst,
			     const TEST_DEFAULT_feat_t *bit_field_src);

ALWAYS_INLINE void
TEST_DEFAULT_plain_set_value(TEST_DEFAULT_plain_t *bit_field, uint64_t val);

ALWAYS_INLINE uint64_t
TEST_DEFAULT_plain_get_value(const TEST_DEFAULT_plain_t *bit_field);

ALWAYS_INLINE void
TEST_DEFAULT_plain_copy_value(TEST_DEFAULT_plain_t	 *bit_field_dst,
			      const TEST_DEFAULT_plain_t *bit_field_src);

color_code_t
color_code_raw_cast(uint32_t val);

#define color_code_cast(val) color_code_raw_cast((uint32_t)(val))

color_code_result_t
color_code_raw_cast_safe(uint32_t val);

#define color_code_cast_safe(val) color_code_raw_cast_safe((uint32_t)(val))

bool
color_code_raw_is_valid(uint32_t val);

#define color_code_is_valid(val) color_code_raw_is_valid((uint32_t)(val))

ALWAYS_INLINE void
auto_layout_register_set_auto_field_5bit(auto_layout_register_t *bit_field,
					 uint8_t		 val);

ALWAYS_INLINE uint8_t
auto_layout_register_get_auto_field_5bit(
	const auto_layout_register_t *bit_field);

ALWAYS_INLINE void
auto_layout_register_copy_auto_field_5bit(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src);

ALWAYS_INLINE void
auto_layout_register_set_auto_field_enum(auto_layout_register_t *bit_field,
					 color_code_t		 val);

ALWAYS_INLINE color_code_t
auto_layout_register_get_auto_field_enum(
	const auto_layout_register_t *bit_field);

ALWAYS_INLINE void
auto_layout_register_copy_auto_field_enum(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src);

ALWAYS_INLINE void
auto_layout_register_set_replacement_field(auto_layout_register_t *bit_field,
					   uint16_t		   val);

ALWAYS_INLINE uint16_t
auto_layout_register_get_replacement_field(
	const auto_layout_register_t *bit_field);

ALWAYS_INLINE void
auto_layout_register_copy_replacement_field(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src);

ALWAYS_INLINE void
auto_layout_register_set_additional_auto_field(
	auto_layout_register_t *bit_field, uint8_t val);

ALWAYS_INLINE uint8_t
auto_layout_register_get_additional_auto_field(
	const auto_layout_register_t *bit_field);

ALWAYS_INLINE void
auto_layout_register_copy_additional_auto_field(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src);

ALWAYS_INLINE void
peripheral_control_register_set_peripheral_id(
	peripheral_control_register_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
peripheral_control_register_get_peripheral_id(
	const peripheral_control_register_t *bit_field);

ALWAYS_INLINE void
peripheral_control_register_copy_peripheral_id(
	peripheral_control_register_t	    *bit_field_dst,
	const peripheral_control_register_t *bit_field_src);

ALWAYS_INLINE uint16_t
peripheral_control_register_get_version(
	const peripheral_control_register_t *bit_field);

ALWAYS_INLINE void
peripheral_control_register_set_config_data(
	peripheral_control_register_t *bit_field, uint64_t val);

ALWAYS_INLINE uint64_t
peripheral_control_register_get_config_data(
	const peripheral_control_register_t *bit_field);

ALWAYS_INLINE void
peripheral_control_register_copy_config_data(
	peripheral_control_register_t	    *bit_field_dst,
	const peripheral_control_register_t *bit_field_src);

error_code_t
error_code_raw_cast(int32_t val);

#define error_code_cast(val) error_code_raw_cast((int32_t)(val))

error_code_result_t
error_code_raw_cast_safe(int32_t val);

#define error_code_cast_safe(val) error_code_raw_cast_safe((int32_t)(val))

bool
error_code_raw_is_valid(int32_t val);

#define error_code_is_valid(val) error_code_raw_is_valid((int32_t)(val))

ALWAYS_INLINE void
initialization_register_set_reset_flag(initialization_register_t *bit_field,
				       uint16_t			  val);

ALWAYS_INLINE uint16_t
initialization_register_get_reset_flag(
	const initialization_register_t *bit_field);

ALWAYS_INLINE void
initialization_register_copy_reset_flag(
	initialization_register_t	*bit_field_dst,
	const initialization_register_t *bit_field_src);

ALWAYS_INLINE void
initialization_register_set_magic_byte_b(initialization_register_t *bit_field,
					 uint16_t		    val);

ALWAYS_INLINE uint16_t
initialization_register_get_magic_byte_c(
	const initialization_register_t *bit_field);

ALWAYS_INLINE void
initialization_register_set_extended_config(
	initialization_register_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
initialization_register_get_extended_config(
	const initialization_register_t *bit_field);

ALWAYS_INLINE void
initialization_register_copy_extended_config(
	initialization_register_t	*bit_field_dst,
	const initialization_register_t *bit_field_src);

ALWAYS_INLINE void
interrupt_control_register_set_interrupt_id(
	interrupt_control_register_t *bit_field, color_code_t val);

ALWAYS_INLINE color_code_t
interrupt_control_register_get_interrupt_id(
	const interrupt_control_register_t *bit_field);

ALWAYS_INLINE void
interrupt_control_register_copy_interrupt_id(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src);

ALWAYS_INLINE void
interrupt_control_register_set_priority_level(
	interrupt_control_register_t *bit_field, int16_t val);

ALWAYS_INLINE int16_t
interrupt_control_register_get_priority_level(
	const interrupt_control_register_t *bit_field);

ALWAYS_INLINE void
interrupt_control_register_copy_priority_level(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src);

ALWAYS_INLINE uint16_t
interrupt_control_register_get_handler_index(
	const interrupt_control_register_t *bit_field);

ALWAYS_INLINE void
interrupt_control_register_set_interrupt_flags(
	interrupt_control_register_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
interrupt_control_register_get_interrupt_flags(
	const interrupt_control_register_t *bit_field);

ALWAYS_INLINE void
interrupt_control_register_copy_interrupt_flags(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src);

ALWAYS_INLINE void
memory_config_register_set_base_address(memory_config_register_t *bit_field,
					uint16_t		  val);

ALWAYS_INLINE uint16_t
memory_config_register_get_base_address(
	const memory_config_register_t *bit_field);

ALWAYS_INLINE void
memory_config_register_copy_base_address(
	memory_config_register_t       *bit_field_dst,
	const memory_config_register_t *bit_field_src);

ALWAYS_INLINE uint16_t
memory_config_register_get_read_only_id(
	const memory_config_register_t *bit_field);

ALWAYS_INLINE void
memory_config_register_set_extended_address(memory_config_register_t *bit_field,
					    uint64_t		      val);

ALWAYS_INLINE uint64_t
memory_config_register_get_extended_address(
	const memory_config_register_t *bit_field);

ALWAYS_INLINE void
memory_config_register_copy_extended_address(
	memory_config_register_t       *bit_field_dst,
	const memory_config_register_t *bit_field_src);

ALWAYS_INLINE void
module_config_register_set_extended_module_id(
	module_config_register_t *bit_field, uint16_t val);

ALWAYS_INLINE uint16_t
module_config_register_get_extended_module_id(
	const module_config_register_t *bit_field);

ALWAYS_INLINE void
module_config_register_copy_extended_module_id(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src);

ALWAYS_INLINE void
module_config_register_set_module_version(module_config_register_t *bit_field,
					  uint16_t		    val);

ALWAYS_INLINE uint16_t
module_config_register_get_module_version(
	const module_config_register_t *bit_field);

ALWAYS_INLINE void
module_config_register_copy_module_version(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src);

ALWAYS_INLINE void
module_config_register_set_feature_flags(module_config_register_t *bit_field,
					 uint8_t		   val);

ALWAYS_INLINE uint8_t
module_config_register_get_feature_flags(
	const module_config_register_t *bit_field);

ALWAYS_INLINE void
module_config_register_copy_feature_flags(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src);

ALWAYS_INLINE void
module_config_register_set_capability_bits(module_config_register_t *bit_field,
					   uint32_t		     val);

ALWAYS_INLINE uint32_t
module_config_register_get_capability_bits(
	const module_config_register_t *bit_field);

ALWAYS_INLINE void
module_config_register_copy_capability_bits(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src);

ALWAYS_INLINE void
module_config_register_set_additional_capability(
	module_config_register_t *bit_field, uint8_t val);

ALWAYS_INLINE uint8_t
module_config_register_get_additional_capability(
	const module_config_register_t *bit_field);

ALWAYS_INLINE void
module_config_register_copy_additional_capability(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src);

priority_level_t
priority_level_raw_cast(uint32_t val);

#define priority_level_cast(val) priority_level_raw_cast((uint32_t)(val))

priority_level_result_t
priority_level_raw_cast_safe(uint32_t val);

#define priority_level_cast_safe(val)                                          \
	priority_level_raw_cast_safe((uint32_t)(val))

bool
priority_level_raw_is_valid(uint32_t val);

#define priority_level_is_valid(val)                                           \
	priority_level_raw_is_valid((uint32_t)(val))

result_code_t
result_code_raw_cast(uint32_t val);

#define result_code_cast(val) result_code_raw_cast((uint32_t)(val))

result_code_result_t
result_code_raw_cast_safe(uint32_t val);

#define result_code_cast_safe(val) result_code_raw_cast_safe((uint32_t)(val))

bool
result_code_raw_is_valid(uint32_t val);

#define result_code_is_valid(val) result_code_raw_is_valid((uint32_t)(val))

status_flag_t
status_flag_raw_cast(uint32_t val);

#define status_flag_cast(val) status_flag_raw_cast((uint32_t)(val))

status_flag_result_t
status_flag_raw_cast_safe(uint32_t val);

#define status_flag_cast_safe(val) status_flag_raw_cast_safe((uint32_t)(val))

bool
status_flag_raw_is_valid(uint32_t val);

#define status_flag_is_valid(val) status_flag_raw_is_valid((uint32_t)(val))

ALWAYS_INLINE void
system_control_register_set_device_id(system_control_register_t *bit_field,
				      uint16_t			 val);

ALWAYS_INLINE uint16_t
system_control_register_get_device_id(
	const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_device_id(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_priority(system_control_register_t *bit_field,
				     int16_t			val);

ALWAYS_INLINE int16_t
system_control_register_get_priority(const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_priority(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_status_code(system_control_register_t *bit_field,
					int16_t			   val);

ALWAYS_INLINE int16_t
system_control_register_get_status_code(
	const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_status_code(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_flags(system_control_register_t *bit_field,
				  uint8_t		     val);

ALWAYS_INLINE uint8_t
system_control_register_get_flags(const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_flags(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_mode(system_control_register_t *bit_field,
				 uint8_t		    val);

ALWAYS_INLINE uint8_t
system_control_register_get_mode(const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_mode(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_address_offset(system_control_register_t *bit_field,
					   uint32_t		      val);

ALWAYS_INLINE uint32_t
system_control_register_get_address_offset(
	const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_address_offset(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

ALWAYS_INLINE void
system_control_register_set_enable(system_control_register_t *bit_field,
				   bool			      val);

ALWAYS_INLINE uint8_t
system_control_register_get_lock_bit(const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_set_sign_bit(system_control_register_t *bit_field,
				     int8_t			val);

ALWAYS_INLINE int8_t
system_control_register_get_sign_bit(const system_control_register_t *bit_field);

ALWAYS_INLINE void
system_control_register_copy_sign_bit(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src);

temperature_offset_t
temperature_offset_raw_cast(int32_t val);

#define temperature_offset_cast(val) temperature_offset_raw_cast((int32_t)(val))

temperature_offset_result_t
temperature_offset_raw_cast_safe(int32_t val);

#define temperature_offset_cast_safe(val)                                      \
	temperature_offset_raw_cast_safe((int32_t)(val))

bool
temperature_offset_raw_is_valid(int32_t val);

#define temperature_offset_is_valid(val)                                       \
	temperature_offset_raw_is_valid((int32_t)(val))
#else
#error multiple include HYPTYPES_H_
#endif
