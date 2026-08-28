#include <ArduinoBLE.h>

BLEService reactionService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedLongCharacteristic goTimeChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    while (1);
  }

  BLE.setLocalName("BlockStartDevice");
  BLE.setAdvertisedService(reactionService);
  reactionService.addCharacteristic(goTimeChar);
  BLE.addService(reactionService);
  BLE.advertise();

  Serial.println("Ready. Type 'g' + Enter in Serial Monitor to simulate a 'go' event.");
}

void loop() {
  BLE.poll();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'g') {
      unsigned long simulatedDelay = random(1000, 4000);
      delay(simulatedDelay);

      unsigned long goTimestamp = micros();
      goTimeChar.writeValue(goTimestamp);

      Serial.print("GO! Timestamp: ");
      Serial.println(goTimestamp);
    }
  }
}