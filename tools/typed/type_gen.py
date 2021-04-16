#!/usr/bin/env python3
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

from lark import Lark
from exceptions import RangeError, DSLError
from ir import TransformTypes
from abi import AArch64ABI

import argparse
import sys
import os
import subprocess
import inspect
import logging
import pickle

logger = logging.getLogger(__name__)


abi_classes = {
    'aarch64': AArch64ABI,
}


def parse_dsl(parser, inputs, abi):
    trees = []
    for p in inputs:
        text = p.read()
        parse_tree = parser.parse(text)
        cur_tree = TransformTypes(text).transform(parse_tree)

        trees.append(cur_tree.get_intermediate_tree())

    final_tree = trees.pop(0)
    for t in trees:
        final_tree.merge(t)

    final_tree.update(abi_classes[abi]())

    return final_tree


def apply_template(tree, template, public_only=False):
    if template is None:
        code = tree.gen_output(public_only=public_only)
    else:
        code = tree.apply_template(template, public_only=public_only)

    return code


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )
    __loc__ = os.path.realpath(
        os.path.dirname(os.path.join(os.getcwd(), os.path.dirname(__file__))))

    arg_parser = argparse.ArgumentParser()

    mode_args = arg_parser.add_mutually_exclusive_group(required=True)
    mode_args.add_argument('-P', '--dump-pickle', type=argparse.FileType('wb'),
                           help="Dump the IR to a Python pickle")
    mode_args.add_argument("-o", "--output",
                           help="Output file (default stdout)",
                           type=argparse.FileType('w', encoding='utf-8'),
                           default=sys.stdout)

    arg_parser.add_argument('-t', '--template',
                            type=argparse.FileType('r', encoding='utf-8'),
                            help="Template file used to generate output")
    arg_parser.add_argument('--public', action='store_true',
                            help="Include only public API types")
    arg_parser.add_argument('--traceback', action="store_true",
                            help="Print a full traceback if an error occurs")
    arg_parser.add_argument("-a", "--abi", help="specify the target machine "
                            "compiler ABI name", choices=abi_classes.keys(),
                            required=True)
    arg_parser.add_argument("-f", "--formatter",
                            help="specify clang-format to format the code")
    arg_parser.add_argument("-d", "--deps", default=None,
                            type=argparse.FileType('w', encoding='utf-8'),
                            help="write implicit dependencies to a Makefile")
    arg_parser.add_argument("input", metavar='INPUT', nargs="*",
                            type=argparse.FileType('r', encoding='utf-8'),
                            help="Input type DSL files to process")
    arg_parser.add_argument('-p', '--load-pickle',
                            type=argparse.FileType('rb'),
                            help="Load the IR from a Python pickle")

    options = arg_parser.parse_args()

    # Calling sanity checks
    if options.input and options.load_pickle:
        logger.error("Cannot specify both inputs and --load-pickle")
        arg_parser.print_usage()
        sys.exit(1)

    grammar_file = os.path.join(__loc__, 'grammars', 'typed_dsl.lark')

    parser = Lark.open(grammar_file, 'start', propagate_positions=True)

    if options.input:
        try:
            ir = parse_dsl(parser, options.input, options.abi)
        except (DSLError, RangeError) as e:
            if options.traceback:
                import traceback
                traceback.print_exc(file=sys.stderr)
            else:
                logger.error("Parse error", e)
            sys.exit(1)

        if options.dump_pickle:
            pickle.dump(ir, options.dump_pickle, protocol=4)
    elif options.load_pickle:
        ir = pickle.load(options.load_pickle)
    else:
        logger.error("Must specify inputs or --load-pickle")
        arg_parser.print_usage()
        sys.exit(1)

    if not options.dump_pickle:
        result = apply_template(ir, options.template,
                                public_only=options.public)

        if options.formatter:
            ret = subprocess.run([options.formatter],
                                 input=result.encode("utf-8"),
                                 stdout=subprocess.PIPE)
            result = ret.stdout.decode("utf-8")
            if ret.returncode != 0:
                logger.error("Error formatting output", result)
                sys.exit(1)

        options.output.write(result)
        options.output.close()

    if options.deps is not None:
        deps = set()
        deps.add(grammar_file)
        if options.template is not None:
            deps.add(options.template.name)
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
        # TODO: include Cheetah templates
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
