#include <Wire.h>

void setup() {
  Serial.begin(115200);
  // Stesso motivo spiegato in Arduino/BLEtest/BLEtest.ino: sul XIAO nRF52840
  // la USB e' nativa e `Serial` diventa true solo quando un host apre la porta
  // CDC, quindi un `while (!Serial)` senza limite blocca setup() per sempre se
  // la board non e' collegata a un Serial Monitor. Aspetta al massimo 3
  // secondi, poi procedi comunque.
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
