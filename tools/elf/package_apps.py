#!/usr/bin/env python3

# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import logging
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.constants import SH_FLAGS, P_FLAGS
from elftools import construct

PAGE_SIZE = 4096


def is_pow2_non_zero(val):
    return (val > 0) and ((val & (val - 1)) == 0)


def align_up(addr, size):
    assert is_pow2_non_zero(size)

    return (addr + (size - 1)) & (0xffffffffffffffff ^ (size - 1))


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

        assert (p_align == 0) or is_pow2_non_zero(p_align)
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

        self.dtb = None

        paddr_last = 0
        for i in range(0, base.num_segments()):
            seg = base.get_segment(i)
            seg._data = seg.data()
            self.segments.append(seg)
            hdr = seg.header
            # print("   ", hdr)
            if hdr.p_filesz and (paddr_last > hdr.p_paddr):
                raise RuntimeError("Input elf segments not sorted by p_paddr")
            paddr_last = hdr.p_paddr
            assert (not hdr.p_align) or is_pow2_non_zero(hdr.p_align)

            if hdr.p_align > PAGE_SIZE:
                # This can happen if the linker thinks the p_offset mod p_vaddr
                # and segment size heuristics would better support a larger
                # alignment.
                # Reduce the alignment to PAGE_SIZE
                hdr.p_align = PAGE_SIZE
                print("reduce segment index {:d} alignment to {:#x}"
                      .format(i, PAGE_SIZE))

        for i in range(0, base.num_sections()):
            sec = base.get_section(i)
            sec._data = sec.data()
            self.sections.append(sec)
            hdr = sec.header
            # print("   ", hdr)
            assert (not hdr.sh_addralign) or is_pow2_non_zero(hdr.sh_addralign)

            if hdr.sh_addralign > PAGE_SIZE:
                # This can happen if the linker thinks the p_offset mod p_vaddr
                # and section size heuristics would better support a larger
                # alignment.
                # Reduce the alignment to PAGE_SIZE
                hdr.sh_addralign = PAGE_SIZE
                print("reduce section index {:d} alignment to {:#x}"
                      .format(i, PAGE_SIZE))

    def strip(self):
        print("strip() unimplemented")

    # Insert a segment into a pre-sorted ELF
    def insert_segment(self, newseg, phys):
        print("inserting...")

        phys_offset = self.segments[0].header.p_paddr - \
            self.segments[0].header.p_vaddr
        newseg.header.p_paddr = phys
        newseg.header.p_vaddr = phys - phys_offset

        idx = 0
        insert = False
        offset_last = 0
        # Find the position to insert segment
        for seg in self.segments:
            if seg.header.p_paddr > newseg.header.p_paddr:
                insert = True
                break
            idx += 1
            seg_last = seg.header.p_offset + seg.header.p_filesz
            if offset_last < seg_last:
                offset_last = seg_last

        offset_adj = 0

        assert newseg.header.p_align == PAGE_SIZE
        if insert and (seg.header.p_align != PAGE_SIZE):
            # Its possible a TLS or similar segment with start addr matching a
            # following PT_LOAD segment is encountered. Currently this isn't
            # supported.
            raise RuntimeError("unsupported ELF segment alignment")

        # Align p_offset for new segment
        if insert:
            # insert before 'seg'
            p_offset = seg.header.p_offset
        else:
            # append after 'seg'
            p_offset = align_up(seg_last, PAGE_SIZE)
        p_offset_insert = p_offset

        p_offset = align_up(p_offset, PAGE_SIZE)
        # print('p_offset', hex(p_offset))
        newseg.header.p_offset = p_offset
        p_offset += newseg.header.p_filesz
        # print('p_filesz', hex(newseg.header.p_filesz))
        p_offset = align_up(p_offset, PAGE_SIZE)

        offset_adj = p_offset - p_offset_insert
        # print('offset_adj', hex(offset_adj))

        # Update file offsets of moved segments and sections
        for seg in self.segments:
            seg_last = seg.header.p_offset + seg.header.p_filesz
            if offset_last != seg_last and seg_last >= p_offset_insert:
                seg.header.p_offset += offset_adj

        for sec in self.sections:
            if sec.header.sh_flags & SH_FLAGS.SHF_ALLOC:
                offset_base = p_offset_insert
                adjust = offset_adj
            else:
                offset_base = offset_last
                align = align_up(offset_base, PAGE_SIZE)
                if insert:
                    adjust = offset_adj
                else:
                    adjust = offset_adj + (align - offset_base)

            if sec.header.sh_offset >= offset_base:
                # print(sec.header)
                sec.header.sh_offset += adjust

        if self.header.e_shoff >= p_offset_insert:
            self.header.e_shoff += p_offset_insert

        # Insert the new segment at requested index
        self.segments.insert(idx, newseg)
        self.header.e_phnum += 1

    # Align LOAD segment's p_filesz
    def segment_filesz_align(self, align):
        print("segment align...")

        assert (align & (align - 1)) == 0
        last_end = 0

        # Adjust file offsets for affected sections
        for seg in self.segments:
            # print(seg, seg.header.p_type)
            if seg.header.p_type == 'PT_LOAD':
                assert seg.header.p_offset >= last_end
                if seg.header.p_align < align:
                    print('WARN: segment {:#x} / {:#x} p_align < {:d}'.format(
                          seg.header.p_paddr, seg.header.p_vaddr, align))
                    continue
                p_filesz = seg.header.p_filesz
                seg.header.p_filesz += align - 1
                seg.header.p_filesz &= ~(align - 1)
                if p_filesz < seg.header.p_filesz:
                    assert len(seg._data) == p_filesz
                    pad = bytes([0] * (seg.header.p_filesz-p_filesz))
                    seg._data = seg._data + pad
                if seg.header.p_memsz < seg.header.p_filesz:
                    seg.header.p_memsz = seg.header.p_filesz
                last_end = seg.header.p_offset + seg.header.p_filesz

    # Align LOAD segment's p_memsz
    def segment_memsz_align(self, align):
        print("segment memsz align...")

        assert (align & (align - 1)) == 0

        # Align p_memsz for all LOAD segments
        for seg in self.segments:
            if seg.header.p_type == 'PT_LOAD':
                p_memsz = seg.header.p_memsz
                seg.header.p_memsz += align - 1
                seg.header.p_memsz &= ~(align - 1)
                if p_memsz != seg.header.p_memsz:
                    print(
                        'Aligned segment {:#x} / {:#x} p_memsz from '
                        '{:#x} to {:#x}'.format(
                            seg.header.p_paddr,
                            seg.header.p_vaddr,
                            p_memsz,
                            seg.header.p_memsz,
                        )
                    )

    # Merge physically adjacent segments
    def merge_physical(self):
        print("merge physical...")

        prev = None

        next_list = []

        # Adjust file offsets for affected sections
        for seg in self.segments:
            # print(seg, seg.header.p_type)
            if seg.header.p_type != 'PT_LOAD':
                next_list.append(seg)
                continue
            if prev:
                prev_end = prev.header.p_paddr + prev.header.p_filesz
                if prev_end == seg.header.p_paddr:
                    assert prev.header.p_filesz == prev.header.p_memsz
                    prev.header.p_filesz += seg.header.p_filesz
                    prev.header.p_memsz += seg.header.p_memsz
                    prev.header.p_flags |= seg.header.p_flags
                    self.header.e_phnum -= 1
                    prev._data = prev._data + seg._data
                    # self.segments.remove(seg)
                    # seg = prev
                else:
                    next_list.append(seg)
                    prev = seg
            else:
                next_list.append(seg)
                prev = seg
        self.segments = next_list

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
            if seg.header.p_type in ['PT_DYNAMIC', 'PT_TLS']:
                continue
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
            if (sec.header.sh_flags & SH_FLAGS.SHF_ALLOC) == 0:
                # print("SH", sec.header)
                f.seek(sec.header.sh_offset)
                f.write(sec._data)
                continue

        # If we have a dtb, find the device tree section and copy the dtb into
        # that section.
        if self.dtb:
            sec = [x for x in self.sections if x.name == '.data.devicetree']
            if len(sec) != 1:
                raise ValueError("number of device tree sections != 1")
            sec = sec[0]

            dtb_data = self.dtb.read()
            dtb_data_len = len(dtb_data)
            sec_size = len(sec._data)

            if dtb_data_len > sec_size:
                raise ValueError("dtb size {} > section size {}".format(
                    dtb_data_len, sec_size))

            print("copying dtb to offset {:#x}".format(sec.header.sh_offset))
            f.seek(sec.header.sh_offset)
            f.write(dtb_data)


def package_files(base, app, runtime, output, p_filesz_align=None,
                  p_memsz_align=None, merge_phys=False, dtb=None):

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
    segment = NewSegment(base_elf, PAGE_SIZE)

    segment.add_data(pkg_hdr.build(hdr))
    segment.add_data(run_data)
    segment.add_data(app_data)

    new.insert_segment(segment, pkg_phys)
    if p_filesz_align:
        new.segment_filesz_align(p_filesz_align)
    if p_memsz_align:
        new.segment_memsz_align(p_memsz_align)
    if merge_phys:
        new.merge_physical()
    if dtb:
        if dtb.name[-4:] != '.dtb':
            raise ValueError("provided dtb file does not end with .dtb")
        new.dtb = dtb

    new.write(output)


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )

    args = argparse.ArgumentParser()

    args.add_argument('--segment-size-align',
                      type=int,
                      help="Align p_filesz to page-size")
    args.add_argument('--segment-memsz-align',
                      type=int,
                      help="Align p_memsz to page-size")
    args.add_argument('--merge-phys-segments',
                      action='store_true',
                      help="Merge physically adjacent segments")
    args.add_argument('-a', "--app",
                      type=argparse.FileType('rb'),
                      help="Input application ELF",
                      required=True)
    args.add_argument('-r', "--runtime",
                      type=argparse.FileType('rb'),
                      help="Input runtime ELF",
                      required=True)
    args.add_argument('--dtb',
                      type=argparse.FileType('rb'),
                      help="Input device tree blob")
    args.add_argument('-o', '--output',
                      type=argparse.FileType('wb'),
                      default=sys.stdout,
                      required=True,
                      help="Write output to file")
    args.add_argument('input', metavar='INPUT', nargs=1,
                      type=argparse.FileType('rb'),
                      help="Input hypervisor ELF")
    options = args.parse_args()

    p_filesz_align = options.segment_size_align
    p_memsz_align = options.segment_memsz_align

    if p_filesz_align is not None:
        if (p_filesz_align == 0) or \
           ((p_filesz_align & (p_filesz_align - 1)) != 0):
            raise ValueError("segment-size-align must be a power of 2!")
        if p_filesz_align > PAGE_SIZE:
            raise ValueError(
                "segment-size-align must be <= {:d}".format(PAGE_SIZE))

    if p_memsz_align is not None:
        if (p_memsz_align == 0) or \
           ((p_memsz_align & (p_memsz_align - 1)) != 0):
            raise ValueError("segment-memsz-align must be a power of 2!")
        if p_memsz_align > PAGE_SIZE:
            raise ValueError(
                "segment-memsz-align must be <= {:d}".format(PAGE_SIZE))

    package_files(options.input[0], options.app, options.runtime,
                  options.output, p_filesz_align, p_memsz_align,
                  options.merge_phys_segments, options.dtb)


if __name__ == '__main__':
    main()
