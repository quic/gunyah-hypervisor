// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <abort.h>
#include <autodt_helpers.h>
#include <compiler.h>
#include <dt.h>
#include <fdt.h>
#include <fdt_helpers.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <trace.h>
#include <util.h>

static dt_autodt_ctx_t *dt_autodt_ctx;

dt_autodt_ctx_t *
dt_autodt_create_context(void)
{
	assert(dt_autodt_ctx == NULL);
	partition_t *partition = partition_get_private();

	void_ptr_result_t alloc_r = partition_alloc(
		partition, sizeof(*dt_autodt_ctx), alignof(*dt_autodt_ctx));
	if (alloc_r.e != OK) {
		panic("autodt: failed to allocate context");
	}

	memset_s(alloc_r.r, sizeof(*dt_autodt_ctx), 0, sizeof(*dt_autodt_ctx));
	dt_autodt_ctx = (dt_autodt_ctx_t *)alloc_r.r;

	return dt_autodt_ctx;
}

dt_autodt_ctx_t *
dt_autodt_get_context(void)
{
	assert(dt_autodt_ctx != NULL);
	return dt_autodt_ctx;
}

void
dt_auto_destroy_context(void)
{
	assert(dt_autodt_ctx != NULL);
	partition_t *partition = partition_get_private();

	partition_free(partition, dt_autodt_ctx, sizeof(*dt_autodt_ctx));
}

const char **
dt_auto_type_stringlist_load(const char *sl, count_t sl_size, count_t *len_out)
{
	dt_autodt_ctx_t *ctx = dt_autodt_get_context();

	const char **ret	= ctx->string_pool + ctx->string_pool_size;
	count_t	     iterations = 0U;

	count_t sl_ctx = 0U;
	while (1) {
		const char *s = fdt_iter_stringlist(sl, sl_size, &sl_ctx);
		if (s == NULL) {
			break;
		}

		if (ctx->string_pool_size >=
		    util_array_size(ctx->string_pool)) {
			panic("autodt: exhausted string ctx->string_pool");
		}

		ctx->string_pool[ctx->string_pool_size] = s;
		ctx->string_pool_size++;
		iterations++;
	}

	*len_out = iterations;
	return ret;
}
