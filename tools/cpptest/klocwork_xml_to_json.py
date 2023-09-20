#!/usr/bin/env python3
# coding: utf-8
#
# © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Run as a part of gitlab CI, after Klocwork reports have been generated.

This script converts the Klocwork JSON-format report to a Code Climate
compatible json file, that gitlab code quality can interpret.
"""

import json
import argparse
import sys
import os
import hashlib

argparser = argparse.ArgumentParser(
    description="Convert Klocwork JSON to Code Climate JSON")
argparser.add_argument('input', type=argparse.FileType('r'), nargs='?',
                       default=sys.stdin, help="the Klocwork JSON input")
argparser.add_argument('--output', '-o', type=argparse.FileType('w'),
                       default=sys.stdout, help="the Code Climate JSON output")
args = argparser.parse_args()

json_input_file = ""

kw_violations = []
issue = {}

severity_map = {
    1: "blocker",
    2: "critical",
    3: "major",
    4: "minor",
    5: "info",
}


def get_severity(v):
    if (v > 5):
        return severity_map[5]
    else:
        return severity_map[v]


with open(args.input.name, 'r') as json_file:
    json_input_file = json.load(json_file)

for v in range(len(json_input_file)):
    # print(json_input_file[v])
    issue["type"] = "issue"
    issue["categories"] = ["Bug Risk"]
    issue["severity"] = get_severity(
        int(json_input_file[v]['severityCode']))
    issue["check_name"] = json_input_file[v]['code']
    issue["description"] = json_input_file[v]['message'] + '. ' + \
        json_input_file[v]['severity'] + \
        '. (' + json_input_file[v]['code'] + ')'
    issue["location"] = {}
    issue["location"]["path"] = os.path.relpath(json_input_file[v]['file'])
    issue["location"]["lines"] = {}
    issue["location"]["lines"]["begin"] = int(json_input_file[v]['line'])
    issue["location"]["lines"]["end"] = int(json_input_file[v]['line'])
    dump_issue = json.dumps(issue)
    issue["fingerprint"] = hashlib.md5(dump_issue.encode('utf-8')).hexdigest()
    # print(issue)
    kw_violations.append(issue)
    issue = {}  # was getting wired result without clearing it
args.output.write(json.dumps(kw_violations))
