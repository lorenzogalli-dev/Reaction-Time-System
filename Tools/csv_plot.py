#!/usr/bin/env python3
"""
Offline viewer for accelerometer CSV captures. Replaces
Arduino/HighFrequencySampleRate/CSV_Visualizer, which crashed outright on
anything but its own original column layout.

Handles both formats seen in this repo:
  - current (Tools/accel_live.py): commented metadata header, then
    t_s,t_us,x_g,y_g,z_g,host_iso
  - older (e.g. Data/blockstart_20260831_test0.csv): no comment header,
    timestamp_iso,elapsed_s,x_g,y_g,z_g

The time column is auto-detected (t_s if present, else elapsed_s), so both
plot correctly without needing to know which pipeline produced the file.

Usage:
    python3 Tools/csv_plot.py

Always opens a file-browser dialog to pick the CSV - no path to type or
remember on the command line.
"""

import os
import sys

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def pick_file():
    import tkinter as tk
    from tkinter import filedialog

    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return filedialog.askopenfilename(
        title="Select an accel_live CSV file",
        initialdir=os.path.join(repo_root, "Data"),
        filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
    )


def main(argv=None):
    file_path = pick_file()
    if not file_path:
        print("No file selected.")
        return 0

    print("Loading %s" % file_path)
    # comment="#" skips accel_live.py's metadata header lines; it's a no-op
    # on older files that don't have any.
    df = pd.read_csv(file_path, comment="#")
    df["mag_g"] = np.sqrt(df["x_g"] ** 2 + df["y_g"] ** 2 + df["z_g"] ** 2)

    if "t_s" in df.columns:
        time_col = "t_s"          # Tools/accel_live.py
    elif "elapsed_s" in df.columns:
        time_col = "elapsed_s"    # older kinestart/BLEtest.ino format
    else:
        raise SystemExit(
            "Unrecognized CSV: expected a 't_s' or 'elapsed_s' column, "
            "found: %s" % list(df.columns))

    print("\n--- first 5 rows ---")
    print(df.head())
    print("\n--- summary ---")
    print(df[["x_g", "y_g", "z_g", "mag_g"]].describe())

    t = df[time_col]
    file_name = os.path.basename(file_path)
    fig, (a1, a2, a3, a4) = plt.subplots(4, 1, figsize=(12, 11), sharex=True)
    fig.suptitle("%s (%d samples, %.2f s)" % (file_name, len(df), t.iloc[-1] - t.iloc[0]),
                  fontsize=13, fontweight="bold")

    for ax, col, color, label in (
        (a1, "x_g", "#e74c3c", "X"),
        (a2, "y_g", "#2ecc71", "Y"),
        (a3, "z_g", "#3498db", "Z"),
    ):
        ax.plot(t, df[col], color=color, lw=0.8, label=label)
        ax.set_ylabel("%s [g]" % label)
        ax.grid(True, ls="--", alpha=0.5)
        ax.legend(loc="upper right", fontsize=8)

    a4.plot(t, df["mag_g"], color="#8e44ad", lw=0.9, label="|a|")
    a4.axhline(df["mag_g"].max(), color="0.5", ls="--", lw=0.8,
               label="peak %.3f g" % df["mag_g"].max())
    a4.set_xlabel("Time since recording start [s] (board clock)")
    a4.set_ylabel("Magnitude [g]")
    a4.grid(True, ls="--", alpha=0.5)
    a4.legend(loc="upper right", fontsize=8)

    fig.tight_layout()
    plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
