#!/usr/bin/env python
import collections

import numpy as np

def fold(results, reduce_func):
    ret = results[0]
    for res in results[1:]:
        ret = reduce_func(ret, res)

    return ret

def group_by(results, key_func, agg_func=None):
    ret = collections.defaultdict(list)
    for result in results:
        k1 = key_func(result)
        ret[key_func(result)].append(result)

    if agg_func and callable(agg_func):
        for k in ret.keys():
            ret[k] = agg_func(ret[k])

    return ret


def select(results, key):
    ret = []
    for res in results:
        now = None
        if type(key) is str:
            if hasattr(res, key):
                now = getattr(res, key)
            else:
                now = res[key]
            if callable(now):
                now = now()
        elif callable(key):
            now = key(res)
        elif type(key) is int:
            now = res[key]
        ret.append(now)
    return ret


def where(results, filter_func):
    return [result for result in results if filter_func(result)]


def uniqued(ls, key):
    d = {}
    for val in ls:
        d[key(val)] = val
    return d.values()


class Stat():
    def __init__(self, elements, drop_min=False, drop_max=False):
        if drop_min:
            elements = sorted(elements)[1:]
        if drop_max:
            elements = sorted(elements[:-1])

        self.elements = elements

    def min(self):
        return np.min(self.elements)

    def max(self):
        return np.max(self.elements)

    def std(self):
        return np.std(self.elements)

    def mean(self):
        return np.mean(self.elements)

    def median(self):
        return np.median(self.elements)

    def first(self):
        return self.elements[0]


def statize(elements):
    if elements is None or len(elements) == 0:
        return {}

    ret = {}
    for k in elements[0].keys():
        ret[k] = Stat(select(elements, k))

    return ret

