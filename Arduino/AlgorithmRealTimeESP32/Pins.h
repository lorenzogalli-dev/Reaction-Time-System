#pragma once
// ---------------------------------------------------------------------------
// Pins.h - every GPIO this sketch uses on the ESP32-C3 SuperMini, in one place.
// Wiring table and the reasons behind it: INFO.md.
//
// The C3 routes I2C, SPI and LEDC to any pin through its GPIO matrix, so what
// decides this map is not peripherals but four hardware facts:
//   - Only GPIO0-5 can wake the chip from deep sleep -> the button is on one.
//   - GPIO2, 8, 9 are boot strapping pins. GPIO9 must read HIGH at reset (it is
//     also the BOOT button) and GPIO8 carries the onboard blue LED to 3V3.
//     Neither may be pulled LOW by anything at reset.
//   - GPIO20/21 are UART0, and the ROM prints its boot log on 21.
//   - The IMU's SDA/SCL are fixed by the hardware already built: GPIO0/GPIO3.
// ---------------------------------------------------------------------------
#include <stdint.h>

// BMI270 breakout (I2C, address 0x68: SDO to GND)
static const int IMU_SDA_PIN  = 0;
static const int IMU_SCL_PIN  = 3;
static const int IMU_INT1_PIN = 2;   // data-ready. Input at reset: the BMI270
                                     // keeps INT1 high-Z until we enable it

// Button to GND, INPUT_PULLUP. On 1 because it must wake from deep sleep.
static const int BUTTON_PIN = 1;

// Passive piezo, antiphase, no GND. 8 and 9 are strapping pins, safe here:
// a piezo is a capacitor and pulls neither one at reset.
static const int BUZZER_PIN   = 8;   // (+)  shares the onboard blue LED
static const int BUZZER_PIN_B = 9;   // (-)  shares the BOOT button: do not
                                     //      press BOOT while it is running

// 1.77" ST7735S, SPI. 4/6/7 are the SuperMini's own SCK/MOSI/SS labels.
static const int TFT_SCK_PIN  = 4;
static const int TFT_MOSI_PIN = 6;   // display pin "SDA"
static const int TFT_CS_PIN   = 7;
static const int TFT_DC_PIN   = 5;   // display pin "RS"
static const int TFT_RST_PIN  = 10;
static const int TFT_BL_PIN   = 20;  // display pin "LEDA", through its Q1

// Free: GPIO21 (UART0 TX, toggles with the boot log at every reset).
