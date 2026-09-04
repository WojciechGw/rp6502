/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/aud/mix.h"
#include "core/aud/pcm.h"
#include "core/ria/regs.h"
#include "core/sys/xram.h"

#if defined(DEBUG_AUD) || defined(DEBUG_AUD_PCM)
#include <stdio.h>
#define DBG(...) printf(__VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

#define PCM_BUF_LOG2_MIN 9    /* 512 B */
#define PCM_BUF_LOG2_MAX 13   /* 8192 B */
#define PCM_BUF_LOG2_DEF 10   /* 1024 B */

/*
 * XRAM layout at pcm_base:
 *   [+0..+1]  uint16_t write_ptr     - byte index into ring, maintained by 6502
 *   [+2]      uint8_t  format        - bit 0=mono, bit 1=8-bit, bit 2=unsigned
 *   [+3]      uint8_t  buf_size_log2 - 9..13; 0 or invalid → 10
 *   [+4..+5]  uint16_t sample_rate   - 8000/11025/16000/22050/32000/44100 LE; 0 → 44100
 *   [+6..+7]  uint16_t read_ptr  - pcm_read_ptr, updated as frames are consumed
 *   [+8..]    ring buffer            - (1 << buf_size_log2) bytes
 *
 * Ring buffer byte layout per frame:
 *   stereo 16-bit signed:  [L_lo, L_hi, R_lo, R_hi]  4 bytes
 *   mono   16-bit signed:  [S_lo, S_hi]               2 bytes  (L = R)
 *   stereo  8-bit:         [L, R]                     2 bytes
 *   mono    8-bit:         [S]                        1 byte   (L = R)
 *
 * write_ptr and pcm_read_ptr are byte indices masked with pcm_buf_mask.
 * Both advance in steps of pcm_frame_sz. buf_size is always a power-of-2
 * multiple of pcm_frame_sz, so pcm_read_ptr never wraps within a frame.
 *
 * Feeding guide at 44100 Hz stereo 16-bit (176 B/ms consumed):
 *   log2=10 (1 KB, ~5.8 ms)  - must feed from main loop, not only VSYNC
 *   log2=12 (4 KB, ~23 ms)   - sufficient for once-per-VSYNC at 60 Hz
 *   log2=13 (8 KB, ~46 ms)   - comfortable for once-per-VSYNC with margin
 */

static volatile uint16_t pcm_base;
static volatile uint16_t pcm_buf_mask;
static volatile uint8_t  pcm_format;
static volatile uint8_t  pcm_frame_sz;
static uint16_t pcm_read_ptr;

/* The mixer calls every device at the fixed AUD_NATIVE_RATE, but a program
 * picks PCM's own rate. pcm_phase/pcm_phase_inc is a Q16.16 accumulator that
 * walks the ring buffer at the program's rate while the mixer keeps calling
 * at its own; pcm_l0/r0 and pcm_l1/r1 are the frame either side of it, and
 * the fractional part of pcm_phase is the lerp weight between them. */
static int16_t  pcm_l0, pcm_r0;
static int16_t  pcm_l1, pcm_r1;
static uint32_t pcm_phase;
static uint32_t pcm_phase_inc;

void pcm_sample(int16_t *left, int16_t *right)
{
    pcm_phase += pcm_phase_inc;
    while (pcm_phase >= (1u << 16))
    {
        pcm_phase -= (1u << 16);
        pcm_l0 = pcm_l1;
        pcm_r0 = pcm_r1;

        uint16_t base = pcm_base;
        uint16_t mask = pcm_buf_mask;
        uint8_t  fmt  = pcm_format;
        uint8_t  fsz  = pcm_frame_sz;
        uint16_t write_ptr = (uint16_t)xram[base] | ((uint16_t)xram[base + 1] << 8);
        if (((write_ptr - pcm_read_ptr) & mask) >= fsz)
        {
            const volatile uint8_t *ring = &xram[base + 8];
            if (fmt & 2)
            {
                uint8_t raw_l = ring[pcm_read_ptr];
                uint8_t raw_r = (fmt & 1) ? raw_l : ring[pcm_read_ptr + 1];
                if (fmt & 4)
                {
                    pcm_l1 = (int16_t)((int16_t)raw_l - 128) << 8;
                    pcm_r1 = (int16_t)((int16_t)raw_r - 128) << 8;
                }
                else
                {
                    pcm_l1 = (int8_t)raw_l << 8;
                    pcm_r1 = (int8_t)raw_r << 8;
                }
            }
            else
            {
                pcm_l1 = (int16_t)((uint16_t)ring[pcm_read_ptr] |
                                   ((uint16_t)ring[pcm_read_ptr + 1] << 8));
                if (fmt & 1)
                    pcm_r1 = pcm_l1;
                else
                    pcm_r1 = (int16_t)((uint16_t)ring[pcm_read_ptr + 2] |
                                       ((uint16_t)ring[pcm_read_ptr + 3] << 8));
            }
            pcm_read_ptr = (pcm_read_ptr + fsz) & mask;
        }
        xram[base + 6] = (uint8_t)pcm_read_ptr;
        xram[base + 7] = (uint8_t)(pcm_read_ptr >> 8);

        // Drain xram_queue; write_ptr is read directly from xram, queue values unused
        uint8_t max_work = 32;
        while (max_work-- && xram_queue_tail != xram_queue_head)
            xram_queue_tail++;
    }
    int32_t t = (int32_t)pcm_phase;
    *left  = (int16_t)(pcm_l0 + (((int32_t)(pcm_l1 - pcm_l0) * t) >> 16));
    *right = (int16_t)(pcm_r0 + (((int32_t)(pcm_r1 - pcm_r0) * t) >> 16));
}

bool pcm_xreg(uint16_t word)
{
    if (word & 0x0003)
        return word == 0xFFFF;
    uint8_t log2 = xram[word + 3];
    if (log2 < PCM_BUF_LOG2_MIN || log2 > PCM_BUF_LOG2_MAX)
        log2 = PCM_BUF_LOG2_DEF;
    uint8_t  fmt  = xram[word + 2];
    uint16_t rate = (uint16_t)xram[word + 4] | ((uint16_t)xram[word + 5] << 8);
    if (rate != 8000  && rate != 11025 && rate != 16000
     && rate != 22050 && rate != 32000 && rate != 44100)
        rate = 44100;
    uint8_t fsz = (fmt & 2) ? ((fmt & 1) ? 1 : 2)
                             : ((fmt & 1) ? 2 : 4);
    if ((uint32_t)word + 8 + (1u << log2) > 65536)
        return false;
    pcm_buf_mask  = (uint16_t)((1u << log2) - 1);
    pcm_format    = fmt;
    pcm_frame_sz  = fsz;
    pcm_base      = word;
    pcm_read_ptr  = 0;
    pcm_l0 = pcm_r0 = pcm_l1 = pcm_r1 = 0;
    pcm_phase     = 0;
    pcm_phase_inc = (uint32_t)(((uint64_t)rate << 16) / AUD_NATIVE_RATE);
    xram_queue_page = word >> 8;
    xram_queue_tail = xram_queue_head;
    aud_setup(pcm_sample);
    return true;
}
