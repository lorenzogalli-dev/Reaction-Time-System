#include "Pins.h"
#include "Bmi270.h"
#include "StartDetector.h"
#include "AicPicker.h"
#include "Display.h"
#include "Storage.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "soc/gpio_sig_map.h"

static StartDetector detector;

// ---------------------------------------------------------------------------
// AlgorithmRealTimeESP32 - THE SAME FIRMWARE, PORTED TO AN ESP32-C3 SUPERMINI
// WITH AN EXTERNAL BMI270 (2026-10-07). The XIAO original is untouched in
// ../AlgorithmRealTime. What changed, and only that:
//   - IMU: onboard LSM6DS3TR-C -> BMI270 breakout on I2C (Bmi270.h), 800 Hz
//     instead of 833 (the BMI270 has no 833), +/-16 g, same 0.488 mg/LSB.
//   - Data-ready timestamp taken IN the ISR: micros() is esp_timer here, 1 us
//     and ISR-safe, so the loop's latency in noticing the flag is gone.
//   - Buzzer: nRF PWM -> LEDC + GPIO-matrix inversion (still two register
//     writes at the instant of the beep). Pins rest HIGH, not LOW.
//   - Display: mbed::SPI -> SPIClass. Storage: QSPI -> internal flash.
//   - Off: nRF System OFF -> ESP32 light sleep, woken by the button (on
//     GPIO9, which cannot wake deep sleep), then a restart.
//   - Dump header gains SENSOR,bmi270 and ODR,800 so a capture says where it
//     came from.
//   - Samples come out of the BMI270's FIFO, not its data registers, and
//     INT1 is only a clock (2026-10-07, after the first block starts lost 63
//     samples at "set" and 3 every 2 s to loop stalls). See serviceSampling().
// StartDetector.h and AicPicker.h are byte-identical to the XIAO's. Wiring,
// build and what still has to be measured: INFO.md in this folder.
// Below, the XIAO history is kept as it was; "D0", "QSPI", "System OFF" in it
// mean the XIAO.
// ---------------------------------------------------------------------------
//
// AlgorithmRealTime - no-BLE accelerometer streamer + full-rate buffered
// recorder with the start detector running on the board, XIAO nRF52840 Sense.
// Named AccelStream through v4; the version history below uses that name.
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
// v5 (2026-09-29): STANDALONE - DISPLAY, ONE BUTTON, FLASH
// --------------------------------------------------------
// The board no longer needs a terminal. A 1.77" TFT (Display.h, wiring there)
// shows every step, the one button does everything, and every start is kept
// on the QSPI flash (Storage.h) to be pulled later. Serial output is unchanged
// and still complete - with no terminal open the core simply drops it.
//
//   off  --hold 1.5 s (beep)-->  READY  --press-->  3, 2, 1  -->  "On your marks" (beep)
//        --20-25 s-->  "Set" (beep)  --armed + 0.7-1.5 s-->  "Go" (beep)
//        --1 s-->  RESULT (reaction time, or FALSE START with its time)
//   RESULT --press--> READY.   During a start, two presses within 2 s abort
//   it (the first only shows "press again to cancel"), back to READY.
//   Hold 1.5 s anywhere: beep, "Nice session today!", then System OFF (~µA).
//   A press wakes the chip, and it only stays on if held the full 1.5 s.
//   Plugging in / uploading / reset boots straight to READY.
//
// Sequence delays other than the countdown stay randomised so the rhythm
// cannot be learned:
//   button -> 3 s countdown -> beep "on your marks" -> 20-25 s -> beep "set"
//          -> armed + 0.7-1.5 s -> beep "go" -> 1 s -> result, store, dump
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
// The button (BUTTON_PIN to GND) starts a sequence; two presses within
// ABORT_CONFIRM_MS during one abort it. Nothing is printed during a sequence except its progress lines
// SEQ,armed / SEQ,marks / SEQ,set / SEQ,gate,<ms>,<mg>,<how> / SEQ,go /
// SEQ,abort,<why>.
//   SEQ,armed  the button was pressed; a sequence is starting.
//   SEQ,gate   the athlete has been measured still and the judged window is
//              open - <ms> after "set", at <mg> of movement, <how> being
//              "still" (measured) or "cap" (gave up waiting, see
//              DET_ARM_CAP_MS). "go" is scheduled from this instant.
// 'b' does exactly what the button does - so the whole sequence can be
//     exercised with no button wired yet.
// 'a' aborts a running sequence.
// 'd' dumps the whole ring immediately (no sequence, no markers) - this is
//     what verify_rate.py uses for its rate/integrity check.
// 'p' prints one immediate reading, independent of the above.
// 'L' lists the runs stored on flash: FILES,<n>,<free slots> then one
//     FILE,<id>,<samples> each, oldest first, then FILES_END.
// 'F' replays every stored run as a normal DUMP_START..DUMP_END block (with a
//     STORED,<id> line), then FETCH_END. pull_captures.py drives this.
// 'X' erases every stored run: ERASED,<n>.
// 'T<unix seconds>' sets the time runs are stamped with (WALLCLOCK), until
//     the next power-off. capture.py and pull_captures.py send it.
// 'E' diagnostic: erases the EMPTY slots and reports erase timings.
//
// One second after "go" the board dumps the window by itself:
//   DUMP_START,<n>
//   ON,<t_us or 0>        beep 1, "on your marks"
//   SET,<t_us or 0>       beep 2, "set"
//   GO,<t_us or 0>        beep 3, "go" - the reaction-time reference
//   PREROLL,<n>           samples present BEFORE the "set" instant
//   TRUNCATED,<0|1>       1 = the ring wrapped, the pre-roll is short
//   DROPPED,<n>           samples with no data-ready stamp of their own
//                         (ESP32: interpolated/extrapolated on the sensor's
//                         period, see serviceSampling)
//   CLOCKSTEP,<us>        measured micros() resolution of this build
//   GAPS,<n>              sample intervals over 1.5x nominal since the button
//   MAXGAP,<us>           the longest of them
//   STORED,<id>           this run's id on the flash (absent: not stored)
//   WALLCLOCK,<unix s>    when the start happened (absent: board had no time)
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
//
// ESP32 port: 800 Hz, the BMI270 step nearest 833. Bmi270.h writes the ODR
// code and reads it back; the two must agree, hence the assert.
static const uint16_t ACCEL_ODR_HZ = 800;
static_assert(ACCEL_ODR_HZ == 800 && BMI_ACC_ODR_800 == 0x0B,
              "Bmi270.h configures 800 Hz; change both together");
static const uint32_t SAMPLE_PERIOD_US = 1000000UL / ACCEL_ODR_HZ;

// The IMU's data-ready line: BMI270 INT1 -> IMU_INT1_PIN (Pins.h).

// XIAO: DRDY was level-latched and a missed edge stopped sampling until a
// watchdog read. ESP32: the BMI270's non-latched data-ready keeps pulsing
// whether or not anything is read (the 2026-10-07 captures show fresh edges
// right after an 80 ms stall), and the samples come from the FIFO, so there
// is no edge to lose. What is left of the watchdog is in serviceSampling():
// a frame with no stamp for 3 periods gets an extrapolated one.

// +/-16 g: a real push-off rigidly mounted on the block is expected around
// 1-3 g, but a 2026-09-07 bench test (a hard hand hit, well above a real
// push-off) clipped hard at +/-8g on two axes simultaneously for ~200ms.
// 16g costs almost nothing here - LSB resolution only drops from 0.244 mg
// to 0.488 mg, still far below the ~20 mg detection threshold - so it's
// cheap margin against clipping the one part of the signal that matters.
static const uint8_t ACCEL_RANGE_G = 16;

// BMI270 at +/-16 g: full scale over 32768 counts, 2048 LSB/g (0.488 mg/LSB,
// against the LSM6DS3's 0.488 at 16 g - the thresholds keep their meaning).
static_assert(ACCEL_RANGE_G == 16 && BMI_ACC_RANGE_16G == 0x03,
              "Bmi270.h configures +/-16 g; change both together");
static const float ACCEL_SCALE_G_PER_LSB = (float)ACCEL_RANGE_G / 32768.0f;

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
//
// ESP32 port: BUTTON_PIN, BUZZER_PIN and BUZZER_PIN_B are in Pins.h.
// PASSIVE PIEZO buzzer, driven ANTIPHASE from two pins by the nRF52840's PWM
// peripheral. Both halves of that sentence are deliberate:
//
//   passive  - no internal oscillator, so a steady digitalWrite() level (what
//              this used to be) produces one click and then silence.
//   antiphase - the two pins swing in opposite directions, so the element sees
//              6.6 Vpp instead of the 3.3 Vpp a single pin against GND can
//              give it. That is +6 dB for the cost of one GPIO and no
//              components, and it is safe *because* the part is a piezo: a
//              piezo is a capacitor and passes no DC, so neither pad ever
//              sources steady current. Do NOT wire a magnetic buzzer (a
//              ~16 ohm coil) this way - it would double a current the pad
//              already cannot supply.
//   PWM      - not tone(). See beep() for why that matters to the measurement.
//
// WIRING: buzzer (+) -> GPIO1, buzzer (-) -> GPIO2. No connection to GND.
// 4000 Hz is this buzzer's measured resonance, not a round number. A frequency
// sweep on 2026-09-11 (Arduino/BuzzerSweep) found a clear peak at 4000 Hz with
// weaker secondary modes at 1600 and 4700; the previous 3000 Hz sat off the
// peak, which is most of why the beep was too quiet to use at the track. The
// same sweep identified the part as a PIEZO - toggling the nRF52840 pad to
// high drive (5 mA) changed the level not at all, so it is voltage-driven and
// capacitive, not a current-driven magnetic coil.
//
// Re-run the sweep if the buzzer is ever replaced: resonance is a property of
// the part, and a Q of ~20 makes the peak only a couple of hundred Hz wide.
static const unsigned int BEEP_FREQ_HZ = 4000;

static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t BEEP_MS = 100;

// Randomised so the athlete cannot learn the rhythm - the reason this matters
// is that an anticipated "go" is exactly what a reaction time must not
// measure. Arduino's random(a, b) is inclusive of a, exclusive of b.
// Button -> "on your marks" is a fixed, visible 3-2-1 countdown since v5 (it
// was a random 2-3 s). Nobody reacts to "on your marks", so nothing is lost.
static const uint32_t COUNTDOWN_MS = 3000;
// Held this long: power off (or, just woken, stay on). Both end with a beep.
static const uint32_t LONG_PRESS_MS = 1500;
// A start is aborted by a second press within this long of the first: one
// stray press on a start that is running must not throw it away.
static const uint32_t ABORT_CONFIRM_MS = 2000;
static const uint32_t POWER_BEEP_MS = 150;
// Back to 20-25 s on 2026-09-11, the original v4 value (it had been shortened
// to 10-15 s to make bench iteration less tedious). This is the realistic
// "on your marks" hold, and it costs nothing: the sample ring fills
// continuously in every state, so a pause that overwrites it several times
// over is expected - the dump window is measured backwards from "set", not
// forwards from here.
static const long SET_DELAY_MIN_MS   = 20000, SET_DELAY_MAX_MS   = 25001;
// set -> go is no longer a blind random from "set". The firmware now waits for
// the athlete to be measured still (StartDetector's arming gate) and fires this
// long AFTER that instant, the way a starter holds the gun until the field is
// steady. What the athlete can anticipate is therefore this window, not the
// time since "set" - which is why it, and not the old 2.2-3.0 s, is the
// unpredictability budget. 700 ms of spread, chosen against the measured
// set->go it produces (median 2.29 s, 17% over 3 s) rather than in the
// abstract; widen it if athletes start anticipating, at a cost in waiting.
//
// Raised from 500-1200 on 2026-09-14. With the athlete settling at set+1.0 s -
// which is what the 09-11 captures show - the old window put set->go at a
// median of 1.87 s and as low as 1.50 s. A real starter holds "set" for about
// 1.5-2.0 s, so that was at the short end of it. 700-1500 moves the median to
// 2.12 s with a 1.70 s floor, and widens the unpredictability budget from
// 700 ms to 800 ms. Above ~1000 ms of minimum the ceiling below starts being
// hit often enough to matter (19% on the 09-09 captures against 8% here).
static const long GO_AFTER_ARM_MIN_MS = 700,  GO_AFTER_ARM_MAX_MS = 1501;

// Hard ceiling on set -> go however late the arming lands. Without it a slow
// settle plus the random could reach 4.5 s, and a sprinter held that long in
// the set position is a worse measurement, not a safer one.
//
// RANDOMISED, and that is not decoration. A fixed ceiling fires "go" at
// exactly set + 3.5 s every time it clamps, which hands the athlete a
// perfectly predictable instant in precisely the case where they were slow to
// settle - and being slow to settle is common enough (8% of attempts at the
// current window, and rising with it) for that to be learnable. A ceiling that
// is itself unpredictable cannot be counted on.
static const long SET_TO_GO_CAP_MIN_MS = 3400, SET_TO_GO_CAP_MAX_MS = 3601;

enum SeqState {
  SEQ_IDLE,        // nothing running; idle preview streams
  SEQ_WAIT_MARKS,  // button pressed, waiting to sound "on your marks"
  SEQ_WAIT_SET,    // "on your marks" sounded, waiting to sound "set"
  SEQ_WAIT_ARM,    // "set" sounded - waiting for the athlete to go still
  SEQ_WAIT_GO,     // armed - the judged window is open, "go" is scheduled
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

// What the board decided, kept so dumpRecording() can write it into the CSV.
// The verdict is a property of THIS run, taken by the firmware at the instant
// it had the data - not something a host should have to re-derive later. See
// the note on ARM in dumpRecording().
// Drawn once per attempt, at "set": the ceiling has to be a single instant for
// the whole run, not re-rolled on every poll of the arming gate.
static uint32_t goCapUs = 0;

static const char* lastVerdict = nullptr;
static float lastReactionMs = 0.0f;
static uint32_t lastOnsetUs = 0;

static bool imuReady = false;
static uint32_t idleSampleIndex = 0;

// Every data-ready edge, stamped by the ISR into a queue: one stamp per
// sample the BMI270 wrote to its FIFO. The loop pairs them up oldest first.
// Counters are free-running; the slot is the counter mod STAMP_Q.
static const uint32_t STAMP_Q = 1024;              // power of 2, ~1.28 s
static volatile uint32_t stampQ[STAMP_Q];
static volatile uint32_t stampHead = 0;            // ISR only
static uint32_t stampTail = 0;                     // loop only

// Frames already read out of the FIFO, waiting for their stamp (the ISR runs
// a few us after the frame lands, so the newest one can be read first).
static const uint16_t PEND_Q = 1024;
static int16_t pendX[PEND_Q], pendY[PEND_Q], pendZ[PEND_Q];
static uint16_t pendHead = 0, pendCount = 0;

// Timestamp given to the last sample handed on, and the sensor's period
// measured from consecutive stamps, in 1/256 us (starts at nominal).
static uint32_t lastStampT = 0;
static uint32_t periodQ8 = 0;
static uint32_t lastFifoPollUs = 0;
// Samples handed on in one go, worst case since the button: how long the
// loop stalled, and what the FIFO absorbed. Printed with the verdict.
static uint32_t maxBacklog = 0;
// FIFO or stamp queue overran and had to be restarted: samples were lost.
static uint32_t fifoResyncs = 0;

// Samples whose true instant is unknown because the watchdog had to recover
// them rather than an edge delivering them. Reported with every dump: a
// capture with a non-zero count here is not clean data, and silently
// discarding that fact is how a timing bug survives to the next person.
static uint32_t droppedSamples = 0;

// Intervals between consecutive samples longer than 1.5x nominal, counted from
// the button press. The watchdog count above cannot see these: a loop blocked
// for 20 ms loses ~16 samples but recovers with one clean edge afterwards. v5
// adds a display to the loop, so this is how a draw that stalls sampling
// would show up in the capture instead of passing silently.
static uint32_t gapCount = 0, maxGapUs = 0, prevSampleT = 0;

// Button-press instant; the countdown digits are timed from it.
static uint32_t seqStartUs = 0;
// Unix time at millis() == 0, set by the PC with 'T' (capture.py and
// pull_captures.py send it on connect). 0 = unknown. RAM only: the board has
// no clock that survives power-off, so a session started on a power bank
// records its runs without a date rather than with a wrong one.
static uint32_t epochAtBoot = 0;
// Id the last finished run got on the flash, 0 if it was not stored.
static uint32_t lastStoredId = 0;
// READY or RESULT, when no sequence is running.
static bool showingResult = false;
// True while the press that powered the board on is still being held: its
// release must not also start a sequence.
static bool buttonSwallow = false;

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

// ESP32 port: this one DOES timestamp. The XIAO's could not (micros() in an
// ISR fell back to a 1024 Hz tick on that core, see below); here micros() is
// esp_timer_get_time(), 1 us and ISR-safe, so the stamp is taken a few us
// after the BMI270 raised INT1 instead of whenever the loop got round to the
// flag. Every edge is queued, so a busy loop no longer loses any: the sample
// itself waits in the BMI270's FIFO and the stamp waits here.
//
// XIAO note, kept for the history: "Deliberately does NOT timestamp. micros()
// called from interrupt context on this core does not have microsecond
// resolution - it falls back to a 1024 Hz counter, so every timestamp lands on
// a ~976.6 us grid."
static void IRAM_ATTR drdyIsr() {
  uint32_t h = stampHead;
  stampQ[h & (STAMP_Q - 1)] = (uint32_t)esp_timer_get_time();
  stampHead = h + 1;
}

// Lines the FIFO and the stamp queue up: right after an edge, empty both, so
// the next frame and the next stamp belong to the same edge. Off by one here
// would put every timestamp one period (1.25 ms) late, so the window is
// checked rather than assumed: the flush must land before the next edge.
// Also the recovery after either queue overran (a loop stalled > ~1.2 s,
// which only the flash write and the dump do, both outside a start).
static bool fifoResync() {
  pendHead = pendCount = 0;
  if (periodQ8 == 0) periodQ8 = SAMPLE_PERIOD_US << 8;
  for (int attempt = 0; attempt < 20; attempt++) {
    uint32_t h0 = stampHead;
    uint32_t w0 = micros();
    while (stampHead == h0) {
      if ((uint32_t)(micros() - w0) > 20000) goto noEdges;
    }
    uint32_t te = stampQ[(stampHead - 1) & (STAMP_Q - 1)];
    delayMicroseconds(30);          // the frame is in the FIFO by now
    bmiFifoFlush();
    noInterrupts();
    bool inTime = (uint32_t)(micros() - te) < SAMPLE_PERIOD_US - 200;
    if (inTime) stampTail = stampHead;
    interrupts();
    if (inTime) {
      lastStampT = te;
      lastFifoPollUs = micros();
      return true;
    }
  }
noEdges:
  // INT1 dead (unwired?): samples still flow, on extrapolated timestamps,
  // every one counted in DROPPED.
  bmiFifoFlush();
  stampTail = stampHead;
  lastStampT = micros();
  lastFifoPollUs = lastStampT;
  return false;
}

// Survives esp_restart(), not a power-up (whose garbage is rejected by the
// magic value): tells setup() that the restart is a switch-on from "off"
// (see enterSystemOff).
static RTC_NOINIT_ATTR uint32_t wakeMagic;
static const uint32_t WAKE_MAGIC = 0x50524F31;   // "PRO1"

void setup() {
  // Why this boot happened, read before anything else. A restart from
  // enterSystemOff = the button switched the board back on (hold and beep are
  // already done); everything else - plugging in, an upload, the reset
  // button - is a deliberate "on" too, just not from a button.
  bool wokeFromOff = (esp_reset_reason() == ESP_RST_SW && wakeMagic == WAKE_MAGIC);
  wakeMagic = 0;
  // Pads frozen for the sleep (enterSystemOff) are handed back first.
  gpio_hold_dis((gpio_num_t)TFT_BL_PIN);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Backlight driven low before anything else: after the reset its pin floats,
  // and through the module's Q1 a floating pin can light the panel.
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, LOW);
  buzzerInit();
  tftBegin();

  // USB Serial/JTAG (HWCDC): the baud rate is ignored, USB sets the speed.
  // A larger TX buffer keeps the dump flowing in bigger USB packets.
  Serial.setTxBufferSize(4096);
  // Bounded blocking. Plugged into a computer with no terminal open, the C3's
  // USB keeps saying "connected" and a full buffer would block each write for
  // 20 x this timeout; the XIAO's core dropped the data instead. See also
  // serialRoomFor() on the dump path.
  Serial.setTxTimeoutMs(5);
  Serial.begin(921600);
  // Native USB CDC: Serial only becomes true once a host opens the port.
  // Bounded wait so the banner below is not lost right after an upload - and
  // skipped after a wake, where the person is holding the board, not a laptop.
  if (!wokeFromOff) {
    screenClearAll();
    queueTextCentered(40, 3, C_ORANGE, "ProStart");
    queueTextCentered(80, 1, C_GREY, "starting...");
    displayPump(0xFFFFFFFFUL);
    unsigned long waitStart = millis();
    while (!Serial && millis() - waitStart < 3000) delay(10);
  }

  imuReady = bmiBegin();
  if (!imuReady) {
    Serial.print("IMU error - BMI270: ");
    Serial.print(bmiError);
    Serial.print(" (0x");
    Serial.print(bmiErrorValue, HEX);
    Serial.println(")");
  } else {
    pinMode(IMU_INT1_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(IMU_INT1_PIN), drdyIsr, RISING);
    if (!fifoResync()) Serial.println("WARNING: no data-ready edges on INT1 - timestamps are extrapolated");

    Serial.print("IMU OK - BMI270 accel ");
    Serial.print(ACCEL_ODR_HZ);
    Serial.print(" Hz (data-ready on INT1, GPIO");
    Serial.print(IMU_INT1_PIN);
    Serial.print("), +/-");
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
    Serial.println("!! Not expected on the esp32 core (esp_timer is 1 us): check the build.");
  }

  if (storageBegin()) {
    Serial.print("flash: JEDEC ");
    Serial.print(jedecId, HEX);
    Serial.print(", ");
    Serial.print(storageCount());
    Serial.print("/");
    Serial.print(SLOT_COUNT);
    Serial.print(" run slots used, next id ");
    Serial.println(nextRunId);
  } else {
    Serial.print("!! flash: ");
    Serial.print(flashErrStep);
    Serial.println(" - runs will NOT be stored");
  }

  Serial.println("Ready. Idle preview streaming. 'p' one reading.");
  Serial.print("Start sequence: press the button on GPIO");
  Serial.print(BUTTON_PIN);
  Serial.print(" (or send 'b'); buzzer antiphase on GPIO");
  Serial.print(BUZZER_PIN);
  Serial.print("/GPIO");
  Serial.print(BUZZER_PIN_B);
  Serial.print(" at ");
  Serial.print(BEEP_FREQ_HZ);
  Serial.println(" Hz");
  Serial.println("  button -> 3-2-1 -> MARKS -> 20-25s -> SET -> armed+0.7-1.5s -> GO -> 1s -> dump");
  Serial.println("  'a' or two presses within 2 s abort. 'd' dumps the ring as-is.");
  Serial.println("  Hold the button 1.5 s to power off. 'L' list / 'F' fetch / 'X' erase stored runs.");

  // Still holding the press that switched it on: its release is not a command.
  buttonSwallow = (digitalRead(BUTTON_PIN) == LOW);
  showReady();
}

// Single I2C transaction: X/Y/Z occupy six consecutive registers
// (OUTX_L_XL..OUTZ_H_XL), so one readRegisterRegion() call replaces the
// three separate readFloatAccelX/Y/Z() calls (six transactions) the first
// version used - this is what actually removes the per-sample I2C overhead.
// (ESP32: same idea on the BMI270, ACC_X_LSB..ACC_Z_MSB, see Bmi270.h.)
static void readSampleRaw(int16_t* rawX, int16_t* rawY, int16_t* rawZ) {
  bmiReadAccelRaw(rawX, rawY, rawZ);
}

static inline float rawToG(int16_t raw) {
  return (float)raw * ACCEL_SCALE_G_PER_LSB;
}

// Waits until the USB buffer can take n more bytes. False if nobody has drained
// it for SERIAL_STALL_MS: then no host is reading, and a 6000-row dump would
// otherwise hold the board for minutes (see Serial.setTxTimeoutMs in setup).
static const uint32_t SERIAL_STALL_MS = 200;
static bool serialRoomFor(int n) {
  uint32_t t0 = millis();
  while (Serial.availableForWrite() < n) {
    if (millis() - t0 > SERIAL_STALL_MS) return false;
    delay(1);
  }
  return true;
}
static const int CSV_ROW_MAX = 48;

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
// The window a dump covers, [from, recWritten), and the header describing it.
static void buildLiveMeta(bool wholeRing, CaptureMeta* m, uint32_t* fromOut) {
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

  memset(m, 0, sizeof(*m));
  m->magic = CAPTURE_MAGIC;
  m->version = CAPTURE_VERSION;
  m->metaSize = sizeof(CaptureMeta);
  m->n = recWritten - from;
  m->onT = onCaptured ? onT : 0;
  m->setT = setCaptured ? setT : 0;
  m->goT = goCaptured ? goT : 0;
  m->preroll = wholeRing ? 0 : (setSampleIdx - from);
  m->truncated = truncated ? 1 : 0;
  m->dropped = droppedSamples;
  m->gaps = gapCount;
  m->maxGapUs = maxGapUs;
  m->clockStep = clockStepUs;
  m->armValid = detector.gate_open ? 1 : 0;
  m->armT = detector.gate_t_us;
  m->armMg = detector.gate_peak_mg;
  m->armCapped = detector.gate_capped ? 1 : 0;
  if (lastVerdict != nullptr) {
    m->hasVerdict = 1;
    strncpy(m->verdict, lastVerdict, sizeof(m->verdict) - 1);
    m->rtMs = lastReactionMs;
    m->onsetUs = lastOnsetUs;
  }
  // To the second, taken at dump/store time (~1-2 s after "go").
  m->wallclock = epochAtBoot ? epochAtBoot + millis() / 1000 : 0;
  *fromOut = from;
}

static void emitDumpHeader(const CaptureMeta& m) {
  Serial.print("DUMP_START,");
  Serial.println(m.n);
  // Which hardware took it: this board's captures must never be mistaken for
  // the XIAO's (other sensor, other filter delay, other rate).
  Serial.println("SENSOR,bmi270");
  Serial.print("ODR,");
  Serial.println(ACCEL_ODR_HZ);
  Serial.print("ON,");
  Serial.println(m.onT);
  Serial.print("SET,");
  Serial.println(m.setT);
  Serial.print("GO,");
  Serial.println(m.goT);
  // How much pre-"set" history the detector actually gets, in samples. It is
  // reported rather than assumed because TRUNCATED can shorten it.
  Serial.print("PREROLL,");
  Serial.println(m.preroll);
  Serial.print("TRUNCATED,");
  Serial.println(m.truncated);
  // Non-zero means some samples had no data-ready stamp of their own and got
  // one interpolated on the sensor's period (see serviceSampling). The
  // host treats an unrecognised 2-field line as banner text, so this is safe
  // to add to the protocol.
  Serial.print("DROPPED,");
  Serial.println(m.dropped);
  // Stamped into every capture so a CSV can always be checked after the
  // fact, instead of trusting that whoever recorded it used the right core.
  Serial.print("CLOCKSTEP,");
  Serial.println(m.clockStep);
  Serial.print("GAPS,");
  Serial.println(m.gaps);
  Serial.print("MAXGAP,");
  Serial.println(m.maxGapUs);
  if (m.id != 0) {
    Serial.print("STORED,");
    Serial.println(m.id);
  }
  if (m.wallclock != 0) {
    Serial.print("WALLCLOCK,");
    Serial.println(m.wallclock);
  }
  // ARM is a marker in exactly the sense ON/SET/GO are: an instant the board
  // decided, on the board's own clock. It is written down rather than left to
  // be recomputed because re-deriving it from the samples is not guaranteed to
  // land on the same instant - the board works in float32 and numpy in
  // float64, and on a capture where the signal hovers at the stillness limit a
  // 0.3 mg difference moved the arming by 1.5 s. Reading the decision beats
  // reconstructing it, for the same reason the "go" beep is timestamped rather
  // than inferred.
  if (m.armValid) {
    Serial.print("ARM,");     Serial.println(m.armT);
    Serial.print("ARMMG,");   Serial.println(m.armMg, 2);
    Serial.print("ARMCAP,");  Serial.println(m.armCapped);
  }
  // And the verdict itself. Without this the board's answer lives only in the
  // terminal: reopen the capture tomorrow and there is nothing to compare the
  // offline analysis against, which is precisely the comparison that catches a
  // firmware and a bench tool drifting apart.
  if (m.hasVerdict) {
    Serial.print("VERDICT,");  Serial.println(m.verdict);
    Serial.print("RTMS,");     Serial.println(m.rtMs, 3);
    Serial.print("ONSET,");    Serial.println(m.onsetUs);
  }
}

static void dumpRecording(bool wholeRing, uint32_t storedId) {
  CaptureMeta m;
  uint32_t from;
  buildLiveMeta(wholeRing, &m, &from);
  m.id = storedId;
  emitDumpHeader(m);
  for (uint32_t i = from; i < recWritten; i++) {
    if (!serialRoomFor(CSV_ROW_MAX)) return;   // no host: the run is on flash anyway
    uint32_t slot = i % MAX_REC_SAMPLES;
    printCsvRow(recT[slot], recX[slot], recY[slot], recZ[slot]);
  }
  Serial.println("DUMP_END");
}

// Writes the same window dumpRecording(false) prints to the flash. Returns the
// run id, 0 on failure. Blocks for ~1 s (mostly sector erases): called on the
// result screen, where nothing is being measured and the display is drawn.
static uint32_t storeRun() {
  if (!storageReady) return 0;
  CaptureMeta m;
  uint32_t from;
  buildLiveMeta(false, &m, &from);
  if (m.n > SLOT_MAX_SAMPLES) {
    // Never expected (a start dumps ~6.5 s, a slot holds ~9.5 s); if it
    // happens, keep the end - "go" and the push-off - and say so.
    uint32_t cut = m.n - SLOT_MAX_SAMPLES;
    from += cut;
    m.n = SLOT_MAX_SAMPLES;
    m.preroll = (m.preroll > cut) ? m.preroll - cut : 0;
    m.truncated = 1;
  }
  m.id = nextRunId;

  uint32_t slot = storagePickSlot();
  uint32_t base = slot * SLOT_BYTES;
  uint32_t bytes = sizeof(CaptureMeta) + m.n * sizeof(SampleRec);
  slotId[slot] = 0;   // whatever was there is gone from here on
  if (!flashErase(base, (bytes + SECTOR_BYTES - 1) / SECTOR_BYTES * SECTOR_BYTES)) return 0;

  // Samples first, header last (see Storage.h).
  SampleRec* chunk = (SampleRec*)flashBuf;
  const uint32_t perChunk = sizeof(flashBuf) / sizeof(SampleRec);
  uint32_t addr = base + sizeof(CaptureMeta);
  uint32_t k = 0;
  for (uint32_t i = 0; i < m.n; i++) {
    uint32_t slotIdx = (from + i) % MAX_REC_SAMPLES;
    chunk[k].t = recT[slotIdx];
    chunk[k].x = recX[slotIdx];
    chunk[k].y = recY[slotIdx];
    chunk[k].z = recZ[slotIdx];
    if (++k == perChunk || i + 1 == m.n) {
      uint32_t len = k * sizeof(SampleRec);
      memset((uint8_t*)flashBuf + len, 0xFF, sizeof(flashBuf) - len);   // word padding
      if (!flashWrite(addr, flashBuf, len)) return 0;
      addr += len;
      k = 0;
    }
  }
  if (!flashWrite(base, &m, sizeof(m))) return 0;

  // Read the header back: a write that "succeeded" into a flash that ignored
  // it (write-protected, wrong mode) must not be reported as saved.
  CaptureMeta check;
  if (!flashRead(base, &check, sizeof(check))) return 0;
  if (check.magic != CAPTURE_MAGIC || check.id != m.id) {
    flashFail("verify", (int)check.magic, base);
    return 0;
  }
  slotId[slot] = m.id;
  slotN[slot] = m.n;
  nextRunId++;
  return m.id;
}

// Replays one stored run as a normal dump. False if it could not be read.
static bool emitStoredRun(uint32_t slot) {
  uint32_t base = slot * SLOT_BYTES;
  CaptureMeta m;
  if (!flashRead(base, &m, sizeof(m)) || m.magic != CAPTURE_MAGIC ||
      m.metaSize != sizeof(CaptureMeta) || m.n > SLOT_MAX_SAMPLES) {
    return false;
  }
  emitDumpHeader(m);
  SampleRec* chunk = (SampleRec*)flashBuf;
  const uint32_t perChunk = sizeof(flashBuf) / sizeof(SampleRec);
  uint32_t addr = base + sizeof(CaptureMeta);
  for (uint32_t done = 0; done < m.n; ) {
    uint32_t k = (m.n - done < perChunk) ? (m.n - done) : perChunk;
    if (!flashRead(addr, flashBuf, k * sizeof(SampleRec))) break;   // count mismatch says so
    for (uint32_t i = 0; i < k; i++) {
      if (!serialRoomFor(CSV_ROW_MAX)) return false;
      printCsvRow(chunk[i].t, chunk[i].x, chunk[i].y, chunk[i].z);
    }
    addr += k * sizeof(SampleRec);
    done += k;
  }
  Serial.println("DUMP_END");
  return true;
}

// One sample, timestamped, into the ring and the detector.
static void storeSample(uint32_t t, int16_t rawX, int16_t rawY, int16_t rawZ) {
  if (seqState != SEQ_IDLE) {
    uint32_t dt = t - prevSampleT;
    if (dt > SAMPLE_PERIOD_US + SAMPLE_PERIOD_US / 2) {
      gapCount++;
      if (dt > maxGapUs) maxGapUs = dt;
    }
  }
  prevSampleT = t;

  // The ring fills in every state, always - that IS the pre-roll. There is no
  // "start recording" step any more, only a "set" boundary marked in data
  // that is already being kept.
  uint32_t slot = recWritten % MAX_REC_SAMPLES;
  recT[slot] = t;
  recX[slot] = rawX;
  recY[slot] = rawY;
  recZ[slot] = rawZ;
  recWritten++;

  // Causal, real-time detection once the "set" command has been given. Fed the
  // same samples the ring stores, so the on-device verdict and the dumped CSV
  // can never disagree about what the detector saw.
  if (seqState == SEQ_WAIT_ARM || seqState == SEQ_WAIT_GO || seqState == SEQ_TAIL) {
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

// Pulls whatever the FIFO holds into the pending queue. False if it overran.
static bool pullFifo() {
  int32_t n = bmiFifoFrames();
  lastFifoPollUs = micros();
  if (n <= 0) return true;
  // Full means the oldest frames were overwritten: frames and stamps no
  // longer line up, and the only honest fix is to start again.
  if ((uint32_t)n >= BMI_FIFO_MAX_FRAMES - 1) return false;
  while (n > 0 && pendCount < PEND_Q) {
    uint16_t room = PEND_Q - pendCount;
    uint16_t k = (uint16_t)min((int32_t)BMI_FIFO_READ_FRAMES, n);
    if (k > room) k = room;
    int16_t x[BMI_FIFO_READ_FRAMES], y[BMI_FIFO_READ_FRAMES], z[BMI_FIFO_READ_FRAMES];
    if (!bmiFifoRead(k, x, y, z)) return true;     // bus hiccup: try next loop
    for (uint16_t i = 0; i < k; i++) {
      uint16_t slot = (pendHead + pendCount) % PEND_Q;
      pendX[slot] = x[i]; pendY[slot] = y[i]; pendZ[slot] = z[i];
      pendCount++;
    }
    n -= k;
  }
  return true;
}

// How far a stamp may sit from a whole number of periods after the last one
// and still be that edge's own. Real stamps sit within ~1 us; an edge that
// waited out a stretch with interrupts off is stamped late, by up to a
// period, and must not be believed.
static const uint32_t STAMP_PHASE_TOL_US = 200;

// Returns true when it handed on at least one sample - the display may draw
// right after, never between two (see loop()).
//
// ESP32, 2026-10-07: the samples come out of the BMI270's FIFO, and INT1 only
// tells the time. The first block starts read the data registers on each
// edge and lost every sample the loop was too busy to read: 63 in the ~80 ms
// the detector's pre-roll replay takes at "set" (no FPU on the C3), and 3
// every 2.000 s to something outside this sketch. Now the sensor keeps every
// sample until it is read (1.28 s of room) and the ISR keeps every edge's
// stamp, so a stall only delays samples, it cannot lose them.
//
// Pairing, oldest first: a stamp k sensor periods after the last one is
// that frame's own when k == 1. k > 1 means k-1 edges were never stamped
// (ISR held off): this frame gets the time interpolated between the two
// neighbours, on the sensor's own clock, and DROPPED counts it. k == 0 is
// an edge already accounted for; a stamp off the period grid was delayed and
// is not used. With no stamp at all for 3 periods, the frame is extrapolated.
static bool serviceSampling() {
  if (!imuReady) return false;

  if ((uint32_t)(stampHead - stampTail) > STAMP_Q) {
    fifoResync();
    fifoResyncs++;
    return false;
  }
  if (stampHead != stampTail || pendCount > 0 ||
      (uint32_t)(micros() - lastFifoPollUs) > SAMPLE_PERIOD_US) {
    if (!pullFifo()) {
      fifoResync();
      fifoResyncs++;
      return false;
    }
  }

  uint32_t handed = 0;
  uint32_t backlog = pendCount;
  while (pendCount > 0) {
    uint32_t t;
    if (stampHead != stampTail) {
      uint32_t st = stampQ[stampTail & (STAMP_Q - 1)];
      uint32_t d = st - lastStampT;
      // At or before the last sample (half a period of slack): an edge
      // already accounted for. Also keeps an older stamp from wrapping.
      if ((int32_t)d < (int32_t)(periodQ8 >> 9)) {
        stampTail++;
        continue;
      }
      uint32_t k = ((uint64_t)d * 256 + periodQ8 / 2) / periodQ8;
      int32_t phase = (int32_t)(d - (uint32_t)(((uint64_t)k * periodQ8) >> 8));
      if (phase > (int32_t)STAMP_PHASE_TOL_US || phase < -(int32_t)STAMP_PHASE_TOL_US) {
        stampTail++;                               // not this frame's edge
        continue;
      }
      if (k == 1) {
        t = st;
        stampTail++;
        // Sensor period from clean neighbours, smoothed over ~64 samples.
        int32_t err = (int32_t)(d << 8) - (int32_t)periodQ8;
        periodQ8 += err / 64;
      } else {
        t = lastStampT + d / k;
        droppedSamples++;
      }
    } else if ((uint32_t)(micros() - lastStampT) > 3 * SAMPLE_PERIOD_US + (periodQ8 >> 8)) {
      t = lastStampT + (periodQ8 >> 8);
      droppedSamples++;
    } else {
      break;                                       // its stamp is on the way
    }
    uint16_t slot = pendHead;
    pendHead = (pendHead + 1) % PEND_Q;
    pendCount--;
    lastStampT = t;
    storeSample(t, pendX[slot], pendY[slot], pendZ[slot]);
    handed++;
  }
  if (seqState != SEQ_IDLE && backlog > maxBacklog) maxBacklog = backlog;
  return handed > 0;
}

// Hands on everything the sensor has produced up to now, so that the ring is
// complete at a sequence boundary ("set", the verdict). Normally one sample.
static void drainSampling() {
  uint32_t t0 = micros();
  do {
    serviceSampling();
  } while ((pendCount > 0 || bmiFifoFrames() > 0) && (uint32_t)(micros() - t0) < 5000);
}

// --- buzzer drive ----------------------------------------------------------
//
// Two complementary square waves from the PWM peripheral, replacing tone().
// tone() was wrong here for three separate reasons, all of which land on the
// one instant the whole measurement is referenced to:
//
//  1. It drives ONE pin, so it cannot do the antiphase that buys the +6 dB.
//  2. The mbed core's tone() does `new Tone` and `new DigitalOut` on every
//     call - a heap allocation at the exact microsecond that defines the
//     reaction time's zero. malloc is not constant-time, and on a fragmented
//     heap it is not even bounded.
//  3. That same implementation leaks: Tone::stop() nulls its DigitalOut*
//     instead of deleting it, so every beep loses the object. Three beeps a
//     run, forever, on a board with ~70 KB free.
//
// The PWM peripheral has none of these. Starting it is two register writes,
// the first edge follows within one 16 MHz tick, and it then runs from its own
// hardware with no CPU and no interrupts - which matters because the "go" beep
// overlaps the 100 ms in which the athlete's push-off is being sampled, and a
// software ticker would be firing 8000 times a second right through it.
//
// PWM2 is used rather than PWM0 because the mbed core hands out PWM instances
// from 0 upwards for analogWrite()/PwmOut. This sketch calls neither today,
// but taking the far end of the range costs nothing and keeps it that way.
//
// ESP32 port. The C3 has no MCPWM, so the antiphase is built differently but
// keeps the property that mattered: starting the beep is two register writes
// and the waveform then runs in hardware with no CPU and no interrupts.
//   - One LEDC channel runs a 4000 Hz, 50% square wave CONTINUOUSLY from boot.
//     It is connected to no pin while silent.
//   - Beep on  = route that signal to (+) and, through the GPIO matrix's output
//     inverter, to (-). Same signal, one inverted: exact antiphase.
//   - Beep off = hand both pins back to plain GPIO, resting HIGH.
// Because the wave is already running, one of the two pins changes level at
// the instant of the routing, whichever half-period the wave is in: the
// element is driven from that instant, which is what beep() stamps. The
// phase of the 4 kHz wave relative to that instant is random (0-125 us);
// on the XIAO the sequence restarted from a fixed phase.
//
// Rest HIGH, not LOW as on the XIAO: chosen when the buzzer was on GPIO8, which
// also drives the SuperMini's blue LED to 3V3. On GPIO1/2 either level would
// do; both pins at the same level is what keeps the piezo free of DC bias.
static const ledc_timer_t   BUZZER_TIMER = LEDC_TIMER_3;     // far end, as PWM2 was
static const ledc_channel_t BUZZER_CH    = LEDC_CHANNEL_5;
static const uint32_t BUZZER_SIG = LEDC_LS_SIG_OUT0_IDX + BUZZER_CH;
static bool buzzerDriving = false;

static void buzzerRest() {
  esp_rom_gpio_connect_out_signal(BUZZER_PIN,   SIG_GPIO_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(BUZZER_PIN_B, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level((gpio_num_t)BUZZER_PIN,   1);
  gpio_set_level((gpio_num_t)BUZZER_PIN_B, 1);
}

static void buzzerInit() {
  gpio_reset_pin((gpio_num_t)BUZZER_PIN);
  gpio_reset_pin((gpio_num_t)BUZZER_PIN_B);
  gpio_set_direction((gpio_num_t)BUZZER_PIN,   GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)BUZZER_PIN_B, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)BUZZER_PIN,   1);
  gpio_set_level((gpio_num_t)BUZZER_PIN_B, 1);

  ledc_timer_config_t tc = {};
  tc.speed_mode      = LEDC_LOW_SPEED_MODE;
  tc.duty_resolution = LEDC_TIMER_10_BIT;
  tc.timer_num       = BUZZER_TIMER;
  tc.freq_hz         = BEEP_FREQ_HZ;
  tc.clk_cfg         = LEDC_AUTO_CLK;
  ledc_timer_config(&tc);

  // ledc_channel_config needs a pin, and briefly routes the wave to it; the
  // reroute right after leaves it running on no pin at all.
  ledc_channel_config_t cc = {};
  cc.gpio_num   = BUZZER_PIN;
  cc.speed_mode = LEDC_LOW_SPEED_MODE;
  cc.channel    = BUZZER_CH;
  cc.timer_sel  = BUZZER_TIMER;
  cc.duty       = 512;   // 50% of 10 bits
  cc.hpoint     = 0;
  ledc_channel_config(&cc);
  buzzerRest();
  buzzerDriving = false;
}

static void buzzerDriveOn() {
  esp_rom_gpio_connect_out_signal(BUZZER_PIN_B, BUZZER_SIG, true,  false);   // (-) inverted
  esp_rom_gpio_connect_out_signal(BUZZER_PIN,   BUZZER_SIG, false, false);   // (+)
  buzzerDriving = true;
}

static void buzzerDriveOff() {
  // Safe to call when already off (an abort between two beeps does).
  if (!buzzerDriving) return;
  buzzerRest();
  buzzerDriving = false;
}

// Drives the buzzer and stamps the marker in one place. The timestamp is
// taken immediately AFTER the drive signal starts, so it marks the
// electrical instant the transducer was driven. What separates that from the
// first pressure wave reaching the athlete is the buzzer's own latency - for
// an active buzzer this was a fixed 5-20 ms acoustic startup, constant and
// one-directional, hence a calibration constant rather than an error.
//
// That constant has NEVER been measured on this project, and it is now the
// dominant term in the whole error budget (detection is ~1-2 ms). It is also
// no longer the number the old handover notes describe: the part is a piezo
// at resonance driven antiphase, not an active buzzer, and a high-Q resonator
// rings up over roughly Q/pi cycles - at 4 kHz that is on the order of a
// millisecond or two, likely *better* than the 5-20 ms previously assumed,
// but assumed is exactly the problem. Measure it once with a GPIO edge and a
// microphone on one time base and subtract it. Re-measure if the buzzer,
// BEEP_FREQ_HZ or the antiphase wiring changes - all three move it.
static void beep(uint32_t* markerOut, bool* capturedOut) {
  buzzerDriveOn();
  uint32_t t = micros();
  buzzerOn = true;
  buzzerOffUs = t + BEEP_MS * 1000UL;
  *markerOut = t;
  *capturedOut = true;
}

// Power on/off confirmation. Blocking and unstamped: never during a start.
static void powerBeep() {
  buzzerDriveOn();
  delay(POWER_BEEP_MS);
  buzzerDriveOff();
}

// Nothing in the sequence may call delay(). Sampling is paced by the DRDY
// interrupt on a LEVEL-LATCHED line, so a loop that blocks past one sample
// period does not merely stutter - a missed edge is permanent until the
// watchdog recovers it (see DRDY_STALL_TIMEOUT_US). Hence every wait below is
// a micros() deadline checked from loop(), never a delay.
static void serviceBuzzer() {
  if (buzzerOn && (int32_t)(micros() - buzzerOffUs) >= 0) {
    buzzerDriveOff();
    buzzerOn = false;
  }
}

// ===========================================================================
// On-device STA/LTA + AIC
// ===========================================================================

// Seeds the detector from the pre-roll that is already in the ring: estimates
// gravity from the resting window, then replays the remaining pre-roll samples
// so the LTA is warm by the time "set" opens the judged window. Without this
// the detector would be blind for one LTA time constant (~800 ms) starting at
// exactly the moment a false start becomes possible.
//
// The gravity sum is accumulated over the ring in place. The original copied
// the resting window into three float[300] locals - a 3.6 KB stack frame and a
// silent min(rest_n, 300) cap that is invisible at 833 Hz and shortens the
// calibration window at any higher ODR.
static void setupDetectorFromPreroll() {
  detector.begin((float)ACCEL_ODR_HZ, setT);

  uint32_t oldest = (recWritten > MAX_REC_SAMPLES) ? (recWritten - MAX_REC_SAMPLES) : 0;
  uint32_t from = (setSampleIdx > PREROLL_SAMPLES) ? (setSampleIdx - PREROLL_SAMPLES) : 0;
  if (from < oldest) from = oldest;

  uint32_t rn = detector.rest_n;
  if (recWritten - from < rn) {
    detector.calibrateGravity(0.0f, 0.0f, 0.0f, recWritten - from);  // records the fault
    return;
  }

  float sx = 0.0f, sy = 0.0f, sz = 0.0f;
  for (uint32_t i = 0; i < rn; i++) {
    uint32_t slot = (from + i) % MAX_REC_SAMPLES;
    sx += rawToG(recX[slot]);
    sy += rawToG(recY[slot]);
    sz += rawToG(recZ[slot]);
  }
  if (!detector.calibrateGravity(sx, sy, sz, rn)) return;

  for (uint32_t i = from + rn; i < recWritten; i++) {
    uint32_t slot = i % MAX_REC_SAMPLES;
    detector.update(recT[slot], rawToG(recX[slot]), rawToG(recY[slot]), rawToG(recZ[slot]));
  }
}

// Second stage, once the tail has elapsed: AIC refines each trigger's onset,
// then the product rule turns it into a verdict. Both are non-causal (AIC
// reads samples after the trigger), which is why they run here and not in
// update() - on device that only delays the report, never the timestamp.
static void evaluateAndReportResults() {
  lastVerdict = nullptr;
  lastReactionMs = 0.0f;
  lastOnsetUs = 0;
  for (int i = 0; i < detector.num_events; i++) {
    uint32_t ref_t = detector.events[i].t_us;
    float moved = 0.0f;
    const char* why = nullptr;
    uint32_t prev_t = (i > 0) ? detector.events[i - 1].t_us : 0;
    bool ok = refineOnsetAIC(detector.events[i].trigger_t_us, prev_t,
                             recT, recX, recY, recZ, recWritten, MAX_REC_SAMPLES,
                             ACCEL_SCALE_G_PER_LSB, detector.g_hat, detector.events[i].b_h,
                             &ref_t, &moved, &why);
    if (ok) {
      detector.events[i].t_us = ref_t;
      detector.events[i].aic_ok = true;
      detector.events[i].aic_moved_ms = moved;
    }
    classifyEvent(&detector.events[i], setT, goT);
  }

  Serial.println();
  Serial.println("================================================");
  Serial.println(">> ON-DEVICE DETECTION RESULT <<");

  // A fault outranks everything: without a gravity estimate the projection is
  // not the horizontal component and nothing below would mean what it says.
  if (detector.fault != nullptr) {
    lastVerdict = "UNUSABLE";
    Serial.print("UNUSABLE      : "); Serial.println(detector.fault);
    Serial.print("measured      : "); Serial.println(detector.fault_value, 3);
    Serial.println("================================================");
    Serial.println();
    return;
  }

  // One verdict. The arming gate means the first event after it IS the answer:
  // there is no settling to skip past and no second candidate to prefer.
  if (detector.num_events > 0) {
    DetectedEvent* ev = &detector.events[0];
    lastVerdict = ev->verdict;
    lastReactionMs = ev->reaction_ms;
    lastOnsetUs = ev->t_us;
    Serial.print("VERDICT       : "); Serial.println(ev->verdict);
    Serial.print("Reaction Time : "); Serial.print(ev->reaction_ms, 1); Serial.println(" ms");
    Serial.print("STA/LTA Trig  : "); Serial.print(ev->trigger_t_us); Serial.println(" us");
    Serial.print("AIC Refined   : "); Serial.print(ev->t_us);
    Serial.print(" us (shift: "); Serial.print(ev->aic_moved_ms, 1);
    Serial.println(ev->aic_ok ? " ms, AIC OK)" : " ms, KEPT THRESHOLD)");
    Serial.print("Horiz Energy  : "); Serial.print(ev->horiz_mg, 1); Serial.println(" mg");
  } else {
    lastVerdict = "no movement";
    Serial.println("VERDICT       : no movement in the judged window");
  }

  // The evidence behind the arming decision, printed with every verdict.
  Serial.print("Armed         : ");
  Serial.print((int32_t)(detector.gate_t_us - setT) / 1000);
  Serial.print(" ms after set, at "); Serial.print(detector.gate_peak_mg, 1);
  Serial.print(" mg (limit "); Serial.print(DET_SETTLED_MG, 0); Serial.println(" mg)");

  // Never a refusal - an annotation. The athlete never went still inside the
  // cap, so the verdict above stands but a human should look at it.
  if (detector.gate_capped) {
    Serial.println("NOTE          : armed on the cap, athlete never settled -");
    Serial.println("                verdict stands but is worth reviewing");
  }
  // Proof the FIFO did its job: how far behind the loop fell, and that no
  // sample went missing anyway.
  Serial.print("Sampling      : backlog max "); Serial.print(maxBacklog);
  Serial.print(" samples ("); Serial.print(maxBacklog * SAMPLE_PERIOD_US / 1000);
  Serial.print(" ms), "); Serial.print(droppedSamples);
  Serial.print(" interpolated, "); Serial.print(gapCount);
  Serial.print(" gaps, "); Serial.print(fifoResyncs); Serial.println(" resyncs");
  if (detector.events_dropped > 0) {
    Serial.print("NOTE          : "); Serial.print(detector.events_dropped);
    Serial.println(" further event(s) past MAX_EVENTS were not recorded");
  }
  Serial.println("================================================");
  Serial.println();
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
  gapCount = maxGapUs = 0;
  maxBacklog = 0;
  fifoResyncs = 0;
  showingResult = false;
  seqStartUs = seed;
  seqState = SEQ_WAIT_MARKS;
  seqDeadlineUs = seed + COUNTDOWN_MS * 1000UL;
  showCountdown(3);
  Serial.println("SEQ,armed");
}

static void abortSequence(const char* why) {
  if (seqState == SEQ_IDLE) return;
  seqState = SEQ_IDLE;
  buzzerDriveOff();
  buzzerOn = false;
  Serial.print("SEQ,abort,");
  Serial.println(why);
  showReady();
}

// Deadline comparisons are done on a signed difference so they stay correct
// across the ~71 minute micros() wrap.
static void serviceSequence() {
  serviceBuzzer();
  if (seqState == SEQ_IDLE) return;

  // Polled, not deadline-driven: this is the one transition the athlete
  // decides rather than the clock.
  if (seqState == SEQ_WAIT_ARM) {
    // Belt and braces. The detector opens its own gate on DET_ARM_CAP_MS, but
    // that only runs while it is being fed - a failed gravity estimate makes
    // update() return immediately and the gate would never open at all. The
    // sequence must always reach "go", so it carries its own fallback.
    bool stuck = (int32_t)(micros() - goCapUs) >= 0;
    if (!detector.gate_open && !stuck) return;

    uint32_t arm_us = detector.gate_open ? detector.gate_t_us : micros();
    uint32_t go_at  = arm_us + (uint32_t)random(GO_AFTER_ARM_MIN_MS, GO_AFTER_ARM_MAX_MS) * 1000UL;
    uint32_t hard   = goCapUs;
    if ((int32_t)(go_at - hard) > 0) go_at = hard;

    seqState = SEQ_WAIT_GO;
    seqDeadlineUs = go_at;
    // NOT "SEQ,armed" - that one already means "the button was pressed and a
    // sequence is starting". Two different events must not share a token: a
    // reader matching on the prefix would take them for the same thing.
    Serial.print("SEQ,gate,");
    Serial.print((int32_t)(arm_us - setT) / 1000);          // ms after "set"
    Serial.print(',');
    Serial.print(detector.gate_peak_mg, 1);                 // how still, in mg
    Serial.print(',');
    Serial.println(detector.gate_open ? (detector.gate_capped ? "cap" : "still")
                                      : "no-detector");
    return;
  }

  if ((int32_t)(micros() - seqDeadlineUs) < 0) return;

  switch (seqState) {
    case SEQ_WAIT_MARKS:
      beep(&onT, &onCaptured);
      seqState = SEQ_WAIT_SET;
      seqDeadlineUs = onT + (uint32_t)random(SET_DELAY_MIN_MS, SET_DELAY_MAX_MS) * 1000UL;
      Serial.println("SEQ,marks");
      showMarks();
      break;
    case SEQ_WAIT_SET:
      beep(&setT, &setCaptured);
      drainSampling();
      // No buffer to start: the ring already holds the last ~13.9 s. "Set"
      // only records where in it the judged window begins.
      setSampleIdx = recWritten;
      goCapUs = setT + (uint32_t)random(SET_TO_GO_CAP_MIN_MS, SET_TO_GO_CAP_MAX_MS) * 1000UL;
      seqState = SEQ_WAIT_ARM;
      // No deadline: the gate decides. serviceSequence() polls it above,
      // and the detector's own cap guarantees it opens.
      seqDeadlineUs = setT;
      Serial.println("SEQ,set");
      setupDetectorFromPreroll();
      showSet();
      break;
    case SEQ_WAIT_GO:
      beep(&goT, &goCaptured);
      seqState = SEQ_TAIL;
      seqDeadlineUs = goT + TAIL_AFTER_GO_MS * 1000UL;
      Serial.println("SEQ,go");
      showGo();
      break;
    case SEQ_TAIL: {
      drainSampling();
      seqState = SEQ_IDLE;
      evaluateAndReportResults();
      // Result on screen first, synchronously: the flash write below blocks
      // for up to a second, and the person is looking at the display now.
      showResult();
      showResultFooter("saving...");
      displayPump(0xFFFFFFFFUL);
      uint32_t storeT0 = millis();
      lastStoredId = storeRun();
      uint32_t storeMs = millis() - storeT0;
      if (lastStoredId) {
        Serial.print("STORE,");
        Serial.print(lastStoredId);
        Serial.print(','); Serial.print(storeMs); Serial.println("ms");
      } else {
        Serial.print("STORE,failed,"); Serial.print(flashErrStep);
        Serial.print(','); Serial.print(flashErrCode);
        Serial.print(",0x"); Serial.print(flashErrAddr, HEX);
        Serial.print(','); Serial.print(storeMs); Serial.println("ms");
      }
      {
        char foot[27];
        if (lastStoredId) snprintf(foot, sizeof(foot), "#%lu saved | press: ready", (unsigned long)lastStoredId);
        else snprintf(foot, sizeof(foot), "NOT saved | press: ready");
        showResultFooter(foot);
      }
      dumpRecording(false, lastStoredId);
      break;
    }
    default:
      break;
  }
}


// ===========================================================================
// Screens. These only QUEUE drawing - loop() pumps it, see there.
// ===========================================================================

static void showReady() {
  showingResult = false;
  screenBegin();
  queueTextCentered(8, 3, C_ORANGE, "ProStart");
  queueTextCentered(40, 3, C_GREEN, "READY");
  queueTextCentered(74, 1, C_WHITE, "Press the button to");
  queueTextCentered(86, 1, C_WHITE, "begin the start sequence");
  char line[27];
  if (storageReady) snprintf(line, sizeof(line), "%lu run(s) on board", (unsigned long)storageCount());
  else snprintf(line, sizeof(line), "flash error: not saving");
  queueTextCentered(114, 1, C_GREY, line);
}

static int countdownShown = 0;

static void showCountdown(int digit) {
  countdownShown = digit;
  screenBegin();
  char s[2] = {(char)('0' + digit), 0};
  queueTextCentered(29, 10, C_WHITE, s);
}

static void showMarks() {
  screenBegin();
  queueTextCentered(30, 3, C_WHITE, "On your");
  queueTextCentered(64, 3, C_WHITE, "marks");
}

static void showSet() {
  screenBegin();
  queueTextCentered(43, 6, C_YELLOW, "Set");
}

static void showGo() {
  screenBegin();
  queueTextCentered(39, 7, C_GREEN, "Go");
}

// Seconds with three decimals, the way a start is reported in athletics:
// 152.4 ms -> "0.152", -45 ms -> "-0.045". Integer maths: this core's printf
// has no %f.
static void formatSeconds(char* buf, size_t len, float ms) {
  long v = lroundf(ms);
  const char* sign = (v < 0) ? "-" : "";
  if (v < 0) v = -v;
  snprintf(buf, len, "%s%ld.%03ld", sign, v / 1000, v % 1000);
}

static void showResult() {
  showingResult = true;
  screenBegin();
  char val[16];
  const char* v = lastVerdict;
  if (v != nullptr && strcmp(v, "valid start") == 0) {
    queueTextCentered(10, 2, C_WHITE, "Reaction time");
    formatSeconds(val, sizeof(val), lastReactionMs);
    queueTextCentered(40, 5, C_GREEN, val);
    queueTextCentered(84, 2, C_GREY, "seconds");
  } else if (v != nullptr && strcmp(v, "FALSE START") == 0) {
    // Negative: moved before "go". Positive but under DET_FALSE_START_MS:
    // after "go", but too soon to have been a reaction to it.
    queueTextCentered(10, 2, C_RED, "FALSE START");
    formatSeconds(val, sizeof(val), lastReactionMs);
    queueTextCentered(44, 4, C_RED, val);
    queueTextCentered(84, 2, C_GREY, "seconds");
  } else if (v != nullptr && strcmp(v, "no movement") == 0) {
    queueTextCentered(30, 3, C_YELLOW, "No start");
    queueTextCentered(66, 2, C_GREY, "detected");
  } else {
    queueTextCentered(24, 3, C_RED, "Error");
    queueTextCentered(64, 1, C_GREY,
                      detector.fault != nullptr ? detector.fault : (v ? v : "no verdict"));
  }
}

static void showResultFooter(const char* text) {
  queueFill(0, 112, TFT_W, 8, C_BLACK);
  queueTextCentered(113, 1, C_GREY, text);
}

// ===========================================================================
// Power: "off" is the ESP32-C3's light sleep - CPU paused, RAM kept, woken by
// the button. Not deep sleep: only GPIO0-5 can wake that, and the button is on
// GPIO9. A press must then be held 1.5 s (dark until then, beep when accepted)
// or the chip goes straight back to sleep. Accepted -> wait for the release
// and restart, so "on" always runs the same setup() as a power-up. The release
// comes first because GPIO9 is the BOOT strap: held across the reset it would
// enter download mode. Light sleep costs more than deep sleep (~0.1-0.2 mA),
// but the SuperMini's red power LED, always on, costs more than either.
// ===========================================================================

static void waitButtonRelease() {
  for (;;) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      delay(BUTTON_DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) == HIGH) return;
    }
    delay(5);
  }
}

// After a wake, display still off. True once the button has been held
// LONG_PRESS_MS from the wake (light sleep wakes in ~1 ms, so the press that
// woke the chip counts in full). Released early -> false, back to sleep.
static bool holdToWake() {
  unsigned long t0 = millis();
  while (millis() - t0 < LONG_PRESS_MS) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      delay(BUTTON_DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) == HIGH) return false;
    }
    delay(10);
  }
  return true;
}

static void enterSystemOff() {
  tftSleep();
  // Freeze the backlight LOW: a pad left to the sleep's own configuration
  // could float, and through the module's Q1 a floating pin lights the panel.
  gpio_hold_en((gpio_num_t)TFT_BL_PIN);
  if (imuReady) {
    detachInterrupt(digitalPinToInterrupt(IMU_INT1_PIN));
    bmiSleep();                     // the BMI270 is on 3V3, not on a GPIO
  }
  buzzerDriveOff();
  ledc_stop(LEDC_LOW_SPEED_MODE, BUZZER_CH, 1);
  Serial.flush();
  // The button keeps its normal input + pull-up through the sleep, and a LOW
  // level on it is the wake-up.
  gpio_sleep_sel_dis((gpio_num_t)BUTTON_PIN);
  gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  for (;;) {
    // Armed while still held, the press that asked for "off" (or a short one
    // that did not make it to 1.5 s) would wake the chip again at once.
    waitButtonRelease();
    esp_light_sleep_start();
    if (holdToWake()) break;
  }
  gpio_wakeup_disable((gpio_num_t)BUTTON_PIN);
  buzzerInit();
  powerBeep();
  waitButtonRelease();
  gpio_hold_dis((gpio_num_t)TFT_BL_PIN);
  wakeMagic = WAKE_MAGIC;
  esp_restart();
}

static void shutDown() {
  if (seqState != SEQ_IDLE) abortSequence("power off");
  Serial.println("POWER,off");
  powerBeep();
  screenBegin();
  queueTextCentered(40, 2, C_WHITE, "Nice session");
  queueTextCentered(66, 2, C_ORANGE, "today!");
  displayPump(0xFFFFFFFFUL);
  delay(2000);
  enterSystemOff();
}

// ===========================================================================
// Button and serial
// ===========================================================================

static void shortPress() {
  if (seqState != SEQ_IDLE) return;
  if (showingResult) showReady();
  else startSequence();
}

// Polled, not interrupt-driven: loop() already turns over at the sample rate,
// so a press is seen within ~1.25 ms, and a bouncing mechanical contact on an
// interrupt is a well-known way to flood a system.
//
// One button, three meanings. Abort acts on the PRESS - the second of two
// within ABORT_CONFIRM_MS; the first only puts a hint on the screen. The rest act on the RELEASE, because until then a
// press cannot be told apart from the start of a 1.5 s hold (power off).
static void serviceButton() {
  static bool lastLevel = HIGH;          // HIGH = released (INPUT_PULLUP)
  static uint32_t lastChangeMs = 0, pressMs = 0;
  static bool pressUsed = false, longFired = false;
  static uint32_t abortArmedMs = 0;       // 0 = no first press pending
  bool level = (digitalRead(BUTTON_PIN) == HIGH);
  uint32_t now = millis();

  if (level != lastLevel && now - lastChangeMs >= BUTTON_DEBOUNCE_MS) {
    lastChangeMs = now;
    lastLevel = level;
    if (!level) {
      pressMs = now;
      pressUsed = longFired = false;
      if (!buttonSwallow && seqState != SEQ_IDLE) {
        if (abortArmedMs != 0 && now - abortArmedMs <= ABORT_CONFIRM_MS) {
          abortArmedMs = 0;
          abortSequence("button");
        } else {
          abortArmedMs = now ? now : 1;
          queueFill(0, 112, TFT_W, 8, C_BLACK);
          queueTextCentered(113, 1, C_ORANGE, "press again to cancel");
        }
        pressUsed = true;
      }
    } else {
      if (buttonSwallow) buttonSwallow = false;
      else if (!pressUsed && !longFired) shortPress();
    }
  }

  if (abortArmedMs != 0 && (seqState == SEQ_IDLE || now - abortArmedMs > ABORT_CONFIRM_MS)) {
    abortArmedMs = 0;
    if (seqState != SEQ_IDLE) queueFill(0, 112, TFT_W, 8, C_BLACK);   // hint gone
  }

  if (!lastLevel && !longFired && !buttonSwallow && now - pressMs >= LONG_PRESS_MS) {
    longFired = true;
    shutDown();
  }
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'b') {
    // Same as the button, except that it starts straight from the result
    // screen too - at the bench one keypress per run is what you want.
    if (seqState == SEQ_IDLE) startSequence();
    else abortSequence("serial");
  } else if (c == 'a') {
    abortSequence("serial");
  } else if (c == 'd') {
    // Dump whatever the ring holds right now, no sequence involved.
    if (seqState == SEQ_IDLE) dumpRecording(true, 0);
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
  } else if (c == 'T') {
    // T<unix seconds>\n from the PC. Sanity-bounded: 2020..2100.
    Serial.setTimeout(100);
    long v = Serial.parseInt();
    if (v > 1577836800L) {
      epochAtBoot = (uint32_t)v - millis() / 1000;
      Serial.print("TIME,");
      Serial.println((uint32_t)v);
    } else {
      Serial.println("TIME,rejected");
    }
  } else if (c == 'E') {
    // Diagnostic: erase every EMPTY slot sector by sector and report how long
    // each erase took. Never touches a stored run.
    if (seqState != SEQ_IDLE || !storageReady) return;
    FLASH_WAIT_LIMIT_MS = 10000;
    uint32_t worst = 0, total = 0, n = 0, slow = 0;
    for (uint32_t sl = 0; sl < SLOT_COUNT; sl++) {
      if (slotId[sl]) continue;
      for (uint32_t a = sl * SLOT_BYTES; a < (sl + 1) * SLOT_BYTES; a += SECTOR_BYTES) {
        flashMaxWaitMs = 0;
        uint32_t t0 = millis();
        bool ok = flashErase(a, SECTOR_BYTES);
        uint32_t ms = millis() - t0;
        if (!ok) { Serial.print("ERASETEST,fail,0x"); Serial.println(a, HEX); }
        if (ms > worst) worst = ms;
        if (ms > 100) { slow++; Serial.print("ERASETEST,slow,0x"); Serial.print(a, HEX); Serial.print(','); Serial.println(ms); }
        total += ms; n++;
      }
    }
    FLASH_WAIT_LIMIT_MS = 1000;
    Serial.print("ERASETEST,sectors,"); Serial.print(n);
    Serial.print(",mean_ms,"); Serial.print(n ? total / n : 0);
    Serial.print(",worst_ms,"); Serial.print(worst);
    Serial.print(",over100ms,"); Serial.println(slow);
  } else if (c == 'L' || c == 'F' || c == 'X') {
    // Flash access blocks; never in the middle of a start.
    if (seqState != SEQ_IDLE) return;
    uint8_t order[SLOT_COUNT];
    uint32_t n = storageSlotsByAge(order);
    if (c == 'L') {
      Serial.print("FILES,"); Serial.print(n); Serial.print(',');
      Serial.println(SLOT_COUNT - n);   // free slots
      for (uint32_t i = 0; i < n; i++) {
        Serial.print("FILE,"); Serial.print(slotId[order[i]]); Serial.print(',');
        Serial.println(slotN[order[i]]);
      }
      Serial.println("FILES_END");
    } else if (c == 'F') {
      for (uint32_t i = 0; i < n; i++) {
        if (!emitStoredRun(order[i])) { Serial.print("FETCH_FAIL,"); Serial.println(slotId[order[i]]); }
      }
      Serial.println("FETCH_END");
    } else {
      Serial.print("ERASED,");
      Serial.println(storageEraseAll());
      if (!showingResult) showReady();   // refresh the "runs on board" line
    }
  }
}

// The countdown digits are the only screen changes driven by the clock alone.
static void serviceCountdown() {
  if (seqState != SEQ_WAIT_MARKS) return;
  int digit = 3 - (int)((micros() - seqStartUs) / 1000000UL);
  if (digit >= 1 && digit != countdownShown) showCountdown(digit);
}

// Time the display may take after each sample. On the XIAO a sample period
// was ~1157 us and the 400 kHz burst read ~0.3 ms of it; here it is 1250 us
// and the read has to be measured (INFO.md) - the value is kept until then.
// On the XIAO: 300 us plus one unit's overshoot
// (~0.2 ms) leaves margin before the next data-ready edge. GAPS in every dump
// is the check. A screen change during a start takes ~50-100 ms this way.
static const uint32_t DRAW_BUDGET_US = 300;

void loop() {
  bool sampled = serviceSampling();
  serviceSequence();
  serviceCountdown();
  serviceButton();
  handleSerial();
  // Right after a sample, never between two: that is the whole point.
  if (sampled || !imuReady) displayPump(DRAW_BUDGET_US);
}
