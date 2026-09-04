# XBee link range & reliability — test and findings

Parallels `playground_IMU/`: the method, the raw captures, and the conclusion.
This covers the **start box ↔ finish box** radio link only. The finish box ↔
phone BLE hop is separate.

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
| `BD` | 3 (9600) | 3 (9600) | matches `XBEE_BAUD` in the sketch; raise both together if changed |
| `CH` | — | — | record the joined channel in the log |

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

1. **XCTU config + bench loopback.** `Arduino/Xbee_RangeTest/Xbee_RangeTest.ino`
   — set `RANGE_TEST_ROLE` to `ROLE_SENDER` on one board, `ROLE_RECEIVER` on
   the other, flash, power both ~1 m apart. Expect the SENDER's log to print
   `# peer found` within a second and PDR ≈ 100%.
2. **Walk test.** SENDER fixed at the start line (tethered to a laptop, or on
   battery). RECEIVER carried away in steps: **10, 25, 50, 75, 100, 150,
   200 m** (add 250/300 if still passing). At least ~300 packets per point
   (~30 s at 10 Hz).
   - Tether the **SENDER** to get uplink PDR + MAC retry count + full RTT
     distribution. Round-trip PDR is a conservative proxy for one-way.
   - Tether the **RECEIVER** as well (second laptop / USB hub) to also get
     clean one-way downlink PDR and inter-arrival jitter.
   - `python3 tools/xbee_range_log.py --distance 0`, then type `d 25`, `d 50`,
     … at each point. Each `d` resets the board + host counters and starts a
     new segment.
3. **Two antenna heights** at each distance: on the ground, and on a ~1 m
   mast. Ground reflection / Fresnel-zone clearance changes the result a lot.
4. **Note the environment**: open field vs track, people present, weather, and
   whether a phone with BLE/Wi-Fi is active nearby (2.4 GHz coexistence — the
   finish box will have exactly that).
5. **Analyse.** `python3 tools/xbee_range_plot.py data/xbee_range_*.csv` →
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

Raw captures: `data/xbee_range_*.csv`. Figures: `data/xbee_range_*.png`.
