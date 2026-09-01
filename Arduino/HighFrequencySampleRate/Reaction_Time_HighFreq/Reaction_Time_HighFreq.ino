#include <ArduinoBLE.h>
#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// Reaction Time System BLE Service
// ---------------------------------------------------------------------------
BLEService reactionService("19B10000-E8F2-537E-4F6C-D104768A1214");

// "Go" trigger timestamp (hardware microseconds via micros())
BLEUnsignedLongCharacteristic goTimeChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Computed Reaction Time (in seconds, sent as a float to the mobile app)
BLEFloatCharacteristic reactionTimeChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

LSM6DS3 myIMU(I2C_MODE, 0x6A);

bool imuReady = false;
bool loggingActive = false;

// Deterministic sampling configuration (1660 Hz => ~602 us period)
const unsigned long SAMPLE_INTERVAL_US = 602;
unsigned long nextSampleMicros = 0;
unsigned long sampleIndex = 0;

void setup() {
  Serial.begin(921600);

  // Non-blocking wait for USB CDC Serial (max 3 seconds)
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) delay(10);

  Wire.begin();
  Wire.setClock(400000); // 400 kHz I2C Fast-mode

  setupImu();

  if (!BLE.begin()) {
    Serial.println("BLE initialization failed!");
    while (1);
  }

  BLE.setLocalName("BlockStartDevice");
  BLE.setAdvertisedService(reactionService);
  reactionService.addCharacteristic(goTimeChar);
  reactionService.addCharacteristic(reactionTimeChar);
  BLE.addService(reactionService);

  BLE.advertise();
  Serial.println("System ready. BLE advertising active and high-speed Serial streaming available.");
}

void setupImu() {
#ifdef PIN_LSM6DS3TR_C_POWER
  // Power gate pin for the IMU on XIAO Sense boards
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif
  delay(100); // Allow sensor startup stabilization

  // Disable gyroscope to save power and free I2C bandwidth
  myIMU.settings.gyroEnabled = 0;
  // Maximum accelerometer sampling rate: 1.66 kHz
  myIMU.settings.accelSampleRate = 1660;
  // Anti-aliasing low-pass filter bandwidth
  myIMU.settings.accelBandWidth = 400;
  // Full-scale range set to +/- 4g for optimal resolution
  myIMU.settings.accelRange = 4;

  imuReady = (myIMU.begin() == 0);
}

void loop() {
  BLE.poll();

  if (imuReady && loggingActive) {
    unsigned long currentMicros = micros();

    // Deterministic hardware timer step
    if ((long)(currentMicros - nextSampleMicros) >= 0) {
      nextSampleMicros += SAMPLE_INTERVAL_US;

      float ax = myIMU.readFloatAccelX();
      float ay = myIMU.readFloatAccelY();
      float az = myIMU.readFloatAccelZ();

      double elapsedSec = (double)sampleIndex * (SAMPLE_INTERVAL_US / 1000000.0);
      sampleIndex++;

      // Stream high-frequency CSV row to Python via Serial
      Serial.print(elapsedSec, 6);
      Serial.print(",");
      Serial.print(ax, 4);
      Serial.print(",");
      Serial.print(ay, 4);
      Serial.print(",");
      Serial.print(az, 4);
      Serial.print("\n");
    }
  }

  // Serial commands for testing and data capture control
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') {
      sampleIndex = 0;
      nextSampleMicros = micros();
      loggingActive = true;
    } else if (c == 's') {
      loggingActive = false;
    } else if (c == 'g') {
      // Simulate "Go" trigger event and notify BLE characteristic
      unsigned long goTimestamp = micros();
      goTimeChar.writeValue(goTimestamp);
      Serial.print("GO! Timestamp: ");
      Serial.println(goTimestamp);
    }
  }
}

// Helper function to dispatch the detected reaction time to the companion app
void notifyReactionTime(float rtSeconds) {
  if (BLE.connected()) {
    reactionTimeChar.writeValue(rtSeconds);
  }
}