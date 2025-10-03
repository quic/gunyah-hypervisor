#!/usr/bin/env python3
# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

# Helper functions for code generation.

from utils import c_utils
from graph import PropertyType


def parent_type(node):
    return "dt_auto_generic" if node.parent_is_generic else node.parent.name


def sig(node):
    if node.is_top_level():
        s = "void {}_parse(fdt_parser_t *parser)".format(node.name)
    else:
        s = "void {}_parse(fdt_parser_t *parser, {}_t *parent)".format(
            node.name,
            parent_type(node),
        )

    return s


def sig_user(node):
    if node.is_top_level():
        s = "void {}_user_callback(fdt_parser_t *parser, {}_t *curr)".format(
            node.name, node.name
        )
    else:
        s = (
            "void {}_user_callback"
            "(fdt_parser_t *parser, {}_t *parent, {}_t *curr)"
        ).format(
            node.name,
            parent_type(node),
            node.name,
        )

    return s


def hash_key(s, p="dt"):
    if not isinstance(s, str):
        s = s.name

    k = "PERFECT_HASH_{}_{}".format(
        p.upper(), c_utils.generate_unique_c_ident(s)
    )

    return k


def hash_key_invalid(p="dt"):
    k = "PERFECT_HASH_{}__zINVALID".format(p.upper())

    return k


def hash_key_type(p="dt"):
    k = "perfect_hash_{}_t".format(p)

    return k


def hash_key_fn(p="dt"):
    k = "trigger_perfect_hash_{}_sym_event".format(p)

    return k


def prop_type_to_tc(k):
    d = {
        PropertyType.VALUE: "pointer char(const)",
        PropertyType.STRING: "pointer char(const)",
        PropertyType.STRINGLIST: "pointer pointer char(const)",
        PropertyType.PHANDLE: "uint32",
        PropertyType.U32: "uint32",
        PropertyType.U64: "uint64",
        PropertyType.BOOL: "bool",
    }

    return d[k]


def prop_type_has_len(k):
    return k in (PropertyType.VALUE, PropertyType.STRINGLIST)


def prop_type_is_stringlist(k):
    return k == PropertyType.STRINGLIST


def prop_load_suffix(v):
    if v.is_required:
        return v.kind.name.lower()
    elif v.is_optional:
        return v.kind.name.lower() + "_optional"
    else:
        return v.kind.name.lower()
