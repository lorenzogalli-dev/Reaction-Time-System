#!/usr/bin/env python3
"""
sta_lta_start_detector.py
--------------------------
Detects movement onset ("the start") in accelerometer CSV data using STA/LTA.
Includes an interactive Graphical User Interface (GUI) and CLI mode.

Usage:
    python sta_lta_start_detector.py                     # Opens the GUI
    python sta_lta_start_detector.py data.csv            # Opens GUI with preloaded file
    python sta_lta_start_detector.py data.csv --cli      # Runs in CLI / terminal mode only
"""

import argparse
import sys
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def find_column(df, explicit, candidates):
    if explicit:
        if explicit not in df.columns:
            raise ValueError(f"Column '{explicit}' not found. Available: {list(df.columns)}")
        return explicit
    lower_map = {c.lower(): c for c in df.columns}
    for cand in candidates:
        if cand in lower_map:
            return lower_map[cand]
    raise ValueError(f"Could not auto-detect column among {candidates}. "
                     f"Available columns: {list(df.columns)}.")


def load_data(csv_path, no_header=False, t_col=None, x_col=None, y_col=None, z_col=None):
    # comment='#' automatically ignores header comment lines
    if no_header:
        df = pd.read_csv(csv_path, comment="#", header=None,
                         names=["t_us", "x_g", "y_g", "z_g"])
    else:
        df = pd.read_csv(csv_path, comment="#")

    t_c = find_column(df, t_col, ["t_us", "t", "time", "timestamp", "time_us"])
    x_c = find_column(df, x_col, ["x_g", "x", "accel_x", "ax"])
    y_c = find_column(df, y_col, ["y_g", "y", "accel_y", "ay"])
    z_c = find_column(df, z_col, ["z_g", "z", "accel_z", "az"])

    df = df[[t_c, x_c, y_c, z_c]].copy()
    df.columns = ["t_us", "x_g", "y_g", "z_g"]
    df = df.dropna().reset_index(drop=True)
    df["t_s"] = (df["t_us"] - df["t_us"].iloc[0]) / 1_000_000.0
    return df


def run_sta_lta(df, mean_tau=3.0, sta_tau=0.05, lta_tau=1.5,
                threshold_on=3.0, threshold_off=1.5,
                confirm_samples=3, lta_floor=1.0e-6, warmup_s=None):
    mag = np.sqrt(df["x_g"].values**2 + df["y_g"].values**2 + df["z_g"].values**2)
    t_s = df["t_s"].values
    n = len(mag)

    slow_mean = np.zeros(n)
    sta = np.zeros(n)
    lta = np.zeros(n)
    ratio = np.zeros(n)

    slow_mean[0] = mag[0]
    sta[0] = 0.0
    lta[0] = 0.0

    for i in range(1, n):
        dt = t_s[i] - t_s[i - 1]
        if dt <= 0:
            dt = 1e-6

        alpha_mean = 1.0 - np.exp(-dt / mean_tau)
        alpha_sta = 1.0 - np.exp(-dt / sta_tau)
        alpha_lta = 1.0 - np.exp(-dt / lta_tau)

        slow_mean[i] = slow_mean[i - 1] + alpha_mean * (mag[i] - slow_mean[i - 1])
        dev2 = (mag[i] - slow_mean[i]) ** 2
        sta[i] = sta[i - 1] + alpha_sta * (dev2 - sta[i - 1])
        lta[i] = lta[i - 1] + alpha_lta * (dev2 - lta[i - 1])
        ratio[i] = sta[i] / max(lta[i], lta_floor)

    total_dur = t_s[-1] if n > 0 else 0.0
    # Automatically adjust warmup if file duration is shorter than default 5 * LTA
    if warmup_s is None or warmup_s <= 0:
        nominal_warmup = 5.0 * lta_tau
        if total_dur > 0 and nominal_warmup >= total_dur:
            warmup_s = max(0.4, total_dur * 0.20)
        else:
            warmup_s = nominal_warmup

    triggers = []
    confirm_count = 0
    warmed_up = False
    armed = False
    for i in range(n):
        if not warmed_up:
            if t_s[i] >= warmup_s:
                warmed_up = True
                armed = True
            else:
                continue

        if armed:
            if ratio[i] > threshold_on:
                confirm_count += 1
            else:
                confirm_count = 0
            if confirm_count >= confirm_samples:
                onset_idx = i - confirm_samples + 1
                triggers.append(onset_idx)
                confirm_count = 0
                armed = False
        else:
            if ratio[i] < threshold_off:
                armed = True

    return mag, slow_mean, sta, lta, ratio, triggers, warmup_s


def run_gui(initial_csv=None):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

    class App:
        def __init__(self, root):
            self.root = root
            self.root.title("STA/LTA Movement Start Detector")
            self.root.geometry("1240x740")
            self.root.minsize(980, 600)

            self.df = None
            self.csv_path = tk.StringVar(value=initial_csv or "")

            # Filter & algorithm variables
            self.mean_tau_var = tk.StringVar(value="3.0")
            self.sta_tau_var = tk.StringVar(value="0.05")
            self.lta_tau_var = tk.StringVar(value="1.5")
            self.thresh_on_var = tk.StringVar(value="3.0")
            self.thresh_off_var = tk.StringVar(value="1.5")
            self.confirm_var = tk.StringVar(value="3")
            self.warmup_var = tk.StringVar(value="auto")
            self.go_t_us_var = tk.StringVar(value="")
            self.false_start_var = tk.StringVar(value="100")
            self.status_var = tk.StringVar(value="Select a CSV file to begin.")

            self._build_ui()
            if initial_csv and os.path.isfile(initial_csv):
                self.process_file()

        def _build_ui(self):
            paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
            paned.pack(fill=tk.BOTH, expand=True)

            # Left Sidebar
            left = ttk.Frame(paned, padding=8, width=380)
            paned.add(left, weight=0)

            # 1. File Input Box
            box_file = ttk.LabelFrame(left, text="Data File", padding=8)
            box_file.pack(fill=tk.X, pady=(0, 6))
            ttk.Entry(box_file, textvariable=self.csv_path).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
            ttk.Button(box_file, text="Browse...", command=self.browse_file).pack(side=tk.RIGHT)

            # 2. Parameters Box
            box_params = ttk.LabelFrame(left, text="Detector Parameters", padding=8)
            box_params.pack(fill=tk.X, pady=(0, 6))

            entries = [
                ("Mean Tau (s):", self.mean_tau_var),
                ("STA Tau (s):", self.sta_tau_var),
                ("LTA Tau (s):", self.lta_tau_var),
                ("Threshold ON:", self.thresh_on_var),
                ("Threshold OFF:", self.thresh_off_var),
                ("Confirm Samples:", self.confirm_var),
                ("Warmup (s) [auto]:", self.warmup_var),
                ("Go t_us (optional):", self.go_t_us_var),
                ("False Start (ms):", self.false_start_var),
            ]
            for row, (lbl, var) in enumerate(entries):
                ttk.Label(box_params, text=lbl).grid(row=row, column=0, sticky="w", pady=2)
                ttk.Entry(box_params, textvariable=var, width=14).grid(row=row, column=1, sticky="e", pady=2)

            btn_run = ttk.Button(left, text="▶ Process / Recalculate", command=self.process_file)
            btn_run.pack(fill=tk.X, pady=(0, 6))

            # 3. Results Box
            box_res = ttk.LabelFrame(left, text="Detected Starts", padding=8)
            box_res.pack(fill=tk.BOTH, expand=True)

            ttk.Label(box_res, textvariable=self.status_var, wraplength=350, font=("Helvetica", 9, "bold")).pack(anchor="w", pady=(0, 4))

            cols = ("num", "t_s", "mag", "ratio", "reaction")
            self.tree = ttk.Treeview(box_res, columns=cols, show="headings", height=8)
            self.tree.heading("num", text="#")
            self.tree.heading("t_s", text="t (s)")
            self.tree.heading("mag", text="|a| (g)")
            self.tree.heading("ratio", text="Ratio")
            self.tree.heading("reaction", text="Reaction / Status")

            self.tree.column("num", width=30, anchor="center")
            self.tree.column("t_s", width=65, anchor="center")
            self.tree.column("mag", width=65, anchor="center")
            self.tree.column("ratio", width=55, anchor="center")
            self.tree.column("reaction", width=125, anchor="center")

            scroll = ttk.Scrollbar(box_res, orient=tk.VERTICAL, command=self.tree.yview)
            self.tree.configure(yscrollcommand=scroll.set)
            scroll.pack(side=tk.RIGHT, fill=tk.Y)
            self.tree.pack(fill=tk.BOTH, expand=True)

            # Right Panel: Plot Canvas & Toolbar
            right = ttk.Frame(paned, padding=4)
            paned.add(right, weight=1)

            self.fig, self.axes = plt.subplots(2, 1, sharex=True, figsize=(8, 6))
            self.fig.tight_layout(pad=2.5)

            self.canvas = FigureCanvasTkAgg(self.fig, master=right)
            self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

            toolbar = NavigationToolbar2Tk(self.canvas, right)
            toolbar.update()

        def browse_file(self):
            path = filedialog.askopenfilename(filetypes=[("CSV Files", "*.csv"), ("All Files", "*.*")])
            if path:
                self.csv_path.set(path)
                self.process_file()

        def process_file(self):
            path = self.csv_path.get().strip()
            if not path or not os.path.isfile(path):
                messagebox.showwarning("Warning", "Please select a valid CSV file.")
                return

            try:
                mean_tau = float(self.mean_tau_var.get())
                sta_tau = float(self.sta_tau_var.get())
                lta_tau = float(self.lta_tau_var.get())
                th_on = float(self.thresh_on_var.get())
                th_off = float(self.thresh_off_var.get())
                conf_samples = int(self.confirm_var.get())
                warmup_val = self.warmup_var.get().strip().lower()
                warmup_s = None if warmup_val in ("auto", "", "none") else float(warmup_val)
                go_val = self.go_t_us_var.get().strip()
                go_t_us = int(go_val) if go_val else None
                false_start_us = float(self.false_start_var.get()) * 1000.0

                df = load_data(path)
                mag, slow_mean, sta, lta, ratio, triggers, warmup_used = run_sta_lta(
                    df, mean_tau=mean_tau, sta_tau=sta_tau, lta_tau=lta_tau,
                    threshold_on=th_on, threshold_off=th_off,
                    confirm_samples=conf_samples, warmup_s=warmup_s
                )
            except Exception as e:
                messagebox.showerror("Processing Error", str(e))
                return

            # Update Treeview results
            for row in self.tree.get_children():
                self.tree.delete(row)

            total_s = df["t_s"].iloc[-1]
            odr_hz = len(df) / total_s if total_s > 0 else 0
            self.status_var.set(f"Duration: {total_s:.2f}s | {len(df)} samples (~{odr_hz:.0f} Hz) | {len(triggers)} trigger(s)")

            for idx_num, tidx in enumerate(triggers, start=1):
                t_val = df["t_s"].iloc[tidx]
                t_us = df["t_us"].iloc[tidx]
                r_val = ratio[tidx]
                m_val = mag[tidx]

                reaction_str = "-"
                if go_t_us is not None:
                    diff_us = int(t_us) - int(go_t_us)
                    if diff_us < 0:
                        reaction_str = "Before Go"
                    elif diff_us < false_start_us:
                        reaction_str = f"False Start ({diff_us/1000.0:.1f} ms)"
                    else:
                        reaction_str = f"{diff_us/1000.0:.1f} ms"

                self.tree.insert("", tk.END, values=(idx_num, f"{t_val:.4f}", f"{m_val:.3f}", f"{r_val:.2f}", reaction_str))

            # Redraw subplots
            t_s = df["t_s"].values
            self.axes[0].clear()
            self.axes[1].clear()

            self.axes[0].plot(t_s, mag, label="|a| (g)", color="#1f77b4", linewidth=0.9)
            self.axes[0].plot(t_s, slow_mean, label="Slow Mean (Gravity/DC)", color="#2ca02c", linestyle="--", linewidth=0.9)
            for tidx in triggers:
                self.axes[0].axvline(t_s[tidx], color="red", linestyle="--", linewidth=1.1,
                                     label="Trigger" if tidx == triggers[0] else "")
            if go_t_us is not None:
                go_s = (go_t_us - df["t_us"].iloc[0]) / 1_000_000.0
                self.axes[0].axvline(go_s, color="green", linestyle="-", linewidth=1.3, label="Go Cue")

            self.axes[0].set_ylabel("Acceleration |a| (g)")
            self.axes[0].grid(True, alpha=0.3)
            self.axes[0].legend(loc="upper right")

            self.axes[1].plot(t_s, ratio, label="STA/LTA Ratio", color="#9467bd", linewidth=0.9)
            self.axes[1].axhline(th_on, color="red", linestyle="--", linewidth=1, label=f"Threshold ON ({th_on})")
            self.axes[1].axhline(th_off, color="orange", linestyle="--", linewidth=1, label=f"Threshold OFF ({th_off})")
            self.axes[1].axvspan(0, warmup_used, color="gray", alpha=0.2, label=f"Warmup ({warmup_used:.2f}s)")
            for tidx in triggers:
                self.axes[1].axvline(t_s[tidx], color="red", linestyle="--", linewidth=1.1)

            self.axes[1].set_ylabel("STA / LTA Ratio")
            self.axes[1].set_xlabel("Time (s)")
            self.axes[1].grid(True, alpha=0.3)
            self.axes[1].legend(loc="upper right")

            self.canvas.draw()

    root = tk.Tk()
    App(root)
    root.mainloop()


def run_cli(args):
    df = load_data(args.csv, no_header=args.no_header, t_col=args.t_col,
                   x_col=args.x_col, y_col=args.y_col, z_col=args.z_col)
    mag, slow_mean, sta, lta, ratio, triggers, warmup_used = run_sta_lta(
        df, args.mean_tau, args.sta_tau, args.lta_tau,
        args.threshold_on, args.threshold_off, args.confirm_samples, args.lta_floor,
        warmup_s=args.warmup_s
    )

    if not triggers:
        print(f"No trigger found (Warmup: {warmup_used:.2f}s). Try lowering --threshold-on.")
        return

    print(f"\n{len(triggers)} trigger(s) found (Warmup: {warmup_used:.2f}s):\n")
    for k, idx in enumerate(triggers, start=1):
        t_us = df["t_us"].iloc[idx]
        t_s = df["t_s"].iloc[idx]
        print(f"  #{k}: sample {idx} | t_us={t_us} | t={t_s:.4f}s | |a|={mag[idx]:.4f}g | ratio={ratio[idx]:.2f}")

        if args.go_t_us is not None:
            reaction_us = int(t_us) - int(args.go_t_us)
            if reaction_us < 0:
                print("       (before go_t_us)")
            elif reaction_us < args.false_start_us:
                print(f"       -> FALSE START ({reaction_us/1000.0:.1f} ms)")
            else:
                print(f"       -> Reaction Time: {reaction_us/1000.0:.1f} ms")
    print()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("csv", nargs="?", default=None, help="Path to accelerometer CSV file")
    p.add_argument("--cli", action="store_true", help="Run in terminal mode without opening GUI")
    p.add_argument("--no-header", action="store_true", help="CSV has no header (assumes: t_us, x_g, y_g, z_g)")
    p.add_argument("--t-col", default=None)
    p.add_argument("--x-col", default=None)
    p.add_argument("--y-col", default=None)
    p.add_argument("--z-col", default=None)
    p.add_argument("--mean-tau", type=float, default=3.0)
    p.add_argument("--sta-tau", type=float, default=0.05)
    p.add_argument("--lta-tau", type=float, default=1.5)
    p.add_argument("--threshold-on", type=float, default=3.0)
    p.add_argument("--threshold-off", type=float, default=1.5)
    p.add_argument("--confirm-samples", type=int, default=3)
    p.add_argument("--lta-floor", type=float, default=1.0e-6)
    p.add_argument("--warmup-s", type=float, default=None)
    p.add_argument("--go-t-us", type=int, default=None)
    p.add_argument("--false-start-us", type=int, default=100_000)

    args = p.parse_args()

    if args.cli:
        if not args.csv:
            sys.exit("Error: --cli mode requires a CSV file path.")
        run_cli(args)
    else:
        run_gui(initial_csv=args.csv)


if __name__ == "__main__":
    main()