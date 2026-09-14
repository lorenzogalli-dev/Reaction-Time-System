// Minimal diagnostic sketch: no IMU, no BLE, no FIFO. It exists to separate
// "the board, cable or port is at fault" from "the rest of the firmware is".
// It prints a heartbeat every second and echoes back every byte it receives,
// so it cannot be mistaken for a silent hang.

unsigned long lastBeat = 0;

void setup() {
  Serial.begin(921600);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("ECHO TEST READY - digita qualcosa, torna indietro. Heartbeat ogni secondo.");
}

void loop() {
  if (millis() - lastBeat >= 1000) {
    lastBeat = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print("heartbeat t=");
    Serial.println(millis());
  }

  if (Serial.available()) {
    char c = Serial.read();
    Serial.print("ricevuto: '");
    Serial.print(c);
    Serial.println("'");
  }
}
