#!/usr/bin/env python3
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import os
from Cheetah.Template import Template
from Cheetah import ImportHooks


def xreg_range(*args):
    return tuple('x{:d}'.format(r) for r in range(*args))


templates_dir = os.path.join('tools', 'hypercalls', 'templates')
abis = {}


class abi():
    def __init__(self, hypcall_base):
        self.hypcall_base = hypcall_base


class abi_aarch64(abi):
    def __init__(self, hypcall_base):
        super().__init__(hypcall_base)

        # The size in bytes of each machine register
        self.register_size = 8

        # Registers used for parameters and results. Note that we don't
        # support indirect results (i.e. structs larger than 16 bytes).
        self.parameter_reg = xreg_range(0, 8)
        self.result_reg = xreg_range(0, 8)

        # Registers clobbered by the hypervisor.
        self.caller_saved_reg = xreg_range(8, 18)

    @classmethod
    def register_name(cls, size, index):
        reg_type = "x" if size == 8 else "w"
        return "{}{}".format(reg_type, index)


# HVC 0 is used for ARM SMCCC (PSCI, etc). Gunyah uses 0x6XXX
abis['aarch64'] = abi_aarch64(0x6000)

# Dictionary with all hypercalls defined
hypcall_dict = dict()


class Variable:
    def __init__(self, ctype, name, type_definition):
        self.ctype = ctype
        self.name = name
        self.size = type_definition.size
        self.category = type_definition.category
        self.ignore = name.startswith('_')
        self.pointer = False
        try:
            from ir import PointerType
            d = type_definition
            if isinstance(d.compound_type, PointerType):
                self.pointer = True
        except AttributeError:
            pass
        if self.ignore:
            if name.startswith('_res0'):
                self.default = 0
            elif name.startswith('_res1'):
                self.default = 0xffffffffffffffff
            else:
                raise Exception("Invalid ignored name {:s}".format(name))


class Hypercall:
    def __init__(self, name, num, abi):
        self.name = name
        self.num = num
        self.used_regs = set()
        self.inputs = []
        self.input_count = 0
        self.outputs = []
        self.output_count = 0
        self.clobbers = set()
        self.abi = abis[abi]

        self.hvc_num = "0x{:x}".format(self.abi.hypcall_base + num)

    def check_type(self, var, role):
        if var.size > self.abi.register_size:
            raise Exception('Hypercall {:s}: {:s} {:s} has type {:s}, which '
                            'does not fit in a {:d}-byte machine register '
                            '(size is {:d} bytes)'.format(
                                self.name, role, var.name, var.ctype,
                                self.abi.register_size, var.size))

    def add_input(self, input):
        self.check_type(input, 'input')
        reg = self.abi.parameter_reg[self.input_count]
        self.used_regs.add(reg)
        self.inputs.append((reg, input))
        self.input_count += 1

    def add_output(self, output):
        self.check_type(output, 'output')
        reg = self.abi.result_reg[self.output_count]
        self.used_regs.add(reg)
        self.outputs.append((reg, output))
        self.output_count += 1

    def finalise(self):
        hypcall_dict[self.num] = self

        self.inputs = tuple(self.inputs)
        self.outputs = tuple(self.outputs)

        # Calculate register clobber list for guest interface
        self.clobbers.update((x for x in self.abi.parameter_reg
                              if x not in self.used_regs))
        self.clobbers.update((x for x in self.abi.result_reg
                              if x not in self.used_regs))
        self.clobbers.update(self.abi.caller_saved_reg)


ns = locals()


def apply_template(template_file):
    ImportHooks.install()
    template = Template(file=template_file, searchList=ns)
    result = str(template)
    ImportHooks.uninstall()
    return result
