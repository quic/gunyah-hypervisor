// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef UNIT_TEST_H
#define UNIT_TEST_H

// Lightweight unit-test framework for host-side hypervisor tests.
//
// Usage pattern:
//
//   static void
//   test_foo(void)
//   {
//       TEST_BEGIN("foo");
//       TEST_ASSERT(some_fn() == OK, "some_fn returns OK");
//       TEST_END();
//   }
//
//   int main(void) {
//	 unit_test_prologue("Info Message");
//       test_foo();
//       return unit_test_epilogue();
//   }
//

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Per-binary test tracking counters
extern bool   test_failed;
extern size_t tests_passed;
extern size_t tests_failed;
extern size_t asserts_passed;
extern size_t asserts_failed;

// Mark the start of a named test case and reset the per-test failure flag.
#define TEST_BEGIN(name)                                                       \
	do {                                                                   \
		test_failed = false;                                           \
		printf("TEST: %s...\n", (name));                               \
	} while (0)

// Evaluate condition and record a pass or fail for the current test case.
// On failure the source location (file:line) is printed to aid debugging.
#define TEST_ASSERT(condition, message)                                        \
	do {                                                                   \
		if (condition) {                                               \
			printf("✓ PASS: %s\n", (message));                     \
			++asserts_passed;                                      \
		} else {                                                       \
			printf("✗ FAIL: %s (%s:%d)\n", (message), __FILE__,    \
			       __LINE__);                                      \
			++asserts_failed;                                      \
			test_failed = true;                                    \
		}                                                              \
	} while (0)

// Unconditionally record a failure for the current test case.
#define TEST_FAIL(message) TEST_ASSERT(false, (message))

// Mark the end of a test case and update the pass/fail test counters.
#define TEST_END()                                                             \
	do {                                                                   \
		if (test_failed) {                                             \
			printf("FAILED\n\n");                                  \
			++tests_failed;                                        \
		} else {                                                       \
			printf("PASSED\n\n");                                  \
			++tests_passed;                                        \
		}                                                              \
	} while (0)

// Print an opening unit test running banner
void
unit_test_prologue(const char *msg);

// Print a summary of all test results and return EXIT_SUCCESS or EXIT_FAILURE.
// Call once at the end of main().
int
unit_test_epilogue(void);

#endif // UNIT_TEST_H
