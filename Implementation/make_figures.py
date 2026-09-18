#!/usr/bin/env python3
"""
Render the project's deliverable figures from the CSV files written by the
cli target.

Usage:
    python make_figures.py [csv_dir] [out_dir]

Defaults to the current directory for both. Requires matplotlib and pandas:
    pip install matplotlib pandas
"""

import os
import sys

import matplotlib
matplotlib.use("Agg")           # no display needed
import matplotlib.pyplot as plt
import pandas as pd


def _save(fig, out_dir, name):
    path = os.path.join(out_dir, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {path}")


def fig1_cost_curves(csv_dir, out_dir):
    """Cost curves and incremental cost, with the operating lambda marked."""
    df = pd.read_csv(os.path.join(csv_dir, "fig1_cost_curves.csv"))

    # The operating point comes from the dispatch file, if present.
    lam = None
    disp_path = os.path.join(csv_dir, "fig2_dispatch.csv")
    if os.path.exists(disp_path):
        d = pd.read_csv(disp_path)
        if not d.empty:
            lam = float(d["incremental_cost"].iloc[0])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    for g, grp in df.groupby("gen"):
        ax1.plot(grp["P_MW"], grp["cost"], label=f"G{int(g)+1}")
        ax2.plot(grp["P_MW"], grp["incremental_cost"], label=f"G{int(g)+1}")

    ax1.set_xlabel("output P [MW]")
    ax1.set_ylabel("fuel cost")
    ax1.set_title("Generator cost curves")
    ax1.legend()
    ax1.grid(alpha=0.3)

    ax2.set_xlabel("output P [MW]")
    ax2.set_ylabel("incremental cost  dC/dP")
    ax2.set_title("Incremental cost and the system $\\lambda$")
    if lam is not None:
        ax2.axhline(lam, color="crimson", linestyle="--",
                    label=f"$\\lambda$ = {lam:.4f}")
    ax2.legend()
    ax2.grid(alpha=0.3)

    _save(fig, out_dir, "fig1_cost_curves.png")


def fig2_dispatch(csv_dir, out_dir):
    """Bar chart of how load is split between generators."""
    df = pd.read_csv(os.path.join(csv_dir, "fig2_dispatch.csv"))

    names = [f"G{int(g)+1}" for g in df["gen"]]
    fig, ax = plt.subplots(figsize=(6.5, 4.2))

    ax.bar(names, df["Pmax"], color="#d5dcea", label="capacity $P_{max}$")
    bars = ax.bar(names, df["P_MW"], color="#4670be", width=0.55,
                  label="dispatched P")

    for b, p, amin, amax in zip(bars, df["P_MW"], df["at_min"], df["at_max"]):
        tag = f"{p:.1f}"
        if amin:
            tag += "\n(at min)"
        elif amax:
            tag += "\n(at max)"
        ax.text(b.get_x() + b.get_width() / 2, p, tag,
                ha="center", va="bottom", fontsize=9)

    ax.set_ylabel("power [MW]")
    ax.set_title("Optimal dispatch allocation")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)

    _save(fig, out_dir, "fig2_dispatch.png")


def fig3_lambda_vs_demand(csv_dir, out_dir):
    """System marginal price against total demand."""
    df = pd.read_csv(os.path.join(csv_dir, "fig3_lambda_vs_demand.csv"))
    df = df[df["feasible"] == 1]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    ax1.plot(df["total_demand_MW"], df["system_lambda"], marker="o", ms=3,
             color="#be4638")
    ax1.set_xlabel("total demand [MW]")
    ax1.set_ylabel("$\\lambda$  [\\$/MWh]")
    ax1.set_title("Marginal price vs demand")
    ax1.grid(alpha=0.3)

    pcols = [c for c in df.columns if c.startswith("P") and c[1:].isdigit()]
    for c in pcols:
        ax2.plot(df["total_demand_MW"], df[c], marker="o", ms=3,
                 label=f"G{int(c[1:])+1}")
    ax2.set_xlabel("total demand [MW]")
    ax2.set_ylabel("output [MW]")
    ax2.set_title("Dispatch vs demand")
    ax2.legend()
    ax2.grid(alpha=0.3)

    _save(fig, out_dir, "fig3_lambda_vs_demand.png")


def fig4_wind(csv_dir, out_dir):
    """Cost and price against wind penetration."""
    df = pd.read_csv(os.path.join(csv_dir, "fig4_wind_sweep.csv"))
    df = df[df["feasible"] == 1]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    ax1.plot(df["penetration_pct"], df["total_cost"], marker="o", ms=3,
             color="#3c785a")
    ax1.set_xlabel("wind penetration [% of demand]")
    ax1.set_ylabel("total fuel cost")
    ax1.set_title("Fuel cost falls as wind displaces thermal output")
    ax1.grid(alpha=0.3)

    ax2.plot(df["penetration_pct"], df["system_lambda"], marker="o", ms=3,
             color="#be4638")
    ax2.set_xlabel("wind penetration [% of demand]")
    ax2.set_ylabel("$\\lambda$  [\\$/MWh]")
    ax2.set_title("Marginal price vs wind penetration")
    ax2.grid(alpha=0.3)

    _save(fig, out_dir, "fig4_wind_sweep.png")


def fig5_losses(csv_dir, out_dir):
    """Transmission losses against wind penetration."""
    path = os.path.join(csv_dir, "fig5_wind_with_losses.csv")
    if not os.path.exists(path):
        print("  (fig5 csv not found, skipping)")
        return
    df = pd.read_csv(path)
    df = df[df["feasible"] == 1]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    ax1.plot(df["penetration_pct"], df["total_loss_MW"], marker="o", ms=3,
             color="#7a5aaa")
    ax1.set_xlabel("wind penetration [% of demand]")
    ax1.set_ylabel("transmission losses [MW]")
    ax1.set_title("Losses vs wind penetration")
    ax1.grid(alpha=0.3)

    ax2.plot(df["penetration_pct"], df["total_cost"], marker="o", ms=3,
             color="#3c785a")
    ax2.set_xlabel("wind penetration [% of demand]")
    ax2.set_ylabel("total fuel cost")
    ax2.set_title("Fuel cost with the loss model active")
    ax2.grid(alpha=0.3)

    _save(fig, out_dir, "fig5_wind_with_losses.png")


def main():
    csv_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    os.makedirs(out_dir, exist_ok=True)

    print(f"reading CSVs from {os.path.abspath(csv_dir)}")
    fig1_cost_curves(csv_dir, out_dir)
    fig2_dispatch(csv_dir, out_dir)
    fig3_lambda_vs_demand(csv_dir, out_dir)
    fig4_wind(csv_dir, out_dir)
    fig5_losses(csv_dir, out_dir)
    print("done.")


if __name__ == "__main__":
    main()
