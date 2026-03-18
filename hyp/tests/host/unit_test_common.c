// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

//
// Common hots unit testing definitions
//

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "unit_test_common.h"

// Per-binary test tracking counters
bool   test_failed    = false;
size_t tests_passed   = 0U;
size_t tests_failed   = 0U;
size_t asserts_passed = 0U;
size_t asserts_failed = 0U;

void
unit_test_prologue(const char *msg)
{
	printf("=== %s ===\n\n", msg);
}

int
unit_test_epilogue(void)
{
	printf("=== Assert Summary ===\n");
	printf("Asserts passed: %zu\n", asserts_passed);
	printf("Asserts failed: %zu\n", asserts_failed);
	printf("Total:          %zu\n\n", asserts_passed + asserts_failed);

	printf("=== Test Summary ===\n");
	printf("Tests passed: %zu\n", tests_passed);
	printf("Tests failed: %zu\n", tests_failed);
	printf("Total:        %zu\n\n", tests_passed + tests_failed);

	if (asserts_failed == 0U) {
		printf("SUCCESS: All unit tests completed successfully!\n");
		return EXIT_SUCCESS;
	} else {
		printf("FAILURE: %zu assert(s) and %zu test(s) failed.\n",
		       asserts_failed, tests_failed);
		return EXIT_FAILURE;
	}
}
