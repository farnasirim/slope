import collections
import json
import matplotlib.pyplot as plt

import etl
import settings
import crunch
import bench_common


def plot_readonly(benches):
    fig, sub0 = plt.subplots(1, 1, figsize=(10, 4))
    fig.set_size_inches(w=settings.plot_width, h=settings.plot_width / (16 / 9))

    logs_list = list(map(etl.get_merged_logs, benches))
    required_keys = ["Prefill duration", "Duration without owner", "End to end latency"]
    infos = list(map(lambda x: bench_common.get_info(x, required_keys), logs_list))

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
        plot_line(fig, sub0, xs, ys, label=plot_name)
        print(ys)

    bench_common.remove_zero(sub0)
    xlabel = "Number of 4KB pages"
    if "using_huge_pages" in logs_list[0]["meta"].items().__iter__().__next__()[1]:
        xlabel = "Number of 2MB pages"

    sub0.set_xlabel(xlabel)
    sub0.set_ylabel("Elapsed time (milliseconds)")

    # plot_line(fig, sub, [1, 2, 3], [4, 5, 6], label="hello", color=(0, 1, 0))

    sub0.legend()
    # points = []
    # for log in logs_list:
    #     info = get_info(log)
    #     print(info)


def plot_line(fig, ax, xs, ys, *args, **kwargs):
    ax.plot(xs, ys,'o-', *args, **kwargs)
