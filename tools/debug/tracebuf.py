#!/usr/bin/env python3

# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Convert a trace buffer binary file to text form.
"""

import os
import struct
import argparse
import sys
import tempfile
import subprocess
import itertools
import math


MAP_ID = 0x14
UNMAP_ID = 0x15

TRACE_FORMAT = 1

TRACE_IDS = {
    0: "INFO",
    1: "WARN",
    2: "HYPERCALL",
    10: "PANIC",
    11: "ASSERT_FAILED",
    32: "VGIC_VIRQ_CHANGED",
    33: "VGIC_DSTATE_CHANGED",
    34: "VGIC_HWSTATE_CHANGED",
    35: "VGIC_HWSTATE_UNCHANGED",
    36: "VGIC_GICD_WRITE",
    37: "VGIC_GICR_WRITE",
    48: "PSCI_PSTATE_VALIDATION",
    49: "PSCI_VPM_STATE_CHANGED",
    50: "PSCI_VPM_SYSTEM_SUSPEND",
    51: "PSCI_VPM_SYSTEM_RESUME",
    52: "PSCI_VPM_VCPU_SUSPEND",
    53: "PSCI_VPM_VCPU_RESUME",
    54: "PSCI_SYSTEM_SUSPEND",
    55: "PSCI_SYSTEM_RESUME",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs='?', type=argparse.FileType('rb'),
                        default=sys.stdin.buffer, help="Trace binary file")
    image_args = parser.add_mutually_exclusive_group()
    image_args.add_argument("--elf", '-e', type=argparse.FileType('rb'),
                            help="ELF image")
    image_args.add_argument("--binary", '-b', type=argparse.FileType('rb'),
                            help="Binary hypervisor image")
    timestamp_args = parser.add_mutually_exclusive_group()
    timestamp_args.add_argument("--freq", '-f', type=int, default=19200000,
                                help="Timer frequency in Hz")
    timestamp_args.add_argument("--ticks", '-t', action="store_true",
                                help="Show time in ticks instead of seconds")
    parser.add_argument('-T', "--time-offset", default=0, type=float,
                        help="Offset to subtract from displayed timestamps"
                        " (in the same units as the timestamp)")
    format_args = parser.add_mutually_exclusive_group()
    format_args.add_argument('-s', "--sort", action="store_const",
                             dest='sort', const='s',
                             help="Sort entries by timestamp")
    format_args.add_argument('-r', "--raw", action="store_const",
                             dest='sort', const='u',
                             help="Entries as positioned in trace ring buffer")
    format_args.add_argument('-m', "--merge", action="store_const",
                             dest='sort', const='m',
                             help="Entries merged and sorted by timestamp")
    parser.add_argument("--show-missing", action="store_true",
                        help="Mark invalid or overwritten log entries")
    parser.add_argument('-o', "--output", default=sys.stdout,
                        type=argparse.FileType('w', encoding='utf-8'),
                        help="Output text file")
    parser.set_defaults(sort='s')
    args = parser.parse_args()

    global image
    image = ()
    if args.elf is not None:
        with tempfile.TemporaryDirectory() as tmpdir:
            binfile = os.path.join(tmpdir, 'hyp.bin')
            objcopy = os.path.join(os.environ['LLVM'], 'bin',
                                   'llvm-objcopy')
            subprocess.check_call([objcopy, '-j', '.text',
                                   '-j', '.rodata', '-O', 'binary',
                                   args.elf.name, binfile])
            with open(binfile, 'rb') as binfile:
                image = binfile.read()
    elif args.binary is not None:
        image = args.binary.read()

    entry_iter = read_all_entries(args)
    log = prepare_log(args, entry_iter)
    print_log(args, log)


class Arg(int):

    __cache = {}

    def __new__(cls, value, strict=False):
        if value in Arg.__cache:
            return Arg.__cache[value]
        self = super().__new__(cls, value)
        self.__strict = strict
        Arg.__cache[value] = self
        self.__str = self.__gen_str()
        return self

    def __format__(self, format_spec):
        if format_spec.endswith('s'):
            return str(self).__format__(format_spec)
        return super().__format__(format_spec)

    def __gen_str(self):
        try:
            bs = bytearray()
            assert (self & 0x1fffff) < len(image)
            for i in range((self & 0x1fffff), len(image)):
                b = image[i]
                if b == 0:
                    break
                bs.append(b)
                if len(bs) > 512:
                    break
            return bs.decode('utf-8')
        except Exception:
            if self.__strict:
                raise
            return '<str:{:#x}>'.format(self)

    def __str__(self):
        return self.__str


class LogEntry:
    def __init__(self, ticks, cpu_id, string=''):
        self.ticks = ticks
        self.cpu_id = cpu_id
        self.__str = string

    def __str__(self):
        return self.__str

    def set_string(self, string):
        self.__str = string


class Event(LogEntry):

    __slots__ = ('ticks', 'trace_id', 'trace_ids', 'cpu_id', 'missing_before',
                 'missing_after', '__str')

    def __init__(self, args, info, tag, fmt_ptr, arg0, arg1, arg2, arg3, arg4):
        if info == 0:
            # Empty trace slot
            raise ValueError("empty slot")

        ticks = info & ((1 << 56) - 1)
        cpu_id = info >> 56

        super().__init__(ticks, cpu_id)

        self.trace_id = tag & 0xffff

        if TRACE_FORMAT == 1:
            self.trace_ids = (tag >> 16) & 0xffffffff
            vmid = self.trace_ids & 0xffff
            vcpu = (self.trace_ids >> 16) & 0xffff
            caller_id = '{:#04x}:{:#02d}'.format(vmid, vcpu)
        else:
            self.trace_ids = 0
            caller_id = ''

        if self.trace_id in TRACE_IDS:
            trace_id = TRACE_IDS[self.trace_id]
        else:
            trace_id = '{:#06x}'.format(self.trace_id)

        # Try to obtain a C string at the given offset
        try:
            fmt = str(Arg(fmt_ptr, strict=True))
        except Exception:
            fmt = "? fmt_ptr {:#x}".format(fmt_ptr) + \
                " args {:#x} {:#x} {:#x} {:#x} {:#x}"

        # Try to format the args using the given format string
        try:
            msg = fmt.format(Arg(arg0), Arg(arg1), Arg(arg2), Arg(arg3),
                             Arg(arg4))
        except Exception:
            msg = ("? fmt_str {:s} args {:#x} {:#x} {:#x} {:#x} {:#x}"
                   .format(fmt, arg0, arg1, arg2, arg3, arg4))

        if args.ticks:
            rel_time = int(self.ticks - args.time_offset)
            abs_time = int(self.ticks)
            if args.time_offset:
                ts_str = "[{:12d}/{:12d}]".format(rel_time, abs_time)
            else:
                ts_str = "[{:12d}]".format(abs_time)

        else:
            rel_time = (float(self.ticks) / args.freq) - args.time_offset
            abs_time = float(self.ticks) / args.freq
            if args.time_offset:
                ts_str = "[{:12.6f}/{:12.6f}]".format(rel_time, abs_time)
            else:
                ts_str = "[{:12.6f}]".format(abs_time)

        self.set_string("{:s} <{:d}> {:s} {:s} {:s}\n".format(
            ts_str, self.cpu_id, caller_id, trace_id, msg))

        self.missing_before = False
        self.missing_after = False


def read_entries(args):
    header = args.input.read(64)
    if not header or (len(header) < 64):
        # Reached end of file
        if header:
            print("<skipped trailing bytes>\n")
        raise StopIteration

    magic = struct.unpack('<L', header[:4])[0]
    if magic == 0x46554236:  # 6BUF
        endian = '<'
    elif magic == 0x36425568:  # FUB6
        endian = '>'
    else:
        print("Unexpected magic number {:#x}".format(magic))
        raise StopIteration

    cpu_mask = struct.unpack(endian + 'QQQQ', header[8:40])

    entries_max = struct.unpack(endian + 'L', header[4:8])[0]
    head_index = struct.unpack(endian + 'L', header[40:44])[0]

    # Check if this buffer has wrapped around. Since the older traces that
    # don't implement this flag will read it as zero, to stay backwards
    # compatible, we decode a 0 as "wrapped" and 1 as "unwrapped".
    wrapped = True if header[44:45] == b'\x00' else False
    # If wrapped around or old format, read the whole buffer, otherwise only
    # read the valid entries
    entry_count = entries_max if wrapped else head_index

    if entry_count == 0:
        # Empty buffer, skip over the unused bytes
        print("Empty buffer")
        args.input.seek(entries_max * 64, 1)
        return iter(())

    entries = []
    for i in range(entry_count):
        trace = args.input.read(64)
        try:
            entries.append(Event(args, *struct.unpack(endian + "QQQQQQQQ",
                                                      trace)))
        except ValueError:
            pass

    cpu_mask = cpu_mask[0] | (cpu_mask[1] << 64) | (cpu_mask[2] << 128) | \
        (cpu_mask[2] << 192)
    global_buffer = cpu_mask == 0

    cpus = ''
    while cpu_mask != 0:
        msb = cpu_mask.bit_length() - 1
        cpus += '{:d}'.format(msb)
        cpu_mask &= ~(1 << msb)
        if cpu_mask != 0:
            cpus += ','

    if args.sort == 'm':
        if global_buffer:
            header_string = "=== GLOBAL TRACES START ===\n"
        else:
            header_string = "=== CPU {:s} TRACES START ===\n".format(cpus)
    else:
        if global_buffer:
            header_string = "=== GLOBAL TRACE ===\n"
        else:
            header_string = "=== CPU {:s} TRACE ===\n".format(cpus)

    if not wrapped:
        first_index = 0
    else:
        first_index = head_index

    # Add the same timestamp as the first entry
    entry_header = LogEntry(entries[first_index].ticks, 0, header_string)

    if args.sort == 's':
        # Split at the head index
        entry_iter = itertools.chain(
            [entry_header], entries[head_index:], entries[:head_index])
    else:
        entry_iter = itertools.chain([entry_header], entries)

    if not wrapped:
        # Skip over the unused bytes
        args.input.seek((entries_max - head_index) * 64, 1)

    return entry_iter


def read_all_entries(args):
    def entry_iters():
        if args.sort == 'm':
            yield [LogEntry(0, 0, "==== MERGED CPU AND GLOBAL TRACES ====\n")]

        while True:
            try:
                yield read_entries(args)
            except StopIteration:
                break

    return itertools.chain(*entry_iters())


def prepare_log(args, entry_iter):
    if args.show_missing:
        # Simple search for missing entries: look for either an invalid info
        # field, or a timestamp jumping backwards.
        #
        # If the timestamp jumps backwards by less than 10 ticks, we assume
        # that it was an out-of-order trace write due to a race to obtain a
        # slot. This typically suppresses several false positives in any large
        # trace buffer.
        timestamp_slack = 10
        last_timestamp = -math.inf
        missing_entry = False
        log = []
        for entry in entry_iter:
            if entry is None:
                missing_entry = True
                if log:
                    log[-1].missing_after = True
            else:
                if missing_entry:
                    entry.missing_before = True
                    missing_entry = False

                timestamp = entry.ticks
                if timestamp + timestamp_slack < last_timestamp:
                    entry.missing_before = True
                    if log:
                        log[-1].missing_after = True

                last_timestamp = timestamp
                log.append(entry)
    else:
        log = list(entry_iter)

    if args.sort == 'm':
        log = sorted(log, key=lambda e: e.ticks)

    if len(log) == 0:
        sys.exit(1)

    return log


def print_log(args, log):
    prev_entry = None
    for entry in log:
        if args.show_missing and (prev_entry is not None and (
                entry.missing_before or prev_entry.missing_after)):
            args.output.write("<possible missing entries>\n")
        args.output.write(str(entry))
        prev_entry = entry


if __name__ == "__main__":
    main()
