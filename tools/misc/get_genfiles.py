#!/usr/bin/env python3
# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Simple script to parse the compile_commands to extract generated source and
header files, to pass to cscope or other source indexing tools.
"""

import json
import sys
import os
import re

build_dir = 'build'
commands_file = 'compile_commands.json'
compile_commands = []

build_preferences = [("gunyah-rm", 120)]

files = set()
incdirs = set()

include_regex = re.compile('(-iquote|-I) (\\w+[-\\{:s}\\w]+)'.format(os.sep))

for dir, dir_dirs, dir_files in os.walk(build_dir):
    if commands_file in dir_files:
        x = os.stat(dir)
        time = max(x.st_atime, x.st_mtime, x.st_ctime)
        compile_commands.append((time, os.path.join(dir, commands_file)))

if not compile_commands:
    print('no build found!', file=sys.stderr)
    exit(1)

newest = 0.0

for time, f in compile_commands:
    x = os.stat(f)
    time = max(x.st_atime, x.st_mtime, x.st_ctime, time)
    for p, weight in build_preferences:
        if p in f:
            # Boost these to preference them
            time += weight

    if time > newest:
        newest = time
        infile = f

if len(compile_commands) > 1:
    print('warning: multiple builds found, using: {:s}'.format(
        infile), file=sys.stderr)

try:
    with open(infile, 'r') as f:
        compile = json.loads(f.read())

        for s in compile:
            if s['file'].startswith(build_dir):
                files.add(s['file'])
            cmd = s['command']
            for t, dir in include_regex.findall(cmd):
                if dir.startswith(build_dir):
                    incdirs.add(dir)
except FileNotFoundError:
    exit(1)

for dir in incdirs:
    try:
        for f in os.listdir(dir):
            filename = os.path.join(dir, f)
            if filename.endswith('.h'):
                files.add(filename)
    except OSError:
        pass

for f in files:
    print(f)
