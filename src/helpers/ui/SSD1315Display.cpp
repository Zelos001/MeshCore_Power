#include "SSD1315Display.h"

#ifndef SCREEN_XOFFSET
  #define SCREEN_XOFFSET  0
#endif

#ifndef SCREEN_YOFFSET
  #define SCREEN_YOFFSET  0
#endif

bool SSD1315Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SSD1315Display::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;
  }
  #ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
  #endif
  return display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void SSD1315Display::turnOn() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;  // set before begin() to prevent double claim
    if (_peripher_power) begin();  // re-init display after power was cut
  }
  display.ssd1306_command(SSD1306_DISPLAYON);
}

void SSD1315Display::turnOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (_isOn) {
    if (_peripher_power) {
#if PIN_OLED_RESET >= 0
      digitalWrite(PIN_OLED_RESET, LOW);
#endif
      _peripher_power->release();
    }
    _isOn = false;
  }
}

void SSD1315Display::clear() {
  display.clearDisplay();
  display.display();
}

void SSD1315Display::startFrame(Color bkg) {
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1315Display::setTextSize(int sz) {
  display.setTextSize(1);  // only small allowed
}

void SSD1315Display::setColor(Color c) {
  _color = (c != 0) ? SSD1306_WHITE : SSD1306_BLACK;
  display.setTextColor(_color);
}

void SSD1315Display::setCursor(int x, int y) {
  display.setCursor(x + SCREEN_XOFFSET, y + SCREEN_YOFFSET);
}

void SSD1315Display::print(const char* str) {
  display.print(str);
}

void SSD1315Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x + SCREEN_XOFFSET, y + SCREEN_YOFFSET, w, h, _color);
}

void SSD1315Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x + SCREEN_XOFFSET, y + SCREEN_YOFFSET, w, h, _color);
}

void SSD1315Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;

  x += SCREEN_XOFFSET;
  y += SCREEN_YOFFSET;
  // Process the bitmap row by row
  for (int by = 0; by < h; by++) {
    // Scan across the row bit by bit
    for (int bx = 0; bx < w; bx++) {
      // Get the current bit using MSB ordering (like GxEPDDisplay)
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 1 << (bx & 7);
      bool bitSet = bits[byteOffset] & bitMask;

      // If the bit is set, draw the pixel
      if (bitSet) {
        display.drawPixel(x + bx, y + by, SSD1306_WHITE);
      }
    }
  }
  // wrong bit order:
  // display.drawBitmap(x + SCREEN_XOFFSET, y + SCREEN_YOFFSET, bits, w, h, SSD1306_WHITE);
}

uint16_t SSD1315Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1315Display::endFrame() {
  display.display();
}
