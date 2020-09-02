import matplotlib.pyplot as plt
import numpy as np


def time_between(log, fr, to):
    if fr not in log or to not in log:
        return None
    return log[to] - log[fr]


def clean_none_keys(d):
    to_del = set()
    for k in d.keys():
        if d[k] is None:
            to_del.add(k)

    for k in to_del:
        del d[k]


def get_info(log, required_keys):
    machines = list(log["meta"].keys())
    for machine in machines:
        assert(log["meta"][machine]["param_num_pages"]["value"] == \
               log["meta"][machines[0]]["param_num_pages"]["value"])

    info = {}
    info["param_num_pages"] = int(log["meta"][machines[0]]["param_num_pages"]["value"])

    by_name = {}
    for op in log["time_series"]["operation"]:
        assert(op["value"] in "finished time calibration" or
               op["value"] not in by_name)
        by_name[op["value"]] = op["nanos"]

    info["Prefill duration"] = time_between(by_name,
        "start: init_migration", "finish: prefill writes")

    info["Duration without owner"] = time_between(by_name,
        "transfer ownership to destination", "done wait: receive ownership")

    info["Time spent transferring dirty pages"] = time_between(by_name,
                "start: setting page protections", "finish: reading dirty pages")

    info["End to end latency"] = time_between(by_name, "start: init_migration",
        "received: final confirmation from the destination")

    # info["Time to send the object"] = time_between(by_name,
    #     "start: check_bandwidth", "finish: check_bandwidth")


    want_keys = set(required_keys)
    del_keys = set()
    for k in info.keys():
        if not k.startswith("param_") and k not in want_keys:
            del_keys.add(k)

    for k in del_keys:
        del info[k]

    clean_none_keys(info)
    return info


def remove_zero(ax):
    ticks = ax.get_xticks()
    for i, x in enumerate(ticks):
        if abs(x) < 1e-9:
            ticks[i] = 1.0
            break
    ticks = ticks[1:-1]
    ax.set_xticks(ticks)
