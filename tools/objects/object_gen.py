#!/usr/bin/env python3
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

from Cheetah.Template import Template

import argparse
import subprocess
import sys


class Object:
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name

    def type_enum(self):
        return "OBJECT_TYPE_{:s}".format(self.name.upper())

    def rcu_destroy_enum(self):
        return "RCU_UPDATE_CLASS_{:s}_DESTROY".format(self.name.upper())


def main():
    args = argparse.ArgumentParser()

    mode_args = args.add_mutually_exclusive_group(required=True)
    mode_args.add_argument('-t', '--template',
                           type=argparse.FileType('r', encoding="utf-8"),
                           help="Template file used to generate output")

    args.add_argument('-o', '--output',
                      type=argparse.FileType('w', encoding="utf-8"),
                      default=sys.stdout, help="Write output to file")
    args.add_argument("-f", "--formatter",
                      help="specify clang-format to format the code")
    args.add_argument('input', metavar='INPUT', nargs='+', action='append',
                      help="List of objects to process")
    options = args.parse_args()

    object_list = [Object(o) for group in options.input for o in group]

    output = "// Automatically generated. Do not modify.\n"
    output += "\n"

    ns = {'object_list': object_list}
    output += str(Template(file=options.template, searchList=ns))

    if options.formatter:
        ret = subprocess.run([options.formatter], input=output.encode("utf-8"),
                             stdout=subprocess.PIPE)
        output = ret.stdout.decode("utf-8")
        if ret.returncode != 0:
            raise Exception("failed to format output:\n ", ret.stderr)

    options.output.write(output)


if __name__ == '__main__':
    main()
