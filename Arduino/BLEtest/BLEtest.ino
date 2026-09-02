#include <ArduinoBLE.h>
#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// Prostart / BlockStart firmware - XIAO nRF52840 Sense
//
// REGOLA FONDAMENTALE DEL TEMPO
// -----------------------------
// Esiste un solo orologio in tutto il sistema: micros() del nRF52840.
// Ogni istante che finisce in una misura - il "go", il campione IMU, l'onset
// della partenza - e' espresso in quel clock. Il telefono non timestampa
// nulla: e' un logger/monitor passivo. La latenza del BLE puo' essere di
// decine di ms e variabile, ma non tocca la misura perche' il tempo e' gia'
// stato deciso a bordo prima di entrare nella radio.
//
// La cattura CSV del 2026-08-31 lo dimostra al contrario: quei timestamp erano
// ore di arrivo sul telefono, con raffiche di 1-4 campioni consegnate insieme
// e gap di 29-31 ms (a volte 58-62). Incertezza reale +/-15-30 ms, contro un
// target di 1-2 ms. Vedi playground_IMU/README.md.
//
// CARATTERISTICHE BLE
//   19B10001  "go" timestamp - single-shot, micros(). Invariata.
//   19B10002  stream accelerometro ~50 Hz per la vista Live Data.
//             *** PAYLOAD CAMBIATO: ora 16 byte, non piu' 12. ***
//             Decimato dal flusso ad alta frequenza, e ogni campione porta
//             il proprio istante di cattura. Resta puramente cosmetico.
//   19B10004  dump raw ad alta frequenza (notify) - vedi "DUMP" sotto.
//   19B10005  comandi dall'app (write) - per ora solo "richiedi un dump".
//
// Gli UUID e i formati dei payload devono restare allineati a
// prostart/lib/services/ble_service.dart.
//
// PERCHE' LA FIFO
// ---------------
// Prima si leggevano i registri dell'IMU in polling su millis() ogni 20 ms,
// con l'ODR a 104 Hz: due frequenze non sincrone, quindi campioni ripetuti e
// campioni persi, e comunque una quantizzazione di 9.6 ms sull'onset. Ora
// l'accelerometro gira a 833 Hz dentro la FIFO hardware e il loop la svuota a
// blocchi: il campionamento e' scandito dal sensore, non dal loop, e il jitter
// del software non entra piu' nei dati.
//
// DUMP
// ----
// A 833 Hz servono ~5 kB/s solo per i raw: il link BLE (connection interval
// ~30 ms) non ci arriva. Quindi i campioni ad alta frequenza vivono in un ring
// buffer in RAM (~2.4 s di storico) e vengono riversati a richiesta, a
// pacchetti, quando non c'e' nulla di urgente da fare. Dal punto 3 in poi il
// dump sara' innescato automaticamente dalla detection dell'onset, per portare
// al telefono la finestra attorno alla partenza.
// ---------------------------------------------------------------------------

BLEService reactionService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedLongCharacteristic goTimeChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Payload live: 16 byte = uint32 t_us little-endian + 3 float32 (X, Y, Z) in g.
// t_us e' l'istante di *cattura* del campione nel clock micros() di bordo, non
// l'istante di invio. Il nRF52840 e' little-endian e usa IEEE-754 a 32 bit,
// quindi il cast del buffer basta: e' esattamente quello che l'app legge con
// ByteData.getUint32/getFloat32(offset, Endian.little).
BLECharacteristic accelChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, 16, true);

// Dump raw. Header 8 byte + N campioni da 6 byte (3 x int16 raw, non in g):
//   [0..3]  uint32 t0_us   istante di cattura del PRIMO campione del pacchetto
//   [4..5]  uint16 seq     numero di pacchetto dentro questo dump
//   [6]     uint8  count   campioni nel pacchetto
//   [7]     uint8  flags   bit0 = primo pacchetto, bit1 = ultimo
//   [8..]   count x { int16 x, int16 y, int16 z }
// I campioni sono raw a 16 bit: l'app li converte in g con lo stesso fattore
// della libreria, 0.061 * (range >> 1) / 1000 (vedi LSM6DS3::calcAccel).
// Ogni pacchetto porta il proprio t0_us, quindi il flusso e' ricostruibile
// anche se qualche pacchetto si perde.
// Quanti campioni per pacchetto. 2 tiene il payload a 20 byte, che passa anche
// con l'MTU minimo (23) senza essere troncato. Se l'app negozia un MTU piu'
// grande (Android: requestMtu(247); iOS lo fa da solo a 185) questo valore si
// puo' alzare a 28 -> 180 byte per pacchetto, e il dump diventa ~14x piu'
// veloce. Da alzare solo dopo aver verificato l'MTU sul campo: se il payload
// supera MTU-3 la notifica viene troncata in silenzio.
#define BURST_SAMPLES_PER_PKT 2
#define BURST_HEADER_BYTES    8
#define BURST_PKT_BYTES       (BURST_HEADER_BYTES + BURST_SAMPLES_PER_PKT * 6)

BLECharacteristic burstChar("19B10004-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, BURST_PKT_BYTES, false);

// Comandi dall'app. 1 byte: 0x01 = avvia un dump del ring buffer.
BLECharacteristic cmdChar("19B10005-E8F2-537E-4F6C-D104768A1214", BLEWrite, 1, false);

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// --- Configurazione IMU ---------------------------------------------------
// 833 Hz: periodo 1.2 ms. E' la prima ODR che mette la quantizzazione sotto il
// target di 1-2 ms *senza* interpolazione; con l'interpolazione sulla salita
// (punto 3) si scende ampiamente sotto il millisecondo. 416 Hz sarebbe il
// minimo accettabile, 833 lascia margine.
static const uint16_t ACCEL_ODR_HZ = 833;
// Codice ODR della FIFO corrispondente a 833 Hz: la libreria lo chiama "800"
// ma il registro e' lo stesso (FIFO_CTRL5 = 0x38). Deve combaciare con l'ODR
// dell'accelerometro, altrimenti la FIFO decima o duplica.
static const int16_t FIFO_ODR_CODE = 800;
// +/-4 g per ora. Da rivalutare a +/-8 g quando il montaggio meccanico sul
// blocco e' definitivo: nella cattura del 31/08 il picco era 0.43 g, ma con il
// sensore rigidamente sul blocco una partenza vera fa 2-5 g e a +/-4 g si
// rischia il clipping proprio sul fronte di salita.
static const uint8_t ACCEL_RANGE_G = 4;

// La FIFO tiene parole da 16 bit e le organizza in "pattern". Con il giroscopio
// spento e i dataset 3/4 disabilitati il pattern e' esattamente X, Y, Z.
static const uint8_t FIFO_PATTERN_WORDS = 3;

// Registri usati direttamente: la libreria non espone ne' la lettura a blocchi
// della FIFO ne' FIFO_STATUS3/4 (che servono per riallinearsi al pattern).
static const uint8_t REG_FIFO_CTRL4    = 0x09;
static const uint8_t REG_FIFO_STATUS1  = 0x3A;
static const uint8_t REG_FIFO_DATA_OUT = 0x3E;

// --- Ring buffer ad alta frequenza ----------------------------------------
// 2048 campioni a 833 Hz = 2.46 s di storico, 12 kB di RAM. Abbondante per
// coprire una finestra attorno alla partenza (tipicamente -0.5 s / +1.0 s).
static const uint16_t RING_SAMPLES = 2048;
static int16_t ringBuf[RING_SAMPLES * 3];

// Indice globale monotono del campione piu' recente + 1, cioe' quanti campioni
// sono stati acquisiti da sempre. La posizione nel ring e' (k % RING_SAMPLES),
// quindi l'indice globale e' anche la base dei timestamp.
static uint32_t totalSamples = 0;

// --- Modello del tempo (PLL software) -------------------------------------
// La FIFO dice *quanti* campioni ci sono, non *quando* sono stati presi. Il
// periodo lo conosciamo (1/ODR) ma l'oscillatore dell'LSM6DS3 ha la sua
// tolleranza (qualche %) rispetto al clock del nRF52840: estrapolare da una
// singola ancora accumulerebbe deriva. Quindi si tiene un anello ad aggancio
// di fase: a ogni svuotamento si confronta l'istante previsto per il campione
// piu' recente con micros() e si correggono piano sia la fase sia il periodo.
// Il risultato e' un periodo auto-calibrato sul clock di bordo e timestamp che
// non sobbalzano quando un giro di loop arriva tardi.
static double   estPeriodUs   = 1000000.0 / (double)ACCEL_ODR_HZ;
static uint32_t newestSampleUs = 0;   // micros() del campione piu' recente
static bool     clockLocked    = false;

// Guadagni del PLL. Proporzionale alto = riaggancio rapido dopo un buco;
// integrale basso = il periodo si muove lentamente e filtra il jitter del loop.
static const double PLL_KP = 0.10;
static const double PLL_KI = 0.0005;
// Il periodo non puo' allontanarsi piu' del 6% dal nominale: oltre significa
// che qualcosa e' andato storto (overrun, riallineamento), non che
// l'oscillatore e' derivato.
static const double PERIOD_NOMINAL_US = 1000000.0 / (double)ACCEL_ODR_HZ;
static const double PERIOD_MIN_US     = PERIOD_NOMINAL_US * 0.94;
static const double PERIOD_MAX_US     = PERIOD_NOMINAL_US * 1.06;

// --- Stream live decimato -------------------------------------------------
// 833 / 17 = 49 Hz, la stessa cadenza di prima per la schermata Live Data.
static const uint16_t LIVE_DECIMATION = 17;
static uint32_t nextLiveSample = 0;

// --- Stato del dump -------------------------------------------------------
static bool     dumpActive = false;
static uint32_t dumpNext = 0;     // prossimo indice globale da inviare
static uint32_t dumpEnd = 0;      // indice globale (escluso) di fine dump
static uint16_t dumpSeq = 0;

// --- "go" simulato --------------------------------------------------------
// Il trigger e' simulato da seriale finche' il buzzer non e' cablato, ma passa
// dallo stesso micros() che usera' il trigger audio vero: quando si sostituira'
// la sorgente, la gestione del tempo non cambia di una riga.
// Non blocca piu' con delay(): un delay qui fermerebbe anche lo svuotamento
// della FIFO e la farebbe andare in overrun (4096 byte = ~1.6 s a 833 Hz).
static bool     goPending = false;
static uint32_t goDueUs = 0;

static bool imuReady = false;

// Prototipi espliciti: l'auto-prototyping dell'IDE Arduino non e' affidabile
// con le funzioni `static` in un .ino.
void setupImu();
static void drainFifo();
static void updateTimebase(uint32_t tStatus, uint16_t n, uint16_t leftBehind);
static uint32_t sampleTimeUs(uint32_t k);
static void publishLiveSamples();
static void startDump(uint32_t count);
static void serviceDump();
static void serviceGo();
static void handleSerial();
static void handleCommand();
static void printTimebase();
static void printLatest();

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
  reactionService.addCharacteristic(burstChar);
  reactionService.addCharacteristic(cmdChar);
  BLE.addService(reactionService);

  // Richiesta (non garanzia: decide il central) di un connection interval
  // 15-30 ms. Unita' = 1.25 ms. Con l'intervallo di default piu' lungo il
  // grafico a 50 Hz arriva a scatti e il dump ci mette un'eternita'.
  BLE.setConnectionInterval(12, 24);

  BLE.advertise();

  Serial.println("Ready. Serial: 'g' go, 'd' dump, 't' timebase, 'p' latest sample.");
}

void setupImu() {
#ifdef PIN_LSM6DS3TR_C_POWER
  // Sul XIAO Sense l'IMU ha un pin di alimentazione dedicato: se resta basso
  // begin() ritorna errore anche con l'I2C cablato correttamente.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif
  delay(100);  // l'LSM6DS3 ha bisogno di un attimo prima di rispondere su I2C

  // Solo accelerometro: il giroscopio non serve e ogni parola in meno nella
  // FIFO e' banda I2C risparmiata, che a 833 Hz conta.
  myIMU.settings.gyroEnabled = 0;
  myIMU.settings.gyroFifoEnabled = 0;

  myIMU.settings.accelEnabled = 1;
  myIMU.settings.accelSampleRate = ACCEL_ODR_HZ;
  myIMU.settings.accelRange = ACCEL_RANGE_G;
  // Attenzione a questi due, insieme: la libreria azzera CTRL4_C.BW_SCAL_ODR a
  // meno che accelODROff non sia 1, e con quel bit a 0 i bit BW_XL vengono
  // *ignorati* e la banda la decide l'ODR (ODR/2). Nel firmware precedente
  // accelBandWidth = 50 non aveva quindi alcun effetto: a 104 Hz la banda
  // reale era 52 Hz per coincidenza. Qui la fissiamo esplicitamente a 200 Hz
  // cosi' il filtro anti-alias - e il suo ritardo di gruppo, da misurare al
  // punto 5 - resta lo stesso anche se l'ODR cambia.
  myIMU.settings.accelODROff = 1;
  myIMU.settings.accelBandWidth = 200;

  myIMU.settings.accelFifoEnabled = 1;
  myIMU.settings.accelFifoDecimation = 1;  // /1: nessuna decimazione
  myIMU.settings.tempEnabled = 0;
  // Il timestamp hardware dell'LSM6DS3 sarebbe scritto nella FIFO come dataset
  // extra, ma e' su un altro oscillatore: non serve, il clock di riferimento e'
  // micros(). Tenerlo fuori accorcia il pattern e semplifica l'allineamento.
  myIMU.settings.timestampEnabled = 0;
  myIMU.settings.timestampFifoEnabled = 0;

  myIMU.settings.fifoThreshold = 96;   // non usiamo il watermark, ma va scritto
  myIMU.settings.fifoSampleRate = FIFO_ODR_CODE;
  myIMU.settings.fifoModeWord = 6;     // continuous

  imuReady = (myIMU.begin() == 0);
  if (!imuReady) {
    Serial.println("IMU error - streaming disabled");
    return;
  }

  // 400 kHz invece dei 100 kHz di default: a 833 Hz si leggono ~15 kB/s dalla
  // FIFO, a 100 kHz non ci starebbero nel budget del loop.
  Wire.setClock(400000);

  myIMU.fifoBegin();
  // fifoBegin() scrive FIFO_CTRL4 = 0x09, che infila i dataset 3 e 4 nel
  // pattern della FIFO. Non ci servono e allungherebbero il pattern da 3 a 5
  // parole: azzerali.
  myIMU.writeRegister(REG_FIFO_CTRL4, 0x00);
  myIMU.fifoClear();

  Serial.print("IMU OK - accel ");
  Serial.print(ACCEL_ODR_HZ);
  Serial.print(" Hz, +/-");
  Serial.print(ACCEL_RANGE_G);
  Serial.println(" g, FIFO continuous");
}

void loop() {
  BLE.poll();

  drainFifo();
  serviceGo();
  serviceDump();
  handleSerial();
  handleCommand();
}

// ---------------------------------------------------------------------------
// FIFO
// ---------------------------------------------------------------------------

// Legge FIFO_STATUS1..4 in un colpo solo: parole disponibili, flag e - la cosa
// che conta - la posizione nel pattern della prossima parola da leggere.
static bool readFifoStatus(uint16_t* words, uint16_t* pattern, bool* overrun, bool* empty) {
  uint8_t s[4];
  if (myIMU.readRegisterRegion(s, REG_FIFO_STATUS1, 4) != IMU_SUCCESS) return false;
  *words   = ((uint16_t)(s[1] & 0x0F) << 8) | s[0];
  *overrun = (s[1] & 0x40) != 0;
  *empty   = (s[1] & 0x10) != 0;
  *pattern = ((uint16_t)(s[3] & 0x03) << 8) | s[2];
  return true;
}

// Legge `count` parole da 16 bit dalla FIFO. Letture a blocchi da 0x3E: e' il
// modo documentato da ST (il registro di uscita non auto-incrementa, ogni
// coppia di byte letta fa avanzare la FIFO). A blocchi perche' una transazione
// I2C per parola costerebbe ~2500 transazioni al secondo.
// Diagnostica: ogni modo in cui la lettura puo' andare storta ha il suo
// contatore, cosi' 't' dice quale sta succedendo invece di lasciarlo indovinare.
static uint32_t errShortRead = 0;   // l'I2C ha trasferito meno del richiesto
static uint32_t errAddrNack = 0;    // NACK sull'indirizzo del registro
static uint32_t errMisaligned = 0;  // pattern non a 0 dopo un blocco intero
static uint32_t cntOverrun = 0;
static uint32_t cntRealign = 0;

// Legge `count` parole da 16 bit dalla FIFO.
//
// Scritta a mano invece di usare LSM6DS3::readRegisterRegion per due motivi,
// entrambi emersi dal test sul banco:
//   - readRegisterRegion butta via il valore di ritorno di requestFrom(). Se
//     il trasferimento I2C fallisce, riempie meno byte del richiesto e ritorna
//     comunque IMU_SUCCESS: chi chiama crede di avere dati validi, il buffer
//     contiene avanzi della lettura precedente, e la FIFO resta disallineata.
//     Ogni parola persa ruota gli assi X/Y/Z fra loro da li' in avanti.
// (Il repeated START e' stato provato e scartato: vedi il commento sotto.)
static bool readFifoWords(int16_t* dest, uint16_t count) {
  // Il buffer di Wire del core mbed e' 256 byte, quindi il limite vero e' il
  // tempo che il loop puo' passare fermo su una singola transazione.
  static const uint8_t MAX_WORDS_PER_READ = 15;
  uint8_t raw[MAX_WORDS_PER_READ * 2];

  while (count > 0) {
    uint8_t n = (count > MAX_WORDS_PER_READ) ? MAX_WORDS_PER_READ : (uint8_t)count;
    uint8_t bytes = n * 2;

    Wire.beginTransmission(0x6A);
    Wire.write(REG_FIFO_DATA_OUT);
    // STOP, non repeated START. Il repeated START sembrerebbe piu' corretto per
    // un accesso alla FIFO, ma sul driver I2C del core mbed incastra il bus: la
    // sua read() non ha timeout, quindi il loop si pianta e la board smette di
    // rispondere. Provato sul banco. Con lo STOP nessuno interferisce comunque:
    // l'I2C ha un solo master e un solo slave.
    if (Wire.endTransmission(true) != 0) {
      errAddrNack++;
      return false;
    }
    if (Wire.requestFrom((uint8_t)0x6A, (size_t)bytes) != bytes) {
      errShortRead++;
      return false;
    }
    for (uint8_t i = 0; i < bytes; i++) {
      if (!Wire.available()) {
        errShortRead++;
        return false;
      }
      raw[i] = (uint8_t)Wire.read();
    }
    for (uint8_t i = 0; i < n; i++) {
      dest[i] = (int16_t)(((uint16_t)raw[i * 2 + 1] << 8) | raw[i * 2]);
    }
    dest += n;
    count -= n;
  }
  return true;
}

// Butta via parole finche' la prossima letta non e' l'inizio di un pattern, e
// ritorna quante ne ha scartate. Serve dopo un overrun: se si legge sfasati,
// X/Y/Z si scambiano fra loro e i dati sembrano plausibili pur essendo
// sbagliati - il tipo di bug che non si vede guardando un grafico.
//
// FIFO_PATTERN e' l'indice, dentro il pattern, della prossima parola che
// uscira'. Se vale 1 la prossima e' Y, quindi per arrivare alla X successiva
// vanno scartate 3-1 = 2 parole (non 1).
static uint16_t realignFifo(uint16_t pattern, uint16_t available) {
  uint16_t offset = pattern % FIFO_PATTERN_WORDS;
  if (offset == 0) return 0;
  uint16_t toDiscard = FIFO_PATTERN_WORDS - offset;
  if (toDiscard > available) return available;  // niente da fare per ora
  int16_t discard[FIFO_PATTERN_WORDS];
  readFifoWords(discard, toDiscard);
  return toDiscard;
}

static void drainFifo() {
  if (!imuReady) return;

  uint16_t words, pattern;
  bool overrun, empty;
  if (!readFifoStatus(&words, &pattern, &overrun, &empty)) return;
  // micros() preso subito dopo lo status: e' l'ancora del PLL. Da qui in poi
  // il tempo che passa a leggere i dati non conta piu'.
  uint32_t tStatus = micros();

  if (overrun) {
    // La FIFO e' andata in overrun: il loop non ha tenuto il passo e c'e' un
    // buco di durata ignota. Riparti pulito invece di produrre timestamp che
    // sembrano continui ma non lo sono.
    myIMU.fifoClear();
    clockLocked = false;
    cntOverrun++;
    return;
  }
  if (empty || words < FIFO_PATTERN_WORDS) return;

  uint16_t discarded = realignFifo(pattern, words);
  if (discarded) cntRealign++;
  uint16_t usable = words - discarded;
  uint16_t available = usable / FIFO_PATTERN_WORDS;
  if (available == 0) return;

  // Non svuotare tutto in un colpo: oltre un certo blocco il loop resta fermo
  // troppo a lungo e BLE.poll() ne soffre. Il resto se lo prende il giro dopo.
  static const uint16_t MAX_SAMPLES_PER_DRAIN = 64;
  uint16_t nSamples = (available > MAX_SAMPLES_PER_DRAIN) ? MAX_SAMPLES_PER_DRAIN : available;
  // Quanti campioni restano nella FIFO dopo questa lettura: servono al PLL,
  // perche' l'ancora micros() si riferisce al campione piu' recente *nella
  // FIFO*, non all'ultimo che abbiamo letto. Senza questo, ogni svuotamento
  // troncato sposterebbe i timestamp all'indietro di decine di ms.
  uint16_t leftBehind = available - nSamples;

  int16_t block[MAX_SAMPLES_PER_DRAIN * 3];
  if (!readFifoWords(block, nSamples * FIFO_PATTERN_WORDS)) {
    // Lettura fallita a meta': la FIFO e' in uno stato che non conosciamo.
    // Non provare a indovinare quanto e' avanzata, riparti pulita.
    myIMU.fifoClear();
    clockLocked = false;
    return;
  }

  // Abbiamo letto un numero intero di pattern partendo da un pattern intero,
  // quindi il prossimo deve essere di nuovo a 0. Se non lo e', qualcosa ha
  // mangiato o aggiunto parole e da qui in poi X/Y/Z sarebbero permutati: i
  // dati sembrerebbero plausibili pur essendo sbagliati. Meglio buttare via il
  // blocco e risincronizzare che salvarlo.
  uint16_t w2, p2;
  bool o2, e2;
  if (readFifoStatus(&w2, &p2, &o2, &e2) && (p2 % FIFO_PATTERN_WORDS) != 0) {
    errMisaligned++;
    myIMU.fifoClear();
    clockLocked = false;
    return;
  }

  for (uint16_t i = 0; i < nSamples; i++) {
    uint32_t slot = ((totalSamples + i) % RING_SAMPLES) * 3;
    ringBuf[slot + 0] = block[i * 3 + 0];
    ringBuf[slot + 1] = block[i * 3 + 1];
    ringBuf[slot + 2] = block[i * 3 + 2];
  }
  totalSamples += nSamples;

  updateTimebase(tStatus, nSamples, leftBehind);
  publishLiveSamples();
}

// ---------------------------------------------------------------------------
// Modello del tempo
// ---------------------------------------------------------------------------

// tStatus:     micros() letto insieme allo stato della FIFO.
// n:           campioni appena aggiunti al ring.
// leftBehind:  campioni rimasti nella FIFO, non ancora letti.
static void updateTimebase(uint32_t tStatus, uint16_t n, uint16_t leftBehind) {
  // Il campione piu' recente *della FIFO* e' stato catturato fra 0 e un periodo
  // prima della lettura dello status: il valore atteso e' mezzo periodo prima.
  // Ma l'ultimo campione che abbiamo messo nel ring e' piu' vecchio di quello
  // di `leftBehind` periodi, ed e' a lui che si riferisce newestSampleUs.
  uint32_t measured = tStatus
                    - (uint32_t)(estPeriodUs * 0.5)
                    - (uint32_t)(estPeriodUs * (double)leftBehind);

  if (!clockLocked) {
    newestSampleUs = measured;
    estPeriodUs = PERIOD_NOMINAL_US;
    clockLocked = true;
    return;
  }

  // Dove ci aspettavamo che fosse il nuovo campione piu' recente, estrapolando
  // dal modello corrente.
  double predicted = (double)newestSampleUs + estPeriodUs * (double)n;
  // Differenza calcolata su uint32 e poi su int32: cosi' il wrap di micros()
  // (ogni ~71 minuti) si gestisce da solo.
  int32_t err = (int32_t)(measured - (uint32_t)predicted);

  // Un errore enorme non e' deriva, e' una discontinuita': riaggancia di
  // colpo invece di trascinarla dentro il periodo.
  if (err > 20000 || err < -20000) {
    newestSampleUs = measured;
    estPeriodUs = PERIOD_NOMINAL_US;
    return;
  }

  newestSampleUs = (uint32_t)(predicted + PLL_KP * (double)err);
  estPeriodUs += PLL_KI * (double)err / (double)n;
  if (estPeriodUs < PERIOD_MIN_US) estPeriodUs = PERIOD_MIN_US;
  if (estPeriodUs > PERIOD_MAX_US) estPeriodUs = PERIOD_MAX_US;
}

// Istante di cattura del campione con indice globale k, nel clock micros().
// Vale per qualunque k gia' acquisito: e' cosi' che la detection dell'onset
// (punto 3) potra' datare un campione trovato all'indietro nel ring, e come il
// dump timestampa quello che manda.
static uint32_t sampleTimeUs(uint32_t k) {
  double back = (double)(totalSamples - 1 - k) * estPeriodUs;
  return newestSampleUs - (uint32_t)(back + 0.5);
}

// ---------------------------------------------------------------------------
// Stream live (cosmetico)
// ---------------------------------------------------------------------------

static void publishLiveSamples() {
  // subscribed() e' vero solo mentre l'app e' sulla schermata Live Data. La
  // FIFO pero' si svuota comunque: la detection on-device non deve dipendere
  // da chi sta guardando.
  if (!BLE.connected() || !accelChar.subscribed()) {
    nextLiveSample = totalSamples;
    return;
  }
  uint32_t oldest = (totalSamples > RING_SAMPLES) ? (totalSamples - RING_SAMPLES) : 0;
  // Rimasti indietro oltre il ring: riparti dal presente invece di leggere
  // campioni gia' sovrascritti.
  if (nextLiveSample < oldest) nextLiveSample = totalSamples;
  // Non e' ancora ora del prossimo campione decimato.
  if (nextLiveSample >= totalSamples) return;

  // Un pacchetto per svuotamento: a 833 Hz gli svuotamenti sono ogni pochi ms,
  // quindi 49 Hz si mantengono comunque e la coda del BLE non si intasa.
  uint32_t k = nextLiveSample;
  nextLiveSample = k + LIVE_DECIMATION;

  uint32_t slot = (k % RING_SAMPLES) * 3;
  uint8_t payload[16];
  uint32_t t = sampleTimeUs(k);
  float g[3] = {
    myIMU.calcAccel(ringBuf[slot + 0]),
    myIMU.calcAccel(ringBuf[slot + 1]),
    myIMU.calcAccel(ringBuf[slot + 2]),
  };
  memcpy(payload, &t, 4);
  memcpy(payload + 4, g, 12);
  accelChar.writeValue(payload, sizeof(payload));
}

// ---------------------------------------------------------------------------
// Dump del ring buffer
// ---------------------------------------------------------------------------

// Avvia il dump degli ultimi `count` campioni. Dal punto 3 questa sara'
// chiamata dalla detection con una finestra centrata sull'onset.
static void startDump(uint32_t count) {
  if (totalSamples == 0) return;
  uint32_t available = min(totalSamples, (uint32_t)RING_SAMPLES);
  if (count > available) count = available;
  dumpNext = totalSamples - count;
  dumpEnd = totalSamples;
  dumpSeq = 0;
  dumpActive = true;
  Serial.print("Dump started: ");
  Serial.print(count);
  Serial.println(" samples");
}

static void serviceDump() {
  if (!dumpActive) return;
  if (!BLE.connected() || !burstChar.subscribed()) {
    dumpActive = false;
    return;
  }
  // Se il ring ha girato sotto i piedi del dump, salta avanti: meglio un buco
  // dichiarato che campioni sovrascritti spacciati per buoni.
  uint32_t oldest = (totalSamples > RING_SAMPLES) ? (totalSamples - RING_SAMPLES) : 0;
  if (dumpNext < oldest) dumpNext = oldest;

  uint8_t count = BURST_SAMPLES_PER_PKT;
  if (dumpEnd - dumpNext < count) count = (uint8_t)(dumpEnd - dumpNext);
  if (count == 0) {
    dumpActive = false;
    Serial.println("Dump complete");
    return;
  }

  uint8_t pkt[BURST_PKT_BYTES];
  uint32_t t0 = sampleTimeUs(dumpNext);
  uint8_t flags = 0;
  if (dumpSeq == 0) flags |= 0x01;
  if (dumpNext + count >= dumpEnd) flags |= 0x02;

  memcpy(pkt, &t0, 4);
  memcpy(pkt + 4, &dumpSeq, 2);
  pkt[6] = count;
  pkt[7] = flags;
  for (uint8_t i = 0; i < count; i++) {
    uint32_t slot = ((dumpNext + i) % RING_SAMPLES) * 3;
    memcpy(pkt + BURST_HEADER_BYTES + i * 6, &ringBuf[slot], 6);
  }
  burstChar.writeValue(pkt, BURST_HEADER_BYTES + count * 6);

  dumpNext += count;
  dumpSeq++;
  // Un pacchetto per giro di loop: il resto del tempo serve a svuotare la FIFO
  // e a far girare la radio. Il dump e' logging, non ha fretta.
}

// ---------------------------------------------------------------------------
// Trigger
// ---------------------------------------------------------------------------

static void serviceGo() {
  if (!goPending) return;
  if ((int32_t)(micros() - goDueUs) < 0) return;
  goPending = false;

  // L'istante che conta e' quello in cui l'evento si e' verificato, letto qui
  // e non quando la notifica parte. Quando al posto della seriale ci sara' il
  // buzzer, cambia solo chi arma goDueUs.
  uint32_t goTimestamp = micros();
  goTimeChar.writeValue(goTimestamp);

  Serial.print("GO! Timestamp: ");
  Serial.println(goTimestamp);
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'g') {
    // Ritardo casuale come prima, ma armato invece che atteso con delay():
    // il loop continua a svuotare la FIFO nel frattempo.
    goDueUs = micros() + (uint32_t)random(1000000, 4000000);
    goPending = true;
    Serial.println("Go armed");
  } else if (c == 'd') {
    startDump(RING_SAMPLES);
  } else if (c == 't') {
    printTimebase();
  } else if (c == 'p') {
    printLatest();
  }
}

// ---------------------------------------------------------------------------
// Diagnostica
// ---------------------------------------------------------------------------

// Verifica della timebase. Il confronto che conta e' fra il periodo stimato dal
// PLL e il periodo *misurato*: campioni acquisiti contro microsecondi passati
// sul clock di bordo, fra due invocazioni di questo comando. Se la FIFO si
// svuota correttamente e l'ODR e' davvero 833 Hz, entrambi stanno attorno a
// 1200 us. Guardare i delta fra timestamp consecutivi non proverebbe nulla:
// sono generati dal periodo stimato, quindi tornerebbero per costruzione.
static uint32_t lastStatSamples = 0;
static uint32_t lastStatMicros = 0;

static void printTimebase() {
  uint32_t now = micros();
  uint32_t n = totalSamples;

  Serial.println("--- timebase ---");
  Serial.print("totalSamples   "); Serial.println(n);
  Serial.print("estPeriod_us   "); Serial.println(estPeriodUs, 3);
  Serial.print("estRate_Hz     "); Serial.println(1000000.0 / estPeriodUs, 2);
  // Quanto e' vecchio il campione piu' recente: se il loop sta al passo deve
  // essere di pochi ms. Un valore grande significa FIFO che si accumula.
  Serial.print("newestAge_us   "); Serial.println((int32_t)(now - newestSampleUs));
  Serial.print("overruns       "); Serial.println(cntOverrun);
  Serial.print("realigns       "); Serial.println(cntRealign);
  Serial.print("shortReads     "); Serial.println(errShortRead);
  Serial.print("addrNacks      "); Serial.println(errAddrNack);
  Serial.print("misaligned     "); Serial.println(errMisaligned);

  if (lastStatSamples != 0 && n > lastStatSamples) {
    uint32_t dn = n - lastStatSamples;
    uint32_t dt = now - lastStatMicros;
    Serial.print("--- since last 't' ---");
    Serial.println();
    Serial.print("samples        "); Serial.println(dn);
    Serial.print("interval_us    "); Serial.println(dt);
    Serial.print("measPeriod_us  "); Serial.println((double)dt / (double)dn, 3);
    Serial.print("measRate_Hz    "); Serial.println(1000000.0 * (double)dn / (double)dt, 2);
  } else {
    Serial.println("(press 't' again to get a measured rate)");
  }
  lastStatSamples = n;
  lastStatMicros = now;
}

// Ultimo campione in g. Serve a controllare l'allineamento del pattern FIFO:
// con la board ferma sul tavolo Z deve stare attorno a 1 g e X/Y vicini a 0.
// Se il pattern fosse sfasato gli assi risulterebbero permutati.
static void printLatest() {
  if (totalSamples == 0) {
    Serial.println("no samples yet");
    return;
  }
  uint32_t k = totalSamples - 1;
  uint32_t slot = (k % RING_SAMPLES) * 3;
  Serial.print("latest  t_us=");
  Serial.print(sampleTimeUs(k));
  Serial.print("  x=");
  Serial.print(myIMU.calcAccel(ringBuf[slot + 0]), 4);
  Serial.print("  y=");
  Serial.print(myIMU.calcAccel(ringBuf[slot + 1]), 4);
  Serial.print("  z=");
  Serial.println(myIMU.calcAccel(ringBuf[slot + 2]), 4);
}

static void handleCommand() {
  if (!cmdChar.written()) return;
  const uint8_t* v = cmdChar.value();
  if (cmdChar.valueLength() >= 1 && v[0] == 0x01) {
    startDump(RING_SAMPLES);
  }
}
