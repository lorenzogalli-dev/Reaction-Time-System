#include <ArduinoBLE.h>
#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// Prostart / BlockStart firmware - XIAO nRF52840 Sense
//
// FUNDAMENTAL TIMING RULE
// ----------------------
// There is a single clock in the whole system: the nRF52840's micros().
// Every instant that ends up in a measurement - the "go", the IMU sample, the
// start onset - is expressed in that clock. The phone does not timestamp
// anything: it is a passive logger/monitor. BLE latency can be tens of ms and
// variable, but it does not touch the measurement because the time was already
// decided on board before entering the radio.
//
// The 2026-08-31 CSV capture proves this by counterexample: those timestamps
// were arrival times on the phone, with bursts of 1-4 samples delivered
// together and gaps of 29-31 ms (sometimes 58-62). Real uncertainty
// +/-15-30 ms, against a target of 1-2 ms. See playground_IMU/README.md.
//
// BLE CHARACTERISTICS
//   19B10001  "go" timestamp - single-shot, micros(). Unchanged.
//   19B10002  accelerometer stream ~50 Hz for the Live Data view.
//             *** PAYLOAD CHANGED: now 16 bytes, no longer 12. ***
//             Decimated from the high-frequency stream, and each sample carries
//             its own capture instant. Still purely cosmetic.
//   19B10004  high-frequency raw dump (notify) - see "DUMP" below.
//   19B10005  commands from the app (write) - for now only "request a dump".
//
// The UUIDs and the payload formats must stay aligned with
// prostart/lib/services/ble_service.dart.
//
// WHY THE FIFO
// ------------
// Before, the IMU registers were polled on millis() every 20 ms, with the ODR
// at 104 Hz: two non-synchronous rates, hence repeated samples and lost
// samples, and in any case a 9.6 ms quantization on the onset. Now the
// accelerometer runs at 833 Hz inside the hardware FIFO and the loop drains it
// in blocks: sampling is paced by the sensor, not by the loop, and software
// jitter no longer enters the data.
//
// SERIAL CAPTURE
// --------------
// BLE cannot carry 833 Hz continuously, but the USB cable can. The 'r'/'s'
// commands open and close a CSV stream on the serial port using *the same*
// measured time the rest of the firmware uses: every row carries the elapsed
// value derived from sampleTimeUs(), i.e. from the PLL locked to the FIFO, not
// from a counter multiplied by the nominal period. That is the difference
// between a file that looks sampled at 833 Hz and one that actually is. Format
// compatible with the Python script in
// Arduino/HighFrequencySampleRate/Python_Serial:
//   elapsed_s,x_g,y_g,z_g
//
// DUMP
// ----
// At 833 Hz the raw data alone needs ~5 kB/s: the BLE link (connection
// interval ~30 ms) cannot keep up. So the high-frequency samples live in a RAM
// ring buffer (~2.4 s of history) and are flushed on demand, in packets, when
// there is nothing urgent to do. From step 3 onward the dump will be triggered
// automatically by onset detection, to carry the window around the start to
// the phone.
// ---------------------------------------------------------------------------

BLEService reactionService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedLongCharacteristic goTimeChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Live payload: 16 bytes = uint32 t_us little-endian + 3 float32 (X, Y, Z) in g.
// t_us is the sample's *capture* instant in the board's micros() clock, not the
// send instant. The nRF52840 is little-endian and uses 32-bit IEEE-754, so
// casting the buffer is enough: it is exactly what the app reads with
// ByteData.getUint32/getFloat32(offset, Endian.little).
BLECharacteristic accelChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, 16, true);

// Raw dump. 8-byte header + N samples of 6 bytes each (3 x int16 raw, not in g):
//   [0..3]  uint32 t0_us   capture instant of the FIRST sample in the packet
//   [4..5]  uint16 seq     packet number within this dump
//   [6]     uint8  count   samples in the packet
//   [7]     uint8  flags   bit0 = first packet, bit1 = last
//   [8..]   count x { int16 x, int16 y, int16 z }
// The samples are raw 16-bit: the app converts them to g with the same factor
// as the library, 0.061 * (range >> 1) / 1000 (see LSM6DS3::calcAccel).
// Each packet carries its own t0_us, so the stream can be reconstructed even
// if a packet is lost.
// How many samples per packet. 2 keeps the payload at 20 bytes, which passes
// even with the minimum MTU (23) without being truncated. If the app
// negotiates a larger MTU (Android: requestMtu(247); iOS does it by itself at
// 185) this value can be raised to 28 -> 180 bytes per packet, and the dump
// becomes ~14x faster. Raise it only after verifying the MTU in the field: if
// the payload exceeds MTU-3 the notification is silently truncated.
#define BURST_SAMPLES_PER_PKT 2
#define BURST_HEADER_BYTES    8
#define BURST_PKT_BYTES       (BURST_HEADER_BYTES + BURST_SAMPLES_PER_PKT * 6)

BLECharacteristic burstChar("19B10004-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, BURST_PKT_BYTES, false);

// Commands from the app. 1 byte: 0x01 = start a dump of the ring buffer.
BLECharacteristic cmdChar("19B10005-E8F2-537E-4F6C-D104768A1214", BLEWrite, 1, false);

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// --- IMU configuration --------------------------------------------------
// 833 Hz: 1.2 ms period. It is the first ODR that puts quantization below the
// 1-2 ms target *without* interpolation; with interpolation on the rising edge
// (step 3) it drops well below a millisecond. 416 Hz would be the minimum
// acceptable, 833 leaves margin.
static const uint16_t ACCEL_ODR_HZ = 833;
// FIFO ODR code corresponding to 833 Hz: the library calls it "800" but the
// register is the same (FIFO_CTRL5 = 0x38). It must match the accelerometer
// ODR, otherwise the FIFO decimates or duplicates.
static const int16_t FIFO_ODR_CODE = 800;
// +/-4 g for now. To be re-evaluated at +/-8 g once the mechanical mounting on
// the block is final: in the 31/08 capture the peak was 0.43 g, but with the
// sensor rigidly on the block a real start produces 2-5 g and at +/-4 g there
// is a risk of clipping right on the rising edge.
static const uint8_t ACCEL_RANGE_G = 4;

// The FIFO holds 16-bit words and organizes them into "patterns". With the
// gyroscope off and datasets 3/4 disabled the pattern is exactly X, Y, Z.
static const uint8_t FIFO_PATTERN_WORDS = 3;

// Registers used directly: the library exposes neither the FIFO block read nor
// FIFO_STATUS3/4 (which are needed to realign to the pattern).
static const uint8_t REG_FIFO_CTRL4    = 0x09;
static const uint8_t REG_FIFO_STATUS1  = 0x3A;
static const uint8_t REG_FIFO_DATA_OUT = 0x3E;

// --- High-frequency ring buffer -----------------------------------------
// 2048 samples at 833 Hz = 2.46 s of history, 12 kB of RAM. Ample to cover a
// window around the start (typically -0.5 s / +1.0 s).
static const uint16_t RING_SAMPLES = 2048;
static int16_t ringBuf[RING_SAMPLES * 3];

// Monotonic global index of the most recent sample + 1, i.e. how many samples
// have ever been acquired. The position in the ring is (k % RING_SAMPLES), so
// the global index is also the base for the timestamps.
static uint32_t totalSamples = 0;

// --- Time model (software PLL) -----------------------------------------
// The FIFO says *how many* samples there are, not *when* they were taken. We
// know the period (1/ODR) but the LSM6DS3's oscillator has its own tolerance
// (a few %) relative to the nRF52840's clock: extrapolating from a single
// anchor would accumulate drift. So a phase-locked loop is kept: on each drain
// the expected instant of the most recent sample is compared with micros() and
// both the phase and the period are corrected slowly. The result is a period
// auto-calibrated to the on-board clock and timestamps that do not jump when a
// loop iteration arrives late.
static double   estPeriodUs   = 1000000.0 / (double)ACCEL_ODR_HZ;
static uint32_t newestSampleUs = 0;   // micros() of the most recent sample
static bool     clockLocked    = false;

// PLL gains. High proportional = fast re-lock after a gap; low integral = the
// period moves slowly and filters loop jitter.
static const double PLL_KP = 0.10;
static const double PLL_KI = 0.0005;
// The period cannot move more than 6% from nominal: beyond that means
// something went wrong (overrun, realignment), not that the oscillator has
// drifted.
static const double PERIOD_NOMINAL_US = 1000000.0 / (double)ACCEL_ODR_HZ;
static const double PERIOD_MIN_US     = PERIOD_NOMINAL_US * 0.94;
static const double PERIOD_MAX_US     = PERIOD_NOMINAL_US * 1.06;

// --- Decimated live stream -------------------------------------------
// 833 / 17 = 49 Hz, the same cadence as before for the Live Data screen.
static const uint16_t LIVE_DECIMATION = 17;
static uint32_t nextLiveSample = 0;

// --- Dump state ------------------------------------------------------
static bool     dumpActive = false;
static uint32_t dumpNext = 0;     // next global index to send
static uint32_t dumpEnd = 0;      // global index (exclusive) of the dump end
static uint16_t dumpSeq = 0;

// --- Simulated "go" ------------------------------------------------
// The trigger is simulated from serial until the buzzer is wired, but it goes
// through the same micros() the real audio trigger will use: when the source
// is swapped, the time handling does not change by a single line.
// It no longer blocks with delay(): a delay here would also stop the FIFO
// drain and would send it into overrun (4096 bytes = ~1.6 s at 833 Hz).
static bool     goPending = false;
static uint32_t goDueUs = 0;

static bool imuReady = false;
// BLE being absent is not a reason to stop the board: the wired capture stays
// useful (and is in fact how the sensor is characterized). See setup().
static bool bleReady = false;

// --- High-frequency serial capture ---------------------------------------
// Every sample that enters the ring is emitted as a CSV row while the stream
// is active. At 833 Hz that is ~30 rows/ms * 32 bytes = ~27 kB/s: below the
// nominal 921600 baud, and on the nRF52840's native USB the baud rate is just
// a label anyway. If the output buffer fills up it does not block: it retries
// on the next iteration, because a blocking Serial.write() would stop
// drainFifo() and send the FIFO into overrun - a cure worse than the disease.
static bool     serialStreaming = false;
static uint32_t nextSerialSample = 0;
static uint64_t serialElapsedUs = 0;   // time since the stream started
static uint32_t serialLastUs = 0;      // sampleTimeUs() of the last row emitted
static bool     serialFirstRow = true;
// Samples that left the ring before being emitted: the only way the capture
// can lose data. Declaring it is better than discovering it from the CSV.
static uint32_t serialDropped = 0;
// Longest possible row with plenty of margin (see formatSampleLine).
static const uint8_t SERIAL_ROW_MAX = 64;

// Explicit prototypes: the Arduino IDE's auto-prototyping is not reliable with
// `static` functions in a .ino.
void setupImu();
static void drainFifo();
static void updateTimebase(uint32_t tStatus, uint16_t n, uint16_t leftBehind);
static uint32_t sampleTimeUs(uint32_t k);
static void publishLiveSamples();
static void publishSerialSamples();
static uint8_t writeFixed(char* out, int64_t value, uint32_t scale, uint8_t decimals);
static uint8_t formatSampleLine(uint32_t k, char* out);
static void startDump(uint32_t count);
static void serviceDump();
static void serviceGo();
static void handleSerial();
static void handleCommand();
static void printTimebase();
static void printLatest();

void setup() {
  Serial.begin(921600);
  // On the XIAO nRF52840 USB is native: `Serial` only becomes true when a host
  // opens the CDC port. Waiting forever blocks setup() permanently if the board
  // is powered from a battery, from a port with no Serial Monitor open, or from
  // a power supply - and then BLE.advertise() below never runs and no app can
  // find the device. Wait at most 3 seconds, then proceed anyway.
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) delay(10);

  // Without a seed, random() repeats the same sequence on every power-up: on a
  // reaction-time system that means the athlete learns the wait and anticipates
  // it. The value is taken from an unconnected analog input (noise) mixed with
  // the clock.
  randomSeed(((uint32_t)analogRead(A0) << 16) ^ micros());

  setupImu();

  // A failing BLE.begin() must not brick the board. The previous firmware did
  // while(1) here: the board stayed alive but mute, with no useful serial and
  // no radio, and it needed the reset double-tap to reprogram it. Now it
  // proceeds regardless: the wired capture and diagnostics keep working, and
  // the failure is declared instead of silent.
  bleReady = BLE.begin();
  if (!bleReady) {
    Serial.println("BLE init failed - continuing without radio (serial capture active)");
  } else {
    BLE.setLocalName("BlockStartDevice");
    BLE.setAdvertisedService(reactionService);
    reactionService.addCharacteristic(goTimeChar);
    reactionService.addCharacteristic(accelChar);
    reactionService.addCharacteristic(burstChar);
    reactionService.addCharacteristic(cmdChar);
    BLE.addService(reactionService);

    // Request (not a guarantee: the central decides) of a connection interval
    // of 15-30 ms. Unit = 1.25 ms. With the longer default interval the 50 Hz
    // chart arrives in jerks and the dump takes forever.
    BLE.setConnectionInterval(12, 24);

    BLE.advertise();
  }

  Serial.println("Ready. Serial: 'g' go, 'd' dump, 't' timebase, 'p' latest, 'r'/'s' CSV capture.");
}

void setupImu() {
#ifdef PIN_LSM6DS3TR_C_POWER
  // On the XIAO Sense the IMU has a dedicated power pin: if it stays low
  // begin() returns an error even with I2C wired correctly.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif
  delay(100);  // the LSM6DS3 needs a moment before it responds on I2C

  // Accelerometer only: the gyroscope is not needed and every word less in the
  // FIFO is I2C bandwidth saved, which matters at 833 Hz.
  myIMU.settings.gyroEnabled = 0;
  myIMU.settings.gyroFifoEnabled = 0;

  myIMU.settings.accelEnabled = 1;
  myIMU.settings.accelSampleRate = ACCEL_ODR_HZ;
  myIMU.settings.accelRange = ACCEL_RANGE_G;
  // Careful with these two, together: the library clears CTRL4_C.BW_SCAL_ODR
  // unless accelODROff is 1, and with that bit at 0 the BW_XL bits are
  // *ignored* and the bandwidth is decided by the ODR (ODR/2). In the previous
  // firmware accelBandWidth = 50 therefore had no effect: at 104 Hz the real
  // bandwidth was 52 Hz by coincidence. Here we fix it explicitly at 200 Hz so
  // the anti-alias filter - and its group delay, to be measured at step 5 -
  // stays the same even if the ODR changes.
  myIMU.settings.accelODROff = 1;
  myIMU.settings.accelBandWidth = 200;

  myIMU.settings.accelFifoEnabled = 1;
  myIMU.settings.accelFifoDecimation = 1;  // /1: no decimation
  myIMU.settings.tempEnabled = 0;
  // The LSM6DS3's hardware timestamp would be written into the FIFO as an extra
  // dataset, but it is on another oscillator: it is not needed, the reference
  // clock is micros(). Keeping it out shortens the pattern and simplifies
  // alignment.
  myIMU.settings.timestampEnabled = 0;
  myIMU.settings.timestampFifoEnabled = 0;

  myIMU.settings.fifoThreshold = 96;   // we do not use the watermark, but it must be written
  myIMU.settings.fifoSampleRate = FIFO_ODR_CODE;
  myIMU.settings.fifoModeWord = 6;     // continuous

  imuReady = (myIMU.begin() == 0);
  if (!imuReady) {
    Serial.println("IMU error - streaming disabled");
    return;
  }

  // 400 kHz instead of the default 100 kHz: at 833 Hz ~15 kB/s are read from
  // the FIFO, at 100 kHz they would not fit in the loop budget.
  Wire.setClock(400000);

  myIMU.fifoBegin();
  // fifoBegin() writes FIFO_CTRL4 = 0x09, which pushes datasets 3 and 4 into
  // the FIFO pattern. We do not need them and they would extend the pattern
  // from 3 to 5 words: zero them.
  myIMU.writeRegister(REG_FIFO_CTRL4, 0x00);
  myIMU.fifoClear();

  Serial.print("IMU OK - accel ");
  Serial.print(ACCEL_ODR_HZ);
  Serial.print(" Hz, +/-");
  Serial.print(ACCEL_RANGE_G);
  Serial.println(" g, FIFO continuous");
}

void loop() {
  if (bleReady) BLE.poll();

  drainFifo();
  serviceGo();
  serviceDump();
  handleSerial();
  handleCommand();
}

// ---------------------------------------------------------------------------
// FIFO
// ---------------------------------------------------------------------------

// Reads FIFO_STATUS1..4 in one go: available words, flags and - the thing that
// matters - the position in the pattern of the next word to read.
static bool readFifoStatus(uint16_t* words, uint16_t* pattern, bool* overrun, bool* empty) {
  uint8_t s[4];
  if (myIMU.readRegisterRegion(s, REG_FIFO_STATUS1, 4) != IMU_SUCCESS) return false;
  *words   = ((uint16_t)(s[1] & 0x0F) << 8) | s[0];
  *overrun = (s[1] & 0x40) != 0;
  *empty   = (s[1] & 0x10) != 0;
  *pattern = ((uint16_t)(s[3] & 0x03) << 8) | s[2];
  return true;
}

// Reads `count` 16-bit words from the FIFO. Block reads from 0x3E: it is the
// method documented by ST (the output register does not auto-increment, every
// pair of bytes read advances the FIFO). In blocks because one I2C transaction
// per word would cost ~2500 transactions per second.
// Diagnostics: every way the read can go wrong has its own counter, so 't'
// tells you which one is happening instead of leaving you to guess.
static uint32_t errShortRead = 0;   // I2C transferred less than requested
static uint32_t errAddrNack = 0;    // NACK on the register address
static uint32_t errMisaligned = 0;  // pattern not at 0 after a whole block
static uint32_t cntOverrun = 0;
static uint32_t cntRealign = 0;

// Reads `count` 16-bit words from the FIFO.
//
// Hand-written instead of using LSM6DS3::readRegisterRegion for two reasons,
// both of which surfaced during bench testing:
//   - readRegisterRegion throws away requestFrom()'s return value. If the I2C
//     transfer fails, it fills fewer bytes than requested and still returns
//     IMU_SUCCESS: the caller believes it has valid data, the buffer holds
//     leftovers from the previous read, and the FIFO stays misaligned. Every
//     lost word rotates the X/Y/Z axes among themselves from there on.
// (Repeated START was tried and discarded: see the comment below.)
static bool readFifoWords(int16_t* dest, uint16_t count) {
  // The mbed core's Wire buffer is 256 bytes, so the real limit is the time
  // the loop can spend stalled on a single transaction.
  static const uint8_t MAX_WORDS_PER_READ = 15;
  uint8_t raw[MAX_WORDS_PER_READ * 2];

  while (count > 0) {
    uint8_t n = (count > MAX_WORDS_PER_READ) ? MAX_WORDS_PER_READ : (uint8_t)count;
    uint8_t bytes = n * 2;

    Wire.beginTransmission(0x6A);
    Wire.write(REG_FIFO_DATA_OUT);
    // STOP, not repeated START. Repeated START would seem more correct for a
    // FIFO access, but on the mbed core's I2C driver it jams the bus: its
    // read() has no timeout, so the loop hangs and the board stops responding.
    // Tried on the bench. With STOP nobody interferes anyway: the I2C has a
    // single master and a single slave.
    if (Wire.endTransmission(true) != 0) {
      errAddrNack++;
      return false;
    }
    if (Wire.requestFrom((uint8_t)0x6A, (size_t)bytes) != bytes) {
      errShortRead++;
      return false;
    }
    for (uint8_t i = 0; i < bytes; i++) {
      if (!Wire.available()) {
        errShortRead++;
        return false;
      }
      raw[i] = (uint8_t)Wire.read();
    }
    for (uint8_t i = 0; i < n; i++) {
      dest[i] = (int16_t)(((uint16_t)raw[i * 2 + 1] << 8) | raw[i * 2]);
    }
    dest += n;
    count -= n;
  }
  return true;
}

// Discards words until the next one read is the start of a pattern, and returns
// how many it discarded. Needed after an overrun: if you read out of phase,
// X/Y/Z swap among themselves and the data looks plausible while being wrong -
// the kind of bug you cannot see by looking at a chart.
//
// FIFO_PATTERN is the index, within the pattern, of the next word that will
// come out. If it is 1 the next one is Y, so to reach the following X you have
// to discard 3-1 = 2 words (not 1).
static uint16_t realignFifo(uint16_t pattern, uint16_t available) {
  uint16_t offset = pattern % FIFO_PATTERN_WORDS;
  if (offset == 0) return 0;
  uint16_t toDiscard = FIFO_PATTERN_WORDS - offset;
  if (toDiscard > available) return available;  // nothing to do for now
  int16_t discard[FIFO_PATTERN_WORDS];
  readFifoWords(discard, toDiscard);
  return toDiscard;
}

static void drainFifo() {
  if (!imuReady) return;

  uint16_t words, pattern;
  bool overrun, empty;
  if (!readFifoStatus(&words, &pattern, &overrun, &empty)) return;
  // micros() taken right after the status read: it is the PLL's anchor. From
  // here on the time spent reading the data no longer matters.
  uint32_t tStatus = micros();

  if (overrun) {
    // The FIFO overran: the loop did not keep up and there is a gap of unknown
    // duration. Restart clean instead of producing timestamps that look
    // continuous but are not.
    myIMU.fifoClear();
    clockLocked = false;
    cntOverrun++;
    return;
  }
  if (empty || words < FIFO_PATTERN_WORDS) return;

  uint16_t discarded = realignFifo(pattern, words);
  if (discarded) cntRealign++;
  uint16_t usable = words - discarded;
  uint16_t available = usable / FIFO_PATTERN_WORDS;
  if (available == 0) return;

  // Do not drain everything in one go: past a certain block size the loop stays
  // stalled too long and BLE.poll() suffers. The rest is taken on the next
  // iteration.
  static const uint16_t MAX_SAMPLES_PER_DRAIN = 64;
  uint16_t nSamples = (available > MAX_SAMPLES_PER_DRAIN) ? MAX_SAMPLES_PER_DRAIN : available;
  // How many samples remain in the FIFO after this read: needed by the PLL,
  // because the micros() anchor refers to the most recent sample *in the
  // FIFO*, not to the last one we read. Without this, every truncated drain
  // would shift the timestamps backward by tens of ms.
  uint16_t leftBehind = available - nSamples;

  int16_t block[MAX_SAMPLES_PER_DRAIN * 3];
  if (!readFifoWords(block, nSamples * FIFO_PATTERN_WORDS)) {
    // Read failed halfway: the FIFO is in a state we do not know. Do not try to
    // guess how far it advanced, restart clean.
    myIMU.fifoClear();
    clockLocked = false;
    return;
  }

  // We read a whole number of patterns starting from a whole pattern, so the
  // next one must be back at 0. If it is not, something ate or added words and
  // from here on X/Y/Z would be permuted: the data would look plausible while
  // being wrong. Better to throw away the block and resync than to save it.
  uint16_t w2, p2;
  bool o2, e2;
  if (readFifoStatus(&w2, &p2, &o2, &e2) && (p2 % FIFO_PATTERN_WORDS) != 0) {
    errMisaligned++;
    myIMU.fifoClear();
    clockLocked = false;
    return;
  }

  for (uint16_t i = 0; i < nSamples; i++) {
    uint32_t slot = ((totalSamples + i) % RING_SAMPLES) * 3;
    ringBuf[slot + 0] = block[i * 3 + 0];
    ringBuf[slot + 1] = block[i * 3 + 1];
    ringBuf[slot + 2] = block[i * 3 + 2];
  }
  totalSamples += nSamples;

  updateTimebase(tStatus, nSamples, leftBehind);
  // After updateTimebase(): sampleTimeUs() depends on newestSampleUs, which has
  // just been updated with this block.
  publishSerialSamples();
  publishLiveSamples();
}

// ---------------------------------------------------------------------------
// Time model
// ---------------------------------------------------------------------------

// tStatus:     micros() read together with the FIFO status.
// n:           samples just added to the ring.
// leftBehind:  samples still in the FIFO, not yet read.
static void updateTimebase(uint32_t tStatus, uint16_t n, uint16_t leftBehind) {
  // The most recent sample *in the FIFO* was captured between 0 and one period
  // before the status read: the expected value is half a period earlier. But
  // the last sample we put in the ring is older than that by `leftBehind`
  // periods, and it is the one newestSampleUs refers to.
  uint32_t measured = tStatus
                    - (uint32_t)(estPeriodUs * 0.5)
                    - (uint32_t)(estPeriodUs * (double)leftBehind);

  if (!clockLocked) {
    newestSampleUs = measured;
    estPeriodUs = PERIOD_NOMINAL_US;
    clockLocked = true;
    return;
  }

  // Where we expected the new most-recent sample to be, extrapolating from the
  // current model.
  double predicted = (double)newestSampleUs + estPeriodUs * (double)n;
  // Difference computed on uint32 and then on int32: this way the micros() wrap
  // (every ~71 minutes) handles itself.
  int32_t err = (int32_t)(measured - (uint32_t)predicted);

  // A huge error is not drift, it is a discontinuity: re-lock at once instead
  // of dragging it into the period.
  if (err > 20000 || err < -20000) {
    newestSampleUs = measured;
    estPeriodUs = PERIOD_NOMINAL_US;
    return;
  }

  newestSampleUs = (uint32_t)(predicted + PLL_KP * (double)err);
  estPeriodUs += PLL_KI * (double)err / (double)n;
  if (estPeriodUs < PERIOD_MIN_US) estPeriodUs = PERIOD_MIN_US;
  if (estPeriodUs > PERIOD_MAX_US) estPeriodUs = PERIOD_MAX_US;
}

// Capture instant of the sample with global index k, in the micros() clock.
// Valid for any k already acquired: this is how onset detection (step 3) will
// be able to date a sample found by looking back in the ring, and how the dump
// timestamps what it sends.
static uint32_t sampleTimeUs(uint32_t k) {
  double back = (double)(totalSamples - 1 - k) * estPeriodUs;
  return newestSampleUs - (uint32_t)(back + 0.5);
}

// ---------------------------------------------------------------------------
// Live stream (cosmetic)
// ---------------------------------------------------------------------------

static void publishLiveSamples() {
  // subscribed() is true only while the app is on the Live Data screen. The
  // FIFO drains anyway though: on-device detection must not depend on who is
  // watching.
  if (!bleReady || !BLE.connected() || !accelChar.subscribed()) {
    nextLiveSample = totalSamples;
    return;
  }
  uint32_t oldest = (totalSamples > RING_SAMPLES) ? (totalSamples - RING_SAMPLES) : 0;
  // Fallen behind past the ring: restart from the present instead of reading
  // samples that have already been overwritten.
  if (nextLiveSample < oldest) nextLiveSample = totalSamples;
  // It is not yet time for the next decimated sample.
  if (nextLiveSample >= totalSamples) return;

  // One packet per drain: at 833 Hz drains happen every few ms, so 49 Hz is
  // maintained anyway and the BLE queue does not clog.
  uint32_t k = nextLiveSample;
  nextLiveSample = k + LIVE_DECIMATION;

  uint32_t slot = (k % RING_SAMPLES) * 3;
  uint8_t payload[16];
  uint32_t t = sampleTimeUs(k);
  float g[3] = {
    myIMU.calcAccel(ringBuf[slot + 0]),
    myIMU.calcAccel(ringBuf[slot + 1]),
    myIMU.calcAccel(ringBuf[slot + 2]),
  };
  memcpy(payload, &t, 4);
  memcpy(payload + 4, g, 12);
  accelChar.writeValue(payload, sizeof(payload));
}

// ---------------------------------------------------------------------------
// High-frequency serial capture
// ---------------------------------------------------------------------------

// Writes `value / scale` with `decimals` decimal places, without touching
// printf on floats: on this core printf with %f is heavy (and in some newlib
// nano configurations it is not there at all), and at 833 Hz it would be the
// most expensive part of the iteration anyway. All in integer arithmetic.
// Returns the characters written.
static uint8_t writeFixed(char* out, int64_t value, uint32_t scale, uint8_t decimals) {
  uint8_t n = 0;
  if (value < 0) {
    out[n++] = '-';
    value = -value;
  }
  uint64_t whole = (uint64_t)value / scale;
  uint32_t frac  = (uint32_t)((uint64_t)value % scale);

  char tmp[20];
  uint8_t t = 0;
  do {
    tmp[t++] = (char)('0' + (whole % 10));
    whole /= 10;
  } while (whole);
  while (t) out[n++] = tmp[--t];

  out[n++] = '.';
  uint32_t p = scale;
  for (uint8_t d = 0; d < decimals; d++) {
    p /= 10;
    out[n++] = (char)('0' + ((frac / p) % 10));
  }
  return n;
}

// Builds the CSV row for the sample with global index k:
//   elapsed_s,x_g,y_g,z_g\n
//
// The elapsed is accumulated from the deltas between *measured* timestamps
// (sampleTimeUs), not counted in samples: this is exactly the difference from
// the HighFreq sketch, where the time axis is an index multiplied by the
// nominal period and therefore ignores both oscillator drift and gaps. Summing
// the deltas instead of doing a plain subtraction also holds past the micros()
// wrap (~71 minutes), which a long capture would hit.
static uint8_t formatSampleLine(uint32_t k, char* out) {
  uint32_t t = sampleTimeUs(k);
  if (serialFirstRow) {
    serialLastUs = t;
    serialFirstRow = false;
  }
  // The PLL can shift timestamps backward slightly when it re-locks: a negative
  // delta becomes 0, never a jump of 4000 seconds.
  int32_t d = (int32_t)(t - serialLastUs);
  if (d < 0) d = 0;
  serialElapsedUs += (uint32_t)d;
  serialLastUs = t;

  uint8_t n = writeFixed(out, (int64_t)serialElapsedUs, 1000000, 6);

  uint32_t slot = (k % RING_SAMPLES) * 3;
  for (uint8_t a = 0; a < 3; a++) {
    out[n++] = ',';
    // Same factor as LSM6DS3::calcAccel - raw * 0.061 * (range>>1) / 1000 -
    // but expressed in ten-thousandths of g and in integers:
    //   g * 10000 = raw * 61 * (range>>1) / 100
    int32_t num = (int32_t)ringBuf[slot + a] * 61L * (int32_t)(ACCEL_RANGE_G >> 1);
    int32_t units = (num >= 0) ? (num + 50) / 100 : (num - 50) / 100;
    n += writeFixed(out + n, units, 10000, 4);
  }
  out[n++] = '\n';
  return n;
}

static void publishSerialSamples() {
  if (!serialStreaming) return;

  // If the stream has fallen behind past the ring, those samples have been
  // overwritten: restart from the oldest still valid one and count the loss.
  // The CSV will still show the gap, because the elapsed jumps.
  uint32_t oldest = (totalSamples > RING_SAMPLES) ? (totalSamples - RING_SAMPLES) : 0;
  if (nextSerialSample < oldest) {
    serialDropped += oldest - nextSerialSample;
    nextSerialSample = oldest;
  }

  char row[SERIAL_ROW_MAX];
  while (nextSerialSample < totalSamples) {
    // The check must be done *before* formatting, because formatSampleLine
    // advances the accumulated elapsed: formatting and then giving up on
    // writing would leave a gap in the time axis. We ask for the space for the
    // longest possible row, so a single comparison is enough.
    if ((int)Serial.availableForWrite() < (int)SERIAL_ROW_MAX) return;
    uint8_t len = formatSampleLine(nextSerialSample, row);
    Serial.write((const uint8_t*)row, len);
    nextSerialSample++;
  }
}

static void startSerialStream() {
  // Start from the present: the ring content is history prior to the command
  // and does not belong to this capture.
  nextSerialSample = totalSamples;
  serialElapsedUs = 0;
  serialFirstRow = true;
  serialDropped = 0;
  serialStreaming = true;
}

static void stopSerialStream() {
  serialStreaming = false;
  Serial.print("Stream stopped: ");
  Serial.print(serialDropped);
  Serial.println(" dropped");
}

// ---------------------------------------------------------------------------
// Ring buffer dump
// ---------------------------------------------------------------------------

// Starts the dump of the last `count` samples. From step 3 this will be called
// by detection with a window centered on the onset.
static void startDump(uint32_t count) {
  if (totalSamples == 0) return;
  uint32_t available = min(totalSamples, (uint32_t)RING_SAMPLES);
  if (count > available) count = available;
  dumpNext = totalSamples - count;
  dumpEnd = totalSamples;
  dumpSeq = 0;
  dumpActive = true;
  Serial.print("Dump started: ");
  Serial.print(count);
  Serial.println(" samples");
}

static void serviceDump() {
  if (!dumpActive) return;
  if (!bleReady || !BLE.connected() || !burstChar.subscribed()) {
    dumpActive = false;
    return;
  }
  // If the ring has wrapped under the dump's feet, skip ahead: better a
  // declared gap than overwritten samples passed off as good.
  uint32_t oldest = (totalSamples > RING_SAMPLES) ? (totalSamples - RING_SAMPLES) : 0;
  if (dumpNext < oldest) dumpNext = oldest;

  uint8_t count = BURST_SAMPLES_PER_PKT;
  if (dumpEnd - dumpNext < count) count = (uint8_t)(dumpEnd - dumpNext);
  if (count == 0) {
    dumpActive = false;
    Serial.println("Dump complete");
    return;
  }

  uint8_t pkt[BURST_PKT_BYTES];
  uint32_t t0 = sampleTimeUs(dumpNext);
  uint8_t flags = 0;
  if (dumpSeq == 0) flags |= 0x01;
  if (dumpNext + count >= dumpEnd) flags |= 0x02;

  memcpy(pkt, &t0, 4);
  memcpy(pkt + 4, &dumpSeq, 2);
  pkt[6] = count;
  pkt[7] = flags;
  for (uint8_t i = 0; i < count; i++) {
    uint32_t slot = ((dumpNext + i) % RING_SAMPLES) * 3;
    memcpy(pkt + BURST_HEADER_BYTES + i * 6, &ringBuf[slot], 6);
  }
  burstChar.writeValue(pkt, BURST_HEADER_BYTES + count * 6);

  dumpNext += count;
  dumpSeq++;
  // One packet per loop iteration: the rest of the time is needed to drain the
  // FIFO and run the radio. The dump is logging, it is not in a hurry.
}

// ---------------------------------------------------------------------------
// Trigger
// ---------------------------------------------------------------------------

static void serviceGo() {
  if (!goPending) return;
  if ((int32_t)(micros() - goDueUs) < 0) return;
  goPending = false;

  // The instant that matters is the one when the event happened, read here and
  // not when the notification goes out. When the buzzer replaces the serial,
  // only what arms goDueUs changes.
  uint32_t goTimestamp = micros();
  if (bleReady) goTimeChar.writeValue(goTimestamp);

  Serial.print("GO! Timestamp: ");
  Serial.println(goTimestamp);
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'g') {
    // Random delay as before, but armed instead of waited on with delay(): the
    // loop keeps draining the FIFO in the meantime.
    goDueUs = micros() + (uint32_t)random(1000000, 4000000);
    goPending = true;
    Serial.println("Go armed");
  } else if (c == 'd') {
    startDump(RING_SAMPLES);
  } else if (c == 't') {
    printTimebase();
  } else if (c == 'p') {
    printLatest();
  } else if (c == 'r') {
    // 'r'/'s' are the letters the Python script already sends: this way the
    // capture works without touching the host side.
    startSerialStream();
  } else if (c == 's') {
    stopSerialStream();
  }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Timebase check. The comparison that matters is between the period estimated
// by the PLL and the *measured* period: samples acquired against microseconds
// elapsed on the on-board clock, between two invocations of this command. If
// the FIFO drains correctly and the ODR really is 833 Hz, both are around
// 1200 us. Looking at the deltas between consecutive timestamps would prove
// nothing: they are generated from the estimated period, so they would match
// by construction.
static uint32_t lastStatSamples = 0;
static uint32_t lastStatMicros = 0;

static void printTimebase() {
  uint32_t now = micros();
  uint32_t n = totalSamples;

  Serial.println("--- timebase ---");
  Serial.print("totalSamples   "); Serial.println(n);
  Serial.print("estPeriod_us   "); Serial.println(estPeriodUs, 3);
  Serial.print("estRate_Hz     "); Serial.println(1000000.0 / estPeriodUs, 2);
  // How old the most recent sample is: if the loop keeps up it must be a few
  // ms. A large value means the FIFO is piling up.
  Serial.print("newestAge_us   "); Serial.println((int32_t)(now - newestSampleUs));
  Serial.print("overruns       "); Serial.println(cntOverrun);
  Serial.print("realigns       "); Serial.println(cntRealign);
  Serial.print("shortReads     "); Serial.println(errShortRead);
  Serial.print("addrNacks      "); Serial.println(errAddrNack);
  Serial.print("misaligned     "); Serial.println(errMisaligned);
  Serial.print("serialStream   "); Serial.println(serialStreaming ? "on" : "off");
  Serial.print("serialDropped  "); Serial.println(serialDropped);

  if (lastStatSamples != 0 && n > lastStatSamples) {
    uint32_t dn = n - lastStatSamples;
    uint32_t dt = now - lastStatMicros;
    Serial.print("--- since last 't' ---");
    Serial.println();
    Serial.print("samples        "); Serial.println(dn);
    Serial.print("interval_us    "); Serial.println(dt);
    Serial.print("measPeriod_us  "); Serial.println((double)dt / (double)dn, 3);
    Serial.print("measRate_Hz    "); Serial.println(1000000.0 * (double)dn / (double)dt, 2);
  } else {
    Serial.println("(press 't' again to get a measured rate)");
  }
  lastStatSamples = n;
  lastStatMicros = now;
}

// Last sample in g. Used to check the FIFO pattern alignment: with the board
// still on the table Z must be around 1 g and X/Y near 0. If the pattern were
// out of phase the axes would come out permuted.
static void printLatest() {
  if (totalSamples == 0) {
    Serial.println("no samples yet");
    return;
  }
  uint32_t k = totalSamples - 1;
  uint32_t slot = (k % RING_SAMPLES) * 3;
  Serial.print("latest  t_us=");
  Serial.print(sampleTimeUs(k));
  Serial.print("  x=");
  Serial.print(myIMU.calcAccel(ringBuf[slot + 0]), 4);
  Serial.print("  y=");
  Serial.print(myIMU.calcAccel(ringBuf[slot + 1]), 4);
  Serial.print("  z=");
  Serial.println(myIMU.calcAccel(ringBuf[slot + 2]), 4);
}

static void handleCommand() {
  if (!bleReady || !cmdChar.written()) return;
  const uint8_t* v = cmdChar.value();
  if (cmdChar.valueLength() >= 1 && v[0] == 0x01) {
    startDump(RING_SAMPLES);
  }
}
