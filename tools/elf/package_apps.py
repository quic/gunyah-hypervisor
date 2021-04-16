#!/usr/bin/env python3

# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import logging
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.constants import SH_FLAGS, P_FLAGS
from elftools import construct


class NewSegment():
    def __init__(self, base, p_align=16):
        self._data = b''
        hdr = construct.lib.Container()
        hdr.p_type = 'PT_LOAD'
        hdr.p_flags = P_FLAGS.PF_R
        hdr.p_offset = 0
        hdr.p_vaddr = 0
        hdr.p_paddr = 0
        hdr.p_filesz = 0
        hdr.p_memsz = 0
        hdr.p_align = p_align
        self.header = hdr
        # print(self.header)

    def add_data(self, data):
        n = len(data)
        self._data += data

        self.header.p_filesz += n
        self.header.p_memsz += n
        # print(self.header)


class NewELF():
    def __init__(self, base):
        self.structs = base.structs

        self.header = base.header
        # print(self.header)

        self.segments = []
        self.sections = []

        for i in range(0, base.num_segments()):
            seg = base.get_segment(i)
            seg._data = seg.data()
            self.segments.append(seg)
            # print("   ", self.segments[i].header)

        for i in range(0, base.num_sections()):
            sec = base.get_section(i)
            sec._data = sec.data()
            self.sections.append(sec)
            # print("   ", self.sections[i].header)

    def strip(self):
        print("strip() unimplemented")

    def merge_segments(self, elf):
        print("merging...")

        p_last = 0
        # Find the end of the last segment
        for seg in self.segments:
            last = seg.header.p_offset + seg.header.p_filesz
            if last > p_last:
                p_last = last

        p_adj = p_last

        # Append new segments
        for i in range(0, elf.num_segments()):
            seg = elf.get_segment(i)
            seg._data = seg.data()

            p_last = (p_last + (seg.header.p_align - 1)) & \
                     (0xffffffffffffffff ^ (seg.header.p_align - 1))
            # print(hex(p_last))
            seg.header.p_offset = p_last
            # print(seg.header)
            self.segments.append(seg)
            self.header.e_phnum += 1

            p_last = p_last + seg.header.p_filesz

        p_off = p_last - p_adj
        # print(">>", hex(p_adj), hex(p_last), hex(p_off))

        # Adjust file offsets for affected sections
        for sec in self.sections:
            if sec.header.sh_offset >= p_adj:
                # print(sec.header)
                align = sec.header.sh_addralign
                if align > 1:
                    p_off = (p_off + (align - 1)) & \
                            (0xffffffffffffffff ^ (align - 1))
                # print("SA", hex(sec.header.sh_offset), hex(p_off),
                #       hex(sec.header.sh_offset + p_off))
                sec.header.sh_offset += p_off

        if self.header.e_shoff >= p_adj:
            self.header.e_shoff += p_off

    def append_segment(self, newseg):
        print("appending...")

        p_last = 0
        # Find the end of the last segment
        for seg in self.segments:
            last = seg.header.p_offset + seg.header.p_filesz
            if last > p_last:
                p_last = last

        p_adj = p_last

        # Append new segment
        p_last = (p_last + (newseg.header.p_align - 1)) & \
                 (0xffffffffffffffff ^ (newseg.header.p_align - 1))
        # print(hex(p_last))
        newseg.header.p_offset = p_last
        # print(newseg.header)
        self.segments.append(newseg)
        self.header.e_phnum += 1

        p_last = p_last + newseg.header.p_filesz

        p_off = p_last - p_adj
        # print(">>", hex(p_adj), hex(p_last), hex(p_off))

        # Adjust file offsets for affected sections
        for sec in self.sections:
            if sec.header.sh_offset >= p_adj:
                # print(sec.header)
                align = sec.header.sh_addralign
                if align > 1:
                    p_off = (p_off + (align - 1)) & \
                            (0xffffffffffffffff ^ (align - 1))
                # print("SA", hex(sec.header.sh_offset), hex(p_off),
                #       hex(sec.header.sh_offset + p_off))
                sec.header.sh_offset += p_off

        if self.header.e_shoff >= p_adj:
            self.header.e_shoff += p_off

    def write(self, f):

        print("writing...")

        # print("EH", self.header)

        # Write out the ELF header
        f.seek(0)
        self.structs.Elf_Ehdr.build_stream(self.header, f)

        # Write out the ELF program headers
        f.seek(self.header.e_phoff)
        for seg in self.segments:
            # print("PH", seg.header)
            self.structs.Elf_Phdr.build_stream(seg.header, f)

        # Write out the ELF segment data
        for seg in self.segments:
            f.seek(seg.header.p_offset)
            f.write(seg._data)

        # Write out the ELF section headers
        f.seek(self.header.e_shoff)
        for sec in self.sections:
            # print("SH", sec.header)
            self.structs.Elf_Shdr.build_stream(sec.header, f)

        # Write out the ELF non-segment based sections
        for sec in self.sections:
            # Copy extra sections, mostly strings and debug
            if sec.header.sh_flags & SH_FLAGS.SHF_ALLOC == 0:
                # print("SH", sec.header)
                f.seek(sec.header.sh_offset)
                f.write(sec._data)
                continue


def package_files(base, app, runtime, output):

    base_elf = ELFFile(base)
    new = NewELF(base_elf)

    symtab = base_elf.get_section_by_name('.symtab')
    pkg_phys = symtab.get_symbol_by_name('image_pkg_start')
    if pkg_phys:
        print(pkg_phys[0].name, hex(pkg_phys[0].entry.st_value))
        pkg_phys = pkg_phys[0].entry.st_value
    else:
        logging.error("can't find symbol 'image_pkg_start'")
        sys.exit(1)

    # Describe the package header structure
    pkg_hdr = construct.Struct(
        'pkg_hdr',
        construct.ULInt32('ident'),
        construct.ULInt32('items'),
        construct.Array(
            3,
            construct.Struct(
                'list',
                construct.ULInt32('type'),
                construct.ULInt32('offset'))
        ),
    )
    hdr = construct.lib.Container()
    # Initialize package header
    hdr.ident = 0x47504b47  # GPKG
    hdr.items = 0
    items = []
    for i in range(0, 3):
        item = construct.lib.Container()
        item.type = 0
        item.offset = 0
        items.append(item)
    hdr.list = items
    hdr_len = len(pkg_hdr.build(hdr))

    # Add the runtime ELF image
    run_data = runtime.read()
    run_data_len = len(run_data)

    pad = ((run_data_len + 0x1f) & ~0x1f) - run_data_len
    if pad:
        run_data += b'\0' * pad
        run_data_len += pad
    hdr.list[0].type = 0x1  # Runtime
    hdr.list[0].offset = hdr_len
    hdr.items += 1

    # Add the application ELF image
    app_data = app.read()
    app_data_len = len(app_data)

    pad = ((app_data_len + 0x1f) & ~0x1f) - app_data_len
    if pad:
        app_data += b'\0' * pad
        app_data_len += pad

    hdr.list[1].type = 0x2  # Application
    hdr.list[1].offset = hdr_len + run_data_len
    hdr.items += 1

    # note, we align segment to 4K for signing tools
    segment = NewSegment(base_elf, 4096)

    segment.add_data(pkg_hdr.build(hdr))
    segment.add_data(run_data)
    segment.add_data(app_data)

    segment.header.p_paddr = pkg_phys

    new.append_segment(segment)
    new.write(output)


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

    args = argparse.ArgumentParser()

    args.add_argument('-a', "--app",
                      type=argparse.FileType('rb'),
                      help="Input application ELF",
                      required=True)
    args.add_argument('-r', "--runtime",
                      type=argparse.FileType('rb'),
                      help="Input runtime ELF",
                      required=True)
    args.add_argument('-o', '--output',
                      type=argparse.FileType('wb'),
                      default=sys.stdout,
                      required=True,
                      help="Write output to file")
    args.add_argument('input', metavar='INPUT', nargs=1,
                      type=argparse.FileType('rb'),
                      help="Input hypervisor ELF")
    options = args.parse_args()

    package_files(options.input[0], options.app, options.runtime,
                  options.output)


if __name__ == '__main__':
    main()
