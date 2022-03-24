// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// This file documents the permitted function, type and variable attributes, and
// defines short names for them. Do not use any attribute specifier unless it is
// listed below.
//
// This file is automatically included in all source files; this is done with
// -imacro, so this file must not contain any non-preprocessor declarations.
//
// This rule does not apply to language constructs that have an effect that is
// similar or equivalent to an attribute, such as _Noreturn or _Alignas.

// Don't inline the function. This is used to mark cold functions for which
// inlining would be a waste of space and/or would make debugging inconvenient.
#define NOINLINE __attribute__((noinline))

// Always inline the function. This is used for certain inline assembler
// wrappers which cannot safely be wrapped in a function call, such as
// load-exclusive instructions which might lose their exclusivity.
#define ALWAYS_INLINE __attribute__((always_inline))

// Declare or define a function as weakly linked.
//
// If placed on a definition, this creates a weak definition, which can be
// overridden by a non-weak definition in another object.
//
// If placed on a declaration, this causes calls to the function to be linked as
// weak references, which do not require a definition to exist at all. The
// behaviour of calling an undefined function is unspecified, so this usage
// is not recommended.
#define WEAK __attribute__((weak))

//
// Clang thread safety analysis attributes.
//

// Declare a type as representing a thread-safety capability (in Clang SA terms;
// this is unrelated to the Gunyah API's object capabilities).
//
// Note that calling this "lockable" is misleading because it is not necessarily
// exclusive, but that's better than overloading "capability".
//
// The name is a human-readable string used in error messages.
#define LOCKABLE(name) __attribute__((capability(name)))

// Declare a variable as being protected by a specific lock.
#define PROTECTED_BY(lock) __attribute__((guarded_by(lock)))

// Declare a variable as being protected by a specific lock.
#define PTR_PROTECTED_BY(lock) __attribute__((pt_guarded_by(lock)))

// Declare a function as acquiring a specified lock object.
#define ACQUIRE_LOCK(lock) __attribute__((acquire_capability(lock)))

// Declare a function as acquiring a specified lock object if it returns a
// specified value.
#define TRY_ACQUIRE_LOCK(success, lock)                                        \
	__attribute__((try_acquire_capability(success, lock)))

// Declare a function as releasing a specified lock object.
#define RELEASE_LOCK(lock) __attribute__((release_capability(lock)))

// Declare a function as requiring a lock to be held.
#define REQUIRE_LOCK(lock) __attribute__((requires_capability(lock)))

// Declare a function as requiring a lock to _not_ be held.
#define EXCLUDE_LOCK(lock) __attribute__((requires_capability(!&(lock))))

// Declare a function as acquiring a shared reader lock.
#define ACQUIRE_READ(lock) __attribute__((acquire_shared_capability(lock)))

// Declare a function as acquiring a specified shared read lock if it
// returns a specified value.
#define TRY_ACQUIRE_READ(success, lock)                                        \
	__attribute__((try_acquire_shared_capability(success, lock)))

// Declare a function as releasing a shared reader lock.
#define RELEASE_READ(lock) __attribute__((release_shared_capability(lock)))

// Declare a function as requiring a shared reader lock to be held.
#define REQUIRE_READ(lock) __attribute__((requires_shared_capability(lock)))

// Declare a function as requiring a shared reader lock to _not_ be held.
#define EXCLUDE_READ(lock) __attribute__((locks_excluded(lock)))

// Define a function that implements a lock acquire or release. This disables
// checking of thread safety throughout the function, so the function should
// ideally do nothing other than implement the lock, hence the name.
//
// This attribute can also disable analysis in a function that is too complex
// for the analyser to understand; e.g. one that has conditional locking. Using
// it for this purpose so is strongly deprecated.
#define LOCK_IMPL __attribute__((no_thread_safety_analysis))
