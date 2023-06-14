// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(ARCH_ENDIAN_LITTLE) && ARCH_ENDIAN_LITTLE
#define USEFULBUF_CONFIG_LITTLE_ENDIAN 1
#elif defined(ARCH_ENDIAN_BIG) && ARCH_ENDIAN_BIG
#define USEFULBUF_CONFIG_BIG_ENDIAN 1
#endif

#include "qcbor/qcbor_encode.h"

typedef UsefulBuf  useful_buff_t;
typedef UsefulBufC const_useful_buff_t;

typedef QCBOREncodeContext qcbor_enc_ctxt_t;
typedef QCBORError	   qcbor_err_t;
