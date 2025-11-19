// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef HYPREGISTERS_H_
#define HYPREGISTERS_H_
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"

#define register_TEST_COMB_feat_var_read_ordered_raw(val)                              \
	__asm__(".arch_extension fp; mrs %0, TEST_COMB_feat_var; .arch_extension nofp" \
		: "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline TEST_COMB_feat_var_VAR1_t
register_TEST_COMB_feat_var_VAR1_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_COMB_feat_var as type
	// TEST_COMB_feat_var_VAR1_t
	register_TEST_COMB_feat_var_read_ordered_raw(raw);
	return TEST_COMB_feat_var_VAR1_cast((uint64_t)raw);
}

#define register_TEST_COMB_feat_var_write_ordered_raw(val)                               \
	__asm__ volatile(                                                                \
		".arch_extension fp; msr TEST_COMB_feat_var, %[r]; .arch_extension nofp" \
		: "+m"(*ordering_var)                                                    \
		: [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_COMB_feat_var_VAR1_write_ordered(
	const TEST_COMB_feat_var_VAR1_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)TEST_COMB_feat_var_VAR1_raw(val);
	// Write ordered register TEST_COMB_feat_var as type
	// TEST_COMB_feat_var_VAR1_t
	register_TEST_COMB_feat_var_write_ordered_raw(raw);
}

ALWAYS_INLINE static inline TEST_COMB_feat_var_VAR2_t
register_TEST_COMB_feat_var_VAR2_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_COMB_feat_var as type
	// TEST_COMB_feat_var_VAR2_t
	register_TEST_COMB_feat_var_read_ordered_raw(raw);
	return TEST_COMB_feat_var_VAR2_cast((uint64_t)raw);
}

ALWAYS_INLINE static inline void
register_TEST_COMB_feat_var_VAR2_write_ordered(
	const TEST_COMB_feat_var_VAR2_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)TEST_COMB_feat_var_VAR2_raw(val);
	// Write ordered register TEST_COMB_feat_var as type
	// TEST_COMB_feat_var_VAR2_t
	register_TEST_COMB_feat_var_write_ordered_raw(raw);
}

#define register_TEST_COMB_multi_over_read_volatile_ordered_raw(val)                                                                 \
	__asm__ volatile(                                                                                                            \
		".arch_extension fp; .arch_extension sve; mrs %0, TEST_COMB_multi_over; .arch_extension nofp; .arch_extension nosve" \
		: "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline TEST_COMB_multi_over_VAR1_t
register_TEST_COMB_multi_over_VAR1_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_COMB_multi_over as type
	// TEST_COMB_multi_over_VAR1_t
	register_TEST_COMB_multi_over_read_volatile_ordered_raw(raw);
	return TEST_COMB_multi_over_VAR1_cast((uint64_t)raw);
}

ALWAYS_INLINE static inline TEST_COMB_multi_over_VAR2_t
register_TEST_COMB_multi_over_VAR2_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_COMB_multi_over as type
	// TEST_COMB_multi_over_VAR2_t
	register_TEST_COMB_multi_over_read_volatile_ordered_raw(raw);
	return TEST_COMB_multi_over_VAR2_cast((uint64_t)raw);
}

#define register_TEST_DEFAULT_feat_read_raw(val)                                      \
	__asm__(".arch_extension fp; mrs %0, TEST_DEFAULT_feat; .arch_extension nofp" \
		: "=r"(val))

ALWAYS_INLINE static inline TEST_DEFAULT_feat_t
register_TEST_DEFAULT_feat_read(void)
{
	register_t raw;
	// Read register TEST_DEFAULT_feat as type TEST_DEFAULT_feat_t
	register_TEST_DEFAULT_feat_read_raw(raw);
	return TEST_DEFAULT_feat_cast((uint64_t)raw);
}

#define register_TEST_DEFAULT_feat_write_raw(val)                                       \
	__asm__ volatile(                                                               \
		".arch_extension fp; msr TEST_DEFAULT_feat, %[r]; .arch_extension nofp" \
		:                                                                       \
		: [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_DEFAULT_feat_write(const TEST_DEFAULT_feat_t val)
{
	register_t raw = (register_t)TEST_DEFAULT_feat_raw(val);
	// Write register TEST_DEFAULT_feat as type TEST_DEFAULT_feat_t
	register_TEST_DEFAULT_feat_write_raw(raw);
}

#define register_TEST_DEFAULT_plain_read_raw(val)                              \
	__asm__("mrs %0, TEST_DEFAULT_plain" : "=r"(val))

ALWAYS_INLINE static inline TEST_DEFAULT_plain_t
register_TEST_DEFAULT_plain_read(void)
{
	register_t raw;
	// Read register TEST_DEFAULT_plain as type TEST_DEFAULT_plain_t
	register_TEST_DEFAULT_plain_read_raw(raw);
	return TEST_DEFAULT_plain_cast((uint64_t)raw);
}

#define register_TEST_DEFAULT_plain_write_raw(val)                             \
	__asm__ volatile("msr TEST_DEFAULT_plain, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_DEFAULT_plain_write(const TEST_DEFAULT_plain_t val)
{
	register_t raw = (register_t)TEST_DEFAULT_plain_raw(val);
	// Write register TEST_DEFAULT_plain as type TEST_DEFAULT_plain_t
	register_TEST_DEFAULT_plain_write_raw(raw);
}

#define register_TEST_DEFAULT_type_read_raw(val)                               \
	__asm__("mrs %0, TEST_DEFAULT_type" : "=r"(val))

ALWAYS_INLINE static inline uint32_t
register_TEST_DEFAULT_type_read(void)
{
	register_t raw;
	// Read register TEST_DEFAULT_type as type uint32_t
	register_TEST_DEFAULT_type_read_raw(raw);
	return (uint32_t)(raw);
}

#define register_TEST_DEFAULT_type_write_raw(val)                              \
	__asm__ volatile("msr TEST_DEFAULT_type, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_DEFAULT_type_write(const uint32_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_DEFAULT_type as type uint32_t
	register_TEST_DEFAULT_type_write_raw(raw);
}

#define register_TEST_FEAT_multi_read_raw(val)                                                                                  \
	__asm__(".arch_extension fp; .arch_extension sve; mrs %0, TEST_FEAT_multi; .arch_extension nofp; .arch_extension nosve" \
		: "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_FEAT_multi_read(void)
{
	register_t raw;
	// Read register TEST_FEAT_multi as type uint64_t
	register_TEST_FEAT_multi_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_FEAT_multi_write_raw(val)                                                                                   \
	__asm__ volatile(                                                                                                         \
		".arch_extension fp; .arch_extension sve; msr TEST_FEAT_multi, %[r]; .arch_extension nofp; .arch_extension nosve" \
		:                                                                                                                 \
		: [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_FEAT_multi_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_FEAT_multi as type uint64_t
	register_TEST_FEAT_multi_write_raw(raw);
}

#define register_TEST_FEAT_type_read_ordered_raw(val)                              \
	__asm__(".arch_extension fp; mrs %0, TEST_FEAT_type; .arch_extension nofp" \
		: "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_FEAT_type_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_FEAT_type as type uint64_t
	register_TEST_FEAT_type_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_FEAT_type_write_ordered_raw(val)                               \
	__asm__ volatile(                                                            \
		".arch_extension fp; msr TEST_FEAT_type, %[r]; .arch_extension nofp" \
		: "+m"(*ordering_var)                                                \
		: [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_FEAT_type_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_FEAT_type as type uint64_t
	register_TEST_FEAT_type_write_ordered_raw(raw);
}

#define register_TEST_REG_O_read_ordered_raw(val)                              \
	__asm__("mrs %0, TEST_REG_O" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_O_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_O as type uint64_t
	register_TEST_REG_O_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_O_write_ordered_raw(val)                             \
	__asm__ volatile("msr TEST_REG_O, %[r]"                                \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_O_write_ordered(const uint64_t	val,
				  asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_O as type uint64_t
	register_TEST_REG_O_write_ordered_raw(raw);
}

#define register_TEST_REG_OR_read_volatile_ordered_raw(val)                    \
	__asm__ volatile("mrs %0, TEST_REG_OR" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OR as type uint64_t
	register_TEST_REG_OR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_ORW_read_volatile_ordered_raw(val)                   \
	__asm__ volatile("mrs %0, TEST_REG_ORW"                                \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_ORW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_ORW as type uint64_t
	register_TEST_REG_ORW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_ORW_write_barrier_ordered_raw(val)                   \
	__asm__ volatile("msr TEST_REG_ORW, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_ORW_write_barrier_ordered(const uint64_t	  val,
					    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_ORW as type uint64_t
	register_TEST_REG_ORW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_ORWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_ORWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_ORWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_ORWm as type uint64_t
	register_TEST_REG_ORWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_ORWm_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_ORWm"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_ORWm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_ORWm as type uint64_t
	register_TEST_REG_ORWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_ORWm_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_ORWm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_ORWm_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_ORWm as type uint64_t
	register_TEST_REG_ORWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_ORm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_ORm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_ORm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_ORm as type uint64_t
	register_TEST_REG_ORm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_ORm_read_volatile_ordered_raw(val)                   \
	__asm__ volatile("mrs %0, TEST_REG_ORm"                                \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_ORm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_ORm as type uint64_t
	register_TEST_REG_ORm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OW_write_barrier_ordered_raw(val)                    \
	__asm__ volatile("msr TEST_REG_OW, %[r]"                               \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OW_write_barrier_ordered(const uint64_t	 val,
					   asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OW as type uint64_t
	register_TEST_REG_OW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OWm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_OWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OWm as type uint64_t
	register_TEST_REG_OWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OWm_write_barrier_ordered_raw(val)                   \
	__asm__ volatile("msr TEST_REG_OWm, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OWm_write_barrier_ordered(const uint64_t	  val,
					    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OWm as type uint64_t
	register_TEST_REG_OWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_Om_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_Om" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Om_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Om as type uint64_t
	register_TEST_REG_Om_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Or_read_ordered_raw(val)                             \
	__asm__("mrs %0, TEST_REG_Or" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Or_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_Or as type uint64_t
	register_TEST_REG_Or_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrR_read_ordered_raw(val)                            \
	__asm__("mrs %0, TEST_REG_OrR" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrR_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrR as type uint64_t
	register_TEST_REG_OrR_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrR_read_volatile_ordered_raw(val)                   \
	__asm__ volatile("mrs %0, TEST_REG_OrR"                                \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrR as type uint64_t
	register_TEST_REG_OrR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRW_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrRW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrRW as type uint64_t
	register_TEST_REG_OrRW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRW_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_OrRW"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrRW as type uint64_t
	register_TEST_REG_OrRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_OrRW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrRW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrRW as type uint64_t
	register_TEST_REG_OrRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrRWm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrRWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrRWm as type uint64_t
	register_TEST_REG_OrRWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrRWm as type uint64_t
	register_TEST_REG_OrRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRWm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_OrRWm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRWm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrRWm as type uint64_t
	register_TEST_REG_OrRWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_OrRWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrRWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrRWm as type uint64_t
	register_TEST_REG_OrRWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrRm_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrRm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrRm as type uint64_t
	register_TEST_REG_OrRm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrRm as type uint64_t
	register_TEST_REG_OrRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrRm_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_OrRm"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrRm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrRm as type uint64_t
	register_TEST_REG_OrRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrW_read_ordered_raw(val)                            \
	__asm__("mrs %0, TEST_REG_OrW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrW as type uint64_t
	register_TEST_REG_OrW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrW_write_barrier_ordered_raw(val)                   \
	__asm__ volatile("msr TEST_REG_OrW, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrW_write_barrier_ordered(const uint64_t	  val,
					    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrW as type uint64_t
	register_TEST_REG_OrW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrWm_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrWm as type uint64_t
	register_TEST_REG_OrWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrWm as type uint64_t
	register_TEST_REG_OrWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrWm_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_OrWm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrWm_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrWm as type uint64_t
	register_TEST_REG_OrWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_Orm_read_ordered_raw(val)                            \
	__asm__("mrs %0, TEST_REG_Orm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Orm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_Orm as type uint64_t
	register_TEST_REG_Orm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Orm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_Orm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Orm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Orm as type uint64_t
	register_TEST_REG_Orm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Orw_read_ordered_raw(val)                            \
	__asm__("mrs %0, TEST_REG_Orw" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Orw_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_Orw as type uint64_t
	register_TEST_REG_Orw_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Orw_write_ordered_raw(val)                           \
	__asm__ volatile("msr TEST_REG_Orw, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_Orw_write_ordered(const uint64_t	  val,
				    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_Orw as type uint64_t
	register_TEST_REG_Orw_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwR_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrwR" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwR_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwR as type uint64_t
	register_TEST_REG_OrwR_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwR_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_OrwR"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrwR as type uint64_t
	register_TEST_REG_OrwR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwR_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_OrwR, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwR_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwR as type uint64_t
	register_TEST_REG_OrwR_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwRW_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrwRW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwRW as type uint64_t
	register_TEST_REG_OrwRW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRW_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_OrwRW"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrwRW as type uint64_t
	register_TEST_REG_OrwRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRW_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_OrwRW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwRW_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwRW as type uint64_t
	register_TEST_REG_OrwRW_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwRW_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_OrwRW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrwRW_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrwRW as type uint64_t
	register_TEST_REG_OrwRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrwRWm_read_ordered_raw(val)                         \
	__asm__("mrs %0, TEST_REG_OrwRWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwRWm as type uint64_t
	register_TEST_REG_OrwRWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRWm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_OrwRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrwRWm as type uint64_t
	register_TEST_REG_OrwRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRWm_read_volatile_ordered_raw(val)                \
	__asm__ volatile("mrs %0, TEST_REG_OrwRWm"                             \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRWm_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrwRWm as type uint64_t
	register_TEST_REG_OrwRWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRWm_write_ordered_raw(val)                        \
	__asm__ volatile("msr TEST_REG_OrwRWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwRWm_write_ordered(const uint64_t	     val,
				       asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwRWm as type uint64_t
	register_TEST_REG_OrwRWm_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwRWm_write_barrier_ordered_raw(val)                \
	__asm__ volatile("msr TEST_REG_OrwRWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrwRWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrwRWm as type
	// uint64_t
	register_TEST_REG_OrwRWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrwRm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrwRm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwRm as type uint64_t
	register_TEST_REG_OrwRm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrwRm as type uint64_t
	register_TEST_REG_OrwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_OrwRm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwRm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OrwRm as type uint64_t
	register_TEST_REG_OrwRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwRm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_OrwRm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwRm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwRm as type uint64_t
	register_TEST_REG_OrwRm_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwW_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OrwW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwW as type uint64_t
	register_TEST_REG_OrwW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwW_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_OrwW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwW_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwW as type uint64_t
	register_TEST_REG_OrwW_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_OrwW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrwW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrwW as type uint64_t
	register_TEST_REG_OrwW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OrwWm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrwWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_OrwWm as type uint64_t
	register_TEST_REG_OrwWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OrwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OrwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OrwWm as type uint64_t
	register_TEST_REG_OrwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OrwWm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_OrwWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OrwWm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OrwWm as type uint64_t
	register_TEST_REG_OrwWm_write_ordered_raw(raw);
}

#define register_TEST_REG_OrwWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_OrwWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OrwWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OrwWm as type uint64_t
	register_TEST_REG_OrwWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_Orwm_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_Orwm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Orwm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_Orwm as type uint64_t
	register_TEST_REG_Orwm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Orwm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_Orwm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Orwm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Orwm as type uint64_t
	register_TEST_REG_Orwm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Orwm_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_Orwm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_Orwm_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_Orwm as type uint64_t
	register_TEST_REG_Orwm_write_ordered_raw(raw);
}

#define register_TEST_REG_Ow_write_ordered_raw(val)                            \
	__asm__ volatile("msr TEST_REG_Ow, %[r]"                               \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_Ow_write_ordered(const uint64_t	 val,
				   asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_Ow as type uint64_t
	register_TEST_REG_Ow_write_ordered_raw(raw);
}

#define register_TEST_REG_OwR_read_volatile_ordered_raw(val)                   \
	__asm__ volatile("mrs %0, TEST_REG_OwR"                                \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OwR as type uint64_t
	register_TEST_REG_OwR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwR_write_ordered_raw(val)                           \
	__asm__ volatile("msr TEST_REG_OwR, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwR_write_ordered(const uint64_t	  val,
				    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwR as type uint64_t
	register_TEST_REG_OwR_write_ordered_raw(raw);
}

#define register_TEST_REG_OwRW_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_OwRW"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwRW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OwRW as type uint64_t
	register_TEST_REG_OwRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwRW_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_OwRW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwRW_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwRW as type uint64_t
	register_TEST_REG_OwRW_write_ordered_raw(raw);
}

#define register_TEST_REG_OwRW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_OwRW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OwRW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OwRW as type uint64_t
	register_TEST_REG_OwRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OwRWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_OwRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OwRWm as type uint64_t
	register_TEST_REG_OwRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwRWm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_OwRWm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwRWm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OwRWm as type uint64_t
	register_TEST_REG_OwRWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwRWm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_OwRWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwRWm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwRWm as type uint64_t
	register_TEST_REG_OwRWm_write_ordered_raw(raw);
}

#define register_TEST_REG_OwRWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_OwRWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OwRWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OwRWm as type uint64_t
	register_TEST_REG_OwRWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OwRm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OwRm as type uint64_t
	register_TEST_REG_OwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwRm_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_OwRm"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwRm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_OwRm as type uint64_t
	register_TEST_REG_OwRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwRm_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_OwRm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwRm_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwRm as type uint64_t
	register_TEST_REG_OwRm_write_ordered_raw(raw);
}

#define register_TEST_REG_OwW_write_ordered_raw(val)                           \
	__asm__ volatile("msr TEST_REG_OwW, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwW_write_ordered(const uint64_t	  val,
				    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwW as type uint64_t
	register_TEST_REG_OwW_write_ordered_raw(raw);
}

#define register_TEST_REG_OwW_write_barrier_ordered_raw(val)                   \
	__asm__ volatile("msr TEST_REG_OwW, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OwW_write_barrier_ordered(const uint64_t	  val,
					    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OwW as type uint64_t
	register_TEST_REG_OwW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_OwWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_OwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_OwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_OwWm as type uint64_t
	register_TEST_REG_OwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_OwWm_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_OwWm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_OwWm_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_OwWm as type uint64_t
	register_TEST_REG_OwWm_write_ordered_raw(raw);
}

#define register_TEST_REG_OwWm_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_OwWm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_OwWm_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_OwWm as type uint64_t
	register_TEST_REG_OwWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_Owm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_Owm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Owm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Owm as type uint64_t
	register_TEST_REG_Owm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Owm_write_ordered_raw(val)                           \
	__asm__ volatile("msr TEST_REG_Owm, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_Owm_write_ordered(const uint64_t	  val,
				    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_Owm as type uint64_t
	register_TEST_REG_Owm_write_ordered_raw(raw);
}

#define register_TEST_REG_R_read_volatile_raw(val)                             \
	__asm__ volatile("mrs %0, TEST_REG_R" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_R_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_R as type uint64_t
	register_TEST_REG_R_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_RW_read_volatile_raw(val)                            \
	__asm__ volatile("mrs %0, TEST_REG_RW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_RW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_RW as type uint64_t
	register_TEST_REG_RW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_RW_write_barrier_raw(val)                            \
	__asm__ volatile("msr TEST_REG_RW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_RW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_RW as type uint64_t
	register_TEST_REG_RW_write_barrier_raw(raw);
}

#define register_TEST_REG_RWm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_RWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_RWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_RWm as type uint64_t
	register_TEST_REG_RWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_RWm_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_RWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_RWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_RWm as type uint64_t
	register_TEST_REG_RWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_RWm_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_RWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_RWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_RWm as type uint64_t
	register_TEST_REG_RWm_write_barrier_raw(raw);
}

#define register_TEST_REG_Rm_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_Rm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Rm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Rm as type uint64_t
	register_TEST_REG_Rm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Rm_read_volatile_raw(val)                            \
	__asm__ volatile("mrs %0, TEST_REG_Rm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Rm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_Rm as type uint64_t
	register_TEST_REG_Rm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_W_write_barrier_raw(val)                             \
	__asm__ volatile("msr TEST_REG_W, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_W_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_W as type uint64_t
	register_TEST_REG_W_write_barrier_raw(raw);
}

#define register_TEST_REG_Wm_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_Wm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_Wm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_Wm as type uint64_t
	register_TEST_REG_Wm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_Wm_write_barrier_raw(val)                            \
	__asm__ volatile("msr TEST_REG_Wm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_Wm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_Wm as type uint64_t
	register_TEST_REG_Wm_write_barrier_raw(raw);
}

#define register_TEST_REG_m_read_barrier_raw(val)                              \
	__asm__("mrs %0, TEST_REG_m" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_m_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_m as type uint64_t
	register_TEST_REG_m_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_o_read_raw(val)                                      \
	__asm__("mrs %0, TEST_REG_o" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_o_read(void)
{
	register_t raw;
	// Read register TEST_REG_o as type uint64_t
	register_TEST_REG_o_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_o_write_raw(val)                                     \
	__asm__ volatile("msr TEST_REG_o, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_o_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_o as type uint64_t
	register_TEST_REG_o_write_raw(raw);
}

#define register_TEST_REG_oOR_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_oOR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOR as type uint64_t
	register_TEST_REG_oOR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOR_read_volatile_ordered_raw(val)                   \
	__asm__ volatile("mrs %0, TEST_REG_oOR"                                \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOR as type uint64_t
	register_TEST_REG_oOR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORW_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_oORW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oORW as type uint64_t
	register_TEST_REG_oORW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORW_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_oORW"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oORW as type uint64_t
	register_TEST_REG_oORW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oORW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oORW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oORW as type uint64_t
	register_TEST_REG_oORW_write_barrier_raw(raw);
}

#define register_TEST_REG_oORW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_oORW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oORW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oORW as type uint64_t
	register_TEST_REG_oORW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oORWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oORWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oORWm as type uint64_t
	register_TEST_REG_oORWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORWm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oORWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oORWm as type uint64_t
	register_TEST_REG_oORWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORWm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oORWm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORWm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oORWm as type uint64_t
	register_TEST_REG_oORWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oORWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oORWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oORWm as type uint64_t
	register_TEST_REG_oORWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oORWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oORWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oORWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oORWm as type uint64_t
	register_TEST_REG_oORWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oORm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oORm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oORm as type uint64_t
	register_TEST_REG_oORm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_oORm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oORm as type uint64_t
	register_TEST_REG_oORm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oORm_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_oORm"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oORm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oORm as type uint64_t
	register_TEST_REG_oORm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_oOW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOW as type uint64_t
	register_TEST_REG_oOW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOW_write_barrier_ordered_raw(val)                   \
	__asm__ volatile("msr TEST_REG_oOW, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOW_write_barrier_ordered(const uint64_t	  val,
					    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOW as type uint64_t
	register_TEST_REG_oOW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOWm as type uint64_t
	register_TEST_REG_oOWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOWm as type uint64_t
	register_TEST_REG_oOWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOWm_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_oOWm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOWm_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOWm as type uint64_t
	register_TEST_REG_oOWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_oOm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOm as type uint64_t
	register_TEST_REG_oOm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOr_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_oOr" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOr_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOr as type uint64_t
	register_TEST_REG_oOr_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOr_read_ordered_raw(val)                            \
	__asm__("mrs %0, TEST_REG_oOr" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOr_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOr as type uint64_t
	register_TEST_REG_oOr_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrR_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_oOrR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrR_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrR as type uint64_t
	register_TEST_REG_oOrR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrR_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOrR" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrR_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrR as type uint64_t
	register_TEST_REG_oOrR_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrR_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_oOrR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrR as type uint64_t
	register_TEST_REG_oOrR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrR_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_oOrR"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrR as type uint64_t
	register_TEST_REG_oOrR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRW_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRW_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrRW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRW_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oOrRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRW_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oOrRW"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRW_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrRW, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrRW_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oOrRW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrRW_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrRW as type uint64_t
	register_TEST_REG_oOrRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrRWm_read_raw(val)                                 \
	__asm__("mrs %0, TEST_REG_oOrRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRWm_read_ordered_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrRWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRWm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRWm_read_volatile_raw(val)                        \
	__asm__ volatile("mrs %0, TEST_REG_oOrRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRWm_read_volatile_ordered_raw(val)                \
	__asm__ volatile("mrs %0, TEST_REG_oOrRWm"                             \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRWm_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRWm_write_barrier_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrRWm, %[r]"                           \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrRWm as type uint64_t
	register_TEST_REG_oOrRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrRWm_write_barrier_ordered_raw(val)                \
	__asm__ volatile("msr TEST_REG_oOrRWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrRWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrRWm as type
	// uint64_t
	register_TEST_REG_oOrRWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrRm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrRm as type uint64_t
	register_TEST_REG_oOrRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrRm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrRm as type uint64_t
	register_TEST_REG_oOrRm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrRm as type uint64_t
	register_TEST_REG_oOrRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oOrRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrRm as type uint64_t
	register_TEST_REG_oOrRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrRm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oOrRm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrRm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrRm as type uint64_t
	register_TEST_REG_oOrRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrW_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_oOrW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrW_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrW as type uint64_t
	register_TEST_REG_oOrW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrW_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOrW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrW as type uint64_t
	register_TEST_REG_oOrW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOrW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrW as type uint64_t
	register_TEST_REG_oOrW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_oOrW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrW as type uint64_t
	register_TEST_REG_oOrW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrWm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrWm as type uint64_t
	register_TEST_REG_oOrWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrWm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrWm as type uint64_t
	register_TEST_REG_oOrWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrWm as type uint64_t
	register_TEST_REG_oOrWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrWm as type uint64_t
	register_TEST_REG_oOrWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oOrWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrWm as type uint64_t
	register_TEST_REG_oOrWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_oOrm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrm as type uint64_t
	register_TEST_REG_oOrm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrm_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOrm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrm as type uint64_t
	register_TEST_REG_oOrm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOrm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrm as type uint64_t
	register_TEST_REG_oOrm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrw_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_oOrw" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrw_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrw as type uint64_t
	register_TEST_REG_oOrw_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrw_read_ordered_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOrw" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrw_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrw as type uint64_t
	register_TEST_REG_oOrw_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrw_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_oOrw, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrw_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrw as type uint64_t
	register_TEST_REG_oOrw_write_raw(raw);
}

#define register_TEST_REG_oOrw_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOrw, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrw_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrw as type uint64_t
	register_TEST_REG_oOrw_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwR_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwR_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwR_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrwR" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwR_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwR_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oOrwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwR_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oOrwR"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwR_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOrwR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_write_raw(raw);
}

#define register_TEST_REG_oOrwR_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrwR, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwR_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwR as type uint64_t
	register_TEST_REG_oOrwR_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwRW_read_raw(val)                                 \
	__asm__("mrs %0, TEST_REG_oOrwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRW_read_ordered_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrwRW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRW_read_volatile_raw(val)                        \
	__asm__ volatile("mrs %0, TEST_REG_oOrwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRW_read_volatile_ordered_raw(val)                \
	__asm__ volatile("mrs %0, TEST_REG_oOrwRW"                             \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRW_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRW_write_raw(val)                                \
	__asm__ volatile("msr TEST_REG_oOrwRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_write_raw(raw);
}

#define register_TEST_REG_oOrwRW_write_ordered_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrwRW, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRW_write_ordered(const uint64_t	     val,
				       asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwRW_write_barrier_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrwRW, %[r]"                           \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrwRW as type uint64_t
	register_TEST_REG_oOrwRW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrwRW_write_barrier_ordered_raw(val)                \
	__asm__ volatile("msr TEST_REG_oOrwRW, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRW_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrwRW as type
	// uint64_t
	register_TEST_REG_oOrwRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrwRm_read_raw(val)                                 \
	__asm__("mrs %0, TEST_REG_oOrwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRm_read_ordered_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrwRm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRm_read_volatile_raw(val)                        \
	__asm__ volatile("mrs %0, TEST_REG_oOrwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRm_read_volatile_ordered_raw(val)                \
	__asm__ volatile("mrs %0, TEST_REG_oOrwRm"                             \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwRm_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwRm_write_raw(val)                                \
	__asm__ volatile("msr TEST_REG_oOrwRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_write_raw(raw);
}

#define register_TEST_REG_oOrwRm_write_ordered_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrwRm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwRm_write_ordered(const uint64_t	     val,
				       asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwRm as type uint64_t
	register_TEST_REG_oOrwRm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwW_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrwW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwW_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwW_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrwW" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwW_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwW_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOrwW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_write_raw(raw);
}

#define register_TEST_REG_oOrwW_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrwW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwW_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwW_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrwW, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrwW_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oOrwW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwW_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrwW as type uint64_t
	register_TEST_REG_oOrwW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrwWm_read_raw(val)                                 \
	__asm__("mrs %0, TEST_REG_oOrwWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwWm_read_ordered_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrwWm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwWm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwWm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOrwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwWm_write_raw(val)                                \
	__asm__ volatile("msr TEST_REG_oOrwWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_write_raw(raw);
}

#define register_TEST_REG_oOrwWm_write_ordered_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrwWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwWm_write_ordered(const uint64_t	     val,
				       asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOrwWm_write_barrier_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOrwWm, %[r]"                           \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOrwWm as type uint64_t
	register_TEST_REG_oOrwWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOrwWm_write_barrier_ordered_raw(val)                \
	__asm__ volatile("msr TEST_REG_oOrwWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOrwWm as type
	// uint64_t
	register_TEST_REG_oOrwWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOrwm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_oOrwm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwm_read(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwm as type uint64_t
	register_TEST_REG_oOrwm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwm_read_ordered_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrwm" : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwm_read_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read ordered register TEST_REG_oOrwm as type uint64_t
	register_TEST_REG_oOrwm_read_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOrwm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOrwm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOrwm as type uint64_t
	register_TEST_REG_oOrwm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOrwm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOrwm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOrwm as type uint64_t
	register_TEST_REG_oOrwm_write_raw(raw);
}

#define register_TEST_REG_oOrwm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOrwm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOrwm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOrwm as type uint64_t
	register_TEST_REG_oOrwm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOw_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_oOw, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOw_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOw as type uint64_t
	register_TEST_REG_oOw_write_raw(raw);
}

#define register_TEST_REG_oOw_write_ordered_raw(val)                           \
	__asm__ volatile("msr TEST_REG_oOw, %[r]"                              \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOw_write_ordered(const uint64_t	  val,
				    asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOw as type uint64_t
	register_TEST_REG_oOw_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwR_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_oOwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOwR as type uint64_t
	register_TEST_REG_oOwR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwR_read_volatile_ordered_raw(val)                  \
	__asm__ volatile("mrs %0, TEST_REG_oOwR"                               \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwR_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOwR as type uint64_t
	register_TEST_REG_oOwR_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwR_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_oOwR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwR as type uint64_t
	register_TEST_REG_oOwR_write_raw(raw);
}

#define register_TEST_REG_oOwR_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOwR, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwR_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwR as type uint64_t
	register_TEST_REG_oOwR_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwRW_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oOwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRW_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oOwRW"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRW_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRW_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOwRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_write_raw(raw);
}

#define register_TEST_REG_oOwRW_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOwRW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRW_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwRW_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOwRW, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOwRW_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oOwRW, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRW_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOwRW as type uint64_t
	register_TEST_REG_oOwRW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOwRWm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_oOwRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRWm_read_volatile_raw(val)                        \
	__asm__ volatile("mrs %0, TEST_REG_oOwRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRWm_read_volatile_ordered_raw(val)                \
	__asm__ volatile("mrs %0, TEST_REG_oOwRWm"                             \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRWm_read_volatile_ordered(
	asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRWm_write_raw(val)                                \
	__asm__ volatile("msr TEST_REG_oOwRWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_write_raw(raw);
}

#define register_TEST_REG_oOwRWm_write_ordered_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOwRWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRWm_write_ordered(const uint64_t	     val,
				       asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwRWm_write_barrier_raw(val)                        \
	__asm__ volatile("msr TEST_REG_oOwRWm, %[r]"                           \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOwRWm as type uint64_t
	register_TEST_REG_oOwRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOwRWm_write_barrier_ordered_raw(val)                \
	__asm__ volatile("msr TEST_REG_oOwRWm, %[r]"                           \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOwRWm as type
	// uint64_t
	register_TEST_REG_oOwRWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOwRm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOwRm as type uint64_t
	register_TEST_REG_oOwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_oOwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oOwRm as type uint64_t
	register_TEST_REG_oOwRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRm_read_volatile_ordered_raw(val)                 \
	__asm__ volatile("mrs %0, TEST_REG_oOwRm"                              \
			 : "=r"(val), "+m"(*ordering_var))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwRm_read_volatile_ordered(asm_ordering_dummy_t *ordering_var)
{
	register_t raw;
	// Read volatile ordered register TEST_REG_oOwRm as type uint64_t
	register_TEST_REG_oOwRm_read_volatile_ordered_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwRm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOwRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwRm as type uint64_t
	register_TEST_REG_oOwRm_write_raw(raw);
}

#define register_TEST_REG_oOwRm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOwRm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwRm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwRm as type uint64_t
	register_TEST_REG_oOwRm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwW_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_oOwW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwW as type uint64_t
	register_TEST_REG_oOwW_write_raw(raw);
}

#define register_TEST_REG_oOwW_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOwW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwW_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwW as type uint64_t
	register_TEST_REG_oOwW_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOwW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOwW as type uint64_t
	register_TEST_REG_oOwW_write_barrier_raw(raw);
}

#define register_TEST_REG_oOwW_write_barrier_ordered_raw(val)                  \
	__asm__ volatile("msr TEST_REG_oOwW, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwW_write_barrier_ordered(const uint64_t	   val,
					     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOwW as type uint64_t
	register_TEST_REG_oOwW_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOwWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_oOwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOwWm as type uint64_t
	register_TEST_REG_oOwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwWm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_oOwWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwWm as type uint64_t
	register_TEST_REG_oOwWm_write_raw(raw);
}

#define register_TEST_REG_oOwWm_write_ordered_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOwWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwWm_write_ordered(const uint64_t	    val,
				      asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwWm as type uint64_t
	register_TEST_REG_oOwWm_write_ordered_raw(raw);
}

#define register_TEST_REG_oOwWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_oOwWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oOwWm as type uint64_t
	register_TEST_REG_oOwWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oOwWm_write_barrier_ordered_raw(val)                 \
	__asm__ volatile("msr TEST_REG_oOwWm, %[r]"                            \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oOwWm_write_barrier_ordered(
	const uint64_t val, asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier ordered register TEST_REG_oOwWm as type uint64_t
	register_TEST_REG_oOwWm_write_barrier_ordered_raw(raw);
}

#define register_TEST_REG_oOwm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oOwm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oOwm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oOwm as type uint64_t
	register_TEST_REG_oOwm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oOwm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_oOwm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_oOwm as type uint64_t
	register_TEST_REG_oOwm_write_raw(raw);
}

#define register_TEST_REG_oOwm_write_ordered_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oOwm, %[r]"                             \
			 : "+m"(*ordering_var)                                 \
			 : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_oOwm_write_ordered(const uint64_t	   val,
				     asm_ordering_dummy_t *ordering_var)
{
	register_t raw = (register_t)(register_t)(val);
	// Write ordered register TEST_REG_oOwm as type uint64_t
	register_TEST_REG_oOwm_write_ordered_raw(raw);
}

#define register_TEST_REG_oR_read_volatile_raw(val)                            \
	__asm__ volatile("mrs %0, TEST_REG_oR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oR as type uint64_t
	register_TEST_REG_oR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oRW_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_oRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oRW as type uint64_t
	register_TEST_REG_oRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oRW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_oRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oRW as type uint64_t
	register_TEST_REG_oRW_write_barrier_raw(raw);
}

#define register_TEST_REG_oRWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_oRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oRWm as type uint64_t
	register_TEST_REG_oRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oRWm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_oRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oRWm as type uint64_t
	register_TEST_REG_oRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oRWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_oRWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oRWm as type uint64_t
	register_TEST_REG_oRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_oRm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_oRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oRm as type uint64_t
	register_TEST_REG_oRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oRm_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_oRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_oRm as type uint64_t
	register_TEST_REG_oRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oW_write_barrier_raw(val)                            \
	__asm__ volatile("msr TEST_REG_oW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oW as type uint64_t
	register_TEST_REG_oW_write_barrier_raw(raw);
}

#define register_TEST_REG_oWm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_oWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_oWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_oWm as type uint64_t
	register_TEST_REG_oWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_oWm_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_oWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_oWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_oWm as type uint64_t
	register_TEST_REG_oWm_write_barrier_raw(raw);
}

#define register_TEST_REG_om_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_om" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_om_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_om as type uint64_t
	register_TEST_REG_om_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_or_read_raw(val)                                     \
	__asm__("mrs %0, TEST_REG_or" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_or_read(void)
{
	register_t raw;
	// Read register TEST_REG_or as type uint64_t
	register_TEST_REG_or_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orR_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_orR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orR_read(void)
{
	register_t raw;
	// Read register TEST_REG_orR as type uint64_t
	register_TEST_REG_orR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orR_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_orR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orR as type uint64_t
	register_TEST_REG_orR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRW_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_orRW as type uint64_t
	register_TEST_REG_orRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRW_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_orRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orRW as type uint64_t
	register_TEST_REG_orRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_orRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orRW as type uint64_t
	register_TEST_REG_orRW_write_barrier_raw(raw);
}

#define register_TEST_REG_orRWm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_orRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orRWm as type uint64_t
	register_TEST_REG_orRWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_orRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orRWm as type uint64_t
	register_TEST_REG_orRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRWm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_orRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orRWm as type uint64_t
	register_TEST_REG_orRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_orRWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orRWm as type uint64_t
	register_TEST_REG_orRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_orRm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orRm as type uint64_t
	register_TEST_REG_orRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_orRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orRm as type uint64_t
	register_TEST_REG_orRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orRm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_orRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orRm as type uint64_t
	register_TEST_REG_orRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orW_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_orW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orW_read(void)
{
	register_t raw;
	// Read register TEST_REG_orW as type uint64_t
	register_TEST_REG_orW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_orW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orW as type uint64_t
	register_TEST_REG_orW_write_barrier_raw(raw);
}

#define register_TEST_REG_orWm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orWm as type uint64_t
	register_TEST_REG_orWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_orWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orWm as type uint64_t
	register_TEST_REG_orWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_orWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orWm as type uint64_t
	register_TEST_REG_orWm_write_barrier_raw(raw);
}

#define register_TEST_REG_orm_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_orm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orm as type uint64_t
	register_TEST_REG_orm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_orm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orm as type uint64_t
	register_TEST_REG_orm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orw_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_orw" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orw_read(void)
{
	register_t raw;
	// Read register TEST_REG_orw as type uint64_t
	register_TEST_REG_orw_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orw_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_orw, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orw_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orw as type uint64_t
	register_TEST_REG_orw_write_raw(raw);
}

#define register_TEST_REG_orwR_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwR_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwR as type uint64_t
	register_TEST_REG_orwR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwR_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_orwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orwR as type uint64_t
	register_TEST_REG_orwR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwR_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_orwR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwR as type uint64_t
	register_TEST_REG_orwR_write_raw(raw);
}

#define register_TEST_REG_orwRW_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_orwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwRW as type uint64_t
	register_TEST_REG_orwRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRW_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_orwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orwRW as type uint64_t
	register_TEST_REG_orwRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRW_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_orwRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwRW as type uint64_t
	register_TEST_REG_orwRW_write_raw(raw);
}

#define register_TEST_REG_orwRW_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_orwRW, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orwRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orwRW as type uint64_t
	register_TEST_REG_orwRW_write_barrier_raw(raw);
}

#define register_TEST_REG_orwRWm_read_raw(val)                                 \
	__asm__("mrs %0, TEST_REG_orwRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwRWm as type uint64_t
	register_TEST_REG_orwRWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRWm_read_barrier_raw(val)                         \
	__asm__("mrs %0, TEST_REG_orwRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orwRWm as type uint64_t
	register_TEST_REG_orwRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRWm_read_volatile_raw(val)                        \
	__asm__ volatile("mrs %0, TEST_REG_orwRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orwRWm as type uint64_t
	register_TEST_REG_orwRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRWm_write_raw(val)                                \
	__asm__ volatile("msr TEST_REG_orwRWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwRWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwRWm as type uint64_t
	register_TEST_REG_orwRWm_write_raw(raw);
}

#define register_TEST_REG_orwRWm_write_barrier_raw(val)                        \
	__asm__ volatile("msr TEST_REG_orwRWm, %[r]"                           \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orwRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orwRWm as type uint64_t
	register_TEST_REG_orwRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_orwRm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_orwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwRm as type uint64_t
	register_TEST_REG_orwRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_orwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orwRm as type uint64_t
	register_TEST_REG_orwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_orwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_orwRm as type uint64_t
	register_TEST_REG_orwRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwRm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_orwRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwRm as type uint64_t
	register_TEST_REG_orwRm_write_raw(raw);
}

#define register_TEST_REG_orwW_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orwW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwW_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwW as type uint64_t
	register_TEST_REG_orwW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwW_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_orwW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwW as type uint64_t
	register_TEST_REG_orwW_write_raw(raw);
}

#define register_TEST_REG_orwW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_orwW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orwW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orwW as type uint64_t
	register_TEST_REG_orwW_write_barrier_raw(raw);
}

#define register_TEST_REG_orwWm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_orwWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwWm as type uint64_t
	register_TEST_REG_orwWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_orwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orwWm as type uint64_t
	register_TEST_REG_orwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwWm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_orwWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwWm as type uint64_t
	register_TEST_REG_orwWm_write_raw(raw);
}

#define register_TEST_REG_orwWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_orwWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_orwWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_orwWm as type uint64_t
	register_TEST_REG_orwWm_write_barrier_raw(raw);
}

#define register_TEST_REG_orwm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_orwm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwm_read(void)
{
	register_t raw;
	// Read register TEST_REG_orwm as type uint64_t
	register_TEST_REG_orwm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_orwm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_orwm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_orwm as type uint64_t
	register_TEST_REG_orwm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_orwm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_orwm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_orwm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_orwm as type uint64_t
	register_TEST_REG_orwm_write_raw(raw);
}

#define register_TEST_REG_ow_write_raw(val)                                    \
	__asm__ volatile("msr TEST_REG_ow, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_ow_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_ow as type uint64_t
	register_TEST_REG_ow_write_raw(raw);
}

#define register_TEST_REG_owR_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_owR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_owR as type uint64_t
	register_TEST_REG_owR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owR_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_owR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owR as type uint64_t
	register_TEST_REG_owR_write_raw(raw);
}

#define register_TEST_REG_owRW_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_owRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_owRW as type uint64_t
	register_TEST_REG_owRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owRW_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_owRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owRW as type uint64_t
	register_TEST_REG_owRW_write_raw(raw);
}

#define register_TEST_REG_owRW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_owRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_owRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_owRW as type uint64_t
	register_TEST_REG_owRW_write_barrier_raw(raw);
}

#define register_TEST_REG_owRWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_owRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_owRWm as type uint64_t
	register_TEST_REG_owRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owRWm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_owRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_owRWm as type uint64_t
	register_TEST_REG_owRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owRWm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_owRWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owRWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owRWm as type uint64_t
	register_TEST_REG_owRWm_write_raw(raw);
}

#define register_TEST_REG_owRWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_owRWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_owRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_owRWm as type uint64_t
	register_TEST_REG_owRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_owRm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_owRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_owRm as type uint64_t
	register_TEST_REG_owRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owRm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_owRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_owRm as type uint64_t
	register_TEST_REG_owRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owRm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_owRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owRm as type uint64_t
	register_TEST_REG_owRm_write_raw(raw);
}

#define register_TEST_REG_owW_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_owW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owW as type uint64_t
	register_TEST_REG_owW_write_raw(raw);
}

#define register_TEST_REG_owW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_owW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_owW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_owW as type uint64_t
	register_TEST_REG_owW_write_barrier_raw(raw);
}

#define register_TEST_REG_owWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_owWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_owWm as type uint64_t
	register_TEST_REG_owWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owWm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_owWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owWm as type uint64_t
	register_TEST_REG_owWm_write_raw(raw);
}

#define register_TEST_REG_owWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_owWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_owWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_owWm as type uint64_t
	register_TEST_REG_owWm_write_barrier_raw(raw);
}

#define register_TEST_REG_owm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_owm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_owm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_owm as type uint64_t
	register_TEST_REG_owm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_owm_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_owm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_owm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_owm as type uint64_t
	register_TEST_REG_owm_write_raw(raw);
}

#define register_TEST_REG_r_read_raw(val)                                      \
	__asm__("mrs %0, TEST_REG_r" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_r_read(void)
{
	register_t raw;
	// Read register TEST_REG_r as type uint64_t
	register_TEST_REG_r_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rR_read_raw(val)                                     \
	__asm__("mrs %0, TEST_REG_rR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rR_read(void)
{
	register_t raw;
	// Read register TEST_REG_rR as type uint64_t
	register_TEST_REG_rR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rR_read_volatile_raw(val)                            \
	__asm__ volatile("mrs %0, TEST_REG_rR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rR as type uint64_t
	register_TEST_REG_rR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRW_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_rRW as type uint64_t
	register_TEST_REG_rRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRW_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_rRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rRW as type uint64_t
	register_TEST_REG_rRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_rRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rRW as type uint64_t
	register_TEST_REG_rRW_write_barrier_raw(raw);
}

#define register_TEST_REG_rRWm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_rRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rRWm as type uint64_t
	register_TEST_REG_rRWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_rRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rRWm as type uint64_t
	register_TEST_REG_rRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRWm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_rRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rRWm as type uint64_t
	register_TEST_REG_rRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_rRWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rRWm as type uint64_t
	register_TEST_REG_rRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_rRm_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rRm as type uint64_t
	register_TEST_REG_rRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_rRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rRm as type uint64_t
	register_TEST_REG_rRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rRm_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_rRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rRm as type uint64_t
	register_TEST_REG_rRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rW_read_raw(val)                                     \
	__asm__("mrs %0, TEST_REG_rW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rW_read(void)
{
	register_t raw;
	// Read register TEST_REG_rW as type uint64_t
	register_TEST_REG_rW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rW_write_barrier_raw(val)                            \
	__asm__ volatile("msr TEST_REG_rW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rW as type uint64_t
	register_TEST_REG_rW_write_barrier_raw(raw);
}

#define register_TEST_REG_rWm_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rWm as type uint64_t
	register_TEST_REG_rWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rWm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_rWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rWm as type uint64_t
	register_TEST_REG_rWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rWm_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_rWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rWm as type uint64_t
	register_TEST_REG_rWm_write_barrier_raw(raw);
}

#define register_TEST_REG_rm_read_raw(val)                                     \
	__asm__("mrs %0, TEST_REG_rm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rm as type uint64_t
	register_TEST_REG_rm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rm_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_rm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rm as type uint64_t
	register_TEST_REG_rm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rw_read_raw(val)                                     \
	__asm__("mrs %0, TEST_REG_rw" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rw_read(void)
{
	register_t raw;
	// Read register TEST_REG_rw as type uint64_t
	register_TEST_REG_rw_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rw_write_raw(val)                                    \
	__asm__ volatile("msr TEST_REG_rw, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rw_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rw as type uint64_t
	register_TEST_REG_rw_write_raw(raw);
}

#define register_TEST_REG_rwR_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwR_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwR as type uint64_t
	register_TEST_REG_rwR_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwR_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_rwR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rwR as type uint64_t
	register_TEST_REG_rwR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwR_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_rwR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwR as type uint64_t
	register_TEST_REG_rwR_write_raw(raw);
}

#define register_TEST_REG_rwRW_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_rwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRW_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwRW as type uint64_t
	register_TEST_REG_rwRW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRW_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_rwRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rwRW as type uint64_t
	register_TEST_REG_rwRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRW_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_rwRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwRW as type uint64_t
	register_TEST_REG_rwRW_write_raw(raw);
}

#define register_TEST_REG_rwRW_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_rwRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rwRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rwRW as type uint64_t
	register_TEST_REG_rwRW_write_barrier_raw(raw);
}

#define register_TEST_REG_rwRWm_read_raw(val)                                  \
	__asm__("mrs %0, TEST_REG_rwRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwRWm as type uint64_t
	register_TEST_REG_rwRWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRWm_read_barrier_raw(val)                          \
	__asm__("mrs %0, TEST_REG_rwRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rwRWm as type uint64_t
	register_TEST_REG_rwRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRWm_read_volatile_raw(val)                         \
	__asm__ volatile("mrs %0, TEST_REG_rwRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rwRWm as type uint64_t
	register_TEST_REG_rwRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRWm_write_raw(val)                                 \
	__asm__ volatile("msr TEST_REG_rwRWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwRWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwRWm as type uint64_t
	register_TEST_REG_rwRWm_write_raw(raw);
}

#define register_TEST_REG_rwRWm_write_barrier_raw(val)                         \
	__asm__ volatile("msr TEST_REG_rwRWm, %[r]"                            \
			 :                                                     \
			 : [r] "rz"(val)                                       \
			 : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rwRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rwRWm as type uint64_t
	register_TEST_REG_rwRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_rwRm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_rwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwRm as type uint64_t
	register_TEST_REG_rwRm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_rwRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rwRm as type uint64_t
	register_TEST_REG_rwRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_rwRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_rwRm as type uint64_t
	register_TEST_REG_rwRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwRm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_rwRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwRm as type uint64_t
	register_TEST_REG_rwRm_write_raw(raw);
}

#define register_TEST_REG_rwW_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rwW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwW_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwW as type uint64_t
	register_TEST_REG_rwW_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwW_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_rwW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwW as type uint64_t
	register_TEST_REG_rwW_write_raw(raw);
}

#define register_TEST_REG_rwW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_rwW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rwW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rwW as type uint64_t
	register_TEST_REG_rwW_write_barrier_raw(raw);
}

#define register_TEST_REG_rwWm_read_raw(val)                                   \
	__asm__("mrs %0, TEST_REG_rwWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwWm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwWm as type uint64_t
	register_TEST_REG_rwWm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_rwWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rwWm as type uint64_t
	register_TEST_REG_rwWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwWm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_rwWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwWm as type uint64_t
	register_TEST_REG_rwWm_write_raw(raw);
}

#define register_TEST_REG_rwWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_rwWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_rwWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_rwWm as type uint64_t
	register_TEST_REG_rwWm_write_barrier_raw(raw);
}

#define register_TEST_REG_rwm_read_raw(val)                                    \
	__asm__("mrs %0, TEST_REG_rwm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwm_read(void)
{
	register_t raw;
	// Read register TEST_REG_rwm as type uint64_t
	register_TEST_REG_rwm_read_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_rwm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_rwm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_rwm as type uint64_t
	register_TEST_REG_rwm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_rwm_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_rwm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_rwm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_rwm as type uint64_t
	register_TEST_REG_rwm_write_raw(raw);
}

#define register_TEST_REG_w_write_raw(val)                                     \
	__asm__ volatile("msr TEST_REG_w, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_w_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_w as type uint64_t
	register_TEST_REG_w_write_raw(raw);
}

#define register_TEST_REG_wR_read_volatile_raw(val)                            \
	__asm__ volatile("mrs %0, TEST_REG_wR" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wR_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_wR as type uint64_t
	register_TEST_REG_wR_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wR_write_raw(val)                                    \
	__asm__ volatile("msr TEST_REG_wR, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wR_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wR as type uint64_t
	register_TEST_REG_wR_write_raw(raw);
}

#define register_TEST_REG_wRW_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_wRW" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wRW_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_wRW as type uint64_t
	register_TEST_REG_wRW_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wRW_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_wRW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wRW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wRW as type uint64_t
	register_TEST_REG_wRW_write_raw(raw);
}

#define register_TEST_REG_wRW_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_wRW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_wRW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_wRW as type uint64_t
	register_TEST_REG_wRW_write_barrier_raw(raw);
}

#define register_TEST_REG_wRWm_read_barrier_raw(val)                           \
	__asm__("mrs %0, TEST_REG_wRWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wRWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_wRWm as type uint64_t
	register_TEST_REG_wRWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wRWm_read_volatile_raw(val)                          \
	__asm__ volatile("mrs %0, TEST_REG_wRWm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wRWm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_wRWm as type uint64_t
	register_TEST_REG_wRWm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wRWm_write_raw(val)                                  \
	__asm__ volatile("msr TEST_REG_wRWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wRWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wRWm as type uint64_t
	register_TEST_REG_wRWm_write_raw(raw);
}

#define register_TEST_REG_wRWm_write_barrier_raw(val)                          \
	__asm__ volatile("msr TEST_REG_wRWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_wRWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_wRWm as type uint64_t
	register_TEST_REG_wRWm_write_barrier_raw(raw);
}

#define register_TEST_REG_wRm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_wRm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wRm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_wRm as type uint64_t
	register_TEST_REG_wRm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wRm_read_volatile_raw(val)                           \
	__asm__ volatile("mrs %0, TEST_REG_wRm" : "=r"(val))

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wRm_read_volatile(void)
{
	register_t raw;
	// Read volatile register TEST_REG_wRm as type uint64_t
	register_TEST_REG_wRm_read_volatile_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wRm_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_wRm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wRm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wRm as type uint64_t
	register_TEST_REG_wRm_write_raw(raw);
}

#define register_TEST_REG_wW_write_raw(val)                                    \
	__asm__ volatile("msr TEST_REG_wW, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wW_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wW as type uint64_t
	register_TEST_REG_wW_write_raw(raw);
}

#define register_TEST_REG_wW_write_barrier_raw(val)                            \
	__asm__ volatile("msr TEST_REG_wW, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_wW_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_wW as type uint64_t
	register_TEST_REG_wW_write_barrier_raw(raw);
}

#define register_TEST_REG_wWm_read_barrier_raw(val)                            \
	__asm__("mrs %0, TEST_REG_wWm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wWm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_wWm as type uint64_t
	register_TEST_REG_wWm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wWm_write_raw(val)                                   \
	__asm__ volatile("msr TEST_REG_wWm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wWm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wWm as type uint64_t
	register_TEST_REG_wWm_write_raw(raw);
}

#define register_TEST_REG_wWm_write_barrier_raw(val)                           \
	__asm__ volatile("msr TEST_REG_wWm, %[r]" : : [r] "rz"(val) : "memory")

ALWAYS_INLINE static inline void
register_TEST_REG_wWm_write_barrier(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write memory-barrier register TEST_REG_wWm as type uint64_t
	register_TEST_REG_wWm_write_barrier_raw(raw);
}

#define register_TEST_REG_wm_read_barrier_raw(val)                             \
	__asm__("mrs %0, TEST_REG_wm" : "=r"(val)::"memory")

ALWAYS_INLINE static inline uint64_t
register_TEST_REG_wm_read_barrier(void)
{
	register_t raw;
	// Read register TEST_REG_wm as type uint64_t
	register_TEST_REG_wm_read_barrier_raw(raw);
	return (uint64_t)(raw);
}

#define register_TEST_REG_wm_write_raw(val)                                    \
	__asm__ volatile("msr TEST_REG_wm, %[r]" : : [r] "rz"(val))

ALWAYS_INLINE static inline void
register_TEST_REG_wm_write(const uint64_t val)
{
	register_t raw = (register_t)(register_t)(val);
	// Write register TEST_REG_wm as type uint64_t
	register_TEST_REG_wm_write_raw(raw);
}

#pragma clang diagnostic pop
#else
#error HYPREGISTERS_H_ multiple include
#endif
