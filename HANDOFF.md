# HANDOFF — Prostart live IMU data view & sensor evaluation

Last updated: 2026-09-08 (evening). Written for an agent starting with no prior context.
Sections are newest first.

## READ THIS FIRST — 2026-09-08 (evening): firmware v4, the board now runs the start

The single most important change in the project so far, and it is not about
precision. Through v3 the `go` reference was **a human pressing a key**, relayed
over serial. That made the reference worse than the thing being measured: on
`Data/data_before_080926/accel_20260907_160108.csv` the `go` marker lands **~90 ms
after** the
movement it was supposed to mark. Tuning a ±10 ms detection threshold against a
±100 ms reference is circular, and that is why "the detector is not tuned" had
been the standing open item for two sessions.

v4 removes the human from the timing path. A button starts a real, randomised
start sequence; the firmware sounds the three beeps itself and timestamps each
one with the same `micros()` every accelerometer sample uses. No cross-clock
sync, no keypress jitter, no host latency. **The go marker's error goes from
±100 ms to the buzzer's own acoustic latency: 5-20 ms, systematic and constant,
therefore calibrated once and subtracted rather than fought on every run.**

The consequence for planning: the LED + 240 fps video jig the previous session
called for is **no longer a prerequisite for tuning**. It is now only a way to
measure that one buzzer constant.

### What v4 does

```
button ──rand 2-3 s──> BEEP "on your marks"
       ──rand 20-25 s──> BEEP "set"      <- the judged window opens
       ──rand 2.2-3 s─> BEEP "go"
       ──1 s──> stop, dump the window over serial
```

All three delays are randomised, and `random()` is seeded from `micros()` at
the instant the button was pressed. This is not decoration: an **unseeded**
`random()` on Arduino replays the identical sequence after every reset, so by
the third attempt an athlete would know when `go` is coming — which would
invalidate precisely the measurement the system exists to make.

The state machine is entirely non-blocking; every wait is a `micros()` deadline
checked from `loop()`. **Nothing in it may ever call `delay()`.** Sampling is
paced by the IMU's data-ready interrupt on a *level-latched* line, so a loop
that blocks past one sample period does not merely stutter — the missed edge is
permanent until the watchdog recovers it (see `DRDY_STALL_TIMEOUT_US`).

Deadline comparisons use a signed difference (`(int32_t)(micros() - deadline)`)
so they stay correct across the ~71 minute `micros()` wrap.

### The pre-roll — the design point most likely to be broken by a "simplification"

The recording window is **`[set − 3 s, go + 1 s]`**, not `[set, go + 1 s]`.

The three extra seconds are not padding. The detector's LTA has an 800 ms time
constant, and the gravity estimate and baseline need settled data too. A capture
starting exactly at `set` would leave the detector **blind for the first 800 ms
of the window in which a false start can actually happen** — the worst
possible place for it to be blind.

The mechanism: the sample buffer is now a **ring that is always filling**, in
every state, with no arm/start step at all. `set` merely records a position in
data that already exists (`setSampleIdx = recWritten`), and the dump reaches
`PREROLL_SAMPLES` back behind it. The ring holds ~13.9 s against a ~5 s window,
so the 20-25 s "on your marks" pause overwriting it several times over is
harmless and expected.

`dumpRecording()` clamps the window to what the ring still physically holds and
reports `TRUNCATED,1` if it had to. Without that clamp, a request reaching
further back than the ring would read slots newer samples have already
overwritten and emit them as if they were old — a silently wrong capture, which
is exactly the class of bug that cost this project a day (see the morning
section below).

### Serial protocol, replacing the old `o`/`s`/`g`/`S`

The marker keys are **gone**. There is nothing to arm and nothing to stop.

| | |
|---|---|
| `b` | same as pressing the button — walks the whole sequence with no hardware wired |
| `a` | abort a running sequence (a second button press does the same) |
| `d` | dump the ring as it stands, no sequence, no markers — this is what `verify_rate.py` uses |
| `p` | one immediate reading |

Dump framing gained three lines: `PREROLL,<n>` (samples actually present before
`set`), `TRUNCATED,<0|1>`, alongside the existing `ON`/`SET`/`GO`/`DROPPED`/
`CLOCKSTEP`. The idle preview is suppressed while a sequence runs, so nothing
can interleave with a run or with the dump that follows it.

### Wiring — required reading before anything is soldered

| Part | Wiring |
|---|---|
| **Active** buzzer (has its own oscillator) | `+` → **D1**, `−` → **GND** |
| Momentary button | one leg → **D0**, the **diagonally opposite** leg → **GND** |

No resistors: `D0` is `INPUT_PULLUP` and the button pulls it to ground. On a
4-pin tactile button the pins are paired internally, so two legs on the *same*
side are a permanently closed circuit — take them diagonally opposite. D0/D1 are
clear of the IMU's I2C and of the UART on D6/D7.

**The buzzer must be an active one.** A passive buzzer needs a driven waveform
(`tone()`/PWM), which puts an unmeasured delay on the very instant that defines
the reaction time's zero — and `tone()`'s behaviour on the mbed core is one more
thing that would need verifying.

**Everything works with neither part attached.** Send `b` instead of pressing
the button; `D0` reads a stable HIGH through its pull-up and never triggers
spuriously. What you lose without a buzzer is only the audible `go`, so the
pipeline can be validated but a real reaction time cannot be taken. Do not touch
the board during a run — the detector will fire on it.

### The firmware compiles, and `arduino-cli` exists

`HANDOFF.md` had long said "no `arduino-cli` on this machine", which is why no
sketch had ever been compiled by an agent. **It is false.** The Arduino IDE
bundles one:

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" \
  compile --fqbn Seeeduino:mbed:xiaonRF52840Sense \
  --libraries Arduino/libraries Arduino/AccelStream
```

v4 result: flash 12%, **RAM 69% (166 KB used, 71.5 KB free)** — the ring buffer
is most of it. Compile before handing over any firmware change, and report the
RAM figure; a future addition that pushes this over will fail at link time
rather than mysteriously at runtime, but only if someone looks.

**The button and buzzer code has never seen hardware.** Compilation is not
validation. First thing on the bench: send `b` and check `SEQ,armed` →
`SEQ,marks` → `SEQ,set` → `SEQ,go` → dump.

---

## Tooling: five Python scripts became three

The `Tools/` directory had two overlapping detectors and two overlapping
viewers, and it was no longer obvious which was current. It is now:

| File | Role |
|---|---|
| `capture.py` | **new** — writes the board's dumps to CSV. Nothing else. |
| `start_detector.py` | **new** — onset + false-start detection, GUI and CLI |
| `verify_rate.py` | unchanged in purpose; updated to the `d` command |

Deleted: `accel_live.py`, `detect_pushoff.py`, `sta_lta_start_detector.py`,
`csv_plot.py`. Also deleted: `Arduino/Reaction_HardwareTest/` (ST7735 + buzzer +
XBee on `Serial1` — hardware from an architecture that is no longer the plan),
and a **127 MB untracked duplicate `prostart/` in the repo root**, which held
170 files of pure build output and IDE state, an empty `lib/`, and no
`pubspec.yaml`. The real app is and always was `Flutter App/prostart/`.

### Why `capture.py` is deliberately stupid

It opens the port, prints the board's `SEQ,*` progress lines, and writes each
dump to a CSV. It holds no recording state and makes no timing decision, so it
cannot corrupt one. **Keep it that way.** Anything that needs to *decide*
belongs in the firmware (if it is about *when*) or in `start_detector.py` (if it
is about *what the data means*). The whole v4 change is the removal of host-side
timing authority; re-adding a "record" button would walk it back.

Markers are written into the **leading** comment block now, since the firmware
sends them before the rows. `accel_live.py` had to append them after the data
(it only learned them once the dump finished), and a reader that stopped
scanning at the first non-comment line silently found no markers at all. The new
reader scans the whole file anyway, so old captures still load.

### `start_detector.py` — the algorithm, in detail

It is `sta_lta_start_detector.py`'s interface (sidebar of parameters, results
table, matplotlib canvas) with `detect_pushoff.py`'s causal structure and a
corrected decision signal.

**1. The signal it decides on.** Not `|a|`. The 3-axis vector magnitude is the
intuitive choice and the wrong one: horizontal acceleration adds *in quadrature*
with gravity, so a purely lateral event is under-reported — measured at 4.2× on
the sensor-evaluation capture (46 mg against a true 196 mg). The drive out of
the blocks is predominantly horizontal, which is exactly what `|a|` hides.

Not "drop whichever axis gravity is on", either, which is what `detect_pushoff`
did. That is only correct if the board is mounted perfectly flat. Tilt it 20° on
the block and part of gravity leaks onto the two "horizontal" axes while part of
the real horizontal drive leaks onto the "vertical" one.

What it does instead is the standard, mount-angle-independent form: estimate
gravity as a **vector** `ĝ` from the resting window, then split every sample:

```
a_vert  = a · ĝ
a_horiz = a − (a · ĝ) ĝ
```

No axis is chosen, no mounting angle is assumed, and `a_vert` falls out for
free. **This is only possible because v4 supplies a resting window** — the
3 s pre-roll. On a pre-v4 capture there is barely one.

**2. The trigger.** STA/LTA — the standard seismological trigger — on
`|a_horiz − baseline|²`: a short-window energy average against a long-window
one. The ratio asks "is there much more energy now than in the recent
background", which adapts to whatever baseline jitter is present instead of
trusting one fixed absolute number. A candidate opens when `horiz` crosses an
absolute floor **and** the ratio crosses `ratio_on`; the horizontal baseline is
frozen at that instant so a real movement cannot drag its own reference along
and hide itself.

**3. Confirmation and backdating.** The candidate must hold above a lower
confirm floor for `confirm_ms`, which rejects single-sample blips. The reported
time is then **backdated** to the first raw sample in the rolling window that
crossed the floor — the confirm window costs decision latency, never timestamp
accuracy.

**4. Re-arming — fixed from `detect_pushoff`.** That version set
`self.triggered = True` and reported exactly one event per file, so a settling
twitch would mask the real push-off behind it. This one uses hysteresis: after
an event it waits for the ratio to fall back under `ratio_off` before it will
trigger again. One capture can now legitimately contain a settling twitch, a
false start and a real push-off, and report all three.

**5. Warmup.** Triggering (not the EMA updates) is gated for one full LTA time
constant. Without it the LTA is still climbing away from its seed and every
file's opening samples look like an infinite-ratio event.

**6. The verdict — product rule, not a diagnostic convenience.**

| Event lands | Verdict |
|---|---|
| before `set` | pre-set settling — not judged |
| within `blank_ms` (1 s) after `set` | **rise into set position — not judged** |
| between then and `go` | **FALSE START** |
| after `go`, under 100 ms | **FALSE START** (IAAF-style floor: no human reacts faster) |
| after `go`, over 100 ms | valid start, reaction time reported |

**The blanking window is the part most likely to be "simplified" away.** At the
`set` command an athlete *raises the hips into the set position* — a real
movement of several hundred mg lasting about a second. A rule that flags any
movement between `set` and `go` therefore flags, every single time, exactly the
movement `set` just ordered. The first second after `set` is not judged.

It is still **analysed**: events there are reported and labelled, not discarded,
so an athlete who really did start during the rise shows up in the table for a
human to look at.

A stillness gate was considered instead — arm only once the signal has been
quiet — and rejected: an athlete who never settles *because they are already
starting* would never satisfy it, so the detector would never arm, disarming
itself in precisely the case it exists to catch. A clock always runs out.

There is a third outcome, `NOT JUDGEABLE`: if the signal is still above
`settled_mg` in the last 200 ms of the blanking, the rise had not finished, so
nothing after it can be told apart from its tail. Saying so beats calling the
attempt clean or false on a coin flip.

`GO_DELAY` in the firmware was raised from 1-2 s to **2.2-3 s** to match — with
a 1 s blanking, a 1-2 s window left almost nothing judged, and the `go` could
fire while the athlete was still settling. **These two numbers are a pair:**
change one and revisit the other.

Note this replaces `detect_pushoff.py`'s `--after-go` flag, which existed to
*hide* pre-go movement. A real false-start detector must never do that —
catching movement before `go` is the entire point.

**7. Second stage: the AIC onset picker.** STA/LTA is good at deciding *that*
something happened and bad at deciding *when* it started, because "when" comes
out as a threshold crossing on a rising ramp. Measured on
`Data/accel_20260908_233722.csv`: the ramp climbs at **1.28 mg/ms** against a
noise floor of **1.19 mg (1σ)**, so 1 mg of doubt about the threshold is 0.8 ms
of doubt about the time — and to sit clear of the noise the threshold must be
5-8 mg, which is already 4-6 ms late. Moving the floor from 10 to 50 mg moves
the reported reaction time by **29 ms**.

That is not measurement error. The detector is perfectly repeatable; it is the
*definition* of onset that moves with the threshold. Worse, the lateness scales
with how steep the ramp is, so it varies with how explosive the athlete is and
does **not** cancel out of a calibration.

So there is a second stage, the standard one from seismology, where the problem
has exactly this shape: STA/LTA triggers, then **Maeda's AIC picker** refines
the onset with no threshold at all. It finds the sample that best splits the
window into a noise segment and a signal segment:

```
AIC(k) = k·log(var(sig[:k])) + (n-k-1)·log(var(sig[k:]))
```

Measured, same file, reported reaction time against floor:

| floor | with AIC | without |
|---|---|---|
| 10 mg | 302.6 ms | 307.2 ms |
| 20 mg | 302.6 ms | 315.3 ms |
| 30 mg | 302.6 ms | 323.4 ms |
| 50 mg | 302.6 ms | 336.2 ms |

**Spread 0.0 ms against 29.0 ms.** The floor stops being a critical parameter:
it only has to say "something happened here", and being wrong about it by a
factor of five no longer moves the measurement.

Three things this does *not* mean, all of which matter:

- **Stable is not correct.** AIC stops depending on tuning; that it lands on
  the *true* onset is unproven and needs an independent reference.
- **AIC is not causal** in the STA/LTA sense — it reads `aic_post_ms` (50 ms)
  of samples after the trigger. Harmless for reporting a reaction time; on
  device it would add that much latency to a live false-start alarm, without
  touching the reported timestamp's accuracy.
- **AIC assumes the window contains a quiet part and an active part.** For an
  event that fires in the middle of movement already in progress there is no
  such split, and AIC dutifully reports the largest variance change it can find
  inside a uniformly active window — which is meaningless. Caught on
  `Data/accel_20260908_233329.csv`, where a third event 200 ms into a 400 mg
  movement was dragged 83 ms backwards. Guarded now: the window's opening
  quarter must be 16x quieter than its closing quarter, or the threshold
  estimate is kept and the event is reported as
  `AIC: no clear onset, kept the threshold`. Conservative on purpose — falling
  back to a known-biased number beats silently substituting a wrong one.

`use_aic=False` / `--no-aic` / the GUI checkbox turns the stage off, which is
how the table above was produced. The plot draws the reported onset solid and,
when AIC moved it, the original threshold crossing dotted.

**The error budget with the second stage in:**

| Term | Contribution |
|---|---|
| clock resolution | 8 µs |
| sample timing | σ ≈ 334 µs |
| AIC (~1 sample) | ~1.2 ms |
| **detection total** | **~1-2 ms** |
| buzzer acoustic latency | **5-20 ms, systematic** ← now the dominant term |

So the detection side meets a 1-2 ms target, and the bottleneck moves to the
buzzer — which is constant and calibrated once.

**8. The plot.** Three panels sharing an x axis: raw x/y/z, **`horiz` with its
floors drawn**, and the STA/LTA ratio with its thresholds; the `set`→`go` window
is shaded. Use the `horiz` panel, not the raw one, to sanity-check where a
marker landed. Plotting `|a|` instead once produced a "the marker is in the
wrong place" report that turned out to be the plot and not the placement: `|a|`
crept 1.00 → 0.93 g, invisible on its axis, while `horiz` climbed cleanly
through the floor at the same samples.

### Where the captures live

`Data/` holds only v4 captures. Everything from before the evening of
2026-09-08 was moved to `Data/data_before_080926/`, which has a README
explaining why those files' `go` markers cannot be used to tune thresholds
(they are human keypresses, +/-100 ms) even though the data itself is fine.

### Verified against the existing captures

Run over every file in `Data/`, the new detector reproduces the reaction times
the 2026-09-07 session documented (**313.4 ms**, **265.2 ms**) and, thanks to
re-arming, now also reports the events that were previously masked:

```
accel_20260907_160354.csv   valid start                 t=2.2277s   +313.4 ms
accel_20260907_160755.csv   #1 FALSE START (pre-go)     t=1.5633s   −419.3 ms
                            #2 valid start              t=2.2478s   +265.2 ms
```

The GUI was smoke-tested headlessly: it builds, loads a capture, runs the
analysis and draws all three panels without error.

### What is still open

- **The thresholds are still not tuned against real on-block data.** Everything
  above changes the *reference* and the *signal*, not the constants.

  A floor sweep on the three 2026-09-08 captures shows the absolute floor is
  **not** the dominant term: the reported reaction time moves only 307 → 328 ms
  across floors of 5 → 40 mg, and 8 ms of that is between 5 and 20. Nor does 5 mg
  produce a false positive on the still-board control (its noise peaks at 7.6 mg).
  There is far more headroom than assumed — but on a block, with an athlete
  holding their own weight and real ambient vibration, that headroom is unmeasured.
  Leave the floor at 20 mg until it is.

  The number actually worth measuring first: **how much a real athlete moves
  while holding the set position.** It sets `settled_mg`, and it decides whether
  the judged window has any margin at all. On the clean bench run the window
  peaked at 12.3 mg against a 20 mg floor — only 1.6x.
- **The buzzer's acoustic latency is unmeasured.** Until it is, every reaction
  time carries a 5-20 ms systematic offset in a known direction. Measure it once
  with a GPIO edge and a microphone on one time base.
- **Button and buzzer code is untested on hardware** (see above).
- Nothing from this session is committed yet.

---

## READ THIS SECOND — 2026-09-08 (morning): the Arduino core silently decides your timestamp resolution

**If you read nothing else: build `AccelStream.ino` with the `Seeeduino:mbed`
core — the board menu entry labelled `XIAO nRF52840 Sense (No Updates)`. Not
the `Seeeduino:nrf52` entry, despite what earlier versions of `BUILD.md` said.**

### What happened

A day was lost to timestamps that looked fine and were not. Symptom: sample
intervals in a capture took only two values, 977 µs and 1954 µs (= 2 × 977),
against captures from 2026-09-03/07 that had 59 distinct interval values around
1022 µs. Same board, same sketch.

Two wrong diagnoses were pursued before anyone measured anything:

1. *"`micros()` inside the ISR is coarse"* — plausible, wrong. Moving the
   timestamp out of the ISR changed nothing.
2. *"the mbed core is the broken one"* — exactly backwards, and acting on it
   is what introduced the fault, by switching a working setup onto the
   `nrf52` core.

`Arduino/ClockCheck/ClockCheck.ino` was then written to stop the guessing: it
reports which core it was compiled with and measures `micros()`'s smallest
observable step. It answered in three seconds.

### Root cause

`Seeeduino:nrf52`, `cores/nRF5/delay.h`:

```c
static inline uint32_t micros( void )
{
  // Use DWT cycle count if it is enabled, otherwise use rtos tick
  return dwt_enabled() ? (DWT->CYCCNT / 64) : tick2us(xTaskGetTickCount());
}
```

`dwt_enabled()` tests `CoreDebug->DEMCR & TRCENA` and `DWT->CTRL & CYCCNTENA`.
The DWT cycle counter is off unless a debugger enabled it, so on a normally
flashed board the fallback is what runs — and `tick2us` divides by
`configTICK_RATE_HZ`, which `freertos/config/FreeRTOSConfig.h` sets to **1024**.
That is 976.5625 µs per step: coarser than the sampling interval itself.

`Seeeduino:mbed` implements `micros()` as `timer.elapsed_time().count()` off a
real hardware timer — measured at **8 µs** resolution on the board. That is why
the September captures were fine: they were taken on the mbed core all along.

### Why this class of bug matters more than the millisecond

Nothing in the bad captures looked bad. `DROPPED` was 0, there were no gaps, no
duplicate timestamps, and the accelerations were physically sensible. Only the
distribution of `dt` gave it away, and only because someone plotted it. A
product shipped in that state would compute confident, wrong reaction times.

Three defences now exist, all cheap:

- **Firmware measures `micros()` resolution at boot**, prints it in the banner,
  and emits a loud five-line `!! WARNING` block if it exceeds 100 µs.
- **Every dump carries `CLOCKSTEP,<us>`**, so any CSV stays checkable after the
  fact instead of relying on whoever recorded it having picked the right core.
- **`verify_rate.py` fails** if the reported resolution exceeds 100 µs.

Prefer this shape of fix — make the machine assert its own preconditions —
over documenting the trap. The trap *was* documented, in `HANDOFF.md:504`,
about the very same `#ifdef`-invisible-macro hazard on the very same core, and
it still cost a day.

### Firmware v3, validated on hardware

`AccelStream.ino` now samples at **ODR 833 Hz, paced by the IMU's data-ready
interrupt on INT1** (pin 18) instead of by however fast the read loop happens
to run.

Why the change: v2 configured 1660 Hz and achieved ~977 Hz. That was never a
sensor fallback — the chip really ran at 1660 Hz, but one burst read costs
~1023 µs against a 602 µs budget, so the pacing deadline was unreachable and
the loop free-ran at I2C throughput. The rate was repeatable but *emergent*:
whatever is left after the loop's workload. Since the STA/LTA detector is meant
to run on-device, adding it would have lowered the sample rate silently — the
same failure that capped v1 at 232 Hz.

`verify_rate.py` on 2026-09-08, PASS:

| | |
|---|---|
| effective rate | **863.6 Hz** (ODR 833 nominal; the sensor's oscillator runs ~3.4% high — normal tolerance) |
| `dt` | mean 1158.1 µs, **std 11.5 µs**, min 1115, max 1199 |
| `CLOCKSTEP` | 8 µs |
| `DROPPED` / gaps | 0 / 0 |

Note what this did *not* buy: total sample-timing error is σ ≈ 334 µs, against
≈ 342 µs for v2. Unchanged. Coarser quantisation (1158 µs steps vs 1023) is
traded against removing the 0–602 µs of random staleness the free-running read
carried, and the two cancel. **The gain is determinism, not precision** — a
rate set by the sensor rather than by the loop's workload, ~135 µs/sample of
headroom for the detector, and an error budget that is now measured instead of
assumed.

Do **not** go back to ODR 1660 with this design: DRDY would fire every 602 µs
while the read takes ~1023 µs, so roughly half the interrupts would be missed.
1660 only ever worked in the old free-running scheme.

Other v3 changes: a `static_assert` on `ACCEL_ODR_HZ` (the library's
`accelSampleRate` switch silently falls through to 104 Hz for any value not in
its list); a watchdog for the latched DRDY line (a missed edge would otherwise
be permanent — no read means no falling edge means no next rising edge) whose
recoveries are counted and reported; and the pin macros are now pulled in
explicitly, since `pins_arduino.h` is not auto-included on the mbed core, which
had also been silently compiling away the `PIN_LSM6DS3TR_C_POWER` guard.

### Still the dominant error, and untouched

Everything above concerns sub-millisecond sampling. Deciding **where on the
push-off ramp the movement began** is worth tens of milliseconds and is still
untuned — roughly 100× larger. It cannot be tuned against the current `go`
marker either, which is a human keypress: on
`Data/data_before_080926/accel_20260907_160108.csv`
the marker lands ~90 ms *after* the movement had already started. Tuning a
±10 ms threshold against a ±100 ms reference is circular.

The agreed next step is an independent reference: an LED driven by the firmware
at the same instant it timestamps `g`, filmed at 240 fps, so video and CSV point
at one physical event. That is a temporary calibration jig, **not** a product
change — the shipped system stays IMU-only (a force sensor was already
evaluated and rejected as invasive, `README.md:65`).

---

## READ THIS FIRST — 2026-09-07: AccelStream.ino v2, detection algorithm work, real end-to-end reaction times

Big session, done live with three people testing hardware in real time. Everything
below is committed and pushed on `main` as of this write-up. `Arduino/AccelStream/AccelStream.ino`
and `Tools/accel_live.py` were both substantially rewritten (not incrementally
patched) - read them fresh rather than diffing against memory of the old version.

### Firmware: why it changed, in order

1. **The 416 Hz sketch only actually delivered ~232 Hz.** Diagnosed by capturing real
   data and computing `dt` between consecutive `t_us` values - median was 4.31 ms, not
   the 2.4 ms the 416 Hz config implied, with almost no jitter (a hard ceiling, not
   scheduling noise). Root cause: `readFloatAccelX/Y/Z()` each does two I2C
   transactions (write register pointer, read 2 bytes) - six transactions per sample.
   Each transaction has a large, fairly fixed driver overhead on the nRF52840's Wire
   implementation (measured indirectly at ~510 us/transaction). **Fix:** X/Y/Z occupy
   six consecutive registers on the LSM6DS3, so one `readRegisterRegion()` burst call
   reads all six bytes in one transaction, replacing three library calls (six
   transactions) with two. This alone took measured throughput from 232 Hz to ~977 Hz.
2. **ODR bumped 416 -> 1660 Hz** (`ACCEL_ODR_HZ`). Careful gotcha here: the vendored
   library's own settings comment says "1666" but `LSM6DS3.cpp`'s `accelSampleRate`
   switch statement's case label is literally `1660` - passing 1666 silently falls
   through to the 104 Hz default. Verified against the library source before picking
   the constant; costs nothing to double check again if this ever gets touched.
3. **Even with the burst read, printing an ASCII CSV row over serial for every single
   sample leaves near-zero timing margin at high rate**, and was the second half of
   the original 232 Hz ceiling (not just the I2C cost). Fix: printing is decoupled
   from sampling entirely.
   - **Idle** (default, from boot, no command needed): every `STREAM_DECIMATE`-th
     sample (5, so ~332 Hz) is printed as a live-preview CSV row
     (`t_us,x_g,y_g,z_g`) - fast enough for a chart, slow enough that ASCII
     formatting has huge margin and can never rob a sample.
   - **Recording**: nothing is printed. Every sample at full ODR goes straight into a
     RAM buffer (`recT`/`recX`/`recY`/`recZ`, `MAX_REC_SAMPLES = 12000` -> ~12.3 s at
     the ~977 Hz *achieved* rate, ~117 KB, comfortably inside the XIAO nRF52840's
     256 KB with no BLE stack running). The whole buffer is dumped over serial only
     after the capture stops (`DUMP_START,<n>` / rows / `DUMP_END` framing). This is
     the real fix for rate-under-load: serial timing can never compete with sampling
     during the window that actually matters.
   - **Real achieved rate is ~977-979 Hz, not 1660 Hz**, verified repeatedly on real
     hardware (`Tools/verify_rate.py`, and every real capture's own `t_s` column).
     Root cause: even at 2 I2C transactions/sample (~510 us each = ~1020 us total),
     there's no margin left in a 1660 Hz (602 us) budget - the code is now
     I2C-transaction-bound, not print-bound. **This was a deliberate stopping point,
     not an oversight**: 977 Hz already exceeds the 833 Hz the earlier sensor
     evaluation (below) called sufficient (quantization σ ≈ 0.30 ms at 977 Hz vs its
     0.35 ms figure at 833 Hz). Going further would mean batching multiple samples
     per I2C read via the chip's hardware FIFO - explicitly **not attempted**, since
     the one prior firmware here that used the FIFO (`BLEtest.ino`, see below) had an
     unresolved crash, and reintroducing FIFO complexity wasn't judged worth it for a
     rate that's already past the target. If someone wants to chase 1660 Hz later,
     FIFO batching (amortizing the per-transaction I2C cost over many samples) is the
     way, done incrementally and tested at each step - not the BLEtest.ino path.
4. **Range raised 8g -> 16g.** A 2026-09-07 bench test (hard hand-hit, well above a
   real push-off) clipped hard at ±8g on two axes simultaneously for ~200 ms. 16g
   costs almost nothing (LSB resolution 0.244 mg -> 0.488 mg, still far below the
   ~20 mg detection floor) so it's cheap margin.
5. **Ground-truth markers for bench testing: `o` / `s` / `g` / `S`.** Needed a way to
   know when a push-off *should* have started, to validate the detection algorithm
   against something other than eyeballing a chart. Iterated through two designs
   before landing here - both false starts are worth knowing about if this gets
   touched again:
   - First tried a GPIO button wired to a pin, debounced in firmware. Replaced before
     ever being wired up in favor of the option below (simpler, zero extra hardware).
   - Then tried an automatic firmware-generated 3-beep "ready/set/go" sequence
     relayed to the PC to play audibly. Abandoned at the design stage: a realistic
     15 s "on your marks" + 2 s "set" pause doesn't fit in the ~12.3 s recording
     buffer (buffer only starts filling at "set", so the beep-timing idea would have
     needed either a much longer buffer - real RAM pressure - or an unrealistically
     short pause).
   - **What's actually in the firmware now:** a human presses three keys/buttons and
     says the word out loud at the same instant. `o` = "on your marks" (clears any
     leftover set/go from a previous attempt). `s` = "set" - **also arms full-rate
     recording**, no separate arm command needed; this is what keeps a real
     on-your-marks/set pause from ever overflowing the buffer, since nothing is
     buffered before "set". `g` = "go" - the reference timestamp a push-off is
     measured against. `S` (**uppercase** - lowercase `s` means "set") stops and
     dumps. Every marker is timestamped with the firmware's own `micros()`, the same
     clock every accelerometer sample uses, so there's no cross-clock sync problem
     and no PC-audio-latency to account for (the human's own voice is the real
     stimulus - arguably more realistic than a synthesized beep would have been
     anyway, at the cost of some small, unavoidable human tap/say coordination jitter
     that doesn't matter for this bench-reference use).
   - Firmware dump order: `DUMP_START,<n>` / `ON,<t_us|0>` / `SET,<t_us|0>` /
     `GO,<t_us|0>` / `<n data rows>` / `DUMP_END`.

### `accel_live.py`: what changed to match

- Command bytes: `CMD_ON=b"o"`, `CMD_SET=b"s"`, `CMD_GO=b"g"`, `CMD_STOP=b"S"`.
  `start_recording()` (bound to the "Set" button/key) now sends `CMD_SET`, not the
  old `CMD_START`. New `mark_on()`/`mark_go()` just relay a marker, no recording
  state change.
- Saved CSVs get trailing comment lines when a marker was captured -
  `# on_t_us` / `# on_t_s`, `# set_t_us` / `# set_t_s`, `# go_t_us` / `# go_t_s` -
  written in `_finalize_recording()`, right before the file closes. `*_t_s` is
  already relative to the recording's own `t_s` clock (same zero as the data rows),
  so e.g. `go_t_s` is directly usable against the `t_s` column. **These are appended
  after all the data rows, not in the header** - a first cut at reading them back
  (`Tools/detect_pushoff.py`'s `read_go_t_s()`) stopped scanning at the first
  non-comment line and missed them entirely until that was caught by testing against
  a real saved file, not assumed.
- **Real bug, caught live, not hypothetical:** matplotlib binds `o`/`s`/`g`/`p` to
  built-in figure actions by default (zoom-rect / save-figure-dialog / toggle-grid /
  pan) - exactly this app's marker keys. Pressing `s` popped matplotlib's own save
  dialog instead of sending the "set" marker. Fixed in `_build_ui()` by stripping
  just those letters out of the relevant `plt.rcParams['keymap.*']` lists before the
  figure is created (leaves `ctrl+s` etc. alone). `S` (capital) never collided,
  matplotlib's defaults are lowercase-only.
- **Diagnostic stderr logging added** on every marker send/receive and on ignored
  Set/Stop presses (`[accel_live] sent SET marker...`, `[accel_live] board confirms
  SET marker: ...`, `[accel_live] 'Stop' ignored - recording=... awaiting_dump=...`).
  This was what actually resolved a "markers aren't showing up in the CSV" report
  that looked like a bug from the file alone - the log showed commands were sent,
  confirmed by the board, and the dump was simply still in transit (a few thousand
  CSV rows over serial can take a few real seconds) when the user checked/re-pressed
  Stop. Not a bug; the dump just needs to be given time to finish. Keep this logging
  in place - it's the fast way to tell "not sent" vs "sent but board didn't confirm"
  vs "sent, confirmed, just still transferring" apart, instead of guessing from a
  possibly-incomplete file afterward.
- 5 buttons now: On your marks (o) / Set (s) / Go (g) / Stop+Save (S) / Snapshot (p).

### New: `Tools/verify_rate.py`

CLI-only (no matplotlib), for when the graph doesn't matter and you just want a
pass/fail on rate + integrity. Arms a recording, waits a fixed window, stops it,
reports achieved Hz vs. target, row-count-matches-expected, gap count, and basic
value sanity (resting ~1g, nothing pinned at the clip rail). This is what caught the
232 Hz ceiling and later confirmed the fix (977.5 Hz, exact row count match, zero
gaps) before any GUI tool was involved.

### New: `Tools/detect_pushoff.py` — the actual detection-algorithm prototype

Explicitly a **starting point for the three of you to tune, not a validated
detector** - built so the algorithm can be developed entirely offline against
recorded CSVs (no hardware needed), which was the original ask this tool answers.
`PushOffDetector` is written **causally** (`update()` sees only the current sample
plus a short rolling history, never a future sample, never the whole file at once) -
deliberately, so that whatever gets tuned here can be ported to C++ on the real
device later without changing the logic, only the language. The one place this
isn't quite true yet is the confirm window (see below).

Algorithm: horizontal-plane deviation from a slow-adapting baseline
(`horiz = |(two non-gravity axes) - baseline|`), fed into a fast/slow moving-average
pair (STA/LTA) on `horiz²`; a candidate opens when `horiz` crosses an absolute floor
*and* the STA/LTA ratio crosses a threshold, must then stay above a lower confirm
floor for `confirm_ms` to be accepted (rejects single-sample noise blips), and the
reported timestamp is backdated to the first raw sample that crossed the floor - not
the (later) confirmation instant. Defaults (`sta_ms=15, lta_ms=800, ratio=6,
floor_mg=20, confirm_ms=15`) are the same ballpark numbers discussed while designing
this, unchanged since - **not yet tuned against real block data**, that's the actual
next step.

Three real bugs found and fixed by testing against real captures, not by inspection
alone - worth knowing about if the constants or the class get touched:

1. **LTA starts near zero**, so STA/LTA is meaningless (spuriously huge) for the
   first fraction of a second of any file until LTA has converged on real background
   noise. Without a warmup gate this made every file's very first few samples look
   like an infinite-ratio "event". Fixed with `warmup_n` (defaults to one full LTA
   time constant) gating candidate detection, not the EMA updates themselves.
2. **Gravity axis was hardcoded to "exclude Z"**, on the assumption Z is always
   vertical. Wrong: three different real captures on 2026-09-07 had gravity on Z, X,
   and Y respectively (mounting/handling orientation isn't fixed). Fixed with
   auto-detection - the first ~20 ms of each file is averaged and whichever axis is
   largest in magnitude is excluded (`gravity_axis="auto"`, overridable). Verified
   against real files with gravity on each of the three different axes.
3. **The plotted `|a|` (raw 3-axis magnitude) is not the signal the detector
   decides on, and looks visually flat right where the detector correctly fires.**
   Confirmed with real numbers: at a reported event, `|a|` crept from 1.00g to 0.93g
   over ~13ms (invisible on a 0-4g axis) while `horiz` (gravity-axis excluded,
   baseline-subtracted - the actual decision signal) was already rising cleanly
   through the 20mg floor at the same samples. This produced a "the marker looks like
   it's placed before the real push" impression that was investigated and traced to
   plotting the wrong signal, not a placement bug. **Fix: `--plot` now adds `horiz`
   itself as its own panel (with the floor line drawn on it), both full-recording and
   zoomed**, so a placement can be checked against the actual decision signal instead
   of an unrelated (if visually intuitive) proxy. Use this panel, not the `|a|` one,
   when sanity-checking where a marker landed.

Also added: `--after-go` (ignore any movement before the file's own go marker, for
isolating a real push-off from pre-go handling/fidgeting when checking one bench
file) and `--zoom-ms` (width of the zoomed, individual-samples-marked panel).
**`--after-go` is a diagnostic convenience only** - a real false-start detector must
NOT do this, since catching movement before "go" is the entire point of a
false-start check. Confirmed directly on real data: run unrestricted, one capture's
detector caught a small pre-go handling blip instead of the real push (reaction time
came out negative, -114 ms and -2045 ms in two different files) - `--after-go`
isolated the real push-off in those same files (75 ms and 198 ms respectively) to
confirm the rest of the pipeline was sound, but the underlying question - how to
tell "settling into position" apart from "an actual false start" - is unresolved and
needs real on-block data, not bench data, to answer.

### End-to-end validation on real hardware

Multiple real captures on 2026-09-07 with the `o`/`s`/`g`/`S` protocol, checked with
`detect_pushoff.py`: rate 977.5-978.5 Hz every time, zero gaps, zero dropped/
duplicate timestamps. Two captures had "go" cleanly separated in time from the
actual push (no overlap, no pre-go movement crossing threshold) and gave plausible,
mutually consistent reaction times - **266.2 ms and 313.4 ms** - with the unrestricted
and `--after-go` detector runs agreeing exactly on both. One capture had the "go"
marker and the push overlapping in time (looks like a solo test where marking "go"
and starting to move weren't cleanly separated) and produced nonsensical negative
reaction times either way - not a pipeline bug, a test-protocol issue (mark clearly
before moving, or use two people: one marking, one reacting).

### Where this leaves things

- Firmware, `accel_live.py`, and the three `Tools/` scripts are all committed and
  pushed as of this write-up - see `git log` for the actual commit if this doc and
  the tree ever disagree.
- **Rate: settled at ~977 Hz for now**, deliberately not pursuing 1660 Hz via FIFO
  unless someone decides the extra precision is actually needed (see point 3 above)
  - it already beats the sensor evaluation's own 833 Hz bar.
  *(Superseded 2026-09-08: now a deterministic 833 Hz ODR paced by the INT1
  data-ready interrupt, ~863 Hz measured. See the 2026-09-08 section at the top.
  FIFO is still not being pursued.)*
- **Detection algorithm: prototyped and causally correct, not yet tuned against real
  on-block data.** The bench captures validate the *mechanism* (rate, markers,
  sample-level placement, gravity-axis handling) but not the *thresholds* - the open
  question (pre-go fidgeting vs. a real false start) needs a real block session to
  answer, not more bench testing.
- Whoever picks this up next: read `Tools/detect_pushoff.py`'s module docstring and
  the `--after-go`/gravity-axis notes above before changing the algorithm's
  constants, and use `--plot`'s `horiz` panel (not `|a|`) to sanity-check any
  placement.

## READ THIS THIRD — README.md rewritten, new architecture direction, unresolved discrepancy

Later on 2026-09-04, after the repo reorganization described in the section
below, `README.md` was rewritten from scratch and `BUILD.md` was added.
None of this changed anything in `Arduino/`, `Tools/`, or `Flutter App/` —
it's documentation and product/business content only. What changed:

- **New target architecture (not yet implemented in code):** the starting
  device and a new **finish unit** (a second XIAO nRF52840 Sense) link over
  **Zigbee** instead of Wi-Fi. Wi-Fi was dropped for UX reasons (forces the
  user to join a device-hosted network each session); a straight BLE link
  was found in real testing to max out at **~25 m** range, too short for a
  100 m track, hence splitting into two hops with Zigbee for the long,
  fixed start↔finish leg.
- **A new system diagram** was added: `Docs/reaction_time_diagram_v2.png`
  (the original `Docs/reaction_time_diagram.png` was kept, not replaced).
  `README.md`'s "System at a glance" image now points at the v2 diagram.
- **A business plan section** was added to `README.md` (hardware BOM/pricing,
  a freemium app model with monthly/6-month/annual/club tiers, ad-supported
  free tier, and an illustrative TAM/revenue projection). All figures in it
  are explicitly invented/illustrative for a course business case, not real
  financials — don't treat them as sourced data if reusing this content
  elsewhere.
- **The old ASCII "System Architecture" diagram was removed** from
  `README.md` (redundant with the image), replaced by a prose "How the
  System Works" section.

**Discrepancy above: resolved 2026-09-04, same day.** The first
`reaction_time_diagram_v2.png` the user supplied showed the finish block
talking Zigbee to the phone (no BLE anywhere in it), contradicting the
user's own verbal description and `README.md`'s prose (both said BLE). The
user then supplied a corrected diagram (same filename, overwritten) that
matches the verbal description: **Zigbee for start↔finish, BLE for
finish↔phone**, with **WiFi Direct (SoftAP) as a fallback specifically for
the start↔finish hop** if Zigbee proves unreliable — not a phone-facing
fallback. `README.md`'s "How the System Works", "Key design decisions"
table, and "Open Risks" section were updated to state the WiFi-fallback
nuance explicitly, so the image and the prose now agree. Nothing further to
reconcile here.

## READ THIS SECOND — repo reorganized 2026-09-04, BLEtest.ino removed

Everything below this section (up to **State of the tree**) describes work from
2026-08-29 and earlier, centered on a firmware called `BLEtest.ino` that no
longer exists in this repo (deleted 2026-09-04 — see why below). Treat paths
in those older sections as **historical**, not current: several folders were
renamed or moved on 2026-09-04:

| Old path | New path |
|---|---|
| `prostart/` | `Flutter App/prostart/` |
| `data/` | `Data/` |
| `tools/` | `Tools/` |
| `docs/` | `Docs/` |
| `playground_IMU/` | **deleted entirely** — see note below |
| `Arduino/BLEtest/` | **deleted** — see why below |
| `Arduino/HighFrequencySampleRate/` | **deleted** — superseded, see why below |
| `tools/kinestart_live.py` | **deleted** — superseded by `Tools/accel_live.py` |

**Why `BLEtest.ino` is gone:** it combined a hardware-FIFO accelerometer
capture at 833 Hz, a software PLL to derive real timestamps from the FIFO's
sample count, and a full BLE stack (live view + "go" trigger + raw dump
characteristics). On 2026-09-03 it reproducibly hung or crash-looped
(`setup()` re-running on its own, "IMU OK"/"Ready" reprinting with no user
input) on **two separate physical XIAO nRF52840 Sense boards**, ruling out a
single bad board. A bare-bones sanity sketch with no IMU/BLE/FIFO
(`Arduino/SerialEchoTest/SerialEchoTest.ino` — heartbeat print + serial echo,
nothing else) worked correctly on both boards, ruling out the cable, USB
port, and Mac-side toolchain — the fault was specifically in `BLEtest.ino`'s
code. One concrete symptom caught along the way: `BLE.begin()` was observed
taking **~2 minutes** to return instead of milliseconds; disabling BLE
entirely didn't fix responsiveness by itself, and with BLE *and* the FIFO
drain both disabled the board was still unresponsive to serial commands. The
root cause was **never isolated** — debugging was abandoned in favor of a
clean rewrite rather than continuing to guess blindly. It was fully deleted
(not just deprecated) on 2026-09-04 during a repo cleanup, so it can't be
accidentally reflashed. It's still recoverable from git history
(`git log --all --oneline -- Arduino/BLEtest/BLEtest.ino`) if ever needed for
reference, but starting over cleanly is the recommended path — see
**Unresolved** below.

**Current, working pipeline** (verified end-to-end on real hardware, real
push-off-scale data already captured — see `Data/data_before_080926/`):

- **Firmware:** `Arduino/AccelStream/AccelStream.ino` — deliberately minimal.
  No BLE, no hardware FIFO, no hand-rolled I2C register/FIFO code - just the
  vendored LSM6DS3 library's plain polling reads (`readFloatAccelX/Y/Z`) on a
  `micros()`-scheduled loop at **416 Hz**, **±8 g** (raised from the old
  ±4 g: a real push-off rigidly mounted on the block can hit 2-5 g and would
  clip at ±4 g). Each sample's timestamp is a direct `micros()` read taken
  immediately after that sample's I2C transaction - a real measurement, not
  a count-times-nominal-period model like the old (also now-deleted)
  `Reaction_Time_HighFreq.ino`, or a PLL-inferred one like `BLEtest.ino` was.
  Serial protocol: `'r'` start CSV stream, `'s'` stop, `'p'` one immediate
  reading; streaming rows are `t_us,x_g,y_g,z_g`.
- **Live capture:** `Tools/accel_live.py` — live X/Y/Z + magnitude plot,
  autodetects the port, Record/Stop/Snapshot buttons with an actually-visible
  state change (button turns solid red with "● Recording..." while
  recording, flashes green "✓ Saved" on stop), gap/clipping detection, CSV +
  a 4-panel PNG (X, Y, Z separately, then magnitude) saved on stop.
- **Offline review:** `Tools/csv_plot.py` — always opens a native file-picker
  dialog (no CLI path to type), auto-detects the CSV's time column (`t_s`
  from `accel_live.py`, or `elapsed_s` from older captures like
  `Data/blockstart_20260831_test0.csv`, the old `Block_Start_Data.csv`
  renamed on 2026-09-04), same 4-panel layout, standard matplotlib zoom/pan
  toolbar for inspecting sub-second detail.
- **Diagnostic sketch:** `Arduino/SerialEchoTest/SerialEchoTest.ino` - keep
  this around. Flash it first on any new or suspect board before trusting
  more complex firmware; it isolates hardware/cable/port problems from
  firmware bugs in about a minute.

**Deleted on 2026-09-04, not just deprecated — don't go looking for these:**

- `tools/kinestart_live.py` - was paired with `BLEtest.ino`'s old protocol
  and is incompatible with `AccelStream.ino`'s CSV output (`elapsed_s` vs.
  `t_us`). Superseded by `Tools/accel_live.py`.
- `Arduino/HighFrequencySampleRate/` in its entirety (`Reaction_Time_HighFreq/`,
  `Python_Serial`, `CSV_Visualizer`) - the original HighFreq firmware computed
  elapsed time as `sampleIndex * nominal_period`, ignoring real timing jitter;
  its paired Python scripts had a hardcoded Windows path and crashed on both
  of the current CSV formats.
- `playground_IMU/` - the sensor-evaluation notebook, its six figures, and
  the raw capture CSV it analyzed. The **findings** are preserved below under
  **Sensor evaluation**, but the notebook and raw data themselves are gone
  from the repo (recoverable from git history if needed:
  `git log --all --oneline -- playground_IMU`).

**Unresolved:** the actual `BLEtest.ino` hang/crash-loop was never root-caused.
If BLE support is needed again, don't restart from its FIFO+PLL design -
consider adding a minimal BLE characteristic to `AccelStream.ino`'s simple
polling loop instead, one piece at a time, testing after each addition (that
incremental approach is exactly what would have caught this bug early).

---

## Goal (part 1)

Add a live accelerometer view to the Prostart Flutter app, and move device
connection out of the Home screen.

1. **Restructure connection UI** — the "Connect your Prostart device" card/flow
   moves from Home to the **Settings** tab. Home keeps only a status indicator:
   a prompt pointing at Settings when disconnected, a "See Live Data" entry
   point when connected.
2. **Live data screen** — subscribe to a BLE accelerometer stream (X/Y/Z),
   smooth scrolling chart at ~50 Hz, a Record button that buffers timestamped
   samples in memory, and after stopping: per-axis min/max/average plus CSV
   export via the platform share sheet.
3. **Firmware** — a second notification characteristic streaming accelerometer
   readings at ~50 Hz, separate from the existing single-shot, high-precision
   "go" timestamp characteristic, which stays exactly as it is. The new one is
   **visualization only** — it is not part of the reaction-time measurement.

All three are implemented. See Next Steps for what remains unverified.

## Project layout

Current, as of the 2026-09-08 evening cleanup:

- `Flutter App/prostart/` — Flutter app (Dart, `provider` for state, `flutter_blue_plus` for BLE)
- `Arduino/AccelStream/AccelStream.ino` — **current working firmware**: start
  sequence, buzzer, button, ring buffer, capture; no BLE. See the top section.
- `Arduino/ClockCheck/ClockCheck.ino` — diagnostic: reports which core it was
  built with and measures `micros()`'s real resolution
- `Arduino/SerialEchoTest/SerialEchoTest.ino` — minimal hardware/cable sanity
  check, no IMU or BLE
- `Arduino/I2C_Scanner/` — I2C debug sketch
- `Arduino/libraries/Seeed_Arduino_LSM6DS3/` — vendored IMU library
- `Tools/capture.py` — writes the board's dumps to CSV; holds no state
- `Tools/start_detector.py` — onset + false-start detector, GUI and CLI
- `Tools/verify_rate.py` — pass/fail on clock, rate and integrity
- `Data/` — recorded CSV captures and their plots
- `Docs/` — diagrams and figures used by the root README

Deleted 2026-09-08: `Arduino/Reaction_HardwareTest/` (TFT/buzzer/XBee bring-up
for an architecture no longer planned), `Tools/accel_live.py`,
`Tools/csv_plot.py`, `Tools/detect_pushoff.py`,
`Tools/sta_lta_start_detector.py`, and an untracked duplicate `prostart/` in the
repo root that held only build output.

`Arduino/BLEtest/`, `Arduino/HighFrequencySampleRate/`, `tools/kinestart_live.py`,
and `playground_IMU/` are all **deleted** — see the top section for why.

The root `README.md` used to describe a `firmware/` and `app/` layout that does
not exist; that is now corrected. It still links to `report/main.tex`, but no
`report/` directory has ever been committed here — left alone deliberately,
since it may live elsewhere.

Hardware in use for prototyping: **Seeed XIAO nRF52840 Sense** (BLE only),
onboard **LSM6DS3** IMU at I2C address `0x6A`. The final target per the README
is an ESP32-WROOM-32U over Wi-Fi, but nothing here targets that yet.

## Current progress

### BLE contract

Service `19B10000-E8F2-537E-4F6C-D104768A1214`, device local name
`BlockStartDevice`. Two characteristics:

| UUID | Role |
|---|---|
| `19B10001-…` | "go" timestamp — single-shot `micros()`, little-endian uint. **Untouched.** |
| `19B10002-…` | **New.** Accelerometer stream, ~50 Hz notify, 12 bytes = 3 × float32 little-endian (X, Y, Z) in g. |

The UUID and the 12-byte layout were duplicated in two places and had to stay
in sync: `Flutter App/prostart/lib/services/ble_service.dart` and the
now-deleted `Arduino/BLEtest/BLEtest.ino`. The Dart side still has this BLE
code; there is currently no firmware counterpart for it.

### Flutter — files added

- `lib/models/accel_sample.dart` — `AccelSample` (elapsed µs + X/Y/Z in g), `AccelAxis` enum.
- `lib/models/recording_session.dart` — `AxisStats.fromSamples` (min/max/avg), `RecordingSession.toCsv()`, timestamped file name.
- `lib/state/live_data_controller.dart` — screen-scoped `ChangeNotifier`. Enables notifications on entry, keeps a 5 s rolling window, buffers recordings (capped at 30 000 samples ≈ 10 min), computes stats, exports via `share_plus`.
- `lib/widgets/live_accel_chart.dart` — hand-painted scrolling strip chart. Also owns `accelAxisColors`.
- `lib/widgets/recording_summary_card.dart` — post-recording stats table + Export CSV button.
- `lib/widgets/home_connection_card.dart` — `ConnectPromptCard` and `LiveDataCard`.
- `lib/screens/live_data_screen.dart` — the live view, with `LiveDataScreen.route()`.
- `lib/screens/settings_screen.dart` — the Settings tab; now hosts connection.

### Flutter — files modified

- `lib/screens/home_screen.dart` — now takes an `onGoToSettings` callback; shows `ConnectPromptCard` or `LiveDataCard`.
- `lib/screens/home_shell.dart` — Settings tab is real; `_screens` became instance-level (was `static const`) so Home can receive the tab-switch callback.
- `lib/state/ble_connection_controller.dart` — retains the discovered `List<BluetoothService>`, exposes `accelerometerCharacteristic`.
- `lib/services/ble_service.dart` — new UUID constants, plus the scan fix described below.
- `pubspec.yaml` — added `share_plus` 13.3.0 and `path_provider`.

`ConnectDeviceCard`, `ConnectionSheet`, `DeviceStatusCard` and
`ConnectionBadge` are unchanged and still in use, just from Settings now.

### Firmware — `Arduino/BLEtest/BLEtest.ino` rewritten

Now does IMU + BLE in one sketch. Streams only while
`accelChar.subscribed()` is true — i.e. only while the app's Live Data screen
is open, since `LiveDataController.dispose()` calls `setNotifyValue(false)`.

IMU config, all deliberate departures from the library defaults:
`accelRange = 4` (default ±16 g is far too coarse for human movement),
`accelSampleRate = 104` (just over 2× the 50 Hz notify rate),
`accelBandWidth = 50`, `gyroEnabled = 0` (unused, and saves I2C time inside the
20 ms window).

### Verified

`flutter analyze` clean, `flutter test` passing (one pre-existing onboarding
widget test).

The end-to-end path has since been exercised **by hand on real hardware**: the
sketch was flashed, the app connected, the live chart streamed, and a recording
was exported to CSV — that file is `playground_IMU/prostart_imu_20260829_213017.csv`
and is the basis of the sensor evaluation below. No agent has run anything on
hardware or a simulator; treat the automated checks as covering the Dart side
only.

> `flutter test` may fail to launch on this machine with `Failed to find …
> flutter_tester`. That is a stale Flutter artifact cache (darwin-x64 tools on
> an arm64 host), not a code problem — `flutter precache --force --universal`
> fixes it.

## What worked

- **CustomPainter instead of a charting package.** `fl_chart` and friends rebuild the widget tree per update and get janky at 50 Hz. A `CustomPainter` repainting off the vsync ticker, with x mapped from each sample's own timestamp, scrolls smoothly and stays jitter-free when packets arrive early or late. Y-axis auto-scales with easing so spikes don't make the axis jump.
- **Splitting the notification channels.** Sample arrivals go through a `ValueNotifier<AccelSample?>` (`LiveDataController.latest`), not `notifyListeners()`. Only status/recording changes call `notifyListeners()`. This keeps 50 Hz of data from rebuilding the whole screen. The recording timer label has its own `ValueNotifier<Duration>` ticked once a second.
- **Scoping the controller to the route.** `LiveDataController` is created and disposed with the screen, so BLE notifications are only enabled while the user is actually looking at the stream, and the firmware's `subscribed()` gate means the board stops reading the IMU too.

## What didn't work — do not repeat

Four debugging dead ends cost real time. All are now fixed, but the reasoning
matters if similar symptoms return. A fifth — the CSV export failure — is
written up under the `path_provider_foundation` pin below; its lesson was that
the error surfaced as "check permissions" when the real cause was a native
framework that was never embedded.

- **"Device not found" was blamed on the app first.** It was not. The scan and connect path was never touched by this work — `git diff` on `ble_service.dart` was purely additive constants. Check the board before the app: pair with nRF Connect or LightBlue to see whether `BlockStartDevice` is advertising at all. That single check separates firmware problems from app problems in seconds.
- **`FlutterBluePlus.connectedDevices` cannot find stale connections.** The old `_disconnectStaleConnections()` used it to clean up links surviving a hot restart — but it is a snapshot of an in-memory map built by *this* process, so after a restart it is empty, precisely in the case it was written for. A connected BLE peripheral stops advertising, so the still-live OS link made the board invisible to scanning. Replaced with `_findAlreadyConnectedDevice()`, which uses `FlutterBluePlus.systemDevices([serviceUuid])` (reports links held by any app) and **adopts** the device rather than disconnecting and rescanning. `connect()` is still required to attach it to our app but returns immediately. Note Android ignores the `withServices` argument to `systemDevices`, so there is a local-name fallback; `discoverServices()` is the real verification either way.
- **`while (!Serial) delay(10);` bricks the sketch on a XIAO.** USB is native on the nRF52840: `Serial` only goes true when a host opens the CDC port. On battery, on a port with no Serial Monitor open, or on a charger, `setup()` blocks there forever and `BLE.advertise()` never runs. Reset makes it worse — it re-enters the block. Now a bounded 3-second wait, in `BLEtest.ino` and in `I2C_Scanner.ino`. **The pattern is still present in the vendored library examples** — watch for it if you flash one.
- **Flashing an IMU debug sketch silently removed BLE.** Commit `33e3cd5` edited `Arduino/libraries/.../HighLevelExample.ino`, which contains zero BLE code. With that on the board, no app can ever find it. Obvious in hindsight, invisible from the app side. Now moot, since BLE and IMU live in the same sketch.

## Next steps

Steps 3–5 of the original list are now answered by a real capture (see the
sensor evaluation below). What remains:

1. **Flash `Arduino/BLEtest/BLEtest.ino`** and watch the Serial Monitor for `IMU OK`. If it prints `IMU error - live data disabled`, BLE still works and reaction time is unaffected — but the live view will be dead. The sketch sets `PIN_LSM6DS3TR_C_POWER` high inside an `#ifdef` (the XIAO Sense IMU has a dedicated power pin; if it stays low `begin()` fails even with I2C wired correctly). If the macro is missing from the installed core, that guard compiles it away and the pin is never driven — check the variant header.
2. ~~**The sketch has never been compiled by an agent.** No `arduino-cli` on this machine.~~ **Wrong, corrected 2026-09-08:** the Arduino IDE bundles an `arduino-cli` and `AccelStream.ino` now compiles from the command line. See the top section for the exact invocation.
3. **Act on the firmware changes** the evaluation calls for (ODR, detector, audio-latency calibration) — listed under *What the evaluation implies for the firmware*.
4. **Resolve the board question.** The system diagram shows an **ESP32-WROOM-32U with an external IMU**; the README's "Hardware direction under evaluation" note right below it argues for the **Arduino Nano 33 IoT** precisely because it avoids an external IMU. One of the two is stale. Nothing else can be finalised until this is settled.
5. **Verify the CSV export on iPad.** It works on iPhone (the capture in `playground_IMU/` came out of it). On iPad the share sheet is a popover needing an anchor rect; `_export` in `live_data_screen.dart` derives one from the summary card's `RenderBox`, still unexercised. "Save to Files" is the save-to-device path.

## Fixed since the first draft

- `I2C_Scanner.ino` now uses the same bounded 3 s `Serial` wait as `BLEtest.ino`.
- `prostartAccelerometerSampleRateHz` is wired into the recording summary line,
  which reads e.g. `48.8 Hz (nominal 50 Hz)`.
- Root `README.md` repository structure and the `cd app` / `firmware/` paths in
  Getting Started corrected to `Arduino/` and `prostart/`. A system overview
  diagram was added near the top, from `docs/`.
- **CSV export on iOS was broken and is fixed** — see the pin below.

### `path_provider_foundation` is pinned - do not remove the override

`path_provider_foundation` 2.5.0 replaced its iOS/macOS plugin with an
`objective_c` FFI backend, 2.5.1 reverted it, and 2.6.0 went back to FFI. That
backend ships as a Flutter *code asset*, only built and embedded into
`Runner.app` when native assets are enabled (`flutter config` shows
`enable-native-assets: (Not set)` on stable). Without it every call — including
the `getTemporaryDirectory()` in `LiveDataController.exportLastSession` —
throws `Failed to load dynamic library 'objective_c.framework/objective_c'`.

The tell is `ios/Podfile.lock`: the FFI version registers no pod at all, so
`path_provider_foundation` was simply absent from it. `pubspec.yaml` now has a
`dependency_overrides` pinning 2.5.1, the newest pigeon/CocoaPods release.
`share_plus` was suspected first and is **not** implicated — it stays on 13.3.0.

Remove the override once native assets are on by default on stable, and rebuild
from scratch (`flutter clean` + `pod install`) when you do.

---

## Sensor evaluation — read this before touching the firmware

> **`playground_IMU/` was deleted on 2026-09-04** (see the top section) — the
> notebook, its six figures, and the raw capture CSV are gone from the repo.
> The findings below are what survive; they're summarized conclusions, not
> reproducible from anything currently in this tree.

A 14 s capture was taken from real hardware through the live view and exported
via the CSV path. Full analysis was in `playground_IMU/` (notebook with six
figures, plus a README covering the architecture questions). The headlines:

**The sensor is not the constraint.** Noise floor 0.65 mg (1σ). A light finger
tap peaks at 196 mg — 300× the noise — and a real block push-off is 1–3 g. A
20 mg threshold sits ~30σ clear of the noise. Resting magnitude 0.991 g, so
`accelRange` and the library scaling agree (this closes the old sanity check).

**The BLE stream's timestamps cannot be used for timing.** Arrival intervals are
bimodal and the nominal 20 ms bin is *empty*: 30% of samples arrive with no
measurable gap from the previous one, the rest cluster at 29.7 ms. This is
connection-event batching, and the app timestamps on arrival — so the CSV
records when a packet was received, not when the sensor was read. A systematic
±30 ms distortion. Effective rate is 48.8 Hz, so the firmware is producing
samples correctly; only the delivery is bursty.

This confirms rather than threatens the design: the live stream was always
meant to be visualization-only, and the single-shot `micros()` "go"
characteristic exists precisely because of this.

**Do not threshold on |a|.** The capture contains a near-purely lateral tap that
the vector magnitude under-reports by 4.2× (46 mg vs 196 mg), because horizontal
acceleration adds in quadrature with gravity on Z. The drive out of the blocks
is predominantly horizontal, so this is the direction that matters most.

### What the evaluation implies for the firmware

1. **Raise `accelSampleRate` from 104 Hz to 416 or 833 Hz** for the detection
   path. At 104 Hz quantization alone is σ = 2.8 ms; at 833 Hz it is 0.35 ms.
   The gyro is already disabled, so the I2C budget is there. Note the current
   104 Hz is correct *for the 50 Hz visualization stream* — this is about the
   detection path, which does not exist yet.
2. **Trigger on horizontal-plane magnitude** `sqrt(dx² + dy²)`, with Z vertical
   and deltas against a resting baseline captured at "on your marks". Measured
   on this capture it keeps full sensitivity to both lateral (196 mg) and
   vertical (128–165 mg) events on a ~1 mg noise floor, and is orientation-
   independent in the mounting plane.
3. **Threshold ~20 mg.**
4. **Calibrate the speaker's audio latency and subtract it.** The "go" timestamp
   must mark when sound leaves the speaker, not when the code called `play()`.
   The DAC, amplifier and membrane add 5–20 ms — systematic, always in the same
   direction, and 5–20% of the false-start threshold if ignored. It is
   deterministic on an MCU, so one calibration holds: record the GPIO marking
   `play()` and a microphone next to the speaker on the same time base, measure
   the delay to the first pressure wave. Re-check if the amp, speaker or sample
   rate changes.

With those, the reaction-time budget is **σ ≈ 1.2 ms** after calibration — the
5–20 ms audio figure is systematic and calibratable, not a random error, which
is why it does not dominate. Well inside a 100 ms rule.

### Scope limit

The events in the capture are **finger taps on a benchtop**, not an athlete
driving out of blocks. They establish the noise floor, expose the timing
problem and reveal the magnitude-detector trap — all properties of the hardware
and data path, all of which transfer. They do **not** validate the threshold
against real start dynamics, or show whether pre-start fidgeting false-triggers.
The next capture worth taking is logged **on-device** at 416 Hz+, from a device
mounted where it will actually live.

The photo-finish clock-synchronization figures in `playground_IMU/README.md`
are estimates from known BLE/WiFi behaviour, **not measured on this hardware**.

## Conventions

Commit messages in this repo carry **no Claude/Anthropic attribution trailers**
— no `Co-Authored-By: Claude`, no `Claude-Session:`. The four commits that had
them were rewritten on 2026-08-29 and force-pushed.

## State of the tree

The table below (through `0e701f8`) is from the 2026-08-29 work described in
this document's older sections and is committed and pushed on `main`. It
predates the `BLEtest.ino` FIFO+PLL+BLE rewrite (`5c2eaa1`..`1e86094`, also
committed and pushed) and the `AccelStream.ino`/`accel_live.py`/`csv_plot.py`
introduction (`a6e9787`, committed and pushed 2026-09-04).

The folder reorganization described in the top section of this file
(`data`→`Data`, `tools`→`Tools`, `docs`→`Docs`, `prostart`→`Flutter App/prostart`,
plus deleting `Arduino/BLEtest/`, `Arduino/HighFrequencySampleRate/`,
`tools/kinestart_live.py`, and `playground_IMU/`) was made **after** `a6e9787`
and its commit/push status should be checked with `git status` / `git log`
rather than assumed from this document — it may or may not be pushed by the
time you're reading this.

| Commit | What |
|---|---|
| `de65ad6` | Live IMU data view, connection moved to Settings |
| `51900e9` | I2C_Scanner Serial fix, README paths, nominal-rate label |
| `56e90cb` | CSV export fix (`path_provider_foundation` pin) |
| `4e8d220` | IMU evaluation (`playground_IMU/`) |
| `0e701f8` | System overview diagram in the README |
| `5c2eaa1`..`1e86094` | `BLEtest.ino` FIFO+PLL+BLE rewrite, `kinestart_live.py` |
| `a6e9787` | `AccelStream.ino` no-BLE pipeline, `accel_live.py`, `csv_plot.py` |

Hashes changed in the 2026-08-29 history rewrite; anything referencing the old
`37060b5` means `de65ad6`.

The 2026-09-07 session (see **READ THIS FIRST** at the top of this document) added
one more commit on top of all the above: `AccelStream.ino` burst-I2C-read + 1660Hz
config (~977Hz achieved) + buffer-then-dump protocol + o/s/g/S ground-truth markers,
`accel_live.py` rewritten to match (+ matplotlib keymap fix, diagnostic logging),
and two new scripts, `Tools/verify_rate.py` and `Tools/detect_pushoff.py`. Check
`git log` for the exact hash rather than assuming one here.
