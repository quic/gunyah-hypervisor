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
    # Deviation because the behaviour proscribed by the rule is exactly the
    # intended behaviour of assert(): it prints the unexpanded expression.
    'MISRAC2012-RULE_20_12-a': [
        (None, re.compile(r"parameter of potential macro 'assert'")),
    ],
    # False positives due to __c11 builtins taking int memory order arguments
    # instead of enum in the Clang implementation.
    'MISRAC2012-RULE_10_3-b': [
        (None, re.compile(r"number '2'.*'essentially Enum'.*"
                          r"'__c11_atomic_load'.*'essentially signed'")),
        (None, re.compile(r"number '3'.*'essentially Enum'.*"
                          r"'__c11_atomic_(store'|exchange'|fetch_).*"
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
    'MISRAC2012-RULE_8_7-a': [
        # The could-be-static advisory rule is impractical to enforce for
        # generated accessors, since the type system has no information about
        # which accessors are used.
        (re.compile(r'^build/.*/accessors\.c$'), None),
        # The smccc module specifically has events that are only triggered by
        # handlers for other events.
        (re.compile(r'^build/.*/events/src/smccc\.c$'), None),
        # The object module has type-specific APIs that are only used directly
        # for some specific object types, and otherwise are called only by the
        # type-generic APIs defined in the same file.
        (re.compile(r'^build/.*/objects/.*\.c$'), None),
    ],
    # Invariant expressions are expected and unavoidable in generated event
    # triggers because it is not possible to remove error result types from
    # handlers that never return errors.
    'MISRAC2012-RULE_14_3-ac': [
        (re.compile(r'^build/.*/events/src/.*\.c$'), None),
    ],
    # Could-be-const pointers are expected and unavoidable in generated event
    # triggers because the object may or may not be modified depending on the
    # handlers and the module configuration. The const qualifier is used to
    # specify whether the handlers are allowed to modify the objects, rather
    # than whether they actually do.
    'MISRAC2012-RULE_8_13-a': [
        (re.compile(r'^build/.*/events/src/.*\.c$'), None),
    ],
    # The generated type-generic object functions terminate non-empty default
    # clauses with a _Noreturn function, panic(), to indicate that the object
    # type is invalid. There is an approved deviation for this, and in any
    # case these rules are downgraded to advisory in generated code.
    'MISRAC2012-RULE_16_1-d': [
        (re.compile(r'^build/.*/objects/.*\.c$'), None),
    ],
    'MISRAC2012-RULE_16_3-b': [
        (re.compile(r'^build/.*/objects/.*\.c$'), None),
    ],
    # False positive due to a builtin sizeof variant that does not evaluate its
    # argument, so there is no uninitialised use.
    'MISRAC2012-RULE_9_1-a': [
        (None, re.compile(r'passed to "__builtin_object_size"')),
    ],
    'MISRAC2012-RULE_1_3-b': [
        (None, re.compile(r'passed to "__builtin_object_size"')),
    ],
    # Deviation because casting a pointer to _Atomic to a pointer that can't be
    # dereferenced at all (const void *) is reasonably safe, and is needed for
    # certain builtin functions where the compiler knows the real underlying
    # object type anyway (e.g. __builtin_object_size) or where the object type
    # does not matter (e.g. __builtin_prefetch).
    'MISRAC2012-RULE_11_8-a': [
        (None, re.compile(r"to the 'const void \*' type which removes the "
                          r"'_Atomic' qualifiers")),
    ],
    # Compliance with rule 21.25 would have a significant performance impact.
    # All existing uses have been thoroughly analysed and tested, so we will
    # seek a project-wide deviation for this rule.
    'MISRAC2012-RULE_21_25-a': [
        (None, None),
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
