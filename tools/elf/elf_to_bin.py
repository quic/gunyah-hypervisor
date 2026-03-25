#!/usr/bin/env python3

# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Extract ELF segments to binary using program headers (PT_LOAD segments).
This is similar to objcopy -O binary, but uses program headers instead of
section headers, which is what a bootloader would do.
"""

import argparse
import sys
from elftools.elf.elffile import ELFFile


def elf_to_binary(elf_file, output_file, base_addr=None):
    """
    Extract PT_LOAD segments from an ELF file to a flat binary.

    Args:
        elf_file: Input ELF file object
        output_file: Output binary file object
        base_addr: Optional base address to subtract from segment addresses.
                   If None, uses the lowest segment address.
    """
    elffile = ELFFile(elf_file)

    # Find all PT_LOAD segments
    load_segments = []
    for segment in elffile.iter_segments():
        if segment['p_type'] == 'PT_LOAD' and segment['p_filesz'] > 0:
            load_segments.append(segment)

    if not load_segments:
        print("Error: No PT_LOAD segments found in ELF file", file=sys.stderr)
        sys.exit(1)

    # Sort segments by physical address
    load_segments.sort(key=lambda s: s['p_paddr'])

    # Determine base address
    if base_addr is None:
        base_addr = load_segments[0]['p_paddr']

    print(f"Base address: {base_addr:#x}")
    print(f"Found {len(load_segments)} PT_LOAD segments:")

    # Calculate total size needed
    last_segment = load_segments[-1]
    total_size = (last_segment['p_paddr'] +
                  last_segment['p_memsz']) - base_addr

    # Create output buffer filled with zeros
    output_data = bytearray(total_size)

    # Copy each segment's data
    for i, segment in enumerate(load_segments):
        paddr = segment['p_paddr']
        offset = paddr - base_addr
        filesz = segment['p_filesz']
        memsz = segment['p_memsz']

        print(f"  Segment {i}: paddr={paddr:#x} offset={offset:#x} "
              f"filesz={filesz:#x} memsz={memsz:#x}")

        if offset < 0:
            print(f"Error: Segment {i} address {paddr:#x} is below base "
                  f"address {base_addr:#x}", file=sys.stderr)
            sys.exit(1)

        # Copy file data
        segment_data = segment.data()
        output_data[offset:offset + filesz] = segment_data

        # Note: memsz > filesz region is already zero-filled (BSS)

    print(f"Writing {len(output_data)} bytes to output file")
    output_file.write(output_data)


def main():
    parser = argparse.ArgumentParser(
        description='Extract ELF segments to binary using program headers',
        epilog='This tool extracts PT_LOAD segments from an ELF file to create'
               ' a flat binary, similar to what a bootloader would load. '
               'Unlike objcopy -O binary which uses section headers, this uses'
               ' program headers (PT_LOAD segments).'
    )

    parser.add_argument('input', type=argparse.FileType('rb'),
                        help='Input ELF file')
    parser.add_argument('-o', '--output', type=argparse.FileType('wb'),
                        required=True,
                        help='Output binary file')
    parser.add_argument('-b', '--base-addr', type=lambda x: int(x, 0),
                        help='Base address to subtract from segment addresses '
                             '(default: lowest segment paddr)')

    args = parser.parse_args()

    try:
        elf_to_binary(args.input, args.output, args.base_addr)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
