#pragma once
// ---------------------------------------------------------------------------
// Storage.h - every start is kept on the XIAO's 2 MB QSPI flash (P25Q16H), so
// a session run with no computer attached can be pulled later with
// Python_Tools/pull_captures.py.
//
// NO FILESYSTEM, on purpose. The Seeeduino:mbed 2.9.3 build for this board
// ships neither mbed::QSPI nor QSPIFBlockDevice (DEVICE_QSPI is off), only
// the Nordic nrfx_qspi driver. A start is also always about the same size, so
// fixed slots do everything a filesystem would:
//
//   25 slots x 80 KB = 2000 KB, then one 4 KB sector holding the id counter.
//   A slot = CaptureMeta (92 bytes) + n x SampleRec (10 bytes), n <= 8182,
//   i.e. ~9.5 s of samples against the ~6.5 s a start actually dumps.
//
// Stored BINARY, not as the CSV text the serial dump prints (~190 KB a start,
// which would fit only 10). The CSV is produced on the way out: the board
// replays a stored run through the very same dump code the live path uses, so
// a pulled run and a live capture of the same start come out in one format.
//
// The header is written LAST. A slot whose write was cut short (power pulled
// mid-save) has no valid magic and simply reads as empty.
//
// RETENTION: the oldest run is overwritten when all 25 slots are full. The
// board has no calendar clock (micros() restarts at every power-on), so
// "delete after N days" cannot be done honestly; by count it can.
// ---------------------------------------------------------------------------
#include "nrfx_qspi.h"

static const uint32_t CAPTURE_MAGIC   = 0x50535431;   // "PST1"
static const uint16_t CAPTURE_VERSION = 2;   // 2: + wallclock
static const uint32_t SLOT_COUNT      = 25;
static const uint32_t SLOT_BYTES      = 80UL * 1024;
static const uint32_t COUNTER_ADDR    = SLOT_COUNT * SLOT_BYTES;   // one 4 KB sector
static const uint32_t SECTOR_BYTES    = 4096;

// Everything the dump prints before the rows. One struct for the live dump and
// the stored run, so the two can never drift apart.
struct CaptureMeta {
  uint32_t magic;
  uint16_t version, metaSize;
  uint32_t id;               // 0 = not stored
  uint32_t n;                // sample rows that follow
  uint32_t onT, setT, goT;   // 0 = not captured
  uint32_t preroll;
  uint32_t dropped, gaps, maxGapUs, clockStep;
  uint8_t  truncated, armValid, armCapped, hasVerdict;
  uint32_t armT;
  float    armMg, rtMs;
  uint32_t onsetUs;
  char     verdict[24];
  uint32_t wallclock;        // unix time of the start, 0 = board had no time
};
static_assert(sizeof(CaptureMeta) % 4 == 0, "QSPI transfers are whole words");

struct __attribute__((packed)) SampleRec {
  uint32_t t;
  int16_t x, y, z;
};

static const uint32_t SLOT_MAX_SAMPLES = (SLOT_BYTES - sizeof(CaptureMeta)) / sizeof(SampleRec);

static bool storageReady = false;
static uint32_t jedecId = 0;
static uint32_t nextRunId = 1;
static uint32_t slotId[SLOT_COUNT];   // 0 = empty
static uint32_t slotN[SLOT_COUNT];

// EasyDMA: RAM only, word aligned, lengths in whole words.
static uint32_t qspiBuf[1000];        // 4000 bytes = 400 samples

// Why the last flash operation failed, for the STORE,failed line: which step,
// nrfx's return code, at which address. A save that fails without saying why
// is not debuggable from the field.
static const char* flashErrStep = "";
static int flashErrCode = 0;
static uint32_t flashErrAddr = 0;

static uint32_t FLASH_WAIT_LIMIT_MS = 1000;

static bool flashFail(const char* step, int code, uint32_t addr) {
  flashErrStep = step; flashErrCode = code; flashErrAddr = addr;
  return false;
}

// Polls the flash's WIP bit. Bounded by time, not iterations: a 4 KB sector
// erase is ~50 ms typical, 300 ms worst case on the P25Q16H.
static uint32_t flashMaxWaitMs = 0;    // longest wait since last reset of it

// Reads the flash's status register (RDSR, 0x05). Returns nrfx's code.
static int flashReadStatus(uint8_t* sr) {
  nrf_qspi_cinstr_conf_t ci;
  memset(&ci, 0, sizeof(ci));
  ci.opcode = 0x05;
  ci.length = NRF_QSPI_CINSTR_LEN_2B;
  ci.io2_level = true;
  ci.io3_level = true;
  uint8_t rx[1] = {0xFF};
  int e = (int)nrfx_qspi_cinstr_xfer(&ci, NULL, rx);
  *sr = rx[0];
  return e;
}

// Waits for the flash's WIP bit to clear, reading the status register itself.
//
// NOT nrfx_qspi_mem_busy_check(). On this core (SDK 15.0 nrfx) it was caught
// both ways on 2026-09-29: right after an erase it answered "free" while RDSR
// read 0x03 (WEL|WIP, erasing), and in the failing case it answered BUSY (17)
// for 10 s on a flash long done - which is what made the first save after
// every boot fail with "NOT saved". Raw RDSR read the truth every time.
static bool qspiWait(const char* step, uint32_t addr) {
  uint32_t t0 = millis();
  int e = 0;
  uint8_t sr = 0xFF;
  while (millis() - t0 < FLASH_WAIT_LIMIT_MS) {
    e = flashReadStatus(&sr);
    if (e == (int)NRFX_SUCCESS && (sr & 0x01) == 0) {
      uint32_t w = millis() - t0;
      if (w > flashMaxWaitMs) flashMaxWaitMs = w;
      return true;
    }
  }
  return flashFail(step, e != (int)NRFX_SUCCESS ? e : 1000 + sr, addr);
}

static bool flashRead(uint32_t addr, void* buf, uint32_t len) {
  nrfx_err_t e = nrfx_qspi_read(buf, (len + 3) & ~3UL, addr);
  return e == NRFX_SUCCESS || flashFail("read", (int)e, addr);
}

static bool flashWrite(uint32_t addr, const void* buf, uint32_t len) {
  nrfx_err_t e = nrfx_qspi_write(buf, (len + 3) & ~3UL, addr);
  if (e != NRFX_SUCCESS) return flashFail("write", (int)e, addr);
  return qspiWait("write-wait", addr);
}

static bool flashErase(uint32_t addr, uint32_t len) {
  for (uint32_t a = addr; a < addr + len; a += SECTOR_BYTES) {
    nrfx_err_t e = nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, a);
    if (e != NRFX_SUCCESS) return flashFail("erase", (int)e, a);
    if (!qspiWait("erase-wait", a)) return false;
  }
  return true;
}

static void storageRescan() {
  uint32_t maxId = 0;
  for (uint32_t s = 0; s < SLOT_COUNT; s++) {
    CaptureMeta m;
    slotId[s] = slotN[s] = 0;
    if (!flashRead(s * SLOT_BYTES, &m, sizeof(m))) continue;
    if (m.magic != CAPTURE_MAGIC || m.id == 0) continue;
    // A header from an older format still holds an id that was handed out:
    // it keeps the counter above it, even though the run itself is not
    // readable any more and its slot counts as free.
    if (m.id > maxId) maxId = m.id;
    if (m.metaSize != sizeof(CaptureMeta) || m.n > SLOT_MAX_SAMPLES) continue;
    slotId[s] = m.id;
    slotN[s] = m.n;
  }
  // The counter sector outlives an erase-all, so a pulled "r00007" never gets
  // a different start's data under the same name later.
  uint32_t counter[2];
  if (flashRead(COUNTER_ADDR, counter, sizeof(counter)) &&
      counter[0] == CAPTURE_MAGIC && counter[1] > maxId) {
    nextRunId = counter[1];
  } else {
    nextRunId = maxId + 1;
  }
}

static bool storageBegin() {
  nrfx_qspi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  // XIAO nRF52840 flash pins (variant.cpp: D23..D28).
  cfg.pins.sck_pin = 21;
  cfg.pins.csn_pin = 25;
  cfg.pins.io0_pin = 20;
  cfg.pins.io1_pin = 24;
  cfg.pins.io2_pin = 22;
  cfg.pins.io3_pin = 23;
  // Single-line read/program: no quad-enable bit to set in the flash, and
  // 8 MHz x 1 line is still ~50 ms for a whole start.
  cfg.prot_if.readoc    = NRF_QSPI_READOC_FASTREAD;
  cfg.prot_if.writeoc   = NRF_QSPI_WRITEOC_PP;
  cfg.prot_if.addrmode  = NRF_QSPI_ADDRMODE_24BIT;
  cfg.prot_if.dpmconfig = false;
  cfg.phy_if.sck_delay  = 1;
  cfg.phy_if.dpmen      = false;
  cfg.phy_if.spi_mode   = NRF_QSPI_MODE_0;
  cfg.phy_if.sck_freq   = NRF_QSPI_FREQ_32MDIV4;
  cfg.irq_priority      = 7;
  if (nrfx_qspi_init(&cfg, NULL, NULL) != NRFX_SUCCESS) return false;   // NULL = blocking

  // Release from deep power-down in case anything put it there, then check the
  // chip answers at all: a JEDEC id of 0 or all-ones means no flash.
  nrfx_qspi_cinstr_quick_send(0xAB, NRF_QSPI_CINSTR_LEN_1B, NULL);
  delay(1);
  nrf_qspi_cinstr_conf_t ci;
  memset(&ci, 0, sizeof(ci));
  ci.opcode = 0x9F;
  ci.length = NRF_QSPI_CINSTR_LEN_4B;
  ci.io2_level = true;
  ci.io3_level = true;
  uint8_t id[3] = {0, 0, 0};
  if (nrfx_qspi_cinstr_xfer(&ci, NULL, id) != NRFX_SUCCESS) return false;
  jedecId = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
  if (jedecId == 0 || jedecId == 0xFFFFFF) return false;

  storageRescan();
  storageReady = true;
  return true;
}

static uint32_t storageCount() {
  uint32_t n = 0;
  for (uint32_t s = 0; s < SLOT_COUNT; s++) if (slotId[s]) n++;
  return n;
}

// Stored runs as slot indices, oldest first. Returns how many.
static uint32_t storageSlotsByAge(uint8_t* order) {
  uint32_t n = 0;
  for (uint32_t s = 0; s < SLOT_COUNT; s++) if (slotId[s]) order[n++] = s;
  for (uint32_t i = 1; i < n; i++) {
    uint8_t v = order[i]; int32_t j = i - 1;
    while (j >= 0 && slotId[order[j]] > slotId[v]) { order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
  return n;
}

// An empty slot if there is one, else the oldest run's.
static uint32_t storagePickSlot() {
  uint32_t best = 0;
  for (uint32_t s = 0; s < SLOT_COUNT; s++) {
    if (slotId[s] == 0) return s;
    if (slotId[s] < slotId[best]) best = s;
  }
  return best;
}

// Invalidates every run by erasing the sector that holds its header - 25
// erases, not 500 - and keeps the id counter going.
static uint32_t storageEraseAll() {
  uint32_t n = storageCount();
  for (uint32_t s = 0; s < SLOT_COUNT; s++) {
    if (slotId[s]) flashErase(s * SLOT_BYTES, SECTOR_BYTES);
  }
  flashErase(COUNTER_ADDR, SECTOR_BYTES);
  uint32_t counter[2] = {CAPTURE_MAGIC, nextRunId};
  flashWrite(COUNTER_ADDR, counter, sizeof(counter));
  storageRescan();
  return n;
}
