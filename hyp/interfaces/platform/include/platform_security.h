// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Gets the device security state
//
// If this function returns true, meaning that the device is secure, then
// debugging is not permitted.
bool
platform_security_state_debug_disabled(void);

bool
platform_security_state_hlos_debug_disabled(void);
