import collections
import json
import matplotlib.pyplot as plt

from brokenaxes import brokenaxes

from brokenaxes import brokenaxes 
import numpy as np 

import etl
import settings
import crunch
import bench_common

def downsample(factor, l):
    ret = []
    for i in range(factor, len(l) + 1, factor):
        ret.append(sum(l[i - factor: i])/factor)
    return ret


def plot_map(bench):
    assert(len(bench) == 1)
    bench = bench[0]
    logs = etl.get_merged_logs(bench)
    fig = plt.figure(figsize=(12, 4))
    fig.set_size_inches(w=settings.plot_width, h=2*settings.plot_width / (16 / 9))

    ts = logs["time_series"]["operation"]


    last_tp = {}
    throughputs = collections.defaultdict(lambda: [(0, 0)])

    x = []
    ys = collections.defaultdict(list)

    names = set()
    for op in ts:
        if ":" in op["value"] and "local" not in op["value"]:
            try:
                name, val = op["value"].split(":")
                tm = op["nanos"]
                val = int(val)
            except:
                continue
            names.add(name)

    for op in ts:
        if ":" in op["value"] and "local" not in op["value"]:
            try:
                name, val = op["value"].split(":")
                tm = op["nanos"]
                val = int(val)
            except:
                continue

            if name in last_tp:
                throughputs[name].append((tm, val / (tm - last_tp[name])))
            last_tp[name] = tm

            x.append(tm)
            for name in names:
                ys[name].append(throughputs[name][-1][1])

    start_x = x[0]
    for i in range(len(x)):
        x[i] -= start_x

    by_name = {}
    for op in logs["time_series"]["operation"]:
        by_name[op["value"]] = op["nanos"] - start_x

    sub0 = brokenaxes(xlims=((0, by_name["call:init_migration"]/1e6 + 50),
                             (by_name["finish: prefill writes"]/1e6 - 50, by_name["finish: reading dirty pages"]/1e6 + 100)), hspace=.1)

    def get_label(v_name):
        if v_name == "start: calling try_finish_write":
            return "finished writes"
        elif v_name == "transfer ownership to destination":
            return "Transferred ownership to the destination"
        elif v_name == "start: calling try_finish_read":
            return "finished reads"
        elif v_name == "received: final confirmation from the destination":
            pass
        elif v_name == "start: reading dirty pages":
            return "Started reading dirty pages"
        elif v_name == "start: prefill writes":
            return "Started prefill writes"
        elif v_name == "finish: prefill writes":
            return "Finished prefill writes"
        elif v_name == "finish: reading dirty pages":
            return "Finished reading dirty pages"
        elif v_name == "call:init_migration":
            return "Started migration"
        elif v_name == "dst_rem_read":
            return "BF1 read at destination"
        elif v_name == "dst_rem_write":
            return "BF1 write at destination"
        elif v_name == "src_ptr_ops_1":
            return "MP read at source"
        elif v_name == "src_ptr_ops_2":
            return "MP update at source"
        elif v_name == "src_ptr_ops_3":
            return "MP insert at source"
        elif v_name == "dst_ptr_ops_1":
            return "MP read at destination"
        elif v_name == "dst_ptr_ops_2":
            return "MP update at destination"
        elif v_name == "dst_ptr_ops_3":
            return "MP insert at destination"
        return ""



    # print(len(x))
    # x = x[:10]
    # for k in ys.keys():
    #     ys[k] = ys[k][:10]

    # print(x)
    # print(ys)



# --- FORMAT 1

# Your x and y axis
    factor = 10
    x = downsample(factor, x)
    keys = sorted(list(throughputs.keys()))
    yys = []
    for k in keys:
        yys.append(downsample(factor, ys[k]))

    for i in range(len(yys)):
        for j in range(len(yys[i])):
            yys[i][j] *= 1000
    for i in range(len(x)):
        x[i] /= 1e6
    # x=range(1,6)
    # y=[ [1,4,6,8,9], [2,2,7,10,12], [2,8,5,10,6] ]

    # for k in keys:
    #     sub0.plot(crunch.select(throughputs[k], 0), crunch.select(throughputs[k], 1), '-',
    #               label=k)

# Basic stacked area chart.
    sub0.stackplot(x,yys, labels=list(map(get_label, keys)))
    verticals = [
                 # "done wait: for call to finish writes",
                 "call:init_migration",
                 "finish: prefill writes",
                 "start: calling try_finish_write",
                 "start: calling try_finish_read",
                 # "start: prefill writes",
                "transfer ownership to destination",
                 "start: reading dirty pages",
                 "finish: reading dirty pages",
                 "received: final confirmation from the destination",
                 # "finish: collect"
                 ]

    colors = ['b',  'y', 'm', 'r', 'g', 'c', 'm', 'k']

    i = 0
    machines = list(logs["meta"].keys())
    for v_name in verticals:
        label = get_label(v_name)
        if len(label):
            sub0.axvline(by_name[v_name] / 1e6, 0, 1, label=label, color=colors[i % len(colors)])
            i += 1
    # sub0.legend(loc='upper left', prop={'size': 6})
    sub0.legend(loc='upper left')

#plt.show()

# --- FORMAT 2</pre>
#     x=range(1,6)
#     y1=[1,4,6,8,9]
#     y2=[2,2,7,10,12]
#     y3=[2,8,5,10,6]
#
# # Basic stacked area chart.
#     plt.stackplot(x,y1, y2, y3, labels=['A','B','C'])
#     plt.legend(loc='upper left')


    # for i, x in enumerate(ticks):
    #     if abs(x) < 1e-9:
    #         ticks[i] = 1.0
    #         break
    # ticks = ticks[1:-1]
    # ax.set_xticks(ticks)


    # required_keys = ["Prefill duration", "Duration without owner", "End to end latency", "Time spent transferring dirty pages"]
    # infos = list(map(lambda x: bench_common.get_info(x, required_keys), logs_list))

    # grouped_info = crunch.group_by(infos, lambda info: info["param_num_pages"],
    #                                crunch.statize)

    # plots_data = collections.defaultdict(list)

    # for k, info in grouped_info.items():
    #     keys = list(k for k in info.keys() if not k.startswith("param_"))
    #     param = list(k for k in info.keys() if k.startswith("param_"))[0]
    #     for k in keys:
    #         plots_data[k].append((info[param].first(), info[k].mean()))

    # for plot_name, plot_data in plots_data.items():
    #     data = sorted(plot_data)
    #     xs = crunch.select(data, 0)
    #     ys = crunch.select(data, 1)
    #     ys = list(map(lambda x: x/1e6, ys))
    #     plot_line(fig, sub0, xs, ys, label=plot_name)
    #     print(ys)

    # bench_common.remove_zero(sub0)
    sub0.set_xlabel("Time (milliseconds)")
    sub0.set_ylabel("Throughput (million operations/second)")
    # # plot_line(fig, sub, [1, 2, 3], [4, 5, 6], label="hello", color=(0, 1, 0))

    # sub0.legend()
    # points = []
    # for log in logs_list:
    #     info = get_info(log)
    #     print(info)


def plot_line(fig, ax, xs, ys, *args, **kwargs):
    ax.plot(xs, ys,'o-', *args, **kwargs)

