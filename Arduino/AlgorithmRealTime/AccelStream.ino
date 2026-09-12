#include "LSM6DS3.h"
#include "Wire.h"
#include "StartDetector.h"
#include "AicPicker.h"

static StartDetector detector;
// ---------------------------------------------------------------------------
// AccelStream - no-BLE accelerometer streamer + full-rate buffered recorder,
// XIAO nRF52840 Sense.
//
// v2: the first version polled the IMU with three separate
// readFloatAccelX/Y/Z() calls (six I2C transactions/sample - each has fixed
// driver overhead on the nRF52840) and printed an ASCII CSV row over serial
// for every sample. Measured effective rate on real hardware: ~232 Hz, well
// under the configured 416 Hz ODR, with near-zero jitter - a hard ceiling
// from fixed per-sample cost, not sensor speed or scheduling noise.
//
// This version fixes both halves of that cost:
//  - X/Y/Z live in six consecutive registers on the chip; one burst
//    readRegisterRegion() call reads all six bytes in a single I2C
//    transaction instead of six.
//  - Serial.print of an ASCII float, every sample, has almost no margin
//    left in a 1660 Hz budget (602 us/sample) and silently throttles the
//    whole loop the moment the host or USB stalls even briefly - exactly
//    the failure mode that produced the 232 Hz ceiling. So printing is
//    decoupled from sampling: while idle, only every STREAM_DECIMATE-th
//    sample is printed (a live preview, plenty fast for a chart); while
//    recording, nothing is printed at all - every single sample at full
//    ODR is written straight into a RAM buffer, and the whole buffer is
//    dumped over serial only after the capture stops. Serial timing can
//    never rob a sample during the window that actually matters.
//
// v4 (2026-09-08): THE BOARD RUNS THE START, THE HOST ONLY WRITES THE FILE
// ------------------------------------------------------------------------
// Through v3 the o/s/g markers were human keypresses relayed over serial.
// That made the reference worse than the thing being measured: on
// Data/accel_20260907_160108.csv the "go" marker lands ~90 ms AFTER the
// movement it was supposed to mark, so tuning a +/-10 ms detection threshold
// against it was circular. v4 removes the human from the timing path
// entirely - a button starts a real, randomised start sequence and the
// firmware sounds the three beeps itself, timestamping each one on the same
// micros() clock as every accelerometer sample. No cross-clock sync, no
// keypress jitter. What remains is the buzzer's own acoustic latency
// (5-20 ms): systematic and constant, so it is calibrated once and
// subtracted, not fought on every run.
//
// Sequence, all delays randomised so the rhythm cannot be learned:
//   button -> 2-3 s -> beep "on your marks" -> 20-25 s -> beep "set"
//          -> 2.2-3 s -> beep "go" -> 1 s -> dump
//
// The recording window is [set - PREROLL_SAMPLES, go + 1 s]. The pre-roll is
// not padding: the detector's LTA needs ~800 ms to converge, so a capture
// starting exactly at "set" would leave it blind for the first 800 ms of the
// 1-2 s window in which a false start can happen. The sample buffer is
// therefore a ring that is ALWAYS filling; "set" merely marks a position in
// data that already exists.
//
// SERIAL PROTOCOL
// ----------------
// Idle (default from boot, only while no sequence is running): the board
// streams decimated CSV rows continuously, no command needed - live preview.
//   t_us,x_g,y_g,z_g
// The button (BUTTON_PIN to GND) starts a sequence; pressing it again during
// one aborts. Nothing is printed during a sequence except its progress lines
// SEQ,armed / SEQ,marks / SEQ,set / SEQ,go / SEQ,abort,<why>.
// 'b' does exactly what the button does - so the whole sequence can be
//     exercised with no button wired yet.
// 'a' aborts a running sequence.
// 'd' dumps the whole ring immediately (no sequence, no markers) - this is
//     what verify_rate.py uses for its rate/integrity check.
// 'p' prints one immediate reading, independent of the above.
//
// One second after "go" the board dumps the window by itself:
//   DUMP_START,<n>
//   ON,<t_us or 0>        beep 1, "on your marks"
//   SET,<t_us or 0>       beep 2, "set"
//   GO,<t_us or 0>        beep 3, "go" - the reaction-time reference
//   PREROLL,<n>           samples present BEFORE the "set" instant
//   TRUNCATED,<0|1>       1 = the ring wrapped, the pre-roll is short
//   DROPPED,<n>           samples the data-ready watchdog had to recover
//   CLOCKSTEP,<us>        measured micros() resolution of this build
//   <n CSV rows, t_us,x_g,y_g,z_g, full ODR, no decimation>
//   DUMP_END
//   ... then idle decimated streaming resumes automatically.
//
// DROPPED other than 0 means some timestamps in that capture are degraded.
// TRUNCATED 1 means the detector has less warmup than intended - the run is
// still usable, but PREROLL says how much it actually got.
//
// t_us is read the moment the INT1 data-ready flag is seen and BEFORE the
// I2C burst read, so it no longer includes the ~1023 us that read takes -
// which is what the pre-v3 value did. It is not taken in the ISR itself:
// micros() has no microsecond resolution there on this core (see drdyIsr).
//
// t_us is an unsigned micros() value (wraps every ~71 minutes, ignored here
// since captures are short bench/block tests, not multi-hour sessions).
// ---------------------------------------------------------------------------

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// 833 Hz, paced by the sensor's own data-ready line (INT1) rather than by
// however fast this loop happens to run - see serviceSampling().
//
// v2 asked for 1660 Hz and measured ~977 Hz. That was not the sensor falling
// back: the chip really did run at 1660 Hz, but one burst read costs ~1023 us
// against a 602 us budget, so the pacing deadline below could never be met
// and the loop simply free-ran at whatever I2C throughput allowed. The rate
// was repeatable (977.5-978.5 Hz) but *emergent* - it is whatever is left
// after the loop's workload, so adding the on-device STA/LTA detector would
// have lowered it silently. That is exactly the failure mode that capped v1
// at 232 Hz (a Serial.print per sample), so it is worth not building on.
//
// 833 Hz is the next ODR step the library recognizes and one the read path
// can genuinely sustain: a 1200 us budget against ~1023 us of work leaves
// ~177 us per sample (~11k cycles at 64 MHz with FPU) of headroom for the
// detector - and headroom can be measured, whereas a drifting rate cannot.
//
// Accuracy is deliberately NOT the point here: total sample-timing error
// goes from sigma ~0.34 ms to ~0.35 ms, i.e. unchanged. Coarser quantisation
// (1200 us steps instead of 1023) is traded against removing the 0-602 us
// staleness the old timestamps carried, and the two cancel. What is bought
// is a rate that is specified instead of observed, and a timestamp taken at
// the instant the sample was produced. The dominant error in a reaction time
// remains where the detector threshold lands on the push-off ramp - tens of
// ms, and still untuned - not this.
//
// The library's accelSampleRate switch silently falls through to its 104 Hz
// default for any value not in its list (its own comment says "1666" while
// the case is literally 1660), which would be a very quiet way to lose 8x
// the rate - hence the static_assert rather than a bare constant.
static const uint16_t ACCEL_ODR_HZ = 833;
static_assert(ACCEL_ODR_HZ == 13 || ACCEL_ODR_HZ == 26 || ACCEL_ODR_HZ == 52 ||
              ACCEL_ODR_HZ == 104 || ACCEL_ODR_HZ == 208 || ACCEL_ODR_HZ == 416 ||
              ACCEL_ODR_HZ == 833 || ACCEL_ODR_HZ == 1660 || ACCEL_ODR_HZ == 3330 ||
              ACCEL_ODR_HZ == 6660 || ACCEL_ODR_HZ == 13330,
              "ACCEL_ODR_HZ must be a value LSM6DS3.cpp's accelSampleRate switch "
              "handles; anything else silently configures 104 Hz instead");
static const uint32_t SAMPLE_PERIOD_US = 1000000UL / ACCEL_ODR_HZ;

// The IMU's data-ready line has to reach a GPIO for any of this to work.
//
// Which header carries that pin number depends on the core, not on the board:
// Seeeduino:nrf52 (1.1.13) pulls it in through Arduino.h/variant.h, so the
// macro is already visible here, while Seeeduino:mbed (2.9.3 - the "No
// Updates" board entries) puts it in a pins_arduino.h that Arduino.h does not
// include for us. Same board, same pin 18 - just not declared yet. So pull it
// in when it is missing rather than rejecting a perfectly capable board.
// Note this also governs PIN_LSM6DS3TR_C_POWER used in setup(): without it
// the IMU is never powered up and begin() fails on that core.
#if !defined(PIN_LSM6DS3TR_C_INT1) && defined(__has_include)
#if __has_include("pins_arduino.h")
#include "pins_arduino.h"
#endif
#endif

#ifndef PIN_LSM6DS3TR_C_INT1
#error "AccelStream needs the IMU's data-ready line (PIN_LSM6DS3TR_C_INT1). Select a XIAO nRF52840 *Sense* - the plain XIAO has no onboard IMU - or define the pin by hand if you wired an external one."
#endif

// DRDY is level-latched: it goes high when a sample is ready and only drops
// once the output registers are read. So a single missed edge is permanent -
// no read means no falling edge means no next rising edge, and sampling would
// stop dead. This watchdog bounds that: if no edge arrives for three sample
// periods, read anyway (which clears DRDY and lets edges resume) and count it.
// It also covers startup, where DRDY can already be high before
// attachInterrupt() is in place and there is no edge left to catch.
static const uint32_t DRDY_STALL_TIMEOUT_US = 3 * SAMPLE_PERIOD_US;

// +/-16 g: a real push-off rigidly mounted on the block is expected around
// 1-3 g, but a 2026-09-07 bench test (a hard hand hit, well above a real
// push-off) clipped hard at +/-8g on two axes simultaneously for ~200ms.
// 16g costs almost nothing here - LSB resolution only drops from 0.244 mg
// to 0.488 mg, still far below the ~20 mg detection threshold - so it's
// cheap margin against clipping the one part of the signal that matters.
static const uint8_t ACCEL_RANGE_G = 16;

// Same scaling the library's calcAccel() applies (LSM6DS3.cpp), replicated
// here so the burst read path doesn't need three separate library calls.
static const float ACCEL_SCALE_G_PER_LSB = 0.061f * (ACCEL_RANGE_G >> 1) / 1000.0f;

// Idle live-preview rate = ACCEL_ODR_HZ / STREAM_DECIMATE (~167 Hz) - fast
// enough for a smooth chart, slow enough that ASCII float printing has huge
// timing margin and can never throttle the underlying sample schedule.
static const uint16_t STREAM_DECIMATE = 5;

// RAM budget: 10 bytes/sample (uint32 t_us + 3x int16 raw) x 12000 = ~117 KB,
// comfortably inside the XIAO nRF52840's 256 KB RAM with no BLE stack
// running. ~13.9 s at the 863 Hz this actually achieves.
//
// v4 fills this CIRCULARLY and continuously, rather than starting it at
// "set" - that is what makes the pre-roll possible. See the header: the
// dumped window reaches PREROLL_SAMPLES back behind the "set" instant, into
// data that was already there. 13.9 s of ring against a ~6 s window is ~2x
// margin, so the 20-25 s "on your marks" pause overwriting the ring several
// times over is harmless and expected.
static const uint32_t MAX_REC_SAMPLES = 12000;
static uint32_t recT[MAX_REC_SAMPLES];
static int16_t recX[MAX_REC_SAMPLES];
static int16_t recY[MAX_REC_SAMPLES];
static int16_t recZ[MAX_REC_SAMPLES];
// Total samples ever written. Sample i lives in slot i % MAX_REC_SAMPLES;
// only the newest MAX_REC_SAMPLES of them still exist. A plain counter rather
// than a head index because the "set" boundary has to be comparable against
// it across a wrap.
static uint32_t recWritten = 0;

// How far behind "set" the dump reaches. Sized in samples at the nominal ODR,
// so ~2.9 s at the 863 Hz really achieved - still 3.6x the detector's 800 ms
// LTA time constant, and it costs nothing: the samples are already in RAM.
static const uint32_t PREROLL_SAMPLES = 3UL * ACCEL_ODR_HZ;
// Recording tail after "go". A push-off is long over inside this.
static const uint32_t TAIL_AFTER_GO_MS = 1000;

// --- Start sequence --------------------------------------------------------
// D0/D1 are plain GPIO on the XIAO nRF52840 Sense - clear of the I2C the IMU
// sits on and of the UART on D6/D7. The macros come from the core's
// pins_arduino.h, pulled in above; the fallbacks keep this compiling if a
// core ever declines to define them, since on this board they are pins 0/1.
#ifndef D0
#define D0 0
#endif
#ifndef D1
#define D1 1
#endif
static const int BUTTON_PIN = D0;   // momentary button to GND, INPUT_PULLUP
// PASSIVE buzzer, driven with tone()/noTone() - no internal oscillator, so a
// steady digitalWrite() level (what this used to be) produces at most one
// click and then silence. tone()'s latency and its effect on sample timing
// are UNVERIFIED on this core - see the comment on beep() below.
static const int BUZZER_PIN = D1;   // (+) here; (-) to GND
static const unsigned int BEEP_FREQ_HZ = 3000;

static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t BEEP_MS = 100;

// Randomised so the athlete cannot learn the rhythm - the reason this matters
// is that an anticipated "go" is exactly what a reaction time must not
// measure. Arduino's random(a, b) is inclusive of a, exclusive of b.
static const long MARKS_DELAY_MIN_MS = 2000,  MARKS_DELAY_MAX_MS = 3001;
static const long SET_DELAY_MIN_MS   = 8000, SET_DELAY_MAX_MS   = 10001;
// set -> go raised from 1-2 s to 2.2-3 s on 2026-09-08. An athlete needs about
// a second to rise into the set position after the command, and that rise is a
// real movement of several hundred mg. At 1-2 s the "go" could fire while they
// were still settling, which makes the reaction time meaningless, and left
// almost no judged window after the rise. The detector blanks the first second
// after "set" to match (blank_ms in start_detector.py); these two numbers are
// a pair - change one and revisit the other.
static const long GO_DELAY_MIN_MS    = 2200,  GO_DELAY_MAX_MS    = 3001;

enum SeqState {
  SEQ_IDLE,        // nothing running; idle preview streams
  SEQ_WAIT_MARKS,  // button pressed, waiting to sound "on your marks"
  SEQ_WAIT_SET,    // "on your marks" sounded, waiting to sound "set"
  SEQ_WAIT_GO,     // "set" sounded - this is the false-start window
  SEQ_TAIL         // "go" sounded, capturing TAIL_AFTER_GO_MS more
};
static SeqState seqState = SEQ_IDLE;
static uint32_t seqDeadlineUs = 0;
static uint32_t buzzerOffUs = 0;
static bool buzzerOn = false;
static bool randomSeeded = false;

// Value of recWritten at the instant "set" sounded - the boundary the dump
// window is measured from, in both directions.
static uint32_t setSampleIdx = 0;

static bool imuReady = false;
static uint32_t idleSampleIndex = 0;

// Set by the data-ready ISR, cleared by the main loop.
static volatile bool drdyPending = false;
static uint32_t lastSampleUs = 0;

// Samples whose true instant is unknown because the watchdog had to recover
// them rather than an edge delivering them. Reported with every dump: a
// capture with a non-zero count here is not clean data, and silently
// discarding that fact is how a timing bug survives to the next person.
static uint32_t droppedSamples = 0;

// Measured resolution of micros() on this build, in microseconds. Every
// timestamp in a capture is only as good as this number, and it is NOT a
// property of the sketch - it depends on which core the sketch was built
// with, silently:
//   - Seeeduino:mbed  -> mbed::Timer, a real 1 MHz counter. ~1 us. Good.
//   - Seeeduino:nrf52 -> DWT cycle counter IF enabled, otherwise it falls
//     back to the FreeRTOS tick, and configTICK_RATE_HZ is 1024, giving
//     976.5625 us steps. DWT is off unless a debugger enabled it, so the
//     normal case is the bad one.
// A 2026-09-08 bench run hit exactly that: sample intervals collapsed to
// only 977 us and 1954 us. The data looked plausible - no gaps, no dropped
// samples, sane accelerations - while every timestamp was ~1 ms granular.
// That is the dangerous kind of wrong, so it is measured at boot and
// reported with every capture rather than assumed.
static uint32_t clockStepUs = 0;

// Smallest non-zero increment micros() is observed to make. A 1 MHz source
// gives 1-2; the 1024 Hz tick fallback can only ever give ~977.
static uint32_t measureClockStepUs() {
  uint32_t minStep = 0xFFFFFFFFUL;
  uint32_t prev = micros();
  for (uint32_t i = 0; i < 40000; i++) {
    uint32_t now = micros();
    uint32_t d = now - prev;
    if (d > 0) {
      if (d < minStep) minStep = d;
      prev = now;
    }
  }
  return (minStep == 0xFFFFFFFFUL) ? 0 : minStep;
}

// The three start markers. As of v4 these are set by the firmware itself
// when it drives the buzzer, not by a relayed keypress - each is a micros()
// reading on the same clock every accelerometer sample uses, so a marker and
// the push-off measured against it are always on one timeline with no
// cross-clock or host-latency term at all.
static uint32_t onT = 0, setT = 0, goT = 0;
static bool onCaptured = false, setCaptured = false, goCaptured = false;

// Deliberately does NOT timestamp. micros() called from interrupt context on
// this core does not have microsecond resolution - it falls back to a 1024 Hz
// counter, so every timestamp lands on a ~976.6 us grid. A first cut of v3
// took the timestamp here and a bench run showed it plainly: dt collapsed to
// just two values, 977 us and 1954 us, where the v2 captures it replaced had
// 59 distinct values around 1022 us. Reading the clock in the main loop is
// both finer and, given the flag is serviced promptly, barely later.
static void drdyIsr() {
  drdyPending = true;
}

void setup() {
  Serial.begin(921600);
  // Native USB CDC: Serial only becomes true once a host opens the port.
  // Bounded wait so a battery-powered board (no host attached) doesn't hang
  // here forever.
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) delay(10);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

#ifdef PIN_LSM6DS3TR_C_POWER
  // XIAO Sense IMU power-enable pin: if left low, begin() fails even with
  // I2C wired correctly.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
#endif

  delay(100);  // LSM6DS3 needs a moment before it responds on I2C

  myIMU.settings.gyroEnabled = 0;
  myIMU.settings.accelEnabled = 1;
  myIMU.settings.accelSampleRate = ACCEL_ODR_HZ;
  myIMU.settings.accelRange = ACCEL_RANGE_G;
  myIMU.settings.accelFifoEnabled = 0;  // no FIFO - plain polling reads only

  imuReady = (myIMU.begin() == 0);
  if (!imuReady) {
    Serial.println("IMU error - check wiring/power pin");
  } else {
    Wire.setClock(400000);

    // Route accelerometer data-ready to INT1.
    myIMU.writeRegister(LSM6DS3_ACC_GYRO_INT1_CTRL,
                        LSM6DS3_ACC_GYRO_INT1_DRDY_XL_ENABLED);

    // DRDY may already be latched high from a sample taken during begin(),
    // in which case there is no rising edge left for attachInterrupt() to
    // catch. One throwaway read clears it so edges start cleanly.
    int16_t discardX, discardY, discardZ;
    readSampleRaw(&discardX, &discardY, &discardZ);
    lastSampleUs = micros();

    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), drdyIsr, RISING);

    Serial.print("IMU OK - accel ");
    Serial.print(ACCEL_ODR_HZ);
    Serial.print(" Hz (data-ready on INT1), +/-");
    Serial.print(ACCEL_RANGE_G);
    Serial.println(" g");
  }

  clockStepUs = measureClockStepUs();
  Serial.print("clock: micros() resolution ~");
  Serial.print(clockStepUs);
  Serial.println(" us");
  if (clockStepUs > 100) {
    Serial.println("!! WARNING: this build's micros() is ~1 ms granular, not microsecond.");
    Serial.println("!! Every timestamp below inherits that. Reaction times are NOT reliable.");
    Serial.println("!! Cause: the Seeeduino:nrf52 core falls back to the 1024 Hz FreeRTOS");
    Serial.println("!! tick when the DWT cycle counter is off. Build with the mbed core");
    Serial.println("!! (\"XIAO nRF52840 Sense (No Updates)\"), which uses a real 1 MHz timer.");
  }

  Serial.println("Ready. Idle preview streaming. 'p' one reading.");
  Serial.print("Start sequence: press the button on D");
  Serial.print(BUTTON_PIN);
  Serial.print(" (or send 'b'); buzzer on D");
  Serial.println(BUZZER_PIN);
  Serial.println("  button -> 2-3s -> MARKS -> 20-25s -> SET -> 2.2-3s -> GO -> 1s -> dump");
  Serial.println("  'a' or a second press aborts. 'd' dumps the ring as-is.");
}

// Single I2C transaction: X/Y/Z occupy six consecutive registers
// (OUTX_L_XL..OUTZ_H_XL), so one readRegisterRegion() call replaces the
// three separate readFloatAccelX/Y/Z() calls (six transactions) the first
// version used - this is what actually removes the per-sample I2C overhead.
static void readSampleRaw(int16_t* rawX, int16_t* rawY, int16_t* rawZ) {
  uint8_t buf[6];
  myIMU.readRegisterRegion(buf, LSM6DS3_ACC_GYRO_OUTX_L_XL, 6);
  *rawX = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
  *rawY = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
  *rawZ = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
}

static inline float rawToG(int16_t raw) {
  return (float)raw * ACCEL_SCALE_G_PER_LSB;
}

static void printCsvRow(uint32_t t_us, int16_t rawX, int16_t rawY, int16_t rawZ) {
  Serial.print(t_us);
  Serial.print(',');
  Serial.print(rawToG(rawX), 4);
  Serial.print(',');
  Serial.print(rawToG(rawY), 4);
  Serial.print(',');
  Serial.println(rawToG(rawZ), 4);
}

// wholeRing dumps everything the ring still holds, ignoring the sequence
// markers entirely - that is the rate/integrity check's path (verify_rate.py),
// which needs a long uninterrupted stretch of samples and no start sequence.
// It exists because the ring is always full anyway: "dump what you have" needs
// no arm/stop handshake at all, which is one less piece of state than the
// start/stop commands it replaces.
static void dumpRecording(bool wholeRing) {
  // Window is [set - PREROLL_SAMPLES, recWritten), clamped to what the ring
  // still physically holds. The clamp is not defensive noise: without it, a
  // request reaching further back than MAX_REC_SAMPLES would read slots that
  // newer samples have already overwritten and emit them as if they were old
  // ones - a silently wrong capture, which is the failure mode this firmware
  // has already been bitten by once (see the CLOCKSTEP note above).
  uint32_t oldest = (recWritten > MAX_REC_SAMPLES) ? (recWritten - MAX_REC_SAMPLES) : 0;
  uint32_t from = wholeRing
                      ? oldest
                      : ((setSampleIdx > PREROLL_SAMPLES) ? (setSampleIdx - PREROLL_SAMPLES) : 0);
  bool truncated = (from < oldest);
  if (truncated) from = oldest;
  uint32_t n = recWritten - from;

  Serial.print("DUMP_START,");
  Serial.println(n);
  Serial.print("ON,");
  Serial.println(onCaptured ? onT : 0);
  Serial.print("SET,");
  Serial.println(setCaptured ? setT : 0);
  Serial.print("GO,");
  Serial.println(goCaptured ? goT : 0);
  // How much pre-"set" history the detector actually gets, in samples. It is
  // reported rather than assumed because TRUNCATED can shorten it.
  Serial.print("PREROLL,");
  Serial.println(wholeRing ? 0 : (setSampleIdx - from));
  Serial.print("TRUNCATED,");
  Serial.println(truncated ? 1 : 0);
  // Non-zero means the data-ready watchdog had to recover samples, so some
  // timestamps in this capture are only good to DRDY_STALL_TIMEOUT_US. The
  // host treats an unrecognised 2-field line as banner text, so this is safe
  // to add to the protocol.
  Serial.print("DROPPED,");
  Serial.println(droppedSamples);
  // Stamped into every capture so a CSV can always be checked after the
  // fact, instead of trusting that whoever recorded it used the right core.
  Serial.print("CLOCKSTEP,");
  Serial.println(clockStepUs);
  for (uint32_t i = from; i < recWritten; i++) {
    uint32_t slot = i % MAX_REC_SAMPLES;
    printCsvRow(recT[slot], recX[slot], recY[slot], recZ[slot]);
  }
  Serial.println("DUMP_END");
}

static void serviceSampling() {
  if (!imuReady) return;

  uint32_t t;
  if (drdyPending) {
    drdyPending = false;
    // Timestamp taken here rather than in the ISR (see drdyIsr) and, within
    // the main loop, before the I2C read rather than after it - so it is as
    // close to the sensor's latch instant as this core can measure. What is
    // left is the loop's latency in noticing the flag: small, and roughly
    // constant, so it is a calibratable bias rather than the 0-602 us of
    // random staleness the free-running v2 read carried.
    t = micros();
  } else if ((uint32_t)(micros() - lastSampleUs) > DRDY_STALL_TIMEOUT_US) {
    // Watchdog path - see DRDY_STALL_TIMEOUT_US. Reading clears the latched
    // line so edges resume. The sample is kept (it is real data) but its
    // instant is only known to within the timeout, so it is counted.
    t = micros();
    droppedSamples++;
  } else {
    return;
  }

  int16_t rawX, rawY, rawZ;
  readSampleRaw(&rawX, &rawY, &rawZ);
  lastSampleUs = micros();

  // The ring fills in every state, always - that IS the pre-roll. There is no
  // "start recording" step any more, only a "set" boundary marked in data
  // that is already being kept.
  uint32_t slot = recWritten % MAX_REC_SAMPLES;
  recT[slot] = t;
  recX[slot] = rawX;
  recY[slot] = rawY;
  recZ[slot] = rawZ;
  recWritten++;

  // Deteção causal em tempo real após a ordem de "SET"
  if (seqState == SEQ_WAIT_GO || seqState == SEQ_TAIL) {
    detector.update(t, rawToG(rawX), rawToG(rawY), rawToG(rawZ));
  }
  // Preview only while no sequence is running: during a start, serial stays
  // silent apart from the SEQ lines, so nothing can interleave with the run
  // or with the dump that follows it.
  if (seqState == SEQ_IDLE) {
    idleSampleIndex++;
    if (idleSampleIndex % STREAM_DECIMATE == 0) {
      // Wait for room for the whole row before printing, same reasoning as
      // v1: never let a blocked Serial.print stall the sample schedule.
      static const uint8_t ROW_MAX = 40;
      if ((int)Serial.availableForWrite() >= (int)ROW_MAX) {
        printCsvRow(t, rawX, rawY, rawZ);
      }
    }
  }
}

// Drives the buzzer and stamps the marker in one place. The timestamp is
// taken immediately AFTER tone() starts the drive signal, so it marks the
// electrical instant the transducer was driven. What separates that from the
// first pressure wave reaching the athlete is the buzzer's own latency - for
// an active buzzer this was a fixed 5-20 ms acoustic startup, constant and
// one-directional, hence a calibration constant rather than an error. With
// tone() driving a passive buzzer, that number has NOT been remeasured, and
// tone()'s own call latency and its effect on the DRDY-interrupt sample
// timing are UNVERIFIED on this core - check verify_rate.py / CLOCKSTEP /
// DROPPED across a real beep before trusting a reaction time from this build.
// Measure the acoustic latency the same way as before (GPIO + microphone on
// one time base) and subtract it; re-measure if the buzzer or BEEP_FREQ_HZ
// changes.
static void beep(uint32_t* markerOut, bool* capturedOut) {
  tone(BUZZER_PIN, BEEP_FREQ_HZ);
  uint32_t t = micros();
  buzzerOn = true;
  buzzerOffUs = t + BEEP_MS * 1000UL;
  *markerOut = t;
  *capturedOut = true;
}

// Nothing in the sequence may call delay(). Sampling is paced by the DRDY
// interrupt on a LEVEL-LATCHED line, so a loop that blocks past one sample
// period does not merely stutter - a missed edge is permanent until the
// watchdog recovers it (see DRDY_STALL_TIMEOUT_US). Hence every wait below is
// a micros() deadline checked from loop(), never a delay.
static void serviceBuzzer() {
  if (buzzerOn && (int32_t)(micros() - buzzerOffUs) >= 0) {
    noTone(BUZZER_PIN);
    buzzerOn = false;
  }
}


// ===========================================================================
// STA/LTA + AIC Logic
// ===========================================================================
static void setupDetectorFromPreroll() {
  detector.begin((float)ACCEL_ODR_HZ);

  uint32_t oldest = (recWritten > MAX_REC_SAMPLES) ? (recWritten - MAX_REC_SAMPLES) : 0;
  uint32_t from = (setSampleIdx > PREROLL_SAMPLES) ? (setSampleIdx - PREROLL_SAMPLES) : 0;
  if (from < oldest) from = oldest;

  uint32_t count = recWritten - from;
  if (count < detector.rest_n) return;

  float restX[300], restY[300], restZ[300];
  uint32_t rn = min((uint32_t)detector.rest_n, 300UL);
  for (uint32_t i = 0; i < rn; i++) {
    uint32_t slot = (from + i) % MAX_REC_SAMPLES;
    restX[i] = rawToG(recX[slot]);
    restY[i] = rawToG(recY[slot]);
    restZ[i] = rawToG(recZ[slot]);
  }
  if (!detector.calibrateGravity(restX, restY, restZ, rn)) return;

  for (uint32_t i = from + rn; i < recWritten; i++) {
    uint32_t slot = i % MAX_REC_SAMPLES;
    detector.update(recT[slot], rawToG(recX[slot]), rawToG(recY[slot]), rawToG(recZ[slot]));
  }
}

static void evaluateAndReportResults() {
  uint32_t prev_t = 0;
  for (int i = 0; i < detector.num_events; i++) {
    uint32_t ref_t = detector.events[i].t_us;
    float moved = 0.0f;
    bool ok = refineOnsetAIC(detector.events[i].trigger_t_us, prev_t,
                             recT, recX, recY, recZ, recWritten, MAX_REC_SAMPLES,
                             ACCEL_SCALE_G_PER_LSB, detector.g_hat, detector.events[i].b_h,
                             &ref_t, &moved);
    if (ok) {
      detector.events[i].t_us = ref_t;
      detector.events[i].aic_ok = true;
      detector.events[i].aic_moved_ms = moved;
    }
    prev_t = detector.events[i].t_us;
    classifyEvent(&detector.events[i], setT, goT);
  }

  DetectedEvent* primary = nullptr;
  for (int i = 0; i < detector.num_events; i++) {
    if (strcmp(detector.events[i].verdict, "pre-set (settling)") != 0 &&
        strcmp(detector.events[i].verdict, "rise into set (not judged)") != 0) {
      primary = &detector.events[i];
      break;
    }
  }

  Serial.println("\n================================================");
  Serial.println(">> ON-DEVICE DETECTION RESULT <<");
  if (primary != nullptr) {
    Serial.print("VERDICT       : "); Serial.println(primary->verdict);
    Serial.print("Reaction Time : "); Serial.print(primary->reaction_ms, 1); Serial.println(" ms");
    Serial.print("STA/LTA Trig  : "); Serial.print(primary->trigger_t_us); Serial.println(" us");
    Serial.print("AIC Refined   : "); Serial.print(primary->t_us);
    Serial.print(" us (shift: "); Serial.print(primary->aic_moved_ms, 1);
    Serial.println(primary->aic_ok ? " ms, AIC OK)" : " ms, KEPT THRESHOLD)");
    Serial.print("Horiz Energy  : "); Serial.print(primary->horiz_mg, 1); Serial.println(" mg");
  } else {
    Serial.println("No movement detected in the judged window.");
  }
  Serial.println("================================================\n");
}


static void startSequence() {
  if (seqState != SEQ_IDLE) return;
  uint32_t seed = micros();
  // Seeded from the instant a human pressed the button. An unseeded random()
  // replays the identical sequence after every reset, so by the third attempt
  // the athlete would know when "go" is coming - which would invalidate
  // precisely the measurement this exists to make.
  if (!randomSeeded) {
    randomSeed(seed);
    randomSeeded = true;
  }
  onCaptured = setCaptured = goCaptured = false;
  droppedSamples = 0;
  seqState = SEQ_WAIT_MARKS;
  seqDeadlineUs = seed + (uint32_t)random(MARKS_DELAY_MIN_MS, MARKS_DELAY_MAX_MS) * 1000UL;
  Serial.println("SEQ,armed");
}

static void abortSequence(const char* why) {
  if (seqState == SEQ_IDLE) return;
  seqState = SEQ_IDLE;
  noTone(BUZZER_PIN);
  buzzerOn = false;
  Serial.print("SEQ,abort,");
  Serial.println(why);
}

// Deadline comparisons are done on a signed difference so they stay correct
// across the ~71 minute micros() wrap.
static void serviceSequence() {
  serviceBuzzer();
  if (seqState == SEQ_IDLE) return;
  if ((int32_t)(micros() - seqDeadlineUs) < 0) return;

  switch (seqState) {
    case SEQ_WAIT_MARKS:
      beep(&onT, &onCaptured);
      seqState = SEQ_WAIT_SET;
      seqDeadlineUs = onT + (uint32_t)random(SET_DELAY_MIN_MS, SET_DELAY_MAX_MS) * 1000UL;
      Serial.println("SEQ,marks");
      break;
    case SEQ_WAIT_SET:
      beep(&setT, &setCaptured);
      // No buffer to start: the ring already holds the last ~13.9 s. "Set"
      // only records where in it the judged window begins.
      setSampleIdx = recWritten;
      seqState = SEQ_WAIT_GO;
      seqDeadlineUs = setT + (uint32_t)random(GO_DELAY_MIN_MS, GO_DELAY_MAX_MS) * 1000UL;
      Serial.println("SEQ,set");
      setupDetectorFromPreroll();
      break;
    case SEQ_WAIT_GO:
      beep(&goT, &goCaptured);
      seqState = SEQ_TAIL;
      seqDeadlineUs = goT + TAIL_AFTER_GO_MS * 1000UL;
      Serial.println("SEQ,go");
      break;
    case SEQ_TAIL:
      seqState = SEQ_IDLE;
      evaluateAndReportResults();
      dumpRecording(false);
      break;
    default:
      break;
  }
}

// Polled, not interrupt-driven: loop() already turns over at the sample rate,
// so a press is seen within ~1.2 ms, and a bouncing mechanical contact on an
// interrupt is a well-known way to flood a system.
static void serviceButton() {
  static bool lastLevel = HIGH;
  static uint32_t lastChangeMs = 0;
  bool level = (digitalRead(BUTTON_PIN) == HIGH);
  uint32_t now = millis();
  if (level == lastLevel) return;
  if (now - lastChangeMs < BUTTON_DEBOUNCE_MS) return;  // still bouncing
  lastChangeMs = now;
  lastLevel = level;
  if (!level) {
    // Falling edge = pressed (INPUT_PULLUP, button to GND). Pressing during a
    // running sequence aborts it, so a spoiled start can be thrown away
    // without reaching for a keyboard.
    if (seqState == SEQ_IDLE) startSequence();
    else abortSequence("button");
  }
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'b') {
    // Same effect as the button, so the whole sequence can be exercised
    // before any button is wired.
    if (seqState == SEQ_IDLE) startSequence();
    else abortSequence("serial");
  } else if (c == 'a') {
    abortSequence("serial");
  } else if (c == 'd') {
    // Dump whatever the ring holds right now, no sequence involved.
    if (seqState == SEQ_IDLE) dumpRecording(true);
  } else if (c == 'p') {
    if (!imuReady) {
      Serial.println("IMU not ready");
    } else {
      int16_t rawX, rawY, rawZ;
      readSampleRaw(&rawX, &rawY, &rawZ);
      uint32_t t = micros();
      Serial.print("t_us="); Serial.print(t);
      Serial.print("  x="); Serial.print(rawToG(rawX), 4);
      Serial.print("  y="); Serial.print(rawToG(rawY), 4);
      Serial.print("  z="); Serial.println(rawToG(rawZ), 4);
    }
  }
}

void loop() {
  serviceSampling();
  serviceSequence();
  serviceButton();
  handleSerial();
}
