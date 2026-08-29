# HANDOFF — Prostart live IMU data view

Last updated: 2026-08-29. Written for an agent starting with no prior context.

## Goal

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

Note the root `README.md` describes a `firmware/` and `app/` layout that does
not exist. The real one:

- `prostart/` — Flutter app (Dart, `provider` for state, `flutter_blue_plus` for BLE)
- `Arduino/BLEtest/BLEtest.ino` — the firmware actually flashed to the board
- `Arduino/I2C_Scanner/` — I2C debug sketch
- `Arduino/libraries/Seeed_Arduino_LSM6DS3/` — vendored IMU library
- `report/` — LaTeX project report

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

The UUID and the 12-byte layout are duplicated in two places and must stay in
sync: `prostart/lib/services/ble_service.dart` and
`Arduino/BLEtest/BLEtest.ino`. Both files carry a comment saying so.

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
widget test). **Nothing has been run on hardware or a simulator by the agent.**

## What worked

- **CustomPainter instead of a charting package.** `fl_chart` and friends rebuild the widget tree per update and get janky at 50 Hz. A `CustomPainter` repainting off the vsync ticker, with x mapped from each sample's own timestamp, scrolls smoothly and stays jitter-free when packets arrive early or late. Y-axis auto-scales with easing so spikes don't make the axis jump.
- **Splitting the notification channels.** Sample arrivals go through a `ValueNotifier<AccelSample?>` (`LiveDataController.latest`), not `notifyListeners()`. Only status/recording changes call `notifyListeners()`. This keeps 50 Hz of data from rebuilding the whole screen. The recording timer label has its own `ValueNotifier<Duration>` ticked once a second.
- **Scoping the controller to the route.** `LiveDataController` is created and disposed with the screen, so BLE notifications are only enabled while the user is actually looking at the stream, and the firmware's `subscribed()` gate means the board stops reading the IMU too.

## What didn't work — do not repeat

Three debugging dead ends cost real time. All are now fixed, but the reasoning
matters if similar symptoms return.

- **"Device not found" was blamed on the app first.** It was not. The scan and connect path was never touched by this work — `git diff` on `ble_service.dart` was purely additive constants. Check the board before the app: pair with nRF Connect or LightBlue to see whether `BlockStartDevice` is advertising at all. That single check separates firmware problems from app problems in seconds.
- **`FlutterBluePlus.connectedDevices` cannot find stale connections.** The old `_disconnectStaleConnections()` used it to clean up links surviving a hot restart — but it is a snapshot of an in-memory map built by *this* process, so after a restart it is empty, precisely in the case it was written for. A connected BLE peripheral stops advertising, so the still-live OS link made the board invisible to scanning. Replaced with `_findAlreadyConnectedDevice()`, which uses `FlutterBluePlus.systemDevices([serviceUuid])` (reports links held by any app) and **adopts** the device rather than disconnecting and rescanning. `connect()` is still required to attach it to our app but returns immediately. Note Android ignores the `withServices` argument to `systemDevices`, so there is a local-name fallback; `discoverServices()` is the real verification either way.
- **`while (!Serial) delay(10);` bricks the sketch on a XIAO.** USB is native on the nRF52840: `Serial` only goes true when a host opens the CDC port. On battery, on a port with no Serial Monitor open, or on a charger, `setup()` blocks there forever and `BLE.advertise()` never runs. Reset makes it worse — it re-enters the block. Now a bounded 3-second wait. **The same pattern is still present in `Arduino/I2C_Scanner/I2C_Scanner.ino`** and in the vendored library examples.
- **Flashing an IMU debug sketch silently removed BLE.** Commit `33e3cd5` edited `Arduino/libraries/.../HighLevelExample.ino`, which contains zero BLE code. With that on the board, no app can ever find it. Obvious in hindsight, invisible from the app side. Now moot, since BLE and IMU live in the same sketch.

## Next steps

Nothing is blocked; everything below needs hardware.

1. **Flash `Arduino/BLEtest/BLEtest.ino`** and watch the Serial Monitor for `IMU OK`. If it prints `IMU error - live data disabled`, BLE still works and reaction time is unaffected — but the live view will be dead. The sketch sets `PIN_LSM6DS3TR_C_POWER` high inside an `#ifdef` (the XIAO Sense IMU has a dedicated power pin; if it stays low `begin()` fails even with I2C wired correctly). If the macro is missing from the installed core, that guard compiles it away and the pin is never driven — check the variant header.
2. **The sketch has never been compiled.** No `arduino-cli` is installed on this machine. Verify at upload time.
3. **Confirm the 50 Hz stream end to end.** `RecordingSession.sampleRateHz` on the summary card reports the achieved rate — if it lands well under 50, the connection interval is the first suspect. The sketch requests 15–30 ms via `BLE.setConnectionInterval(12, 24)`, but that is a request and the central decides; iOS in particular may not grant it.
4. **Sanity-check the axis values.** At rest one axis should read ≈ 1.0 g and the others ≈ 0. If everything is ~4× too small or too large, `accelRange` and the library's `calcAccel` scaling have diverged.
5. **Test the CSV export on a real device.** The export threw on iOS - see the
   `path_provider_foundation` note below - and that is fixed, but the share
   sheet itself is still unexercised. On iPad it is a popover needing an anchor
   rect; `_export` in `live_data_screen.dart` derives one from the summary
   card's `RenderBox`. "Save to Files" in the sheet is the save-to-device path.
## Done since — no hardware needed

- `I2C_Scanner.ino` now uses the same bounded 3 s `Serial` wait as `BLEtest.ino`.
- `prostartAccelerometerSampleRateHz` is wired into the recording summary line,
  which now reads `48.7 Hz (nominal 50 Hz)` — giving next step 3 something to
  compare against directly on screen.
- Root `README.md` repository structure and the `cd app` / `firmware/` paths in
  Getting Started corrected to `Arduino/` and `prostart/`.

### `path_provider_foundation` is pinned - do not remove the override

`path_provider_foundation` 2.5.0 replaced its iOS/macOS plugin with an
`objective_c` FFI backend, 2.5.1 reverted it, and 2.6.0 went back to FFI. That
backend ships as a Flutter *code asset*, only built and embedded into
`Runner.app` when native assets are enabled (`flutter config` shows
`enable-native-assets: (Not set)` on stable). Without it every call - including
the `getTemporaryDirectory()` in `LiveDataController.exportLastSession` -
throws `Failed to load dynamic library 'objective_c.framework/objective_c'`.

The tell is `ios/Podfile.lock`: the FFI version registers no pod at all, so
`path_provider_foundation` was simply absent from it. `pubspec.yaml` now has a
`dependency_overrides` pinning 2.5.1, the newest pigeon/CocoaPods release.
`share_plus` was suspected first and is **not** implicated - it stays on 13.3.0.

Remove the override once native assets are on by default on stable, and
rebuild from scratch (`flutter clean` + `pod install`) when you do.

Still wrong in the README: it links to `report/main.tex`, but no `report/`
directory has ever been committed to this repo. Left alone deliberately — it
may live elsewhere.

## State of the tree

Everything described above is committed on `main` (`37060b5` for the live data
view). Note `prostart/android/build/reports/...` is tracked and is a build
artifact that probably should not be.
