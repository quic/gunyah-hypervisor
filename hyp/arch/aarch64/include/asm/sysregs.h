// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define sysreg64_read(reg, val)                                                \
	do {                                                                   \
		register_t reg##_read_val;                                     \
		/* Read 64-bit system register */                              \
		__asm__ volatile("mrs %0, " #reg ";" : "=r"(reg##_read_val));  \
		val = (__typeof__(val))reg##_read_val;                         \
	} while (0)

#define sysreg64_read_ordered(reg, val, ordering_var)                          \
	do {                                                                   \
		register_t reg##_read_val;                                     \
		/* Read 64-bit system register with ordering */                \
		__asm__ volatile("mrs %0, " #reg ";"                           \
				 : "=r"(reg##_read_val), "+m"(ordering_var));  \
		val = (__typeof__(val))reg##_read_val;                         \
	} while (0)

#define sysreg64_write(reg, val)                                               \
	do {                                                                   \
		register_t reg##_write_val = (register_t)val;                  \
		/* Write 64-bit system register */                             \
		__asm__ volatile("msr " #reg ", %0" : : "r"(reg##_write_val)); \
	} while (0)

#define sysreg64_write_ordered(reg, val, ordering_var)                         \
	do {                                                                   \
		register_t reg##_write_val = (register_t)val;                  \
		/* Write 64-bit system register with ordering */               \
		__asm__ volatile("msr " #reg ", %1"                            \
				 : "+m"(ordering_var)                          \
				 : "r"(reg##_write_val));                      \
	} while (0)
