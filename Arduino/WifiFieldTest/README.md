# WifiFieldTest — field rig for the Wi-Fi t0 link

One ESP32-C3, one `.cpp`, no app to install. The measuring instrument is a web
page the board serves to the phone; everything is in `WifiFieldTest/app.cpp`.

## What the link is for

**Not** the reaction time. That is computed on the XIAO and stays there. This
link carries the **t0 instant to the phone**, which films the photofinish and
has to anchor the video to it.

Two consequences worth keeping in mind before reading any number below:

- **Latency does not matter.** The message carries its own timestamp
  (`G <seq> <t0_us> <try>`), not a "now". Arriving two seconds late with the
  right instant is fine. What matters is *arriving at all*, and the clocks
  being aligned.
- **The phone is at the finish line**, because that is where it films. The
  blocks are at the start. That is where the 100 m requirement comes from — it
  is geometry, not performance.

## What it measures in one walk

| On the dashboard | Answers |
|---|---|
| RSSI, RTT median/p95, % lost | usable range |
| Reconnects, link state | does the phone drop the network |
| `Captive:` button | how invasive the connection is (NONE / PORTAL / LIE) |
| Clock offset ± jitter | NTP-style sync, min-delay over the last 60 round trips |
| t0 latency, attempts | delivery of a pushed t0, and how many retries it took |

The log is kept in the phone's own storage and restored on open, so a dropped
link, a reload or a tab the OS kills do not lose it. `Scarica CSV` exports it.

## Running it

```
./flash.sh            # compiles and uploads, finds the port itself
./flash.sh monitor    # ...and opens the serial afterwards
```

Then: phone → Wi-Fi `PROSTART-TEST` (`prostart123`) → `http://192.168.4.1` in a
**normal browser tab**, not the captive window. Press `Azzera` before a run.

Power it from a power bank and leave it at the blocks; walk with the phone. The
log lives on the phone, so the phone is the thing that has to come back.

### Two traps

- The captive portal window (mode `PORTAL`) is a cut-down browser and the OS
  **closes it when the network drops**, taking the log with it. It is there to
  test the UX, not to collect data. The firmware therefore boots in `NONE`.
- The `.ino` is empty on purpose. Arduino's `.ino` preprocessor generates
  function prototypes with ctags; a `.cpp` skips that step. See the note in
  `WifiFieldTest.ino`.

## Measurements

`measurements/20260922/` — first field session. `01_rssi-broken.csv` is kept
only as a record: RSSI never reached the page in that run, the firmware read it
and printed it to serial but never sent it. `03_full-run-70m.csv` is the usable
one. Results are in `HANDOFF.md`.
