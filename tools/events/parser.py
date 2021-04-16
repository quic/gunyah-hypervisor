# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

from lark import Transformer, v_args
from ir import (
    Include, Symbol, Type, ConstExpr, Priority, Result, ExpectedArgs, Param,
    Selectors, SelectorParam, CountParam, Module, Event, HandledEvent,
    MultiEvent, SetupEvent, SelectorEvent, Subscription, Optional, Public,
    Handler, Constant, Unwinder, Success)

import collections
import logging
import math


logger = logging.getLogger(__name__)


class TransformToIR(Transformer):
    def __init__(self, module_dict, event_dict):
        super().__init__()
        self.module_dict = module_dict
        self.event_dict = event_dict
        self.cur_event_dict = {}
        self.errors = 0

    def _add_without_duplicates(self, type_string, in_dict, new):
        if new.name in in_dict:
            existing = in_dict[new.name]
            new_meta = new.name.meta
            old_meta = existing.name.meta
            logger.error("%s:%d:%d: error: duplicate definition of %s '%s'",
                         new_meta.filename, new_meta.line, new_meta.column,
                         type_string, new.name)
            logger.info("%s:%d:%d: note: previous definition of %s '%s'",
                        old_meta.filename, old_meta.line,
                        old_meta.column, type_string, new.name)
            self.errors += 1
        else:
            in_dict[new.name] = new

    def _general_event(self, event_class, children, meta):
        # Check for duplicated parameters
        params = {}
        for c in children:
            if isinstance(c, Param):
                self._add_without_duplicates('parameter', params, c)

        event = event_class(children)
        event.meta = meta
        self._add_without_duplicates('event', self.cur_event_dict, event)
        return event

    @v_args(meta=True)
    def module(self, children, meta):
        assert not self.cur_event_dict
        m = Module(children)
        m.meta = meta
        if m.name in self.module_dict:
            self.module_dict[m.name].merge(m)
        else:
            self.module_dict[m.name] = m
        return m

    @v_args(meta=True)
    def interface(self, children, meta):
        interface_name = next(c for c in children if isinstance(c, Symbol))
        for name, event in self.cur_event_dict.items():
            if not name.startswith(interface_name + "_"):
                meta = event.name.meta
                logger.error("%s:%d:%d: error: incorrect name: "
                             "'%s' should start with '%s_'",
                             meta.filename, meta.line, meta.column, name,
                             interface_name)
                self.errors += 1
            self.event_dict[name] = event
        self.cur_event_dict = {}
        return self.module(children, meta)

    def include(self, children):
        return Include(''.join(str(c) for c in children))

    @v_args(meta=True)
    def publish_event(self, children, meta):
        return self._general_event(Event, children, meta)

    @v_args(meta=True)
    def publish_handled_event(self, children, meta):
        return self._general_event(HandledEvent, children, meta)

    @v_args(meta=True)
    def publish_multi_event(self, children, meta):
        return self._general_event(MultiEvent, children, meta)

    @v_args(meta=True)
    def publish_setup_event(self, children, meta):
        return self._general_event(SetupEvent, children, meta)

    @v_args(meta=True)
    def publish_selector_event(self, children, meta):
        return self._general_event(SelectorEvent, children, meta)

    @v_args(meta=True)
    def symbol(self, children, meta):
        sym = Symbol(*children)
        sym.meta = meta
        return sym

    @v_args(meta=True)
    def event_param(self, children, meta):
        p = Param(children)
        p.meta = meta
        return p

    @v_args(meta=True)
    def selector_param(self, children, meta):
        p = SelectorParam(children)
        p.meta = meta
        return p

    @v_args(meta=True)
    def count_param(self, children, meta):
        p = CountParam(children)
        p.meta = meta
        return p

    @v_args(meta=True)
    def result(self, children, meta):
        r = Result(children)
        r.meta = meta
        return r

    @v_args(meta=True)
    def type_decl(self, children, meta):
        t = Type(' '.join(str(c) for c in children))
        t.meta = meta
        return t

    @v_args(meta=True)
    def subscribe(self, children, meta):
        s = Subscription(children)
        s.meta = meta
        return s

    @v_args(meta=True)
    def selector(self, children, meta):
        s = Selectors(children)
        s.meta = meta
        return s

    subscribe_public = subscribe

    def optional(self, children):
        return Optional()

    def public(self, children):
        return Public()

    @v_args(meta=True)
    def handler(self, children, meta):
        h = Handler(*children)
        h.meta = meta
        return h

    handler_public = handler

    @v_args(meta=True)
    def unwinder(self, children, meta):
        u = Unwinder(*children)
        u.meta = meta
        return u

    unwinder_public = unwinder

    constant = v_args(inline=True)(Constant)

    def expected_args(self, children):
        args = collections.OrderedDict()
        for c in children:
            c.name = c
            self._add_without_duplicates('argument', args, c)
        return ExpectedArgs(args.values())

    @v_args(meta=True)
    def priority(self, children, meta):
        if children[0] in ('first', 'max'):
            c = Priority(math.inf)
        elif children[0] in ('last', 'min'):
            c = Priority(-math.inf)
        elif children[0] == 'default':
            c = Priority(0)
        else:
            c = Priority(children[0])
        c.meta = meta
        return c

    @v_args(meta=True)
    def constexpr(self, children, meta):
        c = ConstExpr(' '.join(children))
        c.meta = meta
        return c

    @v_args(meta=True)
    def success(self, children, meta):
        c = Success(' '.join(children))
        c.meta = meta
        return c
