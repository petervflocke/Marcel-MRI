#include <Arduino.h>

#define GPIO_AUDIO_OUT_LEFT  18

constexpr uint8_t ButDown = 14;
constexpr uint8_t ButEnter = 13;
constexpr unsigned long kDisplayUpdateIntervalMs = 25;
constexpr unsigned long kButtonDebounceMs = 100;

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
alarm_WAV,// 14
gandalf_WAV // 15
};
#endif

constexpr size_t kWavCount = sizeof(kWavs) / sizeof(kWavs[0]);

struct AudioSequence {
  const char* label;
  const uint8_t* clip_indexes;
  size_t length;
};

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

struct ButtonState {
  bool raw_level = true;
  bool stable_level = true;
  unsigned long last_change_ms = 0;
};

ButtonState g_buttonDownState;
ButtonState g_buttonEnterState;
unsigned long g_lastDisplayUpdateMs = 0;

struct SequencePlayerState {
  const AudioSequence* active_sequence = nullptr;
  size_t clip_index = 0;
};

constexpr uint8_t kDiagnosticClipIndexes[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kFirstTryClipIndexes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kSecondTryClipIndexes[] = {1, 1, 1, 1, 1, 1, 1};
constexpr uint8_t kDontTryClipIndexes[] = {2, 2, 3, 3, 3, 4, 4};
constexpr uint8_t kRelaxClipIndexes[] = {5, 5, 5, 6, 6, 6, 7, 7, 7};

const AudioSequence kDiagnosticSequence{
  "Diagnostics",
  kDiagnosticClipIndexes,
  sizeof(kDiagnosticClipIndexes) / sizeof(kDiagnosticClipIndexes[0])
};

const AudioSequence kFirstTrySequence{
  "1st try",
  kFirstTryClipIndexes,
  sizeof(kFirstTryClipIndexes) / sizeof(kFirstTryClipIndexes[0])
};

const AudioSequence kSecondTrySequence{
  "2nd try",
  kSecondTryClipIndexes,
  sizeof(kSecondTryClipIndexes) / sizeof(kSecondTryClipIndexes[0])
};

const AudioSequence kDontTrySequence{
  "Don't try",
  kDontTryClipIndexes,
  sizeof(kDontTryClipIndexes) / sizeof(kDontTryClipIndexes[0])
};

const AudioSequence kRelaxSequence{
  "Relax",
  kRelaxClipIndexes,
  sizeof(kRelaxClipIndexes) / sizeof(kRelaxClipIndexes[0])
};

SequencePlayerState g_sequencePlayer;

bool ButtonPressed(uint8_t pin, ButtonState& state);

bool SequenceIsActive() {
  return g_sequencePlayer.active_sequence != nullptr;
}

void ResetSequencePlayer() {
  g_sequencePlayer.active_sequence = nullptr;
  g_sequencePlayer.clip_index = 0;
  g_oledDisplay.setMenu(g_mainMenu);
}

bool StartCurrentSequenceClip() {
  if (!SequenceIsActive()) {
    return false;
  }

  const AudioSequence& sequence = *g_sequencePlayer.active_sequence;
  if (g_sequencePlayer.clip_index >= sequence.length) {
    return false;
  }

  const uint8_t wav_index = sequence.clip_indexes[g_sequencePlayer.clip_index];
  if (wav_index >= kWavCount) {
    Serial.print(F("[SEQ] Invalid WAV index "));
    Serial.println(static_cast<unsigned>(wav_index));
    return false;
  }

  const unsigned short* clip = kWavs[wav_index];
  if (!clip) {
    Serial.print(F("[SEQ] Missing clip data for index "));
    Serial.println(static_cast<unsigned>(wav_index));
    return false;
  }

  Serial.print(F("[SEQ] Playing WAV index "));
  Serial.println(static_cast<unsigned>(wav_index));

  if (!WavPwmPlayAudio(clip)) {
    Serial.println(F("[SEQ] Failed to start WAV playback"));
    return false;
  }

  return true;
}

bool StartSequencePlayback(const AudioSequence& sequence) {
  if (SequenceIsActive()) {
    Serial.println(F("[SEQ] Another sequence is already playing"));
    return false;
  }

  if (!sequence.clip_indexes || sequence.length == 0) {
    Serial.println(F("[SEQ] Empty sequence"));
    return false;
  }

  g_sequencePlayer.active_sequence = &sequence;
  g_sequencePlayer.clip_index = 0;

  Serial.print(F("[SEQ] Starting sequence: "));
  Serial.println(sequence.label ? sequence.label : "<unnamed>");

  if (!StartCurrentSequenceClip()) {
    WavPwmStopAudio();
    ResetSequencePlayer();
    return false;
  }

  return true;
}

void ServiceSequencePlayback() {
  if (!SequenceIsActive()) {
    return;
  }

  if (ButtonPressed(ButEnter, g_buttonEnterState)) {
    Serial.println(F("[SEQ] Sequence canceled"));
    WavPwmStopAudio();
    ResetSequencePlayer();
    return;
  }

  if (WavPwmIsPlaying()) {
    return;
  }

  AudioSequence const* sequence = g_sequencePlayer.active_sequence;
  if (!sequence) {
    ResetSequencePlayer();
    return;
  }

  g_sequencePlayer.clip_index++;
  if (g_sequencePlayer.clip_index >= sequence->length) {
    Serial.print(F("[SEQ] Sequence finished: "));
    Serial.println(sequence->label ? sequence->label : "<unnamed>");
    ResetSequencePlayer();
    return;
  }

  if (!StartCurrentSequenceClip()) {
    Serial.println(F("[SEQ] Aborting sequence due to playback error"));
    WavPwmStopAudio();
    ResetSequencePlayer();
  }
}

void HandleMenuSelection(size_t index) {
  if (SequenceIsActive()) {
    Serial.println(F("[MENU] Sequence already running"));
    return;
  }

  switch (index) {
    case 0:
      Serial.println(F("[MENU] Running diagnostics"));
      if (!StartSequencePlayback(kDiagnosticSequence)) {
        Serial.println(F("[MENU] Failed to start diagnostics sequence"));
      }
      break;
    case 1:
      Serial.println(F("[MENU] 1st try selected"));
      if (!StartSequencePlayback(kFirstTrySequence)) {
        Serial.println(F("[MENU] Failed to start 1st try sequence"));
      }
      break;
    case 2:
      Serial.println(F("[MENU] 2nd try selected"));
      if (!StartSequencePlayback(kSecondTrySequence)) {
        Serial.println(F("[MENU] Failed to start 2nd try sequence"));
      }
      break;
    case 3:
      Serial.println(F("[MENU] Don't try selected"));
      if (!StartSequencePlayback(kDontTrySequence)) {
        Serial.println(F("[MENU] Failed to start Don't try sequence"));
      }
      break;
    case 4:
      Serial.println(F("[MENU] Relax selected"));
      if (!StartSequencePlayback(kRelaxSequence)) {
        Serial.println(F("[MENU] Failed to start Relax sequence"));
      }
      break;
    default:
      Serial.print(F("[MENU] Unhandled index "));
      Serial.println(static_cast<unsigned>(index));
      break;
  }
}

bool ButtonPressed(uint8_t pin, ButtonState& state) {
  const bool current_level = gpio_get(pin);
  const unsigned long now = millis();

  if (current_level != state.raw_level) {
    state.raw_level = current_level;
    state.last_change_ms = now;
  }

  if ((now - state.last_change_ms) < kButtonDebounceMs) {
    return false;
  }

  if (current_level != state.stable_level) {
    state.stable_level = current_level;
    if (!state.stable_level) {
      return true;  // Only trigger on stable falling edge.
    }
  }

  return false;
}

void ServiceDisplay() {
  const unsigned long now = millis();
  if ((now - g_lastDisplayUpdateMs) < kDisplayUpdateIntervalMs) {
    return;
  }

  g_lastDisplayUpdateMs = now;
  g_oledDisplay.loop();
}

void HandleMenuButtons() {
  if (SequenceIsActive()) {
    return;
  }

  const size_t item_count = g_mainMenu.item_count;
  if (ButtonPressed(ButDown, g_buttonDownState) && item_count > 0) {
    g_mainMenu.selected_index = (g_mainMenu.selected_index + 1) % item_count;
    g_oledDisplay.setMenu(g_mainMenu);
  }

  if (ButtonPressed(ButEnter, g_buttonEnterState) && item_count > 0) {
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
  const bool down_level = gpio_get(ButDown);
  g_buttonDownState.raw_level = down_level;
  g_buttonDownState.stable_level = down_level;
  g_buttonDownState.last_change_ms = millis();

  const bool enter_level = gpio_get(ButEnter);
  g_buttonEnterState.raw_level = enter_level;
  g_buttonEnterState.stable_level = enter_level;
  g_buttonEnterState.last_change_ms = millis();
  WavPwmInit(GPIO_AUDIO_OUT_LEFT);
  
  g_oledDisplay.scanBus();
  g_oledDisplay.begin();
  g_oledDisplay.setMenu(g_mainMenu);
}

void loop() {
  ServiceDisplay();
  ServiceSequencePlayback();
  HandleMenuButtons();
}
