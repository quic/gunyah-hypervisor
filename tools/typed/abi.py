#!/usr/bin/env python3
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import abc


class CType:
    def __init__(self, name, is_signed, size, align=None, bitsize=None):
        self.name = name
        self.is_signed = is_signed
        self.size = size
        self.align = size if align is None else align
        self.bitsize = bitsize


class ABI(metaclass=abc.ABCMeta):
    """
    Abstract base class for ABI definitions.
    """

    def __init__(self):
        basic_ctypes = (
            CType('bool', False, 1, bitsize=1),
            CType('uint8_t', False, 1),
            CType('uint16_t', False, 2),
            CType('uint32_t', False, 4),
            CType('uint64_t', False, 8),
            CType('uintptr_t', False, self.pointer_size,
                  align=self.pointer_align),
            CType('int8_t', True, 1),
            CType('int16_t', True, 2),
            CType('int32_t', True, 4),
            CType('int64_t', True, 8),
            CType('intptr_t', True, self.pointer_size,
                  align=self.pointer_align),
            CType('char', self.signed_char, 1),
            CType('size_t', False, self.pointer_size,
                  align=self.pointer_align),
            CType('uregister_t', False, self.register_size,
                  align=self.register_align),
        )
        self.c_types = {t.name: t for t in basic_ctypes}

    @staticmethod
    def is_power2(val):
        """Returns true if number is a power of two"""
        return ((val & (val - 1)) == 0) and (val > 0)

    @abc.abstractproperty
    def pointer_size(self):
        """The size of a pointer, in bytes."""
        raise NotImplementedError

    @property
    def pointer_align(self):
        """The alignment of a pointer, in bytes."""
        return self.pointer_size

    @abc.abstractproperty
    def register_size(self):
        """The size of a register, in bytes."""
        raise NotImplementedError

    @property
    def register_align(self):
        """The alignment of a register, in bytes."""
        return self.pointer_size

    @abc.abstractproperty
    def signed_char(self):
        """True if the char type is signed."""
        raise NotImplementedError

    def get_c_type(self, type_name):
        return self.c_types[type_name]

    @abc.abstractmethod
    def get_c_type_name(self, abi_type_name):
        """Return the c name for the abi type name."""
        raise NotImplementedError

    def layout_struct_member(self, current_offset, current_alignment,
                             next_size, next_alignment):
        """
        Return the offset at which a new struct member should be placed.

        In principle this is entirely implementation-defined, but nearly all
        implementations use the same algorithm: add enough padding bytes to
        align the next member, and no more. Any ABI that does something
        different can override this.

        The current_offset argument is the size of the structure in bytes up
        to the end of the last member. This is always nonzero, because
        structures are not allowed to be padded at the start, so this function
        is only called for the second member onwards.

        The current_align argument is the largest alignment that has been seen
        in the members added to the struct so far.

        If next_size is not None, the next_size and next_alignment arguments
        are the size and alignment of the member that is being added to the
        struct. The return value in this case is the offset of the new member.

        If next_size is None, all members have been added and this function
        should determine the amount of padding at the end of the structure.
        The return value in this case is the final size of the structure.

        The return value in any case should be an integer that is not less
        than current_offset.
        """
        if next_size is not None:
            alignment = next_alignment
        else:
            alignment = current_alignment
        assert (self.is_power2(alignment))

        align_mask = alignment - 1
        return (current_offset + align_mask) & ~align_mask

    @abc.abstractmethod
    def get_enum_properties(self, e_min, e_max):
        """
        Returns the size, alignment and signedness of the enum.

        Calculates the underlying type properties that the ABI will use for
        an enum with the given enumerator value range.
        """
        raise NotImplementedError


class AArch64ABI(ABI):
    abi_type_map = {
        'uregister_t': 'uint64_t',
        'sregister_t': 'int64_t',
    }

    @property
    def pointer_size(self):
        """The size of a pointer, in bytes."""
        return 8

    @property
    def register_size(self):
        """The size of a register, in bytes."""
        return 8

    @property
    def signed_char(self):
        """True if the char type is signed."""
        return True

    def get_c_type_name(self, abi_type_name):
        """Return the c name for the abi type name."""
        assert (abi_type_name in self.abi_type_map)
        return self.abi_type_map[abi_type_name]

    def get_enum_properties(self, e_min, e_max):
        """
        Returns the size, alignment and signedness of the enum.

        Calculates the underlying type properties that the ABI will use for
        an enum with the given enumerator value range.
        """

        min_bits = e_min.bit_length()
        max_bits = e_max.bit_length()

        signed = e_min < 0

        if not signed and max_bits <= 32:
            return (4, 4, False)
        elif signed and max_bits <= 31 and min_bits <= 32:
            return (4, 4, True)
        elif not signed and max_bits <= 64:
            return (8, 8, False)
        elif signed and max_bits <= 63 and min_bits <= 64:
            return (8, 8, True)
        else:
            raise NotImplementedError
