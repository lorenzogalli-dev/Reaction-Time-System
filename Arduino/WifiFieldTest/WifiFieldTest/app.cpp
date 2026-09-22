#include <Arduino.h>
// WifiFieldTest - banco di prova sul campo per il link Wi-Fi Prostart.
//
// Hardware: solo ESP32-C3 SuperMini, alimentato via USB-C dal Mac. Lo XIAO non
// serve: il t0 qui e' simulato a bordo. L'ESP resta fermo col Mac, tu cammini
// col telefono; il log che conta vive sul telefono.
//
// Misura in una sola passeggiata:
//   - portata utile (RSSI + RTT + pacchetti persi + riconnessioni, per distanza)
//   - invasivita' della connessione (tre modi di rispondere alle sonde captive)
//   - se il telefono si sgancia perche' la rete non ha internet
//   - sincronizzazione del clock ESP <-> telefono (stile NTP, min-delay)
//   - latenza di consegna di un t0 spinto dall'ESP al telefono
//
// Uso: connetti il telefono, apri http://192.168.4.1 (modo AP) e cammina.

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>

// ---------------------------------------------------------------- config ---

// 1 = l'ESP fa da access point e il telefono si connette a lui (nessuna
//     infrastruttura in pista, ma e' il caso peggiore per l'UX)
// 0 = l'ESP si connette all'hotspot del telefono (UX a costo zero, il telefono
//     tiene internet sul cellulare, ma la portata la detta l'AP del telefono)
#define MODE_AP 1

static const char* AP_SSID  = "PROSTART-TEST";
static const char* AP_PASS  = "prostart123";   // >= 8 caratteri

static const char* STA_SSID = "METTI-QUI-HOTSPOT";
static const char* STA_PASS = "METTI-QUI-PASSWORD";

static const uint32_t GO_PERIOD_MS = 5000;     // un t0 simulato ogni 5 s

// Il t0 porta il proprio timestamp, quindi arrivare tardi non costa niente:
// quello che conta e' arrivare. Nell'istante del t0 l'atleta e' accovacciato
// sui blocchi, proprio sulla linea di vista verso l'arrivo, e un corpo umano
// a distanza ravvicinata vale 10-20 dB. Invece di combattere quei dB, il t0
// viene ripetuto finche' il telefono non lo conferma: l'atleta parte, la
// linea si libera, e il messaggio arriva mezzo secondo dopo con l'ora giusta.
static const uint32_t RETRY_MS  = 250;
static const uint16_t MAX_TRIES = 60;          // 15 s, poi lo do per perso

// Solo 802.11b: si rinuncia alle modulazioni veloci, che qui non servono
// (poche decine di byte ogni cinque secondi) e si guadagnano alcuni dB di
// sensibilita'. Se un telefono si rifiutasse di associarsi, c'e' il rientro
// automatico piu' sotto.
#define ONLY_11B 1
static const uint32_t FALLBACK_MS = 90000;     // nessun client entro 90 s -> bgn

// ---------------------------------------------------------------- stato ----

WebServer        http(80);
WebSocketsServer ws(81);
DNSServer        dns;

// Come rispondere alle sonde con cui il telefono decide "c'e' internet?".
//   0 NONE   nessuna risposta -> Android avvisa "connesso, nessun internet"
//   1 PORTAL redirect alla pagina -> l'OS apre da solo il portale, ma sa che
//            internet non c'e'. Attenzione: quella finestrella e' un browser
//            ridotto, e il sistema la CHIUDE quando la rete cade, portandosi
//            via il log. Va bene per provare l'UX, non per raccogliere dati.
//   2 LIE    204 vuoto -> l'OS crede che internet ci sia: nessun avviso, ma
//            rischia di instradare da noi tutto il traffico del telefono
volatile uint8_t captiveMode = 0;
static const char* CAPTIVE_NAMES[] = {"NONE", "PORTAL", "LIE"};

// I t0 ancora da confermare. Con un solo telefono basta un flag; con piu'
// telefoni servirebbe una conferma per client, qui non serve.
struct GoMsg { uint32_t seq; int64_t t0; uint16_t tries; bool used; };
GoMsg    pendGo[8] = {};
uint32_t lastRetry = 0;
bool     everConnected = false;
bool     fellBack = false;

uint32_t goSeq = 0;
uint32_t lastGo = 0;
uint32_t lastRssi = 0;

static inline int64_t nowUs() { return esp_timer_get_time(); }

// RSSI del link. In STA e' diretto; in AP va chiesto alla lista delle stazioni.
int linkRssi() {
#if MODE_AP
  wifi_sta_list_t sta;
  if (esp_wifi_ap_get_sta_list(&sta) == ESP_OK && sta.num > 0) return sta.sta[0].rssi;
  return 0;
#else
  return WiFi.RSSI();
#endif
}

// ---------------------------------------------------------------- pagina ---

static const char PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang="it"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Prostart field test</title><style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:12px;background:#000;color:#fff;
     font:16px/1.35 -apple-system,system-ui,sans-serif;-webkit-text-size-adjust:100%}
h1{font-size:15px;margin:0 0 10px;color:#888;font-weight:600;letter-spacing:.06em}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.c{background:#131313;border:1px solid #262626;border-radius:10px;padding:10px}
.k{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.07em}
.v{font-size:30px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1;margin-top:3px}
.u{font-size:13px;color:#777;font-weight:400}
.wide{grid-column:1/-1}
.ok{color:#3ddc84}.warn{color:#ffc53d}.bad{color:#ff5a5a}
button{width:100%;padding:16px;font-size:17px;font-weight:600;border:0;border-radius:10px;
       background:#1f6feb;color:#fff;margin-top:8px;-webkit-tap-highlight-color:transparent}
button.sec{background:#262626}
.row{display:flex;gap:8px}.row>*{flex:1}
#log{height:150px;overflow:auto;background:#0b0b0b;border:1px solid #262626;border-radius:10px;
     padding:8px;font:11px/1.45 ui-monospace,Menlo,monospace;color:#9a9a9a;margin-top:8px;
     white-space:pre-wrap}
</style></head><body>
<h1>PROSTART - TEST DI CAMPO</h1>
<div class="grid">
  <div class="c"><div class="k">Distanza</div><div class="v" id="dist">0<span class="u"> m</span></div></div>
  <div class="c"><div class="k">RSSI</div><div class="v" id="rssi">--<span class="u"> dBm</span></div></div>
  <div class="c"><div class="k">RTT mediana</div><div class="v" id="rtt">--<span class="u"> ms</span></div></div>
  <div class="c"><div class="k">RTT p95</div><div class="v" id="p95">--<span class="u"> ms</span></div></div>
  <div class="c"><div class="k">Persi</div><div class="v" id="loss">0<span class="u"> %</span></div></div>
  <div class="c"><div class="k">Riconnessioni</div><div class="v" id="drops">0</div></div>
  <div class="c"><div class="k">t0: latenza</div><div class="v" id="golat">--<span class="u"> ms</span></div></div>
  <div class="c"><div class="k">t0: ricevuti</div><div class="v" id="gocnt">0<span class="u"> / 0</span></div></div>
  <div class="c wide"><div class="k">Stato link</div><div class="v" id="state">...</div></div>
  <div class="c"><div class="k">t0: tentativi</div><div class="v" id="gotri">-- / --</div></div>
  <div class="c"><div class="k">Log</div><div class="v" id="nrows">0<span class="u"> righe</span></div></div>
  <div class="c"><div class="k">Salvato</div><div class="v ok" id="saved">--</div></div>
  <div class="c wide"><div class="k">Offset clock (min-delay)</div>
      <div class="v" id="off">--<span class="u"> ms &plusmn; <span id="offj">--</span></span></div></div>
</div>
<button id="mark">SEGNA +10 m</button>
<div class="row">
  <button class="sec" id="back">-10 m</button>
  <button class="sec" id="cap">Captive: ...</button>
</div>
<div class="row">
  <button class="sec" id="dl">Scarica CSV</button>
  <button class="sec" id="rst">Azzera</button>
</div>
<div id="log"></div>
<script>
const $=i=>document.getElementById(i);
const HOST=location.hostname, T0=performance.timeOrigin;
const now=()=>T0+performance.now();          // ms float, monotona
let sock,offset=0,jit=0,dist=0,drops=0,sent=0,got=0,goExp=0,goGot=0,lastSeq=-1;
let rtts=[],samples=[],rssi=0,rows=[],connected=false;

// Ogni ping resta qui finche' non torna. Senza questo il WebSocket accoda in
// silenzio su un link morto: le risposte arrivano tutte insieme molto dopo e
// i round trip risultano di decine di secondi, che non e' latenza ma coda.
// Un ping senza risposta entro 3 s e' perso, e come perso va a log.
let pend=new Map(),lost=0,lostRun=0;
let seen=new Set(),maxTri=1;

// Il telefono puo' chiudere la scheda quando la rete cade, e in ogni caso
// ricaricare la pagina azzererebbe tutto. Il log vive quindi sul telefono e
// viene ripreso all'apertura: e' l'unica copia dei dati, l'ESP non registra
// niente.
const LS='prostart-fieldtest';
function save(){try{localStorage.setItem(LS,JSON.stringify(
  {rows:rows.slice(-5000),dist:dist,drops:drops,sent:sent,got:got,
   goExp:goExp,goGot:goGot,lastSeq:lastSeq,lost:lost}));
  $('saved').textContent=new Date().toTimeString().substr(0,8);}catch(e){
  $('saved').textContent='PIENO';$('saved').className='v bad';}}
function restore(){try{const d=JSON.parse(localStorage.getItem(LS)||'null');
  if(!d||!d.rows||!d.rows.length)return;
  rows=d.rows;dist=d.dist|0;drops=d.drops|0;sent=d.sent|0;got=d.got|0;
  goExp=d.goExp|0;goGot=d.goGot|0;lost=d.lost|0;
  lastSeq=d.lastSeq===undefined?-1:d.lastSeq;
  log('ripreso: '+rows.length+' righe, '+dist+' m');}catch(e){}}

function log(s){const d=new Date().toISOString().substr(11,12);
  $('log').textContent=d+'  '+s+'\n'+$('log').textContent.substr(0,4000);}
function rec(ev,o){rows.push(Object.assign({iso:new Date().toISOString(),ms:now().toFixed(1),
  ev:ev,dist:dist,rssi:rssi,off:offset.toFixed(3),jit:jit.toFixed(2),
  sent:sent,got:got,lost:lost},o||{}));}
const med=a=>{if(!a.length)return NaN;const s=[...a].sort((x,y)=>x-y);return s[s.length>>1];};
const pct=(a,p)=>{if(!a.length)return NaN;const s=[...a].sort((x,y)=>x-y);
  return s[Math.min(s.length-1,Math.floor(s.length*p))];};

function paint(){
  $('dist').innerHTML=dist+'<span class="u"> m</span>';
  const r=med(rtts),q=pct(rtts,.95);
  $('rtt').innerHTML=(isNaN(r)?'--':r.toFixed(1))+'<span class="u"> ms</span>';
  $('p95').innerHTML=(isNaN(q)?'--':q.toFixed(1))+'<span class="u"> ms</span>';
  const rs=$('rssi');rs.innerHTML=(rssi||'--')+'<span class="u"> dBm</span>';
  rs.className='v '+(rssi>-67?'ok':rssi>-80?'warn':'bad');
  const l=sent?100*lost/sent:0;
  const le=$('loss');le.innerHTML=l.toFixed(1)+'<span class="u"> %</span>';
  le.className='v '+(l<1?'ok':l<10?'warn':'bad');
  $('drops').textContent=drops;
  $('nrows').innerHTML=rows.length+'<span class="u"> righe</span>';
  $('gocnt').innerHTML=goGot+'<span class="u"> / '+goExp+'</span>';
  $('off').innerHTML=offset.toFixed(2)+'<span class="u"> ms &plusmn; '+jit.toFixed(2)+'</span>';
  const st=$('state');st.textContent=connected?'CONNESSO':'CADUTO';
  st.className='v '+(connected?'ok':'bad');
}

// Un campione di sincronizzazione vale quanto e' corto il suo round trip:
// tengo l'offset del round trip piu' rapido degli ultimi 60.
function sync(t1,tsrv,t4){
  const rtt=t4-t1;rtts.push(rtt);if(rtts.length>120)rtts.shift();
  samples.push({rtt:rtt,off:tsrv-(t1+t4)/2});if(samples.length>60)samples.shift();
  let b=samples[0];for(const s of samples)if(s.rtt<b.rtt)b=s;
  offset=b.off;jit=b.rtt/2;
  rec('ping',{rtt:rtt.toFixed(2)});
}

function open_(){
  sock=new WebSocket('ws://'+HOST+':81/');
  sock.onopen=()=>{connected=true;log('link aperto');rec('open');paint();};
  sock.onclose=()=>{if(connected){drops++;log('LINK CADUTO');rec('drop');}
    connected=false;paint();setTimeout(open_,700);};
  sock.onerror=()=>{};
  sock.onmessage=e=>{
    const p=e.data.split(' ');
    if(p[0]==='Q'){got++;lostRun=0;pend.delete(+p[1]);
      if(p[3]!==undefined)rssi=+p[3];
      sync(+p[1],+p[2]/1000,now());}
    else if(p[0]==='R'){rssi=+p[1];}
    else if(p[0]==='G'){
      const seq=+p[1],t0=+p[2]/1000,tri=p[3]?+p[3]:1;
      sock.send('A '+seq);                        // conferma sempre, anche i doppi
      if(seen.has(seq))return;                    // ritrasmissione gia' registrata
      seen.add(seq);
      const lat=now()-(t0-offset);                // arrivo - t0, stesso clock
      goGot++;if(seq>goExp)goExp=seq;
      if(lastSeq>=0&&seq>lastSeq+1)log('t0 persi: '+(seq-lastSeq-1));
      lastSeq=seq;maxTri=Math.max(maxTri,tri);
      $('golat').innerHTML=lat.toFixed(1)+'<span class="u"> ms</span>';
      $('gotri').textContent=tri+' / '+maxTri;
      log('t0 #'+seq+'  '+lat.toFixed(1)+' ms, tentativo '+tri);
      rec('go',{seq:seq,golat:lat.toFixed(2),tries:tri});
    }
    else if(p[0]==='C'){$('cap').textContent='Captive: '+p[1];}
    paint();
  };
}

setInterval(()=>{
  if(connected&&sock.readyState===1){const t1=now();pend.set(t1,t1);sent++;sock.send('P '+t1);}
  const lim=now()-3000;
  for(const [k,v] of pend) if(v<lim){
    pend.delete(k);lost++;lostRun++;rec('lost');
    // Sei di fila senza risposta: il socket e' zombie. Lo chiudo io, cosi'
    // la riconnessione viene contata invece di restare appesa.
    if(lostRun>=6&&sock&&sock.readyState===1){log('link bloccato, riapro');sock.close();}
  }
},250);
setInterval(paint,1000);
setInterval(save,2000);
addEventListener('pagehide',save);
document.addEventListener('visibilitychange',()=>{if(document.hidden)save();});

$('mark').onclick=()=>{dist+=10;log('=== '+dist+' m ===');rec('mark');paint();};
$('back').onclick=()=>{dist=Math.max(0,dist-10);rec('mark');paint();};
$('cap').onclick=()=>{if(connected)sock.send('C');};
$('rst').onclick=()=>{if(!confirm('Cancellare il log?'))return;
  rows=[];dist=0;drops=0;sent=0;got=0;lost=0;lostRun=0;pend.clear();
  goExp=0;goGot=0;lastSeq=-1;rtts=[];samples=[];seen.clear();maxTri=1;
  try{localStorage.removeItem(LS);}catch(e){}
  $('log').textContent='';paint();};
$('dl').onclick=()=>{
  const k=['iso','ms','ev','dist','rssi','off','jit','rtt','sent','got','lost','seq','golat','tries'];
  const csv=k.join(',')+'\n'+rows.map(r=>k.map(x=>r[x]===undefined?'':r[x]).join(',')).join('\n');
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));
  a.download='fieldtest_'+Date.now()+'.csv';a.click();
};

// Senza questo lo schermo si spegne mentre cammini e il test si ferma.
async function wake(){try{await navigator.wakeLock.request('screen');}catch(e){}}
wake();document.addEventListener('visibilitychange',()=>{if(!document.hidden)wake();});
restore();open_();paint();
</script></body></html>
)HTMLPAGE";

// ------------------------------------------------------------ websocket ----

void onWs(uint8_t n, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    everConnected = true;
    Serial.printf("[ws] client %u connesso\n", n);
    ws.sendTXT(n, String("C ") + CAPTIVE_NAMES[captiveMode]);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[ws] client %u caduto\n", n);
  } else if (type == WStype_TEXT) {
    if (payload[0] == 'P') {
      // Rispondo subito, prima di qualunque altra cosa: ogni microsecondo
      // speso qui entra nella stima dell'offset come se fosse rete.
      String r = "Q ";
      r += (char*)payload + 2;
      r += ' ';
      r += String((long long)nowUs());
      r += ' ';
      r += String(linkRssi());     // viaggia con la risposta: sempre fresco
      ws.sendTXT(n, r);
    } else if (payload[0] == 'A') {
      uint32_t seq = strtoul((char*)payload + 2, nullptr, 10);
      for (uint8_t i = 0; i < 8; i++)
        if (pendGo[i].used && pendGo[i].seq == seq) {
          Serial.printf("[go ] #%lu confermato al tentativo %u\n",
                        (unsigned long)seq, pendGo[i].tries);
          pendGo[i].used = false;
        }
    } else if (payload[0] == 'C') {
      captiveMode = (captiveMode + 1) % 3;
      ws.broadcastTXT(String("C ") + CAPTIVE_NAMES[captiveMode]);
      Serial.printf("[cap] modo -> %s\n", CAPTIVE_NAMES[captiveMode]);
    }
  }
}

void sendGo(GoMsg& g) {
  ws.broadcastTXT("G " + String(g.seq) + " " + String((long long)g.t0) + " " +
                  String(g.tries));
}

void fireGo() {
  goSeq++;
  int8_t slot = -1;
  for (uint8_t i = 0; i < 8; i++) if (!pendGo[i].used) { slot = i; break; }
  if (slot < 0) {                       // coda piena: il piu' vecchio decade
    slot = 0;
    Serial.printf("[go ] #%lu scartato, coda piena\n",
                  (unsigned long)pendGo[0].seq);
  }
  pendGo[slot] = { goSeq, nowUs(), 1, true };
  sendGo(pendGo[slot]);
  Serial.printf("[go ] #%lu  rssi %d\n", (unsigned long)goSeq, linkRssi());
}

void retryGo() {
  for (uint8_t i = 0; i < 8; i++) {
    GoMsg& g = pendGo[i];
    if (!g.used) continue;
    if (++g.tries > MAX_TRIES) {
      g.used = false;
      Serial.printf("[go ] #%lu MAI confermato dopo %u tentativi\n",
                    (unsigned long)g.seq, MAX_TRIES);
      continue;
    }
    sendGo(g);
  }
}

// ------------------------------------------------------------- captive -----

void captiveProbe() {
  if (captiveMode == 2) {                 // LIE: "internet c'e'"
    http.send(204, "text/plain", "");
  } else if (captiveMode == 1) {          // PORTAL: apri la pagina
    http.sendHeader("Location", "http://192.168.4.1/", true);
    http.send(302, "text/plain", "");
  } else {                                // NONE: lascia decidere all'OS
    http.send(404, "text/plain", "");
  }
}

// ---------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== WifiFieldTest ===");

#if MODE_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  esp_wifi_set_max_tx_power(80);          // 20 dBm, il massimo consentito
#if ONLY_11B
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B);
  Serial.println("radio: solo 802.11b");
#endif
  Serial.printf("AP  \"%s\"  pass \"%s\"  ->  http://%s/\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  dns.start(53, "*", WiFi.softAPIP());    // fa aprire il portale da solo
#else
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                   // il modem sleep aggiunge decine di ms
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.printf("connessione a \"%s\"", STA_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  esp_wifi_set_max_tx_power(80);
  Serial.printf("\nconnesso  ->  http://%s/\n", WiFi.localIP().toString().c_str());
#endif

  http.on("/", []() { http.send_P(200, "text/html", PAGE); });
  for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html",
                        "/connecttest.txt", "/ncsi.txt", "/success.txt"})
    http.on(p, captiveProbe);
  http.onNotFound(captiveProbe);
  http.begin();

  ws.begin();
  ws.onEvent(onWs);

  Serial.println("pronto. 'g' + invio spara un t0 a mano.");
}

void loop() {
  dns.processNextRequest();
  http.handleClient();
  ws.loop();

  if (millis() - lastGo >= GO_PERIOD_MS) { lastGo = millis(); fireGo(); }
  if (millis() - lastRetry >= RETRY_MS) { lastRetry = millis(); retryGo(); }

#if ONLY_11B
  // Se nessun telefono si e' associato entro 90 s il sospetto e' che sia
  // proprio l'11b a bloccarlo: meglio una rete lenta che nessuna rete, e
  // saresti in mezzo al campo senza modo di riflashare.
  if (!everConnected && !fellBack && millis() > FALLBACK_MS) {
    fellBack = true;
    esp_wifi_set_protocol(WIFI_IF_AP,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    Serial.println("nessun client in 90 s: rientro a 802.11 b/g/n");
  }
#endif

  // Anche a link fermo, cosi' la pagina ha un RSSI recente da mettere a log.
  if (millis() - lastRssi >= 1000) {
    lastRssi = millis();
    ws.broadcastTXT("R " + String(linkRssi()));
  }
  if (Serial.available() && Serial.read() == 'g') fireGo();
}
