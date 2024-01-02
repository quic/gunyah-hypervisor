// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <util.h>

#include "string_util.h"

// TODO: Add more failure test cases

#define MAX_TERMINATOR 16U
#define MAX_ARG_CNT    5U

// The string formatter processing states, the order of these needs to be
// preserved.
#define STAGE_START	      0U
#define STAGE_SPECIFIER_START 1U
#define STAGE_ALIGN	      2U
#define STAGE_SIGN	      3U
#define STAGE_ALTERNATIVE     4U
#define STAGE_ZERO_PADDING    5U
#define STAGE_MINWIDTH	      6U
#define STAGE_PRECISE	      7U
#define STAGE_TYPE	      8U
#define STAGE_END	      9U

// Token for components in one format descriptor
typedef struct token {
	// Indicate the token type
	count_t stage;
} token_t;

typedef enum align_e {
	// The same as left, use white space
	ALIGN_DEFAULT = 0,
	ALIGN_LEFT,
	ALIGN_RIGHT,
	// Only valid for numeric
	ALIGN_AFTER_SIGN,
	ALIGN_CENTER
} align_t;

typedef enum sign_e {
	// default
	SIGN_NEG = 0,
	SIGN_BOTH,
	SIGN_POS_LEADING
} sign_t;

typedef enum var_type {
	// Default
	VAR_TYPE_NONE = 0,
	// b, binary
	VAR_TYPE_BIN,
	// FIXME: only support ASCII now, no unicode
	// VAR_TYPE_CHAR,
	// d, decimal
	VAR_TYPE_DEC,
	// o, octal
	VAR_TYPE_OCTAL,
	// x, hex, lower case
	VAR_TYPE_LOW_HEX,
	// X, hex, upper case
	// VAR_TYPE_UP_HEX,
	// FIXME: Don't support n and None, which is the same as d
	// e, exponential
	// VAR_TYPE_LOW_EXP,
	// FIXME: Support E
	// f, float
	// VAR_TYPE_LOW_FLOAT,
	// s, string
	VAR_TYPE_STRING,
	// FIXME: support F, G, n, %
} var_type_t;

typedef struct fmt_info {
	// Default is none type, if it's none, nothing make sense in this
	// structure, so memset 0 is good enough to initialise it
	var_type_t type;
	// Space by default, \0 means default
	char	fill_char;
	bool	alternate_form;
	bool	zero_padding;
	uint8_t padding2[1];
	align_t alignment;
	sign_t	sign;
	// Minimum width, default 0 means no restrict
	size_t min_width;
	// Precise for float/double, ignored for integer, max width for non
	// digital. Default 0 means 2 precise
	size_t	    precise;
	const char *minwidth_start;
	const char *precise_start;
} fmt_info_t;

typedef enum ret_token {
	// Move to next char
	RET_TOKEN_NEXT_CHAR,
	// Check the same char in next stage
	RET_TOKEN_NEXT_STAGE,
	// This format is done
	RET_TOKEN_STOP,
	// Found a token, should goto next char with next stage
	RET_TOKEN_FOUND,
	// Got invalid input in the [% ] format descriptor
	RET_TOKEN_ERROR,
} ret_token_t;

// Check whether a buffer contains the specified character within the specified
// distance (size).
static inline index_t
strnidx(const char *buf, size_t size, char c)
{
	index_t i;

	for (i = 0U; i < size; i++) {
		if (buf[i] == c) {
			break;
		}
	}

	return i;
}

// Return true if c is in [min, max]
static inline bool
in_range(const char c, const char min, const char max)
{
	return c >= min && c <= max;
}

static inline uint64_t
atodec(const char *c, size_t len)
{
	uint64_t v = 0U;

	while (len != 0U) {
		v *= 10U;
		// TODO: Check if *c is in '0' and '9' for debug
		v += (uint64_t)(*c) - (uint64_t)('0');
		c++;
		len--;
	}

	return v;
}

static size_t
insert_padding(char *buf, size_t size, char fill_char, size_t len)
{
	size_t padding = 0U;

	while ((size > 0U) && (len > 0U)) {
		*buf = fill_char;
		buf++;
		padding++;
		len--;
		size--;
	}

	return padding;
}

static error_t
itoa_insert_sign(const fmt_info_t *info, bool positive, size_t *remaining,
		 char **pos_ptr)
{
	error_t ret = OK;
	char   *pos = *pos_ptr;

	switch (info->sign) {
	case SIGN_BOTH:
		if (positive) {
			*pos = '+';
		} else {
			*pos = '-';
		}
		pos++;
		(*remaining)--;
		if (*remaining == 0U) {
			ret = ERROR_STRING_TRUNCATED;
			goto out;
		}
		break;

	case SIGN_POS_LEADING:
		if (positive) {
			*pos = ' ';
		} else {
			*pos = '-';
		}
		pos++;
		(*remaining)--;
		if (*remaining == 0U) {
			ret = ERROR_STRING_TRUNCATED;
			goto out;
		}
		break;

	case SIGN_NEG:
	default:
		if (!positive) {
			*pos = '-';
			(*remaining)--;
			if (*remaining == 0U) {
				ret = ERROR_STRING_TRUNCATED;
				goto out;
			}
			pos++;
		}
		break;
	}

out:
	*pos_ptr = pos;
	return ret;
}

static error_t
itoa_insert_base(uint8_t base, size_t *remaining, char **pos_ptr)
{
	error_t ret = OK;
	char   *pos = *pos_ptr;

	switch (base) {
	case 2:
		*pos = 'b';
		pos++;
		(*remaining)--;
		break;
	case 8:
		*pos = 'o';
		pos++;
		(*remaining)--;
		break;
	case 16:
		*pos = 'x';
		pos++;
		(*remaining)--;
		break;
	default:
		// Unusual base. Nothing to do
		break;
	}

	if (*remaining == 0U) {
		ret = ERROR_STRING_TRUNCATED;
		goto out;
	}

	switch (base) {
	case 2:
	case 8:
	case 16:
		*pos = '0';
		pos++;
		(*remaining)--;
		break;
	default:
		// Unusual base. Nothing to do
		break;
	}

	if (*remaining == 0U) {
		ret = ERROR_STRING_TRUNCATED;
	}

out:
	*pos_ptr = pos;
	return ret;
}

static inline error_t
itoa(char *buf, size_t *size, uint64_t val, uint8_t base, fmt_info_t *info,
     bool positive)
{
	const char digit[] = { '0', '1', '2', '3', '4', '5', '6', '7',
			       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	char	  *pos = buf, padding_char = ' ', *tail = NULL;
	size_t	   padding_cnt = 0U, content_cnt = 0U, padding_ret = 0U;
	size_t	   padding_left_cnt = 0U, padding_right_cnt = 0U;
	size_t	   padding_after_sign = 0U, padding_after_prefix = 0U;
	size_t	   remaining = *size;
	error_t	   ret	     = OK;

	assert(base <= 16);

	if (remaining == 0U) {
		ret = ERROR_STRING_TRUNCATED;
		goto out;
	}

	do {
		index_t i = (index_t)(val % base);
		*pos	  = digit[i];
		content_cnt++;
		pos++;
		remaining--;
		if (remaining == 0U) {
			ret = ERROR_STRING_TRUNCATED;
			goto reverse_out;
		}
		val = val / base;
	} while (val != 0U);

	padding_cnt = util_max(info->min_width, content_cnt) - content_cnt;

	if ((padding_cnt > 0U) && info->alternate_form && (base != 10U)) {
		// Remove two chars of prefix
		padding_cnt -= 2U;
	}
	if ((padding_cnt > 0U) &&
	    ((info->sign == SIGN_BOTH) || (info->sign == SIGN_POS_LEADING) ||
	     ((info->sign == SIGN_NEG) && !positive))) {
		padding_cnt -= 1U;
	}
	// Padding left with white space by default
	padding_left_cnt = padding_cnt;

	// FIXME: Ignore precise for integer, might report error
	// FIXME: Slightly different zero padding behavior, it takes priority
	if (info->zero_padding) {
		padding_after_prefix = padding_cnt;
		padding_after_sign   = 0U;
		padding_left_cnt     = 0U;
		padding_right_cnt    = 0U;
		padding_char	     = '0';
	}

	if ((info->alignment != ALIGN_DEFAULT) && (info->fill_char != '\0')) {
		padding_char = info->fill_char;
	}

	if (info->alignment == ALIGN_AFTER_SIGN) {
		padding_after_prefix = 0U;
		padding_after_sign   = padding_cnt;
		padding_left_cnt     = 0U;
		padding_right_cnt    = 0U;
	} else if (info->alignment == ALIGN_LEFT) {
		// align content to left, add padding to right
		padding_after_prefix = 0U;
		padding_after_sign   = 0U;
		padding_left_cnt     = 0U;
		padding_right_cnt    = padding_cnt;
	} else if (info->alignment == ALIGN_RIGHT) {
		// align content to right, add padding to left
		padding_after_prefix = 0U;
		padding_after_sign   = 0U;
		padding_left_cnt     = padding_cnt;
		padding_right_cnt    = 0U;
	} else if (info->alignment == ALIGN_CENTER) {
		padding_after_prefix = 0U;
		padding_after_sign   = 0U;
		padding_left_cnt     = padding_cnt / 2U;
		padding_right_cnt    = padding_cnt - padding_left_cnt;
	} else {
		// Nothing to do
	}

	padding_ret = insert_padding(pos, remaining, padding_char,
				     padding_after_prefix);
	pos += padding_ret;
	remaining -= padding_ret;
	if ((remaining == 0U) || (padding_ret < padding_after_prefix)) {
		ret = ERROR_STRING_TRUNCATED;
		goto reverse_out;
	}

	if (info->alternate_form) {
		ret = itoa_insert_base(base, &remaining, &pos);
		if (ret != OK) {
			goto reverse_out;
		}
	}

	padding_ret = insert_padding(pos, remaining, padding_char,
				     padding_after_sign);
	pos += padding_ret;
	remaining -= padding_ret;
	if ((remaining == 0U) || (padding_ret < padding_after_sign)) {
		ret = ERROR_STRING_TRUNCATED;
		goto reverse_out;
	}

	ret = itoa_insert_sign(info, positive, &remaining, &pos);
	if (ret != OK) {
		goto reverse_out;
	}

	padding_ret =
		insert_padding(pos, remaining, padding_char, padding_left_cnt);
	pos += padding_ret;
	remaining -= padding_ret;
	// last padding action, no explicit truncation check

reverse_out:
	// Reverse the final string
	tail = pos;
	pos--;
	while (buf < pos) {
		char tmp = *buf;
		*buf	 = *pos;
		*pos	 = tmp;
		pos--;
		buf++;
	}

	padding_ret = insert_padding(tail, remaining, padding_char,
				     padding_right_cnt);
	remaining -= padding_ret;

	*size = remaining;

out:
	return ret;
}

static inline error_t
sitoa(char *buf, size_t *size, int64_t val, uint8_t base, fmt_info_t *info)
{
	bool positive = true;

	if (val < 0) {
		positive = false;
		val	 = -val;
	} else {
		positive = true;
	}

	return itoa(buf, size, (uint64_t)val, base, info, positive);
}

static inline error_t
stringtoa(char *buf, size_t *size, char *val_str, fmt_info_t *info)
{
	error_t ret	  = OK;
	size_t	remaining = *size;

	if (val_str == NULL) {
		ret = ERROR_STRING_MISSING_ARGUMENT;
		goto out;
	}

	char  *pos = buf, padding_char = ' ';
	size_t slen	   = strlen(val_str);
	size_t padding_cnt = 0U, p = 0U;
	size_t padding_left_cnt = 0U, padding_right_cnt = 0U;

	if (info->precise != 0U) {
		slen = util_min(slen, info->precise);
	}

	padding_cnt = util_max(slen, info->min_width) - slen;

	padding_left_cnt = padding_cnt;
	if ((info->alignment != ALIGN_DEFAULT) && (info->fill_char != '\0')) {
		padding_char = info->fill_char;
	}

	if (info->alignment == ALIGN_LEFT) {
		padding_left_cnt  = padding_cnt;
		padding_right_cnt = 0U;
	} else if (info->alignment == ALIGN_RIGHT) {
		padding_left_cnt  = 0U;
		padding_right_cnt = padding_cnt;
	} else if (info->alignment == ALIGN_CENTER) {
		padding_left_cnt  = padding_cnt / 2U;
		padding_right_cnt = padding_cnt - padding_left_cnt;
	} else {
		// Nothing to do.
	}

	p = util_min(padding_left_cnt, remaining);
	if (p > 0U) {
		(void)insert_padding(pos, p, padding_char, p);
		remaining -= p;
		pos += p;
	}
	if ((remaining + p) < padding_left_cnt) {
		ret = ERROR_STRING_TRUNCATED;
		goto out;
	}

	p = util_min(slen, remaining);
	if (p > 0U) {
		(void)memcpy(pos, val_str, p);
		remaining -= p;
		pos += p;
	}
	if ((remaining + p) < slen) {
		ret = ERROR_STRING_TRUNCATED;
		goto out;
	}

	p = util_min(padding_right_cnt, remaining);
	if (p > 0U) {
		(void)insert_padding(pos, p, padding_char, p);
		remaining -= p;
	}
	if ((remaining + p) < padding_right_cnt) {
		ret = ERROR_STRING_TRUNCATED;
		goto out;
	}

	ret = OK;

out:
	*size = remaining;
	return ret;
}

// The following check can depend on the {*(fmt - 1), *fmt, *(fmt + 1)} to check
// (except start check)
static inline ret_token_t
check_start(const char *fmt, fmt_info_t *info)
{
	(void)info;

	if (*fmt == '{') {
		return RET_TOKEN_FOUND;
	}
	return RET_TOKEN_NEXT_CHAR;
}

static inline ret_token_t
check_specifier_start(const char *fmt, fmt_info_t *info)
{
	(void)info;

	// ignore white space
	if (*fmt == ' ') {
		return RET_TOKEN_NEXT_CHAR;
	}

	if (*fmt == ':') {
		return RET_TOKEN_FOUND;
	}

	return RET_TOKEN_NEXT_CHAR;
}

static inline ret_token_t
check_align(const char *fmt, fmt_info_t *info)
{
	// Mapping from input to output, by index (readable duplication)
	const char    stopper[] = { '<', '>', '=', '^' };
	const align_t output[]	= { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_AFTER_SIGN,
				    ALIGN_CENTER };
	const size_t  len	= util_array_size(stopper);
	// Default to skip alignment stage if not found
	ret_token_t ret = RET_TOKEN_NEXT_STAGE;

	const char *next = NULL;
	if (*(fmt + 1) != '\0') {
		next = fmt + 1;
	}

	// Check for a padding character using look-ahead
	if ((next != NULL) && (strnidx(stopper, len, *next) < len)) {
		if (info->fill_char == '\0') {
			info->fill_char = *fmt;
			ret		= RET_TOKEN_NEXT_CHAR;
		} else {
			// e.g. '{: >>5d}' is an error
			ret = RET_TOKEN_ERROR;
		}
	} else {
		index_t i = strnidx(stopper, len, *fmt);
		if (i < len) {
			info->alignment = output[i];

			ret = RET_TOKEN_FOUND;
		}
	}

	return ret;
}

static inline ret_token_t
check_sign(const char *fmt, fmt_info_t *info)
{
	const char   stopper[] = { '+', '-', ' ' };
	const sign_t output[]  = { SIGN_BOTH, SIGN_NEG, SIGN_POS_LEADING };
	const size_t len       = util_array_size(stopper);

	index_t i = strnidx(stopper, len, *fmt);
	if (i < len) {
		info->sign = output[i];
		return RET_TOKEN_FOUND;
	}

	return RET_TOKEN_NEXT_STAGE;
}

static inline ret_token_t
check_alternative(const char *fmt, fmt_info_t *info)
{
	if (*fmt == '#') {
		info->alternate_form = true;
		return RET_TOKEN_FOUND;
	}

	return RET_TOKEN_NEXT_STAGE;
}

static inline ret_token_t
check_zeropadding(const char *fmt, fmt_info_t *info)
{
	if (*fmt == '0') {
		info->zero_padding = true;
		return RET_TOKEN_FOUND;
	}

	return RET_TOKEN_NEXT_STAGE;
}

// NOTE: Make sure current fmt is not the tail of the fmt, (not \0)
static inline ret_token_t
check_minwidth(const char *fmt, fmt_info_t *info)
{
	ret_token_t ret = RET_TOKEN_NEXT_STAGE;

	if (in_range(*fmt, '0', '9')) {
		if (info->minwidth_start == NULL) {
			info->minwidth_start = fmt;
		}

		// If the next is still digital, greedily consume them
		if (in_range(fmt[1], '0', '9')) {
			ret = RET_TOKEN_NEXT_CHAR;
		} else {
			ptrdiff_t len = fmt - info->minwidth_start;
			assert(len >= 0);
			info->min_width	     = atodec(info->minwidth_start,
						      ((size_t)len + 1U));
			info->minwidth_start = NULL;
			ret		     = RET_TOKEN_FOUND;
		}
	} else {
		ret		     = RET_TOKEN_NEXT_STAGE;
		info->minwidth_start = NULL;
	}

	return ret;
}

static inline ret_token_t
check_precise(const char *fmt, fmt_info_t *info)
{
	ret_token_t ret;

	if ((*fmt == '.') && in_range(fmt[1], '0', '9')) {
		info->precise_start = NULL;
		return RET_TOKEN_NEXT_CHAR;
	}

	if (in_range(*fmt, '0', '9')) {
		if (info->precise_start == NULL) {
			info->precise_start = fmt;
		}

		// If the next is still
		if (in_range(fmt[1], '0', '9')) {
			ret = RET_TOKEN_NEXT_CHAR;
		} else {
			ptrdiff_t len = fmt - info->precise_start;
			assert(len >= 0);
			info->precise =
				atodec(info->precise_start, ((size_t)len + 1U));
			info->precise_start = NULL;
			ret		    = RET_TOKEN_FOUND;
		}
	} else {
		ret		    = RET_TOKEN_NEXT_STAGE;
		info->precise_start = NULL;
	}

	return ret;
}

static inline ret_token_t
check_type(const char *fmt, fmt_info_t *info)
{
	const char	 stopper[] = { 'b', 'd', 'o', 'x', 's' };
	const var_type_t output[]  = {
		 VAR_TYPE_BIN,	   VAR_TYPE_DEC,    VAR_TYPE_OCTAL,
		 VAR_TYPE_LOW_HEX, VAR_TYPE_STRING,
	};
	const size_t len = util_array_size(stopper);
	index_t	     i	 = strnidx(stopper, len, *fmt);

	if (i < len) {
		info->type = output[i];
		return RET_TOKEN_FOUND;
	}

	return RET_TOKEN_ERROR;
}

static inline ret_token_t
check_end(const char *fmt, fmt_info_t *info)
{
	(void)info;

	// Ignore white space
	if (*fmt == ' ') {
		return RET_TOKEN_NEXT_CHAR;
	}

	if (*fmt == '}') {
		return RET_TOKEN_STOP;
	}

	return RET_TOKEN_ERROR;
}

static inline ret_token_t
check_token(count_t stage, const char *fmt, fmt_info_t *info)
{
	ret_token_t ret = RET_TOKEN_ERROR;

	switch (stage) {
	case STAGE_START:
		ret = check_start(fmt, info);
		break;
	case STAGE_SPECIFIER_START:
		ret = check_specifier_start(fmt, info);
		break;
	case STAGE_ALIGN:
		ret = check_align(fmt, info);
		break;
	case STAGE_SIGN:
		ret = check_sign(fmt, info);
		break;
	case STAGE_ALTERNATIVE:
		ret = check_alternative(fmt, info);
		break;
	case STAGE_ZERO_PADDING:
		ret = check_zeropadding(fmt, info);
		break;
	case STAGE_MINWIDTH:
		ret = check_minwidth(fmt, info);
		break;
	case STAGE_PRECISE:
		ret = check_precise(fmt, info);
		break;
	case STAGE_TYPE:
		ret = check_type(fmt, info);
		break;
	case STAGE_END:
		ret = check_end(fmt, info);
		break;
	default:
		// Nothing to do
		break;
	}

	return ret;
}

// Process to the next format descriptor, then construct the format information
// structure, return it and the length of character consumed in format string,
// and the literal character length, which can be directly copy to output
// buffer.
static inline error_t
get_next_fmt(const char *fmt, fmt_info_t *info, size_t *consumed_len,
	     size_t *literal_len, bool *end)
{
	index_t	    idx	      = 0U;
	error_t	    ret	      = OK;
	count_t	    stage     = STAGE_START;
	ret_token_t ret_check = RET_TOKEN_ERROR;

	while (fmt[idx] != '\0') {
		ret_check = check_token(stage, fmt + idx, info);

		switch (ret_check) {
		case RET_TOKEN_NEXT_CHAR:
			idx++;
			break;

		case RET_TOKEN_NEXT_STAGE:
			stage++;
			break;

		case RET_TOKEN_STOP:
			*consumed_len = (size_t)idx + 1U;
			return OK;

		case RET_TOKEN_FOUND:
			if (stage == STAGE_START) {
				*literal_len = idx;
			}

			idx++;
			stage++;
			break;

		case RET_TOKEN_ERROR:
		default:
			return ERROR_STRING_INVALID_FORMAT;
		}
	}

	if (fmt[idx] == '\0') {
		// Nothing found
		if (stage == STAGE_START) {
			*literal_len  = idx;
			*consumed_len = (size_t)idx + 1U;
		}
		*end = true;
	}

	return ret;
}

// Generate string based on argument & format information, write these
// characters into output buffer. If the size is bigger than buffer size,
// return STRING_TRUNCATED. The len should contain the actually bytes written in
// the output buffer.
static inline error_t
gen_str(char *buf, size_t size, fmt_info_t *info, register_t arg, size_t *len)
{
	error_t ret	  = OK;
	size_t	remaining = size;

	switch (info->type) {
	case VAR_TYPE_BIN:
		ret = itoa(buf, &remaining, (uint64_t)arg, 2, info, true);
		break;
	case VAR_TYPE_DEC:
		ret = sitoa(buf, &remaining, (int64_t)arg, 10, info);
		break;
	case VAR_TYPE_OCTAL:
		ret = itoa(buf, &remaining, (uint64_t)arg, 8, info, true);
		break;
	case VAR_TYPE_LOW_HEX:
		ret = itoa(buf, &remaining, (uint64_t)arg, 16, info, true);
		break;
	case VAR_TYPE_STRING:
		ret = stringtoa(buf, &remaining, (char *)arg, info);
		break;
	case VAR_TYPE_NONE:
	default:
		ret = ERROR_STRING_INVALID_FORMAT;
		break;
	}

	*len = size - remaining;
	return ret;
}

size_result_t
snprint(char *str, size_t size, const char *format, register_t arg0,
	register_t arg1, register_t arg2, register_t arg3, register_t arg4)
{
	const char *fmt = format;
	// Current buffer pointer, increasing while filling strings into
	char	  *buf		     = str;
	error_t	   ret		     = OK;
	size_t	   remaining	     = size - 1U; // space for terminating null
	register_t args[MAX_ARG_CNT] = { arg0, arg1, arg2, arg3, arg4 };
	index_t	   arg_idx	     = 0U;
	bool	   end		     = false;

	while (remaining != 0U) {
		fmt_info_t info = { 0 };
		size_t	   p;
		size_t	   consumed_len = 0U;
		size_t	   literal_len	= 0U;

		// Handle the next argument, return the format information and
		// the length consumed in the input format string
		ret = get_next_fmt(fmt, &info, &consumed_len, &literal_len,
				   &end);
		if (ret != OK) {
			break;
		}

		// Copy literal characters to output buffer
		p = util_min(literal_len, remaining);
		if (p > 0U) {
			(void)memcpy(buf, fmt, p);
		}

		fmt += consumed_len;
		buf += p;
		remaining -= p;

		// Not enough space for the fmt chars
		if ((remaining + p) < literal_len) {
			ret = ERROR_STRING_TRUNCATED;
			break;
		}

		if (info.type != VAR_TYPE_NONE) {
			if (arg_idx == MAX_ARG_CNT) {
				// Exceeded number of placeholders
				ret = ERROR_STRING_MISSING_PLACEHOLDER;
				break;
			}

			// Produce output for the current formatter substring
			ret = gen_str(buf, remaining, &info, args[arg_idx],
				      &consumed_len);
			if (ret == OK) {
				assert(consumed_len <= remaining);
				// Step output buffer
				buf = buf + consumed_len;
				remaining -= consumed_len;
				// Proceed to the next argument
				arg_idx++;
			}
		}

		// End processing on any error
		if (end || (ret != OK)) {
			break;
		}
	}

	// Add the terminator
	*buf = '\0';

	size_t written;
	if (ret == ERROR_STRING_TRUNCATED) {
		written = size;
	} else {
		written = (size - 1U) - remaining;
	}
	return (size_result_t){ .e = ret, .r = written };
}
