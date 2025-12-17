#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define GPIO_AUDIO_OUT_LEFT  18

constexpr uint8_t ButDown = 14;
constexpr uint8_t ButEnter = 13;
constexpr unsigned long kDisplayUpdateIntervalMs = 25;
constexpr unsigned long kButtonDebounceMs = 100;
constexpr float kPi = 3.1415926535f;

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "oled_display.h"
#include "PICsData.h"
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

struct SequenceVisuals {
  void (*on_start)();
  void (*on_stop)();
};

struct AudioSequence {
  const char* label;
  const uint8_t* clip_indexes;
  size_t length;
  const SequenceVisuals* visuals = nullptr;
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
  const SequenceVisuals* visuals = nullptr;
  bool visuals_active = false;
};

constexpr unsigned long kDiagnosticScrollIntervalMs = 80;
constexpr int kDiagnosticScrollStepPx = 1;
constexpr int kDiagnosticLineHeightPx = 8;

constexpr const char* const kDiagnosticScrollLines[] = {
  "%% Read the Pulseq f",
  "seq = mr.Sequence();",
  "seq.read('gre.seq');",
  "",
  "seq=mr.Sequence(sys)",
  "fov=256e-3; Nx=64;",
  "thickness=4e-3;",
  "sliceGap=1e-3;",
  "Nslices=8;",
  "",
  "amp=zeros(3,1);",
  "gradNames={'gx','gy'",
  "for iC=1:3",
  "grad = block.(gradNa",
  "if isGrad(iC) && str",
  " % Trapezoid gradien",
  " tRampUp(iC) = grad.",
  " tFlat(iC) = grad.fl",
  " tRampDown(iC) = gra",
  " amp(iC) = grad.ampl",
  "end",
  "",
  "% correct flip angle",
  "PVM_RefAttCh1 = 17.0",
  "flipRef=pi/2;",
  "tauRef=1e-3;",
  "refB1=flipRef/tauRef",
  "B1=2*pi*amp;",
  "",
  "for i=1:Ny",
  " for c=1:length(TE)",
  "  %seq.addBlock(rf_f",
  "  rf.phaseOffset=rf_",
  "  adc.phaseOffset=rf",
  "  rf_inc=mod(rf_inc+",
  "  rf_phase=mod(rf_ph",
  "  seq.addBlock(rf,gz",
  "  seq.addBlock(gxPre",
  "  seq.addBlock(mr.ma",
  "  seq.addBlock(gx,ad",
  "  seq.addBlock(mr.ma",
  " end",
  "end",
  "",
};

constexpr size_t kDiagnosticScrollLineCount =
    sizeof(kDiagnosticScrollLines) / sizeof(kDiagnosticScrollLines[0]);

struct DiagnosticScrollState {
  int scroll_offset_px = 0;
  unsigned long last_step_ms = 0;
};

DiagnosticScrollState g_diagnosticScrollState;

void DiagnosticScrollRender(Adafruit_SSD1306& display, void* context) {
  auto* state = static_cast<DiagnosticScrollState*>(context);
  const unsigned long now = millis();
  if (now - state->last_step_ms >= kDiagnosticScrollIntervalMs) {
    state->last_step_ms = now;
    state->scroll_offset_px += kDiagnosticScrollStepPx;
    const int total_height =
        static_cast<int>(kDiagnosticScrollLineCount) * kDiagnosticLineHeightPx;
    if (state->scroll_offset_px >= total_height) {
      state->scroll_offset_px = 0;
    }
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int y = -state->scroll_offset_px;
  for (size_t i = 0; i < kDiagnosticScrollLineCount; ++i) {
    display.setCursor(0, y);
    display.println(kDiagnosticScrollLines[i]);
    y += kDiagnosticLineHeightPx;
  }
}

void DiagnosticScrollStart() {
  g_diagnosticScrollState.scroll_offset_px = 0;
  g_diagnosticScrollState.last_step_ms = millis();
  g_oledDisplay.clearMenu();
  g_oledDisplay.setCustomRenderer(DiagnosticScrollRender,
                                  &g_diagnosticScrollState);
}

void DiagnosticScrollStop() {
  g_oledDisplay.clearCustomRenderer();
}

const SequenceVisuals kDiagnosticVisuals{
  DiagnosticScrollStart,
  DiagnosticScrollStop,
};

constexpr unsigned long kFirstTryDialIntervalMs = 200;
constexpr float kFirstTryDialStepDeg = 3.0f;
constexpr size_t kFirstTryDialStepCount =
    static_cast<size_t>(180.0f / kFirstTryDialStepDeg);
constexpr int kFirstTryDialRadiusPx = 28;
constexpr int kFirstTryDialLineExtendPx = 4;
constexpr int kFirstTryDialLineHalfThicknessPx = 1;

struct FirstTryDialState {
  unsigned long last_step_ms = 0;
  size_t filled_steps = 0;
  bool drawn_steps[kFirstTryDialStepCount] = {};
};

FirstTryDialState g_firstTryDialState;

void DrawDialCircle(Adafruit_SSD1306& display, int cx, int cy) {
  for (int r = kFirstTryDialRadiusPx - 1; r <= kFirstTryDialRadiusPx + 1; ++r) {
    if (r > 0) {
      display.drawCircle(cx, cy, r, SSD1306_WHITE);
    }
  }
}

void DrawDialLine(Adafruit_SSD1306& display,
                  int cx,
                  int cy,
                  float angle_deg) {
  const float angle_rad = angle_deg * (kPi / 180.0f);
  const float dir_x = cosf(angle_rad);
  const float dir_y = sinf(angle_rad);
  const float perp_x = -dir_y;
  const float perp_y = dir_x;
  const float half_len = static_cast<float>(kFirstTryDialRadiusPx + kFirstTryDialLineExtendPx);

  for (int offset = -kFirstTryDialLineHalfThicknessPx;
       offset <= kFirstTryDialLineHalfThicknessPx;
       ++offset) {
    const float offset_x = perp_x * static_cast<float>(offset);
    const float offset_y = perp_y * static_cast<float>(offset);
    const float start_x = static_cast<float>(cx) - dir_x * half_len + offset_x;
    const float start_y = static_cast<float>(cy) - dir_y * half_len + offset_y;
    const float end_x = static_cast<float>(cx) + dir_x * half_len + offset_x;
    const float end_y = static_cast<float>(cy) + dir_y * half_len + offset_y;
    display.drawLine(static_cast<int>(roundf(start_x)),
                     static_cast<int>(roundf(start_y)),
                     static_cast<int>(roundf(end_x)),
                     static_cast<int>(roundf(end_y)),
                     SSD1306_WHITE);
  }
}

void FirstTryDialRender(Adafruit_SSD1306& display, void* context) {
  auto* state = static_cast<FirstTryDialState*>(context);
  const unsigned long now = millis();
  if ((now - state->last_step_ms) >= kFirstTryDialIntervalMs &&
      state->filled_steps < kFirstTryDialStepCount) {
    state->last_step_ms = now;
    state->drawn_steps[state->filled_steps] = true;
    state->filled_steps++;
  }

  const int cx = display.width() / 2;
  const int cy = display.height() / 2;

  display.fillRect(0, 0, display.width(), display.height(), SSD1306_BLACK);
  DrawDialCircle(display, cx, cy);
  for (size_t i = 0; i < kFirstTryDialStepCount; ++i) {
    if (state->drawn_steps[i]) {
      const float angle = static_cast<float>(i) * kFirstTryDialStepDeg;
      DrawDialLine(display, cx, cy, angle);
    }
  }
}

void FirstTryDialStart() {
  g_firstTryDialState.last_step_ms = millis() - kFirstTryDialIntervalMs;
  g_firstTryDialState.filled_steps = 0;
  for (size_t i = 0; i < kFirstTryDialStepCount; ++i) {
    g_firstTryDialState.drawn_steps[i] = false;
  }
  g_oledDisplay.clearMenu();
  g_oledDisplay.setCustomRenderer(FirstTryDialRender, &g_firstTryDialState);
}

void FirstTryDialStop() {
  g_oledDisplay.clearCustomRenderer();
}

const SequenceVisuals kFirstTryVisuals{
  FirstTryDialStart,
  FirstTryDialStop,
};

constexpr unsigned long kSecondTryStripeIntervalMs = 250;
constexpr int kSecondTryImageWidthPx = 64;
constexpr int kSecondTryImageHeightPx = 64;
constexpr int kSecondTryStripeHeightPx = 4;
constexpr size_t kSecondTryStripeCount =
    kSecondTryImageHeightPx / kSecondTryStripeHeightPx;
constexpr size_t kSecondTryBytesPerRow =
    (kSecondTryImageWidthPx + 7) / 8;

const unsigned char* const kSecondTryImages[] = {
  epd_bitmap_br00,
  epd_bitmap_br01,
  epd_bitmap_br02,
};

constexpr size_t kSecondTryImageCount =
    sizeof(kSecondTryImages) / sizeof(kSecondTryImages[0]);

struct SecondTryImageState {
  size_t current_image = 0;
  size_t previous_image = 0;
  bool previous_active = false;
  size_t stripes_revealed = 0;
  unsigned long last_step_ms = 0;
};

SecondTryImageState g_secondTryImageState;

void DrawSecondTryStripe(Adafruit_SSD1306& display,
                         int origin_x,
                         int origin_y,
                         const unsigned char* bitmap,
                         size_t stripe_index) {
  const size_t offset =
      stripe_index * kSecondTryStripeHeightPx * kSecondTryBytesPerRow;
  display.drawBitmap(origin_x,
                     origin_y + static_cast<int>(stripe_index * kSecondTryStripeHeightPx),
                     bitmap + offset,
                     kSecondTryImageWidthPx,
                     kSecondTryStripeHeightPx,
                     SSD1306_WHITE);
}

void SecondTryImageRender(Adafruit_SSD1306& display, void* context) {
  auto* state = static_cast<SecondTryImageState*>(context);
  const unsigned long now = millis();
  if (kSecondTryImageCount > 0 &&
      (now - state->last_step_ms) >= kSecondTryStripeIntervalMs) {
    state->last_step_ms = now;
    if (state->stripes_revealed >= kSecondTryStripeCount) {
      state->previous_image = state->current_image;
      state->previous_active = true;
      state->current_image = (state->current_image + 1) % kSecondTryImageCount;
      state->stripes_revealed = 0;
    }
    state->stripes_revealed =
        (state->stripes_revealed + 1) % (kSecondTryStripeCount + 1);
  }

  if (kSecondTryImageCount == 0) {
    return;
  }

  const int x = (display.width() - kSecondTryImageWidthPx) / 2;
  const int y = (display.height() - kSecondTryImageHeightPx) / 2;
  if (state->previous_active && state->previous_image < kSecondTryImageCount) {
    const unsigned char* previous_bitmap =
        kSecondTryImages[state->previous_image];
    display.drawBitmap(x, y, previous_bitmap, kSecondTryImageWidthPx,
                       kSecondTryImageHeightPx, SSD1306_WHITE);
  }

  const unsigned char* bitmap = kSecondTryImages[state->current_image];
  const size_t stripes_to_draw =
      (state->stripes_revealed > kSecondTryStripeCount)
          ? kSecondTryStripeCount
          : state->stripes_revealed;
  for (size_t stripe = 0; stripe < stripes_to_draw; ++stripe) {
    DrawSecondTryStripe(display, x, y, bitmap, stripe);
  }

  if (stripes_to_draw >= kSecondTryStripeCount) {
    state->previous_active = false;
  }
}

void SecondTryImageStart() {
  g_secondTryImageState.current_image = 0;
  g_secondTryImageState.previous_image = 0;
  g_secondTryImageState.previous_active = false;
  g_secondTryImageState.stripes_revealed = 0;
  g_secondTryImageState.last_step_ms = millis();
  g_oledDisplay.clearMenu();
  g_oledDisplay.setCustomRenderer(SecondTryImageRender, &g_secondTryImageState);
}

void SecondTryImageStop() {
  g_oledDisplay.clearCustomRenderer();
}

const SequenceVisuals kSecondTryVisuals{
  SecondTryImageStart,
  SecondTryImageStop,
};

constexpr uint8_t kDiagnosticClipIndexes[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kFirstTryClipIndexes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kSecondTryClipIndexes[] = {0, 0, 0, 0, 5, 6, 6, 6, 9, 9, 9, 9, 11, 11, 11, 10};
constexpr uint8_t kDontTryClipIndexes[] = {2, 2, 3, 3, 3, 4, 4};
constexpr uint8_t kRelaxClipIndexes[] = {14,14,14,14,14,14,15,15,15,15,15,15};

const AudioSequence kDiagnosticSequence{
  "Diagnostics",
  kDiagnosticClipIndexes,
  sizeof(kDiagnosticClipIndexes) / sizeof(kDiagnosticClipIndexes[0]),
  &kDiagnosticVisuals
};

const AudioSequence kFirstTrySequence{
  "1st try",
  kFirstTryClipIndexes,
  sizeof(kFirstTryClipIndexes) / sizeof(kFirstTryClipIndexes[0]),
  &kFirstTryVisuals
};

const AudioSequence kSecondTrySequence{
  "2nd try",
  kSecondTryClipIndexes,
  sizeof(kSecondTryClipIndexes) / sizeof(kSecondTryClipIndexes[0]),
  &kSecondTryVisuals
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

void StopSequenceVisuals() {
  if (g_sequencePlayer.visuals_active && g_sequencePlayer.visuals &&
      g_sequencePlayer.visuals->on_stop) {
    g_sequencePlayer.visuals->on_stop();
  }
  g_sequencePlayer.visuals_active = false;
  g_sequencePlayer.visuals = nullptr;
}

void ResetSequencePlayer() {
  StopSequenceVisuals();
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
  g_sequencePlayer.visuals = sequence.visuals;
  g_sequencePlayer.visuals_active = false;

  Serial.print(F("[SEQ] Starting sequence: "));
  Serial.println(sequence.label ? sequence.label : "<unnamed>");

  if (!StartCurrentSequenceClip()) {
    WavPwmStopAudio();
    ResetSequencePlayer();
    return false;
  }

  if (g_sequencePlayer.visuals && g_sequencePlayer.visuals->on_start) {
    g_sequencePlayer.visuals->on_start();
    g_sequencePlayer.visuals_active = true;
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
  if (g_oledDisplay.customRendererActive()) {
    g_oledDisplay.forceRender();
  } else {
    g_oledDisplay.loop();
  }
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
