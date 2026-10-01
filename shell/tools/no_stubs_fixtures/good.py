#!/usr/bin/env python3
"""good.py -- clean, non-trivial Python bodies. no_stubs.py MUST exit 0."""


def add(a, b):
    total = a + b
    return total


def lookup_or_none(mapping, key):
    # Early-return None is a legitimate guard, not a stub sole-body.
    if key not in mapping:
        return None
    return mapping[key]


def classify(x):
    if x < 0:
        return -1
    elif x == 0:
        return 0
    return 1


def evens_up_to(n):
    if n < 0:
        return []
    return [i for i in range(0, n + 1, 2)]


class Widget:
    def __init__(self, value=0):
        self.value = value
        self.dirty = False

    def double_value(self):
        return self.value * 2

    def set_value(self, v):
        self.value = v
        self.dirty = True
