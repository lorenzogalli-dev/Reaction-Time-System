#pragma once
// ---------------------------------------------------------------------------
// Storage.h - every start is kept on the ESP32-C3's internal 4 MB flash, so a
// session run with no computer attached can be pulled later with
// Python_Tools/pull_captures.py.
//
// Same design as the XIAO version, different flash underneath: there it was
// the external QSPI chip through nrfx_qspi, here it is a raw data partition,
// "runs", declared in this folder's partitions.csv (the build picks that file
// up by itself). No filesystem, on purpose - a start is always about the same
// size, so fixed slots do everything a filesystem would:
//
//   25 slots x 80 KB = 2000 KB, then one 4 KB sector holding the id counter.
//   A slot = CaptureMeta (92 bytes) + n x SampleRec (10 bytes), n <= 8182,
//   i.e. ~10.2 s at 800 Hz against the ~7.6 s a start can dump at most.
//
// Stored BINARY; the CSV is produced on the way out through the same dump code
// the live path uses. The header is written LAST: a slot whose write was cut
// short (power pulled mid-save) has no valid magic and reads as empty.
//
// Uploading a new sketch does not touch the partition (esptool only writes the
// app), so runs survive an upload as they did on the XIAO. Flashing a sketch
// built with a different partition table can overwrite them.
//
// Flash writes and erases on the ESP32 stop the CPU's cache, and with it every
// interrupt not in IRAM - the data-ready ISR included. That is fine for the
// same reason it was on the XIAO: storeRun() runs on the result screen, after
// the measurement, never during a start.
//
// RETENTION: the oldest run is overwritten when all 25 slots are full.
// ---------------------------------------------------------------------------
#include "esp_partition.h"
#include "esp_flash.h"

static const uint32_t CAPTURE_MAGIC   = 0x50535431;   // "PST1"
static const uint16_t CAPTURE_VERSION = 2;   // 2: + wallclock
static const uint32_t SLOT_COUNT      = 25;
static const uint32_t SLOT_BYTES      = 80UL * 1024;
static const uint32_t COUNTER_ADDR    = SLOT_COUNT * SLOT_BYTES;   // one 4 KB sector
static const uint32_t SECTOR_BYTES    = 4096;
static const uint32_t STORAGE_BYTES   = COUNTER_ADDR + SECTOR_BYTES;
// partitions.csv: runs, data, 0x40 (any "custom" subtype; this one is ours).
static const esp_partition_subtype_t RUNS_SUBTYPE = (esp_partition_subtype_t)0x40;

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
static_assert(sizeof(CaptureMeta) % 4 == 0, "flash writes are whole words");

struct __attribute__((packed)) SampleRec {
  uint32_t t;
  int16_t x, y, z;
};

static const uint32_t SLOT_MAX_SAMPLES = (SLOT_BYTES - sizeof(CaptureMeta)) / sizeof(SampleRec);

static const esp_partition_t* runsPart = nullptr;
static bool storageReady = false;
static uint32_t jedecId = 0;
static uint32_t nextRunId = 1;
static uint32_t slotId[SLOT_COUNT];   // 0 = empty
static uint32_t slotN[SLOT_COUNT];

static uint32_t flashBuf[1000];       // 4000 bytes = 400 samples

// Why the last flash operation failed, for the STORE,failed line: which step,
// the esp_err_t, at which address.
static const char* flashErrStep = "";
static int flashErrCode = 0;
static uint32_t flashErrAddr = 0;

// Kept for the 'E' diagnostic, which raises it. The IDF calls block until the
// flash is done, so here it only bounds nothing; the timing is still reported.
static uint32_t FLASH_WAIT_LIMIT_MS = 1000;
static uint32_t flashMaxWaitMs = 0;

static bool flashFail(const char* step, int code, uint32_t addr) {
  flashErrStep = step; flashErrCode = code; flashErrAddr = addr;
  return false;
}

static bool flashRead(uint32_t addr, void* buf, uint32_t len) {
  esp_err_t e = esp_partition_read(runsPart, addr, buf, len);
  return e == ESP_OK || flashFail("read", (int)e, addr);
}

static bool flashWrite(uint32_t addr, const void* buf, uint32_t len) {
  esp_err_t e = esp_partition_write(runsPart, addr, buf, (len + 3) & ~3UL);
  return e == ESP_OK || flashFail("write", (int)e, addr);
}

static bool flashErase(uint32_t addr, uint32_t len) {
  for (uint32_t a = addr; a < addr + len; a += SECTOR_BYTES) {
    uint32_t t0 = millis();
    esp_err_t e = esp_partition_erase_range(runsPart, a, SECTOR_BYTES);
    uint32_t w = millis() - t0;
    if (w > flashMaxWaitMs) flashMaxWaitMs = w;
    if (e != ESP_OK) return flashFail("erase", (int)e, a);
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
  esp_flash_read_id(NULL, &jedecId);
  runsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, RUNS_SUBTYPE, "runs");
  if (runsPart == nullptr) return flashFail("no 'runs' partition (partitions.csv?)", 0, 0);
  if (runsPart->size < STORAGE_BYTES) return flashFail("'runs' partition too small", (int)runsPart->size, 0);
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
