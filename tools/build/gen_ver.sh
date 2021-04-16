#!/bin/sh
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

status=`git diff HEAD --quiet || echo '-dirty'`

echo "#define HYP_GIT_VERSION `git rev-parse --short HEAD`$status"

if [ -z "$status" ]
then
	echo "#define HYP_BUILD_DATE \"`TZ=UTC git show -s --pretty="%cd" --date=local HEAD` UTC\""
else
	echo "#define HYP_BUILD_DATE \"`date -R`\""
fi
