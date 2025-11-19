// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef HYPRESULT_H_
#define HYPRESULT_H_
// _result_t type definitions and accessors

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"

typedef struct Back_ptr_result {
	Back_t			   *r;
	error_t alignas(register_t) e;
} Back_ptr_result_t;

ALWAYS_INLINE Back_ptr_result_t
Back_ptr_result_error(error_t err);
ALWAYS_INLINE Back_ptr_result_t
Back_ptr_result_ok(Back_t *ret);

typedef struct Default_result {
	Default_t		    r;
	error_t alignas(register_t) e;
} Default_result_t;

ALWAYS_INLINE Default_result_t
Default_result_error(error_t err);
ALWAYS_INLINE Default_result_t
Default_result_ok(Default_t ret);

typedef struct Default_ptr_result {
	Default_t		   *r;
	error_t alignas(register_t) e;
} Default_ptr_result_t;

ALWAYS_INLINE Default_ptr_result_t
Default_ptr_result_error(error_t err);
ALWAYS_INLINE Default_ptr_result_t
Default_ptr_result_ok(Default_t *ret);

typedef struct Demo2U_ptr_result {
	Demo2U_t		   *r;
	error_t alignas(register_t) e;
} Demo2U_ptr_result_t;

ALWAYS_INLINE Demo2U_ptr_result_t
Demo2U_ptr_result_error(error_t err);
ALWAYS_INLINE Demo2U_ptr_result_t
Demo2U_ptr_result_ok(Demo2U_t *ret);

typedef struct DemoContainer_result {
	DemoContainer_t		    r;
	error_t alignas(register_t) e;
} DemoContainer_result_t;

ALWAYS_INLINE DemoContainer_result_t
DemoContainer_result_error(error_t err);
ALWAYS_INLINE DemoContainer_result_t
DemoContainer_result_ok(DemoContainer_t ret);

typedef struct DemoContainer_ptr_result {
	DemoContainer_t		   *r;
	error_t alignas(register_t) e;
} DemoContainer_ptr_result_t;

ALWAYS_INLINE DemoContainer_ptr_result_t
DemoContainer_ptr_result_error(error_t err);
ALWAYS_INLINE DemoContainer_ptr_result_t
DemoContainer_ptr_result_ok(DemoContainer_t *ret);

typedef struct DemoFixed_result {
	DemoFixed_t		    r;
	error_t alignas(register_t) e;
} DemoFixed_result_t;

ALWAYS_INLINE DemoFixed_result_t
DemoFixed_result_error(error_t err);
ALWAYS_INLINE DemoFixed_result_t
DemoFixed_result_ok(DemoFixed_t ret);

typedef struct DemoFixed_ptr_result {
	DemoFixed_t		   *r;
	error_t alignas(register_t) e;
} DemoFixed_ptr_result_t;

ALWAYS_INLINE DemoFixed_ptr_result_t
DemoFixed_ptr_result_error(error_t err);
ALWAYS_INLINE DemoFixed_ptr_result_t
DemoFixed_ptr_result_ok(DemoFixed_t *ret);

typedef struct DemoPacked_result {
	DemoPacked_t		    r;
	error_t alignas(register_t) e;
} DemoPacked_result_t;

ALWAYS_INLINE DemoPacked_result_t
DemoPacked_result_error(error_t err);
ALWAYS_INLINE DemoPacked_result_t
DemoPacked_result_ok(DemoPacked_t ret);

typedef struct DemoPacked_ptr_result {
	DemoPacked_t		   *r;
	error_t alignas(register_t) e;
} DemoPacked_ptr_result_t;

ALWAYS_INLINE DemoPacked_ptr_result_t
DemoPacked_ptr_result_error(error_t err);
ALWAYS_INLINE DemoPacked_ptr_result_t
DemoPacked_ptr_result_ok(DemoPacked_t *ret);

typedef struct DemoPackedInObject_result {
	DemoPackedInObject_t	    r;
	error_t alignas(register_t) e;
} DemoPackedInObject_result_t;

ALWAYS_INLINE DemoPackedInObject_result_t
DemoPackedInObject_result_error(error_t err);
ALWAYS_INLINE DemoPackedInObject_result_t
DemoPackedInObject_result_ok(DemoPackedInObject_t ret);

typedef struct DemoPackedInObject_ptr_result {
	DemoPackedInObject_t	   *r;
	error_t alignas(register_t) e;
} DemoPackedInObject_ptr_result_t;

ALWAYS_INLINE DemoPackedInObject_ptr_result_t
DemoPackedInObject_ptr_result_error(error_t err);
ALWAYS_INLINE DemoPackedInObject_ptr_result_t
DemoPackedInObject_ptr_result_ok(DemoPackedInObject_t *ret);

typedef struct DemoSingleField_result {
	DemoSingleField_t	    r;
	error_t alignas(register_t) e;
} DemoSingleField_result_t;

ALWAYS_INLINE DemoSingleField_result_t
DemoSingleField_result_error(error_t err);
ALWAYS_INLINE DemoSingleField_result_t
DemoSingleField_result_ok(DemoSingleField_t ret);

typedef struct DemoSingleField_ptr_result {
	DemoSingleField_t	   *r;
	error_t alignas(register_t) e;
} DemoSingleField_ptr_result_t;

ALWAYS_INLINE DemoSingleField_ptr_result_t
DemoSingleField_ptr_result_error(error_t err);
ALWAYS_INLINE DemoSingleField_ptr_result_t
DemoSingleField_ptr_result_ok(DemoSingleField_t *ret);

typedef struct DemoUStruct_result {
	DemoUStruct_t		    r;
	error_t alignas(register_t) e;
} DemoUStruct_result_t;

ALWAYS_INLINE DemoUStruct_result_t
DemoUStruct_result_error(error_t err);
ALWAYS_INLINE DemoUStruct_result_t
DemoUStruct_result_ok(DemoUStruct_t ret);

typedef struct DemoUStruct_ptr_result {
	DemoUStruct_t		   *r;
	error_t alignas(register_t) e;
} DemoUStruct_ptr_result_t;

ALWAYS_INLINE DemoUStruct_ptr_result_t
DemoUStruct_ptr_result_error(error_t err);
ALWAYS_INLINE DemoUStruct_ptr_result_t
DemoUStruct_ptr_result_ok(DemoUStruct_t *ret);

typedef struct ExecContext_ptr_result {
	ExecContext_t		   *r;
	error_t alignas(register_t) e;
} ExecContext_ptr_result_t;

ALWAYS_INLINE ExecContext_ptr_result_t
ExecContext_ptr_result_error(error_t err);
ALWAYS_INLINE ExecContext_ptr_result_t
ExecContext_ptr_result_ok(ExecContext_t *ret);

typedef struct NestUI_ptr_result {
	NestUI_t		   *r;
	error_t alignas(register_t) e;
} NestUI_ptr_result_t;

ALWAYS_INLINE NestUI_ptr_result_t
NestUI_ptr_result_error(error_t err);
ALWAYS_INLINE NestUI_ptr_result_t
NestUI_ptr_result_ok(NestUI_t *ret);

typedef struct Standalone_ptr_result {
	Standalone_t		   *r;
	error_t alignas(register_t) e;
} Standalone_ptr_result_t;

ALWAYS_INLINE Standalone_ptr_result_t
Standalone_ptr_result_error(error_t err);
ALWAYS_INLINE Standalone_ptr_result_t
Standalone_ptr_result_ok(Standalone_t *ret);

typedef struct StructBitField_result {
	StructBitField_t	    r;
	error_t alignas(register_t) e;
} StructBitField_result_t;

ALWAYS_INLINE StructBitField_result_t
StructBitField_result_error(error_t err);
ALWAYS_INLINE StructBitField_result_t
StructBitField_result_ok(StructBitField_t ret);

typedef struct StructBitField_ptr_result {
	StructBitField_t	   *r;
	error_t alignas(register_t) e;
} StructBitField_ptr_result_t;

ALWAYS_INLINE StructBitField_ptr_result_t
StructBitField_ptr_result_error(error_t err);
ALWAYS_INLINE StructBitField_ptr_result_t
StructBitField_ptr_result_ok(StructBitField_t *ret);

typedef struct TEST_COMB_feat_var_VAR1_result {
	TEST_COMB_feat_var_VAR1_t   r;
	error_t alignas(register_t) e;
} TEST_COMB_feat_var_VAR1_result_t;

ALWAYS_INLINE TEST_COMB_feat_var_VAR1_result_t
TEST_COMB_feat_var_VAR1_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_feat_var_VAR1_result_t
TEST_COMB_feat_var_VAR1_result_ok(TEST_COMB_feat_var_VAR1_t ret);

typedef struct TEST_COMB_feat_var_VAR1_ptr_result {
	TEST_COMB_feat_var_VAR1_t  *r;
	error_t alignas(register_t) e;
} TEST_COMB_feat_var_VAR1_ptr_result_t;

ALWAYS_INLINE TEST_COMB_feat_var_VAR1_ptr_result_t
TEST_COMB_feat_var_VAR1_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_feat_var_VAR1_ptr_result_t
TEST_COMB_feat_var_VAR1_ptr_result_ok(TEST_COMB_feat_var_VAR1_t *ret);

typedef struct TEST_COMB_feat_var_VAR2_result {
	TEST_COMB_feat_var_VAR2_t   r;
	error_t alignas(register_t) e;
} TEST_COMB_feat_var_VAR2_result_t;

ALWAYS_INLINE TEST_COMB_feat_var_VAR2_result_t
TEST_COMB_feat_var_VAR2_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_feat_var_VAR2_result_t
TEST_COMB_feat_var_VAR2_result_ok(TEST_COMB_feat_var_VAR2_t ret);

typedef struct TEST_COMB_feat_var_VAR2_ptr_result {
	TEST_COMB_feat_var_VAR2_t  *r;
	error_t alignas(register_t) e;
} TEST_COMB_feat_var_VAR2_ptr_result_t;

ALWAYS_INLINE TEST_COMB_feat_var_VAR2_ptr_result_t
TEST_COMB_feat_var_VAR2_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_feat_var_VAR2_ptr_result_t
TEST_COMB_feat_var_VAR2_ptr_result_ok(TEST_COMB_feat_var_VAR2_t *ret);

typedef struct TEST_COMB_multi_over_VAR1_result {
	TEST_COMB_multi_over_VAR1_t r;
	error_t alignas(register_t) e;
} TEST_COMB_multi_over_VAR1_result_t;

ALWAYS_INLINE TEST_COMB_multi_over_VAR1_result_t
TEST_COMB_multi_over_VAR1_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_multi_over_VAR1_result_t
TEST_COMB_multi_over_VAR1_result_ok(TEST_COMB_multi_over_VAR1_t ret);

typedef struct TEST_COMB_multi_over_VAR1_ptr_result {
	TEST_COMB_multi_over_VAR1_t *r;
	error_t alignas(register_t)  e;
} TEST_COMB_multi_over_VAR1_ptr_result_t;

ALWAYS_INLINE TEST_COMB_multi_over_VAR1_ptr_result_t
TEST_COMB_multi_over_VAR1_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_multi_over_VAR1_ptr_result_t
TEST_COMB_multi_over_VAR1_ptr_result_ok(TEST_COMB_multi_over_VAR1_t *ret);

typedef struct TEST_COMB_multi_over_VAR2_result {
	TEST_COMB_multi_over_VAR2_t r;
	error_t alignas(register_t) e;
} TEST_COMB_multi_over_VAR2_result_t;

ALWAYS_INLINE TEST_COMB_multi_over_VAR2_result_t
TEST_COMB_multi_over_VAR2_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_multi_over_VAR2_result_t
TEST_COMB_multi_over_VAR2_result_ok(TEST_COMB_multi_over_VAR2_t ret);

typedef struct TEST_COMB_multi_over_VAR2_ptr_result {
	TEST_COMB_multi_over_VAR2_t *r;
	error_t alignas(register_t)  e;
} TEST_COMB_multi_over_VAR2_ptr_result_t;

ALWAYS_INLINE TEST_COMB_multi_over_VAR2_ptr_result_t
TEST_COMB_multi_over_VAR2_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_COMB_multi_over_VAR2_ptr_result_t
TEST_COMB_multi_over_VAR2_ptr_result_ok(TEST_COMB_multi_over_VAR2_t *ret);

typedef struct TEST_DEFAULT_feat_result {
	TEST_DEFAULT_feat_t	    r;
	error_t alignas(register_t) e;
} TEST_DEFAULT_feat_result_t;

ALWAYS_INLINE TEST_DEFAULT_feat_result_t
TEST_DEFAULT_feat_result_error(error_t err);
ALWAYS_INLINE TEST_DEFAULT_feat_result_t
TEST_DEFAULT_feat_result_ok(TEST_DEFAULT_feat_t ret);

typedef struct TEST_DEFAULT_feat_ptr_result {
	TEST_DEFAULT_feat_t	   *r;
	error_t alignas(register_t) e;
} TEST_DEFAULT_feat_ptr_result_t;

ALWAYS_INLINE TEST_DEFAULT_feat_ptr_result_t
TEST_DEFAULT_feat_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_DEFAULT_feat_ptr_result_t
TEST_DEFAULT_feat_ptr_result_ok(TEST_DEFAULT_feat_t *ret);

typedef struct TEST_DEFAULT_plain_result {
	TEST_DEFAULT_plain_t	    r;
	error_t alignas(register_t) e;
} TEST_DEFAULT_plain_result_t;

ALWAYS_INLINE TEST_DEFAULT_plain_result_t
TEST_DEFAULT_plain_result_error(error_t err);
ALWAYS_INLINE TEST_DEFAULT_plain_result_t
TEST_DEFAULT_plain_result_ok(TEST_DEFAULT_plain_t ret);

typedef struct TEST_DEFAULT_plain_ptr_result {
	TEST_DEFAULT_plain_t	   *r;
	error_t alignas(register_t) e;
} TEST_DEFAULT_plain_ptr_result_t;

ALWAYS_INLINE TEST_DEFAULT_plain_ptr_result_t
TEST_DEFAULT_plain_ptr_result_error(error_t err);
ALWAYS_INLINE TEST_DEFAULT_plain_ptr_result_t
TEST_DEFAULT_plain_ptr_result_ok(TEST_DEFAULT_plain_t *ret);

typedef struct TestBitField_result {
	TestBitField_t		    r;
	error_t alignas(register_t) e;
} TestBitField_result_t;

ALWAYS_INLINE TestBitField_result_t
TestBitField_result_error(error_t err);
ALWAYS_INLINE TestBitField_result_t
TestBitField_result_ok(TestBitField_t ret);

typedef struct TestBitField_ptr_result {
	TestBitField_t		   *r;
	error_t alignas(register_t) e;
} TestBitField_ptr_result_t;

ALWAYS_INLINE TestBitField_ptr_result_t
TestBitField_ptr_result_error(error_t err);
ALWAYS_INLINE TestBitField_ptr_result_t
TestBitField_ptr_result_ok(TestBitField_t *ret);

typedef struct UBitField_result {
	UBitField_t		    r;
	error_t alignas(register_t) e;
} UBitField_result_t;

ALWAYS_INLINE UBitField_result_t
UBitField_result_error(error_t err);
ALWAYS_INLINE UBitField_result_t
UBitField_result_ok(UBitField_t ret);

typedef struct UBitField_ptr_result {
	UBitField_t		   *r;
	error_t alignas(register_t) e;
} UBitField_ptr_result_t;

ALWAYS_INLINE UBitField_ptr_result_t
UBitField_ptr_result_error(error_t err);
ALWAYS_INLINE UBitField_ptr_result_t
UBitField_ptr_result_ok(UBitField_t *ret);

typedef struct auto_layout_register_result {
	auto_layout_register_t	    r;
	error_t alignas(register_t) e;
} auto_layout_register_result_t;

ALWAYS_INLINE auto_layout_register_result_t
auto_layout_register_result_error(error_t err);
ALWAYS_INLINE auto_layout_register_result_t
auto_layout_register_result_ok(auto_layout_register_t ret);

typedef struct auto_layout_register_ptr_result {
	auto_layout_register_t	   *r;
	error_t alignas(register_t) e;
} auto_layout_register_ptr_result_t;

ALWAYS_INLINE auto_layout_register_ptr_result_t
auto_layout_register_ptr_result_error(error_t err);
ALWAYS_INLINE auto_layout_register_ptr_result_t
auto_layout_register_ptr_result_ok(auto_layout_register_t *ret);

typedef struct byte_result {
	byte_t			    r;
	error_t alignas(register_t) e;
} byte_result_t;

ALWAYS_INLINE byte_result_t
byte_result_error(error_t err);
ALWAYS_INLINE byte_result_t
byte_result_ok(byte_t ret);

typedef struct byte_ptr_result {
	byte_t			   *r;
	error_t alignas(register_t) e;
} byte_ptr_result_t;

ALWAYS_INLINE byte_ptr_result_t
byte_ptr_result_error(error_t err);
ALWAYS_INLINE byte_ptr_result_t
byte_ptr_result_ok(byte_t *ret);

typedef struct color_code_result {
	color_code_t		    r;
	error_t alignas(register_t) e;
} color_code_result_t;

ALWAYS_INLINE color_code_result_t
color_code_result_error(error_t err);
ALWAYS_INLINE color_code_result_t
color_code_result_ok(color_code_t ret);

typedef struct color_code_ptr_result {
	color_code_t		   *r;
	error_t alignas(register_t) e;
} color_code_ptr_result_t;

ALWAYS_INLINE color_code_ptr_result_t
color_code_ptr_result_error(error_t err);
ALWAYS_INLINE color_code_ptr_result_t
color_code_ptr_result_ok(color_code_t *ret);

typedef struct count_result {
	count_t			    r;
	error_t alignas(register_t) e;
} count_result_t;

ALWAYS_INLINE count_result_t
count_result_error(error_t err);
ALWAYS_INLINE count_result_t
count_result_ok(count_t ret);

typedef struct count_ptr_result {
	count_t			   *r;
	error_t alignas(register_t) e;
} count_ptr_result_t;

ALWAYS_INLINE count_ptr_result_t
count_ptr_result_error(error_t err);
ALWAYS_INLINE count_ptr_result_t
count_ptr_result_ok(count_t *ret);

typedef struct device_descriptor_result {
	device_descriptor_t	    r;
	error_t alignas(register_t) e;
} device_descriptor_result_t;

ALWAYS_INLINE device_descriptor_result_t
device_descriptor_result_error(error_t err);
ALWAYS_INLINE device_descriptor_result_t
device_descriptor_result_ok(device_descriptor_t ret);

typedef struct device_descriptor_ptr_result {
	device_descriptor_t	   *r;
	error_t alignas(register_t) e;
} device_descriptor_ptr_result_t;

ALWAYS_INLINE device_descriptor_ptr_result_t
device_descriptor_ptr_result_error(error_t err);
ALWAYS_INLINE device_descriptor_ptr_result_t
device_descriptor_ptr_result_ok(device_descriptor_t *ret);

typedef struct dword_result {
	dword_t			    r;
	error_t alignas(register_t) e;
} dword_result_t;

ALWAYS_INLINE dword_result_t
dword_result_error(error_t err);
ALWAYS_INLINE dword_result_t
dword_result_ok(dword_t ret);

typedef struct dword_ptr_result {
	dword_t			   *r;
	error_t alignas(register_t) e;
} dword_ptr_result_t;

ALWAYS_INLINE dword_ptr_result_t
dword_ptr_result_error(error_t err);
ALWAYS_INLINE dword_ptr_result_t
dword_ptr_result_ok(dword_t *ret);

typedef struct error_code_result {
	error_code_t		    r;
	error_t alignas(register_t) e;
} error_code_result_t;

ALWAYS_INLINE error_code_result_t
error_code_result_error(error_t err);
ALWAYS_INLINE error_code_result_t
error_code_result_ok(error_code_t ret);

typedef struct error_code_ptr_result {
	error_code_t		   *r;
	error_t alignas(register_t) e;
} error_code_ptr_result_t;

ALWAYS_INLINE error_code_ptr_result_t
error_code_ptr_result_error(error_t err);
ALWAYS_INLINE error_code_ptr_result_t
error_code_ptr_result_ok(error_code_t *ret);

typedef struct half_result {
	half_t			    r;
	error_t alignas(register_t) e;
} half_result_t;

ALWAYS_INLINE half_result_t
half_result_error(error_t err);
ALWAYS_INLINE half_result_t
half_result_ok(half_t ret);

typedef struct half_ptr_result {
	half_t			   *r;
	error_t alignas(register_t) e;
} half_ptr_result_t;

ALWAYS_INLINE half_ptr_result_t
half_ptr_result_error(error_t err);
ALWAYS_INLINE half_ptr_result_t
half_ptr_result_ok(half_t *ret);

typedef struct index_result {
	index_t			    r;
	error_t alignas(register_t) e;
} index_result_t;

ALWAYS_INLINE index_result_t
index_result_error(error_t err);
ALWAYS_INLINE index_result_t
index_result_ok(index_t ret);

typedef struct index_ptr_result {
	index_t			   *r;
	error_t alignas(register_t) e;
} index_ptr_result_t;

ALWAYS_INLINE index_ptr_result_t
index_ptr_result_error(error_t err);
ALWAYS_INLINE index_ptr_result_t
index_ptr_result_ok(index_t *ret);

typedef struct initialization_register_result {
	initialization_register_t   r;
	error_t alignas(register_t) e;
} initialization_register_result_t;

ALWAYS_INLINE initialization_register_result_t
initialization_register_result_error(error_t err);
ALWAYS_INLINE initialization_register_result_t
initialization_register_result_ok(initialization_register_t ret);

typedef struct initialization_register_ptr_result {
	initialization_register_t  *r;
	error_t alignas(register_t) e;
} initialization_register_ptr_result_t;

ALWAYS_INLINE initialization_register_ptr_result_t
initialization_register_ptr_result_error(error_t err);
ALWAYS_INLINE initialization_register_ptr_result_t
initialization_register_ptr_result_ok(initialization_register_t *ret);

typedef struct interrupt_control_register_result {
	interrupt_control_register_t r;
	error_t alignas(register_t)  e;
} interrupt_control_register_result_t;

ALWAYS_INLINE interrupt_control_register_result_t
interrupt_control_register_result_error(error_t err);
ALWAYS_INLINE interrupt_control_register_result_t
interrupt_control_register_result_ok(interrupt_control_register_t ret);

typedef struct interrupt_control_register_ptr_result {
	interrupt_control_register_t *r;
	error_t alignas(register_t)   e;
} interrupt_control_register_ptr_result_t;

ALWAYS_INLINE interrupt_control_register_ptr_result_t
interrupt_control_register_ptr_result_error(error_t err);
ALWAYS_INLINE interrupt_control_register_ptr_result_t
interrupt_control_register_ptr_result_ok(interrupt_control_register_t *ret);

typedef struct memory_config_register_result {
	memory_config_register_t    r;
	error_t alignas(register_t) e;
} memory_config_register_result_t;

ALWAYS_INLINE memory_config_register_result_t
memory_config_register_result_error(error_t err);
ALWAYS_INLINE memory_config_register_result_t
memory_config_register_result_ok(memory_config_register_t ret);

typedef struct memory_config_register_ptr_result {
	memory_config_register_t   *r;
	error_t alignas(register_t) e;
} memory_config_register_ptr_result_t;

ALWAYS_INLINE memory_config_register_ptr_result_t
memory_config_register_ptr_result_error(error_t err);
ALWAYS_INLINE memory_config_register_ptr_result_t
memory_config_register_ptr_result_ok(memory_config_register_t *ret);

typedef struct module_config_register_result {
	module_config_register_t    r;
	error_t alignas(register_t) e;
} module_config_register_result_t;

ALWAYS_INLINE module_config_register_result_t
module_config_register_result_error(error_t err);
ALWAYS_INLINE module_config_register_result_t
module_config_register_result_ok(module_config_register_t ret);

typedef struct module_config_register_ptr_result {
	module_config_register_t   *r;
	error_t alignas(register_t) e;
} module_config_register_ptr_result_t;

ALWAYS_INLINE module_config_register_ptr_result_t
module_config_register_ptr_result_error(error_t err);
ALWAYS_INLINE module_config_register_ptr_result_t
module_config_register_ptr_result_ok(module_config_register_t *ret);

typedef struct peripheral_control_register_result {
	peripheral_control_register_t r;
	error_t alignas(register_t)   e;
} peripheral_control_register_result_t;

ALWAYS_INLINE peripheral_control_register_result_t
peripheral_control_register_result_error(error_t err);
ALWAYS_INLINE peripheral_control_register_result_t
peripheral_control_register_result_ok(peripheral_control_register_t ret);

typedef struct peripheral_control_register_ptr_result {
	peripheral_control_register_t *r;
	error_t alignas(register_t)    e;
} peripheral_control_register_ptr_result_t;

ALWAYS_INLINE peripheral_control_register_ptr_result_t
peripheral_control_register_ptr_result_error(error_t err);
ALWAYS_INLINE peripheral_control_register_ptr_result_t
peripheral_control_register_ptr_result_ok(peripheral_control_register_t *ret);

typedef struct priority_level_result {
	priority_level_t	    r;
	error_t alignas(register_t) e;
} priority_level_result_t;

ALWAYS_INLINE priority_level_result_t
priority_level_result_error(error_t err);
ALWAYS_INLINE priority_level_result_t
priority_level_result_ok(priority_level_t ret);

typedef struct priority_level_ptr_result {
	priority_level_t	   *r;
	error_t alignas(register_t) e;
} priority_level_ptr_result_t;

ALWAYS_INLINE priority_level_ptr_result_t
priority_level_ptr_result_error(error_t err);
ALWAYS_INLINE priority_level_ptr_result_t
priority_level_ptr_result_ok(priority_level_t *ret);

typedef struct result_code_result {
	result_code_t		    r;
	error_t alignas(register_t) e;
} result_code_result_t;

ALWAYS_INLINE result_code_result_t
result_code_result_error(error_t err);
ALWAYS_INLINE result_code_result_t
result_code_result_ok(result_code_t ret);

typedef struct result_code_ptr_result {
	result_code_t		   *r;
	error_t alignas(register_t) e;
} result_code_ptr_result_t;

ALWAYS_INLINE result_code_ptr_result_t
result_code_ptr_result_error(error_t err);
ALWAYS_INLINE result_code_ptr_result_t
result_code_ptr_result_ok(result_code_t *ret);

typedef struct sizeof_test_result {
	sizeof_test_t		    r;
	error_t alignas(register_t) e;
} sizeof_test_result_t;

ALWAYS_INLINE sizeof_test_result_t
sizeof_test_result_error(error_t err);
ALWAYS_INLINE sizeof_test_result_t
sizeof_test_result_ok(sizeof_test_t ret);

typedef struct sizeof_test_ptr_result {
	sizeof_test_t		   *r;
	error_t alignas(register_t) e;
} sizeof_test_ptr_result_t;

ALWAYS_INLINE sizeof_test_ptr_result_t
sizeof_test_ptr_result_error(error_t err);
ALWAYS_INLINE sizeof_test_ptr_result_t
sizeof_test_ptr_result_ok(sizeof_test_t *ret);

typedef struct status_flag_result {
	status_flag_t		    r;
	error_t alignas(register_t) e;
} status_flag_result_t;

ALWAYS_INLINE status_flag_result_t
status_flag_result_error(error_t err);
ALWAYS_INLINE status_flag_result_t
status_flag_result_ok(status_flag_t ret);

typedef struct status_flag_ptr_result {
	status_flag_t		   *r;
	error_t alignas(register_t) e;
} status_flag_ptr_result_t;

ALWAYS_INLINE status_flag_ptr_result_t
status_flag_ptr_result_error(error_t err);
ALWAYS_INLINE status_flag_ptr_result_t
status_flag_ptr_result_ok(status_flag_t *ret);

typedef struct system_control_register_result {
	system_control_register_t   r;
	error_t alignas(register_t) e;
} system_control_register_result_t;

ALWAYS_INLINE system_control_register_result_t
system_control_register_result_error(error_t err);
ALWAYS_INLINE system_control_register_result_t
system_control_register_result_ok(system_control_register_t ret);

typedef struct system_control_register_ptr_result {
	system_control_register_t  *r;
	error_t alignas(register_t) e;
} system_control_register_ptr_result_t;

ALWAYS_INLINE system_control_register_ptr_result_t
system_control_register_ptr_result_error(error_t err);
ALWAYS_INLINE system_control_register_ptr_result_t
system_control_register_ptr_result_ok(system_control_register_t *ret);

typedef struct temperature_offset_result {
	temperature_offset_t	    r;
	error_t alignas(register_t) e;
} temperature_offset_result_t;

ALWAYS_INLINE temperature_offset_result_t
temperature_offset_result_error(error_t err);
ALWAYS_INLINE temperature_offset_result_t
temperature_offset_result_ok(temperature_offset_t ret);

typedef struct temperature_offset_ptr_result {
	temperature_offset_t	   *r;
	error_t alignas(register_t) e;
} temperature_offset_ptr_result_t;

ALWAYS_INLINE temperature_offset_ptr_result_t
temperature_offset_ptr_result_error(error_t err);
ALWAYS_INLINE temperature_offset_ptr_result_t
temperature_offset_ptr_result_ok(temperature_offset_t *ret);

typedef struct test_struct_result {
	test_struct_t		    r;
	error_t alignas(register_t) e;
} test_struct_result_t;

ALWAYS_INLINE test_struct_result_t
test_struct_result_error(error_t err);
ALWAYS_INLINE test_struct_result_t
test_struct_result_ok(test_struct_t ret);

typedef struct test_struct_ptr_result {
	test_struct_t		   *r;
	error_t alignas(register_t) e;
} test_struct_ptr_result_t;

ALWAYS_INLINE test_struct_ptr_result_t
test_struct_ptr_result_error(error_t err);
ALWAYS_INLINE test_struct_ptr_result_t
test_struct_ptr_result_ok(test_struct_t *ret);

typedef struct vaddr_result {
	vaddr_t			    r;
	error_t alignas(register_t) e;
} vaddr_result_t;

ALWAYS_INLINE vaddr_result_t
vaddr_result_error(error_t err);
ALWAYS_INLINE vaddr_result_t
vaddr_result_ok(vaddr_t ret);

typedef struct vaddr_ptr_result {
	vaddr_t			   *r;
	error_t alignas(register_t) e;
} vaddr_ptr_result_t;

ALWAYS_INLINE vaddr_ptr_result_t
vaddr_ptr_result_error(error_t err);
ALWAYS_INLINE vaddr_ptr_result_t
vaddr_ptr_result_ok(vaddr_t *ret);

typedef struct word_result {
	word_t			    r;
	error_t alignas(register_t) e;
} word_result_t;

ALWAYS_INLINE word_result_t
word_result_error(error_t err);
ALWAYS_INLINE word_result_t
word_result_ok(word_t ret);

typedef struct word_ptr_result {
	word_t			   *r;
	error_t alignas(register_t) e;
} word_ptr_result_t;

ALWAYS_INLINE word_ptr_result_t
word_ptr_result_error(error_t err);
ALWAYS_INLINE word_ptr_result_t
word_ptr_result_ok(word_t *ret);

typedef struct bool_result {
	bool			    r;
	error_t alignas(register_t) e;
} bool_result_t;

ALWAYS_INLINE bool_result_t
bool_result_error(error_t err);
ALWAYS_INLINE bool_result_t
bool_result_ok(bool ret);

typedef struct uint8_result {
	uint8_t			    r;
	error_t alignas(register_t) e;
} uint8_result_t;

ALWAYS_INLINE uint8_result_t
uint8_result_error(error_t err);
ALWAYS_INLINE uint8_result_t
uint8_result_ok(uint8_t ret);

typedef struct uint16_result {
	uint16_t		    r;
	error_t alignas(register_t) e;
} uint16_result_t;

ALWAYS_INLINE uint16_result_t
uint16_result_error(error_t err);
ALWAYS_INLINE uint16_result_t
uint16_result_ok(uint16_t ret);

typedef struct uint32_result {
	uint32_t		    r;
	error_t alignas(register_t) e;
} uint32_result_t;

ALWAYS_INLINE uint32_result_t
uint32_result_error(error_t err);
ALWAYS_INLINE uint32_result_t
uint32_result_ok(uint32_t ret);

typedef struct uint64_result {
	uint64_t		    r;
	error_t alignas(register_t) e;
} uint64_result_t;

ALWAYS_INLINE uint64_result_t
uint64_result_error(error_t err);
ALWAYS_INLINE uint64_result_t
uint64_result_ok(uint64_t ret);

typedef struct uintptr_result {
	uintptr_t		    r;
	error_t alignas(register_t) e;
} uintptr_result_t;

ALWAYS_INLINE uintptr_result_t
uintptr_result_error(error_t err);
ALWAYS_INLINE uintptr_result_t
uintptr_result_ok(uintptr_t ret);

typedef struct sint8_result {
	int8_t			    r;
	error_t alignas(register_t) e;
} sint8_result_t;

ALWAYS_INLINE sint8_result_t
sint8_result_error(error_t err);
ALWAYS_INLINE sint8_result_t
sint8_result_ok(int8_t ret);

typedef struct sint16_result {
	int16_t			    r;
	error_t alignas(register_t) e;
} sint16_result_t;

ALWAYS_INLINE sint16_result_t
sint16_result_error(error_t err);
ALWAYS_INLINE sint16_result_t
sint16_result_ok(int16_t ret);

typedef struct sint32_result {
	int32_t			    r;
	error_t alignas(register_t) e;
} sint32_result_t;

ALWAYS_INLINE sint32_result_t
sint32_result_error(error_t err);
ALWAYS_INLINE sint32_result_t
sint32_result_ok(int32_t ret);

typedef struct sint64_result {
	int64_t			    r;
	error_t alignas(register_t) e;
} sint64_result_t;

ALWAYS_INLINE sint64_result_t
sint64_result_error(error_t err);
ALWAYS_INLINE sint64_result_t
sint64_result_ok(int64_t ret);

typedef struct sintptr_result {
	intptr_t		    r;
	error_t alignas(register_t) e;
} sintptr_result_t;

ALWAYS_INLINE sintptr_result_t
sintptr_result_error(error_t err);
ALWAYS_INLINE sintptr_result_t
sintptr_result_ok(intptr_t ret);

typedef struct char_result {
	char			    r;
	error_t alignas(register_t) e;
} char_result_t;

ALWAYS_INLINE char_result_t
char_result_error(error_t err);
ALWAYS_INLINE char_result_t
char_result_ok(char ret);

typedef struct size_result {
	size_t			    r;
	error_t alignas(register_t) e;
} size_result_t;

ALWAYS_INLINE size_result_t
size_result_error(error_t err);
ALWAYS_INLINE size_result_t
size_result_ok(size_t ret);

typedef struct void_ptr_result {
	void			   *r;
	error_t alignas(register_t) e;
} void_ptr_result_t;

ALWAYS_INLINE void_ptr_result_t
void_ptr_result_error(error_t err);
ALWAYS_INLINE void_ptr_result_t
void_ptr_result_ok(void *ret);

#pragma clang diagnostic pop
#else
#error HYPRESULT_H_ multiple include
#endif
