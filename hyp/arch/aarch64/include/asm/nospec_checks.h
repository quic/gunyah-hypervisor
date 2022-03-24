// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Check index is valid with speculation prevention.
//
// Check if (unsigned) val is less than limit. Returns an index_result_t which
// contains whether the index is valid, and returns and if valid, returns the
// speculation safe index value for subsequent indexing. Note: DO NOT use the
// input index in subsequent code to index, as it won't be speculation safe.
//
// Returns OK if index is in the range 0 .. (limit-1), and a speculation safe
// copy of index.
index_result_t
nospec_range_check(index_t val, index_t limit);
