# AlgorithmRealTimeESP32 — ESP32-C3 SuperMini + BMI270

The start unit's firmware (`../AlgorithmRealTime`, XIAO nRF52840 Sense)
ported to an **ESP32-C3 SuperMini with an external BMI270**. It has the same
behaviour, screens, button logic, serial protocol and flash storage. The
XIAO folder is untouched.

**Status (2026-10-07): compiles, never run on hardware.** No ESP-NOW yet. This
port only does what the XIAO did. ESP-NOW is the next step, after the tests below.

## Build and upload

- Board: **Nologo ESP32C3 Super Mini** (core `esp32:esp32` 3.3.12). USB CDC
  On Boot is already "Enabled" on this board.
- `partitions.csv` in this folder is picked up automatically. It adds the
  `runs` partition (25 runs). Nothing to choose in the menu.
- CLI:
  ```
  arduino-cli compile --fqbn esp32:esp32:nologo_esp32c3_super_mini Arduino/AlgorithmRealTimeESP32
  arduino-cli upload  --fqbn esp32:esp32:nologo_esp32c3_super_mini -p /dev/cu.usbmodemXXXX Arduino/AlgorithmRealTimeESP32
  ```
  (`arduino-cli` is inside the Arduino IDE app; see BUILD.md.) Last build: 27% flash, 45% RAM.
- If the board is off (light sleep) the USB port is gone. Switch it on, or hold
  the button (or BOOT, same pin), tap RESET, release, then upload.
- On this Mac (2026-10-07) Arduino's `ctags` is x86 and there is no Rosetta.
  `--build-property tools.ctags.pattern=/usr/bin/true` is not enough for this
  sketch: it relies on the prototypes ctags generates. Either install Rosetta
  (`softwareupdate --install-rosetta`), which the IDE needs too, or compile a
  scratch copy with the prototypes added (how the pin change was checked).
- Python tools: `Python_Tools/` here, not the XIAO's. Their default is 800 Hz,
  and they write `sensor: bmi270` / `odr_hz: 800` into every CSV.

## Wiring

| From | Pin | ESP32-C3 SuperMini | Note |
|---|---|---|---|
| BMI270 | 3V3 | 3V3 | |
| BMI270 | GND | GND | |
| BMI270 | SDA | **GPIO0** | |
| BMI270 | SCL | **GPIO3** | |
| BMI270 | INT1 | **GPIO20** | data-ready. **Required**: the sampling runs off it |
| BMI270 | SDO | GND | address **0x68** (SDO to 3V3 makes it 0x69) |
| BMI270 | CS | **3V3** | selects I2C; a floating CS can switch the chip to SPI |
| BMI270 | INT2, OCS, OSDO, SCX, SDX | — | unconnected |
| Display | 1 GND | GND | |
| Display | 2 VCC | 3V3 | not 5V |
| Display | 3 SCK | **GPIO8** | |
| Display | 4 SDA | **GPIO10** | SPI MOSI, not I2C |
| Display | 5 RES | **GPIO5** | |
| Display | 6 RS | **GPIO6** | data/command |
| Display | 7 CS | **GPIO7** | |
| Display | 8 LEDA | **GPIO4** | backlight |
| Display | 9–14 | — | unconnected |
| Button | one side / other | **GPIO9** / GND | INPUT_PULLUP, same pin as BOOT |
| Piezo | (+) / (−) | **GPIO1** / **GPIO2** | antiphase, no GND |

Free: GPIO21. All pins are in `Pins.h`.

Why this map (the user's layout, 2026-10-07; an earlier one had the button on
1, INT1 on 2, the piezo on 8/9 and the display on 4/5/6/7/10/20):
- **Button on GPIO9**, the BOOT pin. Only GPIO0–5 can wake the C3 from deep
  sleep, so "off" is **light sleep**, which any pin can wake. Side effect:
  the button held while plugging in or resetting enters download mode (dark
  screen). Release and reset.
- **INT1 on GPIO20, not 21.** The ROM prints its boot log on GPIO21 at every
  reset; a BMI270 still configured from before the reset would drive INT1
  against it. GPIO20 is an input at boot.
- **Piezo on 1/2.** GPIO2 is a strapping pin, but a piezo is a capacitor and
  does not pull it at reset.
- **SCK on GPIO8**, which also sinks the blue LED from 3V3. The display runs
  in **SPI mode 3** so the clock idles HIGH and the LED stays off between
  draws (it flickers while drawing). If the panel shows garbage in mode 3,
  go back to `SPI_MODE0` in `Display.h` and accept the LED.
- SPI goes through the GPIO matrix (8 MHz, well within its limit).
- **I2C pull-ups.** Only the C3's internal ones (~45 kΩ) are enabled. Measure
  SDA→3V3 on the breakout with power off: 2–10 kΩ means it has its own
  pull-ups. With no pull-ups, add 4.7 kΩ from SDA and from SCL to 3V3.

## What changed vs the XIAO

| | XIAO | ESP32-C3 |
|---|---|---|
| IMU | LSM6DS3TR-C, 833 Hz | BMI270, **800 Hz**, perf mode, bwp normal, ±16 g (same 0.488 mg/LSB) |
| IMU driver | Seeed library | `Bmi270.h`, register level. Uploads Bosch's 8 KB config blob (`Bmi270Config.h`, BSD-3) at each boot, reads back ODR/range |
| Timestamp | main loop, after seeing the flag | **inside the ISR** (`esp_timer`, 1 µs): no loop latency |
| Buzzer | nRF PWM, antiphase | LEDC always running + GPIO-matrix routing/inversion; still 2 register writes at the beep. Wave phase at the beep is random, 0–125 µs |
| Display | mbed::SPI 8 MHz | SPIClass 8 MHz, same queue and budget |
| Storage | external QSPI, nrfx | internal flash, `runs` partition, same 25 × 80 KB slots and format |
| Off | System OFF, few µA | light sleep (button on GPIO9 cannot wake deep sleep); "on" = restart after the 1.5 s hold. The SuperMini's red power LED stays on (~mA). BMI270 suspended, backlight held LOW |
| Serial with no terminal | the core drops the data | bounded: writes block ≤ 100 ms, a dump stops if nobody reads for 200 ms (the run is on flash anyway) |
| Dump header | — | + `SENSOR,bmi270`, `ODR,800` |

`StartDetector.h` and `AicPicker.h` are byte-identical. The detector works on
the component orthogonal to measured gravity, so the BMI270's mounting
orientation does not matter.

## Tests, in order

Each step has a pass criterion. Do not move on until it passes.

1. **Boot.** Open a serial monitor and look for:
   - `IMU OK - BMI270 accel 800 Hz`;
   - `clock: micros() resolution ~1 us`;
   - `flash: ... 0/25 run slots used`.

   If you get `IMU error - BMI270: <step>`, the step names the problem:
   - `chip id` → wiring, address or CS;
   - `INTERNAL_STATUS` → config upload.
2. **Rate.** Run `python3 Arduino/AlgorithmRealTimeESP32/Python_Tools/verify_rate.py`.
   Pass:
   - rate within 5% of 800 (the real value is the BMI270 oscillator's; write it down);
   - `DROPPED 0`;
   - no gaps;
   - dt std comparable to the XIAO's ~17 µs.

   If DROPPED > 0, the BMI270's data-ready edges are being missed. Report it,
   because the read/INT behaviour may need changing.
3. **Start on the bench**, with `b` and with the button:
   - The dump has `GAPS,1`, `MAXGAP` ~12 ms. That one gap is the pre-roll
     replay at "set", the same as on the XIAO.
   - More gaps around "Set"/"Go" means the display is stealing samples.
     Lower `DRAW_BUDGET_US`.
4. **Storage.** Run 2–3 starts and power cycle. Then:
   - `pull_captures.py` gets them all;
   - `L` lists them;
   - `E` reports erase times.
5. **Power.**
   - Hold 1.5 s: beep, "Nice session today!", screen dark, backlight off.
   - A short press stays off.
   - A 1.5 s hold switches on, with a beep; the screen comes up after the
     release (the board restarts once the button is up, see `enterSystemOff`).
   - Blue LED off while idle; it only flickers while the screen redraws
     (SPI mode 3). Screen garbled → `SPI_MODE0` in `Display.h`.
   - Measure the current drawn while off.
6. **No host.** Plug into a laptop with no terminal open and run a start. After
   the result, the board must answer the button within ~1 s.
7. **Real starts.** Block starts with an athlete: `GAPS,1`, sensible verdicts.

## Before this data can be compared with the XIAO's

- **Fixed delay of the BMI270's filter.** Every onset shifts by a constant
  that is not the LSM6DS3's. It has to be measured. One way: a contact closed
  by the same tap that hits the sensor, read on GPIO21 on the same clock. This
  is not in the firmware yet.
- **Thresholds.** The BMI270 has more noise (datasheet ~160 vs ~90 µg/√Hz).
  Measure `settled_mg` at rest, then re-run `sweep_threshold.py` on new
  captures. Until then the DET_* values are the XIAO's.
- **Buzzer acoustic latency**: still never measured, as on the XIAO.

## Next (not here yet)

ESP-NOW long range sending the t0 at "go", in this same sketch. Then repeat
test 2 with the radio transmitting: the C3 has one core, so the radio and the
sampling share it.
