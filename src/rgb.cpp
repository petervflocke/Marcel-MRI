#include <Arduino.h>

/**
 * =====================================================================
 * sets the WS2812 LED on a RP2040-Zero board using PWM and DMA
 * public domain code written by katak255 2025-10-30
 * =====================================================================
 * - Thanks to Greg Chadwick's blog post and github code on PWM and DMA.
 * =====================================================================
 */

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

/*
 * =====================================================================
 * WS2812 function
 * =====================================================================
 */

#if defined(PICO_DEFAULT_WS2812_PIN)
#define WS2812_PIN              PICO_DEFAULT_WS2812_PIN
#else
#define WS2812_PIN              16
#endif

#define WS2812_DMA_SIZE         25      // 24 bits colour + idle

// PWM timing derived from the current clk_sys.
//
static uint ws2812_pwm_clk = 0;
static uint ws2812_t1h_clk = 0;
static uint ws2812_t0h_clk = 0;

// WS2812B version V5 specifies >280us, but who cares if 50us works
//
#define WS2812_RES_US           50

void ws2812_set(uint rgb)
{
    static uint ws_data[WS2812_DMA_SIZE];

    uint grb = ((rgb >> 8) & 0x00FF00)
             | ((rgb << 8) & 0xFF0000)
             |  (rgb       & 0x0000FF);

    // convert GRB colour value into PWM channel A CC data
    // - colour bits always have non-zero CC values
    // - the last data value of 0 idles the PWM output
    //
    for (int i = 0; i < (WS2812_DMA_SIZE - 1); i++) {
        ws_data[i] = (grb & 0x800000) ? ws2812_t1h_clk : ws2812_t0h_clk;
        grb <<= 1;
    }
    ws_data[WS2812_DMA_SIZE - 1] = 0;

    // set up and start PWM
    //
    gpio_set_function(WS2812_PIN, GPIO_FUNC_PWM);
    int ws_slice = pwm_gpio_to_slice_num(WS2812_PIN);
    pwm_set_wrap(ws_slice, ws2812_pwm_clk - 1);
    pwm_set_gpio_level(WS2812_PIN, 0);          // initially idle Low
    pwm_set_enabled(ws_slice, true);            // start PWM

    // set up and start DMA
    // - 32-bit transfer, read increments, write fixed
    //
    int pdma = dma_claim_unused_channel(true);
    dma_channel_config pcfg = dma_channel_get_default_config(pdma);
    channel_config_set_transfer_data_size(&pcfg, DMA_SIZE_32);
    channel_config_set_read_increment(&pcfg, true);
    channel_config_set_write_increment(&pcfg, false);
    channel_config_set_dreq(&pcfg, DREQ_PWM_WRAP0 + ws_slice);

    dma_channel_configure(
        pdma,
        &pcfg,
        &pwm_hw->slice[ws_slice].cc,    // write addr
        ws_data,                        // read addr
        WS2812_DMA_SIZE,                // count
        true                            // start immediately
    );

    // wait for DMA to finish, then cleanup
    dma_channel_wait_for_finish_blocking(pdma);
    dma_channel_cleanup(pdma);
    dma_channel_unclaim(pdma);

    // a short pause for the WS2812 to reset
    // - no more High pulses after the last colour bit goes Low
    //
    busy_wait_us_32(WS2812_RES_US);
    pwm_set_enabled(ws_slice, false);           // stop PWM
}

uint clr_dat[] = {
    0xFFFFFF, 0x808080, 0x404040, 0x202020,
    0xFF0000, 0x800000, 0x400000, 0x200000,
    0x00FF00, 0x008000, 0x004000, 0x002000,
    0x0000FF, 0x000080, 0x000040, 0x000020,
    0x000000
};

static int pause_ms = 100;

void setup()
{
    // Derive WS2812 timings from the current system clock.
    uint32_t clk_hz = clock_get_hz(clk_sys);
    ws2812_pwm_clk = (uint)((clk_hz * 1200ull) / 1000000000ull); // 1200ns period
    ws2812_t1h_clk = (uint)((clk_hz * 800ull) / 1000000000ull);  //  800ns high
    ws2812_t0h_clk = (uint)((clk_hz * 400ull) / 1000000000ull);  //  400ns high
}

void loop()
{
    // show some colours on the WS2812 LED without blocking
    //
    static size_t idx = 0;
    static uint32_t last_ms = 0;
    size_t clr_count = sizeof(clr_dat) / sizeof(clr_dat[0]);
    uint32_t now_ms = millis();

    if ((uint32_t)(now_ms - last_ms) >= (uint32_t)pause_ms) {
        ws2812_set(clr_dat[idx]);
        idx = (idx + 1) % clr_count;
        last_ms = now_ms;
        if (idx == 0) {
            if (pause_ms < 10) pause_ms = 100; else pause_ms /= 10;
        }
    }
}
