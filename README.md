# 🏁 Reaction-Time System

**A low-cost, portable reaction-time and photo-finish system for track & field, built around two low-power microcontrollers and a companion app.**

> On 4 August 2024, Noah Lyles won the Olympic 100 m final in Paris by **five thousandths of a second** (0.005 s) over Kishane Thompson — a margin smaller than a single frame of standard video. Reaction time is one of the few things an athlete can deliberately train, yet the instruments precise enough to measure it are reserved for elite competitions and cost hundreds of thousands of krona. This project closes that gap.

---

## 🖼️ System at a glance

![System overview v2: the start block and finish block (each a XIAO nRF52840 Sense with button, OLED, speaker, IMU, battery, in a compact enclosure) linked by Zigbee, the finish block relaying to the phone app, and the eight-step end-to-end flow from connection and clock calibration, through the local start sequence and reaction-time measurement, to AI torso-crossing analysis and results on the phone.](Docs/reaction_time_diagram_v2.png)

*Both blocks compute and timestamp locally in microseconds on their own clock. The phone is used only for calibration, receiving results, recording video, and AI post-processing.*

---

## 📖 Overview

This repository contains the design and implementation of a **two-part system**:

1. 🔌 **Starting device** — a small, battery-powered unit at the blocks that plays a randomized "on your marks – set – go" sequence, detects the athlete's push-off with an onboard accelerometer, and computes reaction time locally with microsecond-level precision.
2. 📱 **Photo-finish companion app** — uses the phone's own camera at the finish line to reconstruct finish order and total race time. The free tier relies on the user manually marking each athlete's torso crossing the line; the premium tier automates that with computer vision and sub-frame interpolation.

The goal isn't to replace certified competition timing systems, but to bring a meaningful fraction of their precision — enough to be genuinely useful for training, testing, and local competitions — down to a price point and portability that a club or an individual athlete can actually afford. 💪

Costs, pricing, and a rough revenue projection are laid out in [Business Plan](#-business-plan) below. A deeper technical write-up and design-alternative comparison may exist as a separate course report; it isn't part of this repository.

---

## 👥 Team

**KTH Royal Institute of Technology — Technology and Health Project Course, Group 1**

- Arianna Bartocci
- Duarte Chambel
- Lorenzo Galli
- Loke Enlund Östangård
- Ninad Purekar

---

## ⚙️ How the System Works

The architecture went through a real pivot during development, driven by two things we actually measured rather than assumed — see [Open Risks](#-open-risks--things-to-validate) for the details:

1. **No Wi-Fi.** A direct Wi-Fi (SoftAP + UDP) link between the starting device and the phone was the original plan, but it asks the user to join a device-hosted network before every session — a setup step that doesn't belong in a "turn it on and go" product. Dropped in favor of always-on, pair-once radios.
2. **BLE alone doesn't have the range.** Real-world testing put BLE's reliable range at **~25 m** — far short of the 100 m between a sprint start and a finish line. A single BLE hop can't bridge that.

The current design splits the link into two hops instead of asking one radio to do both jobs:

```
Start unit (XIAO #1)  --Zigbee-->  Finish unit (XIAO #2)  --BLE-->  Phone (Flutter app)
```

- **Start unit** — sits at the blocks. Button trigger, onboard speaker for the "on your marks – set – go" sequence, and an IMU (accelerometer) that detects the push-off. Reaction time is computed **locally**, on the same clock that generated the "go" cue, so BLE/Zigbee latency downstream never touches the measurement itself.
- **Finish unit** — sits at (or near) the finish line, within BLE range of the phone. Receives the start unit's timestamp over **Zigbee** (longer range and low power, well suited to a fixed point-to-point link) and relays it to the phone over **BLE** — so the phone only ever needs to be near the finish line, never the start.
- **Phone (Flutter app)** — displays the reaction time, logs it, and runs the photo-finish pipeline against its own camera feed.

This split is under active validation right now — the open question is whether the two independent microcontrollers' clocks can be synchronized precisely enough across the Zigbee hop for reaction-time-grade timing. See [Open Risks](#-open-risks--things-to-validate).

### Key design decisions ⚖️

| Component | Chosen approach | Why |
|---|---|---|
| Push-off detection | **IMU** (accelerometer) on device body | Cheap, no block modification needed, easy retrofit — vs. a force/pressure sensor behind the pedal (more accurate, but invasive) |
| Start sequence | **Onboard speaker**, locally generated | Zero sync uncertainty between "go" cue and measurement clock, works without a human starter |
| Start ↔ Finish link | **Zigbee** | Longer reliable range than BLE at low power, for a fixed point-to-point link that doesn't need a phone in the middle |
| Finish ↔ Phone link | **BLE** | Short-range but simple and universal; the phone only needs to be near the finish line, not the start |

---

## 📊 Key Performance Indicators

- **Reaction time** — µs-precision, from "go" cue to detected push-off
- **Ground contact / flight time** *(future extension)*
- **Finish-line crossing order & total race time** — via the photo-finish extension
- **Clock synchronization stability** across trials — both device-to-device (Zigbee, start ↔ finish) and device-to-phone (BLE)

---

## 🧰 Hardware

### Start unit

| Component | Purpose |
|---|---|
| Seeed XIAO nRF52840 Sense | MCU + onboard 6-axis IMU (LSM6DS3), BLE / 802.15.4 radio |
| Push button | Manual trigger |
| Class-D amplifier + speaker | Start-sequence playback |
| LiPo battery + charge circuit | Portable, multi-hour use |

### Finish unit

| Component | Purpose |
|---|---|
| Seeed XIAO nRF52840 Sense | Zigbee ↔ BLE bridge to the phone |
| LiPo battery + charge circuit | Portable, multi-hour use |

Both units are the same board family, which keeps the bill of materials simple and the firmware toolchain identical between them.

---

## 💻 Software Stack

- **Firmware (implemented today):** Arduino IDE (C/C++), the vendored Seeed `LSM6DS3` library, `Wire.h` (I2C) — see `Arduino/AccelStream/AccelStream.ino`, the current no-BLE accelerometer capture firmware used for on-block validation.
- **Firmware (not yet implemented):** the Zigbee start↔finish link and the finish-unit's BLE bridge to the phone described above don't exist in this repo yet — likely to need Nordic's own SDK/Zephyr for the 802.15.4/Zigbee stack on the nRF52840, rather than the plain `ArduinoBLE` library. Tracked on the [backlog](https://github.com/users/lorenzogalli-dev/projects/4).
- **Companion app:** Flutter (Dart) — `flutter_blue_plus` for BLE.
- **Capture/analysis tooling:** Python (`Tools/accel_live.py`, `Tools/csv_plot.py`) — see [BUILD.md](./BUILD.md) for exact versions and setup.
- **Photo-finish AI pipeline:** computer-vision torso-crossing detection + sub-frame interpolation *(premium tier, planned)*.

---

## 💰 Business Plan

*Illustrative figures for this course project's business case — not a funded venture. All amounts in SEK, with a rough USD equivalent at ~10.5 SEK/USD for context.*

### Cost side

**Hardware (bill of materials, per full kit = 1 start unit + 1 finish unit):**

| Item | Qty | Unit cost | Line cost |
|---|---|---|---|
| Seeed XIAO nRF52840 Sense | 2 | 180 SEK (~$17) | 360 SEK |
| Enclosure, button, speaker, misc. wiring | 1 set | 150 SEK | 150 SEK |
| LiPo battery + charge circuit | 2 | 70 SEK | 140 SEK |
| Assembly & QA overhead | — | 150 SEK | 150 SEK |
| **Total COGS per kit** | | | **~800 SEK (~$76)** |

**Development ("us as programmers"):** built by a 5-person team over one KTH course term — sweat equity, not a cash cost at this stage. If this moved past the course into an actual venture, a realistic estimate to take the current prototype to a manufacturable v1 (firmware hardening, Zigbee bring-up, enclosure design, app polish, compliance testing) is **~1,200,000 SEK (~$114,000)** over 6 months, mostly salaries for a small team.

### Revenue side

**Hardware price:** 1,990 SEK (~$190) per kit, retail — well under the "hundreds of thousands of kronor" professional systems cost from the pitch at the top of this README. Gross margin per kit: ~1,190 SEK (~60%).

**App — freemium:**

| Tier | Price | What you get |
|---|---|---|
| **Free** | 0 SEK | Reaction time display + manual photo-finish (tap to mark torso crossing). Shoe-brand banner ads. |
| **Premium — monthly** | 79 SEK/mo (~$7.5) | + Multi-athlete logging, AI-automated photo-finish, no ads |
| **Premium — 6 months** | 399 SEK (~66 SEK/mo) | Same as monthly, discounted for committing |
| **Premium — annual** | 699 SEK (~58 SEK/mo) | Same as monthly, best per-month rate |
| **Club / Coach plan** | 2,499 SEK/year | Everything in Premium, up to 25 athlete profiles, exportable session history, priority support |

**Advertising:** free-tier users see sponsored shoe-brand placements in the app (a banner plus a post-session "your shoes" card) — a secondary revenue line that subsidizes the free tier without paywalling the core reaction-time feature.

### Market size & a rough projection

*Assumptions, not cited research — a ballpark to size the opportunity.*

- **TAM:** ~50,000,000 people worldwide train or compete in track & field at school, club, or amateur level.
- **Target Year-3 penetration:** a deliberately modest 0.02% → **10,000 hardware kits** sold and **~15,000** active app users in total (some app-only, e.g. on a club-owned kit, or using the free tier without ever buying hardware).
- **Assume** 20% of active app users convert to some Premium tier, at a blended average of ~600 SEK/user/year across the monthly/6-month/annual mix.

| Line | Volume | Unit revenue | Total |
|---|---|---|---|
| Hardware kits | 10,000 | 1,990 SEK | 19,900,000 SEK |
| Premium subscriptions | 3,000 users | ~600 SEK/yr | 1,800,000 SEK |
| Ad revenue (free tier) | 12,000 users | ~20 SEK/yr | 240,000 SEK |
| **Total Year-3 revenue** | | | **~21,940,000 SEK (~$2.1M)** |
| Hardware COGS | 10,000 | 800 SEK | 6,000,000 SEK |
| **Gross profit (before opex)** | | | **~15,940,000 SEK (~$1.5M)** |

This is a back-of-the-envelope model to size the opportunity, not a forecast — customer-acquisition cost, support, warranty returns, and the Zigbee clock-sync risk above all sit outside it.

---

## 📂 Repository Structure

```
Reaction-Time-System/
├── Arduino/                     # Arduino sketches (.ino), one folder per sketch
│   ├── AccelStream/             # current firmware: accelerometer capture, no BLE
│   ├── SerialEchoTest/          # minimal hardware/cable sanity check
│   ├── I2C_Scanner/             # I2C bus debug sketch
│   ├── Reaction_HardwareTest/   # TFT/buzzer/XBee hardware bring-up test
│   └── libraries/               # vendored board libraries (Seeed LSM6DS3)
├── Flutter App/
│   └── prostart/                # Flutter companion app
├── Tools/                       # Python capture/analysis scripts
│   ├── accel_live.py            # live view + record, pairs with AccelStream.ino
│   └── csv_plot.py              # offline CSV viewer
├── Data/                        # recorded CSV captures and their plots
├── Docs/                        # diagrams and figures
├── BUILD.md                     # exact versions and how to run every component
├── HANDOFF.md                   # working notes for an agent picking this up
└── README.md
```

---

## 🚀 Getting Started

### Firmware
1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add your board's package URL under **Preferences → Additional Board Manager URLs**
3. Install the board package via **Tools → Board → Boards Manager**
4. Open a sketch from `Arduino/`, select the correct board & port, and **Upload**

### App
1. Install [Flutter](https://docs.flutter.dev/get-started/install)
2. `cd "Flutter App/prostart" && flutter pub get`
3. `flutter run`

For exact tool/library versions and the full step-by-step for both firmware and the Python capture tooling, see **[BUILD.md](./BUILD.md)**.

---

## ⚠️ Open Risks / Things to Validate

What we're actually seeing right now, not a wishlist:

- ❌ **Wi-Fi is out.** Ruled out on UX grounds (see [How the System Works](#️-how-the-system-works)) — not being pursued further.
- 📏 **BLE range measured at ~25 m max**, confirmed in real testing — this is what forced the two-hop Zigbee + BLE design.
- 🔄 **Zigbee start↔finish link — in progress.** We're now building and testing the two-XIAO link; the open question is whether the two boards' independent clocks can be synchronized tightly enough over Zigbee for reaction-time-grade precision. Not yet validated.
- 🔊 **Speaker audibility** on an active, noisy track, at range.
- 🔋 **Battery life** under real, extended use, for both units.
- 🛠️ **Firmware reliability.** An earlier firmware combining BLE, a hardware FIFO, and a software PLL for timestamping turned out to hang or crash-loop unpredictably on two separate boards; the root cause was never isolated, so it was replaced with a deliberately minimal, no-BLE accelerometer firmware (`Arduino/AccelStream/AccelStream.ino`) that's now verified working on real hardware. Full write-up in `HANDOFF.md`. The lesson for the Zigbee work ahead: add complexity one piece at a time, testing after each addition, rather than integrating everything at once.

---

## 🗺️ Roadmap

Day-to-day tasks and priorities live on the project backlog, not here:

👉 **[GitHub Projects backlog](https://github.com/users/lorenzogalli-dev/projects/4)**

---

## 🔮 Future: how an athlete actually starts a race

The end-to-end experience we're building toward:

1. The coach or athlete opens the app and pairs with the finish unit once over BLE — the finish unit is already paired to the start unit over Zigbee, so this is a one-time setup, not a per-session ritual.
2. The athlete gets into the blocks. Someone taps **Start** in the app, or presses the physical button on the start unit directly — no phone needed at the blocks.
3. The start unit plays "on your marks… set…" with a randomized delay, then "go" — and the reaction-time clock starts at the exact instant the sound leaves the speaker.
4. The IMU detects the push-off; the start unit computes reaction time locally and sends it over Zigbee to the finish unit, which relays it to the phone over BLE.
5. The phone shows the reaction time immediately, and — if the camera was recording — lets the user tag the torso crossing for a total time, automatically on Premium.
6. Everything is logged to that athlete's profile: viewable individually on Free, or across a whole squad on a Coach plan.

No Wi-Fi network to join, no manual clock-calibration step for the user — just press start and run.

---

## 📚 References

- World Athletics, *Competition and Technical Rules* — reaction-time threshold for false starts (100 ms)
- Pain, M. T., & Hibbs, A. (2007). Sprint starts and the minimum auditory reaction time. *Journal of Sports Sciences*, 25(1), 79–86.
- Brosnan, K. C., Hayes, K., & Harrison, A. J. (2017). Effects of false-start disqualification rules on response-times of sprinters and possible sex differences.
- Official results and photo-finish image, Men's 100 m Final, Paris 2024 Olympic Games (Omega Timing / World Athletics)

---

## 📄 License

