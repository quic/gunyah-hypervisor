# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import SCons.Script
import configure
import os

env_vars = {
    'PATH': os.environ['PATH'],
}

if 'LLVM' in os.environ:
    env_vars['LLVM'] = os.environ['LLVM']

env = Environment(tools={}, SCANNERS=[], BUILDERS={}, ENV=env_vars)
configure.SConsBuild(env, Builder, Action, arguments=SCons.Script.ARGUMENTS)()
