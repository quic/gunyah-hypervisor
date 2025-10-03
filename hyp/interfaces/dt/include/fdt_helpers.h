// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Convenience functions for reading numbers.
 *
 * The bytes are assumed to be stored in big-endian order.
 */
uint32_t
fdt_read32(const char *ptr);

uint64_t
fdt_read64(const char *ptr);

uint64_t
fdt_read_num(const char *ptr, count_t cells);

count_t
fdt_read_reg(const char *reg, count_t reg_size, count_t max_count,
	     count_t addr_cells, count_t size_cells, uint64_t *addr,
	     uint64_t *size);

/*
 * Read a memory reservation entry.
 */
fdt_reserve_entry_t
fdt_read_reserve_entry(const char *ptr);

/*
 * Iterate through a stringlist.
 *
 * This function can be used to iterate through a stringlist 'sl' of size
 * 'sl_size' bytes. The current position in the stringlist is stored in
 * '*iter_ctx', which should be set to 0 on the first call and left untouched
 * thereafter.
 *
 * This function returns a pointer to the next string in the list, or NULL if
 * the end of the list is reached.
 */
const char *
fdt_iter_stringlist(const char *sl, count_t sl_size, count_t *iter_ctx);

/*
 * Compare strings for equality.
 *
 * The second argument must be a string literal.
 */
#define fdt_streq(s, t) (util_strncmp((s), (t), sizeof_literal(t)) == 0)

/*
 * Convenience functions for loading properties.
 */
#define fdt_parser_load_prop_value(parser, var)                                \
	do {                                                                   \
		var	  = (parser)->property.value;                          \
		var##_len = (parser)->property.len;                            \
	} while (0)

#define fdt_parser_load_prop_string(parser, var)                               \
	do {                                                                   \
		var = (parser)->property.value;                                \
	} while (0)

#define fdt_parser_load_prop_u32(parser, var)                                  \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint32_t));            \
		var = fdt_read32((parser)->property.value);                    \
	} while (0)

#define fdt_parser_load_prop_u64(parser, var)                                  \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint64_t));            \
		var = fdt_read64((parser)->property.value);                    \
	} while (0)

#define fdt_parser_load_prop_bool(parser, var)                                 \
	do {                                                                   \
		assert((parser)->property.len == 0U);                          \
		var = true;                                                    \
	} while (0)

#define fdt_parser_load_prop_phandle(parser, var)                              \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint32_t));            \
		var = fdt_read32((parser)->property.value);                    \
	} while (0)

#define fdt_parser_load_prop_u32_optional(parser, var)                         \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint32_t));            \
		var	     = fdt_read32((parser)->property.value);           \
		var##_exists = true;                                           \
	} while (0)

#define fdt_parser_load_prop_u64_optional(parser, var)                         \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint64_t));            \
		var	     = fdt_read64((parser)->property.value);           \
		var##_exists = true;                                           \
	} while (0)

#define fdt_parser_load_prop_phandle_optional(parser, var)                     \
	do {                                                                   \
		assert((parser)->property.len == sizeof(uint32_t));            \
		var	     = fdt_read32((parser)->property.value);           \
		var##_exists = true;                                           \
	} while (0)
