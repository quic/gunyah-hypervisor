#!/usr/bin/env python3
# coding: utf-8
#
# © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Run as a part of gitlab CI, after Parasoft reports have been generated.

This script converts the Parasoft XML-format report to a Code Climate
compatible json file, that gitlab code quality can interpret.
"""

import xml.etree.ElementTree as ET
import json
import argparse
import sys
import os
import re

argparser = argparse.ArgumentParser(
    description="Convert Parasoft XML to Code Climate JSON")
argparser.add_argument('input', type=argparse.FileType('r'), nargs='?',
                       default=sys.stdin, help="the Parasoft XML input")
argparser.add_argument('--output', '-o', type=argparse.FileType('w'),
                       default=sys.stdout, help="the Code Climate JSON output")
args = argparser.parse_args()

tree = ET.parse(args.input)

parasoft_viols = tree.findall(".//StdViol") + tree.findall(".//FlowViol")

cc_viols = []

severity_map = {
    1: "blocker",
    2: "critical",
    3: "major",
    4: "minor",
    5: "info",
}

deviation_map = {
    'MISRAC2012-RULE_20_12-a': [
        (None, re.compile(r"parameter of potential macro 'assert'")),
    ],
    # False positives due to __c11 builtins taking int memory order arguments
    # instead of enum
    'MISRAC2012-RULE_10_3-b': [
        (None, re.compile(r"number '2'.*'essentially Enum'.*"
                          r"'__c11_atomic_load'.*'essentially signed'")),
        (None, re.compile(r"number '3'.*'essentially Enum'.*"
                          r"'__c11_atomic_(store'|fetch_).*"
                          r"'essentially signed'")),
        (None, re.compile(r"number '[45]'.*'essentially Enum'.*"
                          r"'__c11_atomic_compare_exchange_(strong|weak)'.*"
                          r"'essentially signed'")),
    ],
    # False positives with unknown cause: the return value of assert_if_const()
    # is always used, to determine whether to call assert_failed()
    'MISRAC2012-RULE_17_7-b': [
        (None, re.compile(r'"assert_if_const"')),
    ],
    # Advisory rule which is impractical to enforce for generated accessors,
    # since the type system has no information about which accessors are used.
    'MISRAC2012-RULE_8_7-a': [
        (re.compile(r'^build/.*/accessors\.c$'), None),
    ],
}


def matches_deviation(v):
    rule = v.attrib['rule']
    if rule not in deviation_map:
        return False

    msg = v.attrib['msg']
    path = v.attrib['locFile'].split(os.sep, 2)[2]

    def check_constraint(constraint, value):
        if constraint is None:
            return True
        try:
            return constraint.search(value)
        except AttributeError:
            return constraint == value

    for d_path, d_msg in deviation_map[rule]:
        if check_constraint(d_path, path) and check_constraint(d_msg, msg):
            return True

    return False


cc_viols = [
    ({
        "type": "issue",
        "categories": ["Bug Risk"],
        "severity": ('info' if matches_deviation(v)
                     else severity_map[int(v.attrib['sev'])]),
        "check_name": v.attrib['rule'],
        "description": (v.attrib['msg'] + '. ' +
                        v.attrib['rule.header'] + '. (' +
                        v.attrib['rule'] + ')'),
        "fingerprint": v.attrib['unbViolId'],
        "location": {
            "path": v.attrib['locFile'].split(os.sep, 2)[2],
            "lines": {
                "begin": int(v.attrib['locStartln']),
                "end": int(v.attrib['locEndLn'])
            }
        }
    })
    for v in parasoft_viols]

args.output.write(json.dumps(cc_viols))
args.output.close()
