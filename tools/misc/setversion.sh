#!/bin/sh
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

outputpath=../../hyp/core/boot/include/version.h
echo "updating Gunyah Hypervisor version #...."
echo "Remember to checkin version.h into P4"
echo chmod a+x ${outputpath}
echo $(source ../build/gen_ver.sh > ${outputpath})
echo "Done! "${outputpath}" is updated!"
