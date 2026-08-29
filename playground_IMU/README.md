# playground_IMU — IMU evaluation for false-start detection

Analysis of a real accelerometer capture from the Prostart prototype, and what
it implies for the reaction-time and photo-finish architecture.

| File | What it is |
|---|---|
| `imu_evaluation.ipynb` | The analysis — 6 figures, executed, outputs included |
| `prostart_imu_20260829_213017.csv` | The raw capture: 700 samples, 14.3 s, streamed over BLE |

Hardware: **Seeed XIAO nRF52840 Sense**, onboard **LSM6DS3** at I2C `0x6A`,
`accelRange = 4 g`, `accelSampleRate = 104 Hz`, gyro disabled.

---

## 1. What the data says

### The sensor is not the problem

| Measurement | Value | Meaning |
|---|---|---|
| Noise floor at rest (1σ) | **0.65 mg** | Exceptionally quiet |
| Resting vector magnitude | 0.991 g | Scaling correct, axes sane |
| Peak of a light finger tap | 196 mg | **300× the noise** |
| Expected peak of a real block push-off | 1–3 g | **1500–4600× the noise** |

A detection threshold at 20 mg sits ~30σ above the noise — immune to false
triggering — while a genuine start exceeds it by a factor of 50. Detecting
*that* a push-off happened is close to trivial for this sensor.

### The BLE stream's timestamps are not usable for timing

Sample arrival intervals are **bimodal, and the nominal 20 ms bin is empty**:

- 30% of samples arrive with *no measurable gap* from the previous one
- the rest cluster around 29.7 ms
- worst-case gap: 294 ms

This is BLE connection-event batching. The firmware really does sample at
50 Hz, but the radio delivers in bursts of one or two, and the app timestamps
each sample **on arrival**. Two readings taken 20 ms apart on the device end up
with near-identical timestamps in the CSV.

> The timestamps in the CSV describe when the *phone received a packet*, not
> when the *sensor was read*. This is a systematic ±30 ms distortion, not noise
> around a correct value.

**This does not affect the reaction-time design** — see section 2, where it
disappears entirely. It only means the live stream is a visualization tool.

### Detect per-axis, not on |a| — and the push is directional

The capture contains three taps. The third (at 11.0 s) was almost purely
lateral, and a magnitude-based detector nearly missed it:

| Detector | Vertical taps | **Lateral tap** |
|---|---|---|
| Vector magnitude \|a\| | 164–169 mg | **46 mg** |
| Worst single axis | 128–165 mg | **196 mg** |
| **Horizontal-plane** √(Δx²+Δy²) | 128–165 mg | **196 mg** |

The device rests with gravity on Z, so |a| ≈ 1 g and a horizontal acceleration
adds *in quadrature* rather than linearly:

```
|a| = sqrt( (1 g)² + a_h² )  ≈  1 g + a_h²/2
```

For small horizontal accelerations that quadratic term is tiny, so |a| barely
moves — it suppressed the lateral event by **4.2×**.

This matters because **the push-off is directional**. The athlete drives
backward against the blocks, so the reaction impulse on the block body is
predominantly *horizontal and along the track axis* — precisely the direction
|a| suppresses. A threshold tuned on |a| using vertical bench taps would be
badly miscalibrated for the real motion.

**Recommended detector: horizontal-plane magnitude** `sqrt(Δx² + Δy²)`, with Z
the vertical axis and Δ measured against a resting baseline. Measured on this
capture it keeps full sensitivity to the lateral event (196 mg) *and* to the
vertical ones (128–165 mg), on a noise floor of ~1 mg. It is also orientation-
independent within the mounting plane, so it does not depend on getting the
device's yaw right when strapping it to the block.

### Scope limit

The three events here are **finger taps on a benchtop**, not an athlete driving
out of blocks. They establish the noise floor, expose the timing problem, and
reveal the magnitude-detector trap — all properties of the hardware and data
path, and all of which transfer. They do **not** validate the threshold against
real start dynamics, or tell us whether pre-start fidgeting causes false
triggers. The next measurement worth taking is a capture logged **on-device**
at 416 Hz+, from a device mounted where it will actually live.

---

## 2. Reaction time with everything wired on the device

**Question:** the start tone and the movement detection are both wired to the
same circuit, the reaction time is computed on-device, and only the result is
sent to the phone. Is the ±30 ms BLE problem an issue?

**No — this architecture removes it completely.**

Reaction time is a **difference between two instants measured by the same
clock**: the MCU emits the tone and the MCU detects the push-off, both read from
the same `micros()` counter. In a subtraction, everything common to both terms
cancels — the absolute time, any offset from the phone's clock, and above all
**how long the result takes to arrive**. A packet that shows up 500 ms late
still carries a number that is correct to the microsecond.

Transmit a *number*, not a waveform, and the radio stops mattering.

### The error budget — and why the earlier table was misleading

The table in the previous answer mixed two kinds of error that behave
completely differently, which made the total look far worse than it is. The
distinction is the whole point:

- **Systematic** errors are the same every time. You measure them **once** and
  subtract them. After calibration only their *variation* remains.
- **Random** errors differ shot to shot. They cannot be calibrated away, and
  they combine in quadrature (√ of the sum of squares), not by addition.

| Source | Raw | Kind | After treatment |
|---|---|---|---|
| Speaker/amplifier audio latency | 5–20 ms | **Systematic** | **< 0.5 ms** |
| Threshold-crossing lag | 1–3 ms | Mostly systematic | ~1 ms residual |
| IMU quantization @ 104 Hz | σ = 2.8 ms | Random | — |
| IMU quantization @ 833 Hz | σ = 0.35 ms | Random | 0.35 ms |

**Total after calibration: σ ≈ √(0.5² + 1² + 0.35²) ≈ 1.2 ms.**

So: no, it is not a huge error. The 5–20 ms figure that looked alarming is the
*uncalibrated* audio latency, and it is the most calibratable term in the whole
system. What matters is that you must actually do the calibration.

### The one thing you must not skip

The "go" timestamp must mark **when sound leaves the speaker**, not when the
code called `play()`. Between the two sit the DAC, the amplifier and the
physical inertia of the membrane — milliseconds, and always in the same
direction. Skip this and *every* reaction time you report is biased by the same
5–20 ms, which is 5–20% of the false-start threshold.

It is deterministic on a microcontroller (unlike on a phone, where the audio
stack is not), so a single calibration holds:

1. Put a microphone next to the speaker.
2. Record the GPIO that marks `play()` and the microphone on the same scope
   or the same audio interface.
3. Measure the delay to the first pressure wave. Subtract it in firmware.

Re-check it if the amplifier, speaker or sample rate changes.

### Firmware changes this implies

1. **Raise `accelSampleRate` from 104 Hz** to 416 or 833 Hz for the detection
   path. At 104 Hz quantization alone is σ = 2.8 ms; at 833 Hz it is 0.35 ms.
   The gyro is already disabled, so the I2C budget is there.
2. **Trigger on `sqrt(Δx² + Δy²)`**, not on `|a|`, not on a single axis.
3. **Threshold ~20 mg** against a resting baseline captured at "on your marks".
4. **Calibrate the audio latency** and subtract it from the "go" timestamp.
5. Keep the live BLE stream exactly as it is — it is visualization only.

---

## 3. Photo-finish: synchronizing the phone and MCU clocks

**Question:** clock synchronization looks like the only workable option. What
error does it give?

Agreed that it is the practical choice. This is a genuinely different problem
from section 2: there you took a **difference on one clock**, so offsets
cancelled. Here you need an **absolute alignment between two clocks**, and
offsets do not cancel — they have to be estimated, and the estimate is limited
by exactly the radio jitter you escaped before.

### Expected error

Estimates from known BLE/WiFi behaviour — **not measured on this hardware**:

| Transport | Sync error | Why |
|---|---|---|
| BLE @ 30 ms connection interval | **5–15 ms** | Everything quantizes to the connection interval |
| BLE @ 7.5 ms connection interval | 2–4 ms | Best case BLE, and the central may refuse it |
| **WiFi UDP, local link** | **1–3 ms** | The recommended path |

Plus clock **drift**: MCU crystals run ±20–50 ppm, phones ±10–20 ppm. A 50 ppm
relative error is 50 µs per second — about **3 ms per minute**. Over a session
this dominates unless you handle it.

### How to actually get 1–3 ms

1. **Use WiFi UDP, not BLE**, for the sync exchange. BLE's connection interval
   puts a floor under the error that no amount of averaging removes.
2. **Cristian's algorithm with min-RTT filtering** (this is what NTP does). Do
   ~20 round trips; keep only the exchanges with the *smallest* RTT. A fast
   packet is one that queued the least, so it is also the least asymmetric —
   and path asymmetry, not RTT itself, is what limits the estimate.
3. **Estimate drift, don't just offset.** Sync repeatedly over ~30 s and fit a
   straight line to (offset vs. time). The slope is the relative clock rate;
   correcting it turns 3 ms/minute of drift into a stable offset. Re-sync
   before each race regardless.
4. **Use monotonic clocks on both ends.** On the phone, wall-clock time can jump
   (NTP corrections, timezone, user changes). In Dart, take deltas from a
   `Stopwatch` rather than `DateTime.now()`.
5. **Mind the camera's time base.** Video frames carry presentation timestamps
   from the platform's own clock (`mach_absolute_time` on iOS,
   `SystemClock.elapsedRealtimeNanos` on Android). You need the offset between
   *that* clock and your sync clock — not between the sync clock and wall time.
   This is a common and easily missed source of error.

### Why 1–3 ms is good enough

Video at 60 fps has one frame every **16.7 ms**. Even with the sub-frame
interpolation planned for Phase 2, the crossing instant will realistically not
be better than 2–5 ms. A 1–3 ms sync error is therefore **not the bottleneck**
— the camera is. Chasing microsecond synchronization here would be wasted
effort; getting the drift handling right would not.

### An open design question

Do the reaction time and the photo-finish need to share **one** timeline
("reaction 142 ms *and* total 10.84 s" as a single coherent measurement), or
are they two independent numbers displayed side by side?

If independent, each lives in its own clock and the synchronization problem
largely disappears. Worth settling before building the sync layer.

---

## Verdict

**The LSM6DS3 is the right sensor. The BLE stream is not a measurement channel.**

Both statements are true at once, and keeping them apart is the useful outcome:

| | Finding | Verdict |
|---|---|---|
| Noise floor | 0.65 mg (1σ) | Excellent |
| Peak detection | 300× SNR on light taps | Trivially detectable |
| Axis scaling | 0.991 g at rest | Correct |
| Detector choice | \|a\| under-reports lateral events 4.2× | Use horizontal-plane magnitude |
| BLE stream timing | ±30 ms, batched | Visualization only |
| On-device reaction time | σ ≈ 1.2 ms after calibration | **Fit for a 100 ms rule** |
| Phone↔MCU clock sync | 1–3 ms over WiFi | Below the video's own limit |
