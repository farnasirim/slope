#!/usr/bin/env python
import os
import string
import json


def get_merged_logs(bench):
    ret = {
        "meta": {},
        "time_series": {}
    }

    for file_name in bench.keys():
        if any(map(lambda ch: ch in file_name, string.digits)):
            j = json.loads(bench[file_name])
            ret["meta"][file_name] = j["meta"]
            for ts_name in j["time_series"].keys():
                if ts_name not in ret["time_series"]:
                    ret["time_series"][ts_name] = []
                for obj in j["time_series"][ts_name]:
                    obj["node_name"] = file_name
                    ret["time_series"][ts_name].append(obj)

    for ts_name in ret["time_series"].keys():
        ret["time_series"][ts_name] = sorted(ret["time_series"][ts_name],
                                             key=lambda x: x["nanos"])
    return ret

def read_file(file_addr):
    with open(file_addr) as f:
        return f.read().strip()


def bench_from_dir(dir, tag=None):
    ret = {}
    if tag is not None and read_file(os.path.join(dir, "tag")) != tag:
        return None

    for root, _, files in os.walk(dir):
        for file in files:
            ret[file] = read_file(os.path.join(root, file))
    return ret


def get_benches(root_dir, tag=None):
    ret = []
    for dir in get_bench_dirs(root_dir):
        maybe_bench = bench_from_dir(dir, tag)
        if maybe_bench:
            ret.append(maybe_bench)

    return ret


def get_bench_dirs(root_dir):
    maybe_bench_dirs = get_sub_dirs_rec(root_dir)
    return list(set(filter(lambda d: os.path.isfile(os.path.join(d, "tag")),
                       maybe_bench_dirs)))


def get_sub_dirs_rec(current_dir):
    dirs_ret = []
    for root, dirs, files in os.walk(current_dir):
        for dir in dirs:
            dirs_ret.append(os.path.abspath(os.path.join(root, dir)))

    return dirs_ret
