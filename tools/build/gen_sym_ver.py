#!/usr/bin/env python3
# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

from random import SystemRandom
rng = SystemRandom()
print("#define HYP_SYM_VERSION 0x{:x}".format(rng.getrandbits(64)))
