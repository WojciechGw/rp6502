/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mathvm/mathvm_client.h"
#include <string.h>

#define RIA_XSTACK (*(volatile uint8_t *)0xFFEC)
#define RIA_OP     (*(volatile uint8_t *)0xFFEF)

typedef unsigned int (*ria_wait_fn_t)(void);
static const ria_wait_fn_t ria_wait = (ria_wait_fn_t)0xFFF1;

static void mx_append_u8(uint8_t *frame, uint8_t *len, uint8_t value)
{
    frame[(*len)++] = value;
}

static void mx_append_u16le(uint8_t *frame, uint8_t *len, uint16_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 8);
}

static void mx_append_u32le(uint8_t *frame, uint8_t *len, uint32_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 8) & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 16) & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 24);
}

static void mx_append_header(uint8_t *frame,
                             uint8_t *len,
                             uint8_t prog_len,
                             uint8_t local_words,
                             uint8_t out_words,
                             uint8_t stack_words)
{
    mx_append_u8(frame, len, 0x4D);
    mx_append_u8(frame, len, 0x01);
    mx_append_u8(frame, len, 0x00);
    mx_append_u8(frame, len, MX_HEADER_BYTES);
    mx_append_u8(frame, len, prog_len);
    mx_append_u8(frame, len, local_words);
    mx_append_u8(frame, len, out_words);
    mx_append_u8(frame, len, stack_words);
    mx_append_u16le(frame, len, 0xFFFFu);
    mx_append_u16le(frame, len, 0xFFFFu);
    mx_append_u16le(frame, len, 0x0001u);
    mx_append_u16le(frame, len, 0x0000u);
}

static void mx_append_words(uint8_t *frame,
                            uint8_t *len,
                            const mx_word_t *words,
                            uint8_t word_count)
{
    uint8_t i;

    for (i = 0; i < word_count; ++i)
        mx_append_u32le(frame, len, words[i].u32);
}

static void mx_push_frame_reverse(const uint8_t *frame, uint8_t frame_len)
{
    while (frame_len > 0)
        RIA_XSTACK = frame[--frame_len];
}

static void mx_drain_words(uint8_t word_count)
{
    while (word_count-- > 0)
    {
        (void)RIA_XSTACK;
        (void)RIA_XSTACK;
        (void)RIA_XSTACK;
        (void)RIA_XSTACK;
    }
}

static void mx_read_words(mx_word_t *out, uint8_t word_count)
{
    uint8_t i;
    uint8_t j;

    for (i = 0; i < word_count; ++i)
    {
        uint32_t value = 0;

        for (j = 0; j < 4; ++j)
            value |= ((uint32_t)RIA_XSTACK) << (j * 8);
        out[i].u32 = value;
    }
}

mx_client_result_t mx_client_call_frame(const uint8_t *frame,
                                        uint8_t frame_len,
                                        mx_word_t *out,
                                        uint8_t out_cap_words)
{
    mx_client_result_t result;
    unsigned int ax;

    mx_push_frame_reverse(frame, frame_len);

    RIA_OP = RIA_OP_MATHVM;
    ax = ria_wait();

    result.status = (uint8_t)(ax & 0x00FFu);
    result.out_words = (uint8_t)(ax >> 8);

    if (out == NULL || out_cap_words < result.out_words)
    {
        mx_drain_words(result.out_words);
        if (result.status == MX_OK && out_cap_words < result.out_words)
            result.status = MX_ERR_PROGRAM;
        return result;
    }

    mx_read_words(out, result.out_words);
    return result;
}

mx_client_result_t mx_client_m3v3l(const mx_word_t mat3[9],
                                   const mx_word_t vec3[3],
                                   mx_word_t out[3])
{
    uint8_t frame[16u + (12u * 4u) + 5u];
    uint8_t len = 0;

    mx_append_header(frame, &len, 5u, 12u, 3u, 8u);
    mx_append_words(frame, &len, mat3, 9u);
    mx_append_words(frame, &len, vec3, 3u);
    mx_append_u8(frame, &len, MX_M3V3L);
    mx_append_u8(frame, &len, 0x00);
    mx_append_u8(frame, &len, 0x09);
    mx_append_u8(frame, &len, MX_RET);
    mx_append_u8(frame, &len, 0x03);

    return mx_client_call_frame(frame, len, out, 3u);
}

mx_client_result_t mx_client_spr2l_bbox(const mx_word_t affine2x3[6],
                                        const mx_word_t sprite[4],
                                        mx_word_t out[4])
{
    uint8_t frame[16u + (10u * 4u) + 6u];
    uint8_t len = 0;

    mx_append_header(frame, &len, 6u, 10u, 4u, 8u);
    mx_append_words(frame, &len, affine2x3, 6u);
    mx_append_words(frame, &len, sprite, 4u);
    mx_append_u8(frame, &len, MX_SPR2L);
    mx_append_u8(frame, &len, 0x00);
    mx_append_u8(frame, &len, 0x06);
    mx_append_u8(frame, &len, 0x02);
    mx_append_u8(frame, &len, MX_RET);
    mx_append_u8(frame, &len, 0x04);

    return mx_client_call_frame(frame, len, out, 4u);
}

mx_client_result_t mx_client_spr2l_corners(const mx_word_t affine2x3[6],
                                           const mx_word_t sprite[4],
                                           mx_word_t out[8])
{
    uint8_t frame[16u + (10u * 4u) + 6u];
    uint8_t len = 0;

    mx_append_header(frame, &len, 6u, 10u, 8u, 8u);
    mx_append_words(frame, &len, affine2x3, 6u);
    mx_append_words(frame, &len, sprite, 4u);
    mx_append_u8(frame, &len, MX_SPR2L);
    mx_append_u8(frame, &len, 0x00);
    mx_append_u8(frame, &len, 0x06);
    mx_append_u8(frame, &len, 0x01);
    mx_append_u8(frame, &len, MX_RET);
    mx_append_u8(frame, &len, 0x08);

    return mx_client_call_frame(frame, len, out, 8u);
}
