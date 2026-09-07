#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// AccelStream - no-BLE accelerometer streamer + full-rate buffered recorder,
// XIAO nRF52840 Sense.
//
// v2: the first version polled the IMU with three separate
// readFloatAccelX/Y/Z() calls (six I2C transactions/sample - each has fixed
// driver overhead on the nRF52840) and printed an ASCII CSV row over serial
// for every sample. Measured effective rate on real hardware: ~232 Hz, well
// under the configured 416 Hz ODR, with near-zero jitter - a hard ceiling
// from fixed per-sample cost, not sensor speed or scheduling noise.
//
// This version fixes both halves of that cost:
//  - X/Y/Z live in six consecutive registers on the chip; one burst
//    readRegisterRegion() call reads all six bytes in a single I2C
//    transaction instead of six.
//  - Serial.print of an ASCII float, every sample, has almost no margin
//    left in a 1660 Hz budget (602 us/sample) and silently throttles the
//    whole loop the moment the host or USB stalls even briefly - exactly
//    the failure mode that produced the 232 Hz ceiling. So printing is
//    decoupled from sampling: while idle, only every STREAM_DECIMATE-th
//    sample is printed (a live preview, plenty fast for a chart); while
//    recording, nothing is printed at all - every single sample at full
//    ODR is written straight into a RAM buffer, and the whole buffer is
//    dumped over serial only after the capture stops. Serial timing can
//    never rob a sample during the window that actually matters.
//
// SERIAL PROTOCOL
// ----------------
// Idle (default from boot): the board streams decimated CSV rows
// continuously, no command needed - this is the live-preview rate.
//   t_us,x_g,y_g,z_g
// 'r' arms a full-ODR recording into the RAM buffer, no on/set/go marker
//     semantics (no serial output while armed).
// 'o' marks "on your marks" (micros() timestamp only, doesn't touch
//     recording state) - clears any leftover set/go from a previous
//     attempt.
// 's' marks "set" AND arms a full-ODR recording (no separate 'r' needed
//     for this workflow) - starting the buffer here, not at "on your
//     marks", is what keeps a real pause between the two from ever
//     overflowing it.
// 'g' marks "go" - the reference timestamp the push-off is measured
//     against (micros() timestamp only, doesn't touch recording state).
// 'S' (uppercase - lowercase 's' means "set") stops recording and dumps
//     the buffer:
//   DUMP_START,<n>
//   ON,<t_us or 0>
//   SET,<t_us or 0>
//   GO,<t_us or 0>
//   <n CSV rows, same t_us,x_g,y_g,z_g format, full ODR, no decimation>
//   DUMP_END
//   ... then idle decimated streaming resumes automatically.
// 'p' prints one immediate reading, independent of the above.
//
// t_us is an unsigned micros() value (wraps every ~71 minutes, ignored here
// since captures are short bench/block tests, not multi-hour sessions).
// ---------------------------------------------------------------------------

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// 1660 Hz is the fastest ODR the vendored library's settings switch actually
// recognizes (its own comment says "1666" but the switch case is literally
// 1660 - passing 1666 silently falls through to the 104 Hz default, which
// would be a very quiet way to lose 16x the sample rate). Verified against
// LSM6DS3.cpp's accelSampleRate switch before picking this constant.
static const uint16_t ACCEL_ODR_HZ = 1660;
static const uint32_t SAMPLE_PERIOD_US = 1000000UL / ACCEL_ODR_HZ;

// +/-16 g: a real push-off rigidly mounted on the block is expected around
// 1-3 g, but a 2026-09-07 bench test (a hard hand hit, well above a real
// push-off) clipped hard at +/-8g on two axes simultaneously for ~200ms.
// 16g costs almost nothing here - LSB resolution only drops from 0.244 mg
// to 0.488 mg, still far below the ~20 mg detection threshold - so it's
// cheap margin against clipping the one part of the signal that matters.
static const uint8_t ACCEL_RANGE_G = 16;

// Same scaling the library's calcAccel() applies (LSM6DS3.cpp), replicated
// here so the burst read path doesn't need three separate library calls.
static const float ACCEL_SCALE_G_PER_LSB = 0.061f * (ACCEL_RANGE_G >> 1) / 1000.0f;

// Idle live-preview rate = ACCEL_ODR_HZ / STREAM_DECIMATE (~332 Hz) - fast
// enough for a smooth chart, slow enough that ASCII float printing has huge
// timing margin and can never throttle the underlying sample schedule.
static const uint16_t STREAM_DECIMATE = 5;

// RAM budget for a recording: 10 bytes/sample (uint32 t_us + 3x int16 raw)
// x 12000 = ~117 KB, comfortably inside the XIAO nRF52840's 256 KB RAM with
// no BLE stack running. ~7.2 s at 1660 Hz - generous for one on-your-marks
// to push-off window; raise it if a longer capture is ever needed.
static const uint32_t MAX_REC_SAMPLES = 12000;
static uint32_t recT[MAX_REC_SAMPLES];
static int16_t recX[MAX_REC_SAMPLES];
static int16_t recY[MAX_REC_SAMPLES];
static int16_t recZ[MAX_REC_SAMPLES];
static uint32_t recCount = 0;

enum Mode { MODE_IDLE, MODE_RECORDING };
static Mode mode = MODE_IDLE;

static bool imuReady = false;
static uint32_t nextSampleDueUs = 0;
static uint32_t idleSampleIndex = 0;

// Bench-test ground-truth markers: a human presses 'o'/'s'/'g' on the
// keyboard (relayed over serial by accel_live.py) and says the word out
// loud at the same instant. Each keypress is timestamped with the
// firmware's own micros() - the same clock every accelerometer sample
// uses - so the marker and the push-off it's compared against are always
// on one shared timeline, no separate PC/audio-latency or clock-sync
// problem to worry about. Purely a bench-test aid, not part of the final
// reaction-time design (see README's audio "go" signal instead).
static uint32_t onT = 0, setT = 0, goT = 0;
static bool onCaptured = false, setCaptured = false, goCaptured = false;

void setup() {
  Serial.begin(921600);
  // Native USB CDC: Serial only becomes true once a host opens the port.
  // Bounded wait so a battery-powered board (no host attached) doesn't hang
  // here forever.
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) delay(10);

#ifdef PIN_LSM6DS3TR_C_POWER
  // XIAO Sense IMU power-enable pin: if left low, begin() fails even with
  // I2C wired correctly.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif

  delay(100);  // LSM6DS3 needs a moment before it responds on I2C

  myIMU.settings.gyroEnabled = 0;
  myIMU.settings.accelEnabled = 1;
  myIMU.settings.accelSampleRate = ACCEL_ODR_HZ;
  myIMU.settings.accelRange = ACCEL_RANGE_G;
  myIMU.settings.accelFifoEnabled = 0;  // no FIFO - plain polling reads only

  imuReady = (myIMU.begin() == 0);
  if (!imuReady) {
    Serial.println("IMU error - check wiring/power pin");
  } else {
    Wire.setClock(400000);
    Serial.print("IMU OK - accel ");
    Serial.print(ACCEL_ODR_HZ);
    Serial.print(" Hz, +/-");
    Serial.print(ACCEL_RANGE_G);
    Serial.println(" g");
  }

  Serial.println("Ready. Idle preview streaming. 'p' one reading.");
  Serial.println("Markers: 'o' on-your-marks, 's' set (also arms recording), 'g' go, 'S' stop+dump.");
  nextSampleDueUs = micros();
}

// Single I2C transaction: X/Y/Z occupy six consecutive registers
// (OUTX_L_XL..OUTZ_H_XL), so one readRegisterRegion() call replaces the
// three separate readFloatAccelX/Y/Z() calls (six transactions) the first
// version used - this is what actually removes the per-sample I2C overhead.
static void readSampleRaw(int16_t* rawX, int16_t* rawY, int16_t* rawZ) {
  uint8_t buf[6];
  myIMU.readRegisterRegion(buf, LSM6DS3_ACC_GYRO_OUTX_L_XL, 6);
  *rawX = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
  *rawY = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
  *rawZ = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
}

static inline float rawToG(int16_t raw) {
  return (float)raw * ACCEL_SCALE_G_PER_LSB;
}

static void printCsvRow(uint32_t t_us, int16_t rawX, int16_t rawY, int16_t rawZ) {
  Serial.print(t_us);
  Serial.print(',');
  Serial.print(rawToG(rawX), 4);
  Serial.print(',');
  Serial.print(rawToG(rawY), 4);
  Serial.print(',');
  Serial.println(rawToG(rawZ), 4);
}

static void dumpRecording() {
  Serial.print("DUMP_START,");
  Serial.println(recCount);
  Serial.print("ON,");
  Serial.println(onCaptured ? onT : 0);
  Serial.print("SET,");
  Serial.println(setCaptured ? setT : 0);
  Serial.print("GO,");
  Serial.println(goCaptured ? goT : 0);
  for (uint32_t i = 0; i < recCount; i++) {
    printCsvRow(recT[i], recX[i], recY[i], recZ[i]);
  }
  Serial.println("DUMP_END");
}

static void serviceSampling() {
  if (!imuReady) return;
  uint32_t now = micros();
  if ((int32_t)(now - nextSampleDueUs) < 0) return;
  nextSampleDueUs += SAMPLE_PERIOD_US;

  int16_t rawX, rawY, rawZ;
  readSampleRaw(&rawX, &rawY, &rawZ);
  uint32_t t = micros();  // real instant the data was obtained

  if (mode == MODE_RECORDING) {
    if (recCount < MAX_REC_SAMPLES) {
      recT[recCount] = t;
      recX[recCount] = rawX;
      recY[recCount] = rawY;
      recZ[recCount] = rawZ;
      recCount++;
    }
    if (recCount >= MAX_REC_SAMPLES) {
      Serial.println("Buffer full - auto-stopped");
      mode = MODE_IDLE;
      dumpRecording();
    }
  } else {
    idleSampleIndex++;
    if (idleSampleIndex % STREAM_DECIMATE == 0) {
      // Wait for room for the whole row before printing, same reasoning as
      // v1: never let a blocked Serial.print stall the sample schedule.
      static const uint8_t ROW_MAX = 40;
      if ((int)Serial.availableForWrite() >= (int)ROW_MAX) {
        printCsvRow(t, rawX, rawY, rawZ);
      }
    }
  }
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'r') {
    // Manual/quick arm, no on/set/go semantics - independent of the o/s/g
    // marker sequence below.
    if (mode == MODE_IDLE) {
      recCount = 0;
      mode = MODE_RECORDING;
    }
  } else if (c == 'o') {
    // "On your marks" - fresh attempt, clear any leftover set/go markers
    // from a previous one.
    onT = micros();
    onCaptured = true;
    setCaptured = false;
    goCaptured = false;
  } else if (c == 's') {
    // "Set" - also arms a full-rate recording, so a separate 'r' isn't
    // needed for the o/s/g protocol. Starting the buffer only here (not at
    // "on your marks") is what keeps a real on-your-marks/set pause from
    // ever overflowing it.
    setT = micros();
    setCaptured = true;
    goCaptured = false;
    if (mode == MODE_IDLE) {
      recCount = 0;
      mode = MODE_RECORDING;
    }
  } else if (c == 'g') {
    // "Go" - the actual reference marker for the push-off.
    goT = micros();
    goCaptured = true;
  } else if (c == 'S') {
    if (mode == MODE_RECORDING) {
      mode = MODE_IDLE;
      dumpRecording();
    }
  } else if (c == 'p') {
    if (!imuReady) {
      Serial.println("IMU not ready");
    } else {
      int16_t rawX, rawY, rawZ;
      readSampleRaw(&rawX, &rawY, &rawZ);
      uint32_t t = micros();
      Serial.print("t_us="); Serial.print(t);
      Serial.print("  x="); Serial.print(rawToG(rawX), 4);
      Serial.print("  y="); Serial.print(rawToG(rawY), 4);
      Serial.print("  z="); Serial.println(rawToG(rawZ), 4);
    }
  }
}

void loop() {
  serviceSampling();
  handleSerial();
}
