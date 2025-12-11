// PicoRecPlayAudio - Raspberry Pi Pico Audio Record/Playbak/WAV File
// Copyright (C) 2022 Jason Birch
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <Arduino.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "WavPwmAudio.h"



static int WavPwmDmaCh = 0;
static unsigned int PwmSliceNum = 0;



void WavPwmInit(unsigned char GpioPinChannelA)
{
  /*************************/
 /* Configure PWM output. */
/*************************/
   gpio_set_function(GpioPinChannelA, GPIO_FUNC_PWM);
   
   // gpio_set_function(GpioPinChannelA + 1, GPIO_FUNC_PWM);
   
   PwmSliceNum = pwm_gpio_to_slice_num(GpioPinChannelA);
   // pwm_set_wrap(PwmSliceNum, WAV_PWM_COUNT);
   pwm_set_wrap(PwmSliceNum, WAV_PWM_COUNT);
   pwm_set_chan_level(PwmSliceNum, PWM_CHAN_A, 0);

   // pwm_set_chan_level(PwmSliceNum, PWM_CHAN_B, 0);
   
   pwm_set_enabled(PwmSliceNum, true);
}



unsigned char WavPwmIsPlaying()
{
   return dma_channel_is_busy(WavPwmDmaCh);
}

void WavPwmStopAudio()
{
   if (dma_channel_is_busy(WavPwmDmaCh)) {
      dma_channel_abort(WavPwmDmaCh);
      pwm_set_chan_level(PwmSliceNum, PWM_CHAN_A, 0);
      // pwm_set_chan_level(PwmSliceNum, PWM_CHAN_B, 0);
      dma_channel_unclaim(WavPwmDmaCh);
   }
}

unsigned char WavPwmPlayAudio(const unsigned short WavPwmData[])
{
   unsigned char Result = false;
   dma_channel_config WavPwmDmaChConfig;

   Serial.println(WavPwmDmaCh);
   WavPwmStopAudio();
   if (dma_channel_is_busy(WavPwmDmaCh)) {
      WavPwmDmaCh = dma_claim_unused_channel(true);
   }  
   Serial.println(WavPwmDmaCh);

   if (!dma_channel_is_busy(WavPwmDmaCh))
   {
      Result = true;

      WavPwmDmaChConfig = dma_channel_get_default_config(WavPwmDmaCh);
      channel_config_set_irq_quiet(&WavPwmDmaChConfig, true);
      channel_config_set_read_increment(&WavPwmDmaChConfig, true);
      channel_config_set_write_increment(&WavPwmDmaChConfig, false);
      channel_config_set_transfer_data_size(&WavPwmDmaChConfig, DMA_SIZE_32);
      channel_config_set_dreq(&WavPwmDmaChConfig, pwm_get_dreq(PwmSliceNum));
      dma_channel_configure(WavPwmDmaCh, &WavPwmDmaChConfig, (void*)(PWM_BASE + PWM_CH0_CC_OFFSET+PwmSliceNum*20), &(WavPwmData[2]), (WavPwmData[0] + (65536 * WavPwmData[1])) / 2, false);

      dma_hw->ints0 = (1 << WavPwmDmaCh);
      dma_start_channel_mask(1 << WavPwmDmaCh);
   }
   
   return Result;
}