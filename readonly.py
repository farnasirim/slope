import collections
import json
import matplotlib.pyplot as plt

import etl
import settings
import crunch


def get_info(log):
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

    info["Prefill duration"] = by_name["finish prefill"] - \
        by_name["start init_migration"]

    info["Duration without owner"] = by_name["received ownership"] - \
        by_name["object locked - proceed to commit"]

    # info["Time spent transferring dirty pages"] = by_name["received ownership"] - \
    #     by_name["got dirty pages"]

    info["End to end latency"] = by_name["received ownership"] - \
        by_name["start init_migration"]

    return info


def plot_readonly(benches):
    fig, (sub0, sub1) = plt.subplots(2, 1, figsize=(10, 8))
    fig.set_size_inches(w=settings.plot_width, h=2*settings.plot_width / (16 / 9))

    logs_list = list(map(etl.get_merged_logs, benches))
    infos = list(map(get_info, logs_list))

    grouped_info = crunch.group_by(infos, lambda info: info["param_num_pages"],
                                   crunch.statize)

    plots_data = collections.defaultdict(list)

    for k, info in grouped_info.items():
        keys = list(k for k in info.keys() if not k.startswith("param_"))
        param = list(k for k in info.keys() if k.startswith("param_"))[0]
        for k in keys:
            plots_data[k].append((info[param].first(), info[k].mean()))

    for plot_name, plot_data in plots_data.items():
        data = sorted(plot_data)
        xs = crunch.select(data, 0)
        ys = crunch.select(data, 1)
        ys = list(map(lambda x: x/1e6, ys))
        if plot_name == "Duration without owner":
            plot_line(fig, sub1, xs, ys, label=plot_name)
        else:
            plot_line(fig, sub0, xs, ys, label=plot_name)

    sub0.set_xlabel("Number of 4KB pages")
    sub0.set_ylabel("Elapsed time (milliseconds)")
    sub0.set_xlim(xmin=1)

    sub1.set_xlabel("Number of 4KB pages")
    sub1.set_ylabel("Elapsed time (milliseconds)")

    # plot_line(fig, sub, [1, 2, 3], [4, 5, 6], label="hello", color=(0, 1, 0))

    sub0.legend()
    sub1.legend()
    # points = []
    # for log in logs_list:
    #     info = get_info(log)
    #     print(info)


def plot_line(fig, ax, xs, ys, *args, **kwargs):
    ax.plot(xs, ys,'o-', *args, **kwargs)

