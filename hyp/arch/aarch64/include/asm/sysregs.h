// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define sysreg64_read(reg, val)                                                \
	do {                                                                   \
		register_t val_;                                               \
		__asm__ volatile("mrs %0, " #reg ";" : "=r"(val_));            \
		val = (__typeof__(val))val_;                                   \
	} while (0)

#define sysreg64_read_ordered(reg, val, ordering_var)                          \
	do {                                                                   \
		register_t val_;                                               \
		__asm__ volatile("mrs %0, " #reg ";"                           \
				 : "=r"(val_), "+m"(ordering_var));            \
		val = (__typeof__(val))val_;                                   \
	} while (0)

#define sysreg64_write(reg, val)                                               \
	do {                                                                   \
		register_t reg = (register_t)val;                              \
		__asm__ volatile("msr " #reg ", %0" : : "r"(reg));             \
	} while (0)

#define sysreg64_write_ordered(reg, val, ordering_var)                         \
	do {                                                                   \
		register_t reg = (register_t)val;                              \
		__asm__ volatile("msr " #reg ", %1"                            \
				 : "+m"(ordering_var)                          \
				 : "r"(reg));                                  \
	} while (0)
