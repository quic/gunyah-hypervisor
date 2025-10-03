#!/usr/bin/env python3
# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

from enum import Enum


class Tree:
    """
    Abstract representation of a device tree.
    """

    def __init__(self):
        # The nodes in the device tree.
        self.nodes = {}

        # The node groups in the device tree (group name -> node)
        self.node_groups = {}

    def collect_strings(self):
        self.strings = set()

        for v in self.nodes.values():
            v.collect_strings(self.strings)

    def collect_generic_props(self):
        self.generic_props = {}

        for v in self.nodes.values():
            v.collect_generic_props(self.generic_props)

    # Functions must below this point must be called only after the tree has
    # been finalised.

    def get_top_level_nodes(self):
        return {k: v for (k, v) in self.nodes.items() if v.is_top_level()}


class Node:
    """
    An abstract device tree node.
    """

    def __init__(self, name):
        # The name of the node.
        self.name = name

        # The parent node. All nodes which aren't top level nodes must have a
        # parent.
        self.parent = None

        # Whether the node has a generic parent.
        #
        # A node will have a generic parent if it may be a child of different
        # nodes.
        self.parent_is_generic = False

        # The properties of the node.
        #
        # The keys are the property names.
        self.props = {}

        # The children of the node.
        #
        # The keys are the node names of the children.
        self.children = {}

        # Whether the node is temporary.
        #
        # Temporary nodes are stored on the stack instead of in parent context.
        self.is_temporary = False

        # Whether to save the node name.
        self.load_node_name = False

        # The varname of the node.
        #
        # The varname is the name of the fields used for this node in the
        # parent struct.
        self.varname = None

        # The max count string for this node.
        self.max_count_str = None

        # The children groups for this node.
        #
        # FIXME: This can be removed probably.
        self.children_groups = set()

        # The grouped children of the node.
        #
        # The grouped children are added to the children if the group name
        # is in children_groups.
        self.grouped_children = {}

        # The group this node belongs to, if any.
        self.group = None

        # Whether this node matches all children.
        self.match_by_all = False

        # The default child node handler, if one exists.
        self.match_default_node = None

        # The match name string for this node.
        self.match_name_str = None

        # The match compatible string for this node.
        self.match_compatible_str = None

        # Whether this node is matched by default.
        self.matched_by_default = False

        # Whether this node is matched by all.
        self.matched_by_all = False

        # Whether the node has any generic properties
        self.has_generic_props = False

    def collect_strings(self, strings):
        strings.update(self.props.keys())

        if self.match_name_str:
            strings.add(self.match_name_str)

        if self.match_compatible_str:
            strings.add(self.match_compatible_str)

    def collect_generic_props(self, generic_props):
        for k, v in self.props.items():
            if v.is_generic:
                generic_props[k] = v

    # Functions below this point must be called only after the node has been
    # finalised.

    def is_top_level(self):
        return self.parent is None


class PropertyType(Enum):
    """
    Property type enumeration.
    """

    VALUE = 0
    STRING = 1
    STRINGLIST = 2
    PHANDLE = 3
    U32 = 4
    U64 = 5
    BOOL = 6

    def __str__(self):
        return self.name.lower()


class Property:
    """
    An abstract device tree property.
    """

    def __init__(self, name):
        # The name of the property.
        self.name = name

        # The property type (PropertyType enum value).
        self.kind = None

        # Whether the property is required.
        #
        # Required properties have their existence checked.
        self.is_required = False

        # Whether the property is optional.
        #
        # Optional properties record both the value and whether the property
        # exists.
        self.is_optional = False

        # Whether the property is generic.
        #
        # Generic properties are saved to the generic context.
        self.is_generic = False
