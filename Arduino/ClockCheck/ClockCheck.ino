// ---------------------------------------------------------------------------
// ClockCheck - answers one question: how fine is micros() on THIS build?
//
// AccelStream timestamps every sample with micros(). A bench run on
// 2026-09-08 produced sample intervals that were only ever 977 us or 1954 us
// (= 2 x 977), i.e. a hard 1/1024 s grid, while captures from a few days
// earlier had 59 distinct interval values around 1022 us. Same sketch, same
// board - so something about the build, not the code, changes the clock.
//
// Rather than keep guessing at which core or which call site is responsible,
// this measures it directly and names the core it was built with. Flash it,
// open the Serial Monitor at 921600, read the verdict.
// ---------------------------------------------------------------------------

// Seeeduino:nrf52 needs the TinyUSB stack pulled in explicitly for Serial to
// link (AccelStream gets it transitively through the IMU library, which this
// sketch deliberately does not use). The mbed core provides Serial itself.
#if !defined(__MBED__)
#include <Adafruit_TinyUSB.h>
#endif

// The two cores that can build for a XIAO nRF52840 Sense identify themselves
// differently; mbed-based Arduino cores always define __MBED__.
#if defined(__MBED__)
  #define CORE_NAME "Seeeduino:mbed (the \"No Updates\" board entries)"
#else
  #define CORE_NAME "Seeeduino:nrf52 (\"Seeed nRF52 Boards\")"
#endif

static const uint32_t PROBE_ITERATIONS = 200000;

void setup() {
  Serial.begin(921600);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) delay(10);

  Serial.println();
  Serial.println("=== ClockCheck ===");
  Serial.print("core: ");
  Serial.println(CORE_NAME);

  // Smallest non-zero step micros() is ever seen to take. For a true 1 MHz
  // source this is 1-2 us; if the underlying tick is 1024 Hz it can only ever
  // be ~977 us, because that is the whole grid.
  uint32_t minStep = 0xFFFFFFFFUL;
  uint32_t steps = 0;
  uint32_t prev = micros();
  for (uint32_t i = 0; i < PROBE_ITERATIONS; i++) {
    uint32_t now = micros();
    uint32_t d = now - prev;
    if (d > 0) {
      steps++;
      if (d < minStep) minStep = d;
      prev = now;
    }
  }

  Serial.print("micros() smallest observed step: ");
  Serial.print(minStep);
  Serial.println(" us");
  Serial.print("micros() changed ");
  Serial.print(steps);
  Serial.print(" times in ");
  Serial.print(PROBE_ITERATIONS);
  Serial.println(" reads");

  Serial.println();
  if (minStep >= 500) {
    Serial.println(">> VERDICT: COARSE. micros() moves in ~1 ms jumps on this");
    Serial.println("   build, so per-sample timestamps cannot be trusted and");
    Serial.println("   reaction times inherit ~1 ms of granularity.");
  } else if (minStep <= 16) {
    Serial.println(">> VERDICT: FINE. micros() has real microsecond resolution");
    Serial.println("   on this build - timestamps are usable.");
  } else {
    Serial.println(">> VERDICT: INTERMEDIATE - report the number above.");
  }
  Serial.println("==================");
}

void loop() {}
