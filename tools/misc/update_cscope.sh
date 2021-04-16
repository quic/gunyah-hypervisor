#!/bin/sh
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

git ls-files "*.[ch]" > cscope.in
tools/misc/get_genfiles.py >> cscope.in

cscope -bk -icscope.in
