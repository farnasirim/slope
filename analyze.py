#!/usr/bin/env python
import os
import sys
import matplotlib
import numpy as np
import json
import collections
import math
import pickle

import etl
from crunch import *
from readonly import plot_readonly
from writeall import plot_writeall

from multiprocessing import Pool
from multiprocessing.pool import ThreadPool

from numpy.polynomial.polynomial import polyfit

import pylab as pl
from matplotlib import collections  as mc

import mpl_toolkits.mplot3d.axes3d as p3
import matplotlib.pyplot as plt
import matplotlib.animation as animation


import argparse


def main():
    parser = argparse.ArgumentParser(description='analyze benchmarks')
    parser.add_argument('action', type=str)
    parser.add_argument('--dir', type=str)
    parser.add_argument('--workload', type=str)
    parser.add_argument('--tag', type=str)
    parser.add_argument('--texname', type=str, default=None)
    args = parser.parse_args()

    benches = etl.get_benches(args.dir, args.tag)

    if args.texname:
        matplotlib.use("pgf")
        matplotlib.rcParams.update({
            "pgf.texsystem": "pdflatex",
            'font.family': 'serif',
            'text.usetex': True,
            'pgf.rcfonts': False,
        })

    if args.action == "plot":
        if args.workload == "readonly":
            plot_readonly(benches)
        elif args.workload == "writeall":
            plot_writeall(benches)
        else:
            assert(False)
    else:
        assert(False)

    if args.texname:
        plt.savefig(args.texname)
    else:
        plt.show()

    return

    cwd = os.path.dirname(os.path.abspath(sys.argv[0]))
    runs_data_dir = os.path.join(cwd, sys.argv[1])
    sub_dirs = get_sub_dirs_rec(runs_data_dir)

    results = map(result_from_dir, sub_dirs)
    results = list(filter(lambda x: x is not None, results))

    results = sorted(results, key=lambda x: (x.key(), x.total_time), reverse=True)

    grouped_results = group_by(results, lambda r: r.key())
    stats = list(map(lambda res: Result(*res[0].key(),
                                Stat(select(res, "insert_time"), drop_min=True, drop_max=True),
                                Stat(select(res, "init_time") ,  drop_min=True, drop_max=True),
                                Stat(select(res, "exec_time") ,  drop_min=True, drop_max=True),
                                Stat(select(res, "total_time"),  drop_min=True, drop_max=True),
                                Stat(select(res, "throughput"),  drop_min=True, drop_max=True),
                                ),
             grouped_results.values()))


    for sk in sorted(group_by(results, lambda x: x.skew).keys()):
        this_skew_stats = where(stats, lambda s: s.skew == sk)
        fig, ((ax0, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(10, 8))
        axs = [ax0, ax2, ax3, ax4]
        fig.suptitle("skew: {} - statistics of total time".format(sk))

        statistical_funs = [lambda x: x.mean(), lambda x: x.std(), lambda x: x.min(), lambda x: x.max()]
        plot_values = ["Throughput mean [txn/s]", "Throughput stddev", "Throughput min [txn/s]", "Throughput max [txn/s]"]
        cmaps = ["RdYlGn", "RdYlGn_r", "RdYlGn", "RdYlGn"]

        for st_func, plot_val, ax, cmp in zip(statistical_funs, plot_values, axs, cmaps):
            d1, d2, grid = group_2d_agg(this_skew_stats, "core", "para", lambda x: st_func(x.throughput))
            d1[0] = "cs {}".format(d1[0])
            d2[0] = "hp {}".format(d2[0])
            im, _ = heatmap(grid, d1, d2, ax=ax,
                            cmap=cmp, cbarlabel=plot_val)
            annotate_heatmap(im, valfmt="{x:.1f}", size=7)

        plt.tight_layout()



    for sk in sorted(group_by(results, lambda x: x.skew).keys()):
        this_skew_stats = where(stats, lambda s: s.skew == sk)
        fig, ((ax0, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(10, 8))
        axs = [ax0, ax2, ax3, ax4]
        fig.suptitle("skew: {} - decomposition of total time".format(sk))

        plot_values = ["insert_time", "init_time", "exec_time", "total_time"]


        for plot_val, ax in zip(plot_values, axs):
            d1, d2, grid = group_2d_agg(this_skew_stats, "core", "para", lambda x: getattr(x, plot_val).mean())
            im, _ = heatmap(grid, d1, d2, ax=ax,
                            cmap="RdYlGn_r", cbarlabel=plot_val + " [ms]")
            annotate_heatmap(im, valfmt="{x:.1f}", size=7)

        plt.tight_layout()

    sk_keys = sorted(group_by(results, lambda x: x.skew).keys())
    fig, all_axs = plt.subplots(len(sk_keys), 2, figsize=(10, 8))
    fig.suptitle("Throughput against single parameters")
    for sk, ax in zip(sk_keys, all_axs):
        ax0, ax1 = ax[0], ax[1]
        this_skew_stats = where(stats, lambda s: s.skew == sk)

        d1, d2, _ = group_2d_agg(this_skew_stats, "core", "para", lambda x: x)
        ys = []
        xs = []
        yerr = []
        annotations = []
        scatter_x, scatter_y = [], []

        for d in d1:
            best_point = fold(where(this_skew_stats, lambda x: x.core == d), lambda x, y: x if x.throughput.mean() > y.throughput.mean() else y)
            xs.append(d)
            ys.append(best_point.throughput.mean())
            yerr.append(best_point.throughput.std())
            for val in best_point.throughput.elements:
                scatter_x.append(d)
                scatter_y.append(val)
            annotations.append("hp: {}".format(best_point.para))

        plot_err(fig, ax0, xs, ys, yerr, annotations, label="skew: {}".format(sk), color=(0, 0, 1, 0.7))
        ax0.set_xlabel("core scaling, skew: {}".format(sk))
        ax0.set_ylabel("max(Throughput mean [txn/s], hp)")
        ax0.scatter(scatter_x, scatter_y, marker='+', color=(0, 0, 0, 0.5))


        ys = []
        xs = []
        yerr = []
        annotations = []
        scatter_x, scatter_y = [], []

        for d in d2:
            best_point = fold(where(this_skew_stats, lambda x: x.para == d), lambda x, y: x if x.throughput.mean() > y.throughput.mean() else y)
            xs.append(d)
            ys.append(best_point.throughput.mean())
            yerr.append(best_point.throughput.std())
            for val in best_point.throughput.elements:
                scatter_x.append(d)
                scatter_y.append(val)
            annotations.append("cs: {}".format(best_point.core))

        plot_err(fig, ax1, xs, ys, yerr, annotations, label="skew: {}", color=(0, 0, 1, 0.7))
        ax1.set_xlabel("handle parallel, skew: {}".format(sk))
        ax1.set_ylabel("best (Throughput mean [txn/s], cs) pair")
        ax1.scatter(scatter_x, scatter_y, marker='+', color=(0, 0, 0, 0.5))

    plt.show()
    return


    skews = group_by(results, lambda result: result.skew)
    paras = group_by(results, lambda result: result.para)
    cores = group_by(results, lambda result: result.core)

    for skew in skews.keys():
        current_results = skews[skew]
        plot_grid(xs=select(current_results, "para"),
                  ys=select(current_results, "core"),
                  vals=select(current_results, "total_time"),
                  title="skew: {}".format(skew),
                  xlabel="VHandleParallel",
                  ylabel="CoreScaling")

    fold_funcs = (lambda x, y: x if x.total_time > y.total_time else y,
                  lambda x, y: x if x.total_time < y.total_time else y)
    fold_descs = ["max", "min"]
    colors = ((1, 0, 0), (0, 1, 0))

    fig, ax = plt.subplots()
    xylims(ax, select(results, lambda result: result.para),
           select(results, lambda result: result.total_time))

    for skew in skews.keys():
        current_results = skews[skew]
        color_intensity_coef = float(skew)/100

        this_paras = group_by(current_results, lambda result: result.para)
        for fold_func, fold_desc, color in zip(fold_funcs, fold_descs, colors):
            xs = []
            ys = []
            annotations = []
            for p in sorted(this_paras.keys()):
                points = this_paras[p]
                chosen = fold(points, fold_func)
                xs.append(chosen.para)
                ys.append(chosen.total_time)
                annotations.append(chosen.core)
            plot_line(fig, ax, xs=xs, ys=ys, annotations=list(map(lambda x: "cs: {}".format(x), annotations)),
                  label=fold_desc + " among 'CoreScaling's, skew: {}".format(skew),
                  color=(*color, color_intensity_coef))

    fig, ax = plt.subplots()
    xylims(ax, select(results, lambda result: result.para),
           select(results, lambda result: result.total_time))

    for skew in skews.keys():
        current_results = skews[skew]
        color_intensity_coef = float(skew)/100

        this_cores = group_by(current_results, lambda result: result.core)
        for fold_func, fold_desc, color in zip(fold_funcs, fold_descs, colors):
            xs = []
            ys = []
            annotations = []
            for p in sorted(this_cores.keys()):
                points = this_cores[p]
                chosen = fold(points, fold_func)
                xs.append(chosen.core)
                ys.append(chosen.total_time)
                annotations.append(chosen.para)
            plot_line(fig, ax, xs=xs, ys=ys, annotations=list(map(lambda x: "hp: {}".format(x), annotations)),
                  label=fold_desc + " among 'HandleParallel's, skew: {}".format(skew),
                 color=(*color, color_intensity_coef))
    plt.legend()


    plt.show()


def plot_line(fig, ax, xs, ys, annotations, label, color):
    plt.plot(xs, ys, label=label, c=color)
    for i in range(len(annotations)):
        ax.annotate(annotations[i], xy=(xs[i], ys[i]))


def plot_err(fig, ax, xs, ys, yerr, annotations, label, color):
    ax.errorbar(xs, ys, yerr=yerr, label=label, c=color)
    x = np.array(xs)
    y = np.array(ys)
    b, m = polyfit(x, y, 1)
    ax.plot(x, x * m + b, '-', c='red')
    for i in range(len(annotations)):
        ax.annotate(annotations[i], xy=(xs[i], ys[i]))


def xylims(ax, xs, ys):
    xmin = min(xs)
    xmax = max(xs)
    d = (xmax - xmin + 1)
    xmin -= d * 0.05
    xmax += d * 0.05
    ymin = min(ys)
    ymax = max(ys)
    d = (ymax - ymin + 1)
    ymin -= d * 0.05
    ymax += d * 0.05

    ax.set_xlim(xmin=xmin, xmax=xmax)
    ax.set_ylim(ymin=ymin, ymax=ymax)


def plot_grid(xs, ys, vals, title, xlabel="", ylabel=""):
    fig, ax = plt.subplots()

    xylims(ax, xs, ys)


    smallest_val = min(vals)
    biggest_val = max(vals)
    value_range = biggest_val - smallest_val

    smallest_circle = 0.1
    biggest_circle = 0.5
    circle_range = value_range

    color_range = value_range
    start_color = color.rgb_to_hsv(0, 1, 0)
    end_color = color.rgb_to_hsv(1, 0, 0)

    for (x, y, val) in zip(xs, ys, vals):
        current_color = color.hsv_to_rgb(*color.lerp3(start_color, end_color, val - smallest_val, color_range))
        current_radius = smallest_circle + (val - smallest_val) * biggest_circle / value_range
        ax.add_artist(plt.Circle((x, y), current_radius, color=current_color))

    plt.xlabel(xlabel)
    plt.ylabel(ylabel)

    plt.title(title)


if __name__ == "__main__":
    main()
