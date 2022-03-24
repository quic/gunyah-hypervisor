#!/usr/bin/env python3
# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import subprocess
import sys
from datetime import datetime

from genfile import GenFile


def main():
    args = argparse.ArgumentParser()

    args.add_argument("-o", "--output", help="Output file (default: stdout)",
                      default=None)
    args.add_argument("-c", dest='commit', help="Use specified GIT revision",
                      default="HEAD")
    args.add_argument("-C", dest='path',
                      help="Run GIT in the specified directory", default=None)
    args.add_argument("-n", dest='now', action='store_true',
                      help="Use now as timestamp")

    options = args.parse_args()

    cwd = options.path

    ret = subprocess.run(['git', 'diff', options.commit, '--quiet'],
                         cwd=cwd, stdout=subprocess.PIPE)
    dirty = ret.returncode

    ret = subprocess.run(['git', 'rev-parse', '--short', options.commit],
                         cwd=cwd, stdout=subprocess.PIPE)
    if ret.returncode:
        raise Exception('git rev-parse failed\n', ret.stderr)

    rev = ret.stdout.decode("utf-8").strip()

    id = rev + ('-dirty' if dirty else '')
    if options.now or dirty:
        utcnow = datetime.utcnow()
        utcnow = utcnow.replace(microsecond=0)
        time = utcnow.isoformat(sep=' ')
        time = time + ' UTC'
    else:
        ret = subprocess.run(['git', 'show', '-s', '--pretty=%cd',
                              '--date=iso-local', options.commit],
                             cwd=cwd, env={'TZ': 'UTC'},
                             stdout=subprocess.PIPE)
        if ret.returncode:
            raise Exception('git rev-parse failed\n', ret.stderr)
        time = ret.stdout.decode("utf-8").strip()
        time = time.replace('+0000', 'UTC')

    out = '#define HYP_GIT_VERSION {:s}\n'.format(id)
    out += '#define HYP_BUILD_DATE \"{:s}\"\n'.format(time)

    if options.output:
        with GenFile(options.output, 'w', encoding='utf-8') as f:
            f.write(out)
    else:
        sys.stdout.write(out)


if __name__ == '__main__':
    sys.exit(main())
