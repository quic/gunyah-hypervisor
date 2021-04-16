# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

import re


class BaseError(Exception):
    def get_context(self, text, pos, span=40):
        start = max(pos - span, 0)
        end = pos + span
        before = text[start:pos].rsplit('\n', 1)[-1]
        after = text[pos:end].split('\n', 1)[0]
        before_spaces = re.sub(r'\S', ' ', before)
        return before + after + '\n' + before_spaces + '^\n'


class DSLError(BaseError):
    def __init__(self, message, token, expecting=None, state=None):
        meta = getattr(token, 'meta', token)
        self.line = getattr(meta, 'line', '?')
        self.column = getattr(meta, 'column', '?')
        self.pos_in_stream = getattr(meta, 'start_pos',
                                     getattr(meta, 'pos_in_stream', None))
        self.expecting = expecting
        self.state = state

        message = "\nError: %s\n" % message
        message += "\nToken %s, at line %s col %s:\n" % (
            str(token), self.line, self.column)
        if isinstance(self.pos_in_stream, int):
            message += '\n' + self.get_context(token.program,
                                               self.pos_in_stream)
        if expecting:
            message += '\nExpecting: %s\n' % expecting

        super(DSLError, self).__init__(message)


class DSLErrorWithRefs(BaseError):
    def __init__(self, message, token, refs, expecting=None, state=None):
        meta = getattr(token, 'meta', token)
        self.line = getattr(meta, 'line', '?')
        self.column = getattr(meta, 'column', '?')
        self.pos_in_stream = getattr(meta, 'start_pos',
                                     getattr(meta, 'pos_in_stream', None))
        self.expecting = expecting
        self.state = state

        message = "\nError: %s\n" % message
        message += "\nAt line %d col %d:\n" % (self.line, self.column)
        message += '\n' + self.get_context(token.program, self.pos_in_stream)

        message += "\nRefs:"
        for r in refs:
            line = getattr(r, 'line', '?')
            column = getattr(r, 'column', '?')
            pos = getattr(r, 'pos_in_stream', None)
            message += "\nAt line %d col %d:\n" % (line, column)
            message += '\n' + self.get_context(token.program, pos)

        if expecting:
            message += '\nExpecting: %s\n' % expecting

        super(DSLErrorWithRefs, self).__init__(message)


class RangeError(DSLError):
    pass
