// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Load fdt header.
 */
fdt_ret_t
fdt_load_header(fdt_header_t *header, char *dtb);

/*
 * Set up the parser.
 *
 * If 'start' is not NULL, the parser is set up so that a tree walk will visit
 * the subtree rooted at 'start' only.
 *
 * Otherwise, the parser is set up so that a walk will visit the whole tree.
 */
void
fdt_parser_setup(fdt_parser_t *parser, dt_tree_t *tree, dt_node_t *start);

/*
 * Advance the parser to the next node or property.
 *
 * If the function returns successfully, the cursor is left pointing to the
 * next token, or one past the end in the case of FDT_RET_DONE.
 *
 * If an error occurs, the cursor points to (near) the location of the error.
 *
 * Return values:
 *  - FDT_RET_NODE: reached a node
 *  - FDT_RET_PROPERTY: reached a property
 *  - FDT_RET_DONE: reached the end of the tree
 *  - FDT_ERR_*: failure
 */
fdt_ret_t
fdt_parser_next(fdt_parser_t *parser);

/*
 * Advance the parser to the next property.
 *
 * This function does *not* skip over FDT_BEGIN_NODE or FDT_END_NODE tokens.
 * Use this function to iterate through the properties of the current node.
 *
 * If the function returns with FDT_RET_PROPERTY, the cursor is left pointing
 * to the next token. Otherwise, the cursor is left pointing to the
 * non-FDT_PROP token that was encountered.
 *
 * Return values:
 *  - FDT_RET_PROPERTY: reached a property
 *  - FDT_RET_DONE: reached the end of this node's properties
 */
fdt_ret_t
fdt_parser_next_property(fdt_parser_t *parser);

/*
 * Advance the parser to the next node.
 *
 * This function does *not* skip over FDT_END_NODE tokens. Use this function
 * with fdt_parser_skip_node() to iterate through child nodes.
 *
 * If the function returns with FDT_RET_NODE, the cursor is left pointing to
 * the next token. Otherwise, the cursor is left pointing to the
 * non-FDT_BEGIN_NODE token that was encounted.
 *
 * Return values:
 * - FDT_RET_NODE: reached a node
 * - FDT_RET_DONE: reached the end of this node's children
 */
fdt_ret_t
fdt_parser_next_node(fdt_parser_t *parser);

/*
 * Advance the parser to the next end of the node.
 *
 * This function does *not* skip over any non-FDT_END_NODE tokens.
 *
 * This function is intended to be called after iterating through all child
 * nodes; it is functionally equivalent to fdt_parser_skip_node() when called
 * there.
 */
void
fdt_parser_next_end_node(fdt_parser_t *parser);

/*
 * Advance the parser to the end of the tree.
 *
 * This function does *not* skip over any non-FDT_END tokens.
 *
 * This function is intended to be called after the root node has been parsed
 * to check that the tree has been parsed correctly.
 */
void
fdt_parser_next_end_tree(fdt_parser_t *parser);

/*
 * Skip the properties of the current node.
 *
 * When this function returns, the cursor is left pointing to the non-FDT_PROP
 * token that was encountered.
 */
void
fdt_parser_skip_properties(fdt_parser_t *parser);

/*
 * Skip the current node (and all it's children).
 *
 * This function also skips over all the properties of each node.
 *
 * When this function returns, the cursor is left pointing to the token after
 * the FDT_END_NODE corresponding to this node.
 */
void
fdt_parser_skip_node(fdt_parser_t *parser);

/*
 * Skip properties until 'compatible' is found.
 *
 * Return values:
 * - FDT_RET_PROPERTY: reached a 'compatible' property.
 * - FDT_RET_DONE: reached the end of this node's properties
 */
fdt_ret_t
fdt_parser_skip_to_compatible(fdt_parser_t *parser);

/*
 * Rewind the parser to the start of the properties of the current node.
 */
void
fdt_parser_rewind_properties(fdt_parser_t *parser);

/*
 * Get the name of the current property.
 */
const char *
fdt_parser_get_current_prop_name(fdt_parser_t *parser);

/*
 * Get the name of the current node.
 */
const char *
fdt_parser_get_current_node_name(fdt_parser_t *parser);
