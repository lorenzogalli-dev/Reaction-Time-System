#include <Wire.h>

void setup() {
  Serial.begin(115200);
  // On the XIAO nRF52840 the USB is native, so `Serial` only becomes true once
  // a host opens the CDC port. An unbounded `while (!Serial)` therefore blocks
  // setup() forever whenever the board is not attached to a Serial Monitor.
  // Wait at most 3 seconds, then carry on regardless.
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
