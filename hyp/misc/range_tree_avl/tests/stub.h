// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef STUB_H
#define STUB_H

void
panic(const char *msg);

void
rcu_read_start(void) ACQUIRE_RCU_READ;

void
rcu_read_finish(void) RELEASE_RCU_READ;

#endif // STUB_H
