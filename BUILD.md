# BUILD — environment, versions, and how to run everything

This is the practical companion to `HANDOFF.md`: only what to install and what
command to run, per component. See `HANDOFF.md` for how each piece works
internally and the history behind the current design.

Only what's actually implemented is covered here. The Zigbee start↔finish
link and the finish-unit's BLE bridge to the phone, described in the root
`README.md`, don't exist in code yet — see [What's not buildable yet](#4-whats-not-buildable-yet).

---

## 1. Firmware — `Arduino/AccelStream/AccelStream.ino`

The only firmware currently implemented and verified working on real hardware:
accelerometer capture at **833 Hz nominal (~863 Hz measured)**, paced by the
IMU's data-ready interrupt, no BLE, no hardware FIFO.

### ⚠️ Use the mbed core — this is not a preference

Board menu entry: **`XIAO nRF52840 Sense (No Updates)`**.

The "No Updates" label makes this look like the wrong choice. It is not. The
two cores that can build for this board provide different `micros()`
implementations, and only one of them can timestamp a reaction time:

| Core | Board menu entry | `micros()` | Resolution |
|---|---|---|---|
| **`Seeeduino:mbed`** ✅ | XIAO nRF52840 Sense **(No Updates)** | `mbed::Timer` | **~8 µs** |
| `Seeeduino:nrf52` ❌ | Seeed XIAO nRF52840 Sense | FreeRTOS tick fallback | **~977 µs** |

On `Seeeduino:nrf52`, `cores/nRF5/delay.h` returns the DWT cycle counter *only
if the DWT is enabled* — and it is off unless a debugger turned it on.
Otherwise it silently falls back to `tick2us(xTaskGetTickCount())`, and
`configTICK_RATE_HZ` is **1024**, giving 976.5625 µs steps.

This cost two days on 2026-09-08. The failure is silent and looks healthy:
captures came back with zero gaps, zero dropped samples and sensible
accelerations, while every sample interval was either 977 µs or 1954 µs
because that was the entire available grid. Reaction times computed from that
data are wrong by up to a millisecond with nothing to indicate it.

The firmware now measures this at boot, prints it, refuses to be quiet about
it, and stamps `CLOCKSTEP,<us>` into every capture — but check the board menu
anyway.

**Prerequisites**
- Arduino IDE (any recent release; developed against the current 2.x stable)
- Board package **"Seeed nRF52 Boards"**, via Boards Manager URL:
  `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
  (this one package supplies *both* cores; you want the mbed board entries
  from it, per the table above)
- Vendored library `Arduino/libraries/Seeed_Arduino_LSM6DS3/` — the Arduino
  IDE only scans its own sketchbook `libraries/` folder, so symlink or copy
  it in (default sketchbook location on macOS: `~/Documents/Arduino/libraries/`)
- Board: **XIAO nRF52840 Sense (No Updates)**

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
2. Tools → Board → **XIAO nRF52840 Sense (No Updates)**. See the warning above.
3. Tools → Port → the board's port (`/dev/cu.usbmodem...` on macOS, `COMx` on Windows).
4. **Upload.** If it times out waiting for the board, double-tap the board's
   physical reset button (until a `XIAO-SENSE` drive appears) to force
   bootloader mode, then upload again.
5. Open the **Serial Monitor at 921600 baud**. You should see, once, with no
   repeats:
   ```
   IMU OK - accel 833 Hz (data-ready on INT1), +/-16 g
   clock: micros() resolution ~8 us
   Ready. Idle preview streaming. 'p' one reading.
   Start sequence: press the button on D0 (or send 'b'); buzzer on D1
     button -> 2-3s -> MARKS -> 20-25s -> SET -> 2.2-3s -> GO -> 1s -> dump
     'a' or a second press aborts. 'd' dumps the ring as-is.
   ```
   If the clock line reports a resolution in the hundreds of µs, a five-line
   `!! WARNING` block follows it — you are on the wrong core, go back to step 2.
   If it prints `IMU error` instead, or the banner repeats on its own, see
   `HANDOFF.md` — those are both documented, previously-seen failure modes.
6. Type `p` and press enter — you should get one `t_us=... x=... y=... z=...`
   line back immediately. Type `b` to walk the whole start sequence with no
   button wired: you should see `SEQ,armed`, then `SEQ,marks`, `SEQ,set`,
   `SEQ,go` and a dump, with the buzzer sounding at each of the three. **Close the Serial Monitor before step 7** — only one
   process can hold the serial port at a time.
7. Run the automated check, board sitting still on the desk:
   ```bash
   python3 Tools/verify_rate.py --seconds 5
   ```
   It must end in **PASS**. A healthy board reports `CLOCKSTEP,8`, `DROPPED,0`,
   an effective rate near **863 Hz** and `gaps: 0`. This is the single command
   that proves the whole capture path, so run it after any firmware change.

**Serial protocol** — `o` "on your marks", `s` "set" (also arms the recording),
`g` "go" (the reference marker for the push-off), `S` stop and dump, `r` arm a
recording without markers, `p` one immediate reading. Each marker is timestamped
with the firmware's own `micros()`, the same clock the samples use.

**If something looks wrong with the clock**, `Arduino/ClockCheck/ClockCheck.ino`
is a standalone sketch that reports which core it was built with and measures
`micros()` resolution directly. It answers in three seconds what is otherwise
very easy to misdiagnose.

---

### Wiring for the start sequence

Both parts are optional for a bench rate check and required for a real start.
Breadboard is fine.

| Part | Wiring |
|---|---|
| **Active** buzzer (with its own oscillator) | `+` to **D1**, `-` to **GND** |
| Momentary button | one leg to **D0**, the diagonally opposite leg to **GND** |

The button needs no resistor — D0 is configured `INPUT_PULLUP` and the button
pulls it to ground. On a 4-pin tactile button the pins are paired internally;
using two legs on the *same* side gives a permanently closed circuit, so take
them diagonally opposite. D0/D1 are clear of the IMU's I2C and of the UART on
D6/D7.

The buzzer must be an **active** one. A passive buzzer needs a driven waveform
(`tone()`/PWM), which adds an unmeasured delay to the very instant that defines
the reaction time's zero.

---

## 2. Python tooling — `Tools/capture.py`, `Tools/verify_rate.py`, `Tools/start_detector.py`

**Tested with:**

| Package | Version |
|---|---|
| Python | 3.13.13 (3.10+ should work) |
| pyserial | 3.5 |
| matplotlib | 3.11.1 |
| numpy | 2.5.2 |
| pandas | 3.0.5 (needed by `start_detector.py`) |

**Install**
```bash
pip3 install pyserial matplotlib numpy pandas
```

**Run — capture** (board flashed with `AccelStream.ino`, plugged in, Serial
Monitor closed):
```bash
python3 Tools/capture.py                       # autodetects the serial port
python3 Tools/capture.py --port /dev/cu.usbmodemXXXX --outdir Data
```
Then press the board's button and run the start. The script only prints the
board's progress (`SEQ,armed` / `marks` / `set` / `go`) and writes each dump to
`Data/accel_<timestamp>.csv`; it holds no recording state and makes no timing
decision, which is the point. It stays running, so you can do attempt after
attempt without restarting it.

**Run — verify the board and the capture path** (the first thing to run after
flashing; see step 7 of the firmware section):
```bash
python3 Tools/verify_rate.py --seconds 5      # autodetects the port; must print PASS
```
Checks clock resolution, effective sample rate, dropped samples, gaps, timestamp
monotonicity and clipping in one shot.

**Run — detection and tuning on a recorded CSV:**
```bash
python3 Tools/start_detector.py                       # GUI, browse to a file
python3 Tools/start_detector.py Data/accel_X.csv      # GUI, preloaded
python3 Tools/start_detector.py "Data/*.csv" --cli    # batch, terminal only
```
The GUI has every threshold in the sidebar and redraws on **Analyse**, so
tuning is a loop against real captures rather than an edit-rerun cycle. Three
panels share an x axis: raw x/y/z, the **horizontal-plane magnitude the
detector actually decides on** with its floors drawn, and the STA/LTA ratio with
its on/off thresholds. The set→go window is shaded — anything detected inside it
is a false start.

The sample rate is measured from each capture's own timestamps, so files
recorded at different rates all work with no flags, and the older
`t_s,t_us,x_g,y_g,z_g,host_iso` captures still load.

**The thresholds are not yet tuned against real on-block data** — read the
module docstring before changing them.

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
