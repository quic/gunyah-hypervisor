// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier; BSD-3-Clause

#if defined(ARCH_ARM_FEAT_VHE)
#define VHE(X)	X##2 [X!]
#define VHE_V(X, V)	X##2 [V]
#define VHE_T(X, T)	X##2 <T> [X!]
#else
#define VHE(X)		X
#define VHE_V(X, V)	X [V]
#define VHE_T(X, T) 	X <T>
#endif
