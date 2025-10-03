// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <abort.h>
#include <compiler.h>
#include <dt.h>
#include <fdt.h>
#include <fdt_helpers.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <trace.h>
#include <util.h>

#define fdt_assert_aligned_uint32(cur)                                         \
	assert(util_is_baligned((uintptr_t)(cur), sizeof(uint32_t)))

static_assert(sizeof(fdt_header_t) == 40U,
	      "fdt_header_t size does not match spec");
static_assert(sizeof(fdt_reserve_entry_t) == 16U,
	      "fdt_reserve_entry_t size does not match spec");
static_assert(sizeof(fdt_property_t) == 8U,
	      "fdt_property_t size does not match spec");

uint32_t
fdt_read32(const char *ptr)
{
	// Note: the compiler should optimise this to a byteswap instruction.
	return ((uint32_t)(unsigned char)ptr[0] << 24) |
	       ((uint32_t)(unsigned char)ptr[1] << 16) |
	       ((uint32_t)(unsigned char)ptr[2] << 8) |
	       ((uint32_t)(unsigned char)ptr[3] << 0);
}

uint64_t
fdt_read64(const char *ptr)
{
	// Note: the compiler should optimise this to a byteswap instruction.
	return ((uint64_t)(unsigned char)ptr[0] << 56) |
	       ((uint64_t)(unsigned char)ptr[1] << 48) |
	       ((uint64_t)(unsigned char)ptr[2] << 40) |
	       ((uint64_t)(unsigned char)ptr[3] << 32) |
	       ((uint64_t)(unsigned char)ptr[4] << 24) |
	       ((uint64_t)(unsigned char)ptr[5] << 16) |
	       ((uint64_t)(unsigned char)ptr[6] << 8) |
	       ((uint64_t)(unsigned char)ptr[7] << 0);
}

uint64_t
fdt_read_num(const char *ptr, count_t cells)
{
	assert((cells == 1U) || (cells == 2U));
	uint64_t ret;

	if (cells == 1U) {
		ret = (uint64_t)fdt_read32(ptr);
	} else {
		ret = fdt_read64(ptr);
	}

	return ret;
}

count_t
fdt_read_reg(const char *reg, count_t reg_size, count_t max_count,
	     count_t addr_cells, count_t size_cells, uint64_t *addr,
	     uint64_t *size)
{
	count_t i = 0;

	count_t stride	    = (addr_cells + size_cells) * sizeof(uint32_t);
	count_t addr_stride = addr_cells * sizeof(uint32_t);

	for (count_t offset = 0U; offset < reg_size; offset += stride) {
		addr[i] = fdt_read_num(reg + offset, addr_cells);
		size[i] = fdt_read_num(reg + offset + addr_stride, size_cells);

		i++;

		if (i >= max_count) {
			break;
		}
	}

	return i;
}

fdt_reserve_entry_t
fdt_read_reserve_entry(const char *ptr)
{
	uint64_t address = fdt_read64(ptr);
	uint64_t size	 = fdt_read64(ptr + 8U);

	return (fdt_reserve_entry_t){
		.address = address,
		.size	 = size,
	};
}

const char *
fdt_iter_stringlist(const char *sl, count_t sl_size, count_t *iter_ctx)
{
	const char *s;

	if (*iter_ctx >= sl_size) {
		s = NULL;
		goto out;
	}

	s = sl + *iter_ctx;
	*iter_ctx += (count_t)util_strnlen(s, sl_size - *iter_ctx) + 1U;
out:
	return s;
}

fdt_ret_t
fdt_load_header(fdt_header_t *header, char *dtb)
{
	fdt_ret_t ret;

	header->magic		  = fdt_read32(dtb + 0U);
	header->totalsize	  = fdt_read32(dtb + 4U);
	header->off_dt_struct	  = fdt_read32(dtb + 8U);
	header->off_dt_strings	  = fdt_read32(dtb + 12U);
	header->off_mem_rsvmap	  = fdt_read32(dtb + 16U);
	header->version		  = fdt_read32(dtb + 20U);
	header->last_comp_version = fdt_read32(dtb + 24U);
	header->boot_cpuid_phys	  = fdt_read32(dtb + 28U);
	header->size_dt_strings	  = fdt_read32(dtb + 32U);
	header->size_dt_struct	  = fdt_read32(dtb + 36U);

	if (header->magic != FDT_MAGIC) {
		ret = FDT_ERR_MAGIC;
		goto out;
	}

	if (((uint64_t)header->off_dt_struct + header->size_dt_struct) >
	    header->off_dt_strings) {
		ret = FDT_ERR_OFFSET;
		goto out;
	}
	if (((uint64_t)header->off_dt_strings + header->size_dt_strings) >
	    header->totalsize) {
		ret = FDT_ERR_OFFSET;
		goto out;
	}

	// This check allows us to assume that all strings in the strings block
	// are NUL terminated.
	char *last_string_byte =
		dtb + header->off_dt_strings + header->size_dt_strings - 1U;
	if (*last_string_byte != '\0') {
		ret = FDT_ERR_STRING_BLOCK_NUL;
		goto out;
	}

	// Check that end of dt_struct is appropriately aligned.
	uint32_t dt_struct_end = header->off_dt_struct + header->size_dt_struct;
	if (!util_is_baligned(dt_struct_end, sizeof(uint32_t))) {
		ret = FDT_ERR_STRUCT_BLOCK_END_ALIGN;
		goto out;
	}

	// FIXME: Add more checks here,

	ret = FDT_OK;
out:
	return ret;
}

static void
fdt_parser_log_parse_error(fdt_parser_t *parser, fdt_ret_t err)
{
	uintptr_t offset =
		(uintptr_t)parser->cur - ((uintptr_t)parser->tree->dtb +
					  parser->tree->header.off_dt_struct);
	TRACE_AND_LOG(ERROR, PANIC, "dtb: parse error {:d}, @ offset {:x}",
		      (register_t)err, (register_t)offset);
}

static uint32_t
fdt_parser_skip_nops(fdt_parser_t *parser)
{
	uint32_t val = FDT_NOP;

	while (parser->cur != parser->end) {
		val = fdt_read32(parser->cur);
		parser->cur += sizeof(uint32_t);

		if (val != FDT_NOP) {
			break;
		}
	}

	return val;
}

static void
fdt_parser_align_uint32(fdt_parser_t *parser)
{
	// Note: we can't go past 'end' because it is guaranteed to be aligned.
	uintptr_t aligned_cur =
		util_balign_up((uintptr_t)parser->cur, sizeof(uint32_t));
	parser->cur = (char *)aligned_cur;
}

static fdt_ret_t
fdt_parser_consume_node(fdt_parser_t *parser)
{
	fdt_ret_t ret;

	// 'cur' is updated only if the operation is successful. This is so the
	// caller can know where the error occurred.
	uintptr_t start = (uintptr_t)parser->cur;
	char	 *cur	= parser->cur;

	while (cur != parser->end) {
		char val = *cur;
		cur++;

		if (val == '\0') {
			parser->node.offset =
				(uint32_t)(start - (uintptr_t)parser->dtb);
			parser->node.namelen =
				(count_t)((uintptr_t)cur - start);
			parser->cur = cur;
			fdt_parser_align_uint32(parser);

			ret = FDT_RET_NODE;
			goto out;
		}
	}

	ret = FDT_ERR_NODE_NAME;
out:
	return ret;
}

static fdt_ret_t
fdt_parser_consume_property(fdt_parser_t *parser)
{
	fdt_ret_t ret;

	// 'cur' is updated only if the operation is successful. This is so the
	// caller can know where the error occurred.
	uintptr_t end = (uintptr_t)parser->end;
	char	 *cur = parser->cur;

	// FIXME: Are all these error checks really necessary? Especially if
	// the input is trusted.
	//
	// Keep them for now as it helps when debugging the parser.

	if ((end - (uintptr_t)cur) < sizeof(fdt_property_t)) {
		ret = FDT_ERR_PROP_STRUCT_LEN;
		goto out;
	}

	uint32_t len	 = fdt_read32(cur);
	uint32_t nameoff = fdt_read32(cur + sizeof(uint32_t));

	if ((end - ((uintptr_t)cur + sizeof(fdt_property_t))) < len) {
		ret = FDT_ERR_PROP_VALUE_LEN;
		goto out;
	}

	if (nameoff >= parser->size_dt_strings) {
		ret = FDT_ERR_PROP_STRING_OFF;
		goto out;
	}

	parser->property.len	 = len;
	parser->property.nameoff = nameoff;
	parser->property.value	 = cur + sizeof(fdt_property_t);
	parser->cur		 = cur + sizeof(fdt_property_t) + len;
	fdt_parser_align_uint32(parser);

	ret = FDT_RET_PROPERTY;
out:
	return ret;
}

void
fdt_parser_setup(fdt_parser_t *parser, dt_tree_t *tree, dt_node_t *start)
{
	parser->depth		 = 0;
	parser->allow_prop_token = true;
	parser->node		 = (fdt_parser_node_t){ 0 };
	parser->property	 = (fdt_parser_property_t){ 0 };

	parser->tree		= tree;
	parser->dtb		= tree->dtb;
	parser->end		= tree->dtb + tree->header.off_dt_struct +
				  tree->header.size_dt_struct;
	parser->size_dt_strings = tree->header.size_dt_strings;

	parser->user_ctx.dummy = 0U;

	if (start == NULL) {
		parser->cur = tree->dtb + tree->header.off_dt_struct;
	} else {
		parser->cur = tree->dtb + start->offset - sizeof(uint32_t);
		parser->break_depth = 1;
	}
}

fdt_ret_t
fdt_parser_next(fdt_parser_t *parser)
{
	fdt_assert_aligned_uint32(parser->cur);
	fdt_ret_t ret;

	while (parser->cur != parser->end) {
		uint32_t token = fdt_parser_skip_nops(parser);

		switch (token) {
		case FDT_BEGIN_NODE:
			ret = fdt_parser_consume_node(parser);
			parser->depth++;
			parser->allow_prop_token = true;

			goto out;
		case FDT_END_NODE:
			if (parser->depth == 0) {
				ret = FDT_ERR_DEPTH_END_NODE;
				goto out;
			}

			parser->depth--;
			parser->allow_prop_token = false;

			if ((parser->depth + 1) == parser->break_depth) {
				parser->break_depth = 0;
				ret		    = FDT_RET_DONE;
				goto out;
			}
			break;
		case FDT_PROP:
			if (!parser->allow_prop_token) {
				ret = FDT_ERR_UNEXPECTED_PROP;
			} else {
				ret = fdt_parser_consume_property(parser);
			}

			goto out;
		case FDT_END:
			if ((parser->allow_prop_token) ||
			    (parser->cur != parser->end)) {
				ret = FDT_ERR_UNEXPECTED_END;
			} else if (parser->depth != 0) {
				ret = FDT_ERR_DEPTH_END;
			} else {
				ret = FDT_RET_DONE;
			}
			goto out;
		default:
			ret = FDT_ERR_UNKNOWN_TOKEN;
			goto out;
		}
	}

	ret = FDT_ERR_END_OF_SEGMENT;
out:
	return ret;
}

fdt_ret_t
fdt_parser_next_property(fdt_parser_t *parser)
{
	fdt_assert_aligned_uint32(parser->cur);
	fdt_ret_t ret;

	uint32_t token = fdt_parser_skip_nops(parser);

	if (token == FDT_PROP) {
		ret = fdt_parser_consume_property(parser);
		assert(ret == FDT_RET_PROPERTY);
	} else {
		// Undo the last token read, so that fdt_parser_next will work
		// as expected.
		parser->cur -= sizeof(uint32_t);
		ret = FDT_RET_DONE;
	}

	return ret;
}

fdt_ret_t
fdt_parser_next_node(fdt_parser_t *parser)
{
	fdt_assert_aligned_uint32(parser->cur);
	fdt_ret_t ret;

	uint32_t token = fdt_parser_skip_nops(parser);

	if (token == FDT_BEGIN_NODE) {
		ret = fdt_parser_consume_node(parser);
		assert(ret == FDT_RET_NODE);
		parser->depth++;
		parser->allow_prop_token = true;
	} else {
		parser->cur -= sizeof(uint32_t);
		ret = FDT_RET_DONE;
	}

	return ret;
}

void
fdt_parser_next_end_node(fdt_parser_t *parser)
{
	fdt_assert_aligned_uint32(parser->cur);
	fdt_ret_t err;

	uint32_t token = fdt_parser_skip_nops(parser);

	if (token == FDT_END_NODE) {
		if (parser->depth == 0) {
			err = FDT_ERR_DEPTH_END_NODE;
			goto out;
		}

		parser->depth--;
		parser->allow_prop_token = false;
		err			 = FDT_OK;
	} else {
		err = FDT_ERR_NOT_END_NODE;
	}

out:
	if (err != FDT_OK) {
		fdt_parser_log_parse_error(parser, err);
		panic("fdt_parser_next_end_node failed");
	}
}

void
fdt_parser_next_end_tree(fdt_parser_t *parser)
{
	fdt_assert_aligned_uint32(parser->cur);
	fdt_ret_t err;

	uint32_t token = fdt_parser_skip_nops(parser);

	if (token == FDT_END) {
		if ((parser->allow_prop_token) ||
		    (parser->cur != parser->end)) {
			err = FDT_ERR_UNEXPECTED_END;
		} else if (parser->depth != 0) {
			err = FDT_ERR_DEPTH_END;
		} else {
			err = FDT_OK;
		}
	} else {
		err = FDT_ERR_NOT_END;
	}

	if (err != FDT_OK) {
		fdt_parser_log_parse_error(parser, err);
		panic("fdt_parser_next_end_tree failed");
	}
}

void
fdt_parser_skip_properties(fdt_parser_t *parser)
{
	while (fdt_parser_next_property(parser) == FDT_RET_PROPERTY) {
	}
}

void
fdt_parser_skip_node(fdt_parser_t *parser)
{
	int32_t old_break_depth = parser->break_depth;
	parser->break_depth	= parser->depth;

	while (1) {
		fdt_ret_t err = fdt_parser_next(parser);

		switch (err) {
		case FDT_RET_NODE:
		case FDT_RET_PROPERTY:
			break;
		case FDT_RET_DONE:
			goto out;
		default: {
			fdt_parser_log_parse_error(parser, err);
			panic("fdt_parser_skip_node failed");
		}
		}
	}
out:
	parser->break_depth = old_break_depth;
}

fdt_ret_t
fdt_parser_skip_to_compatible(fdt_parser_t *parser)
{
	fdt_ret_t ret;

	while (fdt_parser_next_property(parser) == FDT_RET_PROPERTY) {
		const char *name = fdt_parser_get_current_prop_name(parser);

		if (util_strncmp(name, "compatible",
				 sizeof_literal("compatible")) == 0) {
			ret = FDT_RET_PROPERTY;
			goto out;
		}
	}

	ret = FDT_RET_DONE;
out:
	return ret;
}

void
fdt_parser_rewind_properties(fdt_parser_t *parser)
{
	parser->cur =
		parser->tree->dtb + parser->node.offset + parser->node.namelen;
	fdt_parser_align_uint32(parser);
}

const char *
fdt_parser_get_current_prop_name(fdt_parser_t *parser)
{
	return dt_get_string(parser->tree, parser->property.nameoff);
}

const char *
fdt_parser_get_current_node_name(fdt_parser_t *parser)
{
	return parser->dtb + parser->node.offset;
}
