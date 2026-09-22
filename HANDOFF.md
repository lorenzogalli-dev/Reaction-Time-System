# HANDOFF — Prostart live IMU data view & sensor evaluation

Last updated: 2026-09-22. Written for an agent starting with no prior context.
Sections are newest first.

## READ THIS FIRST — 2026-09-22 (evening): the half-time paper checked line by line against the data, and the threshold sweep is finally a committed script

The paper draft was read against `Data/`, the firmware and this file, every
number re-derived rather than trusted. **The primary result holds.** Most of
the rest of the evaluation section did not, and the failure is always the same
shape: a number that was true of *one session* or *one recording* written as if
it were true of the whole set.

New in the repo, and the reason none of this has to be re-derived again:
`Arduino/AlgorithmRealTime/Python_Tools/sweep_threshold.py`. One command, ~55 s,
and it prints the sweep table, the captures the picker cannot save, and writes
`Docs/threshold_sweep.pdf`. This closes the open item that has been carried
since 09-21, and the same one `bench_140926` died of.

### What held

Sample rate 865.8 Hz, one sample 1.155 ms, clock 8 µs, jitter 334 µs, 0 dropped
and 0 truncated rows across all 83 captures. Every detector constant quoted in
the paper matches `StartDetector.h`: STA/LTA 15/800 ms, ratio 6, floor 20 mg,
AIC window 150+50 ms, stillness 15 mg for 200 ms, go 0.7-1.5 s after arming,
false start under 100 ms. The figure's capture is `150926/183435`: AIC 138.0 ms,
threshold-only 154.2 ms, **16.2 ms** apart, exactly as printed.

### What did not

- **"Over 45 starts the unit reports 40 valid" is one session of four.** Those
  are the 14/09 numbers. Across the four sessions it is 79 attempts with an
  event, 72 valid, 7 false starts, median 154.0 ms.
- **147.0 ms is the offline detector's median, not the board's.** The board's
  own verdicts on the same 45 give **147.8 ms**. `Data/README.md` has the same
  conflation and should be fixed with it.
- **"(19 starts)" and "(8 starts)" in the stimulus comparison are attempts,
  not starts.** The valid starts behind 178 ms and 159 ms are **15** and **6**.
- **The 29 ms threshold spread is a single 08-09 recording** (`INFO.md` §4),
  not the block starts the sentence opens on. The 79-capture sweep replaces it.
- **The ramp-steepness claim was inverted.** The threshold's delay is about
  threshold ÷ slope, so it is *largest* on the gentlest push-off: 20/1.25 ≈
  13 ms against 20/3.49 ≈ 5.8 ms. The draft said it grew with steepness.
- **The gate applies to 56 of the 79 starts.** 09-09 and 11-09 were recorded
  before it existed, with "go" on a blind random after "set".
- **"No latency enters" is only true of the host and the serial path.** The
  buzzer's acoustic latency, 5-20 ms and still unmeasured, enters every number.
- **Figure 1 is the old system.** Zigbee, two XIAO blocks, an OLED, an 18650,
  BLE to the phone - none of which is the architecture in the text, and the
  conclusions still say "bring up the Zigbee link". Redraw it around the
  ESP-NOW long-range path, and say *planned*: the coexistence test in the
  section above has not been run.
- **"Five sprinters" cannot be checked against this repo.** `Data/README.md`
  says "a real athlete" and then "same athlete" twice. One of the two documents
  is wrong and it is not obvious which.

### The four captures that produce no event, opened one at a time

They had been dropped from 83 to 79 without anyone looking. They are not one
phenomenon:

| capture | what happened | a start? |
|---|---|---|
| `090926/133119` | **`micros()` wrapped between "set" and "go"** - set at 4294.11 s, go at 1.60 s, so the header reads go 4289 s *before* set and the gate never opens. A 4.1 g push-off sits in the file. | **yes**: unwrapped by hand it is a valid start at **175.6 ms** |
| `090926/123532` | 3.7 g **125 ms before "go"**. The gate, applied retrospectively to a pre-gate session, opens 736 ms *after* go and never sees it. | **yes**, an anticipation |
| `090926/185706` | peak 9 mg in the second after go - noise | no push-off in the window |
| `110926/185009` | peak 50 mg, against 1000-3600 mg for a real push-off | no push-off in the window |

`micros()` wraps every 71.6 minutes from power-up, so this will happen again in
any session that runs that long. It was already an open item; it is now known to
have cost a real start. The fix belongs in the reader, not in the CSVs.

**Decided for the paper: keep 79.** The two recoverable ones are excluded with
a stated reason rather than repaired, so the denominator matches every other
number in the draft. If the wrap is fixed in `start_detector.py` later, it
becomes 80 and the 09-09 row becomes 16 valid, 1 false start.

### The sweep, re-run at 17 thresholds instead of 4

`sweep_threshold.py --step 2.5`, 10-50 mg, first event in the judged window,
shift measured against the 10 mg value:

| | AIC picker | threshold only |
|---|---|---|
| median shift over the sweep | **0.00 ms** | **9.28 ms** |
| p90 | 0.00 ms | 21.60 ms |
| worst | 1.16 ms | 52.08 ms |
| median at 50 mg | 0.00 ms | 9.28 ms |

**The 9.3 in the abstract and the 9.3 at the right-hand edge of the figure are
two different statistics** - median of the per-capture maximum, and median of
the shift at 50 mg - that happen to agree to two digits. Do not let the paper
imply one is the other.

**Sorting the captures by how far the picker's onset moves gives a clean gap:**
zeros, a cluster at one sample (1.14-1.16 ms), then **nothing until 18.5 ms**.
The five above it are the event-*selection* changes already tabled in the 09-21
section. The script splits on 10 ms, anywhere in the gap, so the 74/5 split is
not a judgement call.

**One number is grid-dependent and the paper should stop quoting it alone:**
"identical" was 70/79 over four thresholds and is 69/79 over seventeen, because
a finer sweep gives more chances to land on the next sample. **74/79 within one
sample does not move.** Quote 74, or say "over the four thresholds swept".

### The figure

`Docs/threshold_sweep.pdf`, 3.4 x 2.3 in, i.e. one column at `\linewidth` with
no rescaling. Median and interquartile range over the 74, blue flat at zero with
a zero-width band, red climbing in visible 1.2 ms steps because that is the
sample period. Bands, the ±1 sample grey stripe and the 10 mg reference are all
named *on the figure*, after a reviewer asked what the shaded areas were.

### Open

- **`Data/README.md` says 147.0 ms for the 14/09 median**, which is the offline
  detector's; the board says 147.8. Same conflation as the paper's.
- **The `micros()` wrap is unfixed** in `capture.py` and `start_detector.py`.
- **How many athletes.** Nobody has written down who ran which session.
- **The two captures with no push-off in the recorded second** are still
  unexplained: no start, or a start later than go+1 s, where the window ends.
- Everything in the 09-21 and 09-15 sections is still open, in particular the
  board/Python AIC-input divergence, which the paper now has to declare.

---

## READ THIS FIRST — 2026-09-22 (later): how the t0 reaches the phone, five architectures weighed

Written straight after the field measurements in the section below, working out
what to do about 60-70 m against a 100 m requirement **that may become 150 m**.
Nothing here is built yet. The conclusion is that the deferred-sync design
should be written first **whatever else gets chosen**, because every other
option needs it underneath as a fallback.

Recap of what the link is for, because it governs everything: it carries the
**t0 instant to the phone filming the photofinish**, not the reaction time,
which is computed on the XIAO and stays there. **Latency is therefore free** -
the message carries its own timestamp - and the only things that matter are
that it arrives and that the clocks can be related.

### A. One module, better radio - the incremental path

The gap from the measured 70 m to 100 m is **3-4 dB**. Available:

| | |
|---|---:|
| 802.11b only (staged, unmeasured) | 3-6 dB |
| external u.FL antenna | 3-5 dB |
| antenna out of the block's shadow | a few dB |
| t0 retransmitted until acked (staged) | removes the athlete's 10-20 dB |

About ten dB against three or four needed, and half of it free. **This very
likely reaches 100 m. It does not reach 150 m**, which is another 3.5 dB on top
and would put the single-module path past its limit.

**Regulatory ceiling, and a distinction that was initially confused:** EU is
100 mW EIRP and the board already transmits at 20 dBm, so a 5 dBi antenna is
out of spec *in transmit* and TX power has to come down by the antenna gain.
The **receive** gain is free and unregulated, and the phone-to-board direction
is likely the weaker of the two. Note this ceiling is about **antenna gain**,
not about long-range modulation - see E, which does not raise power at all.

### B. An athlete-worn relay - rejected as specified, viable in principle

The idea: the athlete wears a device that receives the t0 at the blocks (zero
metres, they are crouched on it) and carries it to the finish (zero metres
again, they cross right in front of the phone). Both hops are at contact range,
so the distance problem disappears by construction. The intuition is sound.

**The KKM W52 bracelet cannot do it.** From the manufacturer's page: nRF52
series, **advertisement only, no scanning or receiving**, `Sensor: N/A`, no RTC,
CR2032, 1000 ms default broadcast interval. It shouts an identifier and nothing
else. The nRF52 inside could be reprogrammed but KKM publishes no SDK, and it
is a sealed coin-cell bracelet.

**The flaw in the idea as first stated:** a courier carries a *number*, not a
*clock*. The t0 is in the start unit's time base, and in that design the phone
never talks to the start unit at all, so it has nothing to map it onto. The fix
is to carry **elapsed time** ("t0 was 11.4 s ago") rather than the raw t0 -
which needs the courier to have a clock and to receive, i.e. to be a
programmable device, not a beacon.

Done properly - a real two-way sync at both ends rather than a one-shot
advertisement - the error lands around **3-4 ms**, set by the BLE connection
interval, which is comparable to the Wi-Fi link and under the frame budget. A
XIAO nRF52840 with a 100-150 mAh LiPo is about 30 x 25 x 10 mm and runs 15+
hours on the Arduino core; the built-in BQ25101 charges it over the same USB-C.
Worn at the shorts waistband, where athletes already carry timing chips, rather
than as a bracelet.

**Why it is still not the choice:** a third device to charge, distribute and
wear, competition rules that often forbid it, and - decisively - **no t0 if the
athlete does not finish**. False start, pull-up, injury: exactly the attempts
worth looking at are the ones that produce no data.

### C. The phone's own microphone - ruled out by the athlete

The phone records audio on the same time base as the video, so the buzzer in
the audio track would anchor t0 at audio resolution (0.02 ms at 48 kHz) with no
radio at all, correcting for the 291 ms flight time over 100 m. Residual error
would be 3-5 ms: temperature unknown to ±5 °C (±2.5 ms), 2 m/s wind (±1.7 ms),
distance to ±0.5 m (±1.5 ms).

**Ruled out: the buzzer is not audible at that distance.** Left on record
because it is the cheapest option by a wide margin and would come back if the
start signal ever gets louder, or if a matched filter on the known waveform is
ever tried - it can recover a tone well below audibility.

### D. Deferred sync, store and forward - build this first

The start unit stores each t0 in its own clock with an identifier; the phone
stores each finish event in its own clock; whenever the two are near each other
they connect, sync, and the results resolve. No range requirement at all.

**This is the most accurate of the five**, not a compromise: interpolating
between two syncs over a two-minute rep leaves **~1.3 ms** at the measured
11 ppm drift, against 2.5-4 ms for any live link. What it gives up is the
number being visible at the finish while the athlete is still breathing.

**Build it regardless of what else is chosen.** Even with a perfect radio link,
sooner or later a packet does not arrive, and without store-and-forward that
rep is gone - and an athlete's rep cannot be repeated. It is the one piece of
code that is not at risk of being thrown away.

#### Clock design, in detail, because three traps live here

**The board's clock resets on boot.** `micros()` counts from power-up. Every
record therefore needs a **boot-session id**, incremented in flash at each
start. The rule that follows: *a t0 is exactly mappable only if it lies between
two syncs of the same boot session.* One with no sync after it can only be
extrapolated. In practice, sync at power-up and every time the coach walks past
the blocks, which happens naturally between reps.

**The phone must use a monotonic clock, and the right one.** Not `Date.now()`
or wall time - those jump on NTP correction, timezone change or a user edit,
and would silently destroy the interpolation. On iOS `mach_absolute_time` stops
while the phone sleeps, so **`mach_continuous_time`**; on Android
**`CLOCK_BOOTTIME`**, not `CLOCK_MONOTONIC`. The phone does sleep in a pocket
between reps, so this is not theoretical.

**Do not stop the recording to mark the finish.** The stop has its own variable
latency and throws away the ability to review frames. Keep the camera running
and take the **frame's own timestamp**: `CMSampleBuffer` presentation timestamp
on iOS, camera2 `SENSOR_TIMESTAMP` on Android. Android caveat: that timestamp's
base varies by device - read `SENSOR_INFO_TIMESTAMP_SOURCE`, and if it reports
`UNKNOWN` you do not know what it is anchored to. Must be checked on the handset
models actually used.

### E. Two ESP32s, ESP-NOW long range - the live-numbers path

ESP-A at the blocks, wired to the XIAO and sharing its power; ESP-B at the
finish beside the phone; `WIFI_PROTOCOL_LR` between them. Supported on ESP32,
S2, S3 and **C3**, so the SuperMini already in hand qualifies; Espressif claims
up to a kilometre line of sight. **Long range does not raise transmit power** -
the gain comes from coding, i.e. in receive sensitivity, which is unregulated.
Two lines of code, same core, nothing new to learn.

**The clocks get easier, not harder**, despite three hops instead of one:

| hop | mechanism | error |
|---|---|---:|
| XIAO → ESP-A | **a wire**: XIAO raises a pin at t0, ESP-A captures the edge in a hardware interrupt | sub-µs |
| ESP-A → ESP-B | two bare-metal MCUs, no OS scheduling, no TCP, no association | sub-ms expected |
| ESP-B → phone | Wi-Fi at one metre, the case already measured | 2.5 ms bound, 0.29 ms residual |

The hard hop becomes a wire and the fragile hop is never more than a metre away.
Total is still set by the phone, ~1-2 ms.

**The single technical risk, and it should be tested before anything is built:**
ESP-B has **one 2.4 GHz radio** and must simultaneously run the LR link to ESP-A
and an access point for the phone. A phone cannot associate to an LR-only AP,
so this means STA in LR for ESP-NOW and AP in b/g/n for the phone, **on the same
channel**. Espressif documents per-interface protocol settings, but this exact
combination is the kind that either works in an afternoon or costs a week.
**Unverified.** Half the answer is available with a single module: bring up AP
and LR together and see whether the radio survives and the phone associates.

**If it fails, the fallback is BLE to the phone**, which is workable - a real
connection with a two-way exchange, not the one-shot advertisement that sank
option B - at **3-8 ms**, set by the ~15 ms connection interval iOS negotiates
and CoreBluetooth callback jitter. Fine at 30 fps, marginal at 60. But note it
**moves the radio conflict rather than removing it**: two stacks now time-share
one antenna, with Wi-Fi/BT coexistence added on top of long range, and the
sketch is already at 79% flash with Wi-Fi alone.

**Practical asymmetry worth exploiting:** the finish unit has no size or
placement constraints - tripod or bag, not a sealed box behind a block with an
athlete on top. Put the external antenna and the height *there*. Antenna gain at
one end improves **both** directions of the link, so it is the most efficient
place to spend it, and the block-side unit can stay as it is.

**Powering ESP-B from the phone's USB-C** would probably work electrically on
iPhone 15+ and any USB-C Android, but it drains the phone while it films - the
heaviest thing a phone does - and a cable hanging off a tripod-mounted phone is
the most fragile part of the system. Unnecessary anyway: ESP-B needs to be
*near* the phone, not attached. Give it its own cell.

### Zigbee and sub-GHz, considered and set aside

**Zigbee** is still 2.4 GHz. Its sensitivity is better than plain Wi-Fi (around
−100 dBm against −90), but long-range Wi-Fi already closes that gap, and **the
phone does not speak Zigbee**, so it needs a module at the finish anyway - the
same device count as E, with an unfamiliar stack and unfamiliar hardware. No
reason to prefer it.

**Sub-GHz LoRa at 868 MHz** is the genuinely stronger radio: kilometres, and far
better through obstacles. Seeed's `Wio-SX1262` plugs into the XIAO form factor,
so it stays in the existing ecosystem. Worth revisiting only if the requirement
goes past ~500 m; it is more work than ESP-NOW LR for range that is not needed.

### The error budget, and why most of it does not matter for reps

For a 100 m, with the deferred-sync or the two-ESP design (they differ by about
a millisecond):

| source | type | 30 fps | 60 fps |
|---|---|---:|---:|
| **which frame the torso crosses in** | random | **9.6 ms** | **4.8 ms** |
| rolling shutter, uncorrected | systematic | 10-30 ms | 10-30 ms |
| t0 on the board (already in the budget) | random | 1-2 ms | 1-2 ms |
| sync path asymmetry | systematic | 0.5-2 ms | 0.5-2 ms |
| drift interpolation residual (10 min bracket) | systematic | 0.4 ms | 0.4 ms |
| sync random residual (min-delay, 60 samples) | random | 0.3 ms | 0.3 ms |

**The synchronisation is not the problem** - a couple of ms out of ten. The
camera is, exactly as the measurements in the section below concluded.

**Rolling shutter is the largest line and had not been counted before.** The
sensor reads the image row by row over 10-30 ms, so a torso crossing at
mid-height was read many ms after the frame's timestamp. It is systematic and
**correctable** given the readout time and the row the finish line sits on.

**Frame quantisation is beatable by interpolating.** At 10 m/s the torso moves
33 cm per frame; tracking its position across two consecutive frames and
interpolating onto the line gets to roughly a fifth of a frame, under 7 ms even
at 30 fps.

**One calibration removes every systematic term at once:** the start unit lights
an LED at a known instant of its own clock while the phone films it. Which frame,
and which row, the LED appears in gives the difference between what the board
says and what the phone sees, **through the whole real chain** - sync asymmetry,
audio/video offset, rolling shutter, all together. Done once per handset model,
stored, subtracted thereafter.

**And for training reps it matters less than it looks.** What is compared is one
attempt against the next, same athlete, same setup, so **every systematic term
cancels**. The limit on that comparison is the random part alone: **~10 ms at
30 fps, ~5 at 60**, less with interpolation. The LED calibration is only needed
for absolute times comparable with official timing.

### Open, in the order it should be done

1. **Test the ESP-B radio coexistence** (AP for the phone + LR simultaneously,
   same channel, one module). One afternoon, no build required, and it decides
   whether E is available at all.
2. **Write the deferred-sync design (D)**, with the boot-session id and the
   monotonic-clock rules above. Needed under every other option.
3. **Three or four repeats of the same walk** with the staged 802.11b and t0
   retransmission, to see what A is actually worth. Two measurements of the same
   thing differed by 8 dB, which is the size of the whole gain being chased.
4. **Measure the rolling shutter and the frame-timestamp offset** on the handsets
   that will be used. Currently unmeasured and the largest single line in the
   budget.
5. **2.4 GHz congestion in a stadium** - every number so far was taken on empty
   air, and a meet is the worst RF environment this will ever see.

---

## READ THIS FIRST — 2026-09-22: the Wi-Fi t0 link measured in the field, 60-70 m against a 100 m requirement

First real measurements of the phone link, on an ESP32-C3 SuperMini outdoors,
sitting on the ground the way it will sit behind the block. The rig is now in
the repo as `Arduino/WifiFieldTest/`, which **replaces `Arduino/WifiRangeTest/`**
- that one was written, never compiled, and has been deleted.

### What this link is for, because it changes how every number below is read

It does **not** carry the reaction time. That is computed on the XIAO and stays
there. This link carries the **t0 instant to the phone**, which films the
photofinish and has to anchor the video to it. Two consequences:

- **Latency does not matter.** The message carries its own timestamp, not a
  "now". A t0 that arrives two seconds late with the right instant is still
  usable. What matters is that it arrives, and that the clocks are aligned.
- **The phone is at the finish line**, because that is where it films, and the
  blocks are at the start. The 100 m requirement is geometry, not performance.

### The range result

From `measurements/20260922/03_full-run-70m.csv`, the one usable run (69 s,
markers every 10 m, phone in hand, board on the ground):

| dist | RSSI med | pings ok | lost | RTT med |
|---:|---:|---:|---:|---:|
| 0 m | −57 | 69 | 0 | 9 ms |
| 10 m | −73 | 26 | 0 | 11 ms |
| 20 m | −78 | 26 | 0 | 12 ms |
| 30 m | −83 | 22 | 0 | 422 ms |
| 40 m | −85 | 1 | 17 | - |
| 50 m | −88 | 33 | 1 | 25 ms |
| 60 m | −90 | 11 | 8 | 18 ms |
| 70 m | −91 | 0 | 10 | dead |

**The 30-40 m rows are not a distance effect.** The WebSocket had gone zombie -
TCP never closed, pings kept queueing - and the run recovered at 50 m only
because the client watchdog closed and reopened the socket (`drop` then `open`,
four seconds apart, both at 50 m). Read the table as: clean to ~50 m, losses
from 60, **dead at 70 m at −91 dBm**.

Slope is **~21 dB/decade** between 10 and 70 m, i.e. essentially free space.
Lying on the ground is *not* costing a worse propagation exponent, which also
means **raising it off the ground buys less than expected**. Extrapolating,
100 m lands near **−94 dBm**: 3-4 dB short of where the link died, 8-10 dB
short of having any margin.

**Run-to-run spread is the size of the fix.** `02_short-run-30m.csv`, taken
minutes earlier on the same ground, reads **−81 dBm at 10 m** against **−73**
in the long run. Eight dB between two measurements of the same thing, which is
as large as the entire gain being chased. No single curve should be trusted;
three or four repeats of the same walk are needed before spending money on a
module.

### The clock, and why the frame rate is the real bottleneck

| | |
|---|---:|
| sync jitter at 0 m | 2.50 ms |
| sync jitter at 60 m | 4.00 ms |
| ESP vs phone clock drift | **+11 ppm** (6.7 ms per 10 min) |
| offset scatter, drift removed | 0.29 ms sd |
| t0 latency, healthy link | median 8.1 ms, min 3.1 ms |

Against a 30 fps video the frame period is 33.3 ms, so the quantisation error
is ±16.7 ms worst case and **9.6 ms sd**. Summed in quadrature with the link's
≤4 ms, the network adds **8%**. The bottleneck is the camera, not the radio.

The link only starts to matter at **120 fps**, where the frame is 8.3 ms
(2.4 ms sd) and the jitter becomes the dominant term. And the **+11 ppm drift**
is a fifth of a frame at 30 fps but a frame and a half at 240 fps, so a one-off
sync is not enough at any rate - the page resyncs continuously.

Untested and probably larger than any of this at higher frame rates: **rolling
shutter** (the sensor scans a frame over 10-30 ms, so head and feet in the same
frame are not the same instant) and the accuracy of the timestamp the phone
attaches to each frame.

### What was wrong with the first run, and what got fixed

`01_rssi-broken.csv` is kept only as a record. Three instrumentation defects,
all now fixed:

- **RSSI was 0 in all 306 rows.** The firmware read it and printed it to
  serial but never sent it to the page. The single most important variable was
  missing from the first session entirely. It now rides along with every ping
  reply, plus a broadcast every second.
- **RTTs up to 26 s.** Not latency: queueing. Each ping is now tracked
  individually, unanswered after 3 s counts as lost, and six in a row closes
  the socket so the reconnect is counted instead of hanging.
- **Lost pings were invisible.** Only replies were logged, so the actual
  degradation signal was absent. Losses are now their own rows, with distance.

Also fixed after the first outing: the log now lives in the phone's storage and
survives drops, reloads and OS-killed tabs, and the firmware boots with the
captive portal off, because the portal window is closed by the OS when the
network drops and took the log with it.

### Three changes staged for the next session, not yet measured

- **t0 retransmitted until acknowledged**, every 250 ms for up to 15 s. At the
  instant of t0 the athlete is crouched on the blocks, directly on the line of
  sight to the finish, and a human body at close range costs 10-20 dB - more
  than any module change would recover. Since latency is free, the fix is to
  repeat until the line clears. The CSV now carries a `tries` column, which is
  the number to watch: t0s arriving on attempt 1 mean margin, attempt 12 means
  three seconds of living on repeats.
- **802.11b only**, giving up the fast modulations for a few dB of sensitivity;
  the payload is a few dozen bytes every five seconds. With an automatic
  fallback to b/g/n if no client associates within 90 s, so a phone that
  refuses to join cannot strand the test in the middle of a field.
- **Antenna out of the block's shadow.** Physical, not code. The retransmission
  covers the athlete, who leaves after a second; the block body does not.

### Open

- **The requirement may move to 150 m**, which none of the above reaches. The
  gap to 100 m is 3-4 dB and there are ~10 dB available between 802.11b, an
  external u.FL antenna and placement. 150 m is another 3.5 dB on top and would
  put the single-module path at its limit.
- **Regulatory ceiling.** EU is 100 mW EIRP, and the board already transmits at
  20 dBm. A 5 dBi antenna would be out of spec in transmit, so TX power has to
  come down by the antenna gain. The **receive** gain is free and unregulated,
  and the phone-to-board direction is likely the weaker of the two.
- **Two ESPs in ESP-NOW long-range mode** (`WIFI_PROTOCOL_LR`, Espressif-only,
  hundreds of metres to a kilometre) is the fallback that changes the order of
  magnitude, with one at the blocks and one beside the phone at the finish.
  Rejected for now: a second device to carry is a real product cost.
- **2.4 GHz congestion at a stadium** has not been measured at all. Every
  number here was taken on empty air.
- `capture.py`-style reproducibility: the analysis behind these tables was
  session scratch, not a committed script. Same failure mode as `bench_140926`
  and the threshold sweep in the 09-21 section.

---

## READ THIS FIRST — 2026-09-21: the threshold-independence claim, re-measured on all 83 block starts for the half-time paper

Written while checking one sentence drafted for the half-time paper's abstract
(the slot that has to "state your primary result"). The draft read:

> On 16 real block starts, a two-stage onset detector fixes the reported
> reaction time regardless of the detection threshold (0.0 ms spread over
> 10-50 mg, against 29 ms for a plain threshold crossing on a bench
> recording), with an estimated random error of 1-2 ms.

Three of its four claims did not survive the check. **The result itself is
real, and is stronger than the sentence claimed** - but the evidence was not
where the sentence said it was, and the claim was stated wider than the data
supports.

### What was wrong with it

**"16 real block starts" has no source anywhere in this repo.** The block-start
data is four sessions on real blocks with real athletes: 19 (`090926`) + 8
(`110926`) + 45 (`140926`) + 11 (`150926`) = **83 captures**, of which **79**
produce an event in the judged window. The draft understated its own evidence
by a factor of five.

**The figures in the parentheses were not block starts at all.** `0.0 ms
against 29 ms` is the table in `INFO.md` §4, measured on a *single* bench
recording from 2026-09-08. The sentence opened on real starts and then quoted
bench numbers, which is exactly the kind of seam a reviewer pulls on. It is
also unnecessary: the sweep runs on the real data now, see below.

**"regardless of the detection threshold" is too strong.** The picker makes the
*timing* of a detected onset threshold-free. It does not make *detection*
threshold-free, and on 5 of the 79 the threshold decides **which** event is
first, which the picker cannot repair because it never sees the event that was
missed.

Only the last claim held unchanged: **1-2 ms** is the random-error budget
already in this file (clock 8 µs, sample timing σ ≈ 334 µs, AIC ≈ 1 sample).

### The sweep, re-run on the real data

Method: for every capture in the four `Data/block_starts_*` directories,
`start_detector.analyse()` at `floor_mg` = 10, 20, 30, 50, once with
`use_aic=True` and once with `use_aic=False`, taking the **first event in the
judged window** - the one whose reaction time is the reported one. Where the
header carries the board's own `arm_t_s` (14/09 and 15/09) it is used, so the
arming gate stays fixed and the detection threshold is the only thing moving.
Measured ODR across the set: median **865.8 Hz**, so one sample = **1.155 ms**.

Sweeping 10 -> 50 mg, a five-fold change. The counts are over all 79; the
median, p90 and worst case are over the 74 where the same event stays first
throughout, the five exceptions being tabled separately below:

| spread of the reported reaction time | with the AIC picker | threshold only |
|---|---|---|
| identical to the sample (0.00 ms) | **70/79 (89%)** | 6/79 (8%) |
| within one sample (≤ 1.2 ms) | **74/79 (94%)** | - |
| median | **0.00 ms** | **9.3 ms** |
| p90 | 0.00 ms | 21.9 ms |
| worst case | 1.16 ms | **52.1 ms** |

With the picker in, the worst case over the whole five-fold sweep is one
sample. Without it, the median capture moves 9.3 ms and the worst moves 52 ms -
half a disqualification margin, from a parameter nobody can derive.

### The five the picker cannot save, and why they belong in the paper

These are event-*selection* changes, not onset changes: at a higher floor the
earlier, smaller event is never confirmed, so a later one becomes the first.

| capture | 10 mg | 20 mg | 30 mg | 50 mg | |
|---|---|---|---|---|---|
| `140926/172357` | −1294.7 | −1294.7 | −1294.7 | 197.0 | **verdict flips** |
| `140926/184348` | 49.8 | 49.8 | 49.8 | 165.6 | **verdict flips** |
| `140926/183204` | 213.1 | 213.1 | 213.1 | 231.6 | |
| `140926/185759` | 227.3 | 230.7 | 367.5 | 367.5 | |
| `150926/185751` | 119.3 | 119.3 | 119.3 | 157.6 | |

Two of them cross the 100 ms line, so the threshold, not the athlete, decides
false start vs valid start. `172357` is the same capture the 09-15 section
examines by hand and concludes the gate is marginal on; this is the second,
independent symptom of the same thing, and it strengthens that open item rather
than adding a new one.

### What the abstract can honestly say

Recommended, and the version the numbers above support line by line:

> Across 79 block starts recorded from several athletes, a two-stage onset
> detector makes the reported reaction time independent of the detection
> threshold: over a five-fold sweep (10-50 mg) the onset is unchanged to within
> one sample (1.2 ms) in 74 cases and identical in 70, against a median spread
> of 9.3 ms and a worst case of 52 ms for a plain threshold crossing on the
> same data, with an estimated random error of 1-2 ms.

**The caveat that has to stay visible:** this sweep is offline, run by
`start_detector.py`. The board's C++ agrees with it to the microsecond on 38/45
of the 14/09 captures but diverges on 7, up to 11.6 ms - the AIC-input
difference documented in the 09-15 section. So what is demonstrated is that
**the algorithm** is threshold-independent, not that the shipped firmware is.
Do not let the abstract blur the two; it is a sentence away from a claim the
repo contradicts.

### Open

- **The sweep scripts were session scratch and are not in the repo.** This is
  the `bench_140926` failure mode again: a number in a document with nothing
  behind it anyone can re-open, and this time it is going into a paper. It
  should be a committed script under `Arduino/AlgorithmRealTime/Python_Tools/`
  that prints the two tables above from `Data/` in one run.
- **4 captures produce no event at any threshold** (`090926/123532`,
  `090926/133119`, `090926/185706`, `110926/185009`) and were dropped from the
  79 without anyone looking at why. If any of them is a real start the detector
  missed, the paper's denominator is wrong and a miss rate belongs in it.
- Everything open in the 09-15 section is still open, and the board/Python
  divergence there is now load-bearing for the paper rather than a curiosity.

---

## READ THIS FIRST — 2026-09-15: 45 real starts through the gate, a board/Python divergence that is not float32, and the 09-14 bench proof is gone

The 09-14 section below documents the arm-on-stillness rewrite and says "no
reaction time from an athlete on blocks through this firmware" is the standing
open item. That happened the same evening, off the books: 45 attempts sitting
in `Arduino/AlgorithmRealTime/Python_Tools/Data/`, never mentioned in a commit
because `capture.py` had silently been writing them to the wrong place. Found,
reorganised and pushed as `c40c950`.

### The 45 captures, and why they were hiding

`capture.py --outdir` defaulted to the plain string `"Data"`, resolved against
whatever directory the script was launched from - not the repo root. Run from
inside `Python_Tools/` (as it evidently was, all evening), every capture landed
in `Python_Tools/Data/` instead of the top-level `Data/` every other tool and
`Data/README.md` assumes. Two files were visible at first glance; the other 43
were still in the same nested folder, timestamps running to 19:02.

All 45 are now `Data/block_starts_140926/` - a real athlete, real blocks, the
first live data through the finished gate:

| | |
|---|---|
| valid starts | 40, median **147.0 ms**, range 111.3-476.5 ms |
| false starts | 5 |

`capture.py`'s default is now computed from the script's own path
(`Path(__file__).resolve().parents[3] / "Data"`), so the destination no longer
depends on the current directory when it's launched.

### Old Python vs new Python: identical, as expected

Onset detection (STA/LTA + AIC) is the same code in both; only the gate
changed. Wherever both judge an attempt the reaction time is identical. The old
one refuses **15 of the 45** as `NOT JUDGEABLE` - the defect the 09-14 section
describes, now confirmed on data taken after the fix.

### Board vs `start_detector.py`: 38/45 identical to the µs, 7 not

Compared against the board's own `board_reaction_ms` in each CSV header
(`start_detector.py --cli`, board arming instant read from the header):

| capture | board | Python | gap |
|---|---|---|---|
| `175935` | 151.662 | 140.076 | **+11.6 ms** (10 samples) |
| `181304`, `182820`, `184139`, `184444`, `184622`, `185136` | | | **+1 sample** (~1.15 ms) each |

Every gap has the **board later**. Rounding noise would go both ways.

The colleague's explanation was float32 vs float64. Right order of magnitude -
any numeric difference either moves the AIC pick by a whole sample (1.2 ms) or
not at all, which is why most captures agree exactly - but **not the cause**.
Rebuilding the C++ input in Python and switching one difference on at a time:

| Python variant | matches board |
|---|---|
| as shipped (AIC on the detector's live-baseline trace) | 38/45 |
| **horiz recomputed with the baseline frozen at the candidate, as `AicPicker.h` does** | **44/45**, including the 11.6 ms one |
| + window selected on integer `t_us` + float32 signal | 44/45, no change |

So the divergence is **the AIC input signal**. `AicPicker.h`'s comment "the two
agree to 0.000000 ms, every time" was measured on the 09-09 captures and **does
not hold on the 09-14 set**. The remaining one (`184622`, 1 sample) is not
explained by any variant; candidates are the board's float32 raw->g path or the
CSV's 4-decimal (0.1 mg) rounding of `x_g/y_g/z_g`. Not proven.

**Decided: left as is for now** - no code changed. Which onset is closer to the
truth is unknown without an independent reference. If aligning later, the
natural direction is making `start_detector.py` use the frozen baseline, since
the board's number is the one the athlete is given; and fix the stale comment in
`AicPicker.h` either way. The comparison script was session scratch, not in the
repo: it wraps `StartDetector.update` to record `b_h` when `baseline_frozen`
goes true, recomputes horiz from the raw rows with that `b_h`, and re-runs
`refine_onset`'s contrast check and `aic_pick` on it.

### Confirmed while answering "is this still the old algorithm"

Asked because Python and the board agreed exactly on the two captures examined
by hand below, which used to be the signature of a *bug* (chambel's unfixed
port, pre-09-14). Checked `AicPicker.h` directly: the two AIC fixes from the
09-14 section (`k <= n-5` --> sweep to `n-6`, variance guard `1e-9` --> `0`) are
both present. There is only one algorithm left, in two languages, so exact
agreement is the expected result and not a leftover of the old port - subject to
the input-signal difference in the section above, which is what the 7
disagreements are.

### One false start checked by hand: the verdict is right, the gate is marginal

`accel_20260914_172357.csv` reported `FALSE START` at go−1294.7 ms and it did
not look right to the athlete. Board and Python agree on it exactly (it is one
of the 38, not one of the 7 above), so the question was whether the algorithm is
wrong, not whether the two implementations disagree.

Reconstructing the raw horizontal signal by hand around the arming instant
settles it: `horiz` is genuinely climbing, not a blip - roughly 9 mg at arm,
past 20 mg forty ms later, past **60 mg** within another 150 ms. That is real,
sustained movement, an order of magnitude above the few mg the same file reads
everywhere else while actually still. The verdict is correct by the algorithm's
own rule.

What is worth flagging is *how* it armed. `arm_peak_mg` was **10.89 mg** -
above the 7.0 mg p95 the 19-capture study measured for genuine held stillness,
and close to the 15 mg gate. That is consistent with the gate catching a brief
lull in the middle of still-ongoing settling, not real steadiness - the exact
failure mode `DET_MIN_BLANK_MS`/`DET_QUIET_HOLD_MS` were tuned against, except
that tuning was retrospective, replayed over captures where "go" never actually
depended on the athlete going quiet. This is the first time a real athlete has
had to *earn* the countdown by settling, which is a different task from the one
the 27-capture sweep tested. Whether 800/200/15 mg hold up under that pressure
across more attempts is now the open question, not whether they held up in
replay.

### One valid start on the high side, and an unresolved second event

`accel_20260914_171838.csv`: 286.1 ms, inside the 09-09 range (127-299 ms) but
above the 09-11 median (159 ms). Not obviously wrong on its own. But the file
carries a second, larger re-arm event 165 ms later (87.6 mg against the first
one's 21.3 mg) - plausibly the real push-off, with 286.1 ms belonging to a
smaller preparatory movement instead. Not resolved; wants a look at the plot in
the GUI before trusting either number over the other.

### `old_python_algorithm/start_detector.py` - the version this project no longer runs

The last version before the arm-on-stillness rewrite (`7927ead`): fixed
1000 ms blanking, no `ARM` marker, no arming gate at all. Recovered from git
history (`git show 7927ead~1:Tools/start_detector.py`) and kept for reference
now that the algorithm it implements is gone from the live tree. It is a
snapshot, not a maintained file - do not port future fixes into it.

### `Data/bench_140926/` cannot be found - anywhere

The 09-14 section below describes it in detail: four bench runs, the hardware
proof that the gate/`ARM`/`ARMCAP` markers work, including the one row where
the board's own verdict matched `start_detector.py` to +0.000 ms
(`accel_..._142506`, "valid start, 300.6 ms"). That folder **does not exist**
in the working tree, is absent from `git log --all` for that path, and a full
filesystem search found nothing. It was written to disk, described in prose,
and apparently never `git add`ed. The evidence for "it has run on the board"
now rests entirely on the prose below, not on data anyone can re-open.

### Open

- **Board/Python AIC-input divergence above: 7/45, up to 11.6 ms.**
- **Was `Data/bench_140926/` ever committed anywhere - a stash, a branch, a
  different machine?** Worth asking before assuming it is simply gone.
- **The 45-capture set is not yet analysed for the gate itself:** cap hits,
  arming instant after `set`, `set -> go` distribution, whether the 5 false
  starts are real.
- **The marginal-arming case above needs more than one example.** One false
  start with `arm_peak_mg` near the gate is not enough to say 800/200/15 mg
  need retuning; it is enough to say the retrospective sweep did not test the
  case that matters now.
- The two-event ambiguity on `accel_20260914_171838.csv` (which onset is the
  "real" reaction) is unresolved.
- **`Data/block_starts_150926/`** (11 captures, landed in `723d794`) is not
  described anywhere yet - nobody has said what those runs were.
- Everything else open in the 09-14 section is still open: the buzzer's
  acoustic latency, the `micros()` wrap in `capture.py`'s `t_s`, and soldering
  the breadboard before the track.

---

## READ THIS FIRST — 2026-09-14: the start now arms on measured stillness, it has run on the board, and the 19 captures were never bench runs

Everything below happened in one session and is committed and pushed as
`7927ead`. `main` and `origin/main` are level; the working tree is clean.

### The correction that made the rest possible

The 09-13 section says of the 19 captures in `Data/`: *"those are bench runs
with the board being handled, not an athlete on blocks, so the figure says
nothing about the real case."* **That is wrong.** They are Lorenzo himself on
starting blocks, all nineteen, with the board mounted on the **back of the
block** - which is the product's intended mounting, so the numbers carry.

It matters because that one sentence was load-bearing: it is the whole reason
"measure how much a real athlete moves in the set position" had been the
standing most-useful-number-missing. The measurement was already in the repo.

Horizontal magnitude - the detector's own signal - over the half second before
`go`:

| | mg |
|---|---|
| median across captures | **3.1** |
| p95 | **7.0** |
| push-off peak after `go` | 1000 - 3600 |

The athlete is still to within a few mg and the push is ~300x that. The one
capture that breaks the pattern, `123532` at 175 mg median, is exactly the one
the detector calls a false start. **`DET_SETTLED_MG` = 15 mg is therefore fine**
- about 2x the observed p95. It was a guess and it happens to be a good one.

### The real defect was WHEN the check ran, not its value

The stillness check sat in the last 200 ms of a fixed 1000 ms blanking. But
settling is not over by then: the last movement above 30 mg lands **up to
2.65 s after "set"**, and the arming instant varies from 1.0 s to 3.3 s between
attempts. So the check measured the rise into position, not the hold - which is
why 16 of the 19 captures reported `NOT JUDGEABLE` while every one of them
contained a perfectly good reaction time.

Worse, the check was made 1.2-2.0 s before `go` and then vetoed the reaction
measured after it. By the time `go` sounded the athlete was quiet in 16 of 19
(median 9 mg); the 3 that were not are precisely the 3 false starts. And `go`
fired at `set + random(2.2-3.0 s)` without ever looking at the athlete, where a
real starter holds the gun until the field is steady.

### What replaced it

```
set -> min 800 ms (the rise into position, judged by nothing)
    -> wait: horiz < 15 mg for 200 ms continuous        (cap: 4 s)
    -> ARMED - random(700-1500 ms), ceiling set+3.4-3.6 s -> GO
    -> first onset < go+100 ms -> FALSE START | >= go+100 ms -> valid start
```

One verdict per attempt, a reaction time always reported, no re-arm.
`NOT JUDGEABLE` is gone from the normal path - it was a patch over an arming
rule that never looked at the athlete. The two false-start cases are one
comparison: moving before the gun and reacting under 100 ms are the same fault,
and World Athletics treats them as one.

**The cap must stay.** An athlete who never settles because they are already
starting would otherwise never arm, and the mechanism would disable itself in
exactly the case it exists to catch. On the cap it fires anyway and the verdict
is **annotated, never refused**.

Parameters swept over the 27 captures:

| quiet | hold | min blank | armed | hit the cap | armed too early |
|---|---|---|---|---|---|
| 15 mg | 200 ms | 500 ms | all | 0 | **2** |
| 15 mg | **200 ms** | **800 ms** | **all** | **0** | **0** |
| 15 mg | 500 ms | 800 ms | 26/27 | 1 | 0 |
| 15 mg | 600 ms | 800 ms | 22/27 | 4 | 0 |

800 ms of minimum blanking is what removes the early arming: at 500 ms the
detector latches onto a lull *during* the settling and reports a "false start"
one to two seconds before `go`. Above 400 ms of required quiet the cap starts
being hit. 200/800 sits clear of both.

`go` fires **700-1500 ms** after arming. With the athlete settling at set+1.0 s
the earlier 500-1200 window put `set -> go` at a median of 1.87 s and as low as
1.50 s, and a real starter holds "set" for about 1.5-2.0 s. 700-1500 gives a
2.12 s median with a 1.70 s floor and widens the unpredictability budget from
700 ms to 800 ms. Above ~1000 ms of minimum the ceiling is hit often enough to
matter (19% against 8%).

The ceiling is **randomised, 3.4-3.6 s, drawn once at "set"**. A fixed ceiling
fired `go` at exactly set+3.5 s every time it clamped, handing the athlete a
perfectly predictable instant in precisely the case where they were slow to
settle - and that case is common enough to be learnable.

### It has run on the board

Flashed over the **mbed** core. `arduino-cli` autodetects `Seeeduino:nrf52`,
which is the one that silently degrades `micros()` to ~977 us - force
`Seeeduino:mbed:xiaonRF52840Sense`. Flash 13%, RAM 72%.

`verify_rate.py`: **PASS** - `micros()` 8 us, 0 watchdog recoveries, 865.1 Hz,
0 gaps, 12000/12000 rows.

Four bench runs, on a table, in `Data/bench_140926/`. The gate armed at 1000,
1001 and 1002 ms with the board still - **1000 ms is the floor**
(`DET_MIN_BLANK_MS` 800 + `DET_QUIET_HOLD_MS` 200), so a still board has nothing
to wait for - and at **2984 ms** on the run where the board was deliberately
moved, after letting 200-390 mg of movement pass and seeing it fall to 6.6 mg.
That is the mechanism working in both directions.

The check that was never possible before, now that a capture carries the
board's own verdict:

| capture | board says | Python says | gap |
|---|---|---|---|
| `142506` | valid start, 300.6 ms | valid start, 300.6 ms | **+0.000 ms** |

### The board's decisions are now written down, not re-derived

A dump carries `ARM` / `ARMMG` / `ARMCAP` and the board's own `VERDICT` /
`RTMS` / `ONSET`; `capture.py` stores them in the CSV header; and
`start_detector.py` **reads** the arming instant instead of recomputing it
(`force_rearm=True` re-derives, which is what tuning wants).

Two reasons, and the second is the stronger one. Re-deriving is not guaranteed
to reach the same answer: the board computes in `float32` and numpy in
`float64`, and on `accel_20260909_123532.csv` a **0.30 mg** difference in the
baseline - 2%, systematic, from EMA accumulation - moved the arming by 1.5 s
and changed the verdict. But more fundamentally, that decision was already
taken by the board; recomputing it is second-guessing what actually happened,
the same mistake as recomputing `go` instead of reading its timestamp.

This also closes the older gap where the board's verdict survived only in the
terminal.

### A replay harness existed for a day, found two real bugs, and was removed

`Arduino/AlgorithmRealTime/replay/` built the firmware headers against a host
shim and compared them to `start_detector.py` over every capture. It was
deleted on request as testing scaffolding - it is in git history - but **its two
findings are permanent**, both in `AicPicker.h`, both invisible without it:

1. The AIC swept `k <= n - 5`; the Python reference's `np.arange(5, n - 5)`
   stops at `n - 6`. One split too many.
2. Its variance guard was `v > 1e-9`; the reference uses `v > 0`. On a quiet
   capture - the 09-11 set holds set at ~5 mg - the segment variances are small
   enough for that floor to discard splits the reference accepts.

Neither crashes. Both move the onset by one sample, on some captures only.

Worth recreating for any future detector change. Note also what it did NOT
justify: two of its three flagged disagreements turned out to be one sample
apart (1.137 and 1.179 ms against a 1.156 ms sample period), which is the
resolution floor, not a drift. Demanding better than the sampling resolution
asks for more precision than the data holds.

### Two bench findings that were not in the detector at all

1. **`verify_rate.py` failed a healthy board.** 8734 rows of 12000,
   `ROW COUNT MISMATCH`. The `d` command dumps the whole 12 000-sample ring -
   ~540 KB of ASCII - and the nRF52840's USB CDC moves it at roughly 20 KB/s
   whatever the nominal baud, so it needs ~27 s against a 20 s deadline. Worse,
   a timed-out attempt leaves the board still transmitting, so the NEXT run
   reads that dump's `DUMP_END` and reports `0 rows`. Now waits on silence
   instead of a stopwatch, and drains first. 12000/12000, PASS.
2. **`SEQ,armed` meant two different things** - sequence start and detector
   arming - so a reader matching on the prefix would conflate them. The gate is
   now **`SEQ,gate,<ms>,<mg>,<how>`**, `<how>` being `still` or `cap`.

Note the shape of the first one: a plausible wrong number with a ready-made
hardware explanation. Fourth time in this project.

### Layout, and the 8 new captures

Three sketches became one. `Arduino/AccelStream/` and
`Arduino/AlgorithmRealTime/` were **byte-identical** headers included, except
for `SET_DELAY` (20-25 s against 8-10 s), and `AlgorithmRealTime/start_detector.py`
was a byte-identical copy of the one in `Tools/`. There were never three
algorithms, there were two: the original port and the corrected one.

```
Arduino/AlgorithmRealTime/
  AlgorithmRealTime.ino  StartDetector.h  AicPicker.h
  Python_Tools/          capture.py  start_detector.py  verify_rate.py
```

`Python_Tools/` is inside the sketch folder on purpose - same algorithm, other
language, and the two must not drift - and the IDE and `arduino-cli` ignore any
subfolder that is not `src/`, so it never reaches the firmware. The Windows
COM-port autodetection from the colleague's `capture.py` was merged in rather
than dropped. `Tools/.vscode/settings.json` went with `Tools/`.

`Data/` is now split - see `Data/README.md`:

| folder | what |
|---|---|
| `block_starts_090926/` | 19 attempts, through the 3 kHz off-resonance beep |
| `block_starts_110926/` | 8 attempts, **the first through the 4 kHz antiphase drive** |
| `bench_140926/` | 4 table runs, the arming-gate hardware proof. Do not tune on these |
| `data_before_080926/` | pre-v4, `go` is a human keypress. Do not tune on these |

The 09-11 set shows what 09-11 predicted it would:

| | median RT | range | holding set, p95 |
|---|---|---|---|
| `090926`, 3 kHz off-resonance | 178 ms | 127-299 | 7.0 mg |
| `110926`, 4 kHz at resonance | **159 ms** | 132-169 | 4.8 mg |

19 ms faster and the spread collapses from 172 ms to 37 ms. That is Piéron's
law, and it is the evidence that no number from `090926` may be used to tune
anything.

All remaining Italian prose was translated to English. `RUN.md` was added: what
each file is and the command to run it.

### Open

- **No reaction time from an athlete on blocks through this firmware.** The
  bench runs prove the mechanism, not the measurement. Nothing since the timing
  was widened to 700-1500 ms has been tried with a real start.
  **[CLOSED 2026-09-15: 45 on-block attempts in `Data/block_starts_140926/`.
  See the top section.]**
- **The buzzer's acoustic latency is still unmeasured and still dominant.** The
  cheapest route needs nothing new: the IMU and the buzzer share a support, so
  the buzzer's mechanical onset reaches the accelerometer stamped with the same
  `micros()` that stamps the beep.
- `Tools/start_detector.py`'s successor still mis-reads any capture spanning
  the `micros()` wrap: the `t_s` column arrives already broken from
  `capture.py`. `Data/block_starts_090926/accel_20260909_133119.csv` is that
  file - one in nineteen, not a 71-minute curiosity.
- The whole build is still on a breadboard. Before the track, solder or
  strain-relieve D1, D2, D0 and GND. Do not use tape as a *mount*: it creeps
  under load, which changes the mechanical compliance the accelerometer sees.

---


## READ THIS FIRST — 2026-09-13: the algorithm now runs on the board, and three of the "obvious" fixes to it were decided by measurement (one of them against me)

A second person is working on this repo now. `chambel6` pushed `da47893`
("added algorithm and updated python tools") on 2026-09-12: a C++ port of
`Tools/start_detector.py` that runs the detection **on the board**, so the
firmware prints a verdict and a reaction time before it dumps the CSV.

### The merge was trivial; the divergence it left was not

Nothing conflicted — he worked entirely inside a new `Arduino/AlgorithmRealTime/`
while the 09-11 buzzer work touched `Arduino/AccelStream/`, `BuzzerSweep/` and
this file. `git pull --rebase` was clean.

But he had **copied** the old firmware into his folder as a starting point, so
his copy carried the 3 kHz `tone()` buzzer that 09-11 had just removed. That is
worth stating plainly because of what it would have done: flashing it to take a
reaction time would have measured a real human response to a stimulus half the
voltage and off resonance, and **nothing in the resulting data would have shown
it** — the same failure shape as the `micros()` bug of 09-08 and the quiet beep
of 09-11. Three for three now. Assume the next one has the same shape.

There are now three sketches. They are not redundant; keep them distinct:

| Sketch | What it is |
|---|---|
| `AccelStream/` | the reference pipeline: 09-11 buzzer + his detector hooks |
| `AlgorithmRealTime/` | his, unchanged in substance. `SET_DELAY` 8-10 s for bench iteration |
| `AlgorithmRealTimeFixed/` | `AccelStream` plus the five corrections below |

All three compile at 13% flash. RAM: 71% for the first two (168 712 used, 68 856 free),
71% for Fixed (170 920 used, 66 648 free).

His `.ino` was named `AccelStream.ino` inside `AlgorithmRealTime/`, which the
Arduino IDE cannot open — the sketch file must carry the folder's name. Renamed.
His `capture.py` fork adds Windows COM-port autodetection and is **kept**: he
works on Windows. His `start_detector.py` is a byte-identical copy of the one in
`Tools/` and is pure duplication.

### "How does it fit in so few lines" — it doesn't

The `.ino` diff is only ~84 lines because that is just the wiring: instantiate,
feed each sample, seed at `set`, report at `tail`. The algorithm is in the two
headers he added.

| | lines |
|---|---|
| `StartDetector.h` + `AicPicker.h` | **309** |
| `start_detector.py`, algorithmic parts only | **302** |
| `start_detector.py`, whole file | 829 |

The other 527 Python lines are Tkinter, the matplotlib panels, CSV loading,
marker parsing and the CLI. None of it belongs on a microcontroller. The only
real compression is numpy vectorising the AIC variance sweep into a `for` loop.

### Three things were measured instead of argued. One of them overturned the review.

The review of his port produced five candidate defects. Before "fixing" them,
the two that would have changed numbers were tested over the 19 captures in
`Data/` — which also got them analysed for the first time.

**1. The AIC input signal — REVIEWED AS WRONG, MEASURED AS FINE, LEFT ALONE.**
He recomputes the horizontal magnitude from the raw ring using the baseline
frozen at the event; the Python runs on the detector's own live trace, whose
baseline is still adapting through the pre-window. The Python's own docstring
says it does this so "the two stages never disagree about what the signal is",
so this looked like a clear divergence to remove.

Measured over 64 comparisons — 16 captures x absolute floors of 10/20/30/50 mg —
the two agree to **0.000000 ms**. Not closely. Identically. And there is an
argument the frozen reference is the *better* input, since AIC's model is two
stationary segments and a baseline chasing the onset flattens the very
transition being looked for. Changing it would have cost ~9 KB of stored trace
to buy nothing. **His version stands; the review item was withdrawn.**

**2. Sub-sample interpolation — TRIED, REJECTED.** Fitting a parabola to
`AIC(k-1..k+1)` and taking its vertex is the standard way to beat sample
quantisation (1.2 ms at 833 Hz). On paper it should have helped.

| across floors 10-50 mg | mean spread | worst |
|---|---|---|
| plain pick at the sample | **0.000 ms** | 0.000 ms |
| parabolic sub-sample | 0.060 ms | 0.253 ms |

It made stability *worse*: the window shifts with the trigger and drags the
vertex with it, so an exactly floor-independent answer became a slightly
floor-dependent one. Not adopted. **Do not re-add it without new evidence.**

The conclusion that matters: **the remaining onset uncertainty is no longer in
the estimator.** AIC is already exactly threshold-independent. What is left is
the sample period (1.2 ms) and the timestamp jitter (334 us) — properties of the
sampling, not of the algorithm. The only lever that moves them is a higher ODR,
and both sit far below the still-unmeasured buzzer latency.

**3. The backdating history — CONFIRMED, and worse than the review estimated.**
He used a fixed 32-slot buffer; the Python sizes it `confirm_n + 5` (18 at
833 Hz). The buffer is scanned on confirmation for the first sample above the
floor, and that is its whole job: undo the latency the confirmation just cost,
nothing more. A longer one lets the backdate reach past the confirmation into an
unrelated earlier excursion — above the floor but below `ratio_on`, such as a
settling wobble.

On `Data/accel_20260909_121238.csv` that is a **115 ms** error (183.79 ms
reported instead of 299.28 ms), in the direction that turns a valid start into a
false one. `confirm_n + 5` is not a magic number, it is a principled bound.

### The five corrections in `AlgorithmRealTimeFixed/`

1. `hist_cap = confirm_n + 5` instead of 32 — the 115 ms one above.
2. **`NOT JUDGEABLE` was missing entirely.** The Python's third outcome —
   signal still above `settled_mg` in the last 200 ms of the blanking, so the
   rise into set had not finished and nothing after it can be told from its
   tail. Ported, tracked causally, and it outranks every other verdict.
3. **No more silent failure.** Both early returns in `setupDetectorFromPreroll`
   produced no output, leaving `g_hat = {0,0,0}` — which does not disable the
   detector, it removes the projection, so `horiz` quietly becomes a high-pass
   of the full `|a|` and the horizontal/vertical split the algorithm rests on is
   gone. Now reported as `UNUSABLE` with the measured `|a|`.
4. **Wrap-safe comparisons** in `classifyEvent`, matching the rest of the
   firmware. See below — this one is not theoretical.
5. The `float[300]` stack copies (3.6 KB) and the silent `min(rest_n, 300)` and
   `MAX_AIC` truncations are gone; `MAX_EVENTS` overflow is now reported.

### Validated against the Python on the host, not just compiled

The same C++ was built with `clang++` against a small `Arduino.h` shim and
replayed over all 19 captures, following the firmware's exact sequence
(calibrate from the pre-roll, replay it, then feed live samples from `set`).
Scratch harness, not in the repo — worth re-creating for any future detector
change, since it checks the port against the Python without flashing anything.

| | vs `Tools/start_detector.py` |
|---|---|
| **Fixed** | **18/19 identical to 0.00 ms**; `NOT JUDGEABLE` agrees 19/19 |
| his | two disagreements: **-115.49 ms** and -15.03 ms |

### The 19th capture: `micros()` wrapped, and only the fixed version survived it

`Data/accel_20260909_133119.csv` straddles the ~71 minute wrap:

```
set_t_us = 4294112776      (854 ms below UINT32_MAX)
go_t_us  =    1598118      (already past zero)
(go - set) mod 2^32 = 2.452638 s     <- squarely in the 2.2-3.0 s GO_DELAY range
```

The fixed firmware puts the event at `go + 175.59 ms`, which signed arithmetic
confirms exactly (175 591 us). The Python and his C++ both conclude "everything
is pre-set" and judge nothing — the Python because it compares `t_s` in float
seconds, his because he compares `t_us` with unsigned `<`.

So the wrap is **not** a 71-minute curiosity to be handled someday: it is
already in the data, one capture in nineteen.

### Before this goes to the track: `settled_mg` will refuse almost everything

With the Python default of 15 mg, **16 of the 19 captures would report
`NOT JUDGEABLE`** — peak movement in the checked window runs 7.1 to 711 mg,
median 66. Those are bench runs with the board being handled, not an athlete on
blocks, so the figure says nothing about the real case.
**[WRONG — corrected 2026-09-14: they ARE an athlete on blocks, all nineteen.
The measurement this section calls missing was already in them: a held set reads
3.1 mg median, 7.0 mg p95. The defect is when the check runs, not its value.
See the top section.]**
What it does say is that
flashing this untouched would very likely produce a device that declines to
judge nearly every attempt.

This is the same number HANDOFF has been calling the most useful one still
missing: **how much a real athlete actually moves while holding the set
position.** It is a single constant, `DET_SETTLED_MG` in `StartDetector.h`, with
the reasoning written above it. Measure it on one athlete before the first
session.

### Open, and new since 09-11

- **`Tools/start_detector.py` mis-reads any capture spanning the `micros()`
  wrap.** The fault is upstream of the detector: the `t_s` column arrives
  already broken (it reaches -4288 s on `133119`), because whatever emits it
  subtracts without wrap handling. Every offline analysis therefore has a blind
  spot that opens once every 71 minutes of uptime. Not yet fixed.
- The 19 captures of 09-09 are now analysed: reaction times of 126-299 ms plus
  two pre-`go` events. **They were all taken through the 3 kHz off-resonance
  beep**, so the stimulus intensity behind them is unknown and none of these
  numbers may be used to tune anything.
- Still nothing on hardware: the on-device detector has never run on the board,
  and no reaction time has been taken through the 4 kHz antiphase drive.
- The buzzer's acoustic latency is still unmeasured and still dominates.
- `Tools/.vscode/settings.json` (his conda IDE config) is committed; probably
  should not be.

### State of the tree

`main` is one commit ahead of `origin/main` — `e362727` (the 09-11 buzzer work,
rebased onto his `da47893`) — and **not pushed**. On top of that, uncommitted:
`AccelStream/` carries his detector hooks plus the two headers,
`AlgorithmRealTime/` has the rename and its 8-10 s `SET_DELAY`, and
`AlgorithmRealTimeFixed/` is entirely new. Held deliberately, by request.

**[SUPERSEDED 2026-09-14: all of that is committed and pushed as `7927ead`, and
the three sketches are now one. `main` and `origin/main` are level. Check with
`git status` rather than reading this paragraph.]**

---

## READ THIS FIRST — 2026-09-11: the beep was too quiet to use, and that was a measurement bug, not a comfort one

The system was about to go to the track with a beep that could barely be heard.
Fixing it turned out to be free — no new parts — and the reason it mattered is
not the one it looks like.

### It is a piezo at 4 kHz, and the firmware was driving it at 3 kHz

`Arduino/BuzzerSweep/BuzzerSweep.ino` was written to stop guessing: no IMU, no
sequence, a square wave from a busy-loop (so what you hear is the part and not
the driver), sweeping 1.5-6.0 kHz in 100 Hz steps. Measured on the bench:

| | |
|---|---|
| loudest | **4000 Hz**, clearly; weaker secondary modes at 1600 and 4700 |
| nRF52840 high drive (5 mA vs 0.5 mA) | **no audible change** |

The second row is the diagnosis, not a footnote. A magnetic buzzer is a ~16 ohm
coil and is current-driven, so raising the pad's drive strength would have been
plainly audible. Nothing happened, so the part is a **piezo**: capacitive,
voltage-driven, and — being a high-Q resonator — enormously sensitive to being
driven off its resonance. `BEEP_FREQ_HZ` was 3000, which is not a measurement,
it is a round number. That alone was most of the missing volume.

**If the buzzer is ever replaced, re-run the sweep.** Resonance is a property of
the part, and a Q of ~20 makes the peak only a couple of hundred Hz wide.

### Antiphase drive: +6 dB for one GPIO and no components

The second half of the fix. The element used to see 3.3 Vpp (one pin against
GND). Driving both terminals in opposite phase gives it 6.6 Vpp.

**This is safe *because* it is a piezo.** A piezo is a capacitor and passes no
DC, so neither pad ever sources steady current. Do **not** wire a magnetic
buzzer this way — it would double a current the pad already cannot supply.

```
WIRING, current:   buzzer (+) -> D1      buzzer (-) -> D2
                   there is NO connection to GND
```

This supersedes the v4 wiring table below, which says active buzzer, `(-)` to
GND. Both halves of that are now wrong.

### tone() had to go, and the reasons are about timing, not volume

`tone()` drives one pin, so it cannot do antiphase — but that is the least of
it. The mbed core's implementation (`cores/arduino/Tone.cpp`) is wrong for this
application in two further ways, both of which land on the single instant the
whole measurement is referenced to:

- It does `new Tone` **and** `new DigitalOut` on every call — a heap allocation
  at the exact microsecond that defines the reaction time's zero. `malloc` is
  not constant-time, and on a fragmented heap it is not even bounded.
- It leaks. `Tone::stop()` sets its `DigitalOut*` to null instead of deleting
  it, so the destructor then deletes a null pointer and the object is lost.
  Three beeps per run, forever, on a board with ~70 KB free.

Replaced with the nRF52840's **PWM peripheral** (PWM2 — the mbed core hands out
instances from 0 upwards for `analogWrite`/`PwmOut`, so taking the far end
costs nothing and keeps it clear). Two channels, same compare value, opposite
polarity bit, which is exact antiphase. Starting it is two register writes, the
first edge follows within one 16 MHz tick, and it then runs from its own
hardware with **no CPU and no interrupts** — which matters because the `go`
beep overlaps the 100 ms in which the push-off is being sampled, and a software
ticker would have been firing 8000 times a second right through it.

RAM after the change: **69% (166 064 used, 71 504 free)** — unchanged from v4.

### The part that matters more than the error budget

A louder stimulus does not merely reduce measurement error — it changes the
quantity being measured. Human reaction time falls with stimulus intensity
(Piéron's law), and for auditory stimuli the difference between near-threshold
and clearly audible is **tens of milliseconds**, with much more variance too.

With the old beep, a field session would have produced reaction times that were
inflated and unstable for a reason having nothing to do with the electronics,
and **nothing in the data would have shown it**. That is the same failure shape
as the `micros()` resolution bug of 2026-09-08: confident, physically sensible,
wrong numbers. Worth remembering when judging whether a "cosmetic" complaint is
cosmetic.

### Error budget now

The detection side is untouched and still ~1-2 ms. What changed is that two
*unquantified* risks were removed — `tone()`'s allocation jitter and the
ticker's interrupt load in the measurement window — rather than any measured
term getting smaller.

**The acoustic latency remains the dominant term and is still unmeasured**, but
it is no longer the number the older sections quote. A piezo at resonance rings
up over roughly Q/π cycles; at 4 kHz that is on the order of 1-2 ms, plausibly
*better* than the 5-20 ms assumed for an active buzzer. Plausibly. Not measured.

**The cheapest way to close it needs nothing new:** the IMU is on the same
breadboard as the buzzer, so the buzzer's mechanical onset reaches the
accelerometer stamped with the *same* `micros()` that stamps the beep — which
removes the cross-clock sync problem that made this measurement awkward in the
first place. Run a sequence with the board still and untouched, then measure
`go` to the start of the vibration. At 833 Hz that resolves to ~1.2 ms, ample to
tell 2 ms from 20 ms. Only the air path is left over, and that is 2.9 ms/m
computed from geometry rather than measured.

### Also fixed today

- `SET_DELAY` (`on your marks` -> `set`) is back to **20-25 s**, the original v4
  value. It had been shortened to 10-15 s on 09-09 to make bench iteration less
  tedious. It costs nothing: the sample ring fills continuously in every state,
  and the dump window is measured backwards from `set`.
- The **button was miswired**, not broken. A 4-pin tactile switch has its pins
  paired internally, so it must straddle the breadboard's centre channel or its
  two node rows are permanently shorted. This is documented in the v4 section
  below and it still happened. A temporary `k` command that printed D0's raw
  level separated "wiring" from "firmware" in under a minute; it has been
  removed, but that is the shape of the fix to reach for again.

### Untested on hardware, and hardening for the field

The three-beep sequence has been exercised; **no reaction time has been taken
through the new drive**. Also note the whole build is still on a breadboard, and
a friction contact is what failed today. Before the track, either solder the
four connections (D1, D2, D0, GND) onto perfboard or strain-relieve each wire
with hot glue. Do not use tape as a *mount*: it creeps under load, which changes
the mechanical compliance the accelerometer sees, so a calibration taken today
would not survive to the next session. And never cover the buzzer's sound hole.

---

## The gap: 2026-09-09, never written up

This file jumped from the evening of 09-08 to today. The session in between is
recorded only in commits `78b89dc`, `a3c5e04` and `6032f19`:

- The buzzer was found to be **passive**, not active, and `beep()` moved from
  `digitalWrite` to `tone()` at 3000 Hz. That is what today's section replaces.
- `SET_DELAY` shortened 20-25 s -> 10-15 s (reverted today).
- `start_detector.py`'s left parameter panel became scrollable
  (Canvas + Scrollbar) instead of being clipped.
- 19 captures from 09-09 were added and the 09-08 ones removed. **None of those
  captures has been analysed**, and they were all taken through the 3 kHz
  off-resonance beep, so any reaction time in them carries an unknown stimulus
  intensity.

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

> **SUPERSEDED 2026-09-11 for the buzzer** — the part is a passive piezo, it is
> driven antiphase from **D1 and D2** with no GND connection, and it must not be
> an active one. See the top section. The button row below is still correct.

| Part | Wiring |
|---|---|
| ~~**Active** buzzer (has its own oscillator)~~ | ~~`+` → **D1**, `−` → **GND**~~ |
| Momentary button | one leg → **D0**, the **diagonally opposite** leg → **GND** |

No resistors: `D0` is `INPUT_PULLUP` and the button pulls it to ground. On a
4-pin tactile button the pins are paired internally, so two legs on the *same*
side are a permanently closed circuit — take them diagonally opposite. D0/D1 are
clear of the IMU's I2C and of the UART on D6/D7.

~~**The buzzer must be an active one.**~~ **Wrong, and resolved on 2026-09-11.**
The part on the bench is passive, and it is now driven by the PWM peripheral
rather than `tone()` — which is deterministic, costs no interrupts, and allows
the antiphase drive. The worry the original sentence expressed was right; the
conclusion it drew from it was not. See the top section.

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
