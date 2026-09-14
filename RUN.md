# RUN.md — what each file is and how to run it

Exact versions and setup: [BUILD.md](./BUILD.md). Why the algorithm works the
way it does: [INFO.md](./INFO.md).

Everything lives in `Arduino/AlgorithmRealTime/`. The firmware **decides in
real time**; the Python **tunes and explains**. They are two implementations of
one algorithm and they must stay aligned: change a constant or a rule on one
side and it has to be carried to the other — nothing notices on its own.

---

## The firmware — runs on the board

| file | what it does |
|---|---|
| `AlgorithmRealTime.ino` | the start sequence (3 randomised beeps), 833 Hz sampling, serial dump |
| `StartDetector.h` | the **causal** stage: STA/LTA on the horizontal, sample by sample, from `set` onwards |
| `AicPicker.h` | the **non-causal** stage: AIC refines the instant, then the verdict rule. Runs 1 s after `go` |

`Python_Tools/` sits inside the sketch folder on purpose — it is the same
algorithm in the other language and the two must not drift apart — but the IDE
and `arduino-cli` ignore any subfolder that is not `src/`, so it never reaches
the firmware.

The verdict arrives ~1 s after `go`, but **the instant** is measured in real
time: the delay is only in telling you.

**Compile** (`arduino-cli` ships inside the IDE, nothing to install):

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" \
  compile --fqbn Seeeduino:mbed:xiaonRF52840Sense \
  --libraries Arduino/libraries Arduino/AlgorithmRealTime
```

Expect flash 13%, RAM 72%. **Always report the RAM figure** after a change.
To flash: open the `.ino` in the IDE, board **XIAO nRF52840 Sense (No Updates)**
— it must be the **mbed** core, not nrf52, which silently degrades `micros()`
to ~977 µs.

---

## The Python — `Python_Tools/`, runs on your computer

Only **one process** can hold a serial port: close the Serial Monitor before
running `capture.py` or `verify_rate.py`.

### `verify_rate.py` — the first thing to run after flashing

```bash
python3 Arduino/AlgorithmRealTime/Python_Tools/verify_rate.py --seconds 5
```

Board sitting still on the desk. It must print **PASS**. It checks clock
resolution, effective sample rate, dropped samples, gaps and clipping. If this
fails, nothing else means anything.

### `capture.py` — record the starts

```bash
python3 Arduino/AlgorithmRealTime/Python_Tools/capture.py              # port autodetected (macOS, Linux, Windows)
python3 Arduino/AlgorithmRealTime/Python_Tools/capture.py --port /dev/cu.usbmodemXXXX --outdir Data
```

It listens and writes every dump to `Data/accel_<timestamp>.csv`. It decides
nothing, deliberately. Press the board's button — or Enter here, which sends
`b` (`a` aborts, `p` prints one reading).

What to watch while it runs:

- `SEQ,armed` — the button was pressed; the sequence is starting.
- `SEQ,gate,<ms>,<mg>,<how>` — **the detector armed**: how many ms after `set`,
  at how much movement, and whether `still` (measured) or `cap` (gave up
  waiting). `go` is scheduled from this instant. Held still you should see
  ~1000 ms, which is the floor `MIN_BLANK` 800 + `QUIET_HOLD` 200; move and it
  rises.
- the `>> ON-DEVICE DETECTION RESULT <<` block: one verdict, nothing else.

The verdict and the arming instant also go **into the CSV**.

### `start_detector.py` — analyse and tune

```bash
python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py                    # GUI, pick a file
python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py Data/accel_X.csv   # GUI, preloaded
python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py "Data/block_starts_110926/*.csv" --cli
```

The same algorithm, offline, with every threshold in the left-hand panel. This
is where you **tune**, and where you can see *why* a verdict came out the way it
did — the board only gives you the answer. A parameter changed here has to be
copied into the headers by hand.

---

## The normal sequence, start to finish

```
1. compile and flash                        -> RAM 72%
2. verify_rate.py --seconds 5               -> PASS
3. capture.py, then the button, then run    -> Data/accel_*.csv
4. start_detector.py Data/accel_*.csv       -> look at what it saw
```

---

## Before this goes to the track

`DET_SETTLED_MG` **is measured**: an athlete holding the set position reads
3.1 mg median and 7.0 mg at p95 through the block, against a push-off of
1000-3600 mg. 15 mg is about 2x the p95.

A capture now carries **the arming instant and the board's own verdict**
(`arm_t_s`, `board_verdict`, `board_reaction_ms`). `start_detector.py` reads the
arming instant rather than re-deriving it, because that decision was already
taken by the board — recomputing it would be second-guessing what actually
happened. Pass `force_rearm=True` to re-derive it, which is what tuning needs.
