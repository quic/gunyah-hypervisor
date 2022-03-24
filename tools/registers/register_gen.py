#!/usr/bin/env python3
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

from Cheetah.Template import Template

import argparse
import itertools
import subprocess
import logging
import sys

logger = logging.getLogger(__name__)

valid_access_strs = \
    set([''.join(x) for x in itertools.chain.from_iterable(
        itertools.combinations('oOrwRW', r) for r in range(1, 6))])


class register:
    def __init__(self, name, type_name, variants=[], access='rw'):
        if access in ['o', 'O']:
            access += 'rw'
        if access not in valid_access_strs:
            logger.error("Invalid access type '%s'", access)
            sys.exit(1)
        self.name = name
        self.type_name = type_name
        self._variants = variants
        self._read = 'r' in access
        self._write = 'w' in access
        self._volatile_read = 'R' in access
        self._barrier_write = 'W' in access
        self._ordered = 'O' in access
        self._non_ordered = 'o' in access or 'O' not in access

    @property
    def variants(self):
        ret = []
        type_name = self.type_name[:-1] if self.type_name.endswith(
            '!') else self.type_name

        for v in self._variants:
            if v.endswith('!'):
                ret.append((v[:-1],
                            type_name if self.type_name.endswith(
                                '!') else v[:-1]))
            else:
                ret.append(('_'.join((self.name, v)),
                            type_name if self.type_name.endswith(
                                '!') else '_'.join((type_name, v))))

        if not ret:
            ret = [(self.name, type_name)]
        return sorted(ret)

    @property
    def is_readable(self):
        return self._read

    @property
    def is_volatile(self):
        return self._volatile_read

    @property
    def is_writable(self):
        return self._write

    @property
    def is_writeable_barrier(self):
        return self._barrier_write

    @property
    def need_ordered(self):
        return self._ordered

    @property
    def need_non_ordered(self):
        return self._non_ordered


def generate_accessors(template, input, ns):
    registers = {}

    for line in input.splitlines():
        if line.startswith('//'):
            continue
        tokens = line.split(maxsplit=1)
        if not tokens:
            continue
        name = tokens[0]
        assert name not in registers
        if len(tokens) == 1:
            registers[name] = register(name, name)
            continue
        args = tokens[1]

        type_name = name
        if args.startswith('<'):
            type_name, args = args[1:].split('>', maxsplit=1)
            args = args.strip()

        identifiers = []
        if args.startswith('['):
            identifiers, args = args[1:].split(']', maxsplit=1)
            identifiers = identifiers.split()
            args = args.strip()

        if args:
            registers[name] = register(name, type_name, identifiers, args)
        else:
            registers[name] = register(name, type_name, identifiers)

    ns['registers'] = [registers[r] for r in sorted(registers.keys())]

    output = str(Template(file=template, searchList=ns))

    return output


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

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
    args.add_argument("input", metavar='INPUT', nargs=1,
                      help="Input type register file to process",
                      type=argparse.FileType('r', encoding="utf-8"))
    options = args.parse_args()

    output = ""

    input = ""
    for f in options.input:
        input += f.read()
        f.close()

    output += generate_accessors(options.template, input, {})

    if options.formatter:
        ret = subprocess.run([options.formatter], input=output.encode("utf-8"),
                             stdout=subprocess.PIPE)
        output = ret.stdout.decode("utf-8")
        if ret.returncode != 0:
            raise Exception("failed to format output:\n ", ret.stderr)

    options.output.write(output)


if __name__ == '__main__':
    main()
