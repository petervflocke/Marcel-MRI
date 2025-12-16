#include <Arduino.h>

#define GPIO_AUDIO_OUT_LEFT  18

constexpr uint8_t ButDown = 14;
constexpr uint8_t ButEnter = 13;
constexpr unsigned long kLoopDelayMs = 25;

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "oled_display.h"

OledDisplay g_oledDisplay;

const char* const kMenuItems[] = {
  "Diagnostics",
  "1st try    ",
  "2nd try    ",
  "Don't try  ",
  "Relex      "
};

OledMenu g_mainMenu{
  "Marcel's MRI Lab",
  kMenuItems,
  sizeof(kMenuItems) / sizeof(kMenuItems[0]),
  0,
};

namespace {

bool g_lastDownLevel = true;
bool g_lastEnterLevel = true;

bool ButtonPressed(uint8_t pin, bool& last_level) {
  const bool current_level = gpio_get(pin);
  const bool pressed = last_level && !current_level;
  last_level = current_level;
  return pressed;
}

void HandleMenuButtons() {
  const size_t item_count = g_mainMenu.item_count;
  if (ButtonPressed(ButDown, g_lastDownLevel) && item_count > 0) {
    g_mainMenu.selected_index = (g_mainMenu.selected_index + 1) % item_count;
    g_oledDisplay.setMenu(g_mainMenu);
  }

  if (ButtonPressed(ButEnter, g_lastEnterLevel) && item_count > 0) {
    const char* label = g_mainMenu.items[g_mainMenu.selected_index];
    Serial.print(F("[MENU] Selected: "));
    Serial.println(label ? label : "<unnamed>");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println(F("Starting..."));
  pinMode(ButDown, INPUT_PULLUP);
  pinMode(ButEnter, INPUT_PULLUP);
  g_lastDownLevel = gpio_get(ButDown);
  g_lastEnterLevel = gpio_get(ButEnter);
  
  g_oledDisplay.scanBus();
  g_oledDisplay.begin();
  g_oledDisplay.setMenu(g_mainMenu);
}

void loop() {
  g_oledDisplay.loop();
  HandleMenuButtons();
  delay(kLoopDelayMs);
}
