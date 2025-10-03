#!/usr/bin/env python3
# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import inspect
import sys
import os
import pickle
from lark import Lark, Transformer, v_args
from Cheetah.Template import Template

if __name__ == "__main__" and __package__ is None:
    sys.path.append(os.path.dirname(os.path.dirname(__file__)))
    from utils import genfile, c_utils
    import codegen
    from graph import Tree, Node, Property, PropertyType


def main(args):
    parser = argparse.ArgumentParser(prog="autodt")
    parser.add_argument("-f", "--formatter", help="specify clang-format")
    parser.add_argument(
        "-d",
        "--deps",
        type=genfile.GenFileType("w"),
        help="Write implicit dependencies to Makefile",
        default=None,
    )
    parser.add_argument(
        "--out",
        type=genfile.GenFileType("w"),
    )
    parser.add_argument(
        "--out-pickle",
        type=genfile.GenFileType("wb"),
    )
    parser.add_argument(
        "--grammar",
        help="grammar",
    )
    parser.add_argument(
        "--in-pickle",
        type=argparse.FileType("rb"),
    )
    parser.add_argument(
        "files", type=argparse.FileType("r"), nargs="+", help="input files"
    )
    args = parser.parse_args()

    if args.out_pickle:
        lark_parser = Lark.open(
            args.grammar,
            parser="lalr",
            start="start",
            propagate_positions=True,
        )
        transformer = AutoDtTransformer()

        ast = Ast()
        for f in args.files:
            transformer.filename = f.name
            ast.files[f.name] = f.readlines()
            f.seek(0)

            tree = transformer.transform(lark_parser.parse(f.read()))
            ast.merge(tree)

        tree = ast.walk()

        tree.collect_strings()
        tree.collect_generic_props()

        pickle.dump(tree, args.out_pickle, protocol=-1)
    else:
        tree = pickle.load(args.in_pickle)

        template = args.files[0]

        template_ns = {
            "ast": tree,
            "c_utils": c_utils,
            "cg": codegen,
        }

        output = str(Template(file=template, searchList=[template_ns]))

        if args.out.name[-2:] in (".c", ".h"):
            output = c_utils.format_c_code(output, args.formatter)

        args.out.write(output)

    if args.deps is not None:
        deps = set()

        if args.grammar:
            deps.add(os.path.relpath(args.grammar))

        for m in sys.modules.values():
            try:
                f = inspect.getsourcefile(m)
            except TypeError:
                continue
            if f is None:
                continue
            f = os.path.relpath(f)
            if f.startswith("../"):
                continue
            deps.add(f)

        if args.out_pickle:
            out_name = args.out_pickle.name
        else:
            out_name = args.out.name

        args.deps.write(out_name + " : ")
        args.deps.write(" ".join(sorted(deps)))
        args.deps.write("\n")
        args.deps.close()


class Ast:
    def __init__(self):
        self.handlers = {}
        self.errors = []
        self.children_group_defs = {}
        self.generic_prop_stmts = []

        # Used for error messages only.
        self.files = {}
        self.children_group_defs_handler = {}

    def merge(self, other):
        for name, handler in other.handlers.items():
            if name in self.handlers:
                self.handlers[name].merge(handler)
            else:
                self.handlers[name] = handler

        self.generic_prop_stmts.extend(other.generic_prop_stmts)

    def report_error(self, message, locs=None):
        self.errors.append((message, locs))

    def check_groups(self, tree):
        referenced_groups = set()
        for node in tree.nodes.values():
            referenced_groups.update(node.children_groups)

        for group_name in self.children_group_defs.keys():
            if group_name not in referenced_groups:
                handler = self.children_group_defs_handler[group_name]
                locs = handler.grouped_node_stmts_locs[group_name]

                self.report_error(
                    "Group '{}' is defined but never referenced".format(
                        group_name
                    ),
                    locs,
                )

        for group_name, group_children in self.children_group_defs.items():
            if len(group_children) == 0:
                handler = self.children_group_defs_handler[group_name]
                locs = handler.grouped_node_stmts_locs[group_name]

                self.report_error(
                    "Group '{}' is defined but has no child nodes".format(
                        group_name
                    ),
                    locs,
                )

    def walk(self):
        tree = Tree()

        for handler in self.handlers.values():
            handler.walk(self, tree)

        for handler in self.handlers.values():
            handler.walk_props(self, tree)

        for handler in self.handlers.values():
            handler.walk_nodes(self, tree)

        for handler in self.handlers.values():
            handler.walk_groups(self, tree)

        for handler in self.handlers.values():
            handler.check_child_nodes(self, tree)

        self.check_groups(tree)

        if self.errors:
            print("Errors found during AST walk:")
            for error in self.errors:
                self.print_error(error)
            raise Exception(
                "AST walk failed with {} error(s)".format(len(self.errors))
            )

        return tree

    def print_error(self, error):
        msg, locs = error

        print(msg)

        if locs is None:
            pass
        elif isinstance(locs, list):
            for loc in locs:
                self.print_error_loc(loc)
        else:
            self.print_error_loc(locs)

        print()

    def print_error_loc(self, loc):
        filename, line, column = loc

        prefix1 = "   {} | ".format(line)
        context_line1 = self.files[filename][line - 1].rstrip()
        prefix2 = " " * (len(prefix1) - 2) + "|"
        context_line2 = " " * column + "^"

        print("{}:{}:{}".format(filename, line, column))
        print(prefix1 + context_line1)
        print(prefix2 + context_line2)


class AttributesStmtList:
    def __init__(self, stmts):
        self.stmts = stmts


class PropertiesStmtList:
    def __init__(self, stmts, is_generic=False):
        self.stmts = stmts

        if is_generic:
            for stmt in stmts:
                stmt.attrs.append("generic")


class NodesStmtList:
    def __init__(self, stmts):
        self.stmts = stmts


class GroupedNodesStmtList:
    def __init__(self, group_name, stmts, loc):
        self.group_name = group_name
        self.stmts = stmts
        self.loc = loc


class HandlerStmt:
    def __init__(self, name):
        self.name = name
        self.attr_stmts = []
        self.prop_stmts = []
        self.node_stmts = []
        self.grouped_node_stmts = {}

        # Used for error messages only.
        self.grouped_node_stmts_locs = {}

    def merge(self, other):
        self.attr_stmts.extend(other.attr_stmts)
        self.prop_stmts.extend(other.prop_stmts)
        self.node_stmts.extend(other.node_stmts)
        for group_name, stmts in other.grouped_node_stmts.items():
            if group_name in self.grouped_node_stmts:
                self.grouped_node_stmts[group_name].extend(stmts)
            else:
                self.grouped_node_stmts[group_name] = stmts

        for group_name, locs in other.grouped_node_stmts_locs.items():
            if group_name in self.grouped_node_stmts_locs:
                self.grouped_node_stmts_locs[group_name].extend(locs)
            else:
                self.grouped_node_stmts_locs[group_name] = locs

    def walk(self, ast, tree):
        self.node_ids = {}

        node = Node(self.name)
        tree.nodes[self.name] = node

        self._walk_attr_stmts(ast, tree, node)

    def walk_props(self, ast, tree):
        node = tree.nodes[self.name]

        if node.children_groups:
            self.prop_stmts.extend(ast.generic_prop_stmts)

        for prop_stmt in self.prop_stmts:
            self._walk_prop_stmt(prop_stmt, ast, tree, node)

    def walk_nodes(self, ast, tree):
        node = tree.nodes[self.name]

        if self.node_stmts:
            children_dict = self._walk_child_node_stmts(
                self.node_stmts, ast, tree, node, group=None
            )
            node.children = children_dict

        for group_name, group_stmts in self.grouped_node_stmts.items():
            group_children_dict = self._walk_child_node_stmts(
                group_stmts, ast, tree, node, group=group_name
            )

            if group_name in ast.children_group_defs:
                locs = self.grouped_node_stmts_locs[group_name]

                ast.report_error(
                    "Handler '{}' specifies duplicate group '{}'".format(
                        self.name, group_name
                    ),
                    locs,
                )
            else:
                for k, v in group_children_dict.items():
                    node.grouped_children[k] = v

                ast.children_group_defs[group_name] = group_children_dict
                ast.children_group_defs_handler[group_name] = self
                tree.node_groups[group_name] = node

    def walk_groups(self, ast, tree):
        node = tree.nodes[self.name]

        for group in node.children_groups:
            if group not in ast.children_group_defs:
                locs = [
                    stmt.loc
                    for stmt in self.attr_stmts
                    if isinstance(stmt, GroupStmt) and stmt.group_name == group
                ]

                ast.report_error(
                    "Handler '{}' references non-existent group '{}' ".format(
                        self.name, group
                    ),
                    locs,
                )
            else:
                node.children.update(ast.children_group_defs[group])

    def _walk_attr_stmts(self, ast, tree, node):
        max_count_stmts = [
            stmt for stmt in self.attr_stmts if isinstance(stmt, MaxCountStmt)
        ]

        if len(max_count_stmts) > 1:
            locs = [stmt.loc for stmt in max_count_stmts]

            ast.report_error(
                "Handler '{}' has more than one 'max' attribute".format(
                    self.name
                ),
                locs,
            )

        if len(max_count_stmts) >= 1:
            node.max_count_str = max_count_stmts[0].value
        else:
            node.max_count_str = "1"

        temp_stmts = [
            stmt for stmt in self.attr_stmts if isinstance(stmt, TempStmt)
        ]

        if len(temp_stmts) >= 1:
            node.is_temporary = True

        load_node_name_stmts = [
            stmt
            for stmt in self.attr_stmts
            if isinstance(stmt, LoadNodeNameStmt)
        ]

        if len(load_node_name_stmts) >= 1:
            node.load_node_name = True

        group_stmts = [
            stmt for stmt in self.attr_stmts if isinstance(stmt, GroupStmt)
        ]

        for group_stmt in group_stmts:
            node.children_groups.add(group_stmt.group_name)

    def _walk_prop_stmt(self, stmt, ast, tree, node):
        prop = Property(stmt.name)
        prop.kind = stmt.kind

        for attr in stmt.attrs:
            if attr == "required":
                prop.is_required = True
            elif attr == "optional":
                prop.is_optional = True
            elif attr == "generic":
                prop.is_generic = True
                node.has_generic_props = True

        if prop.is_required and prop.is_optional:
            ast.report_error(
                "Handler '{}', property '{}' "
                "is both required and optional".format(self.name, stmt.name),
                stmt.loc,
            )

        if prop.is_optional and stmt.kind not in [
            PropertyType.U32,
            PropertyType.U64,
            PropertyType.PHANDLE,
        ]:
            prop.is_optional = False

        if stmt.name in node.props:
            locs = [s.loc for s in self.prop_stmts if s.name == stmt.name]

            ast.report_error(
                "Handler '{}' has duplicate property '{}'".format(
                    self.name, stmt.name
                ),
                locs,
            )
        else:
            node.props[stmt.name] = prop

    def _process_node_id_stmt(self, stmt, ast, child_node, child_name):
        node_id_stmts = [s for s in stmt.stmts if isinstance(s, NodeIdStmt)]

        if len(node_id_stmts) > 1:
            locs = [s.loc for s in node_id_stmts]

            ast.report_error(
                "Handler '{}', child node '{}' "
                "has more than one 'id' statement".format(
                    self.name, child_name
                ),
                locs,
            )

        if len(node_id_stmts) >= 1:
            varname = node_id_stmts[0].varname
            if varname in self.node_ids:
                locs = [s.loc for s in node_id_stmts]
                locs.append(self.node_ids[varname].loc)

                ast.report_error(
                    "Handler '{}', child node '{}' "
                    "specifies duplicate id '{}'".format(
                        self.name, child_name, varname
                    ),
                    locs,
                )
            else:
                self.node_ids[varname] = node_id_stmts[0]
            child_node.varname = varname

        if not child_node.is_temporary and child_node.varname is None:
            ast.report_error(
                "Handler '{}', child node '{}' has no 'id' statement".format(
                    self.name, child_name
                ),
                stmt.loc,
            )

    def _process_match_stmt(self, stmt, ast, child_name):
        match_stmts = [
            s
            for s in stmt.stmts
            if isinstance(
                s,
                (
                    NodeMatchAllStmt,
                    NodeMatchDefaultStmt,
                    NodeMatchNameStmt,
                    NodeMatchCompatibleStmt,
                ),
            )
        ]

        if len(match_stmts) == 0:
            ast.report_error(
                "Handler '{}', child node '{}' "
                "has no 'match' statement".format(self.name, child_name),
                stmt.loc,
            )
            match_stmt = NodeMatchAllStmt(None)
        elif len(match_stmts) > 1:
            locs = [s.loc for s in match_stmts]

            ast.report_error(
                "Handler '{}', child node '{}' "
                "has more than one 'match' statement".format(
                    self.name, child_name
                ),
                locs,
            )
            match_stmt = match_stmts[0]
        else:
            match_stmt = match_stmts[0]

        return match_stmt

    def _walk_child_node_stmts(self, stmts, ast, tree, node, group):
        children_dict = {}

        for stmt in stmts:
            child_name = stmt.name

            if child_name not in tree.nodes:
                ast.report_error(
                    "Handler '{}', child node '{}' is undefined".format(
                        self.name, child_name
                    ),
                    stmt.loc,
                )
                continue

            child_node = tree.nodes[child_name]

            if child_node.parent is not None:
                ast.report_error(
                    "Handler '{}', child node '{}' "
                    "already has parent '{}'".format(
                        self.name, child_name, child_node.parent.name
                    ),
                    stmt.loc,
                )
                continue

            child_node.parent = node

            if group:
                child_node.parent_is_generic = True
                child_node.group = group

            self._process_node_id_stmt(stmt, ast, child_node, child_name)

            match_stmt = self._process_match_stmt(stmt, ast, child_name)

            if isinstance(match_stmt, NodeMatchNameStmt):
                child_node.match_name_str = match_stmt.match_str
                child_node.match_name_loc = match_stmt.loc
            elif isinstance(match_stmt, NodeMatchCompatibleStmt):
                child_node.match_compatible_str = match_stmt.match_str
                child_node.match_compatible_loc = match_stmt.loc
            elif isinstance(match_stmt, NodeMatchAllStmt):
                child_node.matched_by_all = True
                child_node.match_all_loc = match_stmt.loc
            elif isinstance(match_stmt, NodeMatchDefaultStmt):
                child_node.matched_by_default = True
                child_node.match_default_loc = match_stmt.loc

            children_dict[child_name] = child_node

        return children_dict

    def check_child_nodes(self, ast, tree):
        node = tree.nodes[self.name]

        names = {}
        compatibles = {}
        match_by_all_children = []
        match_by_default_children = []

        for child_name, child_node in node.children.items():
            match_name = child_node.match_name_str

            if match_name:
                loc = child_node.match_name_loc

                if match_name in names:
                    names[match_name].append(loc)
                else:
                    names[match_name] = [loc]

            compatible = child_node.match_compatible_str

            if compatible:
                loc = child_node.match_compatible_loc

                if compatible in compatibles:
                    compatibles[compatible].append(loc)
                else:
                    compatibles[compatible] = [loc]

            if child_node.matched_by_all:
                match_by_all_children.append(child_node.match_all_loc)

            if child_node.matched_by_default:
                match_by_default_children.append(
                    (child_name, child_node.match_default_loc)
                )

        for match_name, locs in names.items():
            if len(locs) > 1:
                ast.report_error(
                    "Handler '{}' "
                    "has children with duplicate match name parameters".format(
                        self.name
                    ),
                    locs,
                )

        for compatible, locs in compatibles.items():
            if len(locs) > 1:
                ast.report_error(
                    "Handler '{}' has children with duplicate match "
                    "compatible parameters".format(self.name),
                    locs,
                )

        if match_by_all_children:
            if len(node.children) > 1:
                locs = match_by_all_children + [s.loc for s in self.node_stmts]

                ast.report_error(
                    "Handler '{}' has child matched by all "
                    "but has more than one child".format(self.name),
                    locs,
                )
            node.match_by_all = True

        if match_by_default_children:
            if len(match_by_default_children) > 1:
                locs = [x[1] for x in match_by_default_children]

                ast.report_error(
                    "Handler '{}' "
                    "has multiple child nodes matched by default".format(
                        self.name
                    ),
                    locs,
                )
            else:
                child_name = match_by_default_children[0][0]
                node.match_default_node = node.children[child_name]


class GroupStmt:
    def __init__(self, group_name, loc):
        self.group_name = group_name
        self.loc = loc


class MaxCountStmt:
    def __init__(self, value, loc):
        self.value = value
        self.loc = loc


class LoadNodeNameStmt:
    pass


class TempStmt:
    pass


class PropertyStmt:
    def __init__(self, name, kind, attrs, loc):
        self.name = name
        self.kind = PropertyType[kind.upper()]
        self.attrs = attrs
        self.loc = loc


class NodeStmt:
    def __init__(self, name, stmts, loc):
        self.name = name
        self.stmts = stmts
        self.loc = loc


class NodeIdStmt:
    def __init__(self, varname, loc):
        self.varname = varname
        self.loc = loc


class NodeMatchAllStmt:
    def __init__(self, loc):
        self.loc = loc


class NodeMatchNameStmt:
    def __init__(self, match_str, loc):
        self.match_str = match_str
        self.loc = loc


class NodeMatchCompatibleStmt:
    def __init__(self, match_str, loc):
        self.match_str = match_str
        self.loc = loc


class NodeMatchDefaultStmt:
    def __init__(self, loc):
        self.loc = loc


class AutoDtTransformer(Transformer):
    def start(self, children):
        ast = Ast()

        for child in children:
            if isinstance(child, PropertiesStmtList):
                ast.generic_prop_stmts.extend(child.stmts)
            elif isinstance(child, HandlerStmt):
                if child.name in ast.handlers:
                    ast.handlers[child.name].merge(child)
                else:
                    ast.handlers[child.name] = child

        return ast

    def generic_properties_stmt(self, children):
        return PropertiesStmtList(children, True)

    def handler(self, children):
        handler = HandlerStmt(children[0])
        for child in children[1:]:
            if isinstance(child, AttributesStmtList):
                handler.attr_stmts.extend(child.stmts)
            elif isinstance(child, PropertiesStmtList):
                handler.prop_stmts.extend(child.stmts)
            elif isinstance(child, NodesStmtList):
                handler.node_stmts.extend(child.stmts)
            elif isinstance(child, GroupedNodesStmtList):
                group_name = child.group_name
                if group_name in handler.grouped_node_stmts:
                    handler.grouped_node_stmts[group_name].extend(child.stmts)
                    handler.grouped_node_stmts_locs[group_name].append(
                        child.loc
                    )
                else:
                    handler.grouped_node_stmts[group_name] = child.stmts
                    handler.grouped_node_stmts_locs[group_name] = [child.loc]

        return handler

    def HANDLER_NAME(self, children):
        return str(children)

    def attributes_stmt(self, children):
        return AttributesStmtList(children)

    @v_args(meta=True)
    def group_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return GroupStmt(children[0], loc)

    def GROUP_NAME(self, children):
        return str(children)

    @v_args(meta=True)
    def max_count_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return MaxCountStmt(children[0], loc)

    def MAX_COUNT(self, children):
        return str(children)

    def load_node_name_stmt(self, _):
        return LoadNodeNameStmt()

    def temp_stmt(self, _):
        return TempStmt()

    def properties_stmt(self, children):
        return PropertiesStmtList(children)

    @v_args(meta=True)
    def property_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return PropertyStmt(children[-2], children[-1], children[:-2], loc)

    def PROPERTY_NAME(self, children):
        return str(children)

    def PROPERTY_TYPE(self, children):
        return str(children)

    def PROPERTY_ATTR(self, children):
        return str(children)

    def nodes_stmt(self, children):
        return NodesStmtList(children)

    @v_args(meta=True)
    def grouped_nodes_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return GroupedNodesStmtList(children[0], children[1:], loc)

    @v_args(meta=True)
    def node_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return NodeStmt(children[0], children[1:], loc)

    def NODE_NAME(self, children):
        return str(children)

    @v_args(meta=True)
    def node_id_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return NodeIdStmt(children[0], loc)

    def NODE_ID(self, children):
        return str(children)

    @v_args(meta=True)
    def node_match_all_stmt(self, _, meta):
        loc = self.make_loc(meta)
        return NodeMatchAllStmt(loc)

    @v_args(meta=True)
    def node_match_name_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return NodeMatchNameStmt(children[0][1:-1], loc)

    @v_args(meta=True)
    def node_match_compatible_stmt(self, children, meta):
        loc = self.make_loc(meta)
        return NodeMatchCompatibleStmt(children[0][1:-1], loc)

    @v_args(meta=True)
    def node_match_default_stmt(self, _, meta):
        loc = self.make_loc(meta)
        return NodeMatchDefaultStmt(loc)

    def NODE_MATCH_STR(self, children):
        return str(children)

    def make_loc(self, meta):
        return (self.filename, meta.line, meta.column)


if __name__ == "__main__":
    main(sys.argv)
