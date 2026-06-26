/*
 * Copyright (c) 2026 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "aud/aud.h"
#include "aud/bel.h"
#include "aud/pcm.h"
#include "sys/mem.h"
#include <pico/stdlib.h>
#include <hardware/pwm.h>

#if defined(DEBUG_RIA_AUD) || defined(DEBUG_RIA_AUD_PCM)
#include <stdio.h>
#define DBG(...) printf(__VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

#define PCM_SAMPLE_RATE  44100
#define PCM_BUF_LOG2_MIN 9    /* 512 B */
#define PCM_BUF_LOG2_MAX 13   /* 8192 B */
#define PCM_BUF_LOG2_DEF 10   /* 1024 B */

/*
 * XRAM layout at pcm_base:
 *   [+0..+1]  uint16_t write_ptr     - byte index into ring, maintained by 6502
 *   [+2]      uint8_t  flags         - bit 0 = play
 *   [+3]      uint8_t  buf_size_log2 - 9..13 (512..8192 B); 0 or invalid → 10
 *   [+4..]    ring buffer            - (1 << buf_size_log2) bytes
 *
 * Interleaved int16_t stereo: [L_lo, L_hi, R_lo, R_hi, ...].
 * write_ptr and pcm_read_ptr are byte indices masked with pcm_buf_mask.
 * Both advance in steps of 4 (one stereo frame). buf_size is always a
 * power-of-2 multiple of 4, so pcm_read_ptr never wraps within a frame.
 *
 * Feeding frequency guide at 44100 Hz stereo (176 B/ms consumed):
 *   log2=10 (1 KB, ~5.8 ms)  - must feed from main loop, not only VSYNC
 *   log2=12 (4 KB, ~23 ms)   - sufficient for once-per-VSYNC at 60 Hz
 *   log2=13 (8 KB, ~46 ms)   - comfortable for once-per-VSYNC with margin
 */

static volatile uint16_t pcm_base;
static volatile uint16_t pcm_buf_mask;
static uint16_t pcm_read_ptr;
static int16_t pcm_sample_l;
static int16_t pcm_sample_r;

static void
    __attribute__((optimize("O3")))
    __isr
    __time_critical_func(pcm_irq_handler)(void)
{
    pwm_clear_irq(AUD_IRQ_SLICE);

    // Output previous sample at start to minimize jitter
    pwm_set_chan_level(AUD_L_SLICE, AUD_L_CHAN, pcm_sample_l + AUD_PWM_CENTER);
    pwm_set_chan_level(AUD_R_SLICE, AUD_R_CHAN, pcm_sample_r + AUD_PWM_CENTER);

    uint16_t base = pcm_base;
    uint16_t mask = pcm_buf_mask;
    uint16_t write_ptr = (uint16_t)xram[base] | ((uint16_t)xram[base + 1] << 8);
    int16_t next_l = 0;
    int16_t next_r = 0;
    if (((write_ptr - pcm_read_ptr) & mask) >= 4)
    {
        const uint8_t *ring = &xram[base + 4];
        next_l = (int16_t)((uint16_t)ring[pcm_read_ptr] |
                           ((uint16_t)ring[pcm_read_ptr + 1] << 8));
        next_r = (int16_t)((uint16_t)ring[pcm_read_ptr + 2] |
                           ((uint16_t)ring[pcm_read_ptr + 3] << 8));
        pcm_read_ptr = (pcm_read_ptr + 4) & mask;
        next_l >>= (16 - AUD_PWM_BITS);
        next_r >>= (16 - AUD_PWM_BITS);
    }
    int16_t bel_mix = bel_sample(PCM_SAMPLE_RATE);
    next_l += bel_mix;
    next_r += bel_mix;
    int16_t max_val = (1 << (AUD_PWM_BITS - 1)) - 1;
    int16_t min_val = -(1 << (AUD_PWM_BITS - 1));
    if (next_l < min_val)
        next_l = min_val;
    if (next_l > max_val)
        next_l = max_val;
    if (next_r < min_val)
        next_r = min_val;
    if (next_r > max_val)
        next_r = max_val;
    pcm_sample_l = next_l;
    pcm_sample_r = next_r;

    // Drain xram_queue; write_ptr byte writes (offsets 0,1) are ignored
    uint8_t max_work = 32;
    while (max_work-- && xram_queue_tail != xram_queue_head)
        xram_queue_tail++;
}

bool pcm_xreg(uint16_t word)
{
    if (word & 0x0003)
        return word == 0xFFFF;
    uint8_t log2 = xram[word + 3];
    if (log2 < PCM_BUF_LOG2_MIN || log2 > PCM_BUF_LOG2_MAX)
        log2 = PCM_BUF_LOG2_DEF;
    if ((uint32_t)word + 4 + (1u << log2) > 65536)
        return false;
    pcm_buf_mask = (uint16_t)((1u << log2) - 1);
    pcm_base = word;
    pcm_read_ptr = 0;
    pcm_sample_l = 0;
    pcm_sample_r = 0;
    xram_queue_page = word >> 8;
    xram_queue_tail = xram_queue_head;
    aud_setup(pcm_irq_handler, PCM_SAMPLE_RATE);
    return true;
}
