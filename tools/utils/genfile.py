# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
"""

import sys
import io
import argparse


class _ReplaceFileMixin(object):
    def __init__(self, name, mode, encoding=None):
        super().__init__()
        self._name = name
        self._mode = mode
        self._encoding = encoding

    @property
    def name(self):
        return self._name

    def close(self):
        tmp = io.open(self._name, self._mode.replace('w', 'r'),
                      encoding=self._encoding)
        old = tmp.read()
        tmp.close()
        self.seek(0)
        new = self.read()
        if old != new:
            replace = io.open(self._name, self._mode, encoding=self._encoding)
            replace.write(new)
            replace.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


class _ReplaceBinaryFile(_ReplaceFileMixin, io.BytesIO):
    pass


class _ReplaceTextFile(_ReplaceFileMixin, io.StringIO):
    pass


class _GenFileFactory(object):
    def __init__(self, mode, encoding=None):
        self._mode = mode
        if mode not in ('w', 'wt', 'wb'):
            raise ValueError("mode {:s} not supported".format(mode))
        if encoding is None and 'b' not in mode:
            # Default to UTF-8 for text files
            encoding = 'utf-8'
        self._encoding = encoding

    def __call__(self, p):
        if sys.hexversion < 0x03030000:
            # Exclusive file creation ('x' mode) isn't available before Python
            # 3.3, so fall back to just replacing the file.
            return io.open(p, self._mode, encoding=self._encoding)

        try:
            return io.open(p, self._mode.replace('w', 'x'),
                           encoding=self._encoding)
        except FileExistsError:
            if 'b' in self._mode:
                return _ReplaceBinaryFile(p, self._mode)
            else:
                return _ReplaceTextFile(p, self._mode, encoding=self._encoding)


class GenFileType(_GenFileFactory, argparse.FileType):
    def __call__(self, p):
        if p == '-':
            assert 'w' in self._mode
            return sys.stdout
        try:
            return super().__call__(p)
        except OSError as e:
            raise argparse.ArgumentTypeError(
                "can't open {:s}: {:s}".format(p, str(e)))


def GenFile(name, mode, encoding=None):
    return _GenFileFactory(mode, encoding=encoding)(name)
