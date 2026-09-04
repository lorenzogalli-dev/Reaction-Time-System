# HANDOFF — Prostart live IMU data view & sensor evaluation

Last updated: 2026-09-04. Written for an agent starting with no prior context.

## READ THIS SECOND — README.md rewritten, new architecture direction, unresolved discrepancy

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

## READ THIS FIRST — repo reorganized 2026-09-04, BLEtest.ino removed

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
push-off-scale data already captured — see `Data/accel_2026*.png`):

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

Current, as of the 2026-09-04 reorganization:

- `Flutter App/prostart/` — Flutter app (Dart, `provider` for state, `flutter_blue_plus` for BLE)
- `Arduino/AccelStream/AccelStream.ino` — **current working firmware** for
  accelerometer capture (no BLE); see the top section above
- `Arduino/SerialEchoTest/SerialEchoTest.ino` — minimal hardware/cable sanity
  check, no IMU or BLE
- `Arduino/I2C_Scanner/` — I2C debug sketch
- `Arduino/Reaction_HardwareTest/` — TFT display/buzzer/XBee hardware bring-up test
- `Arduino/libraries/Seeed_Arduino_LSM6DS3/` — vendored IMU library
- `Tools/accel_live.py` — current live capture/record tool, pairs with `AccelStream.ino`
- `Tools/csv_plot.py` — current offline CSV viewer (file-picker based)
- `Data/` — recorded CSV captures and their plots
- `Docs/` — diagrams and figures used by the root README

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
2. **The sketch has never been compiled by an agent.** No `arduino-cli` on this machine. It has since been flashed successfully by hand, so this is largely moot — but no automated check exists.
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
