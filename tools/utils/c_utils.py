#!/usr/bin/env python3
# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

import shlex
import subprocess


# Generate a unique C identifier for any arbitrary string.
def generate_unique_c_ident(w):
    s = ""

    # '_x' is followed by the ascii code in hex.
    # '_z' and '_M' are reserved and must not be used.
    for c in w:
        if c.isalnum():
            s += c
        elif ord(c) < 256:
            s += "_x{:02X}".format(ord(c))
        else:
            raise ValueError("unsupported char: {}", c)

    return s


# Generate a standard C identifer for any arbitary string.
def generate_c_ident(w):
    s = ""

    # Set this to true, so that identifiers aren't generated with leading
    # underscores.
    last_underscore = True

    for c in w:
        if c.isalnum():
            s += c
            last_underscore = False
        elif not last_underscore:
            s += "_"
            last_underscore = True

    return s


# Generate a C string literal from a string.
def generate_c_string_literal(w):
    s = ""

    for c in w:
        if c == "\\":
            s += "\\\\"
        elif c == '"':
            s += '\\"'
        else:
            s += c

    return '"' + s + '"'


# Format source code with a given formatter.
def format_c_code(s, formatter):
    if formatter:
        ret = subprocess.run(
            shlex.split(formatter),
            input=s.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        s = ret.stdout.decode("utf-8")
        if ret.returncode != 0:
            raise Exception("failed to format output:\n", ret.stderr)

    return s
