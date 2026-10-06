#pragma once
// ---------------------------------------------------------------------------
// Bmi270.h - accelerometer-only BMI270 driver over I2C, register level.
//
// Own driver rather than a library for the same reason Display.h is one: the
// read on the sample path has to be ONE burst transaction and nothing else,
// and every register the measurement depends on (ODR, filter, range, the
// data-ready routing) is set here in plain sight instead of inside a library
// default. Register map and the init procedure follow Bosch's BMI270_SensorAPI
// v2.86.1 (bmi2.c, bmi2_defs.h); the config blob is Bmi270Config.h.
//
// Configuration, and why:
//   ODR 800 Hz       BMI270 rates are 25 x 2^n: 800 is the step nearest the
//                    833 Hz the XIAO ran. 1600 would need a smaller display
//                    budget and a larger MAX_AIC; not worth it untested.
//   filter_perf = 1  performance mode: the full-rate digital filter, not the
//                    duty-cycled low-power path (which adds jitter).
//   bwp = normal     Bosch's default (0xA8 = perf | normal | 100 Hz). Its
//                    group delay is a FIXED offset on every onset, and it is
//                    not the LSM6DS3's: see INFO.md, it has to be measured.
//   +/-16 g          2048 LSB/g = 0.488 mg/LSB, the same resolution the
//                    LSM6DS3 had at 16 g, so the thresholds keep their units.
//   INT1 = data-ready, push-pull, active high, non-latched.
// ---------------------------------------------------------------------------
#include <Wire.h>
#include "Pins.h"
#include "Bmi270Config.h"

static const uint8_t BMI270_ADDR = 0x68;   // SDO to GND; 0x69 with SDO high

enum : uint8_t {
  BMI_CHIP_ID         = 0x00,   // reads 0x24
  BMI_ERR_REG         = 0x02,
  BMI_STATUS          = 0x03,
  BMI_ACC_X_LSB       = 0x0C,   // X/Y/Z, 6 bytes, little endian
  BMI_INT_STATUS_1    = 0x1D,
  BMI_INTERNAL_STATUS = 0x21,   // low nibble 0x1 = config loaded, running
  BMI_ACC_CONF        = 0x40,
  BMI_ACC_RANGE       = 0x41,
  BMI_INT1_IO_CTRL    = 0x53,
  BMI_INT_LATCH       = 0x55,
  BMI_INT_MAP_DATA    = 0x58,
  BMI_INIT_CTRL       = 0x59,
  BMI_INIT_ADDR_0     = 0x5B,
  BMI_INIT_DATA       = 0x5E,
  BMI_PWR_CONF        = 0x7C,
  BMI_PWR_CTRL        = 0x7D,
  BMI_CMD             = 0x7E,
};

static const uint8_t BMI_CHIP_ID_VALUE = 0x24;
static const uint8_t BMI_ACC_ODR_800   = 0x0B;
static const uint8_t BMI_ACC_BWP_NORM  = 0x02;   // normal_avg4
static const uint8_t BMI_ACC_PERF      = 0x80;
static const uint8_t BMI_ACC_RANGE_16G = 0x03;

static const uint8_t BMI_ACC_CONF_VALUE = BMI_ACC_PERF | (BMI_ACC_BWP_NORM << 4) | BMI_ACC_ODR_800;

// Why the last bmiBegin() failed, for the serial banner.
static const char* bmiError = "";
static uint8_t bmiErrorValue = 0;

static bool bmiWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool bmiWriteBurst(uint8_t reg, const uint8_t* d, size_t n) {
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(reg);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}

static bool bmiRead(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BMI270_ADDR, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static uint8_t bmiRead8(uint8_t reg) {
  uint8_t v = 0xFF;
  bmiRead(reg, &v, 1);
  return v;
}

static bool bmiFail(const char* why, uint8_t value) {
  bmiError = why;
  bmiErrorValue = value;
  return false;
}

// A reset of the C3 in the middle of a read can leave the BMI270 holding SDA
// low, and then nothing on the bus answers until power is cycled. Nine clocks
// let it finish the byte it was sending and release the line.
static void i2cBusRecover() {
  pinMode(IMU_SDA_PIN, INPUT_PULLUP);
  pinMode(IMU_SCL_PIN, INPUT_PULLUP);
  delayMicroseconds(10);
  if (digitalRead(IMU_SDA_PIN) == HIGH) return;
  pinMode(IMU_SCL_PIN, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(IMU_SDA_PIN) == LOW; i++) {
    digitalWrite(IMU_SCL_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(IMU_SCL_PIN, HIGH); delayMicroseconds(5);
  }
  pinMode(IMU_SCL_PIN, INPUT_PULLUP);
}

// Full power-up: soft reset, config upload, accelerometer only. ~200 ms.
static bool bmiBegin() {
  i2cBusRecover();
  // The internal pull-ups (~45 kOhm) are what Wire enables; at 400 kHz they
  // are only enough if the breakout carries its own. INFO.md says how to tell.
  Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN, 400000);

  uint8_t id = bmiRead8(BMI_CHIP_ID);
  if (id != BMI_CHIP_ID_VALUE) return bmiFail("chip id (wiring, 0x68/0x69, CS high?)", id);

  bmiWrite(BMI_CMD, 0xB6);          // soft reset
  delay(2);

  // Config upload, Bosch's sequence: advanced power save off (it otherwise
  // needs 450 us between writes), INIT_CTRL 0, blob in bursts at INIT_ADDR
  // (counted in 16-bit words), INIT_CTRL 1, then wait for the chip to report
  // it is running it.
  if (!bmiWrite(BMI_PWR_CONF, 0x00)) return bmiFail("PWR_CONF write", 0);
  delayMicroseconds(450);
  bmiWrite(BMI_INIT_CTRL, 0x00);
  // 32 data bytes per burst: Wire's buffer on this core is 128, and the
  // register byte has to fit in the same transmission.
  const uint16_t CHUNK = 32;
  for (uint16_t i = 0; i < BMI270_CONFIG_SIZE; i += CHUNK) {
    uint8_t addr[2] = {(uint8_t)((i / 2) & 0x0F), (uint8_t)((i / 2) >> 4)};
    if (!bmiWriteBurst(BMI_INIT_ADDR_0, addr, 2) ||
        !bmiWriteBurst(BMI_INIT_DATA, &bmi270_config_file[i], CHUNK)) {
      return bmiFail("config upload", (uint8_t)(i >> 8));
    }
  }
  bmiWrite(BMI_INIT_CTRL, 0x01);

  uint8_t st = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 200) {
    delay(5);
    st = bmiRead8(BMI_INTERNAL_STATUS) & 0x0F;
    if (st == 0x01) break;
  }
  if (st != 0x01) return bmiFail("INTERNAL_STATUS (config not accepted)", st);

  // Accelerometer only, performance mode.
  bmiWrite(BMI_PWR_CTRL, 0x04);                 // acc_en
  bmiWrite(BMI_ACC_CONF, BMI_ACC_CONF_VALUE);
  bmiWrite(BMI_ACC_RANGE, BMI_ACC_RANGE_16G);
  bmiWrite(BMI_INT1_IO_CTRL, 0x0A);             // output_en | active high, push-pull
  bmiWrite(BMI_INT_LATCH, 0x00);                // non-latched
  bmiWrite(BMI_INT_MAP_DATA, 0x04);             // drdy -> INT1
  bmiWrite(BMI_PWR_CONF, 0x02);                 // adv_power_save off, fifo_self_wakeup on
  delay(2);

  // Read back what the measurement depends on: a register the chip refused
  // (an invalid ODR/bandwidth combination is silently replaced) must not pass.
  uint8_t conf = bmiRead8(BMI_ACC_CONF);
  if (conf != BMI_ACC_CONF_VALUE) return bmiFail("ACC_CONF read back", conf);
  uint8_t range = bmiRead8(BMI_ACC_RANGE);
  if (range != BMI_ACC_RANGE_16G) return bmiFail("ACC_RANGE read back", range);
  uint8_t err = bmiRead8(BMI_ERR_REG);
  if (err != 0) return bmiFail("ERR_REG", err);
  return true;
}

// The one read on the sample path: X/Y/Z in a single burst.
static void bmiReadAccelRaw(int16_t* x, int16_t* y, int16_t* z) {
  uint8_t b[6] = {0, 0, 0, 0, 0, 0};
  bmiRead(BMI_ACC_X_LSB, b, 6);
  *x = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
  *y = (int16_t)((uint16_t)b[2] | ((uint16_t)b[3] << 8));
  *z = (int16_t)((uint16_t)b[4] | ((uint16_t)b[5] << 8));
}

// Before deep sleep: the BMI270 is powered straight from 3V3 and would keep
// sampling at 800 Hz (~0.2 mA). Accelerometer off + advanced power save is
// a few uA. The next boot does a full bmiBegin() anyway.
static void bmiSleep() {
  bmiWrite(BMI_INT_MAP_DATA, 0x00);
  bmiWrite(BMI_PWR_CTRL, 0x00);
  bmiWrite(BMI_PWR_CONF, 0x03);
}
