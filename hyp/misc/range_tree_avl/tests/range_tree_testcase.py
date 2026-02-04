#!/usr/bin/env python3
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

""" Script for test cases generation.

This module generates an input file for the range tree random test.

Based on pgtable_hyp_testcase.py.
"""

import random
import sys
import queue
from collections import Counter
from transitions import Machine


class StateException(Exception):
    # Used to abort a test op that can't progress
    pass


class Range(object):
    def __init__(self, base, end):
        self.base = base
        self.end = end
        self.size = end - base + 1
        self.node_index = None


class TestCase:
    states = ["start", "running", "stop"]
    transitions = [{"trigger": "init",
                    "source": "start",
                    "dest": "running",
                    "after": "gen_init_op",
                    },
                   {"trigger": "destroy",
                    "source": "running",
                    "dest": "stop",
                    "after": "gen_destroy_op",
                    },
                   {"trigger": "step",
                    "source": "running",
                    "dest": "=",
                    "after": "gen_step_ops"},
                   ]

    # the same as C test file
    init_op = 0
    insert_op = 1
    remove_op = 2
    lookup_op = 3
    destroy_op = 4
    check_op = 5
    next_node_op = 6
    split_op = 7

    def __init__(self, debug=False):
        self.top = 1 << 64
        self.debug = debug

        self.machine = Machine(
            model=self,
            states=TestCase.states,
            transitions=TestCase.transitions,
            initial="start",
            auto_transitions=False,
        )

        self.allocations = []

        self.test_ops = queue.Queue()
        self.op_counts = Counter()
        self.err_counts = Counter()
        self.step_ops = [
            self.gen_insert_op,
            self.gen_remove_op,
            self.gen_lookup_found_op,
            self.gen_lookup_missing_op,
            self.gen_overlap_insert_op,
            self.gen_check_op,
            self.gen_next_node_found_op,
            self.gen_next_node_missing_op,
            self.gen_split_op,
            self.gen_split_invalid_op,
        ]

    def format_counts(self, title, counter):
        lines = [f"# {title}"]
        names = sorted(counter.keys())
        width = max((len(name) for name in names), default=0)
        for name in names:
            lines.append(f"#\t{name:<{width}} : {counter[name]:d}")
        total = sum(counter[name] for name in names)
        lines.append(f"#\t{'total':<{width}} : {total:d}")
        return "\n".join(lines)

    def get_op_counts(self):
        return self.format_counts("Operation counts:", self.op_counts)

    def get_err_counts(self):
        return self.format_counts("StateException counts:", self.err_counts)

    def alloc_range(self, r, pos):
        assert r.node_index is None
        msb = self.node_bitmap.bit_length() - 1
        if msb < 0:
            raise StateException("Node ID bitmap is empty")
        self.allocations.insert(pos, r)
        self.node_bitmap = self.node_bitmap & ~(1 << msb)
        r.node_index = msb

    def free_node_count(self):
        return self.node_bitmap.bit_count()

    def free_range(self, r, pos):
        assert r.node_index is not None
        assert (self.node_bitmap & (1 << r.node_index)) == 0
        self.node_bitmap = self.node_bitmap | (1 << r.node_index)
        r.node_index = None
        self.allocations.pop(pos)

    def gen_init_op(self):
        self.node_bitmap = (1 << self.node_count) - 1
        self.test_ops.put(f"{self.init_op:d} {self.node_count:d}")

    def gen_destroy_op(self):
        self.test_ops.put("{:d}".format(self.destroy_op))

    def gen_empty_range(self):
        max_pos = len(self.allocations)
        pos = random.randint(0, max_pos)
        lower_bound = (self.allocations[pos - 1].end + 1) if pos else 0
        upper_bound = self.allocations[pos].base if pos < max_pos else self.top
        if lower_bound == upper_bound:
            raise StateException("No gap between allocations")
        result = sorted((random.randrange(lower_bound, upper_bound)
                         for i in range(2)))
        if result[0] == result[1]:
            raise StateException(
                f"Random range({lower_bound},{upper_bound}) is empty")
        return (Range(result[0], result[1]), pos)

    def gen_overlap_range(self, overlap):
        lower = random.randint(0, overlap.end)
        upper = random.randint(overlap.base, self.top)
        if lower == upper:
            raise StateException("No gap between allocations")
        return Range(lower, upper) if lower < upper else Range(upper, lower)

    def gen_insert_op(self):
        r, pos = self.gen_empty_range()
        self.alloc_range(r, pos)
        self.test_ops.put("# insert")
        self.test_ops.put(
            f"{self.insert_op:d} {r.base:#x} {r.size:#x} {r.node_index:d} 1")

    def gen_remove_op(self):
        if not self.allocations:
            raise StateException("Nothing to remove")
        pos = random.randrange(0, len(self.allocations))
        r = self.allocations[pos]
        self.test_ops.put("# remove")
        self.test_ops.put(f"{self.remove_op:d} {r.node_index:d}")
        self.free_range(r, pos)

    def gen_lookup_found_op(self):
        if not self.allocations:
            raise StateException("Nothing to look up")
        pos = random.randrange(0, len(self.allocations))
        r = self.allocations[pos]
        addr = random.randrange(r.base, r.end + 1)
        size = r.end - addr + 1
        self.test_ops.put("# lookup_found")
        self.test_ops.put(
            f"{self.lookup_op:d} {addr:#x} {size:#x} {r.node_index:d}")

    def gen_lookup_missing_op(self):
        max_pos = len(self.allocations)
        pos = random.randint(0, max_pos)
        lower_bound = (self.allocations[pos - 1].end + 1) if pos else 0
        upper_bound = self.allocations[pos].base if pos < max_pos else self.top
        if lower_bound == upper_bound:
            raise StateException("No gap between allocations")
        addr = random.randrange(lower_bound, upper_bound)
        size = upper_bound - addr
        self.test_ops.put("# lookup_missing")
        self.test_ops.put(f"{self.lookup_op:d} {addr:#x} {size:#x} -1")

    def gen_overlap_insert_op(self):
        if not self.allocations:
            raise StateException("Nothing to overlap with")
        r1 = random.choice(self.allocations)
        r2 = self.gen_overlap_range(r1)
        msb = self.node_bitmap.bit_length() - 1
        if msb < 0:
            raise StateException("Node ID bitmap is empty")
        self.test_ops.put("# overlap_insert")
        self.test_ops.put(
            f"{self.insert_op:d} {r2.base:#x} {r2.size:#x} {msb:d} 0")

    def gen_check_op(self):
        self.test_ops.put("# check")
        self.test_ops.put(
            f"{self.check_op:d} {len(self.allocations):d}")

    def gen_next_node_found_op(self):
        if not self.allocations:
            raise StateException("No next node to find")
        max_pos = len(self.allocations) - 1
        pos = random.randint(0, max_pos)
        lower_bound = self.allocations[pos - 1].base + 1 if pos else 0
        upper_bound = (self.allocations[pos].base if pos <= max_pos else
                       self.top)
        if lower_bound == upper_bound:
            raise StateException("No gap between allocations")
        addr = random.randrange(lower_bound, upper_bound)
        next_index = self.allocations[pos].node_index
        self.test_ops.put("# next_node_found")
        self.test_ops.put(
            f"{self.next_node_op:d} {addr:#x} {next_index:d}")

    def gen_next_node_missing_op(self):
        lower_bound = self.allocations[-1].base + 1 if self.allocations else 0
        addr = random.randrange(lower_bound, self.top)
        self.test_ops.put("# next_node_missing")
        self.test_ops.put(
            f"{self.next_node_op:d} {addr:#x} -1")

    def gen_split_op(self):
        if not self.allocations:
            raise StateException("Nothing to split")
        if self.free_node_count() < 2:
            raise StateException("Not enough free node IDs for split")
        # Find range that's large enough to split
        splittable = [r for r in self.allocations if r.size > 1]
        if not splittable:
            raise StateException("No range large enough to split")
        pos = self.allocations.index(random.choice(splittable))
        r = self.allocations[pos]
        # Randomly choose a valid split point
        split_point = random.randrange(r.base + 1, r.base + r.size)
        # Create new range for second part and insert after existing range
        new_lower_r = Range(r.base, split_point - 1)
        self.alloc_range(new_lower_r, pos + 1)
        new_upper_r = Range(split_point, r.end)
        self.alloc_range(new_upper_r, pos + 2)
        self.test_ops.put("# split")
        self.test_ops.put(
            f"{self.split_op:d} {r.node_index:d} {new_lower_r.node_index:d} "
            f"{new_upper_r.node_index:d} {split_point:#x} 1")
        self.free_range(r, pos)

    def gen_split_invalid_op(self):
        if not self.allocations:
            raise StateException("Nothing to split")
        if self.free_node_count() < 2:
            raise StateException("Not enough free node IDs for split")
        r = random.choice(self.allocations)
        bad_split_points = [
            r.base,
            r.base + r.size,
        ]
        if r.base > 0:
            bad_split_points.append(r.base - 1)
        if r.base + r.size < self.top:
            bad_split_points.append(r.base + r.size + 1)
        bad_split_point = random.choice(bad_split_points)
        # Get lower node index
        msb = self.node_bitmap.bit_length() - 1
        if msb < 0:
            raise StateException("Node ID bitmap is empty")
        # Get upper node index
        remaining = self.node_bitmap & ~(1 << msb)
        if remaining == 0:
            raise StateException("Not enough free node IDs for split_invalid")
        next_msb = remaining.bit_length() - 1
        self.test_ops.put("# split_invalid")
        self.test_ops.put(
            f"{self.split_op:d} {r.node_index:d} {msb:d} {next_msb:d} "
            f"{bad_split_point:#x} 0")

    def gen_step_ops(self):
        op_fn = random.choice(self.step_ops)
        try:
            op_fn()
            self.op_counts[op_fn.__name__] += 1
        except StateException as ex:
            self.err_counts[op_fn.__name__] += 1
            raise StateException(f"{op_fn.__name__}: {ex}") from ex

    def run(self, max_steps, seed):
        self.node_count = 1000
        self.init()

        random.seed(seed)
        yield "# seed({:d})".format(seed)

        step = 0
        while step < max_steps:
            try:
                self.step()
                step = step + 1
                while not self.test_ops.empty():
                    yield self.test_ops.get()
            except StateException as ex:
                if self.debug:
                    yield f"# StateException: {ex}"
                pass

        self.destroy()


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('-o', '--outfile', default=sys.stdout,
                        type=argparse.FileType('w'))
    parser.add_argument('-s', '--seed', type=int,
                        default=random.randrange(0, 1 << 64))
    parser.add_argument('-m', '--max-steps', type=int, default=(1 << 20))
    parser.add_argument('-d', '--debug', action='store_true')

    opts = parser.parse_args()

    m = TestCase(debug=opts.debug)

    for op in m.run(opts.max_steps, opts.seed):
        print(op, file=opts.outfile)

    op_counts = m.get_op_counts()
    print(op_counts, file=opts.outfile)
    err_counts = m.get_err_counts()
    print(err_counts, file=opts.outfile)
    if opts.outfile is not sys.stdout:
        print(op_counts, file=sys.stdout)
        print(err_counts, file=sys.stdout)


if __name__ == "__main__":
    main()
