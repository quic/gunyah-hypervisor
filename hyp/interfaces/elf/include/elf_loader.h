// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Read the ELF header its program headers and return true if valid
bool
elf_valid(void *elf_file, size_t max_size);

// Return ELF's entry point
Elf_Addr
elf_get_entry(void *elf_file);

// Return the number of program headers in the ELF file
count_t
elf_get_num_phdrs(void *elf_file);

// Return a pointer to a requested ELF program header
Elf_Phdr *
elf_get_phdr(void *elf_file, count_t index);

// Load the ELF file to its physical address as per its program headers
error_t
elf_load_phys(void *elf_file, size_t elf_max_size, paddr_t phys_base);
