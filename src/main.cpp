#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define GPIO_AUDIO_OUT_LEFT  0

extern const uint8_t ButDown = 13; 
extern const uint8_t ButEnter = 12; 
extern const uint8_t LedAlarmPin = 11; 
extern const uint8_t OnLEDPin = 10; 
constexpr unsigned long kDisplayUpdateIntervalMs = 25;
constexpr unsigned long kButtonDebounceMs = 100;
constexpr float kPi = 3.1415926535f;

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "oled_display.h"
#include "PICsData.h"
#include "WavPwmAudio.h"
#include "t-rex-duino/DinoGame.h"

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
evacuate_WAV, // 15
EPSI01_WAV, // 16
EPSI02_WAV, // 17
EPSI03_WAV // 18
};
#endif

constexpr uint8_t kDiagnosticClipIndexes[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 18, 13};  
constexpr uint8_t kFirstTryClipIndexes[] = {
  16, 17, 17, 17, 17, 17, 18};
constexpr uint8_t kSecondTryClipIndexes[] = {
  0, 0, 0, 0, 5, 6, 6, 6, 9, 9, 9, 9, 11, 11, 11, 11};
constexpr uint8_t kDontTryClipIndexes[] = {
  16, 17, 17, 17, 17, 18,  16, 17, 17, 17, 17, 18, 14, 14, 14, 15, 15, 15};
constexpr uint8_t kRelaxClipIndexes[] = {};


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
  bool led_fx_enabled = false;
  const uint32_t* led_fx_palette = nullptr;
  size_t led_fx_palette_len = 0;
};

OledDisplay g_oledDisplay;

const char* const kMenuItems[] = {
  " Localizer " , // Diagnostic
  " Debug Code",  // 1st
  " rEPSI     ",  // 2nd
  " rEPSI, more averages", //dont
  " Relex     "
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
bool g_playingDinoGame = false;

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
constexpr size_t kFirstTryDialLineCount = 60;
constexpr int kFirstTryOvalRadiusXPx = 34;
constexpr int kFirstTryOvalRadiusYPx = 20;
constexpr int kFirstTryDialLineHalfThicknessPx = 1;
constexpr int kFirstTryLineBaseRadiusPx =
    (kFirstTryOvalRadiusXPx > kFirstTryOvalRadiusYPx) ? kFirstTryOvalRadiusXPx
                                                      : kFirstTryOvalRadiusYPx;
constexpr int kFirstTryLineExtendMinPx = -4;
constexpr int kFirstTryLineExtendMaxPx = 6;
constexpr float kFirstTryGoldenIncrementDeg = 111.246f;  // 180 / φ
constexpr float kFirstTryAngleJitterDeg = 5.0f;

struct FirstTryDialLine {
  float angle_deg = 0.f;
  int length_offset_px = 0;
};

struct FirstTryDialState {
  unsigned long last_step_ms = 0;
  size_t line_count = 0;
  float current_angle_deg = 0.f;
  FirstTryDialLine lines[kFirstTryDialLineCount];
};

FirstTryDialState g_firstTryDialState;

float WrapAngle180(float angle_deg) {
  while (angle_deg >= 180.0f) angle_deg -= 180.0f;
  while (angle_deg < 0.0f) angle_deg += 180.0f;
  return angle_deg;
}

float RandomFloat(float min_value, float max_value) {
  const float unit =
      static_cast<float>(random(0L, 10000L)) / 9999.0f;  // [0,1]
  return min_value + (max_value - min_value) * unit;
}

int RandomLengthOffset() {
  const int range =
      (kFirstTryLineExtendMaxPx - kFirstTryLineExtendMinPx) + 1;
  const int value = static_cast<int>(random(static_cast<long>(range)));
  return kFirstTryLineExtendMinPx + value;
}

FirstTryDialLine CreateNextDialLine(FirstTryDialState& state) {
  FirstTryDialLine line;
  float candidate =
      WrapAngle180(state.current_angle_deg + kFirstTryGoldenIncrementDeg);
  const float jitter = RandomFloat(-kFirstTryAngleJitterDeg,
                                   kFirstTryAngleJitterDeg);
  line.angle_deg = WrapAngle180(candidate + jitter);
  state.current_angle_deg = line.angle_deg;
  line.length_offset_px = RandomLengthOffset();
  return line;
}

void DrawDialOval(Adafruit_SSD1306& display, int cx, int cy) {
  for (int x = -kFirstTryOvalRadiusXPx; x <= kFirstTryOvalRadiusXPx; ++x) {
    const float normalized_x =
        static_cast<float>(x) / static_cast<float>(kFirstTryOvalRadiusXPx);
    const float inside = 1.0f - normalized_x * normalized_x;
    if (inside < 0.0f) continue;
    const float y_f = sqrtf(inside) * static_cast<float>(kFirstTryOvalRadiusYPx);
    const int y = static_cast<int>(roundf(y_f));
    for (int offset = -1; offset <= 1; ++offset) {
      display.drawPixel(cx + x, cy + y + offset, SSD1306_WHITE);
      display.drawPixel(cx + x, cy - y + offset, SSD1306_WHITE);
    }
  }
}

void DrawDialLine(Adafruit_SSD1306& display,
                  int cx,
                  int cy,
                  float angle_deg,
                  int length_offset_px) {
  const float angle_rad = angle_deg * (kPi / 180.0f);
  const float dir_x = cosf(angle_rad);
  const float dir_y = sinf(angle_rad);
  const float perp_x = -dir_y;
  const float perp_y = dir_x;
  const int base_len = kFirstTryLineBaseRadiusPx + length_offset_px;
  const float half_len =
      static_cast<float>(max(4, base_len));

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
      state->line_count < kFirstTryDialLineCount) {
    state->last_step_ms = now;
    state->lines[state->line_count++] = CreateNextDialLine(*state);
  }

  const int cx = display.width() / 2;
  const int cy = display.height() / 2;

  display.fillRect(0, 0, display.width(), display.height(), SSD1306_BLACK);
  DrawDialOval(display, cx, cy);
  for (size_t i = 0; i < state->line_count; ++i) {
    DrawDialLine(display, cx, cy, state->lines[i].angle_deg,
                 state->lines[i].length_offset_px);
  }
}

void FirstTryDialStart() {
  g_firstTryDialState.last_step_ms = millis() - kFirstTryDialIntervalMs;
  g_firstTryDialState.line_count = 0;
  g_firstTryDialState.current_angle_deg = RandomFloat(0.0f, 180.0f);
  g_oledDisplay.setCustomRenderer(FirstTryDialRender, &g_firstTryDialState);
  g_oledDisplay.clearMenu();
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

constexpr unsigned long kDontTryStripeIntervalMs = 800;
constexpr int kDontTryImageWidthPx = 64;
constexpr int kDontTryImageHeightPx = 64;
constexpr int kDontTryStripeHeightPx = 2;
constexpr size_t kDontTryStripeCount =
    kDontTryImageHeightPx / kDontTryStripeHeightPx;
constexpr size_t kDontTryStripeCutoff = kDontTryStripeCount / 2;
constexpr size_t kDontTryBytesPerRow =
    (kDontTryImageWidthPx + 7) / 8;
constexpr unsigned long kDontTryFlashIntervalMs = 250;
constexpr unsigned long kDontTryPostScanDelayMs = 5000;
constexpr unsigned long kDontTryLedFlashDurationMs = 4500;
constexpr unsigned long kDontTryFlashDurationMs = 4000;
constexpr unsigned long kDontTryFinalDisplayDurationMs = 3000;
constexpr unsigned long kDontTryDistinguishDurationMs = 30000;
constexpr unsigned long kDontTryDistinguishFlashIntervalMs = 1000;
constexpr unsigned long kDontTrySavedMessageDurationMs = 10000;
constexpr unsigned long kDontTryTimeoutMessageDurationMs = 10000;
constexpr unsigned long kDontTryLedFlashIntervalMs = 100;

enum class DontTryPhase : uint8_t {
  Loading,
  PreAlarmPause,
  AlarmFlash,
  FinalMessage,
  DistinguishFire,
  SavedMessage,
  TimeoutMessage,
  Done,
};

struct DontTryVisualState {
  DontTryPhase phase = DontTryPhase::Loading;
  size_t stripes_revealed = 0;
  unsigned long last_step_ms = 0;
  unsigned long phase_start_ms = 0;
  bool flash_on = true;
  unsigned long led_last_toggle_ms = 0;
  bool led_on = false;
  bool led_state = false;
};

DontTryVisualState g_dontTryVisualState;

const unsigned char* const kDontTryBitmap = epd_bitmap_br02;

void LedFxStop();

int16_t MeasureTextWidth(const char* text, uint8_t size) {
  if (!text) {
    return 0;
  }
  const size_t len = strlen(text);
  return static_cast<int16_t>(len * 6 * size);
}

int16_t MeasureLineHeight(uint8_t size) {
  return static_cast<int16_t>(8 * size);
}

void DrawCenteredLines(Adafruit_SSD1306& display,
                       const char* const* lines,
                       size_t line_count,
                       uint8_t size) {
  if (!lines || line_count == 0) {
    return;
  }
  const int16_t line_height = MeasureLineHeight(size);
  const int16_t total_height =
      static_cast<int16_t>(line_count) * line_height;
  int16_t y = (display.height() - total_height) / 2;
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  for (size_t i = 0; i < line_count; ++i) {
    const char* text = lines[i] ? lines[i] : "";
    const int16_t text_width = MeasureTextWidth(text, size);
    int16_t x = (display.width() - text_width) / 2;
    if (x < 0) {
      x = 0;
    }
    display.setCursor(x, y);
    display.print(text);
    y += line_height;
  }
}

void DontTryRenderLoading(Adafruit_SSD1306& display,
                          DontTryVisualState& state,
                          unsigned long now) {
  if ((now - state.last_step_ms) >= kDontTryStripeIntervalMs &&
      state.stripes_revealed < kDontTryStripeCutoff) {
    state.last_step_ms = now;
    state.stripes_revealed++;
    if (state.stripes_revealed >= kDontTryStripeCutoff) {
      state.phase = DontTryPhase::PreAlarmPause;
      state.phase_start_ms = now;
      display.clearDisplay();
      return;
    }
  }

  display.clearDisplay();
  const int origin_x = (display.width() - kDontTryImageWidthPx) / 2;
  const int origin_y = (display.height() - kDontTryImageHeightPx) / 2;
  const size_t stripes_to_draw = state.stripes_revealed;
  for (size_t stripe = 0; stripe < stripes_to_draw; ++stripe) {
    const size_t offset =
        stripe * kDontTryStripeHeightPx * kDontTryBytesPerRow;
    display.drawBitmap(origin_x,
                       origin_y +
                           static_cast<int>(stripe * kDontTryStripeHeightPx),
                       kDontTryBitmap + offset,
                       kDontTryImageWidthPx,
                       kDontTryStripeHeightPx,
                       SSD1306_WHITE);
  }
}

void DontTryRenderPreAlarmPause(Adafruit_SSD1306& display,
                                DontTryVisualState& state,
                                unsigned long now) {
  display.clearDisplay();
  const int origin_x = (display.width() - kDontTryImageWidthPx) / 2;
  const int origin_y = (display.height() - kDontTryImageHeightPx) / 2;
  for (size_t stripe = 0; stripe < kDontTryStripeCutoff; ++stripe) {
    const size_t offset =
        stripe * kDontTryStripeHeightPx * kDontTryBytesPerRow;
    display.drawBitmap(origin_x,
                       origin_y +
                           static_cast<int>(stripe * kDontTryStripeHeightPx),
                       kDontTryBitmap + offset,
                       kDontTryImageWidthPx,
                       kDontTryStripeHeightPx,
                       SSD1306_WHITE);
  }

  if ((now - state.phase_start_ms) >= kDontTryPostScanDelayMs) {
    state.phase = DontTryPhase::AlarmFlash;
    state.phase_start_ms = now;
    state.last_step_ms = now;
    state.flash_on = true;
    state.led_on = true;
    state.led_state = true;
    state.led_last_toggle_ms = now;
    LedFxStop();
    digitalWrite(OnLEDPin, LOW);
    digitalWrite(LedAlarmPin, state.led_state ? HIGH : LOW);
  }
}

void UpdateDontTryAlarmLed(DontTryVisualState& state, unsigned long now) {
  if ((now - state.led_last_toggle_ms) >= kDontTryLedFlashIntervalMs) {
    state.led_last_toggle_ms = now;
    state.led_state = !state.led_state;
    digitalWrite(LedAlarmPin, state.led_state ? HIGH : LOW);
  }
}

void DontTryRenderAlarm(Adafruit_SSD1306& display,
                        DontTryVisualState& state,
                        unsigned long now) {
  if ((now - state.last_step_ms) >= kDontTryFlashIntervalMs) {
    state.last_step_ms = now;
    state.flash_on = !state.flash_on;
  }

  if (state.led_on) {
    UpdateDontTryAlarmLed(state, now);
  }

  display.clearDisplay();
  if (state.flash_on) {
    static const char* const kAlarmLines[] = {"ALARM", " ", "FIRE"};
    DrawCenteredLines(display, kAlarmLines,
                      sizeof(kAlarmLines) / sizeof(kAlarmLines[0]), 2);
  }

  if ((now - state.phase_start_ms) >= kDontTryFlashDurationMs) {
    state.phase = DontTryPhase::FinalMessage;
    state.phase_start_ms = now;
  }
}

void DontTryRenderFinal(Adafruit_SSD1306& display,
                        DontTryVisualState& state,
                        unsigned long now) {
  display.clearDisplay();
  static const char* const kFinalLines[] = {"Evacuate", "all", "patients!"};
  DrawCenteredLines(display, kFinalLines,
                    sizeof(kFinalLines) / sizeof(kFinalLines[0]), 2);
  if (state.led_on) {
    UpdateDontTryAlarmLed(state, now);
  }
  if ((now - state.phase_start_ms) >= kDontTryFinalDisplayDurationMs) {
    state.phase = DontTryPhase::DistinguishFire;
    state.phase_start_ms = now;
    state.last_step_ms = now;
    state.flash_on = true;
  }
}

void DontTryRenderDistinguish(Adafruit_SSD1306& display,
                              DontTryVisualState& state,
                              unsigned long now) {
  if ((now - state.last_step_ms) >= kDontTryDistinguishFlashIntervalMs) {
    state.last_step_ms = now;
    state.flash_on = !state.flash_on;
  }

  if (state.led_on) {
    UpdateDontTryAlarmLed(state, now);
  }

  display.clearDisplay();
  if (state.flash_on) {
    static const char* const kDistinguishLines[] = {
        "DISTIN-", "GUISH", "THE", "FIRE"};
    DrawCenteredLines(display, kDistinguishLines,
                      sizeof(kDistinguishLines) /
                          sizeof(kDistinguishLines[0]),
                      2);
  }

  if ((now - state.phase_start_ms) >= kDontTryDistinguishDurationMs) {
    state.phase = DontTryPhase::TimeoutMessage;
    state.phase_start_ms = now;
  }
}

void DontTryRenderSavedMessage(Adafruit_SSD1306& display,
                               DontTryVisualState& state,
                               unsigned long now) {
  if (state.led_on) {
    state.led_on = false;
    digitalWrite(LedAlarmPin, LOW);
    digitalWrite(OnLEDPin, HIGH);
  }
  display.clearDisplay();
  static const char* const kSavedLines[] = {
      "You saved the scanner", "congratulations"};
  DrawCenteredLines(display, kSavedLines,
                    sizeof(kSavedLines) / sizeof(kSavedLines[0]), 1);
  if ((now - state.phase_start_ms) >= kDontTrySavedMessageDurationMs) {
    state.phase = DontTryPhase::Done;
    state.phase_start_ms = now;
  }
}

void DontTryRenderTimeoutMessage(Adafruit_SSD1306& display,
                                 DontTryVisualState& state,
                                 unsigned long now) {
  if (state.led_on) {
    state.led_on = false;
    digitalWrite(LedAlarmPin, LOW);
    digitalWrite(OnLEDPin, HIGH);
  }
  display.clearDisplay();
  static const char* const kTimeoutLines[] = {
      "Siemens Team arrived", "and saved", "the scanner"};
  DrawCenteredLines(display, kTimeoutLines,
                    sizeof(kTimeoutLines) / sizeof(kTimeoutLines[0]), 1);
  if ((now - state.phase_start_ms) >= kDontTryTimeoutMessageDurationMs) {
    state.phase = DontTryPhase::Done;
    state.phase_start_ms = now;
  }
}

void DontTryRenderDone(Adafruit_SSD1306& display,
                       DontTryVisualState& state,
                       unsigned long now) {
  (void)state;
  (void)now;
  display.clearDisplay();
}

void DontTryRender(Adafruit_SSD1306& display, void* context) {
  auto* state = static_cast<DontTryVisualState*>(context);
  const unsigned long now = millis();
  switch (state->phase) {
    case DontTryPhase::Loading:
      DontTryRenderLoading(display, *state, now);
      break;
    case DontTryPhase::PreAlarmPause:
      DontTryRenderPreAlarmPause(display, *state, now);
      break;
    case DontTryPhase::AlarmFlash:
      DontTryRenderAlarm(display, *state, now);
      break;
    case DontTryPhase::FinalMessage:
      DontTryRenderFinal(display, *state, now);
      break;
    case DontTryPhase::DistinguishFire:
      DontTryRenderDistinguish(display, *state, now);
      break;
    case DontTryPhase::SavedMessage:
      DontTryRenderSavedMessage(display, *state, now);
      break;
    case DontTryPhase::TimeoutMessage:
      DontTryRenderTimeoutMessage(display, *state, now);
      break;
    case DontTryPhase::Done:
      DontTryRenderDone(display, *state, now);
      break;
  }
}

void DontTryStart() {
  g_dontTryVisualState = {};
  g_dontTryVisualState.phase = DontTryPhase::Loading;
  g_dontTryVisualState.last_step_ms = millis();
  g_dontTryVisualState.phase_start_ms = g_dontTryVisualState.last_step_ms;
  if (Adafruit_SSD1306* display = g_oledDisplay.rawDisplay()) {
    display->clearDisplay();
    display->display();
  }
  g_oledDisplay.setCustomRenderer(DontTryRender, &g_dontTryVisualState);
  g_oledDisplay.clearMenu();
}

void DontTryStop() {
  digitalWrite(LedAlarmPin, LOW);
  digitalWrite(OnLEDPin, HIGH);
  g_oledDisplay.clearCustomRenderer();
}

constexpr size_t kLedFxStepCount = 10;
constexpr unsigned long kLedFxStepIntervalMs = 100;
constexpr uint8_t kLedFxBrightness = 32;

const uint32_t kLedFxPaletteDefault[kLedFxStepCount] = {
  0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x00FF7F,
  0x00FFFF, 0x007FFF, 0x0000FF, 0x7F00FF, 0xFF00FF
};

const uint32_t kLedFxPaletteDiagnostic[kLedFxStepCount] = {
  0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x00FF7F,
  0x00FFFF, 0x007FFF, 0x0000FF, 0x7F00FF, 0xFF00FF
};

const uint32_t kLedFxPaletteFirstTry[kLedFxStepCount] = {
  0x00FF00, 0x00FF33, 0x00FF66, 0x00FF99, 0x00FFCC,
  0x00FF99, 0x00FF66, 0x00FF33, 0x00FF00, 0x33FF66
};

const uint32_t kLedFxPaletteSecondTry[kLedFxStepCount] = {
  0x0000FF, 0x0033FF, 0x0066FF, 0x0099FF, 0x00CCFF,
  0x0099FF, 0x0066FF, 0x0033FF, 0x0000FF, 0x3300FF
};

const uint32_t kLedFxPaletteDontTry[kLedFxStepCount] = {
  0xFF0000, 0xCC0000, 0x990000, 0x660000, 0x330000,
  0x660000, 0x990000, 0xCC0000, 0xFF0000, 0xFF3300
};

const SequenceVisuals kDontTryVisuals{
  DontTryStart,
  DontTryStop,
};

const AudioSequence kDiagnosticSequence{
  "Diagnostics",
  kDiagnosticClipIndexes,
  sizeof(kDiagnosticClipIndexes) / sizeof(kDiagnosticClipIndexes[0]),
  &kDiagnosticVisuals,
  true,
  kLedFxPaletteDiagnostic,
  kLedFxStepCount
};

const AudioSequence kFirstTrySequence{
  "1st try",
  kFirstTryClipIndexes,
  sizeof(kFirstTryClipIndexes) / sizeof(kFirstTryClipIndexes[0]),
  &kFirstTryVisuals,
  true,
  kLedFxPaletteFirstTry,
  kLedFxStepCount
};

const AudioSequence kSecondTrySequence{
  "2nd try",
  kSecondTryClipIndexes,
  sizeof(kSecondTryClipIndexes) / sizeof(kSecondTryClipIndexes[0]),
  &kSecondTryVisuals,
  true,
  kLedFxPaletteSecondTry,
  kLedFxStepCount
};

const AudioSequence kDontTrySequence{
  "Don't try",
  kDontTryClipIndexes,
  sizeof(kDontTryClipIndexes) / sizeof(kDontTryClipIndexes[0]),
  &kDontTryVisuals,
  true,
  kLedFxPaletteDontTry,
  kLedFxStepCount
};

const AudioSequence kRelaxSequence{
  "Relax",
  kRelaxClipIndexes,
  sizeof(kRelaxClipIndexes) / sizeof(kRelaxClipIndexes[0])
};

SequencePlayerState g_sequencePlayer;

bool ButtonPressed(uint8_t pin, ButtonState& state);
void LedFxStart(const uint32_t* palette, size_t palette_len);
void LedFxStop();
void ServiceLedFx();
void LedFxFlash(uint32_t rgb, unsigned long duration_ms);

constexpr uint8_t kWs2812Pin = 16;
constexpr float kWs2812BitRateHz = 800000.0f;
constexpr uint8_t kWs2812TimingT1 = 2;
constexpr uint8_t kWs2812TimingT2 = 5;
constexpr uint8_t kWs2812TimingT3 = 3;
constexpr uint8_t kWs2812CyclesPerBit =
    kWs2812TimingT1 + kWs2812TimingT2 + kWs2812TimingT3;

struct LedFxState {
  bool active = false;
  size_t step_index = 0;
  unsigned long last_step_ms = 0;
  const uint32_t* palette = nullptr;
  size_t palette_len = 0;
};

LedFxState g_ledFxState;

struct LedFxFlashState {
  bool active = false;
  uint32_t color = 0;
  unsigned long end_ms = 0;
};

LedFxFlashState g_ledFxFlashState;

const uint16_t kWs2812ProgramInstructions[] = {
  0x6221, // out x, 1 side 0 [2]
  0x1123, // jmp !x, 3 side 1 [1]
  0x1400, // jmp 0 side 1 [4]
  0xa442, // nop side 0 [4]
};

const struct pio_program kWs2812Program = {
  .instructions = kWs2812ProgramInstructions,
  .length = 4,
  .origin = -1,
};

PIO g_ws2812_pio = pio0;
uint g_ws2812_sm = 0;
uint g_ws2812_offset = 0;
bool g_ws2812_ready = false;

uint32_t ApplyBrightness(uint32_t rgb, uint8_t brightness) {
  const uint8_t r = (rgb >> 16) & 0xFF;
  const uint8_t g = (rgb >> 8) & 0xFF;
  const uint8_t b = rgb & 0xFF;
  const uint8_t r_out = (static_cast<uint16_t>(r) * brightness) / 255;
  const uint8_t g_out = (static_cast<uint16_t>(g) * brightness) / 255;
  const uint8_t b_out = (static_cast<uint16_t>(b) * brightness) / 255;
  return (static_cast<uint32_t>(r_out) << 16) |
         (static_cast<uint32_t>(g_out) << 8) |
         static_cast<uint32_t>(b_out);
}

void Ws2812PioInit() {
  g_ws2812_offset = pio_add_program(g_ws2812_pio, &kWs2812Program);
  pio_gpio_init(g_ws2812_pio, kWs2812Pin);
  pio_sm_set_consecutive_pindirs(g_ws2812_pio, g_ws2812_sm, kWs2812Pin, 1, true);

  pio_sm_config config = pio_get_default_sm_config();
  sm_config_set_wrap(&config, g_ws2812_offset + 0, g_ws2812_offset + 3);
  sm_config_set_sideset(&config, 1, false, false);
  sm_config_set_sideset_pins(&config, kWs2812Pin);
  sm_config_set_out_shift(&config, false, true, 24);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

  const float div =
      static_cast<float>(clock_get_hz(clk_sys)) /
      (kWs2812BitRateHz * static_cast<float>(kWs2812CyclesPerBit));
  sm_config_set_clkdiv(&config, div);

  pio_sm_init(g_ws2812_pio, g_ws2812_sm, g_ws2812_offset, &config);
  pio_sm_set_pins_with_mask(g_ws2812_pio, g_ws2812_sm, 0u, 1u << kWs2812Pin);
  pio_sm_set_enabled(g_ws2812_pio, g_ws2812_sm, true);
  g_ws2812_ready = true;
}

void Ws2812Set(uint32_t rgb) {
  if (!g_ws2812_ready) {
    return;
  }
  const uint32_t scaled = ApplyBrightness(rgb, kLedFxBrightness);
  const uint32_t grb = ((scaled >> 8) & 0x00FF00)
                     | ((scaled << 8) & 0xFF0000)
                     |  (scaled       & 0x0000FF);
  pio_sm_put_blocking(g_ws2812_pio, g_ws2812_sm, grb << 8u);
}

void LedFxStart(const uint32_t* palette, size_t palette_len) {
  g_ledFxState.active = true;
  g_ledFxState.step_index = 0;
  g_ledFxState.last_step_ms = millis();
  if (!palette || palette_len == 0) {
    palette = kLedFxPaletteDefault;
    palette_len = kLedFxStepCount;
  }
  g_ledFxState.palette = palette;
  g_ledFxState.palette_len = palette_len;
  Ws2812Set(g_ledFxState.palette[g_ledFxState.step_index]);
}

void LedFxStop() {
  g_ledFxState.active = false;
  g_ledFxFlashState.active = false;
  Ws2812Set(0x000000);
}

void LedFxFlash(uint32_t rgb, unsigned long duration_ms) {
  g_ledFxFlashState.active = true;
  g_ledFxFlashState.color = rgb;
  g_ledFxFlashState.end_ms = millis() + duration_ms;
  Ws2812Set(rgb);
}

void ServiceLedFx() {
  const unsigned long now = millis();
  if (g_ledFxFlashState.active) {
    if (now >= g_ledFxFlashState.end_ms) {
      g_ledFxFlashState.active = false;
      if (g_ledFxState.active) {
        g_ledFxState.last_step_ms = now;
        const uint32_t* palette =
            g_ledFxState.palette ? g_ledFxState.palette : kLedFxPaletteDefault;
        const size_t palette_len =
            g_ledFxState.palette_len == 0 ? kLedFxStepCount
                                          : g_ledFxState.palette_len;
        Ws2812Set(palette[g_ledFxState.step_index % palette_len]);
      } else {
        Ws2812Set(0x000000);
      }
    }
    return;
  }
  if (!g_ledFxState.active) {
    return;
  }
  if ((now - g_ledFxState.last_step_ms) < kLedFxStepIntervalMs) {
    return;
  }
  g_ledFxState.last_step_ms = now;
  const size_t palette_len =
      g_ledFxState.palette_len == 0 ? kLedFxStepCount : g_ledFxState.palette_len;
  g_ledFxState.step_index = (g_ledFxState.step_index + 1) % palette_len;
  const uint32_t* palette =
      g_ledFxState.palette ? g_ledFxState.palette : kLedFxPaletteDefault;
  Ws2812Set(palette[g_ledFxState.step_index]);
}

constexpr uint32_t kDinoCollisionFlashColor = 0x660000;
constexpr unsigned long kDinoCollisionFlashMs = 180;

void DinoCollisionFlash() {
  LedFxFlash(kDinoCollisionFlashColor, kDinoCollisionFlashMs);
}

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
  LedFxStop();
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

  if (sequence.led_fx_enabled) {
    LedFxStart(sequence.led_fx_palette, sequence.led_fx_palette_len);
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
    if (g_sequencePlayer.active_sequence == &kDontTrySequence &&
        g_sequencePlayer.visuals_active &&
        g_dontTryVisualState.phase == DontTryPhase::DistinguishFire) {
      g_dontTryVisualState.phase = DontTryPhase::SavedMessage;
      g_dontTryVisualState.phase_start_ms = millis();
      return;
    }
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

  const bool sequence_finished =
      (g_sequencePlayer.clip_index + 1 >= sequence->length);
  if (sequence == &kDontTrySequence && g_sequencePlayer.visuals_active) {
    if (sequence_finished) {
      if (g_dontTryVisualState.phase != DontTryPhase::Done) {
        return;
      }
      ResetSequencePlayer();
      return;
    }
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
      Serial.println(F("[MENU] Localizer"));
      if (!StartSequencePlayback(kSecondTrySequence)) {
        Serial.println(F("[MENU] Failed to start diagnostics sequence"));
      }
      break;
    case 1:
      Serial.println(F("[MENU] Debug Code"));
      if (!StartSequencePlayback(kDiagnosticSequence)) {
        Serial.println(F("[MENU] Failed to start 1st try sequence"));
      }
      break;
    case 2:
      Serial.println(F("[MENU] rEPSI"));
      if (!StartSequencePlayback(kFirstTrySequence)) {
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
      LedFxStop();
      digitalWrite(OnLEDPin, LOW);
      g_playingDinoGame = true;
      DinoGame::Run();
      g_playingDinoGame = false;
      g_oledDisplay.setMenu(g_mainMenu);
      digitalWrite(OnLEDPin, HIGH);
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
  if (g_playingDinoGame) {
    return;
  }

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
  pinMode(LedAlarmPin, OUTPUT);
  digitalWrite(LedAlarmPin, LOW);
  pinMode(OnLEDPin, OUTPUT);
  digitalWrite(OnLEDPin, LOW);
  const bool down_level = gpio_get(ButDown);
  g_buttonDownState.raw_level = down_level;
  g_buttonDownState.stable_level = down_level;
  g_buttonDownState.last_change_ms = millis();

  const bool enter_level = gpio_get(ButEnter);
  g_buttonEnterState.raw_level = enter_level;
  g_buttonEnterState.stable_level = enter_level;
  g_buttonEnterState.last_change_ms = millis();
  WavPwmInit(GPIO_AUDIO_OUT_LEFT);

  Ws2812PioInit();
  LedFxStop();
  
  g_oledDisplay.scanBus();
  g_oledDisplay.begin();
  g_oledDisplay.setMenu(g_mainMenu);
  digitalWrite(OnLEDPin, HIGH);
  DinoGame::Init(g_oledDisplay.rawDisplay());
  DinoGame::SetCollisionCallback(DinoCollisionFlash);
  DinoGame::SetTickCallback(ServiceLedFx);
  DinoGame::SetGameOverCallback(LedFxStop);
}

void loop() {
  ServiceDisplay();
  ServiceSequencePlayback();
  HandleMenuButtons();
  ServiceLedFx();
}
