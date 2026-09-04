#include <Wire.h>

void setup() {
  Serial.begin(115200);
  // Same reason as explained in Arduino/BLEtest/BLEtest.ino: on the XIAO
  // nRF52840 USB is native and `Serial` only becomes true when a host opens
  // the CDC port, so an unbounded `while (!Serial)` blocks setup() forever if
  // the board is not connected to a Serial Monitor. Wait at most 3 seconds,
  // then proceed anyway.
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) delay(10);

  Wire.begin();

  Serial.println("Scanning I2C bus...");
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found device at address 0x");
      Serial.println(addr, HEX);
      count++;
    }
  }
  Serial.print("Total devices found: ");
  Serial.println(count);
}

void loop() {}
