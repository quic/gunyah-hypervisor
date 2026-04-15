// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

// Result Accessors

Back_ptr_result_t
Back_ptr_result_error(error_t err)
{
	return (Back_ptr_result_t){ .e = err };
}

Back_ptr_result_t
Back_ptr_result_ok(Back_t *ret)
{
	return (Back_ptr_result_t){ .r = ret, .e = OK };
}

Default_result_t
Default_result_error(error_t err)
{
	return (Default_result_t){ .e = err };
}

Default_result_t
Default_result_ok(Default_t ret)
{
	return (Default_result_t){ .r = ret, .e = OK };
}

Default_ptr_result_t
Default_ptr_result_error(error_t err)
{
	return (Default_ptr_result_t){ .e = err };
}

Default_ptr_result_t
Default_ptr_result_ok(Default_t *ret)
{
	return (Default_ptr_result_t){ .r = ret, .e = OK };
}

Demo2U_ptr_result_t
Demo2U_ptr_result_error(error_t err)
{
	return (Demo2U_ptr_result_t){ .e = err };
}

Demo2U_ptr_result_t
Demo2U_ptr_result_ok(Demo2U_t *ret)
{
	return (Demo2U_ptr_result_t){ .r = ret, .e = OK };
}

DemoContainer_result_t
DemoContainer_result_error(error_t err)
{
	return (DemoContainer_result_t){ .e = err };
}

DemoContainer_result_t
DemoContainer_result_ok(DemoContainer_t ret)
{
	return (DemoContainer_result_t){ .r = ret, .e = OK };
}

DemoContainer_ptr_result_t
DemoContainer_ptr_result_error(error_t err)
{
	return (DemoContainer_ptr_result_t){ .e = err };
}

DemoContainer_ptr_result_t
DemoContainer_ptr_result_ok(DemoContainer_t *ret)
{
	return (DemoContainer_ptr_result_t){ .r = ret, .e = OK };
}

DemoFixed_result_t
DemoFixed_result_error(error_t err)
{
	return (DemoFixed_result_t){ .e = err };
}

DemoFixed_result_t
DemoFixed_result_ok(DemoFixed_t ret)
{
	return (DemoFixed_result_t){ .r = ret, .e = OK };
}

DemoFixed_ptr_result_t
DemoFixed_ptr_result_error(error_t err)
{
	return (DemoFixed_ptr_result_t){ .e = err };
}

DemoFixed_ptr_result_t
DemoFixed_ptr_result_ok(DemoFixed_t *ret)
{
	return (DemoFixed_ptr_result_t){ .r = ret, .e = OK };
}

DemoPacked_result_t
DemoPacked_result_error(error_t err)
{
	return (DemoPacked_result_t){ .e = err };
}

DemoPacked_result_t
DemoPacked_result_ok(DemoPacked_t ret)
{
	return (DemoPacked_result_t){ .r = ret, .e = OK };
}

DemoPacked_ptr_result_t
DemoPacked_ptr_result_error(error_t err)
{
	return (DemoPacked_ptr_result_t){ .e = err };
}

DemoPacked_ptr_result_t
DemoPacked_ptr_result_ok(DemoPacked_t *ret)
{
	return (DemoPacked_ptr_result_t){ .r = ret, .e = OK };
}

DemoPackedInObject_result_t
DemoPackedInObject_result_error(error_t err)
{
	return (DemoPackedInObject_result_t){ .e = err };
}

DemoPackedInObject_result_t
DemoPackedInObject_result_ok(DemoPackedInObject_t ret)
{
	return (DemoPackedInObject_result_t){ .r = ret, .e = OK };
}

DemoPackedInObject_ptr_result_t
DemoPackedInObject_ptr_result_error(error_t err)
{
	return (DemoPackedInObject_ptr_result_t){ .e = err };
}

DemoPackedInObject_ptr_result_t
DemoPackedInObject_ptr_result_ok(DemoPackedInObject_t *ret)
{
	return (DemoPackedInObject_ptr_result_t){ .r = ret, .e = OK };
}

DemoSingleField_result_t
DemoSingleField_result_error(error_t err)
{
	return (DemoSingleField_result_t){ .e = err };
}

DemoSingleField_result_t
DemoSingleField_result_ok(DemoSingleField_t ret)
{
	return (DemoSingleField_result_t){ .r = ret, .e = OK };
}

DemoSingleField_ptr_result_t
DemoSingleField_ptr_result_error(error_t err)
{
	return (DemoSingleField_ptr_result_t){ .e = err };
}

DemoSingleField_ptr_result_t
DemoSingleField_ptr_result_ok(DemoSingleField_t *ret)
{
	return (DemoSingleField_ptr_result_t){ .r = ret, .e = OK };
}

DemoUStruct_result_t
DemoUStruct_result_error(error_t err)
{
	return (DemoUStruct_result_t){ .e = err };
}

DemoUStruct_result_t
DemoUStruct_result_ok(DemoUStruct_t ret)
{
	return (DemoUStruct_result_t){ .r = ret, .e = OK };
}

DemoUStruct_ptr_result_t
DemoUStruct_ptr_result_error(error_t err)
{
	return (DemoUStruct_ptr_result_t){ .e = err };
}

DemoUStruct_ptr_result_t
DemoUStruct_ptr_result_ok(DemoUStruct_t *ret)
{
	return (DemoUStruct_ptr_result_t){ .r = ret, .e = OK };
}

ExecContext_ptr_result_t
ExecContext_ptr_result_error(error_t err)
{
	return (ExecContext_ptr_result_t){ .e = err };
}

ExecContext_ptr_result_t
ExecContext_ptr_result_ok(ExecContext_t *ret)
{
	return (ExecContext_ptr_result_t){ .r = ret, .e = OK };
}

NestUI_ptr_result_t
NestUI_ptr_result_error(error_t err)
{
	return (NestUI_ptr_result_t){ .e = err };
}

NestUI_ptr_result_t
NestUI_ptr_result_ok(NestUI_t *ret)
{
	return (NestUI_ptr_result_t){ .r = ret, .e = OK };
}

Standalone_ptr_result_t
Standalone_ptr_result_error(error_t err)
{
	return (Standalone_ptr_result_t){ .e = err };
}

Standalone_ptr_result_t
Standalone_ptr_result_ok(Standalone_t *ret)
{
	return (Standalone_ptr_result_t){ .r = ret, .e = OK };
}

StructBitField_result_t
StructBitField_result_error(error_t err)
{
	return (StructBitField_result_t){ .e = err };
}

StructBitField_result_t
StructBitField_result_ok(StructBitField_t ret)
{
	return (StructBitField_result_t){ .r = ret, .e = OK };
}

StructBitField_ptr_result_t
StructBitField_ptr_result_error(error_t err)
{
	return (StructBitField_ptr_result_t){ .e = err };
}

StructBitField_ptr_result_t
StructBitField_ptr_result_ok(StructBitField_t *ret)
{
	return (StructBitField_ptr_result_t){ .r = ret, .e = OK };
}

TEST_COMB_feat_var_VAR1_result_t
TEST_COMB_feat_var_VAR1_result_error(error_t err)
{
	return (TEST_COMB_feat_var_VAR1_result_t){ .e = err };
}

TEST_COMB_feat_var_VAR1_result_t
TEST_COMB_feat_var_VAR1_result_ok(TEST_COMB_feat_var_VAR1_t ret)
{
	return (TEST_COMB_feat_var_VAR1_result_t){ .r = ret, .e = OK };
}

TEST_COMB_feat_var_VAR1_ptr_result_t
TEST_COMB_feat_var_VAR1_ptr_result_error(error_t err)
{
	return (TEST_COMB_feat_var_VAR1_ptr_result_t){ .e = err };
}

TEST_COMB_feat_var_VAR1_ptr_result_t
TEST_COMB_feat_var_VAR1_ptr_result_ok(TEST_COMB_feat_var_VAR1_t *ret)
{
	return (TEST_COMB_feat_var_VAR1_ptr_result_t){ .r = ret, .e = OK };
}

TEST_COMB_feat_var_VAR2_result_t
TEST_COMB_feat_var_VAR2_result_error(error_t err)
{
	return (TEST_COMB_feat_var_VAR2_result_t){ .e = err };
}

TEST_COMB_feat_var_VAR2_result_t
TEST_COMB_feat_var_VAR2_result_ok(TEST_COMB_feat_var_VAR2_t ret)
{
	return (TEST_COMB_feat_var_VAR2_result_t){ .r = ret, .e = OK };
}

TEST_COMB_feat_var_VAR2_ptr_result_t
TEST_COMB_feat_var_VAR2_ptr_result_error(error_t err)
{
	return (TEST_COMB_feat_var_VAR2_ptr_result_t){ .e = err };
}

TEST_COMB_feat_var_VAR2_ptr_result_t
TEST_COMB_feat_var_VAR2_ptr_result_ok(TEST_COMB_feat_var_VAR2_t *ret)
{
	return (TEST_COMB_feat_var_VAR2_ptr_result_t){ .r = ret, .e = OK };
}

TEST_COMB_multi_over_VAR1_result_t
TEST_COMB_multi_over_VAR1_result_error(error_t err)
{
	return (TEST_COMB_multi_over_VAR1_result_t){ .e = err };
}

TEST_COMB_multi_over_VAR1_result_t
TEST_COMB_multi_over_VAR1_result_ok(TEST_COMB_multi_over_VAR1_t ret)
{
	return (TEST_COMB_multi_over_VAR1_result_t){ .r = ret, .e = OK };
}

TEST_COMB_multi_over_VAR1_ptr_result_t
TEST_COMB_multi_over_VAR1_ptr_result_error(error_t err)
{
	return (TEST_COMB_multi_over_VAR1_ptr_result_t){ .e = err };
}

TEST_COMB_multi_over_VAR1_ptr_result_t
TEST_COMB_multi_over_VAR1_ptr_result_ok(TEST_COMB_multi_over_VAR1_t *ret)
{
	return (TEST_COMB_multi_over_VAR1_ptr_result_t){ .r = ret, .e = OK };
}

TEST_COMB_multi_over_VAR2_result_t
TEST_COMB_multi_over_VAR2_result_error(error_t err)
{
	return (TEST_COMB_multi_over_VAR2_result_t){ .e = err };
}

TEST_COMB_multi_over_VAR2_result_t
TEST_COMB_multi_over_VAR2_result_ok(TEST_COMB_multi_over_VAR2_t ret)
{
	return (TEST_COMB_multi_over_VAR2_result_t){ .r = ret, .e = OK };
}

TEST_COMB_multi_over_VAR2_ptr_result_t
TEST_COMB_multi_over_VAR2_ptr_result_error(error_t err)
{
	return (TEST_COMB_multi_over_VAR2_ptr_result_t){ .e = err };
}

TEST_COMB_multi_over_VAR2_ptr_result_t
TEST_COMB_multi_over_VAR2_ptr_result_ok(TEST_COMB_multi_over_VAR2_t *ret)
{
	return (TEST_COMB_multi_over_VAR2_ptr_result_t){ .r = ret, .e = OK };
}

TEST_DEFAULT_feat_result_t
TEST_DEFAULT_feat_result_error(error_t err)
{
	return (TEST_DEFAULT_feat_result_t){ .e = err };
}

TEST_DEFAULT_feat_result_t
TEST_DEFAULT_feat_result_ok(TEST_DEFAULT_feat_t ret)
{
	return (TEST_DEFAULT_feat_result_t){ .r = ret, .e = OK };
}

TEST_DEFAULT_feat_ptr_result_t
TEST_DEFAULT_feat_ptr_result_error(error_t err)
{
	return (TEST_DEFAULT_feat_ptr_result_t){ .e = err };
}

TEST_DEFAULT_feat_ptr_result_t
TEST_DEFAULT_feat_ptr_result_ok(TEST_DEFAULT_feat_t *ret)
{
	return (TEST_DEFAULT_feat_ptr_result_t){ .r = ret, .e = OK };
}

TEST_DEFAULT_plain_result_t
TEST_DEFAULT_plain_result_error(error_t err)
{
	return (TEST_DEFAULT_plain_result_t){ .e = err };
}

TEST_DEFAULT_plain_result_t
TEST_DEFAULT_plain_result_ok(TEST_DEFAULT_plain_t ret)
{
	return (TEST_DEFAULT_plain_result_t){ .r = ret, .e = OK };
}

TEST_DEFAULT_plain_ptr_result_t
TEST_DEFAULT_plain_ptr_result_error(error_t err)
{
	return (TEST_DEFAULT_plain_ptr_result_t){ .e = err };
}

TEST_DEFAULT_plain_ptr_result_t
TEST_DEFAULT_plain_ptr_result_ok(TEST_DEFAULT_plain_t *ret)
{
	return (TEST_DEFAULT_plain_ptr_result_t){ .r = ret, .e = OK };
}

TestBitField_result_t
TestBitField_result_error(error_t err)
{
	return (TestBitField_result_t){ .e = err };
}

TestBitField_result_t
TestBitField_result_ok(TestBitField_t ret)
{
	return (TestBitField_result_t){ .r = ret, .e = OK };
}

TestBitField_ptr_result_t
TestBitField_ptr_result_error(error_t err)
{
	return (TestBitField_ptr_result_t){ .e = err };
}

TestBitField_ptr_result_t
TestBitField_ptr_result_ok(TestBitField_t *ret)
{
	return (TestBitField_ptr_result_t){ .r = ret, .e = OK };
}

UBitField_result_t
UBitField_result_error(error_t err)
{
	return (UBitField_result_t){ .e = err };
}

UBitField_result_t
UBitField_result_ok(UBitField_t ret)
{
	return (UBitField_result_t){ .r = ret, .e = OK };
}

UBitField_ptr_result_t
UBitField_ptr_result_error(error_t err)
{
	return (UBitField_ptr_result_t){ .e = err };
}

UBitField_ptr_result_t
UBitField_ptr_result_ok(UBitField_t *ret)
{
	return (UBitField_ptr_result_t){ .r = ret, .e = OK };
}

auto_layout_register_result_t
auto_layout_register_result_error(error_t err)
{
	return (auto_layout_register_result_t){ .e = err };
}

auto_layout_register_result_t
auto_layout_register_result_ok(auto_layout_register_t ret)
{
	return (auto_layout_register_result_t){ .r = ret, .e = OK };
}

auto_layout_register_ptr_result_t
auto_layout_register_ptr_result_error(error_t err)
{
	return (auto_layout_register_ptr_result_t){ .e = err };
}

auto_layout_register_ptr_result_t
auto_layout_register_ptr_result_ok(auto_layout_register_t *ret)
{
	return (auto_layout_register_ptr_result_t){ .r = ret, .e = OK };
}

byte_result_t
byte_result_error(error_t err)
{
	return (byte_result_t){ .e = err };
}

byte_result_t
byte_result_ok(byte_t ret)
{
	return (byte_result_t){ .r = ret, .e = OK };
}

byte_ptr_result_t
byte_ptr_result_error(error_t err)
{
	return (byte_ptr_result_t){ .e = err };
}

byte_ptr_result_t
byte_ptr_result_ok(byte_t *ret)
{
	return (byte_ptr_result_t){ .r = ret, .e = OK };
}

color_code_result_t
color_code_result_error(error_t err)
{
	return (color_code_result_t){ .e = err };
}

color_code_result_t
color_code_result_ok(color_code_t ret)
{
	return (color_code_result_t){ .r = ret, .e = OK };
}

color_code_ptr_result_t
color_code_ptr_result_error(error_t err)
{
	return (color_code_ptr_result_t){ .e = err };
}

color_code_ptr_result_t
color_code_ptr_result_ok(color_code_t *ret)
{
	return (color_code_ptr_result_t){ .r = ret, .e = OK };
}

count_result_t
count_result_error(error_t err)
{
	return (count_result_t){ .e = err };
}

count_result_t
count_result_ok(count_t ret)
{
	return (count_result_t){ .r = ret, .e = OK };
}

count_ptr_result_t
count_ptr_result_error(error_t err)
{
	return (count_ptr_result_t){ .e = err };
}

count_ptr_result_t
count_ptr_result_ok(count_t *ret)
{
	return (count_ptr_result_t){ .r = ret, .e = OK };
}

device_descriptor_result_t
device_descriptor_result_error(error_t err)
{
	return (device_descriptor_result_t){ .e = err };
}

device_descriptor_result_t
device_descriptor_result_ok(device_descriptor_t ret)
{
	return (device_descriptor_result_t){ .r = ret, .e = OK };
}

device_descriptor_ptr_result_t
device_descriptor_ptr_result_error(error_t err)
{
	return (device_descriptor_ptr_result_t){ .e = err };
}

device_descriptor_ptr_result_t
device_descriptor_ptr_result_ok(device_descriptor_t *ret)
{
	return (device_descriptor_ptr_result_t){ .r = ret, .e = OK };
}

dword_result_t
dword_result_error(error_t err)
{
	return (dword_result_t){ .e = err };
}

dword_result_t
dword_result_ok(dword_t ret)
{
	return (dword_result_t){ .r = ret, .e = OK };
}

dword_ptr_result_t
dword_ptr_result_error(error_t err)
{
	return (dword_ptr_result_t){ .e = err };
}

dword_ptr_result_t
dword_ptr_result_ok(dword_t *ret)
{
	return (dword_ptr_result_t){ .r = ret, .e = OK };
}

error_code_result_t
error_code_result_error(error_t err)
{
	return (error_code_result_t){ .e = err };
}

error_code_result_t
error_code_result_ok(error_code_t ret)
{
	return (error_code_result_t){ .r = ret, .e = OK };
}

error_code_ptr_result_t
error_code_ptr_result_error(error_t err)
{
	return (error_code_ptr_result_t){ .e = err };
}

error_code_ptr_result_t
error_code_ptr_result_ok(error_code_t *ret)
{
	return (error_code_ptr_result_t){ .r = ret, .e = OK };
}

half_result_t
half_result_error(error_t err)
{
	return (half_result_t){ .e = err };
}

half_result_t
half_result_ok(half_t ret)
{
	return (half_result_t){ .r = ret, .e = OK };
}

half_ptr_result_t
half_ptr_result_error(error_t err)
{
	return (half_ptr_result_t){ .e = err };
}

half_ptr_result_t
half_ptr_result_ok(half_t *ret)
{
	return (half_ptr_result_t){ .r = ret, .e = OK };
}

index_result_t
index_result_error(error_t err)
{
	return (index_result_t){ .e = err };
}

index_result_t
index_result_ok(index_t ret)
{
	return (index_result_t){ .r = ret, .e = OK };
}

index_ptr_result_t
index_ptr_result_error(error_t err)
{
	return (index_ptr_result_t){ .e = err };
}

index_ptr_result_t
index_ptr_result_ok(index_t *ret)
{
	return (index_ptr_result_t){ .r = ret, .e = OK };
}

initialization_register_result_t
initialization_register_result_error(error_t err)
{
	return (initialization_register_result_t){ .e = err };
}

initialization_register_result_t
initialization_register_result_ok(initialization_register_t ret)
{
	return (initialization_register_result_t){ .r = ret, .e = OK };
}

initialization_register_ptr_result_t
initialization_register_ptr_result_error(error_t err)
{
	return (initialization_register_ptr_result_t){ .e = err };
}

initialization_register_ptr_result_t
initialization_register_ptr_result_ok(initialization_register_t *ret)
{
	return (initialization_register_ptr_result_t){ .r = ret, .e = OK };
}

interrupt_control_register_result_t
interrupt_control_register_result_error(error_t err)
{
	return (interrupt_control_register_result_t){ .e = err };
}

interrupt_control_register_result_t
interrupt_control_register_result_ok(interrupt_control_register_t ret)
{
	return (interrupt_control_register_result_t){ .r = ret, .e = OK };
}

interrupt_control_register_ptr_result_t
interrupt_control_register_ptr_result_error(error_t err)
{
	return (interrupt_control_register_ptr_result_t){ .e = err };
}

interrupt_control_register_ptr_result_t
interrupt_control_register_ptr_result_ok(interrupt_control_register_t *ret)
{
	return (interrupt_control_register_ptr_result_t){ .r = ret, .e = OK };
}

memory_config_register_result_t
memory_config_register_result_error(error_t err)
{
	return (memory_config_register_result_t){ .e = err };
}

memory_config_register_result_t
memory_config_register_result_ok(memory_config_register_t ret)
{
	return (memory_config_register_result_t){ .r = ret, .e = OK };
}

memory_config_register_ptr_result_t
memory_config_register_ptr_result_error(error_t err)
{
	return (memory_config_register_ptr_result_t){ .e = err };
}

memory_config_register_ptr_result_t
memory_config_register_ptr_result_ok(memory_config_register_t *ret)
{
	return (memory_config_register_ptr_result_t){ .r = ret, .e = OK };
}

module_config_register_result_t
module_config_register_result_error(error_t err)
{
	return (module_config_register_result_t){ .e = err };
}

module_config_register_result_t
module_config_register_result_ok(module_config_register_t ret)
{
	return (module_config_register_result_t){ .r = ret, .e = OK };
}

module_config_register_ptr_result_t
module_config_register_ptr_result_error(error_t err)
{
	return (module_config_register_ptr_result_t){ .e = err };
}

module_config_register_ptr_result_t
module_config_register_ptr_result_ok(module_config_register_t *ret)
{
	return (module_config_register_ptr_result_t){ .r = ret, .e = OK };
}

peripheral_control_register_result_t
peripheral_control_register_result_error(error_t err)
{
	return (peripheral_control_register_result_t){ .e = err };
}

peripheral_control_register_result_t
peripheral_control_register_result_ok(peripheral_control_register_t ret)
{
	return (peripheral_control_register_result_t){ .r = ret, .e = OK };
}

peripheral_control_register_ptr_result_t
peripheral_control_register_ptr_result_error(error_t err)
{
	return (peripheral_control_register_ptr_result_t){ .e = err };
}

peripheral_control_register_ptr_result_t
peripheral_control_register_ptr_result_ok(peripheral_control_register_t *ret)
{
	return (peripheral_control_register_ptr_result_t){ .r = ret, .e = OK };
}

priority_level_result_t
priority_level_result_error(error_t err)
{
	return (priority_level_result_t){ .e = err };
}

priority_level_result_t
priority_level_result_ok(priority_level_t ret)
{
	return (priority_level_result_t){ .r = ret, .e = OK };
}

priority_level_ptr_result_t
priority_level_ptr_result_error(error_t err)
{
	return (priority_level_ptr_result_t){ .e = err };
}

priority_level_ptr_result_t
priority_level_ptr_result_ok(priority_level_t *ret)
{
	return (priority_level_ptr_result_t){ .r = ret, .e = OK };
}

result_code_result_t
result_code_result_error(error_t err)
{
	return (result_code_result_t){ .e = err };
}

result_code_result_t
result_code_result_ok(result_code_t ret)
{
	return (result_code_result_t){ .r = ret, .e = OK };
}

result_code_ptr_result_t
result_code_ptr_result_error(error_t err)
{
	return (result_code_ptr_result_t){ .e = err };
}

result_code_ptr_result_t
result_code_ptr_result_ok(result_code_t *ret)
{
	return (result_code_ptr_result_t){ .r = ret, .e = OK };
}

sizeof_test_result_t
sizeof_test_result_error(error_t err)
{
	return (sizeof_test_result_t){ .e = err };
}

sizeof_test_result_t
sizeof_test_result_ok(sizeof_test_t ret)
{
	return (sizeof_test_result_t){ .r = ret, .e = OK };
}

sizeof_test_ptr_result_t
sizeof_test_ptr_result_error(error_t err)
{
	return (sizeof_test_ptr_result_t){ .e = err };
}

sizeof_test_ptr_result_t
sizeof_test_ptr_result_ok(sizeof_test_t *ret)
{
	return (sizeof_test_ptr_result_t){ .r = ret, .e = OK };
}

status_flag_result_t
status_flag_result_error(error_t err)
{
	return (status_flag_result_t){ .e = err };
}

status_flag_result_t
status_flag_result_ok(status_flag_t ret)
{
	return (status_flag_result_t){ .r = ret, .e = OK };
}

status_flag_ptr_result_t
status_flag_ptr_result_error(error_t err)
{
	return (status_flag_ptr_result_t){ .e = err };
}

status_flag_ptr_result_t
status_flag_ptr_result_ok(status_flag_t *ret)
{
	return (status_flag_ptr_result_t){ .r = ret, .e = OK };
}

system_control_register_result_t
system_control_register_result_error(error_t err)
{
	return (system_control_register_result_t){ .e = err };
}

system_control_register_result_t
system_control_register_result_ok(system_control_register_t ret)
{
	return (system_control_register_result_t){ .r = ret, .e = OK };
}

system_control_register_ptr_result_t
system_control_register_ptr_result_error(error_t err)
{
	return (system_control_register_ptr_result_t){ .e = err };
}

system_control_register_ptr_result_t
system_control_register_ptr_result_ok(system_control_register_t *ret)
{
	return (system_control_register_ptr_result_t){ .r = ret, .e = OK };
}

temperature_offset_result_t
temperature_offset_result_error(error_t err)
{
	return (temperature_offset_result_t){ .e = err };
}

temperature_offset_result_t
temperature_offset_result_ok(temperature_offset_t ret)
{
	return (temperature_offset_result_t){ .r = ret, .e = OK };
}

temperature_offset_ptr_result_t
temperature_offset_ptr_result_error(error_t err)
{
	return (temperature_offset_ptr_result_t){ .e = err };
}

temperature_offset_ptr_result_t
temperature_offset_ptr_result_ok(temperature_offset_t *ret)
{
	return (temperature_offset_ptr_result_t){ .r = ret, .e = OK };
}

test_struct_result_t
test_struct_result_error(error_t err)
{
	return (test_struct_result_t){ .e = err };
}

test_struct_result_t
test_struct_result_ok(test_struct_t ret)
{
	return (test_struct_result_t){ .r = ret, .e = OK };
}

test_struct_ptr_result_t
test_struct_ptr_result_error(error_t err)
{
	return (test_struct_ptr_result_t){ .e = err };
}

test_struct_ptr_result_t
test_struct_ptr_result_ok(test_struct_t *ret)
{
	return (test_struct_ptr_result_t){ .r = ret, .e = OK };
}

vaddr_result_t
vaddr_result_error(error_t err)
{
	return (vaddr_result_t){ .e = err };
}

vaddr_result_t
vaddr_result_ok(vaddr_t ret)
{
	return (vaddr_result_t){ .r = ret, .e = OK };
}

vaddr_ptr_result_t
vaddr_ptr_result_error(error_t err)
{
	return (vaddr_ptr_result_t){ .e = err };
}

vaddr_ptr_result_t
vaddr_ptr_result_ok(vaddr_t *ret)
{
	return (vaddr_ptr_result_t){ .r = ret, .e = OK };
}

word_result_t
word_result_error(error_t err)
{
	return (word_result_t){ .e = err };
}

word_result_t
word_result_ok(word_t ret)
{
	return (word_result_t){ .r = ret, .e = OK };
}

word_ptr_result_t
word_ptr_result_error(error_t err)
{
	return (word_ptr_result_t){ .e = err };
}

word_ptr_result_t
word_ptr_result_ok(word_t *ret)
{
	return (word_ptr_result_t){ .r = ret, .e = OK };
}

bool_result_t
bool_result_error(error_t err)
{
	return (bool_result_t){ .e = err };
}

bool_result_t
bool_result_ok(bool ret)
{
	return (bool_result_t){ .r = ret, .e = OK };
}

uint8_result_t
uint8_result_error(error_t err)
{
	return (uint8_result_t){ .e = err };
}

uint8_result_t
uint8_result_ok(uint8_t ret)
{
	return (uint8_result_t){ .r = ret, .e = OK };
}

uint16_result_t
uint16_result_error(error_t err)
{
	return (uint16_result_t){ .e = err };
}

uint16_result_t
uint16_result_ok(uint16_t ret)
{
	return (uint16_result_t){ .r = ret, .e = OK };
}

uint32_result_t
uint32_result_error(error_t err)
{
	return (uint32_result_t){ .e = err };
}

uint32_result_t
uint32_result_ok(uint32_t ret)
{
	return (uint32_result_t){ .r = ret, .e = OK };
}

uint64_result_t
uint64_result_error(error_t err)
{
	return (uint64_result_t){ .e = err };
}

uint64_result_t
uint64_result_ok(uint64_t ret)
{
	return (uint64_result_t){ .r = ret, .e = OK };
}

uintptr_result_t
uintptr_result_error(error_t err)
{
	return (uintptr_result_t){ .e = err };
}

uintptr_result_t
uintptr_result_ok(uintptr_t ret)
{
	return (uintptr_result_t){ .r = ret, .e = OK };
}

sint8_result_t
sint8_result_error(error_t err)
{
	return (sint8_result_t){ .e = err };
}

sint8_result_t
sint8_result_ok(int8_t ret)
{
	return (sint8_result_t){ .r = ret, .e = OK };
}

sint16_result_t
sint16_result_error(error_t err)
{
	return (sint16_result_t){ .e = err };
}

sint16_result_t
sint16_result_ok(int16_t ret)
{
	return (sint16_result_t){ .r = ret, .e = OK };
}

sint32_result_t
sint32_result_error(error_t err)
{
	return (sint32_result_t){ .e = err };
}

sint32_result_t
sint32_result_ok(int32_t ret)
{
	return (sint32_result_t){ .r = ret, .e = OK };
}

sint64_result_t
sint64_result_error(error_t err)
{
	return (sint64_result_t){ .e = err };
}

sint64_result_t
sint64_result_ok(int64_t ret)
{
	return (sint64_result_t){ .r = ret, .e = OK };
}

sintptr_result_t
sintptr_result_error(error_t err)
{
	return (sintptr_result_t){ .e = err };
}

sintptr_result_t
sintptr_result_ok(intptr_t ret)
{
	return (sintptr_result_t){ .r = ret, .e = OK };
}

char_result_t
char_result_error(error_t err)
{
	return (char_result_t){ .e = err };
}

char_result_t
char_result_ok(char ret)
{
	return (char_result_t){ .r = ret, .e = OK };
}

size_result_t
size_result_error(error_t err)
{
	return (size_result_t){ .e = err };
}

size_result_t
size_result_ok(size_t ret)
{
	return (size_result_t){ .r = ret, .e = OK };
}

void_ptr_result_t
void_ptr_result_error(error_t err)
{
	return (void_ptr_result_t){ .e = err };
}

void_ptr_result_t
void_ptr_result_ok(void *ret)
{
	return (void_ptr_result_t){ .r = ret, .e = OK };
}
