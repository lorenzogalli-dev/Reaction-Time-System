// ---------------------------------------------------------------------------
// Xbee_RangeTest - start/finish link range and reliability characterisation
// XIAO nRF52840 Sense + XBee / XBee-PRO S2C (XB24CZ7PIT-004), 2.4 GHz Zigbee
// ---------------------------------------------------------------------------
//
// WHAT THIS IS
// -----------
// A standalone bring-up/characterisation tool, not product firmware. Flash it
// to two identical XIAO + XBee units - one SENDER (fixed, at the start line),
// one RECEIVER (carried away from it) - and it measures, per distance point:
//   - uplink delivery ratio and MAC retry count (start -> finish direction)
//   - round-trip time distribution (min / median / p95 / jitter)
//   - RSSI at the receiver
// so the walk test produces PDR-vs-distance and RTT-vs-distance data.
//
// The production Prostart_StartBox / Prostart_FinishBox firmware talks to the
// XBee in TRANSPARENT mode. This sketch uses API mode instead, only because
// API mode is the only way to get the 0x8B transmit-status frame (delivery
// result + retry count) and the per-node RSSI. Nothing here changes the
// production transport decision.
//
// WHY API mode and NOT broadcast
// ------------------------------
// Zigbee broadcasts carry no MAC acknowledgement and no retries, so a
// broadcast link produces no delivery-status or retry data - exactly the
// numbers this test exists to collect. So the SENDER discovers the peer's
// 64-bit address (first few pings go out broadcast, the RECEIVER echoes, the
// SENDER latches the source address of that echo) and then sends unicast.
// Rows logged before the peer is known are marked unicast=0 - drop them in
// analysis.
//
// XCTU CONFIG (do this once, both modules, before anything else)
//   Firmware:   XB24C (Z7) family - ZIGBEE, API mode.  *** not XBee3 ***
//               SENDER module -> "Zigbee Coordinator API"
//               RECEIVER module -> "Zigbee Router API"
//   ID (PAN ID) same nonzero value on both (e.g. 0x2B2B)
//   SC (scan channels) left at default is fine; note the joined CH in the log
//   AP = 1  (API enabled, NOT escaped - this parser does not unescape)
//   AO = 0  (plain 0x90 receive frames, not explicit 0x91)
//   BD = 3  (9600, the factory default this sketch assumes) - see XBEE_BAUD
//   Both modules MUST be the same S2C family/firmware stack, or they will not
//   pair.
//
// WIRING (XBee is 3.3 V - the XIAO is native 3.3 V, so no level shifter)
//   XBee VCC (pin 1)  -> XIAO 3V3
//   XBee GND (pin 10) -> XIAO GND
//   XBee DOUT (pin 2) -> XIAO D7  (Serial1 RX)
//   XBee DIN  (pin 3) -> XIAO D6  (Serial1 TX)
//   (same D6/D7 map as Arduino/Reaction_HardwareTest)
//
// OUTPUT (USB serial, one row per packet; a host logger adds wall-clock time)
//   SENDER:   S,<seq>,<unicast>,<delivery>,<retries>,<echo_ok>,<rtt_us>,<rssi_remote_dbm>
//             delivery/retries = -1 until the 0x8B status arrives
//             echo_ok = 0 and rtt_us = -1 on a round-trip loss
//   RECEIVER: R,<seq>,<rssi_dbm>,<dt_us>,<seq_gap>
//             dt_us = micros() since the previous received ping (clean one-way
//             inter-arrival jitter, independent of the two clocks' offset)
//   Lines starting with '#' are human-readable banners/diagnostics.
//
// SERIAL COMMANDS (either role): 'r' reset counters (use at each new distance
// point), 't' print a summary.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// === EDIT THIS before flashing each board ================================
#define ROLE_SENDER   0
#define ROLE_RECEIVER 1
#ifndef RANGE_TEST_ROLE
#define RANGE_TEST_ROLE  ROLE_SENDER   // set the other board to ROLE_RECEIVER
#endif
// ========================================================================

// Must match XCTU's BD parameter on both modules. 9600 (BD=3) is the factory
// default and is plenty for ~10 Hz of 30-byte frames; raise both to 115200
// (BD=7) if you extend the payload or the rate.
static const uint32_t XBEE_BAUD = 9600;

// Ping cadence and the window the SENDER waits for an echo before it logs the
// packet as a round-trip loss.
static const uint32_t PING_PERIOD_MS   = 100;   // 10 Hz
static const uint32_t ECHO_TIMEOUT_MS  = 400;
// RECEIVER: how long to wait for the ATDB (RSSI) reply before logging the row
// with rssi = 0 and echoing anyway. The reply is normally ~1-2 ms.
static const uint32_t DB_TIMEOUT_MS    = 40;

// ---------------------------------------------------------------------------
// API frame plumbing (AP=1, unescaped)
//   0x7E | len_hi | len_lo | frame_data[len] | checksum
//   checksum = 0xFF - (sum(frame_data) & 0xFF)
// ---------------------------------------------------------------------------

static const uint8_t ADDR_BROADCAST[8] = {0,0,0,0,0,0,0xFF,0xFF};
static const uint16_t ADDR16_UNKNOWN   = 0xFFFE;

static void apiSend(const uint8_t* frame, uint16_t len) {
  uint8_t sum = 0;
  Serial1.write((uint8_t)0x7E);
  Serial1.write((uint8_t)((len >> 8) & 0xFF));
  Serial1.write((uint8_t)(len & 0xFF));
  for (uint16_t i = 0; i < len; i++) {
    Serial1.write(frame[i]);
    sum += frame[i];
  }
  Serial1.write((uint8_t)(0xFF - sum));
}

// 0x10 ZigBee Transmit Request
static void apiTransmit(const uint8_t addr64[8], uint16_t addr16,
                        uint8_t frameId, const uint8_t* payload, uint8_t plen) {
  uint8_t f[14 + 64];
  f[0] = 0x10;
  f[1] = frameId;                       // 0 = suppress 0x8B status
  memcpy(&f[2], addr64, 8);
  f[10] = (addr16 >> 8) & 0xFF;
  f[11] = addr16 & 0xFF;
  f[12] = 0x00;                         // broadcast radius (0 = max)
  f[13] = 0x00;                         // options
  memcpy(&f[14], payload, plen);
  apiSend(f, 14 + plen);
}

// 0x08 Local AT Command Request (query form - no parameter bytes)
static void apiAtQuery(char c0, char c1, uint8_t frameId) {
  uint8_t f[4] = { 0x08, frameId, (uint8_t)c0, (uint8_t)c1 };
  apiSend(f, 4);
}

// Incremental parser: feed it one byte at a time, it calls handleFrame() on
// each complete, checksum-valid frame.
static uint8_t  pBuf[160];
static uint16_t pLen, pIdx;
static uint8_t  pSum;
static uint8_t  pState;   // 0 wait 0x7E, 1 len_hi, 2 len_lo, 3 payload, 4 checksum
static uint32_t pBadChecksum = 0, pOverrun = 0;

static void handleFrame(const uint8_t* f, uint16_t len);

static void apiFeed(uint8_t b) {
  switch (pState) {
    case 0:
      if (b == 0x7E) { pState = 1; }
      break;
    case 1:
      pLen = (uint16_t)b << 8;
      pState = 2;
      break;
    case 2:
      pLen |= b;
      pIdx = 0;
      pSum = 0;
      if (pLen == 0 || pLen > sizeof(pBuf)) { pOverrun++; pState = 0; }
      else { pState = 3; }
      break;
    case 3:
      pBuf[pIdx++] = b;
      pSum += b;
      if (pIdx >= pLen) pState = 4;
      break;
    case 4:
      if ((uint8_t)(0xFF - pSum) == b) handleFrame(pBuf, pLen);
      else pBadChecksum++;
      pState = 0;
      break;
  }
}

// ---------------------------------------------------------------------------
// Shared payload
// ---------------------------------------------------------------------------

struct __attribute__((packed)) RangePkt {
  uint8_t  magic;        // 0x4B 'K'
  uint8_t  kind;         // 0 = ping (S->R), 1 = echo (R->S)
  uint32_t seq;
  uint32_t t_tx_us;      // SENDER micros() when the ping was queued
  int8_t   rssi_remote;  // echo only: RECEIVER's ATDB result (negative dBm)
};
static const uint8_t MAGIC = 0x4B;

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

static void waitForSerial() {
  // Native USB: Serial only goes true when a host opens the port. Bounded wait
  // so a battery-powered board still runs. Same pattern as BLEtest.ino.
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
}

static void printRoleBanner() {
  Serial.println("# Xbee_RangeTest");
#if RANGE_TEST_ROLE == ROLE_SENDER
  Serial.println("# role: SENDER (fixed at the start line)");
#else
  Serial.println("# role: RECEIVER (carried; tether this one, or the sender, or both)");
#endif
  Serial.print("# xbee baud: "); Serial.println(XBEE_BAUD);
  Serial.print("# ping period ms: "); Serial.println(PING_PERIOD_MS);
  Serial.println("# commands: 'r' reset counters at a new distance point, 't' summary");
}

// ===========================================================================
#if RANGE_TEST_ROLE == ROLE_SENDER
// ===========================================================================

static uint8_t  peerAddr[8];
static bool     peerKnown = false;
static uint32_t seqCtr = 0;
static uint8_t  frameIdCtr = 0;
static uint32_t lastPingMs = 0;
static uint32_t lastPeerHintMs = 0;

// Recent unacked pings, matched up as 0x8B (by frame id) and echoes (by seq)
// come back, then logged once complete or timed out.
struct Sent {
  bool     used, emitted;
  uint32_t seq;
  uint8_t  frameId;
  uint32_t txUs, txMs;
  bool     unicast;
  bool     gotStatus;
  uint8_t  delivery, retries;
  bool     gotEcho;
  uint32_t rttUs;
  int8_t   rssiRemote;
};
static Sent ring[32];

// Explicit prototypes: the Arduino auto-prototype generator mangles `static`
// functions that take a struct by reference (it inserts the prototype above the
// struct definition). Same note as BLEtest.ino.
static Sent* ringAlloc();
static void  emit(Sent& e);
static void  resetStats();
static void  printSummary();

// Rolling stats since the last 'r'.
static uint32_t stSent = 0, stStatusOk = 0, stEchoOk = 0, stRetriesSum = 0;
static uint32_t stRttMin = 0xFFFFFFFF, stRttMax = 0, stRttCount = 0;
static uint64_t stRttSum = 0;

static Sent* ringAlloc() {
  for (auto& e : ring) if (!e.used) return &e;
  // No free slot: force-emit the oldest so logging never silently drops a row.
  Sent* oldest = &ring[0];
  for (auto& e : ring) if (e.used && e.txMs < oldest->txMs) oldest = &e;
  oldest->used = false;
  return oldest;
}

static void emit(Sent& e) {
  Serial.print("S,");
  Serial.print(e.seq);            Serial.print(',');
  Serial.print(e.unicast ? 1 : 0); Serial.print(',');
  Serial.print(e.gotStatus ? (int)e.delivery : -1); Serial.print(',');
  Serial.print(e.gotStatus ? (int)e.retries  : -1); Serial.print(',');
  Serial.print(e.gotEcho ? 1 : 0); Serial.print(',');
  Serial.print(e.gotEcho ? (long)e.rttUs : -1); Serial.print(',');
  Serial.println(e.gotEcho ? (int)e.rssiRemote : 0);

  stSent++;
  if (e.gotStatus && e.delivery == 0) stStatusOk++;
  if (e.gotStatus) stRetriesSum += e.retries;
  if (e.gotEcho) {
    stEchoOk++;
    stRttCount++;
    stRttSum += e.rttUs;
    if (e.rttUs < stRttMin) stRttMin = e.rttUs;
    if (e.rttUs > stRttMax) stRttMax = e.rttUs;
  }
  e.used = false;
  e.emitted = true;
}

static void resetStats() {
  stSent = stStatusOk = stEchoOk = stRetriesSum = 0;
  stRttMin = 0xFFFFFFFF; stRttMax = 0; stRttCount = 0; stRttSum = 0;
  Serial.println("# stats reset");
}

static void printSummary() {
  Serial.println("# --- sender summary since last reset ---");
  Serial.print("# logged        "); Serial.println(stSent);
  Serial.print("# uplink ok      "); Serial.print(stStatusOk);
  if (stSent) { Serial.print("  ("); Serial.print(100.0 * stStatusOk / stSent, 1); Serial.print("%)"); }
  Serial.println();
  Serial.print("# round-trip ok  "); Serial.print(stEchoOk);
  if (stSent) { Serial.print("  ("); Serial.print(100.0 * stEchoOk / stSent, 1); Serial.print("%)"); }
  Serial.println();
  if (stStatusOk) { Serial.print("# mean retries   "); Serial.println((double)stRetriesSum / stStatusOk, 2); }
  if (stRttCount) {
    Serial.print("# rtt us  min "); Serial.print(stRttMin);
    Serial.print("  mean ");        Serial.print((double)(stRttSum / stRttCount));
    Serial.print("  max ");         Serial.println(stRttMax);
    Serial.println("# (host log has the full RTT distribution / percentiles)");
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(XBEE_BAUD);
  waitForSerial();
  printRoleBanner();
  Serial.println("# discovering peer (broadcast pings) - if no rows appear,");
  Serial.println("# check XCTU: same PAN ID, one coordinator + one router, AP=1");
}

void loop() {
  while (Serial1.available()) apiFeed((uint8_t)Serial1.read());

  uint32_t now = millis();
  if (now - lastPingMs >= PING_PERIOD_MS) {
    lastPingMs = now;
    do { frameIdCtr++; } while (frameIdCtr == 0);

    RangePkt p;
    p.magic = MAGIC;
    p.kind = 0;
    p.seq = ++seqCtr;
    p.t_tx_us = micros();
    p.rssi_remote = 0;

    Sent* e = ringAlloc();
    e->used = true; e->emitted = false;
    e->seq = p.seq; e->frameId = frameIdCtr;
    e->txUs = p.t_tx_us; e->txMs = now;
    e->unicast = peerKnown;
    e->gotStatus = false; e->gotEcho = false;
    e->delivery = 0; e->retries = 0; e->rttUs = 0; e->rssiRemote = 0;

    apiTransmit(peerKnown ? peerAddr : ADDR_BROADCAST, ADDR16_UNKNOWN,
                frameIdCtr, (const uint8_t*)&p, sizeof(p));
  }

  // Flush completed or timed-out entries.
  for (auto& e : ring) {
    if (!e.used) continue;
    bool done = e.gotEcho && (e.gotStatus || !e.unicast);
    bool stale = (now - e.txMs) > ECHO_TIMEOUT_MS;
    if (done || stale) emit(e);
  }

  if (!peerKnown && now - lastPeerHintMs > 5000) {
    lastPeerHintMs = now;
    Serial.println("# still no echo from the peer - is the RECEIVER powered and joined?");
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') resetStats();
    else if (c == 't') printSummary();
  }
}

static void handleFrame(const uint8_t* f, uint16_t len) {
  if (f[0] == 0x8B && len >= 7) {                 // ZigBee Transmit Status
    uint8_t frameId = f[1];
    uint8_t retries = f[4];
    uint8_t delivery = f[5];
    for (auto& e : ring) {
      if (e.used && !e.emitted && e.frameId == frameId) {
        e.gotStatus = true;
        e.retries = retries;
        e.delivery = delivery;
        break;
      }
    }
  } else if (f[0] == 0x90 && len >= 12 + (int)sizeof(RangePkt)) {  // RX packet
    RangePkt p;
    memcpy(&p, &f[12], sizeof(p));
    if (p.magic != MAGIC || p.kind != 1) return;  // want echoes only
    if (!peerKnown) {
      memcpy(peerAddr, &f[1], 8);                  // latch the RECEIVER's addr
      peerKnown = true;
      Serial.println("# peer found - switching to unicast");
    }
    uint32_t rtt = micros() - p.t_tx_us;
    for (auto& e : ring) {
      if (e.used && !e.emitted && e.seq == p.seq) {
        e.gotEcho = true;
        e.rttUs = rtt;
        e.rssiRemote = p.rssi_remote;
        break;
      }
    }
  }
}

// ===========================================================================
#else   // RANGE_TEST_ROLE == ROLE_RECEIVER
// ===========================================================================

static bool     rxPending = false;
static uint32_t rxPendSeq, rxPendTxUs, rxPendDt, rxPendSince;
static uint8_t  rxPendGap;
static uint8_t  rxPendPeer[8];
static uint32_t rxPendOverwrite = 0;

static bool     haveFirst = false;
static uint32_t firstSeq = 0, lastSeq = 0, rxCount = 0, lastRxUs = 0;
static int32_t  rssiSum = 0, rssiMin = 0, rssiCount = 0;

static const uint8_t FRAMEID_DB = 0x01;

static void sendEcho(const uint8_t peer[8], uint32_t seq, uint32_t t_tx_us, int8_t rssi) {
  RangePkt p;
  p.magic = MAGIC;
  p.kind = 1;
  p.seq = seq;
  p.t_tx_us = t_tx_us;         // carry the SENDER's stamp back untouched
  p.rssi_remote = rssi;
  apiTransmit(peer, ADDR16_UNKNOWN, 0 /*no status needed for echoes*/,
              (const uint8_t*)&p, sizeof(p));
}

static void logRow(uint32_t seq, int rssi, uint32_t dt_us, uint8_t gap) {
  Serial.print("R,");
  Serial.print(seq);    Serial.print(',');
  Serial.print(rssi);   Serial.print(',');
  Serial.print(dt_us);  Serial.print(',');
  Serial.println(gap);
}

static void resetStats() {
  haveFirst = false;
  firstSeq = lastSeq = rxCount = 0;
  rssiSum = rssiMin = rssiCount = 0;
  Serial.println("# stats reset");
}

static void printSummary() {
  Serial.println("# --- receiver summary since last reset ---");
  if (!haveFirst) { Serial.println("# no packets yet"); return; }
  uint32_t expected = lastSeq - firstSeq + 1;
  Serial.print("# received      "); Serial.print(rxCount);
  Serial.print(" / ");              Serial.print(expected);
  Serial.print("   PDR ");          Serial.print(100.0 * rxCount / expected, 1);
  Serial.println("%");
  if (rssiCount) {
    Serial.print("# rssi dbm  mean "); Serial.print((double)rssiSum / rssiCount, 1);
    Serial.print("  worst ");          Serial.println(rssiMin);
  }
  if (rxPendOverwrite) { Serial.print("# ATDB races: "); Serial.println(rxPendOverwrite); }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(XBEE_BAUD);
  waitForSerial();
  printRoleBanner();
  Serial.println("# waiting for pings from the SENDER");
}

void loop() {
  while (Serial1.available()) apiFeed((uint8_t)Serial1.read());

  uint32_t now = millis();
  if (rxPending && now - rxPendSince > DB_TIMEOUT_MS) {
    // ATDB never answered - log with rssi 0, still echo so RTT survives.
    logRow(rxPendSeq, 0, rxPendDt, rxPendGap);
    sendEcho(rxPendPeer, rxPendSeq, rxPendTxUs, 0);
    rxPending = false;
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') resetStats();
    else if (c == 't') printSummary();
  }
}

static void handleFrame(const uint8_t* f, uint16_t len) {
  if (f[0] == 0x88 && len >= 5) {                 // Local AT Command Response
    if (f[2] == 'D' && f[3] == 'B' && f[4] == 0 && len >= 6 && rxPending) {
      int rssi = -(int)f[5];                      // magnitude, in -dBm
      rssiSum += rssi; rssiCount++;
      if (rssi < rssiMin || rssiMin == 0) rssiMin = rssi;
      logRow(rxPendSeq, rssi, rxPendDt, rxPendGap);
      sendEcho(rxPendPeer, rxPendSeq, rxPendTxUs, (int8_t)rssi);
      rxPending = false;
    }
  } else if (f[0] == 0x90 && len >= 12 + (int)sizeof(RangePkt)) {  // RX packet
    RangePkt p;
    memcpy(&p, &f[12], sizeof(p));
    if (p.magic != MAGIC || p.kind != 0) return;  // want pings only

    uint32_t nowUs = micros();
    uint32_t dt = haveFirst ? (nowUs - lastRxUs) : 0;
    uint8_t gap = 0;
    if (!haveFirst) { haveFirst = true; firstSeq = p.seq; }
    else if (p.seq != lastSeq + 1) gap = 1;
    lastSeq = p.seq;
    lastRxUs = nowUs;
    rxCount++;

    if (rxPending) rxPendOverwrite++;              // back-to-back before ATDB
    rxPending = true;
    rxPendSeq = p.seq;
    rxPendTxUs = p.t_tx_us;
    rxPendDt = dt;
    rxPendGap = gap;
    rxPendSince = millis();
    memcpy(rxPendPeer, &f[1], 8);                  // the SENDER's 64-bit addr
    apiAtQuery('D', 'B', FRAMEID_DB);              // RSSI of this last packet
  }
}

#endif
