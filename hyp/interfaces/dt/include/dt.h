// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Get a string from the dtb.
 */
const char *
dt_get_string(dt_tree_t *tree, uint32_t offset);

/*
 * Get the length of the 'node-name' part of a string.
 *
 * This function reads at most 32 bytes.
 */
count_t
dt_strlen_node_name(const char *s);

/*
 * Pointer to the boot dtb.
 *
 * This should be initialised early in the boot procedure.
 */
extern char *dt_boot_dtb_ptr;

/*
 * Get the boot dt.
 */
dt_tree_t *
dt_get_boot_tree(void);

/*
 * Get the root node of the boot dt.
 */
dt_auto_root_t *
dt_get_root(void);

/*
 * Get the memory reservation block.
 */
const char *
dt_get_memory_reservation_block(dt_tree_t *tree);
