#include <Arduino.h>

#define GPIO_AUDIO_OUT_LEFT  18
#define pinBut 14

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "WavPwmAudio.h"

#if defined(USE_WAVDATA2)
#include "WAVData2.h"
const unsigned short *wavs[] = {
  wav
}; 
#else
#include "WAVData.h"
const unsigned short *wavs[] = {
  wav2,
  wav3,
  wav4,
  wav5,
  wav6,
  wav7,
  wav8,
  wav9,
  wav10,
  wav11,
  wav12
};
#endif

const size_t WAV_COUNT = sizeof(wavs) / sizeof(wavs[0]);

void setup() {
  Serial.begin(115200);
  pinMode(pinBut, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  WavPwmInit(GPIO_AUDIO_OUT_LEFT);

}

int counter=0;
void loop() {

 delay(100);
 if (!gpio_get(pinBut)) {
  gpio_put(LED_BUILTIN, 1);
  Serial.println("OK");
  // WavPwmPlayAudio(RETRO_BIT);
  Serial.println(counter);
  WavPwmPlayAudio(wavs[counter++]);
  if (counter >= WAV_COUNT) {
    counter = 0;
  }
  // WavPwmStopAudio();
  while (WavPwmIsPlaying());
  // WavPwmPlayAudio(RETRO_BIT);
  gpio_put(LED_BUILTIN, 0);
 }

}
