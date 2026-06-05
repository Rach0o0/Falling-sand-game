"""parse the output benchmark.gd CSV, make report figures, print the table values
python3 analyze_benchmarks.py [--csv benchmarks/bench_*.csv] [--outdir <figures dir>]
"""
import argparse
import glob
import os
import sys

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

THROUGHPUT_SCENARIOS = [0, 1, 2, 3]
SINGLE_SOURCE = 4

METHOD_PRETTY = {
    "cpu_sequential": "CPU sequential",
    "cpu_parallel": "CPU double buffer",
    "cpu_parallel_columnband": "CPU column bands (locks)",
    "cpu_margolus": "CPU Margolus",
    "gpu": "GPU double buffer",
    "gpu margolus": "GPU Margolus",
}
METHOD_ORDER = ["cpu_sequential", "cpu_parallel", "cpu_parallel_columnband",
                "cpu_margolus", "gpu", "gpu margolus"]
PARALLEL_CPU = ["cpu_parallel", "cpu_parallel_columnband", "cpu_margolus"]
GPU_METHODS = ["gpu", "gpu margolus"]

SCEN_PRETTY = {
    0: "random dense", 1: "checkerboard", 2: "full upper half",
    3: "hourglass", 4: "single source",
}

CONFIG_KEYS = ["sweep", "method_id", "method_name", "scenario_id", "scenario_name",
               "width", "height", "threads", "wg_x", "wg_y", "steps",
               "particles_init", "particles_final", "conserved",
               "fair_L", "fair_R", "fair_bias"]


def find_csv(arg):
    if arg:
        return arg
    here = os.path.dirname(os.path.abspath(__file__))
    cands = sorted(glob.glob(os.path.join(here, "benchmarks", "bench_*.csv")))
    if not cands:
        sys.exit("no benchmarks/bench_*.csv found; pass --csv")
    return cands[-1]


#reduce var
def median_per_config(df):
    g = df.groupby(CONFIG_KEYS, as_index=False).agg(
        run_ms=("run_ms", "median"),
        ms_per_frame=("ms_per_frame", "median"),
        n=("repeat_idx", "count"),
    )
    g["mcells_per_s"] = g["width"] * g["height"] * g["steps"] / (g["run_ms"] / 1000.0) / 1e6
    return g


def avg_over_scenarios(cfg, scenarios, keep):
    sub = cfg[cfg["scenario_id"].isin(scenarios)]
    return sub.groupby(keep, as_index=False).agg(
        ms_per_frame=("ms_per_frame", "mean"),
        run_ms=("run_ms", "mean"),
        mcells_per_s=("mcells_per_s", "mean"),
        nscn=("scenario_id", "nunique"),
    )


# --------------------------------------------------------------------------- plots

def plot_comparison(cfg, outdir):
    comp = cfg[cfg["sweep"] == "comparison"]
    a = avg_over_scenarios(comp, THROUGHPUT_SCENARIOS, ["method_name", "width"])

    # throughput vs grid size
    fig, ax = plt.subplots(figsize=(6.2, 4.2))
    for m in METHOD_ORDER:
        d = a[a["method_name"] == m].sort_values("width")
        if d.empty:
            continue
        ax.plot(d["width"], d["mcells_per_s"], marker="o", label=METHOD_PRETTY.get(m, m))
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("grid side $N$ (grid $N\\times N$)")
    ax.set_ylabel("throughput (Mcells/s)")
    ax.set_title("Throughput vs grid size (mean over the 4 scenarios)")
    ax.grid(True, which="both", ls=":", alpha=0.6)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "bench_throughput.png"), dpi=150)
    plt.close(fig)

    # time per step vs grid size (log-log)
    fig, ax = plt.subplots(figsize=(6.2, 4.2))
    for m in METHOD_ORDER:
        d = a[a["method_name"] == m].sort_values("width")
        if d.empty:
            continue
        ax.plot(d["width"], d["ms_per_frame"], marker="o", label=METHOD_PRETTY.get(m, m))
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("grid side $N$ (grid $N\\times N$)")
    ax.set_ylabel("time per step (ms)")
    ax.set_title("Time per step vs grid size (mean over the 4 scenarios)")
    ax.grid(True, which="both", ls=":", alpha=0.6)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "bench_time_per_step.png"), dpi=150)
    plt.close(fig)


def plot_cpu_scaling(cfg, outdir):
    sc = cfg[cfg["sweep"] == "cpu_scaling"]
    a = avg_over_scenarios(sc, THROUGHPUT_SCENARIOS, ["method_name", "width", "threads"])
    sizes = sorted(a["width"].unique())
    size = sizes[-1] if sizes else None  # largest grid -> cleanest scaling
    if size is None:
        return None

    # speedup vs 1 thread
    fig, ax = plt.subplots(figsize=(6.2, 4.2))
    tmax = int(a[a["width"] == size]["threads"].max())
    for m in PARALLEL_CPU:
        d = a[(a["method_name"] == m) & (a["width"] == size)].sort_values("threads")
        base = d[d["threads"] == 1]["ms_per_frame"]
        if d.empty or base.empty:
            continue
        ax.plot(d["threads"], base.values[0] / d["ms_per_frame"],
                marker="o", label=METHOD_PRETTY.get(m, m))
    ax.plot([1, tmax], [1, tmax], "k--", lw=1, label="ideal (linear)")
    ax.set_xlabel("threads")
    ax.set_ylabel("speedup vs 1 thread")
    ax.set_title(f"CPU thread scaling at {size}$\\times${size} (mean over 4 scenarios)")
    ax.grid(True, ls=":", alpha=0.6)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "bench_cpu_speedup.png"), dpi=150)
    plt.close(fig)
    return size


def plot_gpu_workgroup(cfg, outdir):
    gw = cfg[cfg["sweep"] == "gpu_workgroup"].copy()
    if gw.empty:
        return
    a = avg_over_scenarios(gw, THROUGHPUT_SCENARIOS, ["method_name", "width", "wg_x", "wg_y"])
    a["wg"] = a["wg_x"].astype(int).astype(str) + "x" + a["wg_y"].astype(int).astype(str)
    a = a.sort_values(["wg_x", "wg_y"])
    for m in GPU_METHODS:
        dm = a[a["method_name"] == m]
        if dm.empty:
            continue
        fig, ax = plt.subplots(figsize=(6.2, 4.2))
        for size in sorted(dm["width"].unique()):
            d = dm[dm["width"] == size]
            ax.plot(d["wg"], d["ms_per_frame"], marker="o", label=f"{size}x{size}")
        ax.set_xlabel("workgroup (local_size)")
        ax.set_ylabel("time per step (ms)")
        ax.set_title(f"{METHOD_PRETTY.get(m, m)}: workgroup layout (mean over 4 scenarios)")
        ax.grid(True, ls=":", alpha=0.6)
        ax.legend(fontsize=8, title="grid")
        fig.tight_layout()
        tag = m.replace(" ", "_")
        fig.savefig(os.path.join(outdir, f"bench_gpu_workgroup_{tag}.png"), dpi=150)
        plt.close(fig)


# -------------------------------------------------------------------------- tables

def hr(title):
    print("\n" + "=" * 70 + f"\n{title}\n" + "=" * 70)


def table_conservation(df):
    hr("CONSERVATION  (every method x scenario x repeat must keep the count)")
    order = [(4, 256), (3, 256), (1, 256), (0, 256), (2, 256)]  # (scenario_id, repr. size)
    print(f"{'Scenario':18} {'Initial':>9} {'Final':>9}  {'Pass/Fail'}")
    for scn_id, size in order:
        rows = df[df["scenario_id"] == scn_id]
        if rows.empty:
            continue
        ok = bool((rows["conserved"] == 1).all())
        rep = rows[rows["width"] == size]
        if rep.empty:
            rep = rows
        init = int(rep["particles_init"].iloc[0])
        final = int(rep["particles_final"].iloc[0])
        print(f"{SCEN_PRETTY[scn_id]:18} {init:>9} {final:>9}  {'Pass' if ok else 'FAIL'}"
              f"   (repr. {size}x{size}, all methods checked)")


def table_fairness(cfg):
    hr("FAIRNESS  (single source, settled pile asymmetry around centre column)")
    f = cfg[(cfg["sweep"] == "fairness") & (cfg["scenario_id"] == SINGLE_SOURCE)]
    print(f"{'Implementation':30} {'Left':>7} {'Right':>7} {'Bias |L-R|/(L+R)':>18}")
    for m in METHOD_ORDER:
        d = f[f["method_name"] == m]
        if d.empty:
            continue
        L = int(d["fair_L"].iloc[0])
        R = int(d["fair_R"].iloc[0])
        b = float(d["fair_bias"].iloc[0])
        print(f"{METHOD_PRETTY.get(m, m):30} {L:>7} {R:>7} {b:>18.4f}")


def table_cpu_scaling(cfg):
    sc = cfg[cfg["sweep"] == "cpu_scaling"]
    a = avg_over_scenarios(sc, THROUGHPUT_SCENARIOS, ["method_name", "width", "threads"])
    sizes = sorted(a["width"].unique())
    if not sizes:
        return
    size = sizes[-1]
    steps = int(cfg[cfg["sweep"] == "cpu_scaling"]["steps"].iloc[0])
    # sequential reference (1 thread) for absolute speedup
    seq = a[(a["method_name"] == "cpu_sequential") & (a["width"] == size)]
    seq_ms = seq[seq["threads"] == 1]["ms_per_frame"]
    seq_ms = seq_ms.values[0] if not seq_ms.empty else None
    for m in PARALLEL_CPU:
        hr(f"CPU THREAD SCALING  {METHOD_PRETTY.get(m, m)} @ {size}x{size} "
           f"(mean over 4 scenarios, {steps} steps)")
        d = a[(a["method_name"] == m) & (a["width"] == size)].sort_values("threads")
        base = d[d["threads"] == 1]["ms_per_frame"]
        base = base.values[0] if not base.empty else None
        print(f"{'Threads':>7} {'Total ms':>10} {'ms/step':>9} "
              f"{'Speedup(self)':>14} {'Speedup(vs seq)':>16}")
        for _, r in d.iterrows():
            t = int(r["threads"])
            mspf = r["ms_per_frame"]
            total = mspf * steps
            sp_self = base / mspf if base else float("nan")
            sp_seq = seq_ms / mspf if seq_ms else float("nan")
            print(f"{t:>7} {total:>10.2f} {mspf:>9.4f} {sp_self:>14.2f} {sp_seq:>16.2f}")


def table_gpu(cfg):
    hr("GPU  (best workgroup per grid, vs CPU sequential baseline, mean over 4 scenarios)")
    comp = cfg[cfg["sweep"] == "comparison"]
    seq = avg_over_scenarios(comp[comp["method_name"] == "cpu_sequential"],
                             THROUGHPUT_SCENARIOS, ["width"])
    seq_ms = dict(zip(seq["width"], seq["ms_per_frame"]))

    gw = cfg[cfg["sweep"] == "gpu_workgroup"].copy()
    a = avg_over_scenarios(gw, THROUGHPUT_SCENARIOS, ["method_name", "width", "wg_x", "wg_y"])
    for m in GPU_METHODS:
        print(f"\n-- {METHOD_PRETTY.get(m, m)} --")
        print(f"{'Grid':>10} {'Best WG':>9} {'ms/step':>9} {'Mcells/s':>10} "
              f"{'Speedup vs CPU seq':>20}")
        dm = a[a["method_name"] == m]
        for size in sorted(dm["width"].unique()):
            d = dm[dm["width"] == size]
            best = d.loc[d["ms_per_frame"].idxmin()]
            wg = f"{int(best['wg_x'])}x{int(best['wg_y'])}"
            sp = (seq_ms.get(size, float('nan')) / best["ms_per_frame"]) if size in seq_ms else float("nan")
            print(f"{size:>5}x{size:<4} {wg:>9} {best['ms_per_frame']:>9.4f} "
                  f"{best['mcells_per_s']:>10.1f} {sp:>20.1f}")


def table_comparison_summary(cfg):
    hr("METHOD COMPARISON  (ms/step, mean over 4 scenarios)")
    comp = cfg[cfg["sweep"] == "comparison"]
    a = avg_over_scenarios(comp, THROUGHPUT_SCENARIOS, ["method_name", "width"])
    sizes = sorted(a["width"].unique())
    header = f"{'Method':30} " + " ".join(f"{s:>9}" for s in sizes)
    print(header)
    for m in METHOD_ORDER:
        d = a[a["method_name"] == m]
        cells = []
        for s in sizes:
            v = d[d["width"] == s]["ms_per_frame"]
            cells.append(f"{v.values[0]:>9.3f}" if not v.empty else f"{'-':>9}")
        print(f"{METHOD_PRETTY.get(m, m):30} " + " ".join(cells))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default=None, help="benchmark CSV (default: newest in benchmarks/)")
    ap.add_argument("--outdir", default=None, help="where to write the figures")
    args = ap.parse_args()

    csv = find_csv(args.csv)
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = args.outdir or os.path.join(
        here, "report",
        "Concurrent_Falling_Sand_Game_Intermediate_Report_30_05___Rachid_Tazi___Ayoub_Agouzoul",
        "figures")
    os.makedirs(outdir, exist_ok=True)

    print(f"reading {csv}")
    df = pd.read_csv(csv)
    print(f"{len(df)} rows, sweeps: {sorted(df['sweep'].unique())}")
    cfg = median_per_config(df)

    # figures
    plot_comparison(cfg, outdir)
    plot_cpu_scaling(cfg, outdir)
    plot_gpu_workgroup(cfg, outdir)
    print(f"figures written to {outdir}")

    # tables for the report
    table_conservation(cfg)
    table_fairness(cfg)
    table_comparison_summary(cfg)
    table_cpu_scaling(cfg)
    table_gpu(cfg)


if __name__ == "__main__":
    main()
