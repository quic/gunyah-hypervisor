#!/usr/bin/env python3
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os
import sys
import logging
import subprocess
import inspect
import pickle


if __name__ == '__main__' and __package__ is None:
    sys.path.append(os.path.dirname(os.path.dirname(__file__)))
    from utils import genfile
else:
    from ..utils import genfile


logger = logging.getLogger(__name__)


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )
    __loc__ = os.path.relpath(os.path.realpath(
        os.path.dirname(os.path.join(os.getcwd(), os.path.dirname(__file__)))))

    args = argparse.ArgumentParser()

    mode_args = args.add_mutually_exclusive_group(required=True)
    mode_args.add_argument('-t', '--template',
                           type=argparse.FileType('r', encoding='utf-8'),
                           help="Template file used to generate output")
    mode_args.add_argument('--dump-tree', action='store_true',
                           help="Print the parse tree and exit")
    mode_args.add_argument('-P', '--dump-pickle',
                           type=genfile.GenFileType('wb'),
                           help="Dump the IR to a Python pickle")

    args.add_argument('-m', '--module', default=None,
                      help="Constrain output to a particular module")
    args.add_argument('-I', '--extra-include', action='append', default=[],
                      help="Extra headers to include")
    args.add_argument('-d', "--deps", type=genfile.GenFileType('w'),
                      help="Write implicit dependencies to Makefile",
                      default=None)
    args.add_argument('-o', '--output', type=genfile.GenFileType('w'),
                      default=sys.stdout, help="Write output to file")
    args.add_argument("-f", "--formatter",
                      help="specify clang-format to format the code")
    args.add_argument('-p', '--load-pickle', type=argparse.FileType('rb'),
                      help="Load the IR from a Python pickle")
    args.add_argument('input', metavar='INPUT', nargs='*',
                      type=argparse.FileType('r', encoding='utf-8'),
                      help="Event DSL files to process")
    options = args.parse_args()

    if options.input and options.load_pickle:
        logger.error("Cannot specify both inputs and --load-pickle")
        args.print_usage()
        sys.exit(1)
    elif options.input:
        from lark import Lark, Visitor
        from parser import TransformToIR

        grammar_file = os.path.join(__loc__, 'grammars', 'events_dsl.lark')
        parser = Lark.open(grammar_file, parser='lalr', start='start',
                           propagate_positions=True)

        modules = {}
        events = {}
        transformer = TransformToIR(modules, events)

        for f in options.input:
            tree = parser.parse(f.read())

            class FilenameVisitor(Visitor):
                def __init__(self, filename):
                    self.filename = filename

                def __default__(self, tree):
                    tree.meta.filename = self.filename

            FilenameVisitor(f.name).visit(tree)
            if options.dump_tree:
                print(tree.pretty(), file=options.output)
            transformer.transform(tree)

        if options.dump_tree:
            return 0

        errors = transformer.errors
        for m in modules.values():
            errors += m.resolve(events)

        for m in modules.values():
            errors += m.finalise()

        if errors:
            logger.error("Found %d errors, exiting...", errors)
            sys.exit(1)
    elif options.load_pickle:
        modules = pickle.load(options.load_pickle)
    else:
        logger.error("Must specify inputs or --load-pickle")
        args.print_usage()
        sys.exit(1)

    if options.dump_pickle:
        pickle.dump(modules, options.dump_pickle, protocol=-1)
    else:
        from Cheetah.Template import Template

        try:
            module = modules[options.module]
        except KeyError:
            logger.error("Specified module '%s' is unknown", options.module)
            sys.exit(1)

        ns = [module, {'extra_includes': options.extra_include}]
        template = Template(file=options.template, searchList=ns)

        result = str(template)
        if options.formatter:
            ret = subprocess.run([options.formatter],
                                 input=result.encode("utf-8"),
                                 stdout=subprocess.PIPE)
            result = ret.stdout.decode("utf-8")
            if ret.returncode != 0:
                logger.error("Error formatting output", result)
                sys.exit(1)

        options.output.write(result)

    if options.deps is not None:
        deps = set()
        if options.input:
            deps.add(grammar_file)
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
        if options.dump_pickle:
            out_name = options.dump_pickle.name
        else:
            out_name = options.output.name
        options.deps.write(out_name + ' : ')
        options.deps.write(' '.join(sorted(deps)))
        options.deps.write('\n')
        options.deps.close()


if __name__ == '__main__':
    main()
