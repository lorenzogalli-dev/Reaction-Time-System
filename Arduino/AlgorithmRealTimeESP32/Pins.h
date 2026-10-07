#pragma once
// ---------------------------------------------------------------------------
// Pins.h - every GPIO this sketch uses on the ESP32-C3 SuperMini, in one place.
// Wiring table and the reasons behind it: INFO.md.
//
// The C3 routes I2C, SPI and LEDC to any pin through its GPIO matrix, so what
// decides this map is the board layout plus four hardware facts:
//   - GPIO2, 8, 9 are boot strapping pins. GPIO9 must read HIGH at reset (it is
//     also the BOOT button) and GPIO8 carries the onboard blue LED to 3V3.
//     Nothing here may pull them at reset.
//   - Only GPIO0-5 can wake the chip from deep sleep. The button is on 9, so
//     "off" is light sleep instead, which any GPIO can wake (enterSystemOff).
//   - GPIO20/21 are UART0, and the ROM prints its boot log on 21: an input
//     driven from outside (INT1) goes on 20, never on 21.
//   - The IMU's SDA/SCL are fixed by the hardware already built: GPIO0/GPIO3.
// ---------------------------------------------------------------------------
#include <stdint.h>

// BMI270 breakout (I2C, address 0x68: SDO to GND)
static const int IMU_SDA_PIN  = 0;
static const int IMU_SCL_PIN  = 3;
static const int IMU_INT1_PIN = 20;  // data-ready. UART0 RX, an input at boot,
                                     // so no clash with a BMI270 still driving
                                     // it across a reset

// Button to GND, INPUT_PULLUP (the board's BOOT button is on the same pin).
// Held at reset or power-up it enters download mode: that is the C3's
// strapping, not the firmware. Release and reset to get out.
static const int BUTTON_PIN = 9;

// Passive piezo, antiphase, no GND. GPIO2 is a strapping pin, safe here:
// a piezo is a capacitor and does not pull it at reset.
static const int BUZZER_PIN   = 1;   // (+)
static const int BUZZER_PIN_B = 2;   // (-)

// 1.77" ST7735S, SPI through the GPIO matrix.
static const int TFT_SCK_PIN  = 8;   // also the blue LED (to 3V3): SPI mode 3
                                     // keeps the clock idle HIGH, LED off
static const int TFT_MOSI_PIN = 10;  // display pin "SDA"
static const int TFT_CS_PIN   = 7;
static const int TFT_DC_PIN   = 6;   // display pin "RS"
static const int TFT_RST_PIN  = 5;
static const int TFT_BL_PIN   = 4;   // display pin "LEDA", through its Q1

// Free: GPIO21 (UART0 TX, toggles with the boot log at every reset).
