#include <ArduinoBLE.h>
#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// Prostart / BlockStart firmware - XIAO nRF52840 Sense
//
// Espone due caratteristiche sullo stesso servizio:
//   19B10001  "go" timestamp - single-shot, alta precisione (micros()).
//             E' quella che misura il tempo di reazione: non toccarla.
//   19B10002  stream accelerometro - notifiche continue a ~50 Hz, solo per
//             la visualizzazione live nell'app (schermata "Live Data").
//
// Gli UUID e il formato del payload devono restare allineati a
// prostart/lib/services/ble_service.dart.
// ---------------------------------------------------------------------------

BLEService reactionService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedLongCharacteristic goTimeChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Payload: 12 byte = 3 float32 little-endian (X, Y, Z) in g.
// Il nRF52840 e' little-endian e usa IEEE-754 a 32 bit, quindi il cast del
// buffer basta: e' esattamente quello che l'app legge con
// ByteData.getFloat32(offset, Endian.little).
BLECharacteristic accelChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, 12, true);

// ~50 Hz. L'app mette il timestamp all'arrivo del pacchetto, quindi un po' di
// jitter qui non e' un problema: serve solo che il flusso sia continuo.
const unsigned long IMU_NOTIFY_INTERVAL_MS = 20;

LSM6DS3 myIMU(I2C_MODE, 0x6A);

bool imuReady = false;
unsigned long lastImuNotify = 0;

void setup() {
  Serial.begin(115200);
  // Sul XIAO nRF52840 la USB e' nativa: `Serial` diventa true solo quando un
  // host apre la porta CDC. Aspettare all'infinito blocca setup() per sempre
  // se la board e' alimentata da batteria, da una porta senza Serial Monitor
  // aperto, o da un alimentatore - e quindi BLE.advertise() sotto non viene
  // mai eseguito e nessuna app riesce a trovare il dispositivo. Aspetta al
  // massimo 3 secondi, poi procedi comunque.
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) delay(10);

  setupImu();

  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    while (1);
  }

  BLE.setLocalName("BlockStartDevice");
  BLE.setAdvertisedService(reactionService);
  reactionService.addCharacteristic(goTimeChar);
  reactionService.addCharacteristic(accelChar);
  BLE.addService(reactionService);

  // Richiesta (non garanzia: decide il central) di un connection interval
  // 15-30 ms. Unita' = 1.25 ms. Con l'intervallo di default piu' lungo il
  // grafico a 50 Hz arriva a scatti.
  BLE.setConnectionInterval(12, 24);

  BLE.advertise();

  Serial.println("Ready. Type 'g' + Enter in Serial Monitor to simulate a 'go' event.");
}

void setupImu() {
#ifdef PIN_LSM6DS3TR_C_POWER
  // Sul XIAO Sense l'IMU ha un pin di alimentazione dedicato: se resta basso
  // begin() ritorna errore anche con l'I2C cablato correttamente.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif
  delay(100);  // l'LSM6DS3 ha bisogno di un attimo prima di rispondere su I2C

  // Solo accelerometro: il giroscopio non serve alla vista live e ogni lettura
  // in meno lascia piu' margine sull'I2C dentro la finestra da 20 ms.
  myIMU.settings.gyroEnabled = 0;
  // 104 Hz = poco piu' del doppio dei 50 Hz che notifichiamo, cosi' ogni
  // pacchetto porta un campione fresco senza sprecare banda I2C.
  myIMU.settings.accelSampleRate = 104;
  myIMU.settings.accelBandWidth = 50;
  // Il default della libreria e' +/-16 g: troppo grosso per un movimento
  // umano. A +/-4 g la risoluzione e' quattro volte migliore e restano
  // margini abbondanti per l'urto di una partenza.
  myIMU.settings.accelRange = 4;

  imuReady = (myIMU.begin() == 0);
  Serial.println(imuReady ? "IMU OK" : "IMU error - live data disabled");
}

void loop() {
  BLE.poll();

  streamAccelerometer();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'g') {
      // Nota: questo delay blocca anche BLE.poll() e lo stream IMU. Va bene
      // finche' e' solo la simulazione da Serial Monitor.
      unsigned long simulatedDelay = random(1000, 4000);
      delay(simulatedDelay);

      unsigned long goTimestamp = micros();
      goTimeChar.writeValue(goTimestamp);

      Serial.print("GO! Timestamp: ");
      Serial.println(goTimestamp);
    }
  }
}

void streamAccelerometer() {
  // subscribed() e' vero solo quando il client ha davvero abilitato le
  // notifiche - cioe' mentre l'app e' sulla schermata Live Data. Fuori da li'
  // non leggiamo nemmeno l'IMU.
  if (!imuReady || !BLE.connected() || !accelChar.subscribed()) return;

  unsigned long now = millis();
  if (now - lastImuNotify < IMU_NOTIFY_INTERVAL_MS) return;
  lastImuNotify = now;

  float axes[3];
  axes[0] = myIMU.readFloatAccelX();
  axes[1] = myIMU.readFloatAccelY();
  axes[2] = myIMU.readFloatAccelZ();

  accelChar.writeValue((const uint8_t*)axes, sizeof(axes));
}
