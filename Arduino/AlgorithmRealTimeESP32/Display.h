#pragma once
// ---------------------------------------------------------------------------
// Display.h - 1.77" 160x128 ST7735S TFT (AZ-Delivery "1.77'' TFT Ver 3.1"),
// hardware SPI, landscape. Self-contained: no Adafruit libraries.
//
// WHY NOT Adafruit_ST7735: the display shares the loop with an 800 Hz sampler
// that must not miss a data-ready edge (see DRDY_STALL_TIMEOUT_US in the
// sketch). A library draw call blocks for as long as it takes - a full-screen
// clear is ~80 KB of SPI, ~90 ms - and a blocked loop during "set" -> "go" is
// lost samples exactly where the reaction time lives. So nothing here draws
// directly. Screens are QUEUED as fill/text commands, and displayPump() works
// through them in small units under a TIME budget, called right after each
// sample - never between two.
//
// Time, not pixels: the first cut budgeted 200 px per sample and still lost
// samples at "set" and "go" (bench, 2026-09-29: dt ~2.1 ms at go+34..42 ms).
// Small text is dozens of tiny rectangles, and each costs three command
// transactions whatever its size - the pixel count missed most of the cost.
//
// ESP32-C3 port: only the transport changed (mbed::SPI -> the core's SPIClass
// on FSPI). The queue, the budget and the font are the XIAO's, unchanged.
// WIRING: Pins.h and INFO.md. Display pins 9-14 (GT_* font chip, second GND)
// stay unconnected.
// ---------------------------------------------------------------------------
#include <SPI.h>
#include "Pins.h"

// Panel variants of this module differ in two ways that cannot be read back:
//  - colour order: if red text comes out BLUE, flip TFT_BGR.
//  - which way is up: if the picture is upside down, set TFT_ROTATION to 3.
static const bool    TFT_BGR      = false;
static const uint8_t TFT_ROTATION = 1;      // 1 or 3: the two landscapes

static const int16_t TFT_W = 160, TFT_H = 128;

// RGB565
static const uint16_t C_BLACK  = 0x0000;
static const uint16_t C_WHITE  = 0xFFFF;
static const uint16_t C_GREY   = 0x8410;
static const uint16_t C_RED    = 0xF800;
static const uint16_t C_GREEN  = 0x07E0;
static const uint16_t C_YELLOW = 0xFFE0;
static const uint16_t C_ORANGE = 0xFD20;

// Classic 5x7 font, ASCII 32..126, column-major, bit 0 = top row.
static const uint8_t FONT5X7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x22,0x41,0x49,0x49,0x36},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

// --- low level -------------------------------------------------------------

static bool tftReady = false;
static uint8_t tftFillBuf[512];   // 256 px of one colour, streamed repeatedly

// The bus belongs to the display alone, so its transaction is opened once in
// tftBegin() and never closed: no lock taken per call on the draw path.
static inline void tftSend(const uint8_t* d, int n) {
  SPI.writeBytes(d, (uint32_t)n);
}

static void tftCommand(uint8_t cmd, const uint8_t* data = nullptr, int n = 0) {
  digitalWrite(TFT_CS_PIN, LOW);
  digitalWrite(TFT_DC_PIN, LOW);
  tftSend(&cmd, 1);
  if (n > 0) {
    digitalWrite(TFT_DC_PIN, HIGH);
    tftSend(data, n);
  }
  digitalWrite(TFT_CS_PIN, HIGH);
}

// Leaves CS low and DC high: the caller streams pixels, then raises CS.
static void tftWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  uint8_t ca[4] = {0, (uint8_t)x0, 0, (uint8_t)x1};
  uint8_t ra[4] = {0, (uint8_t)y0, 0, (uint8_t)y1};
  tftCommand(0x2A, ca, 4);   // CASET
  tftCommand(0x2B, ra, 4);   // RASET
  uint8_t c = 0x2C;          // RAMWR
  digitalWrite(TFT_CS_PIN, LOW);
  digitalWrite(TFT_DC_PIN, LOW);
  tftSend(&c, 1);
  digitalWrite(TFT_DC_PIN, HIGH);
}

static void tftFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > TFT_W) w = TFT_W - x;
  if (y + h > TFT_H) h = TFT_H - y;
  if (w <= 0 || h <= 0) return;
  tftWindow(x, y, x + w - 1, y + h - 1);
  uint32_t n = (uint32_t)w * h;
  uint32_t k = (n < 256) ? n : 256;
  for (uint32_t i = 0; i < k; i++) {
    tftFillBuf[2 * i]     = color >> 8;
    tftFillBuf[2 * i + 1] = color & 0xFF;
  }
  while (n > 0) {
    uint32_t m = (n < 256) ? n : 256;
    tftSend(tftFillBuf, 2 * m);
    n -= m;
  }
  digitalWrite(TFT_CS_PIN, HIGH);
}

static void tftBacklight(bool on) { digitalWrite(TFT_BL_PIN, on ? HIGH : LOW); }

// ST7735S power-up, the "black tab" sequence (1.77" 128x160, no RAM offset).
static void tftBegin() {
  pinMode(TFT_CS_PIN, OUTPUT);  digitalWrite(TFT_CS_PIN, HIGH);
  pinMode(TFT_DC_PIN, OUTPUT);  digitalWrite(TFT_DC_PIN, HIGH);
  pinMode(TFT_RST_PIN, OUTPUT);
  pinMode(TFT_BL_PIN, OUTPUT);  tftBacklight(false);

  if (!tftReady) {
    // No MISO: the display never answers. 8 MHz is what the XIAO ran, so the
    // draw costs measured there (GAPS, DRAW_BUDGET_US) carry over; the panel
    // itself is specified to ~15 MHz if more speed is ever needed.
    // Mode 3, not 0: SCK is on GPIO8, which also sinks the SuperMini's blue
    // LED from 3V3. Mode 0 idles the clock LOW and would keep the LED lit; mode
    // 3 idles it HIGH. The ST7735 samples on the rising edge in both.
    SPI.begin(TFT_SCK_PIN, -1, TFT_MOSI_PIN, -1);
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE3));
    tftReady = true;
  }

  digitalWrite(TFT_RST_PIN, HIGH); delay(5);
  digitalWrite(TFT_RST_PIN, LOW);  delay(20);
  digitalWrite(TFT_RST_PIN, HIGH); delay(150);

  tftCommand(0x01); delay(150);    // SWRESET
  tftCommand(0x11); delay(150);    // SLPOUT
  static const uint8_t frm[]  = {0x01, 0x2C, 0x2D};
  static const uint8_t frm3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
  tftCommand(0xB1, frm, 3);
  tftCommand(0xB2, frm, 3);
  tftCommand(0xB3, frm3, 6);
  static const uint8_t inv[] = {0x07};             tftCommand(0xB4, inv, 1);
  static const uint8_t p1[]  = {0xA2, 0x02, 0x84}; tftCommand(0xC0, p1, 3);
  static const uint8_t p2[]  = {0xC5};             tftCommand(0xC1, p2, 1);
  static const uint8_t p3[]  = {0x0A, 0x00};       tftCommand(0xC2, p3, 2);
  static const uint8_t p4[]  = {0x8A, 0x2A};       tftCommand(0xC3, p4, 2);
  static const uint8_t p5[]  = {0x8A, 0xEE};       tftCommand(0xC4, p5, 2);
  static const uint8_t vm[]  = {0x0E};             tftCommand(0xC5, vm, 1);
  tftCommand(0x20);                                // INVOFF
  // MADCTL: MY|MV (0xA0) or MX|MV (0x60) are the two landscapes; bit 3 = BGR.
  uint8_t mad = (TFT_ROTATION == 3 ? 0x60 : 0xA0) | (TFT_BGR ? 0x08 : 0x00);
  tftCommand(0x36, &mad, 1);
  static const uint8_t col[] = {0x05};             tftCommand(0x3A, col, 1);  // 16 bit
  static const uint8_t gp[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                               0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
  static const uint8_t gn[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                               0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
  tftCommand(0xE0, gp, 16);
  tftCommand(0xE1, gn, 16);
  tftCommand(0x13); delay(10);     // NORON
  tftFillRect(0, 0, TFT_W, TFT_H, C_BLACK);
  tftCommand(0x29); delay(20);     // DISPON
  tftBacklight(true);
}

static void tftSleep() {
  tftBacklight(false);
  if (!tftReady) return;           // never initialised (short press while off)
  tftCommand(0x28);                // DISPOFF
  tftCommand(0x10);                // SLPIN
}

// --- queued drawing --------------------------------------------------------

enum { DRAW_FILL, DRAW_TEXT };
struct DrawCmd {
  uint8_t kind, scale;
  int16_t x, y, w, h;
  uint16_t color;
  char text[27];
};
static const uint8_t DRAW_QUEUE_LEN = 24;
static DrawCmd drawQ[DRAW_QUEUE_LEN];
static uint8_t drawHead = 0, drawCount = 0;
// Progress through the command at the head: FILL uses row; TEXT uses
// chr/col/row (character, glyph column, glyph row).
static int16_t progRow = 0, progChr = 0, progCol = 0;

// What is on screen now (or queued to be), so the next screen can erase
// exactly that instead of clearing all 20480 pixels.
struct Box { int16_t x, y, w, h; };
static Box shown[10];
static uint8_t nShown = 0;

static void displayPump(uint32_t budgetUs);

static DrawCmd* drawPush() {
  // Full is not expected (a screen is < 20 commands); if it happens, finish
  // the backlog rather than drop an erase and leave garbage on screen.
  if (drawCount == DRAW_QUEUE_LEN) displayPump(0xFFFFFFFFUL);
  DrawCmd* c = &drawQ[(drawHead + drawCount) % DRAW_QUEUE_LEN];
  drawCount++;
  return c;
}

static void drawPop() {
  drawHead = (drawHead + 1) % DRAW_QUEUE_LEN;
  drawCount--;
  progRow = progChr = progCol = 0;
}

static void queueFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  DrawCmd* c = drawPush();
  c->kind = DRAW_FILL; c->x = x; c->y = y; c->w = w; c->h = h; c->color = color;
}

static int16_t textWidth(const char* s, uint8_t scale) {
  int n = strlen(s);
  return n ? (int16_t)(n * 6 * scale - scale) : 0;
}

static void queueText(int16_t x, int16_t y, uint8_t scale, uint16_t color, const char* s) {
  DrawCmd* c = drawPush();
  c->kind = DRAW_TEXT; c->x = x; c->y = y; c->scale = scale; c->color = color;
  strncpy(c->text, s, sizeof(c->text) - 1);
  c->text[sizeof(c->text) - 1] = 0;
  if (nShown < sizeof(shown) / sizeof(shown[0])) {
    shown[nShown++] = {x, y, textWidth(c->text, scale), (int16_t)(7 * scale)};
  }
}

static void queueTextCentered(int16_t y, uint8_t scale, uint16_t color, const char* s) {
  queueText((TFT_W - textWidth(s, scale)) / 2, y, scale, color, s);
}

// Start a new screen: text still waiting to be drawn is dropped (it would
// only be erased again), then everything the old screen put up is erased.
static void screenBegin() {
  uint8_t keep = 0;
  for (uint8_t i = 0; i < drawCount; i++) {
    DrawCmd& c = drawQ[(drawHead + i) % DRAW_QUEUE_LEN];
    if (c.kind == DRAW_FILL) drawQ[(drawHead + keep++) % DRAW_QUEUE_LEN] = c;
    else if (i == 0) progRow = progChr = progCol = 0;
  }
  // The head may have been a half-drawn FILL; its progress still applies only
  // if it is still at the head, which it is, since fills keep their order.
  drawCount = keep;
  for (uint8_t i = 0; i < nShown; i++) {
    queueFill(shown[i].x, shown[i].y, shown[i].w, shown[i].h, C_BLACK);
  }
  nShown = 0;
}

static void screenClearAll() {
  drawCount = 0;
  progRow = progChr = progCol = 0;
  nShown = 0;
  queueFill(0, 0, TFT_W, TFT_H, C_BLACK);
}

// Largest single unit of work, in pixels: ~300 bytes of SPI plus the window
// commands, ~0.2 ms. The time budget is checked between units, so this bounds
// the overshoot.
static const int32_t DRAW_UNIT_PX = 150;

// Works through the queue for about budgetUs (always at least one unit),
// then returns. A unit is a band of fill rows or one vertical run of font
// pixels, each capped at DRAW_UNIT_PX.
static void displayPump(uint32_t budgetUs) {
  if (!tftReady) { drawCount = 0; return; }
  uint32_t start = micros();
  while (drawCount > 0) {
    DrawCmd& c = drawQ[drawHead];
    if (c.kind == DRAW_FILL) {
      int16_t rows = (int16_t)(DRAW_UNIT_PX / (c.w > 0 ? c.w : 1));
      if (rows < 1) rows = 1;
      if (rows > c.h - progRow) rows = c.h - progRow;
      tftFillRect(c.x, c.y + progRow, c.w, rows, c.color);
      progRow += rows;
      if (progRow >= c.h) drawPop();
    } else {
      char ch = c.text[progChr];
      if (ch == 0) { drawPop(); continue; }
      if (ch < 32 || ch > 126) ch = '?';
      uint8_t bits = FONT5X7[ch - 32][progCol];
      while (progRow < 7 && !((bits >> progRow) & 1)) progRow++;
      if (progRow >= 7) {
        progRow = 0;
        if (++progCol == 5) { progCol = 0; progChr++; }
        continue;   // no pixels sent: not a unit
      }
      int16_t maxRows = (int16_t)(DRAW_UNIT_PX / ((int32_t)c.scale * c.scale));
      if (maxRows < 1) maxRows = 1;
      int16_t end = progRow;
      while (end < 7 && ((bits >> end) & 1) && (end - progRow) < maxRows) end++;
      tftFillRect(c.x + (progChr * 6 + progCol) * c.scale, c.y + progRow * c.scale,
                  c.scale, (end - progRow) * c.scale, c.color);
      progRow = end;
    }
    if ((uint32_t)(micros() - start) >= budgetUs) return;
  }
}
