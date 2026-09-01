#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// --- Pin Definitions (XIAO nRF52840) ---
#define PIN_BUTTON   D0   // Button connected to D0 and GND
#define TFT_CS       D1   // Display Chip Select
#define TFT_DC       D2   // Display RS / Command-Data
#define TFT_RST      D3   // Display Reset
#define PIN_BUZZER   D4   // Buzzer (+) to D4 and (-) to GND
// TFT SCK  -> D8 (Hardware SPI)
// TFT SDA  -> D10 / MOSI (Hardware SPI)
// XBee DIN -> D6 (TX)
// XBee DOUT-> D7 (RX)

// Initialize the display object using Hardware SPI
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  // 1. Initialize USB Serial (for PC debugging)
  Serial.begin(115200);

  // 2. Initialize XBee Serial (UART on pins D6/TX and D7/RX)
  Serial1.begin(9600); // Default factory baud rate for XBee

  // 3. I/O Pin Configuration
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // 4. Initialize 1.77" ST7735 TFT display (128x160)
  tft.initR(INITR_BLACKTAB); // If colors or offsets are incorrect, try INITR_REDTAB or INITR_18BLACKTAB
  tft.setRotation(1);        // 1 = Landscape
  tft.fillScreen(ST77XX_BLACK);

  // 5. Render UI on the display
  drawUI();

  Serial.println("System initialized successfully!");
  Serial1.println("System initialized!");
}

void drawUI() {
  tft.fillScreen(ST77XX_BLACK);

  // Header / Title
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 15);
  tft.println("--- SYSTEM TEST ---");

  // Line 1: "Reaction Time:"
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 35);
  tft.println("Reaction");
  tft.setCursor(10, 55);
  tft.println("Time:");

  // Line 2: "Very slow," in Red
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(10, 80);
  tft.println("Very slow,");

  // Line 3: "Lorenzo" in Cyan
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 105);
  tft.println("Lorenzo");
}

void loop() {
  // Read button state (LOW = Pressed with active pull-up)
  bool buttonPressed = (digitalRead(PIN_BUTTON) == LOW);

  if (buttonPressed) {
    // 1. Sound the buzzer at 2500 Hz
    tone(PIN_BUZZER, 2500);

    // 2. Serial feedback (PC and XBee)
    Serial.println("Button pressed! Buzzer active.");
    Serial1.println("BUTTON_PRESS: Lorenzo reacted!");

    // Short debounce delay to prevent UART flooding
    delay(50);
  } else {
    // Stop the buzzer tone when the button is released
    noTone(PIN_BUZZER);
  }

  // Forward incoming XBee data to the PC Serial Monitor
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}