#include <Arduino.h>

#define GPIO_AUDIO_OUT_LEFT  18

constexpr uint8_t ButDown = 14;
constexpr uint8_t ButEnter = 13;
constexpr unsigned long kLoopDelayMs = 25;

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "oled_display.h"
#include "WavPwmAudio.h"

#if defined(USE_WAVDATA2)
#include "WAVData2.h"
const unsigned short* const kWavs[] = {
  wav
};
#else
#include "WAVData.h"
const unsigned short* const kWavs[] = {
S01_WAV, // 0
S02_WAV, // 1
S03_WAV, // 2
S04_WAV, // 3
S05_WAV, // 4
S09_WAV, // 5
S10_WAV, // 6
S11_WAV, // 7
S12_WAV, // 8
S13_WAV, // 9
S14_WAV, // 10
S15_WAV, // 11
S16_WAV, // 12
S17_WAV, // 13
};
#endif

constexpr size_t kWavCount = sizeof(kWavs) / sizeof(kWavs[0]);

OledDisplay g_oledDisplay;

const char* const kMenuItems[] = {
  " Diagnostics",
  " 1st try    ",
  " 2nd try    ",
  " Don't try  ",
  " Relex      "
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
size_t g_nextWavIndex = 0;

void PlayDiagnosticAudio() {
  if (kWavCount == 0) {
    Serial.println(F("[AUDIO] No diagnostic clips configured"));
    return;
  }

  const size_t clip_index = g_nextWavIndex % kWavCount;
  const unsigned short* clip = kWavs[clip_index];
  if (!clip) {
    Serial.print(F("[AUDIO] Missing clip at index "));
    Serial.println(static_cast<unsigned>(clip_index));
    return;
  }

  Serial.print(F("[AUDIO] Playing diagnostic clip "));
  Serial.println(static_cast<unsigned>(clip_index));
  if (!WavPwmPlayAudio(clip)) {
    Serial.println(F("[AUDIO] Failed to start playback"));
    return;
  }

  while (WavPwmIsPlaying()) {
    delay(1);
  }

  g_nextWavIndex = (clip_index + 1) % kWavCount;
}

void HandleMenuSelection(size_t index) {
  switch (index) {
    case 0:
      Serial.println(F("[MENU] Running diagnostics"));
      PlayDiagnosticAudio();
      break;
    case 1:
      Serial.println(F("[MENU] 1st try selected"));
      break;
    case 2:
      Serial.println(F("[MENU] 2nd try selected"));
      break;
    case 3:
      Serial.println(F("[MENU] Don't try selected"));
      break;
    case 4:
      Serial.println(F("[MENU] Relax selected"));
      break;
    default:
      Serial.print(F("[MENU] Unhandled index "));
      Serial.println(static_cast<unsigned>(index));
      break;
  }
}

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
    HandleMenuSelection(g_mainMenu.selected_index);
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
  WavPwmInit(GPIO_AUDIO_OUT_LEFT);
  
  g_oledDisplay.scanBus();
  g_oledDisplay.begin();
  g_oledDisplay.setMenu(g_mainMenu);
}

void loop() {
  g_oledDisplay.loop();
  HandleMenuButtons();
  delay(kLoopDelayMs);
}
