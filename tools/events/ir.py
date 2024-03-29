# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

import abc
import logging
import collections


logger = logging.getLogger(__name__)


def _first_of_type(children, cls):
    return next(c for c in children if isinstance(c, cls))


def _first_of_type_opt(children, cls):
    try:
        return _first_of_type(children, cls)
    except StopIteration:
        return None


def _all_of_type(children, cls):
    return tuple(c for c in children if isinstance(c, cls))


class IRObject(object):
    def __getstate__(self):
        state = self.__dict__.copy()
        if 'meta' in state:
            del state['meta']
        return state


class DSLError(Exception):
    pass


class Include(str, IRObject):
    pass


class Symbol(str, IRObject):
    pass


class Type(str, IRObject):
    pass


class ConstExpr(str, IRObject):
    pass


class LockName(str, IRObject):
    pass


class LockAnnotation(IRObject):
    def __init__(self, action, kind, lock):
        self.action = action
        self.kind = kind
        self.lock = lock

    def _check_kind(self, kinds):
        if self.lock in kinds:
            if kinds[self.lock] != self.kind:
                logger.error("%s:%d:%d: "
                             "error: inconsistent lock kinds for %s (%s, %s)",
                             self.meta.filename, self.meta.line,
                             self.meta.column, self.lock, self.kind,
                             kinds[self.lock])
                raise DSLError()
        else:
            kinds[self.lock] = self.kind

    def apply(self, acquires, releases, requires, excludes, kinds):
        self._check_kind(kinds)

        if self.action == 'acquire':
            if self.lock in acquires:
                prev = next(acquires & set([self.lock]))
                logger.error("%s:%d:%d: "
                             "error: %s previously acquired at %s:%d:%d",
                             self.meta.filename, self.meta.line,
                             self.meta.column, self.lock, prev.meta.filename,
                             prev.meta.line, prev.meta.column)
                raise DSLError()
            elif self.lock in releases:
                releases.remove(self.lock)
            else:
                acquires.add(self.lock)
                excludes.add(self.lock)
        elif self.action == 'release':
            if self.lock in releases:
                prev = next(releases & set([self.lock]))
                logger.error("%s:%d:%d: "
                             "error: %s previously released at %s:%d:%d",
                             self.meta.filename, self.meta.line,
                             self.meta.column, self.lock, prev.meta.filename,
                             prev.meta.line, prev.meta.column)
                raise DSLError()
            elif self.lock in acquires:
                acquires.remove(self.lock)
            else:
                releases.add(self.lock)
                requires.add(self.lock)
        elif self.action == 'require':
            if self.lock not in acquires:
                requires.add(self.lock)
        elif self.action == 'exclude':
            if self.lock not in releases:
                excludes.add(self.lock)
        else:
            raise NotImplementedError(self.action)

    def combine(self, actions, kinds):
        self._check_kind(kinds)
        actions[self.action].add(self.lock)

    def unwind(self):
        if self.action == 'acquire':
            ret = LockAnnotation('release', self.kind, self.lock)
            ret.meta = self.meta
            return ret
        elif self.action == 'release':
            ret = LockAnnotation('acquire', self.kind, self.lock)
            ret.meta = self.meta
            return ret
        else:
            return self


class Priority(float, IRObject):
    pass


class Result(IRObject):
    def __init__(self, children, void=False):
        try:
            self.type = _first_of_type(children, Type)
            self.default = _first_of_type(children, ConstExpr)
        except StopIteration:
            if void:
                self.type = Type('void')
                self.default = None
            else:
                raise StopIteration


class ExpectedArgs(list, IRObject):
    pass


class Selectors(list, IRObject):
    pass


class Param(IRObject):
    def __init__(self, children):
        self.name = _first_of_type(children, Symbol)
        self.type = _first_of_type(children, Type)


class SelectorParam(Param):
    pass


class CountParam(Param):
    pass


class AbstractEvent(IRObject, metaclass=abc.ABCMeta):
    def __init__(self, children):
        self.name = _first_of_type(children, Symbol)
        self._param_dict = collections.OrderedDict(
            (c.name, c) for c in children if isinstance(c, Param))

    def set_owner(self, module):
        self.module_name = module.name
        self.module_includes = module.includes

    @abc.abstractmethod
    def subscribe(self, subscription):
        pass

    def finalise(self):
        pass

    @abc.abstractproperty
    def subscribers(self):
        raise NotImplementedError

    @abc.abstractproperty
    def lock_opts(self):
        raise NotImplementedError

    @abc.abstractproperty
    def return_type(self):
        raise NotImplementedError

    @abc.abstractproperty
    def noreturn(self):
        raise NotImplementedError

    def param(self, name):
        return self._param_dict[name]

    @property
    def params(self):
        return tuple(self._param_dict.values())

    @property
    def param_names(self):
        return tuple(p.name for p in self.params)

    @property
    def unused_param_names(self):
        params = set(self.param_names)
        for s in self.subscribers:
            for h in s.all_handlers:
                params -= set(h.args)
                if not params:
                    return set()
        return params


class AbstractSortedEvent(AbstractEvent):
    def __init__(self, children):
        super().__init__(children)
        self._subscribers = []
        self._lock_opts = None

    def subscribe(self, subscription):
        super().subscribe(subscription)
        if subscription.priority is None:
            subscription.priority = 0
        if subscription.selectors is not None:
            logger.error("%s:%d:%d: error: selector %s does not apply to "
                         "non-selector event %s",
                         subscription.selectors[0].meta.filename,
                         subscription.selectors[0].meta.line,
                         subscription.selectors[0].meta.column,
                         subscription.selectors[0], self.name)
            raise DSLError()
        if subscription.constant is not None:
            logger.error("%s:%d:%d: error: constant value %s specified for "
                         "non-selector event %s",
                         subscription.constant.meta.filename,
                         subscription.constant.meta.line,
                         subscription.constant.meta.column,
                         subscription.constant, self.name)
            raise DSLError()
        self._subscribers.append(subscription)

    def finalise(self):
        super().finalise()
        subscribers = sorted(self._subscribers,
                             key=lambda x: (-x.priority, x.handler))
        for left, right in zip(subscribers, subscribers[1:]):
            if left.priority != 0 and left.priority == right.priority:
                logger.error("%s:%d:%d: error: handler %s for event %s has "
                             "the same nonzero priority as handler %s\n"
                             "%s:%d:%d: info: handler %s subscribed here",
                             left.priority.meta.filename,
                             left.priority.meta.line,
                             left.priority.meta.column, left.handler,
                             self.name, right.handler,
                             right.priority.meta.filename,
                             right.priority.meta.line,
                             right.priority.meta.column, right.handler)
                raise DSLError()
        self._subscribers = tuple(subscribers)

        acquires = set()
        releases = set()
        requires = set()
        excludes = set()
        kinds = {}
        for s in self._subscribers:
            for lock_opt in s.lock_opts:
                lock_opt.apply(acquires, releases, requires, excludes, kinds)
        lock_opts = []
        for lock in sorted(acquires):
            lock_opts.append(LockAnnotation('acquire', kinds[lock], lock))
        for lock in sorted(releases):
            lock_opts.append(LockAnnotation('release', kinds[lock], lock))
        for lock in sorted(requires):
            lock_opts.append(LockAnnotation('require', kinds[lock], lock))
        for lock in sorted(excludes):
            lock_opts.append(LockAnnotation('exclude', kinds[lock], lock))
        self._lock_opts = tuple(lock_opts)

        noreturn = (self._subscribers and self._subscribers[-1].handler and
                    self._subscribers[-1].handler.noreturn)
        if noreturn and self.return_type != 'void':
            s = self._subscribers[-1]
            n = s.handler.noreturn
            logger.error("%s:%d:%d: error: last handler %s for event %s must "
                         "return, but is declared as noreturn",
                         n.meta.filename, n.meta.line, n.meta.column,
                         s.handler, self.name)
            raise DSLError()
        for s in self._subscribers[:-1]:
            if s.handler is not None and s.handler.noreturn:
                n = s.handler.noreturn
                logger.error("%s:%d:%d: error: handler %s for event %s does "
                             "not return, but is not the last handler (%s)",
                             n.meta.filename, n.meta.line, n.meta.column,
                             s.handler, self.name,
                             self._subscribers[-1].handler)
                raise DSLError()
        self._noreturn = noreturn

    @property
    def noreturn(self):
        return self._noreturn

    @property
    def subscribers(self):
        return self._subscribers

    @property
    def lock_opts(self):
        return self._lock_opts


class Event(AbstractSortedEvent):
    @property
    def return_type(self):
        return 'void'


class HandledEvent(AbstractSortedEvent):
    def __init__(self, children):
        super().__init__(children)
        self.result = _first_of_type_opt(children, Result)

    @property
    def return_type(self):
        return self.result.type if self.result is not None else 'bool'

    @property
    def default(self):
        return self.result.default if self.result is not None else 'false'


class MultiEvent(AbstractSortedEvent):
    def __init__(self, children):
        super().__init__(children)
        self.count = _first_of_type(children, CountParam)

    @property
    def return_type(self):
        return self.count.type

    @property
    def unused_param_names(self):
        return super().unused_param_names - {self.count.name}


class SetupEvent(AbstractSortedEvent):
    def __init__(self, children):
        super().__init__(children)
        self.result = _first_of_type(children, Result)
        self.success = _first_of_type(children, Success)

        self.result.name = Symbol('result')
        if self.result.name in self._param_dict:
            result = self._param_dict[self.result.name]
            logger.error("%s:%d:%d: error: setup event must not have an "
                         "explicit parameter named '%s'",
                         result.meta.filename, result.meta.line,
                         result.meta.column, self.result.name)
            raise DSLError()
        self._result_param = Param([self.result.name, self.result.type])

    def finalise(self):
        super().finalise()

        if self.subscribers and self.subscribers[-1].unwinder is not None:
            u = self.subscribers[-1].unwinder
            logger.warning("%s:%d:%d: warning: unwinder %s() is unused",
                           u.meta.filename, u.meta.line, u.meta.column, u.name)

    @property
    def return_type(self):
        return self.result.type

    def param(self, name):
        if name == self.result.name:
            return self._result_param
        return super().param(name)


class SelectorEvent(AbstractEvent):
    def __init__(self, children):
        super().__init__(children)
        self.selector = _first_of_type(children, SelectorParam)
        try:
            self.result = _first_of_type(children, Result)
        except StopIteration:
            self.result = Result([Type('bool'), ConstExpr('false')])
        self._subscribers = {}

    @property
    def subscribers(self):
        return self._subscribers.values()

    def subscribe(self, subscription):
        if subscription.priority is not None:
            logger.error("%s:%d:%d: error: priority (%d) cannot be specified "
                         "for subscription to a selector event ('%s')",
                         subscription.priority.meta.filename,
                         subscription.priority.meta.line,
                         subscription.priority.meta.column,
                         subscription.priority, self.name)
            raise DSLError()
        if subscription.selectors is None:
            logger.error("%s:%d:%d: error: no selector specified for "
                         "subscription to selector event '%s'",
                         subscription.event_name.meta.filename,
                         subscription.event_name.meta.line,
                         subscription.event_name.meta.column, self.name)
            raise DSLError()
        for s in self._subscribers:
            for new in subscription.selectors:
                if new in self._subscribers[s].selectors:
                    logger.error("%s:%d:%d: error: duplicate selector '%s' "
                                 "specified for subscription to selector "
                                 "event '%s'",
                                 subscription.event_name.meta.filename,
                                 subscription.event_name.meta.line,
                                 subscription.event_name.meta.column, new,
                                 self.name)
                    raise DSLError()
        key = subscription.selectors[0]
        self._subscribers[key] = subscription

    def finalise(self):
        super().finalise()

        kinds = {}
        actions = {
            'acquire': set(),
            'release': set(),
            'require': set(),
            'exclude': set(),
        }
        for s in self._subscribers.values():
            for lock_opt in s.lock_opts:
                lock_opt.combine(actions, kinds)
        lock_opts = []
        for action in actions.keys():
            for lock in actions[action]:
                lock_opts.append(LockAnnotation(action, kinds[lock], lock))
        self._lock_opts = tuple(lock_opts)

    @property
    def lock_opts(self):
        return self._lock_opts

    @property
    def return_type(self):
        return self.result.type

    @property
    def noreturn(self):
        # Note: this could  be true if the selector is a enum type that is
        # covered by noreturn handlers. We're not likely to ever do that.
        return False

    @property
    def unused_param_names(self):
        return super().unused_param_names - {self.selector.name}


class Optional(IRObject):
    pass


class Public(IRObject):
    pass


class NoReturn(IRObject):
    pass


class Subscription(IRObject):
    def __init__(self, children):
        self.event_name = _first_of_type(children, Symbol)
        self.optional = any(c for c in children if isinstance(c, Optional))
        self.selectors = _first_of_type_opt(children, Selectors)
        self.handler = _first_of_type_opt(children, Handler)
        self.constant = _first_of_type_opt(children, Constant)
        if self.handler is None and self.constant is None:
            self.handler = Handler(_first_of_type_opt(children, ExpectedArgs),
                                   _first_of_type_opt(children, NoReturn))
        self.unwinder = _first_of_type_opt(children, Unwinder)
        self.priority = _first_of_type_opt(children, Priority)
        self.lock_opts = _all_of_type(children, LockAnnotation)

    def set_owner(self, module):
        self.module_name = module.name

    def resolve(self, events):
        try:
            self.event = events[self.event_name]
        except KeyError:
            if not self.optional:
                logger.error(
                    "%s:%d:%d: error: subscribed to unknown event '%s'",
                    self.meta.filename, self.meta.line, self.meta.column,
                    self.event_name)
                raise DSLError()
            self.event = NotImplemented
        else:
            self.event.subscribe(self)

        for h in self.all_handlers:
            h.resolve(self)

    @property
    def all_handlers(self):
        if self.event is not NotImplemented:
            if self.handler is not None:
                yield self.handler
            if self.unwinder is not None:
                yield self.unwinder


class AbstractFunction(IRObject, metaclass=abc.ABCMeta):
    def __init__(self, *children):
        self.name = _first_of_type_opt(children, Symbol)
        self.args = _first_of_type_opt(children, ExpectedArgs)
        self.public = any(c for c in children if isinstance(c, Public))
        self._noreturn = _first_of_type_opt(children, NoReturn)

    def resolve(self, subscription):
        self.subscription = subscription
        self.module_name = subscription.module_name
        self.event = subscription.event

        if self.name is None:
            self.name = self._default_name

        if self.args is None:
            self.args = self._available_params
        else:
            for a in self.args:
                if a not in self._available_params:
                    logger.error(
                        "%s:%d:%d: error: event '%s' has no argument '%s'",
                        a.meta.filename, a.meta.line,
                        a.meta.column, self.event.name, a)
                    raise DSLError()

    @abc.abstractproperty
    def _default_name(self):
        yield NotImplementedError

    @property
    def _available_params(self):
        return self.event.param_names

    @property
    def noreturn(self):
        return self._noreturn

    @property
    def return_type(self):
        return self.event.return_type if not self.noreturn else 'void'

    @property
    def params(self):
        for a in self.args:
            yield self.event.param(a)

    @property
    def lock_opts(self):
        for opt in self.subscription.lock_opts:
            yield opt

    def __lt__(self, other):
        return self.name < other.name

    def __str__(self):
        return self.name

    def __hash__(self):
        """Generate a unique hash for the function."""
        return hash((self.name, self.return_type) +
                    tuple((p.name, p.type) for p in self.params))


class Handler(AbstractFunction):
    @property
    def _default_name(self):
        return "{:s}_handle_{:s}".format(self.module_name, self.event.name)


class Unwinder(AbstractFunction):
    @property
    def _default_name(self):
        return "{:s}_unwind_{:s}".format(self.module_name, self.event.name)

    @property
    def _available_params(self):
        return (self.event.result.name,) + super()._available_params

    @property
    def return_type(self):
        return 'void'

    @property
    def lock_opts(self):
        for opt in self.subscription.lock_opts:
            yield opt.unwind()


class Constant(str, IRObject):
    def __init__(self, children):
        self.value = children[0]


class Success(Constant):
    pass


class Module(IRObject):
    def __init__(self, children):
        self.name = _first_of_type(children, Symbol)
        self.includes = _all_of_type(children, Include)
        self.events = _all_of_type(children, AbstractEvent)
        for e in self.events:
            e.set_owner(self)
        self.subscriptions = _all_of_type(children, Subscription)
        for s in self.subscriptions:
            s.set_owner(self)

    def merge(self, other):
        assert self.name == other.name
        self.includes += other.includes
        self.events += other.events
        self.subscriptions += other.subscriptions
        for s in other.subscriptions:
            s.set_owner(self)

    def resolve(self, events):
        errors = 0
        for s in self.subscriptions:
            try:
                s.resolve(events)
            except DSLError:
                errors += 1
        return errors

    def finalise(self):
        errors = 0
        for e in self.events:
            try:
                e.finalise()
            except DSLError:
                errors += 1
        return errors

    @property
    def handlers(self):
        # Unique event handlers defined by this module.
        #
        # Each of these may be used by multiple subscriptions, either to
        # different events, or to the same selector event with different
        # selections, or even repeatedly for one event.
        seen_handlers = dict()
        for s in self.subscriptions:
            for h in s.all_handlers:
                if h.name in seen_handlers:
                    if seen_handlers[h.name] != hash(h):
                        logger.error("handler decl mismatch: %s",
                                     h.name)
                        raise DSLError()
                    continue
                seen_handlers[h.name] = hash(h)
                yield h

    @property
    def declared_handlers(self):
        # Unique event handlers declared by this module's events.
        seen_handlers = dict()
        for e in self.events:
            for s in e.subscribers:
                for h in s.all_handlers:
                    if h.name in seen_handlers:
                        if seen_handlers[h.name] != hash(h):
                            logger.error("handler decl mismatch: %s",
                                         h.name)
                            raise DSLError()
                        continue
                    if h.public:
                        continue
                    seen_handlers[h.name] = hash(h)
                    yield h

    @property
    def handler_includes(self):
        seen_modules = set()
        seen_includes = set()
        for s in self.subscriptions:
            e = s.event
            if e is NotImplemented:
                continue

            m = e.module_name
            if m in seen_modules:
                continue
            seen_modules.add(m)

            for i in e.module_includes:
                if i in seen_includes:
                    continue
                seen_includes.add(i)
                yield i

    @property
    def simple_events(self):
        return (e for e in self.events if isinstance(e, Event))

    @property
    def handled_events(self):
        return (e for e in self.events if isinstance(e, HandledEvent))

    @property
    def multi_events(self):
        return (e for e in self.events if isinstance(e, MultiEvent))

    @property
    def setup_events(self):
        return (e for e in self.events if isinstance(e, SetupEvent))

    @property
    def selector_events(self):
        return (e for e in self.events if isinstance(e, SelectorEvent))
