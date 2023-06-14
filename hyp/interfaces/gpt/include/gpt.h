// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Note that the GPT is not thread-safe by default; the caller must use an
// external lock or some other protection to prevent concurrent calls to these
// APIs. If the rcu_read option is set in the GPT's config, read-only operations
// will be protected by RCU, and external locking is only required for write
// operations.

// Initialise the GPT.
error_t
gpt_init(gpt_t *gpt, partition_t *partition, gpt_config_t config,
	 register_t allowed_types);

// Destroy the GPT.
void
gpt_destroy(gpt_t *gpt);

// Insert a range into the GPT.
//
// If expect_empty is true, the operation will fail if any part of the range is
// not found to be empty during insertion. Otherwise, any existing entries in
// the range will be overwritten.
error_t
gpt_insert(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry,
	   bool expect_empty);

// Update a range in the GPT.
//
// The update will fail if all entries over the range do not match the given old
// entry. If successful, all old entries will be replaced with the new entry.
error_t
gpt_update(gpt_t *gpt, size_t base, size_t size, gpt_entry_t old_entry,
	   gpt_entry_t new_entry);

// Remove a range from the GPT.
//
// This will fail if all entries over the range do not match the given entry.
error_t
gpt_remove(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry);

// Clear a range in the GPT.
error_t
gpt_clear(gpt_t *gpt, size_t base, size_t size);

// Clear the entire GPT.
void
gpt_clear_all(gpt_t *gpt);

// Returns true if the GPT is empty.
bool
gpt_is_empty(gpt_t *gpt);

// Lookup a range in the GPT.
//
// This function returns the entry found at the given base, and returns the size
// of this entry, which will be capped at max_size.
gpt_lookup_result_t
gpt_lookup(gpt_t *gpt, size_t base, size_t max_size);

// Returns true if an entry is contiguous over a range in the GPT.
bool
gpt_is_contiguous(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry);

// Walk over a range in the GPT and perform a callback for regions matching
// the given type.
error_t
gpt_walk(gpt_t *gpt, size_t base, size_t size, gpt_type_t type,
	 gpt_callback_t callback, gpt_arg_t arg);

// Walk over the GPT and dump all contiguous ranges.
//
// This function is intended for debug use only.
void
gpt_dump_ranges(gpt_t *gpt);

// Walk over the GPT and dump all levels.
//
// This function is intended for debug use only.
void
gpt_dump_levels(gpt_t *gpt);
