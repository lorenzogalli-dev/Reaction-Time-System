#include <Arduino.h>
// EspNowLrTest - ESP-NOW long range fra due ESP32-C3, misurato sul campo.
//
// Stesso firmware sulle due schede; il ruolo e' salvato in flash e lo imposta
// il logger (logger.py A|B) la prima volta che si collega.
//
//   A = la partenza. A terra dietro il blocco. Trasmette.
//   B = l'arrivo. Cammina con voi, accanto al telefono. Riceve e fa eco.
//
// Cosa misura, per ogni sosta:
//   - pacchetti consegnati A->B (e ritorno B->A), un pacchetto ogni 50 ms
//   - tre modulazioni a confronto NELLO STESSO punto e negli stessi secondi:
//     cambiano ogni 5 s, LR 250k -> LR 500k -> 802.11b 1M -> di nuovo LR 250k.
//     Il 22/09 due camminate identiche differivano di 8 dB: confrontare i modi
//     in camminate diverse sarebbe misurare il rumore.
//   - un t0 finto per slot, ripetuto ogni 250 ms finche' B non lo conferma:
//     i tentativi che servono sono la metrica del prodotto.
//   - ritardo e offset di clock A<->B (stile NTP) su ogni eco.
//
// Coesistenza (il rischio n.1 in HANDOFF): con "ap on" B tiene anche un
// access point per il telefono sullo stesso canale. Il telefono apre
// http://192.168.4.1 e la pagina genera traffico continuo, come fara' l'app.
//
// Righe sulla seriale (il logger le salva cosi' come sono):
//   I,role,mac,canale,ap,txpow_dBm           informazioni, anche su "?"
//   S,ms,slot,mode,rx_persi_in_coda          A: inizio slot
//   D,ms,seq,mode,rssi,rate                  B: pacchetto dati ricevuto
//   E,ms,seq,mode,delay_us,offset_us,rssiB,rssiA   A: eco ricevuta
//   T,ms,id,try,mode,rssi                    B: tentativo di t0 ricevuto
//   K,ms,id,mode,tries,delay_us              A: t0 confermato
//   X,ms,id,mode,tries                       A: t0 mai confermato nello slot
//   W,ms,stazioni,richieste_5s               B con AP: traffico del telefono
//   #,testo                                  commenti

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_now.h>

// ---------------------------------------------------------------- config ---

static const uint8_t  CHANNEL       = 1;
static const int8_t   TX_QDBM       = 80;     // 20 dBm, come il 22/09
static const uint32_t PKT_MS        = 50;
static const uint32_t PKTS_PER_SLOT = 100;    // 100 x 50 ms = 5 s per modo
static const uint32_t RETRY_MS      = 250;    // t0 ripetuto finche' confermato
static const int      LED_PIN       = 8;      // LED blu della SuperMini, attivo basso

static const char* AP_SSID = "PROSTART-LR";
static const char* AP_PASS = "prostart123";

enum : uint8_t { M_LR250, M_LR500, M_11B, NMODES };

// ---------------------------------------------------------------- pacchetto

static const uint32_t MAGIC = 0x524C5350;     // "PSLR"

struct __attribute__((packed)) Pkt {
  uint32_t magic;
  char     type;      // D dati, E eco, T t0, K conferma t0
  uint8_t  mode;
  uint16_t tries;
  uint32_t seq;       // D/E: numero di sequenza; T/K: id dello slot
  int64_t  tA;        // A: istante di invio
  int64_t  tBrx;      // B: istante di ricezione (E/K)
  int64_t  tBtx;      // B: istante di invio della risposta (E/K)
  int8_t   rssiB;     // RSSI con cui B ha ricevuto (E/K)
};

// Quello che arriva dalla radio. La callback gira nel task Wi-Fi: prende solo
// l'istante e i metadati e passa tutto al loop, dove si stampa e si risponde.
struct Ev { Pkt p; int64_t tRx; int8_t rssi; uint8_t rate; };

// ---------------------------------------------------------------- stato ----

static uint8_t       BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static QueueHandle_t rxq;
static volatile uint32_t rxDropped = 0;
static Preferences   prefs;
static WebServer     http(80);

static char    role = '?';
static bool    apOn = false;
static uint8_t curMode = 255;

static inline int64_t nowUs() { return esp_timer_get_time(); }

// ---------------------------------------------------------------- radio ----

// Il modo di trasmissione vale per il peer broadcast; la ricezione resta
// aperta a tutti e tre perche' il protocollo include b/g/n e LR insieme.
static void setMode(uint8_t m) {
  if (m == curMode) return;
  esp_now_rate_config_t rc = {};
  switch (m) {
    case M_LR250: rc.phymode = WIFI_PHY_MODE_LR;  rc.rate = WIFI_PHY_RATE_LORA_250K; break;
    case M_LR500: rc.phymode = WIFI_PHY_MODE_LR;  rc.rate = WIFI_PHY_RATE_LORA_500K; break;
    default:      rc.phymode = WIFI_PHY_MODE_11B; rc.rate = WIFI_PHY_RATE_1M_L;      break;
  }
  esp_err_t e = esp_now_set_peer_rate_config(BCAST, &rc);
  if (e != ESP_OK) Serial.printf("#,ERRORE modo %u: %s\n", m, esp_err_to_name(e));
  curMode = m;
}

static void send(Pkt& p) {
  p.magic = MAGIC;
  esp_now_send(BCAST, (const uint8_t*)&p, sizeof(p));
}

static void onRecv(const esp_now_recv_info_t* info, const uint8_t* d, int len) {
  if (len != (int)sizeof(Pkt)) return;
  Ev e;
  e.tRx = nowUs();
  memcpy(&e.p, d, sizeof(Pkt));
  if (e.p.magic != MAGIC) return;
  e.rssi = info->rx_ctrl->rssi;
  e.rate = info->rx_ctrl->rate;
  if (xQueueSend(rxq, &e, 0) != pdTRUE) rxDropped++;
}

static void blink() { digitalWrite(LED_PIN, LOW); }

// ---------------------------------------------------------------- A --------

static int64_t  aStart;
static uint32_t aSeq = 0;
struct { bool pending; uint32_t id; uint8_t mode; uint16_t tries; int64_t next; } t0 = {};

static void loopA() {
  int64_t now = nowUs();

  // Pacchetti a orario fisso: seq n parte a aStart + n*50 ms, cosi' il
  // numero di sequenza dice da solo in che modo e' stato trasmesso.
  while (now >= aStart + (int64_t)aSeq * PKT_MS * 1000) {
    uint32_t slot = aSeq / PKTS_PER_SLOT;
    uint8_t  m    = slot % NMODES;
    if (aSeq % PKTS_PER_SLOT == 0) {
      if (t0.pending)
        Serial.printf("X,%lu,%lu,%u,%u\n", millis(), (unsigned long)t0.id, t0.mode, t0.tries);
      setMode(m);
      Serial.printf("S,%lu,%lu,%u,%lu\n", millis(), (unsigned long)slot, m, (unsigned long)rxDropped);
      t0 = {true, slot, m, 0, now};
    }
    Pkt p = {};
    p.type = 'D'; p.mode = m; p.seq = aSeq; p.tA = now;
    send(p);
    aSeq++;
  }

  if (t0.pending && now >= t0.next) {
    Pkt p = {};
    p.type = 'T'; p.mode = t0.mode; p.seq = t0.id; p.tries = ++t0.tries; p.tA = now;
    send(p);
    t0.next += RETRY_MS * 1000;
  }

  Ev e;
  while (xQueueReceive(rxq, &e, 0) == pdTRUE) {
    const Pkt& p = e.p;
    // offset = orologio B - orologio A; delay = andata+ritorno senza il
    // tempo passato dentro B
    int64_t delay  = (e.tRx - p.tA) - (p.tBtx - p.tBrx);
    int64_t offset = ((p.tBrx - p.tA) + (p.tBtx - e.tRx)) / 2;
    if (p.type == 'E') {
      blink();
      Serial.printf("E,%lu,%lu,%u,%lld,%lld,%d,%d\n", millis(), (unsigned long)p.seq, p.mode,
                    delay, offset, p.rssiB, e.rssi);
    } else if (p.type == 'K' && t0.pending && p.seq == t0.id) {
      t0.pending = false;
      Serial.printf("K,%lu,%lu,%u,%u,%lld\n", millis(), (unsigned long)p.seq, p.mode, p.tries, delay);
    }
  }
}

// ---------------------------------------------------------------- B --------

static uint32_t httpReqs = 0;
static uint32_t lastW = 0;

static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Prostart LR</title></head>
<body style="background:#000;color:#fff;font:20px system-ui;padding:16px">
<h3>Prostart LR - traffico telefono</h3><div id="o">...</div>
<script>
let ok=0,ko=0;
async function go(){ try{ const r=await fetch('/p',{cache:'no-store'});
  document.getElementById('o').textContent=(await r.text())+'  |  richieste ok '+(++ok)+', fallite '+ko;
 }catch(e){ko++;} setTimeout(go,100); }
go();
</script></body></html>)HTML";

static uint32_t bRx = 0;
static int8_t   bLastRssi = 0;

static void loopB() {
  Ev e;
  while (xQueueReceive(rxq, &e, 0) == pdTRUE) {
    Pkt r = e.p;
    if (r.type == 'D') {
      bRx++; bLastRssi = e.rssi;
      blink();
      Serial.printf("D,%lu,%lu,%u,%d,%u\n", millis(), (unsigned long)r.seq, r.mode, e.rssi, e.rate);
      r.type = 'E';
    } else if (r.type == 'T') {
      Serial.printf("T,%lu,%lu,%u,%u,%d\n", millis(), (unsigned long)r.seq, r.tries, r.mode, e.rssi);
      r.type = 'K';
    } else continue;
    // si risponde nello stesso modo in cui si e' ricevuto
    setMode(r.mode);
    r.tBrx = e.tRx; r.rssiB = e.rssi; r.tBtx = nowUs();
    send(r);
  }

  if (apOn) {
    http.handleClient();
    if (millis() - lastW >= 5000) {
      lastW = millis();
      Serial.printf("W,%lu,%d,%lu\n", lastW, WiFi.softAPgetStationNum(), (unsigned long)httpReqs);
      httpReqs = 0;
    }
  }
}

// ---------------------------------------------------------------- comandi --

static void info() {
  int8_t q = 0;
  esp_wifi_get_max_tx_power(&q);
  Serial.printf("I,%c,%s,%u,%s,%.1f\n", role, WiFi.macAddress().c_str(), CHANNEL,
                apOn ? "ap" : "noap", q / 4.0);
}

static void command(String c) {
  c.trim();
  if (c == "?") { info(); return; }
  if (c == "role A" || c == "role B") {
    prefs.putChar("role", c[5]);
    Serial.printf("#,ruolo -> %c, riavvio\n", c[5]);
    delay(100); ESP.restart();
  }
  if (c == "ap on" || c == "ap off") {
    prefs.putBool("ap", c == "ap on");
    Serial.printf("#,%s, riavvio\n", c.c_str());
    delay(100); ESP.restart();
  }
  Serial.printf("#,comando sconosciuto: %s\n", c.c_str());
}

// ---------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  // Se nessuno legge la seriale, le stampe non devono bloccare la radio.
  Serial.setTxTimeoutMs(0);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  prefs.begin("lrtest");
  role = prefs.getChar("role", '?');
  apOn = prefs.getBool("ap", false) && role == 'B';

  rxq = xQueueCreate(64, sizeof(Ev));

  WiFi.mode(apOn ? WIFI_AP_STA : WIFI_STA);
  if (apOn) {
    WiFi.softAP(AP_SSID, AP_PASS, CHANNEL);
    // il telefono non sa parlare LR: l'AP resta b/g/n
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    http.on("/", [] { http.send_P(200, "text/html", PAGE); });
    http.on("/p", [] {
      httpReqs++;
      http.send(200, "text/plain", "B riceve da A: " + String(bRx) + " pkt, " + String(bLastRssi) + " dBm");
    });
    http.begin();
  }
  esp_wifi_set_protocol(WIFI_IF_STA,
      WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(TX_QDBM);

  if (esp_now_init() != ESP_OK) Serial.println("#,ERRORE esp_now_init");
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = CHANNEL;
  peer.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("#,ERRORE add_peer");
  setMode(M_LR250);

  aStart = nowUs();
  delay(300);
  Serial.println("#,EspNowLrTest pronto");
  info();
}

void loop() {
  static String line;
  static uint32_t lastHint = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { command(line); line = ""; }
    else if (ch != '\r') line += ch;
  }

  static uint32_t ledOn = 0;
  if (digitalRead(LED_PIN) == LOW) {
    if (!ledOn) ledOn = millis();
    else if (millis() - ledOn > 10) { digitalWrite(LED_PIN, HIGH); ledOn = 0; }
  }

  if (role == 'A') loopA();
  else if (role == 'B') loopB();
  else if (millis() - lastHint > 2000) {
    lastHint = millis();
    Serial.println("#,ruolo non impostato: avvia logger.py A oppure logger.py B");
  }
}
