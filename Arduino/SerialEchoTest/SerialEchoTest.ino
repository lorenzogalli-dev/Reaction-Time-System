// Sketch minimo di diagnostica: nessun IMU, nessun BLE, nessuna FIFO.
// Serve a isolare se il problema e' nella board/cavo/porta o nel resto del
// firmware. Stampa un heartbeat ogni secondo e rimanda indietro ogni byte
// ricevuto, cosi' e' impossibile confonderlo con un blocco silenzioso.

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
