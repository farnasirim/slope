import matplotlib.pyplot as plt
import numpy as np


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

    try:
        info["Prefill duration"] = by_name["finish prefill"] - \
            by_name["start init_migration"]
    except:
        pass


    try:
        info["Duration without owner"] = by_name["received ownership"] - \
            by_name["object locked - proceed to commit"]
    except:
        pass


    try:
        info["Time spent transferring dirty pages"] = by_name["read all dirty pages"] - \
            by_name["got dirty pages"]
    except:
        pass


    try:
        info["End to end latency"] = by_name["sent final confirmation to source"] - \
            by_name["start init_migration"]
    except:
        pass

    want_keys = set(required_keys)
    del_keys = set()
    for k in info.keys():
        if not k.startswith("param_") and k not in want_keys:
            del_keys.add(k)

    for k in del_keys:
        del info[k]

    return info


def remove_zero(ax):
    ticks = ax.get_xticks()
    for i, x in enumerate(ticks):
        if abs(x) < 1e-9:
            ticks[i] = 1.0
            break
    ticks = ticks[1:-1]
    ax.set_xticks(ticks)
