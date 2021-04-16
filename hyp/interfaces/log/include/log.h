// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define LOG(tclass, id, ...)                                                   \
	TRACE_EVENT(tclass, id, TRACE_ACTION_LOG, __VA_ARGS__)

#define TRACE_AND_LOG(tclass, id, ...)                                         \
	TRACE_EVENT(tclass, id, TRACE_ACTION_TRACE_AND_LOG, __VA_ARGS__)
