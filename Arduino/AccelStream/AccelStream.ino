#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// AccelStream - minimal, no-BLE accelerometer streamer, XIAO nRF52840 Sense
//
// Deliberately simple after BLEtest.ino's hand-rolled FIFO + PLL + BLE
// combination turned out to hang/crash-loop on real hardware (reproduced on
// two separate boards; a bare Serial-echo sketch worked fine on both, so the
// fault is in that firmware's complexity, not the hardware). This sketch
// only does one job: read the accelerometer on a fixed schedule and stream
// it over serial with a real, measured timestamp. No FIFO, no BLE, no
// custom register-level I2C code - just the vendored library's plain
// polling reads, which are the best-tested code path available.
//
// TIMESTAMPS
// ----------
// Each sample's t_us is micros() read immediately after that sample's I2C
// transaction completes - the actual instant the value was obtained, not an
// index multiplied by the nominal period. That's what makes this different
// from the old Reaction_Time_HighFreq.ino sketch, which computed elapsed
// time as sampleIndex * nominal_period and so silently ignored any I2C
// jitter or missed schedule slots.
//
// SERIAL PROTOCOL
// ----------------
// 'r' starts the CSV stream, 's' stops it, 'p' prints one immediate reading.
// While streaming, each line is:
//   t_us,x_g,y_g,z_g
// t_us is an unsigned micros() value (wraps every ~71 minutes, ignored here
// since captures are short bench/block tests, not multi-hour sessions).
// ---------------------------------------------------------------------------

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// 416 Hz is a real LSM6DS3 ODR option and comfortably clears 400 Hz. Period
// 2.4 ms leaves plenty of room for three small I2C reads (X/Y/Z) even at the
// default I2C clock, and gives timestamp quantization around 0.7 ms - inside
// the 1 ms target the user wants without needing a FIFO at all.
static const uint16_t ACCEL_ODR_HZ = 416;
static const uint32_t SAMPLE_PERIOD_US = 1000000UL / ACCEL_ODR_HZ;

// +/-8 g: a real push-off rigidly mounted on the block can hit 2-5 g (see
// BLEtest.ino's own notes on this), and clipping right on the rising edge
// would ruin exactly the part of the signal that matters. Since this sketch
// is meant for the actual on-block test, start at the safer range instead of
// the +/-4 g used for earlier bench taps.
static const uint8_t ACCEL_RANGE_G = 8;

static bool imuReady = false;
static bool streaming = false;
static uint32_t nextSampleDueUs = 0;

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

  Serial.println("Ready. Serial: 'r' start CSV, 's' stop, 'p' one reading.");
}

static void readSample(uint32_t* t_us, float* x, float* y, float* z) {
  *x = myIMU.readFloatAccelX();
  *y = myIMU.readFloatAccelY();
  *z = myIMU.readFloatAccelZ();
  // Timestamp taken right after the reads complete: this is the real instant
  // the data was obtained, not a scheduled/nominal one.
  *t_us = micros();
}

static void serviceStream() {
  if (!streaming || !imuReady) return;
  uint32_t now = micros();
  if ((int32_t)(now - nextSampleDueUs) < 0) return;

  // Wait for room for the whole row before reading the sensor. Without this,
  // a momentarily slow host (USB buffer full) would make Serial.print()
  // block, which would stall the schedule - or, if we read first and only
  // then found no room to write, that sample would be silently lost, since
  // this sketch keeps no buffer to retry from (unlike BLEtest.ino's ring
  // buffer). Delaying the read instead just makes it a little late, never
  // lost. Worst-case row: 10-digit t_us + 3 x "-8.0000" + separators + '\n'.
  static const uint8_t ROW_MAX = 40;
  if ((int)Serial.availableForWrite() < (int)ROW_MAX) return;

  nextSampleDueUs += SAMPLE_PERIOD_US;

  uint32_t t;
  float x, y, z;
  readSample(&t, &x, &y, &z);

  Serial.print(t);
  Serial.print(',');
  Serial.print(x, 4);
  Serial.print(',');
  Serial.print(y, 4);
  Serial.print(',');
  Serial.println(z, 4);
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'r') {
    nextSampleDueUs = micros();
    streaming = true;
  } else if (c == 's') {
    streaming = false;
    Serial.println("Stream stopped");
  } else if (c == 'p') {
    if (!imuReady) {
      Serial.println("IMU not ready");
    } else {
      uint32_t t;
      float x, y, z;
      readSample(&t, &x, &y, &z);
      Serial.print("t_us="); Serial.print(t);
      Serial.print("  x="); Serial.print(x, 4);
      Serial.print("  y="); Serial.print(y, 4);
      Serial.print("  z="); Serial.println(z, 4);
    }
  }
}

void loop() {
  serviceStream();
  handleSerial();
}
