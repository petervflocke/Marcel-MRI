#include "oled_display.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 4
#endif

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 5
#endif

constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;
constexpr int kSdaPin = OLED_SDA_PIN;
constexpr int kSclPin = OLED_SCL_PIN;
constexpr uint8_t kI2cAddress = 0x3C;
constexpr uint32_t kI2cClockHz = 400000;
constexpr unsigned long kRenderIntervalMs = 250;
constexpr int kMenuLineHeight = 10;
constexpr int kMenuTitleSeparatorY = 10;
constexpr int kMenuContentStartY = 14;
constexpr int kMenuHelpTextHeight = 8;
constexpr int kMenuHelpGap = 1;

Adafruit_SSD1306 display(kOledWidth, kOledHeight, &Wire, -1);

}  // namespace

void OledDisplay::configureBus() {
  Wire.setSDA(kSdaPin);
  Wire.setSCL(kSclPin);
  Wire.begin();
  Wire.setClock(kI2cClockHz);
}

void OledDisplay::begin() {
  if (initialized_) {
    return;
  }
  init_attempted_ = true;

  Serial.println(F("[OLED] Initializing"));
  configureBus();
  Serial.print(F("[OLED] I2C configured at "));
  Serial.print(kI2cClockHz / 1000);
  Serial.println(F("kHz"));

  delay(20);
  if (!display.begin(SSD1306_SWITCHCAPVCC, kI2cAddress)) {
    Serial.println(F("[OLED] display.begin failed"));
    init_attempted_ = false;
    return;
  }

  display.setTextWrap(false);
  Serial.println(F("[OLED] display.begin OK"));
  initialized_ = true;
  render();
}

void OledDisplay::loop() {
  if (!initialized_ && !init_attempted_) {
    begin();
  }

  if (!initialized_) {
    return;
  }

  if (millis() - last_render_ms_ >= kRenderIntervalMs) {
    render();
  }
}

void OledDisplay::forceRender() {
  if (!initialized_) {
    return;
  }
  render();
}

void OledDisplay::scanBus() {
  Serial.println(F("[I2C] Scanning bus"));
  configureBus();
  Serial.print(F("[I2C] Using SDA="));
  Serial.print(kSdaPin);
  Serial.print(F(" SCL="));
  Serial.println(kSclPin);

  uint8_t found = 0;
  for (uint8_t address = 0x00; address <= 0x7F; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("[I2C] Found device at 0x"));
      Serial.println(address, HEX);
      ++found;
    } else if (error == 4) {
      Serial.print(F("[I2C] Unknown error at 0x"));
      Serial.println(address, HEX);
    }
    delay(5);
  }

  if (found == 0) {
    Serial.println(F("[I2C] No devices found"));
  } else {
    Serial.print(F("[I2C] Devices detected: "));
    Serial.println(found);
  }
}

void OledDisplay::setMenu(const OledMenu& menu) {
  const bool structure_changed = menu_.items != menu.items ||
                                 menu_.item_count != menu.item_count ||
                                 menu_.title != menu.title;
  menu_ = menu;
  if (menu_.item_count > 0 && menu_.selected_index >= menu_.item_count) {
    menu_.selected_index = menu_.item_count - 1;
  }
  if (structure_changed) {
    menu_scroll_offset_ = 0;
  }
  if (initialized_) {
    render();
  }
}

void OledDisplay::clearMenu() {
  menu_ = {};
  menu_scroll_offset_ = 0;
  if (initialized_) {
    render();
  }
}

void OledDisplay::setCustomRenderer(CustomRenderCallback renderer,
                                    void* user_context) {
  custom_renderer_ = renderer;
  custom_renderer_context_ = user_context;
  if (initialized_) {
    render();
  }
}

void OledDisplay::clearCustomRenderer() {
  custom_renderer_ = nullptr;
  custom_renderer_context_ = nullptr;
  if (initialized_) {
    render();
  }
}

bool OledDisplay::customRendererActive() const {
  return custom_renderer_ != nullptr;
}

Adafruit_SSD1306* OledDisplay::rawDisplay() {
  return &display;
}

void OledDisplay::render() {
  if (!initialized_) {
    return;
  }

  display.clearDisplay();
  if (custom_renderer_) {
    custom_renderer_(display, custom_renderer_context_);
  } else if (menu_.items && menu_.item_count > 0) {
    drawMenu();
  } else {
    drawSplash();
  }
  display.display();
  last_render_ms_ = millis();
}

void OledDisplay::drawSplash() {
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("READY"));
  display.setTextSize(1);
  display.println(F("OLED online"));
  display.println(F("Waiting menu"));
}

void OledDisplay::drawMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int y = 0;
  if (menu_.title) {
    display.setCursor(0, y);
    display.println(menu_.title);
    display.drawFastHLine(0, kMenuTitleSeparatorY, kOledWidth, SSD1306_WHITE);
    y = kMenuContentStartY;
  }

  int help_text_y = kOledHeight - kMenuHelpTextHeight;
  if (help_text_y < 0) {
    help_text_y = 0;
  }
  int help_divider_y = help_text_y - kMenuHelpGap;
  if (help_divider_y < 0) {
    help_divider_y = 0;
  }
  display.drawFastHLine(0, help_divider_y, kOledWidth, SSD1306_WHITE);
  display.setCursor(0, help_text_y);
  display.setTextColor(SSD1306_WHITE);
  display.print(F(" * Scroll   * Select"));

  const int menu_area_height = help_divider_y - y;
  if (menu_.item_count == 0 || menu_area_height <= 0) {
    return;
  }

  const size_t visible_items =
      static_cast<size_t>(menu_area_height / kMenuLineHeight);
  if (visible_items == 0) {
    return;
  }

  const size_t item_count = menu_.item_count;
  size_t first_visible = menu_scroll_offset_;
  if (first_visible >= item_count) {
    first_visible = 0;
  }
  const size_t max_first =
      (item_count > visible_items) ? (item_count - visible_items) : 0;
  if (first_visible > max_first) {
    first_visible = max_first;
  }

  const size_t selected = menu_.selected_index;
  if (selected < first_visible) {
    first_visible = selected;
  } else if (selected >= first_visible + visible_items) {
    first_visible = selected - visible_items + 1;
  }
  menu_scroll_offset_ = first_visible;

  size_t last_index = first_visible + visible_items;
  if (last_index > item_count) {
    last_index = item_count;
  }

  for (size_t i = first_visible; i < last_index; ++i) {
    const bool selected_item = (i == selected);
    if (selected_item) {
      display.fillRect(0, y - 1, kOledWidth, kMenuLineHeight, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(2, y);
    const char* label =
        (menu_.items && menu_.items[i]) ? menu_.items[i] : "";
    display.println(label);
    y += kMenuLineHeight;
  }

  display.setTextColor(SSD1306_WHITE);
}
