#!/usr/bin/env python3
"""
Interfaccia live per il firmware Prostart/BlockStart su XIAO nRF52840 Sense.

Mostra l'accelerometro in tempo reale, registra su CSV ed esporta i grafici.

IL TEMPO
--------
L'unico orologio della misura e' micros() del nRF52840. Il firmware emette
righe "elapsed_s,x_g,y_g,z_g" dove elapsed_s viene dai timestamp della FIFO
hardware agganciati dal PLL di bordo: e' il tempo di *cattura* del campione,
non di arrivo. Questo script non timestampa nulla ai fini della misura - si
limita a trascrivere il tempo del dispositivo.

La colonna host_iso nel CSV e' l'ora di arrivo sul computer ed e' li' solo per
incrociare una cattura con altri log. Non va usata per misurare: il buffering
di USB e sistema operativo la sposta di decine di ms in modo variabile.

USO
---
    python3 tools/kinestart_live.py                 # trova la porta da solo
    python3 tools/kinestart_live.py --port /dev/cu.usbmodem1101
    python3 tools/kinestart_live.py --simulate      # senza hardware, per provare la UI

Comandi: pulsanti in basso, oppure i tasti r (registra), s (stop), p (snapshot).

Richiede: pyserial, matplotlib, numpy.
"""

import argparse
import collections
import glob
import math
import os
import sys
import threading
import time
from datetime import datetime, timezone

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

# Il firmware apre lo stream su 'r' e lo chiude su 's'.
CMD_START = b"r"
CMD_STOP = b"s"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUTDIR = os.path.join(REPO_ROOT, "data")


# ---------------------------------------------------------------------------
# Sorgenti dati
# ---------------------------------------------------------------------------

class SerialSource:
    """Legge la seriale a blocchi e restituisce righe di testo complete.

    A blocchi e non con readline(): a 833 Hz arrivano ~833 righe al secondo, e
    una readline() per riga mentre il thread grafico tiene il GIL lascia
    riempire il buffer del sistema operativo. Leggere tutto quello che c'e' e
    spezzarlo in memoria toglie il problema alla radice.
    """

    def __init__(self, port, baud):
        import serial  # importato qui: con --simulate pyserial non serve
        self.port = port
        self._ser = serial.Serial(port, baud, timeout=0.05)
        self._tail = ""
        self._ser.reset_input_buffer()
        self._ser.write(CMD_START)

    def read_lines(self):
        n = self._ser.in_waiting
        chunk = self._ser.read(n if n else 1)
        if not chunk:
            return []
        text = self._tail + chunk.decode("utf-8", errors="ignore")
        parts = text.split("\n")
        self._tail = parts.pop()  # ultima riga: probabilmente incompleta
        return parts

    def close(self):
        try:
            self._ser.write(CMD_STOP)
            self._ser.flush()
        except Exception:
            pass
        try:
            self._ser.close()
        except Exception:
            pass


class SimulatedSource:
    """Genera lo stesso testo del firmware, per provare la UI senza board.

    Serve a verificare la catena completa - parsing, grafico, CSV, export -
    quando la board non e' collegata. I dati sono finti; il formato no.
    """

    def __init__(self, odr_hz, accel_range_g):
        self.port = "simulato"
        self._odr = odr_hz
        self._range = accel_range_g
        self._n = 0
        self._t0 = time.monotonic()
        self._emitted = 0
        self._banner = [
            "IMU OK - accel %d Hz, +/-%d g, FIFO continuous" % (odr_hz, accel_range_g),
            "Ready. Serial: 'g' go, 'd' dump, 't' timebase, 'p' latest, 'r'/'s' cattura CSV.",
        ]

    def read_lines(self):
        if self._banner:
            return [self._banner.pop(0)]
        time.sleep(0.02)
        due = int((time.monotonic() - self._t0) * self._odr)
        out = []
        while self._emitted < due:
            k = self._emitted
            t = k / self._odr
            # Fondo: gravita' su Z piu' rumore. Ogni 5 s un transitorio che
            # somiglia a una partenza, cosi' il picco e il grafico si vedono.
            phase = t % 5.0
            burst = math.exp(-((phase - 2.0) ** 2) / 0.002) * 3.0 if phase < 3 else 0.0
            ax = 0.01 * math.sin(2 * math.pi * 3 * t) + np.random.normal(0, 0.004)
            ay = 0.01 * math.cos(2 * math.pi * 2 * t) + np.random.normal(0, 0.004)
            az = 1.0 + burst + np.random.normal(0, 0.004)
            lim = float(self._range)
            ax, ay, az = (max(-lim, min(lim, v)) for v in (ax, ay, az))
            out.append("%.6f,%.4f,%.4f,%.4f" % (t, ax, ay, az))
            self._emitted += 1
        return out

    def close(self):
        pass


# ---------------------------------------------------------------------------
# Applicazione
# ---------------------------------------------------------------------------

class LiveApp:
    def __init__(self, args):
        self.args = args
        self.odr = args.odr
        self.accel_range = args.range
        self.window_s = args.window

        maxlen = int(self.window_s * self.odr)
        self.t_buf = collections.deque(maxlen=maxlen)
        self.x_buf = collections.deque(maxlen=maxlen)
        self.y_buf = collections.deque(maxlen=maxlen)
        self.z_buf = collections.deque(maxlen=maxlen)
        self.m_buf = collections.deque(maxlen=maxlen)

        self.lock = threading.Lock()
        self.running = True
        self.source = None
        self.status = "avvio..."
        self.last_message = ""

        # --- stato registrazione ---
        self.recording = False
        self.csv_file = None
        self.csv_path = None
        self.rec_t0 = None            # elapsed del dispositivo all'inizio della registrazione
        self.rec_rows = 0
        self.rec_peak = 0.0
        self.rec_clipped = 0
        self.rec_gaps = 0
        self.rec_data = []            # per il PNG di fine registrazione

        # --- statistiche live ---
        self.total_samples = 0
        self.host_t0 = time.monotonic()
        self.last_elapsed = None
        self.gap_count = 0
        self.fw_dropped = None

        self._build_ui()

    # -- sorgente -----------------------------------------------------------

    def connect(self):
        try:
            if self.args.simulate:
                self.source = SimulatedSource(self.odr, self.accel_range)
            else:
                port = self.args.port or autodetect_port()
                if port is None:
                    self.status = "nessuna porta trovata - collega la board e riavvia"
                    return
                self.source = SerialSource(port, self.args.baud)
            self.status = "connesso a %s" % self.source.port
        except Exception as exc:
            self.status = "connessione fallita: %s" % exc
            self.source = None
            return
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while self.running and self.source is not None:
            try:
                lines = self.source.read_lines()
            except Exception as exc:
                self.status = "errore di lettura: %s" % exc
                return
            for line in lines:
                self._handle_line(line.strip())

    def _handle_line(self, line):
        if not line:
            return
        parts = line.split(",")
        if len(parts) != 4:
            self._handle_message(line)
            return
        try:
            elapsed = float(parts[0])
            ax, ay, az = float(parts[1]), float(parts[2]), float(parts[3])
        except ValueError:
            self._handle_message(line)
            return

        mag = math.sqrt(ax * ax + ay * ay + az * az)
        host_iso = datetime.now(timezone.utc).isoformat()

        # Un salto molto piu' largo del periodo nominale significa campioni
        # persi: il firmware li dichiara, ma cosi' si vedono anche qui.
        if self.last_elapsed is not None:
            if elapsed - self.last_elapsed > 3.0 / self.odr:
                self.gap_count += 1
                if self.recording:
                    self.rec_gaps += 1
        self.last_elapsed = elapsed
        self.total_samples += 1

        with self.lock:
            self.t_buf.append(elapsed)
            self.x_buf.append(ax)
            self.y_buf.append(ay)
            self.z_buf.append(az)
            self.m_buf.append(mag)

            if self.recording and self.csv_file is not None:
                if self.rec_t0 is None:
                    self.rec_t0 = elapsed
                t_rel = elapsed - self.rec_t0
                self.csv_file.write("%.6f,%.6f,%.4f,%.4f,%.4f,%s\n"
                                    % (t_rel, elapsed, ax, ay, az, host_iso))
                self.rec_rows += 1
                self.rec_data.append((t_rel, ax, ay, az, mag))
                if mag > self.rec_peak:
                    self.rec_peak = mag
                # Clipping: il fronte di salita e' proprio dove si legge
                # l'onset, saturare li' rovina la misura senza farsi notare.
                if max(abs(ax), abs(ay), abs(az)) >= 0.95 * self.accel_range:
                    self.rec_clipped += 1

    def _handle_message(self, line):
        """Righe di testo del firmware: banner, esiti, diagnostica."""
        self.last_message = line
        if "+/-" in line and " g," in line:
            # "IMU OK - accel 833 Hz, +/-4 g, FIFO continuous"
            try:
                token = line.split("+/-")[1].split("g")[0].strip()
                self.accel_range = int(float(token))
            except (IndexError, ValueError):
                pass
        if "dropped" in line:
            try:
                self.fw_dropped = int(line.split(":")[1].strip().split()[0])
            except (IndexError, ValueError):
                pass

    # -- registrazione ------------------------------------------------------

    def start_recording(self, _event=None):
        if self.recording:
            return
        os.makedirs(self.args.outdir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.csv_path = os.path.join(self.args.outdir, "kinestart_%s.csv" % stamp)
        f = open(self.csv_path, "w", encoding="utf-8")
        f.write("# kinestart capture\n")
        f.write("# started_utc: %s\n" % datetime.now(timezone.utc).isoformat())
        f.write("# source: %s\n" % (self.source.port if self.source else "?"))
        f.write("# odr_hz_nominal: %d\n" % self.odr)
        f.write("# accel_range_g: %d\n" % self.accel_range)
        f.write("# t_s e device_elapsed_s vengono dal clock micros() della board\n")
        f.write("# (FIFO hardware + PLL di bordo). host_iso e' l'ora di arrivo\n")
        f.write("# sul computer: solo per riferimento incrociato, non per misurare.\n")
        f.write("t_s,device_elapsed_s,x_g,y_g,z_g,host_iso\n")

        with self.lock:
            self.csv_file = f
            self.rec_t0 = None
            self.rec_rows = 0
            self.rec_peak = 0.0
            self.rec_clipped = 0
            self.rec_gaps = 0
            self.rec_data = []
            self.recording = True
        self.status = "registrazione -> %s" % os.path.basename(self.csv_path)

    def stop_recording(self, _event=None):
        if not self.recording:
            return
        with self.lock:
            self.recording = False
            f, self.csv_file = self.csv_file, None
            rows, path = self.rec_rows, self.csv_path
            data = list(self.rec_data)
        if f is not None:
            f.close()

        png = None
        if data:
            png = os.path.splitext(path)[0] + ".png"
            self._save_overview(data, png)

        self.status = "salvate %d righe in %s%s" % (
            rows, os.path.basename(path),
            " (+ %s)" % os.path.basename(png) if png else "")

    def _save_overview(self, data, png_path):
        """PNG dell'intera registrazione, non solo della finestra a schermo."""
        arr = np.asarray(data, dtype=float)
        t, ax_, ay_, az_, mag = arr[:, 0], arr[:, 1], arr[:, 2], arr[:, 3], arr[:, 4]

        fig, (a1, a2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
        fig.suptitle("kinestart - %s (%d campioni, %.2f s)"
                     % (os.path.basename(png_path), len(t), t[-1] - t[0]))
        a1.plot(t, ax_, "r-", lw=0.7, label="X")
        a1.plot(t, ay_, "g-", lw=0.7, label="Y")
        a1.plot(t, az_, "b-", lw=0.7, label="Z")
        a1.axhline(self.accel_range, color="k", ls=":", lw=0.8)
        a1.axhline(-self.accel_range, color="k", ls=":", lw=0.8)
        a1.set_ylabel("Accel [g]")
        a1.grid(True, ls="--", alpha=0.5)
        a1.legend(loc="upper right", fontsize=8)

        a2.plot(t, mag, "m-", lw=0.9, label="|a|")
        a2.axhline(mag.max(), color="0.5", ls="--", lw=0.8,
                   label="picco %.3f g" % mag.max())
        a2.set_xlabel("Tempo dall'inizio registrazione [s]  (clock della board)")
        a2.set_ylabel("Magnitudine [g]")
        a2.grid(True, ls="--", alpha=0.5)
        a2.legend(loc="upper right", fontsize=8)

        fig.tight_layout()
        fig.savefig(png_path, dpi=140)
        plt.close(fig)

    def snapshot(self, _event=None):
        os.makedirs(self.args.outdir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(self.args.outdir, "kinestart_snapshot_%s.png" % stamp)
        self.fig.savefig(path, dpi=140)
        self.status = "snapshot salvato in %s" % os.path.basename(path)

    # -- interfaccia --------------------------------------------------------

    def _build_ui(self):
        self.fig, (self.ax_accel, self.ax_mag) = plt.subplots(
            2, 1, figsize=(12, 7.5), sharex=True)
        self.fig.canvas.manager.set_window_title("kinestart - live IMU")
        self.fig.subplots_adjust(bottom=0.17, top=0.86, hspace=0.12)

        (self.l_x,) = self.ax_accel.plot([], [], "r-", lw=0.8, label="X")
        (self.l_y,) = self.ax_accel.plot([], [], "g-", lw=0.8, label="Y")
        (self.l_z,) = self.ax_accel.plot([], [], "b-", lw=0.8, label="Z")
        self.ax_accel.set_ylabel("Accel [g]")
        self.ax_accel.grid(True, ls="--", alpha=0.5)
        self.ax_accel.legend(loc="upper left", fontsize=8, ncol=3)

        (self.l_m,) = self.ax_mag.plot([], [], "m-", lw=1.0, label="|a|")
        self.ax_mag.set_xlabel("Tempo dal clock della board [s]")
        self.ax_mag.set_ylabel("Magnitudine [g]")
        self.ax_mag.grid(True, ls="--", alpha=0.5)

        # Due blocchi separati: lo stato cambia di rado, le statistiche a ogni
        # ridisegno. Il secondo puo' occupare due righe, da cui la distanza.
        self.txt_status = self.fig.text(0.012, 0.965, "", fontsize=9.5,
                                        family="monospace", weight="bold")
        self.txt_stats = self.fig.text(0.012, 0.925, "", fontsize=9,
                                       family="monospace", va="top")

        # I riferimenti ai Button vanno tenuti vivi, altrimenti il garbage
        # collector li porta via e i pulsanti smettono di rispondere.
        self.buttons = []
        specs = [
            ("Registra (r)", self.start_recording),
            ("Stop (s)", self.stop_recording),
            ("Snapshot PNG (p)", self.snapshot),
        ]
        for i, (label, cb) in enumerate(specs):
            axb = self.fig.add_axes([0.012 + i * 0.155, 0.03, 0.145, 0.055])
            b = Button(axb, label)
            b.on_clicked(cb)
            self.buttons.append(b)

        self.fig.canvas.mpl_connect("key_press_event", self._on_key)
        self.fig.canvas.mpl_connect("close_event", lambda _e: self.shutdown())

    def _on_key(self, event):
        if event.key == "r":
            self.start_recording()
        elif event.key == "s":
            self.stop_recording()
        elif event.key == "p":
            self.snapshot()

    def _refresh(self):
        with self.lock:
            if not self.t_buf:
                return
            step = self.args.plot_decimation
            t = list(self.t_buf)[::step]
            x = list(self.x_buf)[::step]
            y = list(self.y_buf)[::step]
            z = list(self.z_buf)[::step]
            m = list(self.m_buf)[::step]
            t_all = (self.t_buf[0], self.t_buf[-1])
            n_win = len(self.t_buf)
            recording = self.recording
            rec_rows, rec_peak = self.rec_rows, self.rec_peak
            rec_clipped, rec_gaps = self.rec_clipped, self.rec_gaps

        self.l_x.set_data(t, x)
        self.l_y.set_data(t, y)
        self.l_z.set_data(t, z)
        self.l_m.set_data(t, m)

        t_end = t_all[1]
        self.ax_accel.set_xlim(max(0.0, t_end - self.window_s), t_end + 0.05)
        if x:
            lo = min(min(x), min(y), min(z))
            hi = max(max(x), max(y), max(z))
            pad = max(0.1, 0.1 * (hi - lo))
            self.ax_accel.set_ylim(lo - pad, hi + pad)
            self.ax_mag.set_ylim(min(m) - 0.1, max(m) + 0.2)

        # Frequenza *misurata* sul clock della board contro quella osservata
        # dall'host: se la seconda e' piu' bassa il collo di bottiglia e' qui,
        # non sulla board.
        span = t_all[1] - t_all[0]
        dev_rate = (n_win - 1) / span if span > 0 else 0.0
        host_rate = self.total_samples / max(1e-6, time.monotonic() - self.host_t0)

        state = "REC" if recording else "---"
        self.txt_status.set_text("[%s] %s" % (state, self.status))
        stats = ("campioni %-9d  rate board %7.1f Hz  rate host %7.1f Hz  "
                 "gap %-4d  fondo scala +/-%d g"
                 % (self.total_samples, dev_rate, host_rate, self.gap_count,
                    self.accel_range))
        if recording:
            stats += "\n              righe %-9d  picco %6.3f g  clip %-5d  gap %d" % (
                rec_rows, rec_peak, rec_clipped, rec_gaps)
        if rec_clipped:
            stats += "   <<< SATURAZIONE: alza il fondo scala"
        self.txt_stats.set_text(stats)

    def run(self):
        self.connect()
        timer = self.fig.canvas.new_timer(interval=self.args.refresh_ms)
        timer.add_callback(self._refresh)
        timer.start()
        plt.show()

    def shutdown(self):
        self.running = False
        if self.recording:
            self.stop_recording()
        if self.source is not None:
            self.source.close()
            self.source = None


def autodetect_port():
    """Prima la porta segnalata da pyserial, poi i nomi tipici di macOS."""
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            name = (p.device or "")
            if "usbmodem" in name or "usbserial" in name or "ACM" in name:
                return name
    except Exception:
        pass
    for pattern in ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="porta seriale (default: rilevata da sola)")
    ap.add_argument("--baud", type=int, default=921600, help="deve combaciare col firmware")
    ap.add_argument("--odr", type=int, default=833, help="ODR nominale dell'accelerometro")
    ap.add_argument("--range", type=int, default=4,
                    help="fondo scala in g; aggiornato da solo se la board lo annuncia")
    ap.add_argument("--window", type=float, default=3.0, help="secondi visibili a schermo")
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR, help="dove finiscono CSV e PNG")
    ap.add_argument("--refresh-ms", type=int, default=200, help="periodo di ridisegno")
    ap.add_argument("--plot-decimation", type=int, default=4,
                    help="1 punto ogni N nel grafico; il CSV resta a piena frequenza")
    ap.add_argument("--simulate", action="store_true",
                    help="dati finti, per provare l'interfaccia senza board")
    args = ap.parse_args(argv)

    app = LiveApp(args)
    try:
        app.run()
    finally:
        app.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
