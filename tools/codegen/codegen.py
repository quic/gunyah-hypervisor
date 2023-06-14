#!/usr/bin/env python3
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

# Simple generic code generator. Assumes that all logic is in the template
# itself and is based only on the architecture names and the preprocessor
# defines, all of which are passed on the command line.
#
# Note that it is generally bad style to have any non-trivial logic in the
# templates. Templates should import Python modules for anything complex.
# The directory containing the template file is automatically added to the
# Python path for this purpose.

from Cheetah.Template import Template

import argparse
import subprocess
import logging
import sys
import inspect
import os
import re

logger = logging.getLogger(__name__)


class DefineAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        defs = getattr(namespace, self.dest, None)
        if defs is None:
            defs = {}
            setattr(namespace, self.dest, defs)
        try:
            name, val = values.split('=')
            try:
                val = int(val.rstrip('uU'), 0)
            except TypeError:
                pass
        except ValueError:
            name = values
            val = True
        defs[name] = val


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

    args = argparse.ArgumentParser()

    args.add_argument("-o", "--output", help="Output file (default: stdout)",
                      default=sys.stdout,
                      type=argparse.FileType('w', encoding='utf-8'))
    args.add_argument("-D", dest='defines', help="Define config variable",
                      action=DefineAction)
    args.add_argument("-imacros",
                      type=argparse.FileType('r', encoding='utf-8'),
                      help="parse imacros CPP file",
                      default=None)
    args.add_argument("-a", dest='archs', help="Define architecture name",
                      action='append')
    args.add_argument("-f", "--formatter",
                      help="specify clang-format to format the code")
    args.add_argument('-d', "--deps",
                      type=argparse.FileType('w', encoding='utf-8'),
                      help="Write implicit dependencies to Makefile",
                      default=None)
    args.add_argument("template", metavar="TEMPLATE",
                      help="Template file used to generate output",
                      type=argparse.FileType('r', encoding='utf-8'))

    options = args.parse_args()

    defines = {}

    if options.defines:
        defines.update(options.defines)

    if options.imacros:
        d = re.compile(r'#define (?P<def>\w+)(\s+\"?(?P<val>[\w0-9,\ ]+)\"?)?')
        for line in options.imacros.readlines():
            match = d.search(line)
            define = match.group('def')
            val = match.group('val')
            try:
                try:
                    val = int(val.rstrip('uU'), 0)
                except TypeError:
                    pass
                except AttributeError:
                    pass
            except ValueError:
                pass

            if define in defines:
                raise Exception("multiply defined: {}\n", define)

            defines[define] = val

    sys.path.append(os.path.dirname(options.template.name))
    output = str(Template(file=options.template,
                          searchList=(defines,
                                      {'arch_list': options.archs})))

    if options.formatter:
        ret = subprocess.run([options.formatter], input=output.encode("utf-8"),
                             stdout=subprocess.PIPE)
        output = ret.stdout.decode("utf-8")
        if ret.returncode != 0:
            raise Exception("failed to format output:\n", ret.stderr)

    if options.deps is not None:
        deps = set()
        for m in sys.modules.values():
            try:
                f = inspect.getsourcefile(m)
            except TypeError:
                continue
            if f is None:
                continue
            f = os.path.relpath(f)
            if f.startswith('../'):
                continue
            deps.add(f)
        deps.add(options.template.name)
        if options.imacros:
            deps.add(options.imacros.name)

        options.deps.write(options.output.name + ' : ')
        options.deps.write(' '.join(sorted(deps)))
        options.deps.write('\n')
        options.deps.close()

    options.output.write(output)
    options.output.close()


if __name__ == '__main__':
    sys.exit(main())
