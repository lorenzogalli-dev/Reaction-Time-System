# XBee link range & reliability — test and findings

The method, the raw captures, and the conclusion for one question. This covers
the **start unit ↔ finish unit** radio link only; the finish unit ↔ phone BLE
hop is separate (measured at ~25 m — see the root `README.md`).

## Hardware under test

- 2 × Seeed XIAO nRF52840 Sense
- 2 × XBee / XBee-PRO **S2C**, standard power (~+5 dBm / ~3 mW), through-hole,
  **PCB-trace antenna** — `XB24CZ7PIT-004`. 2.4 GHz Zigbee (ZB stack, EM357).
  FCC + ETSI approved, legal in SE/EU.
- Firmware family in XCTU: **XB24C (Z7) — Zigbee**, *not* XBee3.

> The PCB-trace antenna is the weakest option in the S2C line. If range falls
> short, the first mitigation is the wire-whip / U.FL variant (`XB24CZ7WIT`),
> then XBee-PRO S2C (+18 dBm). "Doesn't clear 200 m" is a valid result that
> tells us which upgrade to buy — not a blocked task.

## XCTU configuration (both modules, once)

| Param | SENDER module | RECEIVER module | Note |
|---|---|---|---|
| Firmware | Zigbee Coordinator API | Zigbee Router API | must be the same S2C family or they will not pair |
| `ID` (PAN ID) | same nonzero value | same nonzero value | e.g. `0x2B2B` |
| `AP` | 1 | 1 | API enabled, **unescaped** — the parser does not unescape |
| `AO` | 0 | 0 | plain `0x90` RX frames |
| `BD` | **7 (115200)** | **7 (115200)** | matches `XBEE_BAUD` in the sketch — *not* the 9600 factory default, see below |
| `CH` | — | — | record the joined channel in the log |

> ⚠️ **Baud ordering — get this wrong and the link goes dead.**
> A fresh module is `BD=3` (9600). Change it in this order:
> 1. Talk to each module **at 9600** (the passthrough sketch's `LINK_BAUD` is
>    9600 out of the box) and write `BD=7` to **both** — the module applies it
>    on `WR`/exit, so XCTU must then reconnect at 115200.
> 2. *Then* set `XBEE_BAUD = 115200` in `Xbee_RangeTest.ino` and reflash both
>    boards.
> 3. If you go back into XCTU afterwards, bump `LINK_BAUD` in
>    `Xbee_Passthrough.ino` to 115200 too, or it will not see the module.
>
> Changing `XBEE_BAUD` before `BD` leaves a 115200 XIAO talking to a 9600
> module: no frames, no echoes, nothing in the log.

**Why 115200 and not the 9600 default.** One ping is four UART transactions,
not one: sender TX ping (29 B) → receiver RX ping (27 B) → receiver ATDB query
(8 B) + reply (10 B) → receiver TX echo (29 B) → sender RX echo (27 B). At 9600
(~1.04 ms/byte) that is **~135 ms of serial per round trip**, against a radio
contribution of a few ms. RTT would land at ~140 ms — *longer than the 100 ms
`PING_PERIOD_MS`*, so pings back up and the RTT-vs-distance curve measures the
backlog; the receiver would spend ~77 ms of every 100 ms window shifting bytes;
and the ~19 ms ATDB round trip leaves almost no margin under the 40 ms
`DB_TIMEOUT_MS`, so RSSI readings start dropping. The whole walk test would
report UART saturation as radio range. At 115200 the same 135 ms becomes ~11 ms
and the radio is what is measured.

Export both XCTU profiles (`.xpro`) and commit them next to this file so the
config is reproducible.

## Wiring

XBee is 3.3 V; the XIAO is native 3.3 V, so **no level shifter**.

| XBee pin | XIAO |
|---|---|
| 1 VCC | 3V3 |
| 10 GND | GND |
| 2 DOUT | D7 (Serial1 RX) |
| 3 DIN | D6 (Serial1 TX) |

## Procedure

0. **Get XCTU onto the modules.** Our adapter is a **Parallax 32403** — a plain
   2 mm-to-0.1" breakout with *no USB chip*, so there is no COM port for XCTU
   to open. Flash `Arduino/Xbee_Passthrough/` to a XIAO instead: it bridges USB
   ↔ `Serial1` byte-for-byte, so the XIAO *is* the USB-to-serial adapter.
   Wiring is the same D6/D7 map as the range test. Configure the two modules
   **one at a time** (one UART per board).
   - If XCTU's *Discover radio modules* finds nothing, use **Add a radio
     module** and set the port by hand: `LINK_BAUD` (9600 at first), 8-N-1, no
     flow control. Discovery sweeps baud rates and expects command-mode
     answers, which does not always survive a bridge.
   - Reflash `Xbee_RangeTest` over it when you are done.
1. **XCTU config + bench loopback.** Set `BD=7` on both modules per the ordering
   warning above, then `Arduino/Xbee_RangeTest/Xbee_RangeTest.ino` — flash one
   board as `ROLE_SENDER` and the other as `ROLE_RECEIVER` (use the
   `--build-property` line in `Arduino/Xbee_RangeTest/sketch.yaml` rather than
   editing the `.ino` between boards), power both ~1 m apart, and check each
   board's `# role:` banner. Expect the SENDER's log to print `# peer found`
   within a second and PDR ≈ 100%.
2. **Walk test.** SENDER fixed at the start line. RECEIVER carried away in
   steps: **10, 25, 50, 75, 100, 150, 200 m** (add 250/300 if still passing).
   At least ~300 packets per point (~30 s at 10 Hz).
   - **Default: tether the SENDER only.** The laptop stays at the start line;
     the RECEIVER runs off a power bank. The two boards end up hundreds of
     metres apart, so one host cannot reach both — a USB hub does not span
     200 m. This still yields **uplink PDR, MAC retry count, RTT** and the
     **receiver's RSSI**, which the receiver carries back inside the echo.
     Round-trip PDR is a conservative proxy for one-way.
   - **Optionally also tether the RECEIVER**, which needs a *second laptop* at
     the far end running its own copy of the logger. It adds the two things
     the sender log cannot give: one-way **downlink PDR** measured at the far
     end, and clean one-way **inter-arrival jitter** (`dt_us`). Without it you
     have only RTT jitter, which folds both radio hops and the receiver's ATDB
     turnaround into a single number — usable as a trend, but not a clean
     one-way figure.
   - `python3 Tools/xbee_range_log.py --distance 0`, then type `d 25`, `d 50`,
     … at each point. Each `d` resets the board + host counters and starts a
     new segment.
3. **Two antenna heights** at each distance: on the ground, and on a ~1 m
   mast. Ground reflection / Fresnel-zone clearance changes the result a lot.
4. **Note the environment**: open field vs track, people present, weather, and
   whether a phone with BLE/Wi-Fi is active nearby (2.4 GHz coexistence — the
   finish unit will have exactly that).
5. **Analyse.** `python3 Tools/xbee_range_plot.py Data/xbee_range_*.csv` →
   PDR / RSSI / RTT vs distance, and the max distance meeting the criterion.

## Pass criteria

- **PDR ≥ 95%** at the target distance, retries/ACK allowed (the real payload
  is one tiny reaction-time value — cheap to resend).
- Log the **full RTT distribution** (min / median / p95 / jitter) and **retry
  count** per distance point, not just PDR. Rising **min-RTT or jitter** is the
  leading indicator for clock-sync degradation, which matters more here than
  raw PDR: reaction time is computed from synced on-device clocks, not from
  radio latency.

## Results

_To fill in after the walk test._

| distance (m) | antenna height | uplink PDR | round-trip PDR | mean retries | RTT min / p50 / p95 (µs) | RSSI mean / worst (dBm) |
|---|---|---|---|---|---|---|
| 10 | | | | | | |
| 25 | | | | | | |
| 50 | | | | | | |
| 75 | | | | | | |
| 100 | | | | | | |
| 150 | | | | | | |
| 200 | | | | | | |

**Max usable range (PDR ≥ 95%):** _tbd_
**Clears the 200 m target?** _tbd_
**If not — recommended upgrade:** _wire-whip antenna / XBee-PRO_
**Coexistence / jitter observations:** _tbd_

Raw captures: `Data/xbee_range_*.csv`. Figures: `Data/xbee_range_*.png`.
