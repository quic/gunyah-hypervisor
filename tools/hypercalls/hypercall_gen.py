#!/usr/bin/env python3
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

from hypercall import Variable, Hypercall, apply_template
from lark import Lark, Tree

import os
import sys
import argparse
import subprocess
import logging
import pickle
import inspect


# Determine the location of this script.
__loc__ = os.path.realpath(os.path.join(os.getcwd(),
                                        os.path.dirname(__file__)))

# The typed directory is added to the sys path so that when the pickle is
# loaded it can find the corresponding ast nodes.
typed_path = os.path.join(__loc__, '..', 'typed')
sys.path.append(typed_path)


# Silence flake8 warning about CType unused. It is required for pickle.load
from abi import AArch64ABI, CType  # noqa: F401,E402


# The template directory is added to the sys path so templates can be imported
# from it.
template_path = os.path.join(__loc__, 'templates')
sys.path.append(template_path)


logger = logging.getLogger(__name__)

abi_classes = {
    'aarch64': AArch64ABI,
}

primitive_types = dict()
types = dict()

used_ids = set()
used_calls = set()


class HypercallObject:
    def __init__(self, name):
        self.name = name
        self.call_num = None
        self.inputs = []
        self.outputs = []
        self.properties = {}


def get_constant(c):
    type_parent = None
    while isinstance(c, Tree):
        type_parent = c.data
        c = c.children[0]

    assert(type_parent == 'constant_value')
    return c


def get_type(c, ir):
    type_parent = None
    while isinstance(c, Tree):
        type_parent = c.data
        c = c.children[0]

    if "primitive_type" in type_parent:
        try:
            d = primitive_types[c]
        except KeyError:
            logger.error("Type: %s not found", c)
            sys.exit(1)

        return (d.c_type_name, d)
    else:
        try:
            d = types[c]
        except KeyError:
            logger.error("Type: %s not found", c)
            sys.exit(1)

        if not d.type_name.endswith('_t'):
            c = c + '_t'
        return (c, d)

    logger.error("unknown type", c)
    sys.exit(1)


def get_hypercalls(tree, hypercalls, hyp_num, ir):
    for c in tree.children:
        if isinstance(c, Tree):
            if c.data == "hypercall_definition":
                name = c.children[0]
                if name in used_calls:
                    logger.error("Hypercall name: %s already used", name)
                    sys.exit(1)
                used_calls.add(name)
                new_hypercall = HypercallObject(name)
                hypercalls.insert(hyp_num, new_hypercall)
                get_hypercalls(c, hypercalls, hyp_num, ir)
                hyp_num += 1
            elif c.data == "hypercall_declaration":
                if isinstance(c.children[0], Tree):
                    node = c.children[0]
                    if node.data == "declaration_call_num":
                        val = get_constant(node.children[0])
                        if hypercalls[hyp_num].call_num is not None:
                            logger.error("Hypercall: %s multiple call_nums",
                                         hypercalls[hyp_num].name)
                            sys.exit(1)

                        call_num = int(str(val), base=0)
                        if call_num in used_ids:
                            logger.error("Hypercall call_num already used",
                                         hypercalls[hyp_num].name)
                            sys.exit(1)
                        used_ids.add(call_num)

                        hypercalls[hyp_num].call_num = call_num
                    elif node.data == "declaration_sensitive":
                        hypercalls[hyp_num].properties['sensitive'] = True
                    elif node.data == "declaration_vendor_hyp_call":
                        if hypercalls[hyp_num].call_num is not None:
                            logger.error(
                                "Hypercall: %s call_num and "
                                "vendor_hyp_call",
                                hypercalls[hyp_num].name)
                            sys.exit(1)
                        hypercalls[hyp_num].call_num = 0
                        hypercalls[hyp_num].properties['vendor_hyp_call'] = \
                            True
                    else:
                        raise TypeError
                elif isinstance(c.children[1], Tree):
                    identifier = str(c.children[0])
                    node = c.children[1]
                    if node.data == "declaration_input":
                        if len(hypercalls[hyp_num].inputs) >= 8:
                            logger.error("Maximum of 8 inputs per hypercall",
                                         hypercalls[hyp_num].name)
                            sys.exit(1)
                        (t, d) = get_type(node.children[0], ir)
                        hypercalls[hyp_num].inputs.append(
                            Variable(t, identifier, d))
                    elif node.data == "declaration_output":
                        if len(hypercalls[hyp_num].outputs) >= 8:
                            logger.error("Maximum of 8 outputs per hypercall",
                                         hypercalls[hyp_num].name)
                            sys.exit(1)
                        (t, d) = get_type(node.children[0], ir)
                        hypercalls[hyp_num].outputs.append(
                            Variable(t, identifier, d))
                    else:
                        raise TypeError
                else:
                    logger.error("internal error")
                    sys.exit(1)

    return hypercalls, hyp_num


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

    arg_parser = argparse.ArgumentParser()

    arg_parser.add_argument("-o", "--output",
                            help="Output file (default stdout)",
                            type=argparse.FileType('w', encoding='utf-8'),
                            default=sys.stdout)
    arg_parser.add_argument('-t', '--template',
                            type=argparse.FileType('r', encoding='utf-8'),
                            help="Template file used to generate output")
    arg_parser.add_argument('--traceback', action="store_true",
                            help="Print a full traceback if an error occurs")
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
                            help="Load the IR from typed Python pickle")
    arg_parser.add_argument("-a", "--abi", help="specify the target machine "
                            "compiler ABI name", choices=abi_classes.keys(),
                            required=True)

    options = arg_parser.parse_args()

    grammar_file = os.path.join(__loc__, '..', 'grammars',
                                'hypercalls_dsl.lark')

    parser = Lark.open(grammar_file, 'start', propagate_positions=True)

    from ir import PrimitiveType
    # Load typed pickle to get the types used for the inputs and output of the
    # hypercall
    ir = pickle.load(options.load_pickle)
    for d in ir.abi_refs:
        if isinstance(d, PrimitiveType):
            if d.indicator not in primitive_types and d.is_public:
                primitive_types[d.indicator] = d
    for d in ir.definitions:
        if d.indicator not in types and d.is_public:
            types[d.indicator] = d

    # Go through all *.hvc files, parse the content, do a top down iteration to
    # get all hypercalls and get the type and size for the inputs and output
    # arguments by searching in the ir of typed.pickle
    hypercalls = []
    hyp_num = 0
    for p in options.input:
        text = p.read()
        parse_tree = parser.parse(text)
        hypercalls, hyp_num = get_hypercalls(
            parse_tree, hypercalls, hyp_num, ir)
    for h in hypercalls:
        hyper = Hypercall(h.name, h.call_num, h.properties, options.abi)
        for i in h.inputs:
            hyper.add_input(i)
        for o in h.outputs:
            hyper.add_output(o)
        hyper.finalise()

    # Apply templates to generate the output code and format it
    result = apply_template(options.template)

    if options.formatter and not options.template.name.endswith('.S.tmpl'):
        ret = subprocess.run([options.formatter],
                             input=result.encode("utf-8"),
                             stdout=subprocess.PIPE)
        result = ret.stdout.decode("utf-8")
        if ret.returncode != 0:
            logger.error("Error formatting output", result)
            sys.exit(1)

    options.output.write(result)
    options.output.close()

    # Write deps last to get template specific imports
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
        options.deps.write(options.output.name + ' : ')
        options.deps.write(' '.join(sorted(deps)))
        options.deps.write('\n')
        options.deps.close()


if __name__ == '__main__':
    main()
