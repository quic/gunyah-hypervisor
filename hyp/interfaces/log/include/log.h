// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(PARASOFT_CYCLO)
#define LOG(tclass, id, ...)
#else
#define LOG(tclass, id, ...)                                                   \
	TRACE_EVENT(tclass, id, TRACE_ACTION_LOG, __VA_ARGS__)
#endif

#if defined(PARASOFT_CYCLO)
#define TRACE_AND_LOG(tclass, id, ...)
#else
#define TRACE_AND_LOG(tclass, id, ...)                                         \
	TRACE_EVENT(tclass, id, TRACE_ACTION_TRACE_AND_LOG, __VA_ARGS__)
#endif
