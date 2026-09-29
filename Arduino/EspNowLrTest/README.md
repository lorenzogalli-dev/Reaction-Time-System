# EspNowLrTest — ESP-NOW long range between two ESP32-C3, measured in the field

Tests architecture **E** from `HANDOFF.md` (2026-09-22): ESP-A at the blocks,
ESP-B at the finish beside the phone, ESP-NOW between them. It answers three
questions, and touches nothing else in the repo:

1. **Range:** how many packets arrive at 25, 50 … 200 m, in LR 250k, LR 500k
   and plain 802.11b 1M. The three modes rotate every 5 s, so they are compared
   **at the same spot, in the same seconds, with the same bodies in the way**.
   On 22/09 two walks of the same path differed by 8 dB, so comparing modes
   across different walks would mostly measure that noise.
2. **The t0:** one fake t0 per 5 s slot, repeated every 250 ms until B
   acknowledges it. How many tries it takes is the number the product cares about.
3. **Coexistence**, the top open risk: can B keep the LR link to A up while also
   running an access point for the phone on the same channel?

It also measures the A↔B delay and clock offset on every echo (NTP-style),
which gives the jitter of the hop that will carry the t0.

## Files

| | |
|---|---|
| `EspNowLrTest/app.cpp` | the firmware, identical on both boards |
| `flash.sh` | compile and upload (Mac) |
| `logger.py` | runs on each laptop, Mac or Windows: logs to CSV and shows live numbers |
| `analyse.py` | per-station table from one walk: `python3 analyse.py <A.csv> <B.csv>` |
| `measurements/<date>/` | one CSV per laptop per run, `HHMMSS_A.csv` / `HHMMSS_B.csv` |

The role (A or B) is stored in the board's flash and set by the logger: start
`logger.py B` on a board that was A and it switches it and reboots it. Mark
the two boards with tape anyway (**A** / **B**) and do not swap them between
runs except in the run that swaps them on purpose: SuperMini antennas vary from
board to board.

## Before going out

On the Mac, with each board in turn:

```
./flash.sh
```

On both laptops, Python 3 and pyserial:

```
pip3 install pyserial          # Mac
py -m pip install pyserial     # Windows
```

Copy this folder to the Windows laptop (a USB stick is fine, or clone the repo).
Windows 10/11 sees the C3 as a COM port without drivers.

**Bench check, 10 minutes, the boards 1 m apart:**

```
python3 logger.py A     # laptop 1, board A
python3 logger.py B     # laptop 2, board B
```

On B, type `0` + Enter. (On the Windows laptop run it as `python logger.py B`
from the environment where pyserial is installed; `python3` and `py` may be
other interpreters.) After 30 s all three modes should read close to
100 %. **If a mode reads `--`, B cannot receive it** and the field test does
not make sense yet — stop and tell whoever wrote this. That is the one
assumption the firmware makes without having verified it: that a radio set to
b/g/n + LR receives all three at once.

Then the coexistence check, still on the bench: type `ap on` on B (the board
reboots), connect the phone to `PROSTART-LR` / `prostart123`, open
`http://192.168.4.1` in a normal browser tab, leave it for 10 minutes. The
page counts its own requests, the log gets a `W` line every 5 s. Compare the
percentages with and without. `ap off` goes back. **The setting is stored on the board:** type `ap off`
before closing the logger, or B will start the field test with the AP on.

## Power

Each board is powered by its laptop over USB, and each laptop logs its own
side. A C3 with the radio always on draws about 0.5 W: nothing for a laptop.

- **Keep both laptops awake with the lid open.** A closed Mac lid cuts USB
  power. Mac: `caffeinate -dims` in a second terminal. Windows: Settings →
  Power → Screen and sleep → *Never* while plugged in **and** on battery.
- A power bank for A is also fine, and more realistic (no laptop near the
  antenna), but then side A is not logged. Beware of power banks that switch
  off on low current.

## In the field (two people, about an hour)

**Setup.** A on the ground behind the block, antenna the same way every time,
on a 1-2 m USB cable; laptop 1 a metre to the side, not in front. B taped to
the top of a stick or tripod at **1-1.2 m**, where it will sit next to the
phone; laptop 2 at its foot. The 100 m straight has the lines already painted;
past it, pace or measure.

**A station.** Stand still, the person with B away from the line of sight. On
laptop 2 type the distance (`50` + Enter). Live numbers every 2 s; after 30 s
it beeps and prints `DONE`. Walk to the next one. Laptop 1 needs nothing typed:
it shows a rolling 15 s window of what comes back.

Stations: 0, 25, 50, 75, 100, 125, 150, 175, 200 m, and past it if the link
is still alive.

| run | what | why |
|---|---|---|
| 1-3 | the plain walk, identical three times | run-to-run spread |
| 4 | `ap on`, phone connected with the page open, carried with B | coexistence at range |
| 5 | the second person crouched over A, at 100 and 150 m only | the athlete on the blocks |
| 6 (optional) | boards swapped: `logger.py B` on board A and vice versa | link asymmetry |

Type notes as they happen: `n wind picking up`, `n bus parked at 120 m`. Write
down once per session: channel (1), where the antennas point, the height of B,
the weather. A photo of each setup.

**Pass criteria, fixed before the data and not after:** at 100 m, and ideally
at 150 m, every t0 acknowledged within 3 tries, and LR 250k delivering at
least 90 % of single packets.

## Reading the numbers live

```
[100 m    18s]  |  LR250:  98.0%  -86dBm  |  LR500:  95.5%  -86dBm  |  11b1M:  71.0%  -87dBm
```

A mode shows `--` for the first seconds of a station: modes rotate every 5 s
and it has not come round yet. After 15 s all three are there.

On laptop A the same line also has `t0 ok 3/3 max 2 try`. RSSI is the same
across modes, since the signal strength does not change, only how well it gets
decoded.

## Log format

Each CSV line is `host_s,<board line>`. The board lines are documented at the
top of `app.cpp`. `M,<distance>` starts a station and `M,-` ends it; `N,<text>`
is a note. Sequence numbers are deterministic (`seq` goes out at
`start + seq·50 ms`, mode `= (seq // 100) % 3`), so the two laptops' files are
joined by `seq` and need no common clock.
