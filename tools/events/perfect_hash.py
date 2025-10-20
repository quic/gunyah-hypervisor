#!/usr/bin/env python3
# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

import sys
import itertools
import collections


def gen_table(assoc_map, keys):
    hash_map = {}
    for length, key in keys:
        index = length
        for k in key:
            index += assoc_map[k]
        if index in hash_map:
            raise Collision(key, hash_map[index][1])
        hash_map[index] = (length, key)
    return hash_map


def handle_collision(assoc_map, freq_map, first, second):
    assert first != second
    first_set = set(first)
    second_set = set(second)
    to_consider = (first_set | second_set) - (first_set & second_set)
    if not to_consider:
        raise KeyError("no disjoint chars")
    lowest_freq = min(freq_map[ch] for ch in to_consider)
    least_common = sorted(tuple(ch for ch in to_consider
                                if freq_map[ch] == lowest_freq))
    assoc_map[least_common[0]] += 1


class Collision(BaseException):
    def __init__(self, first, second):
        self.first = first
        self.second = second


def try_keyset(words, freq_map, keyset):
    word_map = {}

    for word in words:
        key = ''.join(word[i] for i in keyset if abs(i) < len(word))
        if (len(word), key) in word_map:
            raise KeyError("keyset {:s} has collision".format(str(keyset)))
        word_map[(len(word), key)] = word

    assoc_map = collections.defaultdict(int)
    while True:
        try:
            key_table = gen_table(assoc_map, word_map.keys())
        except Collision as c:
            handle_collision(assoc_map, freq_map, c.first, c.second)
        else:
            break

    word_table = [word_map[key_table[i]] if i in key_table else None
                  for i in range(max(key_table.keys()) + 1)]

    return (keyset, assoc_map, word_table)


def perfect_hash(words):
    words = tuple(sorted(words))

    longest = max(len(word) for word in words)

    freq_map = collections.defaultdict(int)
    for word in words:
        for ch in word:
            freq_map[ch] += 1

    key_candidates = sorted(reversed(range(1 - longest, longest)),
                            key=lambda x: abs(x))
    keysets = itertools.chain(*(itertools.combinations(key_candidates,
                                                       length + 1)
                                for length in range(min(longest, 7))))

    for keyset in keysets:
        try:
            return try_keyset(words, freq_map, keyset)
        except KeyError:
            pass

    raise ValueError("Unable to find perfect hash for words: " +
                     ", ".join(words))


def main(args):
    infile = sys.stdin

    words = set()

    for line in infile.readlines():
        for word in line.split():
            if word in words:
                print("WARNING: duplicate word {:s}".format(word),
                      file=sys.stderr)
            words.add(word)

    (keyset, assoc_map, word_table) = perfect_hash(words)


if __name__ == '__main__':
    main(sys.argv)
