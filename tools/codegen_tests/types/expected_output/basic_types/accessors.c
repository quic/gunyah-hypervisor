// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <panic.h>

// Bitfield Accessors

void
StructBitField_init(StructBitField_t *bit_field)
{
	*bit_field = StructBitField_default();
}

StructBitField_t
StructBitField_clean(StructBitField_t bit_field)
{
	return (StructBitField_t){ .bf = {
					   (bit_field.bf[0] &
					    0x7f807ffffffffffU),
					   0x0,
				   } };
}

bool
StructBitField_is_equal(StructBitField_t b1, StructBitField_t b2)
{
	return ((b1.bf[0] & 0x7f807ffffffffffU) ==
		(b2.bf[0] & 0x7f807ffffffffffU));
}

bool
StructBitField_is_clean(StructBitField_t bit_field)
{
	return ((bit_field.bf[0] & 0xf807f80000000000U) == 0x0U) &&
	       ((bit_field.bf[1] & 0xffffffffffffffffU) == 0x0U);
}

void
StructBitField_set_test(StructBitField_t *bit_field, uint32_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffff00000000U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffU);
}

uint32_t
StructBitField_get_test(const StructBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffU);
	return (uint32_t)val;
}

void
StructBitField_copy_test(StructBitField_t	*bit_field_dst,
			 const StructBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffU;
}

void
StructBitField_set_test1(StructBitField_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffff800ffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x7ffU) << 32U;
}

uint16_t
StructBitField_get_test1(const StructBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 32U) & (uint64_t)0x7ffU);
	return (uint16_t)val;
}

void
StructBitField_copy_test1(StructBitField_t	 *bit_field_dst,
			  const StructBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7ff00000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7ff00000000U;
}

void
StructBitField_set_test5(StructBitField_t *bit_field, uint8_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xf807ffffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 51U;
}

uint8_t
StructBitField_get_test5(const StructBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 51U) & (uint64_t)0xffU);
	return (uint8_t)val;
}

void
StructBitField_copy_test5(StructBitField_t	 *bit_field_dst,
			  const StructBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7f8000000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7f8000000000000U;
}

void
TEST_COMB_feat_var_VAR1_init(TEST_COMB_feat_var_VAR1_t *bit_field)
{
	*bit_field = TEST_COMB_feat_var_VAR1_default();
}

uint64_t
TEST_COMB_feat_var_VAR1_raw(TEST_COMB_feat_var_VAR1_t bit_field)
{
	return bit_field.bf[0];
}

TEST_COMB_feat_var_VAR1_t
TEST_COMB_feat_var_VAR1_clean(TEST_COMB_feat_var_VAR1_t bit_field)
{
	return (TEST_COMB_feat_var_VAR1_t){ .bf = {
						    (bit_field.bf[0] &
						     0xffffffffffffffffU),
					    } };
}

bool
TEST_COMB_feat_var_VAR1_is_equal(TEST_COMB_feat_var_VAR1_t b1,
				 TEST_COMB_feat_var_VAR1_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_COMB_feat_var_VAR1_is_clean(TEST_COMB_feat_var_VAR1_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_COMB_feat_var_VAR1_set_value(TEST_COMB_feat_var_VAR1_t *bit_field,
				  uint64_t		     val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_COMB_feat_var_VAR1_get_value(const TEST_COMB_feat_var_VAR1_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_COMB_feat_var_VAR1_copy_value(
	TEST_COMB_feat_var_VAR1_t	*bit_field_dst,
	const TEST_COMB_feat_var_VAR1_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TEST_COMB_feat_var_VAR2_init(TEST_COMB_feat_var_VAR2_t *bit_field)
{
	*bit_field = TEST_COMB_feat_var_VAR2_default();
}

uint64_t
TEST_COMB_feat_var_VAR2_raw(TEST_COMB_feat_var_VAR2_t bit_field)
{
	return bit_field.bf[0];
}

TEST_COMB_feat_var_VAR2_t
TEST_COMB_feat_var_VAR2_clean(TEST_COMB_feat_var_VAR2_t bit_field)
{
	return (TEST_COMB_feat_var_VAR2_t){ .bf = {
						    (bit_field.bf[0] &
						     0xffffffffffffffffU),
					    } };
}

bool
TEST_COMB_feat_var_VAR2_is_equal(TEST_COMB_feat_var_VAR2_t b1,
				 TEST_COMB_feat_var_VAR2_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_COMB_feat_var_VAR2_is_clean(TEST_COMB_feat_var_VAR2_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_COMB_feat_var_VAR2_set_value(TEST_COMB_feat_var_VAR2_t *bit_field,
				  uint64_t		     val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_COMB_feat_var_VAR2_get_value(const TEST_COMB_feat_var_VAR2_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_COMB_feat_var_VAR2_copy_value(
	TEST_COMB_feat_var_VAR2_t	*bit_field_dst,
	const TEST_COMB_feat_var_VAR2_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TEST_COMB_multi_over_VAR1_init(TEST_COMB_multi_over_VAR1_t *bit_field)
{
	*bit_field = TEST_COMB_multi_over_VAR1_default();
}

uint64_t
TEST_COMB_multi_over_VAR1_raw(TEST_COMB_multi_over_VAR1_t bit_field)
{
	return bit_field.bf[0];
}

TEST_COMB_multi_over_VAR1_t
TEST_COMB_multi_over_VAR1_clean(TEST_COMB_multi_over_VAR1_t bit_field)
{
	return (TEST_COMB_multi_over_VAR1_t){ .bf = {
						      (bit_field.bf[0] &
						       0xffffffffffffffffU),
					      } };
}

bool
TEST_COMB_multi_over_VAR1_is_equal(TEST_COMB_multi_over_VAR1_t b1,
				   TEST_COMB_multi_over_VAR1_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_COMB_multi_over_VAR1_is_clean(TEST_COMB_multi_over_VAR1_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_COMB_multi_over_VAR1_set_value(TEST_COMB_multi_over_VAR1_t *bit_field,
				    uint64_t			 val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_COMB_multi_over_VAR1_get_value(const TEST_COMB_multi_over_VAR1_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_COMB_multi_over_VAR1_copy_value(
	TEST_COMB_multi_over_VAR1_t	  *bit_field_dst,
	const TEST_COMB_multi_over_VAR1_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TEST_COMB_multi_over_VAR2_init(TEST_COMB_multi_over_VAR2_t *bit_field)
{
	*bit_field = TEST_COMB_multi_over_VAR2_default();
}

uint64_t
TEST_COMB_multi_over_VAR2_raw(TEST_COMB_multi_over_VAR2_t bit_field)
{
	return bit_field.bf[0];
}

TEST_COMB_multi_over_VAR2_t
TEST_COMB_multi_over_VAR2_clean(TEST_COMB_multi_over_VAR2_t bit_field)
{
	return (TEST_COMB_multi_over_VAR2_t){ .bf = {
						      (bit_field.bf[0] &
						       0xffffffffffffffffU),
					      } };
}

bool
TEST_COMB_multi_over_VAR2_is_equal(TEST_COMB_multi_over_VAR2_t b1,
				   TEST_COMB_multi_over_VAR2_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_COMB_multi_over_VAR2_is_clean(TEST_COMB_multi_over_VAR2_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_COMB_multi_over_VAR2_set_value(TEST_COMB_multi_over_VAR2_t *bit_field,
				    uint64_t			 val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_COMB_multi_over_VAR2_get_value(const TEST_COMB_multi_over_VAR2_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_COMB_multi_over_VAR2_copy_value(
	TEST_COMB_multi_over_VAR2_t	  *bit_field_dst,
	const TEST_COMB_multi_over_VAR2_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TEST_DEFAULT_feat_init(TEST_DEFAULT_feat_t *bit_field)
{
	*bit_field = TEST_DEFAULT_feat_default();
}

uint64_t
TEST_DEFAULT_feat_raw(TEST_DEFAULT_feat_t bit_field)
{
	return bit_field.bf[0];
}

TEST_DEFAULT_feat_t
TEST_DEFAULT_feat_clean(TEST_DEFAULT_feat_t bit_field)
{
	return (TEST_DEFAULT_feat_t){ .bf = {
					      (bit_field.bf[0] &
					       0xffffffffffffffffU),
				      } };
}

bool
TEST_DEFAULT_feat_is_equal(TEST_DEFAULT_feat_t b1, TEST_DEFAULT_feat_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_DEFAULT_feat_is_clean(TEST_DEFAULT_feat_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_DEFAULT_feat_set_value(TEST_DEFAULT_feat_t *bit_field, uint64_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_DEFAULT_feat_get_value(const TEST_DEFAULT_feat_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_DEFAULT_feat_copy_value(TEST_DEFAULT_feat_t       *bit_field_dst,
			     const TEST_DEFAULT_feat_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TEST_DEFAULT_plain_init(TEST_DEFAULT_plain_t *bit_field)
{
	*bit_field = TEST_DEFAULT_plain_default();
}

uint64_t
TEST_DEFAULT_plain_raw(TEST_DEFAULT_plain_t bit_field)
{
	return bit_field.bf[0];
}

TEST_DEFAULT_plain_t
TEST_DEFAULT_plain_clean(TEST_DEFAULT_plain_t bit_field)
{
	return (TEST_DEFAULT_plain_t){ .bf = {
					       (bit_field.bf[0] &
						0xffffffffffffffffU),
				       } };
}

bool
TEST_DEFAULT_plain_is_equal(TEST_DEFAULT_plain_t b1, TEST_DEFAULT_plain_t b2)
{
	return ((b1.bf[0] & 0xffffffffffffffffU) ==
		(b2.bf[0] & 0xffffffffffffffffU));
}

bool
TEST_DEFAULT_plain_is_clean(TEST_DEFAULT_plain_t bit_field)
{
	return ((bit_field.bf[0] & 0x0U) == 0x0U);
}

void
TEST_DEFAULT_plain_set_value(TEST_DEFAULT_plain_t *bit_field, uint64_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0x0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffffffffU);
}

uint64_t
TEST_DEFAULT_plain_get_value(const TEST_DEFAULT_plain_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffffffffffU);
	return (uint64_t)val;
}

void
TEST_DEFAULT_plain_copy_value(TEST_DEFAULT_plain_t	 *bit_field_dst,
			      const TEST_DEFAULT_plain_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffffffffffU;
}

void
TestBitField_init(TestBitField_t *bit_field)
{
	*bit_field = TestBitField_default();
}

TestBitField_t
TestBitField_clean(TestBitField_t bit_field)
{
	return (TestBitField_t){ .bf = {
					 (bit_field.bf[0] & 0x7ffffffffffffU),
					 0x0,
				 } };
}

bool
TestBitField_is_equal(TestBitField_t b1, TestBitField_t b2)
{
	return ((b1.bf[0] & 0x7ffffffffffffU) == (b2.bf[0] & 0x7ffffffffffffU));
}

bool
TestBitField_is_clean(TestBitField_t bit_field)
{
	return ((bit_field.bf[0] & 0xfff8000000000000U) == 0x0U) &&
	       ((bit_field.bf[1] & 0xffffffffffffffffU) == 0x0U);
}

void
TestBitField_set_test(TestBitField_t *bit_field, uint32_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffff00000000U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffU);
}

uint32_t
TestBitField_get_test(const TestBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffU);
	return (uint32_t)val;
}

void
TestBitField_copy_test(TestBitField_t	    *bit_field_dst,
		       const TestBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffU;
}

void
TestBitField_set_test1(TestBitField_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffff800ffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x7ffU) << 32U;
}

uint16_t
TestBitField_get_test1(const TestBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 32U) & (uint64_t)0x7ffU);
	return (uint16_t)val;
}

void
TestBitField_copy_test1(TestBitField_t	     *bit_field_dst,
			const TestBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7ff00000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7ff00000000U;
}

void
TestBitField_set_test5(TestBitField_t *bit_field, uint8_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfff807ffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 43U;
}

uint8_t
TestBitField_get_test5(const TestBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 43U) & (uint64_t)0xffU);
	return (uint8_t)val;
}

void
TestBitField_copy_test5(TestBitField_t	     *bit_field_dst,
			const TestBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7f80000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7f80000000000U;
}

void
UBitField_init(UBitField_t *bit_field)
{
	*bit_field = UBitField_default();
}

UBitField_t
UBitField_clean(UBitField_t bit_field)
{
	return (UBitField_t){ .bf = {
				      (bit_field.bf[0] & 0x7f807ffffffffffU),
				      0x0,
			      } };
}

bool
UBitField_is_equal(UBitField_t b1, UBitField_t b2)
{
	return ((b1.bf[0] & 0x7f807ffffffffffU) ==
		(b2.bf[0] & 0x7f807ffffffffffU));
}

bool
UBitField_is_clean(UBitField_t bit_field)
{
	return ((bit_field.bf[0] & 0xf807f80000000000U) == 0x0U) &&
	       ((bit_field.bf[1] & 0xffffffffffffffffU) == 0x0U);
}

void
UBitField_set_test(UBitField_t *bit_field, uint32_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffff00000000U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffU);
}

uint32_t
UBitField_get_test(const UBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffffffffU);
	return (uint32_t)val;
}

void
UBitField_copy_test(UBitField_t	      *bit_field_dst,
		    const UBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffU;
}

void
UBitField_set_test1(UBitField_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffff800ffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x7ffU) << 32U;
}

uint16_t
UBitField_get_test1(const UBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 32U) & (uint64_t)0x7ffU);
	return (uint16_t)val;
}

void
UBitField_copy_test1(UBitField_t       *bit_field_dst,
		     const UBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7ff00000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7ff00000000U;
}

void
UBitField_set_test5(UBitField_t *bit_field, uint8_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xf807ffffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 51U;
}

uint8_t
UBitField_get_test5(const UBitField_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 51U) & (uint64_t)0xffU);
	return (uint8_t)val;
}

void
UBitField_copy_test5(UBitField_t       *bit_field_dst,
		     const UBitField_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x7f8000000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x7f8000000000000U;
}

void
auto_layout_register_init(auto_layout_register_t *bit_field)
{
	*bit_field = auto_layout_register_default();
}

uint64_t
auto_layout_register_raw(auto_layout_register_t bit_field)
{
	return bit_field.bf[0];
}

auto_layout_register_t
auto_layout_register_clean(auto_layout_register_t bit_field)
{
	return (auto_layout_register_t){ .bf = {
						 (bit_field.bf[0] & 0xffff7fU),
					 } };
}

bool
auto_layout_register_is_equal(auto_layout_register_t b1,
			      auto_layout_register_t b2)
{
	return ((b1.bf[0] & 0xffff7fU) == (b2.bf[0] & 0xffff7fU));
}

bool
auto_layout_register_is_clean(auto_layout_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xffffffffff000080U) == 0x0U);
}

void
auto_layout_register_set_auto_field_5bit(auto_layout_register_t *bit_field,
					 uint8_t		 val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffffe0U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x1fU);
}

uint8_t
auto_layout_register_get_auto_field_5bit(const auto_layout_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0x1fU);
	return (uint8_t)val;
}

void
auto_layout_register_copy_auto_field_5bit(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x1fU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x1fU;
}

void
auto_layout_register_set_auto_field_enum(auto_layout_register_t *bit_field,
					 color_code_t		 val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff9fU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x3U) << 5U;
}

color_code_t
auto_layout_register_get_auto_field_enum(const auto_layout_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 5U) & (uint64_t)0x3U);
	return (color_code_t)val;
}

void
auto_layout_register_copy_auto_field_enum(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x60U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x60U;
}

void
auto_layout_register_set_replacement_field(auto_layout_register_t *bit_field,
					   uint16_t		   val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffff00ffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 8U;
}

uint16_t
auto_layout_register_get_replacement_field(
	const auto_layout_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 8U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
auto_layout_register_copy_replacement_field(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff00U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff00U;
}

void
auto_layout_register_set_additional_auto_field(
	auto_layout_register_t *bit_field, uint8_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffff00ffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 16U;
}

uint8_t
auto_layout_register_get_additional_auto_field(
	const auto_layout_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xffU);
	return (uint8_t)val;
}

void
auto_layout_register_copy_additional_auto_field(
	auto_layout_register_t	     *bit_field_dst,
	const auto_layout_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff0000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff0000U;
}

void
initialization_register_init(initialization_register_t *bit_field)
{
	*bit_field = initialization_register_default();
}

uint64_t
initialization_register_raw(initialization_register_t bit_field)
{
	return bit_field.bf[0];
}

initialization_register_t
initialization_register_clean(initialization_register_t bit_field)
{
	return (initialization_register_t){ .bf = {
						    // (0x5555035555bead80U &
						    // ~0xff0000ff0080U) | ...
						    (uint64_t)(0x555500555500ad00U) |
							    (bit_field.bf[0] &
							     0xff0000ff0080U),
					    } };
}

bool
initialization_register_is_equal(initialization_register_t b1,
				 initialization_register_t b2)
{
	return ((b1.bf[0] & 0xff0000ff0080U) == (b2.bf[0] & 0xff0000ff0080U));
}

bool
initialization_register_is_clean(initialization_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xffff00ffff00ff7fU) == 0x555500555500ad00U);
}

void
initialization_register_set_magic_byte_b(initialization_register_t *bit_field,
					 uint16_t		    val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffff00ffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 8U;
}

uint16_t
initialization_register_get_magic_byte_c(
	const initialization_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
initialization_register_set_reset_flag(initialization_register_t *bit_field,
				       uint16_t			  val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff7fU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x1U) << 7U;
}

uint16_t
initialization_register_get_reset_flag(
	const initialization_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 7U) & (uint64_t)0x1U);
	return (uint16_t)val;
}

void
initialization_register_copy_reset_flag(
	initialization_register_t	*bit_field_dst,
	const initialization_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x80U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x80U;
}

void
initialization_register_set_extended_config(
	initialization_register_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffff00ffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 40U;
}

uint16_t
initialization_register_get_extended_config(
	const initialization_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 40U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
initialization_register_copy_extended_config(
	initialization_register_t	*bit_field_dst,
	const initialization_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff0000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff0000000000U;
}

void
interrupt_control_register_init(interrupt_control_register_t *bit_field)
{
	*bit_field = interrupt_control_register_default();
}

uint64_t
interrupt_control_register_raw(interrupt_control_register_t bit_field)
{
	return bit_field.bf[0];
}

interrupt_control_register_t
interrupt_control_register_clean(interrupt_control_register_t bit_field)
{
	return (interrupt_control_register_t){ .bf = {
						       (bit_field.bf[0] &
							0xf00ffffffU),
					       } };
}

bool
interrupt_control_register_is_equal(interrupt_control_register_t b1,
				    interrupt_control_register_t b2)
{
	return ((b1.bf[0] & 0xf00ffffffU) == (b2.bf[0] & 0xf00ffffffU));
}

bool
interrupt_control_register_is_clean(interrupt_control_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xfffffff0ff000000U) == 0x0U);
}

void
interrupt_control_register_set_interrupt_id(
	interrupt_control_register_t *bit_field, color_code_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff00U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU);
}

color_code_t
interrupt_control_register_get_interrupt_id(
	const interrupt_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffU);
	return (color_code_t)val;
}

void
interrupt_control_register_copy_interrupt_id(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffU;
}

void
interrupt_control_register_set_priority_level(
	interrupt_control_register_t *bit_field, int16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffff00ffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 8U;
}

int16_t
interrupt_control_register_get_priority_level(
	const interrupt_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 8U) & (uint64_t)0xffU);
	val = (val ^ 0x80U) - 0x80U;
	return (int16_t)val;
}

void
interrupt_control_register_copy_priority_level(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff00U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff00U;
}

uint16_t
interrupt_control_register_get_handler_index(
	const interrupt_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
interrupt_control_register_set_interrupt_flags(
	interrupt_control_register_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffffff0ffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xfU) << 32U;
}

uint16_t
interrupt_control_register_get_interrupt_flags(
	const interrupt_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 32U) & (uint64_t)0xfU);
	return (uint16_t)val;
}

void
interrupt_control_register_copy_interrupt_flags(
	interrupt_control_register_t	   *bit_field_dst,
	const interrupt_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xf00000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xf00000000U;
}

void
memory_config_register_init(memory_config_register_t *bit_field)
{
	*bit_field = memory_config_register_default();
}

uint64_t
memory_config_register_raw(memory_config_register_t bit_field)
{
	return bit_field.bf[0];
}

memory_config_register_t
memory_config_register_clean(memory_config_register_t bit_field)
{
	return (memory_config_register_t){ .bf = {
						   (bit_field.bf[0] &
						    0xffffffffffff00ffU),
					   } };
}

bool
memory_config_register_is_equal(memory_config_register_t b1,
				memory_config_register_t b2)
{
	return ((b1.bf[0] & 0xffffffffffff00ffU) ==
		(b2.bf[0] & 0xffffffffffff00ffU));
}

bool
memory_config_register_is_clean(memory_config_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xff00U) == 0x0U);
}

void
memory_config_register_set_base_address(memory_config_register_t *bit_field,
					uint16_t		  val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff00U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU);
}

uint16_t
memory_config_register_get_base_address(
	const memory_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
memory_config_register_copy_base_address(
	memory_config_register_t       *bit_field_dst,
	const memory_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffU;
}

uint16_t
memory_config_register_get_read_only_id(
	const memory_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
memory_config_register_set_extended_address(memory_config_register_t *bit_field,
					    uint64_t		      val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffU) << 24U;
}

uint64_t
memory_config_register_get_extended_address(
	const memory_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 24U) & (uint64_t)0xffffffffffU);
	return (uint64_t)val;
}

void
memory_config_register_copy_extended_address(
	memory_config_register_t       *bit_field_dst,
	const memory_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffff000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffff000000U;
}

void
module_config_register_init(module_config_register_t *bit_field)
{
	*bit_field = module_config_register_default();
}

uint64_t
module_config_register_raw(module_config_register_t bit_field)
{
	return bit_field.bf[0];
}

module_config_register_t
module_config_register_clean(module_config_register_t bit_field)
{
	return (module_config_register_t){ .bf = {
						   (bit_field.bf[0] &
						    0x1fffffffffffffffU),
					   } };
}

bool
module_config_register_is_equal(module_config_register_t b1,
				module_config_register_t b2)
{
	return ((b1.bf[0] & 0x1fffffffffffffffU) ==
		(b2.bf[0] & 0x1fffffffffffffffU));
}

bool
module_config_register_is_clean(module_config_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xe000000000000000U) == 0x0U);
}

void
module_config_register_set_feature_flags(module_config_register_t *bit_field,
					 uint8_t		   val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffe0ffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x1fU) << 16U;
}

uint8_t
module_config_register_get_feature_flags(
	const module_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0x1fU);
	return (uint8_t)val;
}

void
module_config_register_copy_feature_flags(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x1f0000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x1f0000U;
}

void
module_config_register_set_capability_bits(module_config_register_t *bit_field,
					   uint32_t		     val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffe00000001fffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffU) << 21U;
}

uint32_t
module_config_register_get_capability_bits(
	const module_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 21U) & (uint64_t)0xffffffffU);
	return (uint32_t)val;
}

void
module_config_register_copy_capability_bits(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x1fffffffe00000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x1fffffffe00000U;
}

void
module_config_register_set_extended_module_id(
	module_config_register_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff00U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU);
}

uint16_t
module_config_register_get_extended_module_id(
	const module_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
module_config_register_copy_extended_module_id(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffU;
}

void
module_config_register_set_module_version(module_config_register_t *bit_field,
					  uint16_t		    val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffff00ffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 8U;
}

uint16_t
module_config_register_get_module_version(
	const module_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 8U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
module_config_register_copy_module_version(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff00U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff00U;
}

void
module_config_register_set_additional_capability(
	module_config_register_t *bit_field, uint8_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xe01fffffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 53U;
}

uint8_t
module_config_register_get_additional_capability(
	const module_config_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 53U) & (uint64_t)0xffU);
	return (uint8_t)val;
}

void
module_config_register_copy_additional_capability(
	module_config_register_t       *bit_field_dst,
	const module_config_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x1fe0000000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x1fe0000000000000U;
}

void
peripheral_control_register_init(peripheral_control_register_t *bit_field)
{
	*bit_field = peripheral_control_register_default();
}

uint64_t
peripheral_control_register_raw(peripheral_control_register_t bit_field)
{
	return bit_field.bf[0];
}

peripheral_control_register_t
peripheral_control_register_clean(peripheral_control_register_t bit_field)
{
	return (peripheral_control_register_t){ .bf = {
							(bit_field.bf[0] &
							 0xffffffffffff00ffU),
						} };
}

bool
peripheral_control_register_is_equal(peripheral_control_register_t b1,
				     peripheral_control_register_t b2)
{
	return ((b1.bf[0] & 0xffffffffffff00ffU) ==
		(b2.bf[0] & 0xffffffffffff00ffU));
}

bool
peripheral_control_register_is_clean(peripheral_control_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xff00U) == 0x0U);
}

void
peripheral_control_register_set_peripheral_id(
	peripheral_control_register_t *bit_field, uint16_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff00U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU);
}

uint16_t
peripheral_control_register_get_peripheral_id(
	const peripheral_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
peripheral_control_register_copy_peripheral_id(
	peripheral_control_register_t	    *bit_field_dst,
	const peripheral_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffU;
}

uint16_t
peripheral_control_register_get_version(
	const peripheral_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
peripheral_control_register_set_config_data(
	peripheral_control_register_t *bit_field, uint64_t val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffffffffffU) << 24U;
}

uint64_t
peripheral_control_register_get_config_data(
	const peripheral_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 24U) & (uint64_t)0xffffffffffU);
	return (uint64_t)val;
}

void
peripheral_control_register_copy_config_data(
	peripheral_control_register_t	    *bit_field_dst,
	const peripheral_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffffffffff000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffffffffff000000U;
}

void
system_control_register_init(system_control_register_t *bit_field)
{
	*bit_field = system_control_register_default();
}

uint64_t
system_control_register_raw(system_control_register_t bit_field)
{
	return bit_field.bf[0];
}

system_control_register_t
system_control_register_clean(system_control_register_t bit_field)
{
	return (system_control_register_t){ .bf = {
						    // (0xa00000500U &
						    // ~0x60f00ffffffffU) | ...
						    (uint64_t)(0xa00000000U) |
							    (bit_field.bf[0] &
							     0x60f00ffffffffU),
					    } };
}

bool
system_control_register_is_equal(system_control_register_t b1,
				 system_control_register_t b2)
{
	return ((b1.bf[0] & 0x60f00ffffffffU) == (b2.bf[0] & 0x60f00ffffffffU));
}

bool
system_control_register_is_clean(system_control_register_t bit_field)
{
	return ((bit_field.bf[0] & 0xfff9f0ff00000000U) == 0xa00000000U);
}

void
system_control_register_set_device_id(system_control_register_t *bit_field,
				      uint16_t			 val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffffff00U;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU);
}

uint16_t
system_control_register_get_device_id(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0]) & (uint64_t)0xffU);
	return (uint16_t)val;
}

void
system_control_register_copy_device_id(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xffU;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xffU;
}

void
system_control_register_set_priority(system_control_register_t *bit_field,
				     int16_t			val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffffffff00ffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xffU) << 8U;
}

int16_t
system_control_register_get_priority(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 8U) & (uint64_t)0xffU);
	val = (val ^ 0x80U) - 0x80U;
	return (int16_t)val;
}

void
system_control_register_copy_priority(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xff00U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xff00U;
}

void
system_control_register_set_status_code(system_control_register_t *bit_field,
					int16_t			   val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffffffffff0ffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0xfU) << 16U;
}

int16_t
system_control_register_get_status_code(
	const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 16U) & (uint64_t)0xfU);
	val = (val ^ 0x8U) - 0x8U;
	return (int16_t)val;
}

void
system_control_register_copy_status_code(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xf0000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xf0000U;
}

void
system_control_register_set_flags(system_control_register_t *bit_field,
				  uint8_t		     val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffffffffc0fffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x3fU) << 20U;
}

uint8_t
system_control_register_get_flags(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 20U) & (uint64_t)0x3fU);
	return (uint8_t)val;
}

void
system_control_register_copy_flags(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x3f00000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x3f00000U;
}

void
system_control_register_set_mode(system_control_register_t *bit_field,
				 uint8_t		    val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xffffffff03ffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x3fU) << 26U;
}

uint8_t
system_control_register_get_mode(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 26U) & (uint64_t)0x3fU);
	return (uint8_t)val;
}

void
system_control_register_copy_mode(system_control_register_t *bit_field_dst,
				  const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xfc000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xfc000000U;
}

void
system_control_register_set_address_offset(system_control_register_t *bit_field,
					   uint32_t		      val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffff0ffffffffffU;
	bf[0] |= (((uint64_t)val >> 12U) & (uint64_t)0xfU) << 40U;
}

uint32_t
system_control_register_get_address_offset(
	const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 40U) & (uint64_t)0xfU) << 12U;
	return (uint32_t)val;
}

void
system_control_register_copy_address_offset(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0xf0000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0xf0000000000U;
}

void
system_control_register_set_enable(system_control_register_t *bit_field,
				   bool			      val)
{
	uint64_t  bool_val = val ? (uint64_t)1 : (uint64_t)0;
	uint64_t *bf	   = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffeffffffffffffU;
	bf[0] |= ((bool_val) & (uint64_t)0x1U) << 48U;
}

uint8_t
system_control_register_get_lock_bit(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 49U) & (uint64_t)0x1U);
	return (uint8_t)val;
}

void
system_control_register_set_sign_bit(system_control_register_t *bit_field,
				     int8_t			val)
{
	uint64_t *bf = &bit_field->bf[0];
	bf[0] &= (uint64_t)0xfffbffffffffffffU;
	bf[0] |= (((uint64_t)val) & (uint64_t)0x1U) << 50U;
}

int8_t
system_control_register_get_sign_bit(const system_control_register_t *bit_field)
{
	uint64_t	val = 0;
	const uint64_t *bf  = (const uint64_t *)&bit_field->bf[0];

	val |= ((bf[0] >> 50U) & (uint64_t)0x1U);
	val = (val ^ 0x1U) - 0x1U;
	return (int8_t)val;
}

void
system_control_register_copy_sign_bit(
	system_control_register_t	*bit_field_dst,
	const system_control_register_t *bit_field_src)
{
	uint64_t       *bf_dst = (uint64_t *)&bit_field_dst->bf[0];
	const uint64_t *bf_src = (const uint64_t *)&bit_field_src->bf[0];
	bf_dst[0] &= ~(uint64_t)0x4000000000000U;
	bf_dst[0] |= bf_src[0] & (uint64_t)0x4000000000000U;
}

// Enumeration accessors

color_code_t
color_code_raw_cast(uint32_t val)
{
	color_code_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (uint32_t)RED:
		ret = RED;
		break;
	case (uint32_t)BLUE:
		ret = BLUE;
		break;
	default:
		panic("Invalid color_code");
	}
#else
	ret = (color_code_t)val;
#endif

	return ret;
}

color_code_result_t
color_code_raw_cast_safe(uint32_t val)
{
	color_code_result_t ret;

	switch (val) {
	case (uint32_t)RED:
		ret = color_code_result_ok(RED);
		break;
	case (uint32_t)BLUE:
		ret = color_code_result_ok(BLUE);
		break;
	default:
		ret = color_code_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
color_code_raw_is_valid(uint32_t val)
{
	bool ret;

	switch (val) {
	case (uint32_t)RED:
	case (uint32_t)BLUE:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

error_code_t
error_code_raw_cast(int32_t val)
{
	error_code_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (int32_t)ERROR_CODE_ERROR_OK:
		ret = ERROR_CODE_ERROR_OK;
		break;
	case (int32_t)ERROR_CODE_ERROR_RETRY:
		ret = ERROR_CODE_ERROR_RETRY;
		break;
	case (int32_t)ERROR_CODE_ERROR_INVALID_ARG:
		ret = ERROR_CODE_ERROR_INVALID_ARG;
		break;
	case (int32_t)ERROR_CODE_ERROR_TIMEOUT:
		ret = ERROR_CODE_ERROR_TIMEOUT;
		break;
	case (int32_t)ERROR_CODE_ERROR_RESOURCE_BUSY:
		ret = ERROR_CODE_ERROR_RESOURCE_BUSY;
		break;
	case (int32_t)ERROR_CODE_ERROR_NOT_FOUND:
		ret = ERROR_CODE_ERROR_NOT_FOUND;
		break;
	case (int32_t)ERROR_CODE_ERROR_FATAL:
		ret = ERROR_CODE_ERROR_FATAL;
		break;
	case (int32_t)ERROR_CODE_ERROR_PERMISSION_DENIED:
		ret = ERROR_CODE_ERROR_PERMISSION_DENIED;
		break;
	case (int32_t)ERROR_CODE_ERROR_OUT_OF_MEMORY:
		ret = ERROR_CODE_ERROR_OUT_OF_MEMORY;
		break;
	default:
		panic("Invalid error_code");
	}
#else
	ret = (error_code_t)val;
#endif

	return ret;
}

error_code_result_t
error_code_raw_cast_safe(int32_t val)
{
	error_code_result_t ret;

	switch (val) {
	case (int32_t)ERROR_CODE_ERROR_OK:
		ret = error_code_result_ok(ERROR_CODE_ERROR_OK);
		break;
	case (int32_t)ERROR_CODE_ERROR_RETRY:
		ret = error_code_result_ok(ERROR_CODE_ERROR_RETRY);
		break;
	case (int32_t)ERROR_CODE_ERROR_INVALID_ARG:
		ret = error_code_result_ok(ERROR_CODE_ERROR_INVALID_ARG);
		break;
	case (int32_t)ERROR_CODE_ERROR_TIMEOUT:
		ret = error_code_result_ok(ERROR_CODE_ERROR_TIMEOUT);
		break;
	case (int32_t)ERROR_CODE_ERROR_RESOURCE_BUSY:
		ret = error_code_result_ok(ERROR_CODE_ERROR_RESOURCE_BUSY);
		break;
	case (int32_t)ERROR_CODE_ERROR_NOT_FOUND:
		ret = error_code_result_ok(ERROR_CODE_ERROR_NOT_FOUND);
		break;
	case (int32_t)ERROR_CODE_ERROR_FATAL:
		ret = error_code_result_ok(ERROR_CODE_ERROR_FATAL);
		break;
	case (int32_t)ERROR_CODE_ERROR_PERMISSION_DENIED:
		ret = error_code_result_ok(ERROR_CODE_ERROR_PERMISSION_DENIED);
		break;
	case (int32_t)ERROR_CODE_ERROR_OUT_OF_MEMORY:
		ret = error_code_result_ok(ERROR_CODE_ERROR_OUT_OF_MEMORY);
		break;
	default:
		ret = error_code_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
error_code_raw_is_valid(int32_t val)
{
	bool ret;

	switch (val) {
	case (int32_t)ERROR_CODE_ERROR_OK:
	case (int32_t)ERROR_CODE_ERROR_RETRY:
	case (int32_t)ERROR_CODE_ERROR_INVALID_ARG:
	case (int32_t)ERROR_CODE_ERROR_TIMEOUT:
	case (int32_t)ERROR_CODE_ERROR_RESOURCE_BUSY:
	case (int32_t)ERROR_CODE_ERROR_NOT_FOUND:
	case (int32_t)ERROR_CODE_ERROR_FATAL:
	case (int32_t)ERROR_CODE_ERROR_PERMISSION_DENIED:
	case (int32_t)ERROR_CODE_ERROR_OUT_OF_MEMORY:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

priority_level_t
priority_level_raw_cast(uint32_t val)
{
	priority_level_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (uint32_t)PRIORITY_LEVEL_LOW:
		ret = PRIORITY_LEVEL_LOW;
		break;
	case (uint32_t)PRIORITY_LEVEL_MEDIUM:
		ret = PRIORITY_LEVEL_MEDIUM;
		break;
	case (uint32_t)PRIORITY_LEVEL_HIGH:
		ret = PRIORITY_LEVEL_HIGH;
		break;
	default:
		panic("Invalid priority_level");
	}
#else
	ret = (priority_level_t)val;
#endif

	return ret;
}

priority_level_result_t
priority_level_raw_cast_safe(uint32_t val)
{
	priority_level_result_t ret;

	switch (val) {
	case (uint32_t)PRIORITY_LEVEL_LOW:
		ret = priority_level_result_ok(PRIORITY_LEVEL_LOW);
		break;
	case (uint32_t)PRIORITY_LEVEL_MEDIUM:
		ret = priority_level_result_ok(PRIORITY_LEVEL_MEDIUM);
		break;
	case (uint32_t)PRIORITY_LEVEL_HIGH:
		ret = priority_level_result_ok(PRIORITY_LEVEL_HIGH);
		break;
	default:
		ret = priority_level_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
priority_level_raw_is_valid(uint32_t val)
{
	bool ret;

	switch (val) {
	case (uint32_t)PRIORITY_LEVEL_LOW:
	case (uint32_t)PRIORITY_LEVEL_MEDIUM:
	case (uint32_t)PRIORITY_LEVEL_HIGH:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

result_code_t
result_code_raw_cast(uint32_t val)
{
	result_code_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (uint32_t)RESULT_SUCCESS:
		ret = RESULT_SUCCESS;
		break;
	case (uint32_t)RESULT_FAILURE:
		ret = RESULT_FAILURE;
		break;
	default:
		panic("Invalid result_code");
	}
#else
	ret = (result_code_t)val;
#endif

	return ret;
}

result_code_result_t
result_code_raw_cast_safe(uint32_t val)
{
	result_code_result_t ret;

	switch (val) {
	case (uint32_t)RESULT_SUCCESS:
		ret = result_code_result_ok(RESULT_SUCCESS);
		break;
	case (uint32_t)RESULT_FAILURE:
		ret = result_code_result_ok(RESULT_FAILURE);
		break;
	default:
		ret = result_code_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
result_code_raw_is_valid(uint32_t val)
{
	bool ret;

	switch (val) {
	case (uint32_t)RESULT_SUCCESS:
	case (uint32_t)RESULT_FAILURE:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

status_flag_t
status_flag_raw_cast(uint32_t val)
{
	status_flag_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (uint32_t)INACTIVE:
		ret = INACTIVE;
		break;
	case (uint32_t)STATUS_FLAG_ACTIVE:
		ret = STATUS_FLAG_ACTIVE;
		break;
	case (uint32_t)STATUS_FLAG_PENDING:
		ret = STATUS_FLAG_PENDING;
		break;
	default:
		panic("Invalid status_flag");
	}
#else
	ret = (status_flag_t)val;
#endif

	return ret;
}

status_flag_result_t
status_flag_raw_cast_safe(uint32_t val)
{
	status_flag_result_t ret;

	switch (val) {
	case (uint32_t)INACTIVE:
		ret = status_flag_result_ok(INACTIVE);
		break;
	case (uint32_t)STATUS_FLAG_ACTIVE:
		ret = status_flag_result_ok(STATUS_FLAG_ACTIVE);
		break;
	case (uint32_t)STATUS_FLAG_PENDING:
		ret = status_flag_result_ok(STATUS_FLAG_PENDING);
		break;
	default:
		ret = status_flag_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
status_flag_raw_is_valid(uint32_t val)
{
	bool ret;

	switch (val) {
	case (uint32_t)INACTIVE:
	case (uint32_t)STATUS_FLAG_ACTIVE:
	case (uint32_t)STATUS_FLAG_PENDING:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

temperature_offset_t
temperature_offset_raw_cast(int32_t val)
{
	temperature_offset_t ret;

#if !defined(NDEBUG) || defined(PLATFORM_SAFETY)
	switch (val) {
	case (int32_t)TEMP_ZERO:
		ret = TEMP_ZERO;
		break;
	case (int32_t)TEMP_BELOW:
		ret = TEMP_BELOW;
		break;
	case (int32_t)TEMP_ABOVE:
		ret = TEMP_ABOVE;
		break;
	default:
		panic("Invalid temperature_offset");
	}
#else
	ret = (temperature_offset_t)val;
#endif

	return ret;
}

temperature_offset_result_t
temperature_offset_raw_cast_safe(int32_t val)
{
	temperature_offset_result_t ret;

	switch (val) {
	case (int32_t)TEMP_ZERO:
		ret = temperature_offset_result_ok(TEMP_ZERO);
		break;
	case (int32_t)TEMP_BELOW:
		ret = temperature_offset_result_ok(TEMP_BELOW);
		break;
	case (int32_t)TEMP_ABOVE:
		ret = temperature_offset_result_ok(TEMP_ABOVE);
		break;
	default:
		ret = temperature_offset_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}

	return ret;
}

bool
temperature_offset_raw_is_valid(int32_t val)
{
	bool ret;

	switch (val) {
	case (int32_t)TEMP_ZERO:
	case (int32_t)TEMP_BELOW:
	case (int32_t)TEMP_ABOVE:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}
