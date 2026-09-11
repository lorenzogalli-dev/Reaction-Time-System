// ---------------------------------------------------------------------------
// BuzzerSweep - answers one question: at which frequency, drive mode and
// drive strength is THIS buzzer loudest?
//
// The v4 firmware beeps at a hardcoded BEEP_FREQ_HZ = 3000. That number was
// never chosen against the part: it is a round number. A piezo buzzer is a
// high-Q resonator - typically Q = 15..30 - so driving it 1 kHz off its
// resonance can cost 15-20 dB, which is most of the difference between
// "audible on the bench" and "inaudible at the track". Datasheet resonances
// cluster at 2.7, 3.2, 4.0 and 4.3 kHz; 4.0 kHz is the single most common.
//
// This sketch has no IMU, no sequence and no timing requirements, so it drives
// the pin from a plain busy-loop instead of tone(). Flash it, open the Serial
// Monitor at 921600, and listen.
//
// Commands:
//   s  sweep 1.5 -> 6.0 kHz single-ended, 500 ms per step. Note the loudest.
//   d  the same sweep, differential (see WIRING below) - should be ~6 dB up
//   f<hz>  set the working frequency, e.g. f4000
//   p  play the working frequency for 2 s, single-ended
//   P  play the working frequency for 2 s, differential
//   h  toggle nRF52840 high-drive (5 mA) on the buzzer pins
//   ?  print the current settings
//
// WIRING
//   Single-ended (what the v4 firmware uses): (+) -> D1, (-) -> GND.
//   Differential: (+) -> D1, (-) -> D2. Nothing else changes. The element then
//   sees 6.6 Vpp instead of 3.3 Vpp because the two pins swing in antiphase -
//   a genuine +6 dB for the cost of one GPIO and no components. A piezo is a
//   capacitor and does not care that neither terminal is grounded; a magnetic
//   buzzer is happy too. Run 's' and 'd' back to back on the same part to hear
//   how much it is actually worth here.
// ---------------------------------------------------------------------------

#ifndef D1
#define D1 1
#endif
#ifndef D2
#define D2 2
#endif

static const int BUZZ_A = D1;   // buzzer (+)
static const int BUZZ_B = D2;   // buzzer (-) in differential mode only

static unsigned int workFreq = 4000;
static bool highDrive = false;

// nRF52840 GPIOs default to standard drive (0.5 mA). H0H1 is 5 mA. For a piezo
// - which is a ~10-20 nF capacitor - this sharpens the square wave's edges, so
// more of the drive energy lands in the harmonics instead of being slewed away;
// for a magnetic buzzer, which is current-driven, it is the difference between
// a whisper and a tone. It cannot damage the part: the buzzer's own impedance
// still sets the current, this only raises the pad's ability to supply it.
//
// pinMode()/DigitalOut rewrite PIN_CNF, so this must be re-applied after any
// pinMode() call on the same pin - which is why applyDrive() is called at the
// top of every play function rather than once in setup().
static void setDrive(int arduinoPin, bool high) {
  uint32_t p = (uint32_t)digitalPinToPinName(arduinoPin);
  NRF_GPIO_Type *port = (p < 32) ? NRF_P0 : NRF_P1;
  uint32_t idx = p & 31u;
  uint32_t cnf = port->PIN_CNF[idx];
  cnf &= ~(0x7u << 8);                  // DRIVE field
  cnf |= (uint32_t)(high ? 3u : 0u) << 8;   // 3 = H0H1, 0 = S0S1
  port->PIN_CNF[idx] = cnf;
}

static void applyDrive() {
  setDrive(BUZZ_A, highDrive);
  setDrive(BUZZ_B, highDrive);
}

// Square wave by busy-wait. Deliberately blocking: there is nothing else in
// this sketch to starve, and a busy loop has none of tone()'s ticker-interrupt
// jitter, so what you hear is the part and not the driver.
static void play(unsigned int freq, uint32_t ms, bool differential) {
  if (freq < 100 || freq > 20000) return;
  applyDrive();

  const uint32_t half = 500000UL / freq;   // us per half period
  const uint32_t end  = micros() + ms * 1000UL;
  uint32_t next = micros();
  bool level = false;

  while ((int32_t)(micros() - end) < 0) {
    next += half;
    while ((int32_t)(micros() - next) < 0) { /* spin */ }
    level = !level;
    digitalWrite(BUZZ_A, level ? HIGH : LOW);
    digitalWrite(BUZZ_B, (differential && !level) ? HIGH : LOW);
  }
  digitalWrite(BUZZ_A, LOW);
  digitalWrite(BUZZ_B, LOW);
}

static void sweep(bool differential) {
  Serial.print("sweep ");
  Serial.print(differential ? "DIFFERENTIAL" : "single-ended");
  Serial.print(", high-drive ");
  Serial.println(highDrive ? "ON" : "off");
  Serial.println("listen for the loudest step, then set it with f<hz>");

  // 100 Hz steps through the band every small buzzer resonates in. Finer than
  // that is pointless by ear; coarser can step straight over a Q=25 peak,
  // whose -3 dB width at 4 kHz is only ~160 Hz.
  for (unsigned int f = 1500; f <= 6000; f += 100) {
    Serial.print("  ");
    Serial.print(f);
    Serial.println(" Hz");
    play(f, 500, differential);
    delay(120);   // a gap, so two adjacent steps are told apart by ear
  }
  Serial.println("sweep done");
}

static void printSettings() {
  Serial.print("working freq ");
  Serial.print(workFreq);
  Serial.print(" Hz, high-drive ");
  Serial.print(highDrive ? "ON (5 mA)" : "off (0.5 mA)");
  Serial.print(", pins A=D");
  Serial.print(BUZZ_A);
  Serial.print(" B=D");
  Serial.println(BUZZ_B);
}

void setup() {
  Serial.begin(921600);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { }

  pinMode(BUZZ_A, OUTPUT);
  pinMode(BUZZ_B, OUTPUT);
  digitalWrite(BUZZ_A, LOW);
  digitalWrite(BUZZ_B, LOW);
  applyDrive();

  Serial.println();
  Serial.println("BuzzerSweep - find the loudest drive for this buzzer");
  Serial.println("s=sweep  d=sweep differential  f<hz>=set  p=play  P=play diff");
  Serial.println("h=toggle high-drive  ?=settings");
  printSettings();
}

void loop() {
  if (!Serial.available()) return;
  int c = Serial.read();

  switch (c) {
    case 's': sweep(false); break;
    case 'd': sweep(true);  break;
    case 'p': Serial.println("play single-ended"); play(workFreq, 2000, false); break;
    case 'P': Serial.println("play differential");  play(workFreq, 2000, true);  break;
    case 'h':
      highDrive = !highDrive;
      applyDrive();
      printSettings();
      break;
    case 'f': {
      long v = Serial.parseInt();
      if (v >= 100 && v <= 20000) {
        workFreq = (unsigned int)v;
        printSettings();
      } else {
        Serial.println("f needs 100..20000, e.g. f4000");
      }
      break;
    }
    case '?': printSettings(); break;
    default: break;   // ignore newlines and stray bytes
  }
}
