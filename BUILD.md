# BUILD — environment, versions, and how to run everything

This is the practical companion to `HANDOFF.md`: only what to install and what
command to run, per component. See `HANDOFF.md` for how each piece works
internally and the history behind the current design.

Only what's actually implemented is covered here. The Zigbee start↔finish
link and the finish-unit's BLE bridge to the phone, described in the root
`README.md`, don't exist in code yet — see [What's not buildable yet](#4-whats-not-buildable-yet).

---

## 1. Firmware — `Arduino/AccelStream/AccelStream.ino`

The only firmware currently implemented and verified working on real
hardware: accelerometer capture at 416 Hz, no BLE, no hardware FIFO.

**Prerequisites**
- Arduino IDE (any recent release; developed against the current 2.x stable)
- Board package **"Seeed nRF52 Boards"**, via Boards Manager URL:
  `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
- Vendored library `Arduino/libraries/Seeed_Arduino_LSM6DS3/` — the Arduino
  IDE only scans its own sketchbook `libraries/` folder, so symlink or copy
  it in (default sketchbook location on macOS: `~/Documents/Arduino/libraries/`)
- Board: **Seeed XIAO nRF52840 Sense**

**Setup**
```bash
# 1. Arduino IDE -> Preferences -> Additional Boards Manager URLs -> add the
#    Seeed URL above, then Tools -> Board -> Boards Manager -> install
#    "Seeed nRF52 Boards".

# 2. Make the vendored IMU library visible to the IDE:
mkdir -p ~/Documents/Arduino/libraries
ln -s "$(pwd)/Arduino/libraries/Seeed_Arduino_LSM6DS3" \
      ~/Documents/Arduino/libraries/Seeed_Arduino_LSM6DS3
```

**Flash and verify**
1. Open `Arduino/AccelStream/AccelStream.ino`.
2. Tools → Board → **Seeed nRF52 Boards → Seeed XIAO nRF52840 Sense**.
3. Tools → Port → the board's port (`/dev/cu.usbmodem...` on macOS, `COMx` on Windows).
4. **Upload.** If it times out waiting for the board, double-tap the board's
   physical reset button (until a `XIAO-SENSE` drive appears) to force
   bootloader mode, then upload again.
5. Open the **Serial Monitor at 921600 baud**. You should see, once, with no
   repeats:
   ```
   IMU OK - accel 416 Hz, +/-8 g
   Ready. Serial: 'r' start CSV, 's' stop, 'p' one reading.
   ```
   If it prints `IMU error` instead, or the banner repeats on its own, see
   `HANDOFF.md` — those are both documented, previously-seen failure modes.
6. Type `p` and press enter — you should get one `t_us=... x=... y=... z=...`
   line back immediately. **Close the Serial Monitor before step 2 below** —
   only one process can hold the serial port at a time.

---

## 2. Python capture tooling — `Tools/accel_live.py`, `Tools/csv_plot.py`

**Tested with:**

| Package | Version |
|---|---|
| Python | 3.13.13 (3.10+ should work) |
| pyserial | 3.5 |
| matplotlib | 3.11.1 |
| numpy | 2.5.2 |
| pandas | 3.0.5 (only needed by `csv_plot.py`) |

**Install**
```bash
pip3 install pyserial matplotlib numpy pandas
```

**Run — live view + record** (board flashed with `AccelStream.ino`, plugged
in, Serial Monitor closed):
```bash
python3 Tools/accel_live.py                    # autodetects the serial port
python3 Tools/accel_live.py --port /dev/cu.usbmodemXXXX
python3 Tools/accel_live.py --simulate         # no hardware needed - fake data, for UI testing
```
Buttons (or keys `r`/`s`/`p`) start/stop recording and save a snapshot. Every
recording writes a CSV plus a 4-panel PNG (X, Y, Z, magnitude) to `Data/`.

**Run — offline review of a saved CSV:**
```bash
python3 Tools/csv_plot.py
```
Always opens a native file-picker dialog defaulting to `Data/` — no path to
type. Handles both the current CSV format (`t_s,t_us,x_g,y_g,z_g,host_iso`)
and the older one (`timestamp_iso,elapsed_s,x_g,y_g,z_g`) automatically.

---

## 3. Companion app — `Flutter App/prostart`

**Tested with:**

| Tool | Version |
|---|---|
| Flutter | 3.38.9 (stable channel) |
| Dart | 3.10.8 (`pubspec.yaml` requires `^3.10.4`) |

**Install & run**
```bash
cd "Flutter App/prostart"
flutter pub get
flutter run
```

**Known gotcha (iOS/macOS):** `path_provider_foundation` must stay pinned to
**2.5.1** via `dependency_overrides` in `pubspec.yaml` — later versions ship
an FFI backend that only gets embedded into the app when Flutter's
native-assets feature is enabled, which it isn't by default on stable. This
repo already has the pin; if you ever bump it, check `ios/Podfile.lock` for a
registered `path_provider_foundation` pod, and run `flutter clean && pod
install` afterward. Full story in `HANDOFF.md`.

---

## 4. What's not buildable yet

- The **Zigbee firmware** for the start↔finish link, and the finish unit's
  **BLE bridge** to the phone (both described in the root `README.md`) are
  not implemented. They'll likely need Nordic's own nRF Connect SDK / Zephyr
  for the 802.15.4/Zigbee stack on the nRF52840, rather than the plain
  Arduino IDE toolchain used for `AccelStream.ino` above. Tracked on the
  [backlog](https://github.com/users/lorenzogalli-dev/projects/4).
- `Arduino/BLEtest/`, `Arduino/HighFrequencySampleRate/`,
  `tools/kinestart_live.py`, and `playground_IMU/` were all deleted on
  2026-09-04 (broken or superseded) — there's nothing to build there. See
  `HANDOFF.md` for why.
