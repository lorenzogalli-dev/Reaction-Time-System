# 🏁 Reaction-Time System

**A low-cost, portable reaction-time and photo-finish system for track & field, built around a single microcontroller.**

> On 4 August 2024, Noah Lyles won the Olympic 100 m final in Paris by **five thousandths of a second** (0.005 s) over Kishane Thompson — a margin smaller than a single frame of standard video. Reaction time is one of the few things an athlete can deliberately train, yet the instruments precise enough to measure it are reserved for elite competitions and cost hundreds of thousands of krona. This project closes that gap. 🎽

---

## 📖 Overview

This repository contains the design and implementation of a **two-part system**:

1. 🔌 **Starting device** — a small, battery-powered, Wi-Fi-connected unit that plays a randomized "on your marks – set – go" sequence, detects the athlete's push-off from the blocks, and computes reaction time locally with microsecond-level precision.
2. 📱 **Hardwareless AI photo-finish extension** — uses nothing more than a smartphone camera at the finish line, combined with computer-vision torso-crossing detection and sub-frame interpolation, to reconstruct finish order and total race time — no extra dedicated hardware required.

The goal isn't to replace certified competition timing systems, but to bring a meaningful fraction of their precision — enough to be genuinely useful for training, testing, and local competitions — down to a price point and portability that a club or an individual athlete can actually afford. 💪

Full technical write-up, design-alternative comparisons, cost analysis and business model: see [`report/main.tex`](./report/main.tex) (or the compiled PDF).

---

## 👥 Team

**KTH Royal Institute of Technology — Technology and Health Project Course, Group 1**

- Arianna Bartocci
- Duarte Chambel
- Lorenzo Galli
- Loke Enlund Östangård
- Ninad Purekar

---

## 🏗️ System Architecture

```
┌─────────────────────┐         Wi-Fi (UDP, SoftAP)         ┌──────────────────────┐
│   Starting Device    │ ───────────────────────────────────▶│   Athlete's Phone     │
│   (ESP32 + IMU)       │◀─────────────────────────────────── │   (Flutter app)       │
│                       │      clock calibration (RTT)         │                       │
│  • Push-off detection │                                       │  • Displays reaction  │
│  • "Go" signal (µs)   │                                       │    time               │
│  • Onboard speaker    │                                       │  • Records finish-line│
│  • OLED display       │                                       │    video (Phase 2)    │
└─────────────────────┘                                       └──────────────────────┘
```

### Key design decisions ⚖️

| Component | Chosen approach | Why |
|---|---|---|
| Push-off detection | **IMU** (accelerometer) on device body | Cheap, no block modification needed, easy retrofit — vs. a force/pressure sensor behind the pedal (more accurate, but invasive) |
| Start sequence | **Onboard speaker**, locally generated | Zero sync uncertainty between "go" cue and measurement clock, works without a human starter — vs. microphone listening to an external starter |
| Device ↔ phone link | **Direct Wi-Fi (SoftAP + UDP)** | Simpler BOM and setup than a two-node relay — at the cost of needing ~200 m of reliable direct range, our single largest technical risk |

Bluetooth (BLE, including BLE 5 Long Range / Coded PHY) was also evaluated as a link alternative and remains a candidate for a future, lower-power revision — see [Open Risks](#-open-risks--things-to-validate) below.

---

## 📊 Key Performance Indicators

- **Reaction time** — µs-precision, from "go" cue to detected push-off
- **Ground contact / flight time** *(future extension)*
- **Finish-line crossing order & total race time** — via the photo-finish extension
- **Clock-offset stability** across trials (device ↔ phone synchronization)

---

## 🧰 Hardware

| Component | Purpose |
|---|---|
| ESP32-WROOM-32U (external antenna) | Main MCU — Wi-Fi range is the priority |
| 6-axis IMU (MPU6050 / ICM-42688, 500 Hz–1 kHz ODR) | Push-off detection |
| 0.96" OLED display | Local reaction-time / status readout |
| Class-D amplifier + speaker | Start-sequence playback |
| LiPo battery + charge circuit | ~6–10 h continuous use |
| Push button | Manual trigger |

> 🧪 **Currently prototyping on a Seeed XIAO nRF52840** (BLE-only, no Wi-Fi) while validating IMU wiring, BLE range, and firmware basics — the final hardware target for the Wi-Fi-based architecture above is the ESP32-WROOM-32U.

---

## 💻 Software Stack

- **Firmware:** Arduino IDE (C/C++), `ArduinoBLE`, `Wire.h` (I2C), board-specific IMU libraries
- **Companion app:** Flutter (Dart) — `flutter_blue_plus` (BLE) / `dart:io RawDatagramSocket` (Wi-Fi/UDP)
- **Photo-finish AI pipeline:** computer-vision torso-crossing detection + sub-frame interpolation *(planned, Phase 2)*

---

## 📂 Repository Structure

```
Reaction-Time-System/
├── firmware/              # Arduino sketches (.ino), one folder per sketch
│   ├── I2C_Scanner/
│   ├── IMU_Test/
│   └── ReactionTimeBLE/
├── app/                    # Flutter companion app
├── report/                 # LaTeX project report (main.tex + figures)
└── README.md
```

---

## 🚀 Getting Started

### Firmware
1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add your board's package URL under **Preferences → Additional Board Manager URLs**
3. Install the board package via **Tools → Board → Boards Manager**
4. Open a sketch from `firmware/`, select the correct board & port, and **Upload**

### App
1. Install [Flutter](https://docs.flutter.dev/get-started/install)
2. `cd app && flutter pub get`
3. `flutter run`

---

## ⚠️ Open Risks / Things to Validate

Flagged early as the main items to test experimentally before committing to the final design:

- 📶 **Wi-Fi range** of the direct SoftAP link at up to 200 m in open field — the single largest architectural risk
- 🔊 **Speaker audibility** on an active, noisy track, at range
- ⏱️ **Clock-offset stability** across trials and over a full training session
- 🔋 **Battery life** under real, extended use
- 📡 **BLE Long Range (Coded PHY) feasibility** on our current dev board/library combo — early testing suggests this may need extra work beyond the standard `ArduinoBLE` library

---

## 🗺️ Roadmap

- [x] Validate dev board toolchain (Arduino IDE + board support)
- [ ] Confirm IMU presence/wiring on current dev board
- [ ] Test standard BLE range (baseline)
- [ ] Finalize component list & assemble breadboard prototype
- [ ] Reaction-time firmware: push-off detection, "go" signal, on-device display
- [ ] Flutter app: BLE/Wi-Fi connection, reaction-time display
- [ ] **Phase 2:** photo-finish video capture, torso-crossing detection, sub-frame interpolation, time alignment
- [ ] End-to-end validation against photocell timing gates (Bosön)

---

## 📚 References

- World Athletics, *Competition and Technical Rules* — reaction-time threshold for false starts (100 ms)
- Pain, M. T., & Hibbs, A. (2007). Sprint starts and the minimum auditory reaction time. *Journal of Sports Sciences*, 25(1), 79–86.
- Brosnan, K. C., Hayes, K., & Harrison, A. J. (2017). Effects of false-start disqualification rules on response-times of sprinters and possible sex differences.
- Official results and photo-finish image, Men's 100 m Final, Paris 2024 Olympic Games (Omega Timing / World Athletics)

---

## 📄 License

*(add your chosen license here — e.g. MIT)*
