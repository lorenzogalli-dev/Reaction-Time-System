// ---------------------------------------------------------------------------
// Xbee_Passthrough - USB <-> Serial1 byte bridge, so the XIAO acts as the
// USB-to-serial adapter for configuring an XBee in Digi XCTU.
// ---------------------------------------------------------------------------
//
// WHY THIS EXISTS
// ---------------
// Our XBee adapter is a Parallax 32403: a plain 2 mm-to-0.1" breakout with no
// USB chip on it. There is no COM port for XCTU to open. This sketch turns the
// XIAO into that missing USB-to-serial converter - it copies every byte from
// USB to the XBee and back, verbatim, with no framing and no interpretation,
// so XCTU talks to the module as if it were on a Digi USB explorer.
//
// This is a bring-up utility only. Nothing here is part of the product
// firmware, and it must be reflashed away before a range test.
//
// WIRING (XBee is 3.3 V - the XIAO is native 3.3 V, so no level shifter)
//   XBee VCC (pin 1)  -> XIAO 3V3
//   XBee GND (pin 10) -> XIAO GND
//   XBee DOUT (pin 2) -> XIAO D7  (Serial1 RX)
//   XBee DIN  (pin 3) -> XIAO D6  (Serial1 TX)
//   (same D6/D7 map as Arduino/Xbee_RangeTest and Arduino/Reaction_HardwareTest)
//
// WORKFLOW
//   0. Flash THIS sketch to a XIAO with one XBee wired up.
//   1. In XCTU, add the XIAO's serial port and configure that one module.
//      Then move the second module onto the adapter (or use the second XIAO,
//      also flashed with this sketch) and configure it the same way.
//      Both modules must be done one at a time - there is one UART here.
//   2. Reflash Arduino/Xbee_RangeTest to both boards for the actual test.
//
// XCTU NOTE
//   "Discover radio modules" scans a list of baud rates and expects the module
//   to answer in command mode; through a bridge that does not always work. If
//   discovery finds nothing, use "Add a radio module" instead and set the port
//   parameters by hand: the baud below, 8 data bits, no parity, 1 stop bit, no
//   flow control. The rate has to match the module's current BD - a module at
//   factory defaults is BD=3 (9600), which is what LINK_BAUD is set to.
//
// BAUD ORDERING WARNING
//   playground_xbee/ raises the link to BD=7 (115200). Once you have written
//   BD=7 to a module, this sketch can no longer talk to it at 9600 - bump
//   LINK_BAUD to 115200 and reflash before you open that module in XCTU again.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// The one rate for both ends of the bridge: the USB CDC side (nominal - a
// native-USB port ignores the requested rate) and the XBee UART side (real).
// It MUST equal the module's current BD parameter.
//   9600   = BD 3, the factory default and what a fresh module answers at
//   115200 = BD 7, what Xbee_RangeTest uses after the baud raise in step 2 of
//            playground_xbee/README.md - change this to match once BD=7 is set
#define LINK_BAUD 9600

void setup() {
  Serial.begin(LINK_BAUD);
  Serial1.begin(LINK_BAUD);
  // No banner, no waiting for the host: anything printed here would land in
  // XCTU's byte stream and be parsed as part of a frame.
}

void loop() {
  while (Serial.available())  Serial1.write((uint8_t)Serial.read());
  while (Serial1.available()) Serial.write((uint8_t)Serial1.read());
}
