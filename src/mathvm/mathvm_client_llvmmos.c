/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mathvm/mathvm_client_llvmmos.h"

#define RIA_RW0   (*(volatile uint8_t *)0xFFE4)
#define RIA_STEP0 (*(volatile int8_t *)0xFFE5)
#define RIA_ADDR0 (*(volatile uint16_t *)0xFFE6)

/* Stores a float and its raw uint32 bit pattern in the same memory location. */
typedef union
{
    float f32;
    uint32_t u32;
} mx_llvmmos_f32_bits_t;

/* Appends one raw byte to a small local frame buffer. */
static void mx_llvmmos_append_u8(uint8_t *frame, uint16_t *len, uint8_t value)
{
    frame[(*len)++] = value;
}

/* Appends one 16-bit little-endian value to a small local frame buffer. */
static void mx_llvmmos_append_u16le(uint8_t *frame, uint16_t *len, uint16_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 8);
}

/* Appends one 32-bit little-endian value to a small local frame buffer. */
static void mx_llvmmos_append_u32le(uint8_t *frame, uint16_t *len, uint32_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 8) & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 16) & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 24);
}

/* Appends a minimal MATHVM frame header for one scalar kernel. */
static void mx_llvmmos_append_header(uint8_t *frame,
                                     uint16_t *len,
                                     uint8_t prog_len,
                                     uint8_t out_words,
                                     uint8_t stack_words)
{
    mx_llvmmos_append_u8(frame, len, 0x4D);
    mx_llvmmos_append_u8(frame, len, 0x01);
    mx_llvmmos_append_u8(frame, len, 0x00);
    mx_llvmmos_append_u8(frame, len, MX_HEADER_BYTES);
    mx_llvmmos_append_u8(frame, len, prog_len);
    mx_llvmmos_append_u8(frame, len, 0x00);
    mx_llvmmos_append_u8(frame, len, out_words);
    mx_llvmmos_append_u8(frame, len, stack_words);
    mx_llvmmos_append_u16le(frame, len, 0xFFFFu);
    mx_llvmmos_append_u16le(frame, len, 0xFFFFu);
    mx_llvmmos_append_u16le(frame, len, 0x0001u);
    mx_llvmmos_append_u16le(frame, len, 0x0000u);
}

/* Converts one float array to an array of raw MATHVM words. */
static void mx_llvmmos_copy_f32_words(mx_word_t *dst, const float *src, uint8_t count)
{
    uint8_t i;

    for (i = 0; i < count; ++i)
        dst[i] = mx_client_llvmmos_word_from_f32(src[i]);
}

/* Writes one raw 32-bit little-endian value to XRAM through the sequential
 * MMIO data port. */
static void mx_llvmmos_xram_write_u32_seq(uint16_t addr, uint32_t value)
{
    RIA_ADDR0 = addr;
    RIA_STEP0 = 1;
    RIA_RW0 = (uint8_t)(value & 0xFFu);
    RIA_RW0 = (uint8_t)((value >> 8) & 0xFFu);
    RIA_RW0 = (uint8_t)((value >> 16) & 0xFFu);
    RIA_RW0 = (uint8_t)(value >> 24);
}

/* Packs one native float value into a raw MATHVM word. */
mx_word_t mx_client_llvmmos_word_from_f32(float value)
{
    mx_llvmmos_f32_bits_t bits;
    mx_word_t word;

    bits.f32 = value;
    word.u32 = bits.u32;
    return word;
}

/* Unpacks one raw MATHVM word into a native float value. */
float mx_client_llvmmos_f32_from_word(mx_word_t value)
{
    mx_llvmmos_f32_bits_t bits;

    bits.u32 = value.u32;
    return bits.f32;
}

/* Executes a small scalar MATHVM program that computes sqrt(a*a + b*b). */
mx_client_result_t mx_client_pitagoras_f32(float a, float b, float *out)
{
    uint8_t frame[48];
    uint16_t len = 0u;
    mx_client_result_t result;
    mx_word_t raw_out[1];

    mx_llvmmos_append_header(frame, &len, 26u, 1u, 4u);

    mx_llvmmos_append_u8(frame, &len, MX_PUSHF);
    mx_llvmmos_append_u32le(frame, &len, mx_client_llvmmos_word_from_f32(a).u32);
    mx_llvmmos_append_u8(frame, &len, MX_PUSHF);
    mx_llvmmos_append_u32le(frame, &len, mx_client_llvmmos_word_from_f32(a).u32);
    mx_llvmmos_append_u8(frame, &len, MX_FMUL);

    mx_llvmmos_append_u8(frame, &len, MX_PUSHF);
    mx_llvmmos_append_u32le(frame, &len, mx_client_llvmmos_word_from_f32(b).u32);
    mx_llvmmos_append_u8(frame, &len, MX_PUSHF);
    mx_llvmmos_append_u32le(frame, &len, mx_client_llvmmos_word_from_f32(b).u32);
    mx_llvmmos_append_u8(frame, &len, MX_FMUL);

    mx_llvmmos_append_u8(frame, &len, MX_FADD);
    mx_llvmmos_append_u8(frame, &len, MX_FSQRT);
    mx_llvmmos_append_u8(frame, &len, MX_RET);
    mx_llvmmos_append_u8(frame, &len, 0x01);

    result = mx_client_call_frame(frame, len, raw_out, 1u);
    if (result.status == MX_OK && result.out_words == 1u && out != NULL)
        *out = mx_client_llvmmos_f32_from_word(raw_out[0]);
    return result;
}

/* Executes the fixed M3V3L kernel using native float input and output arrays. */
mx_client_result_t mx_client_m3v3l_f32(const float mat3[9],
                                       const float vec3[3],
                                       float out[3])
{
    mx_word_t mat3_words[9];
    mx_word_t vec3_words[3];
    mx_word_t raw_out[3];
    mx_client_result_t result;
    uint8_t i;

    if (mat3 == NULL || vec3 == NULL)
        return (mx_client_result_t){ MX_ERR_HEADER, 0u };

    mx_llvmmos_copy_f32_words(mat3_words, mat3, 9u);
    mx_llvmmos_copy_f32_words(vec3_words, vec3, 3u);
    result = mx_client_m3v3l(mat3_words, vec3_words, raw_out);

    if (result.status == MX_OK && result.out_words == 3u && out != NULL)
        for (i = 0; i < 3u; ++i)
            out[i] = mx_client_llvmmos_f32_from_word(raw_out[i]);
    return result;
}

/* Executes the SPR2L kernel in bounding-box mode using native float arrays. */
mx_client_result_t mx_client_spr2l_bbox_f32(const float affine2x3[6],
                                            const float sprite[4],
                                            float out[4])
{
    mx_word_t affine_words[6];
    mx_word_t sprite_words[4];
    mx_word_t raw_out[4];
    mx_client_result_t result;
    uint8_t i;

    if (affine2x3 == NULL || sprite == NULL)
        return (mx_client_result_t){ MX_ERR_HEADER, 0u };

    mx_llvmmos_copy_f32_words(affine_words, affine2x3, 6u);
    mx_llvmmos_copy_f32_words(sprite_words, sprite, 4u);
    result = mx_client_spr2l_bbox(affine_words, sprite_words, raw_out);

    if (result.status == MX_OK && result.out_words == 4u && out != NULL)
        for (i = 0; i < 4u; ++i)
            out[i] = mx_client_llvmmos_f32_from_word(raw_out[i]);
    return result;
}

/* Executes the SPR2L kernel in corner-output mode using native float arrays. */
mx_client_result_t mx_client_spr2l_corners_f32(const float affine2x3[6],
                                               const float sprite[4],
                                               float out[8])
{
    mx_word_t affine_words[6];
    mx_word_t sprite_words[4];
    mx_word_t raw_out[8];
    mx_client_result_t result;
    uint8_t i;

    if (affine2x3 == NULL || sprite == NULL)
        return (mx_client_result_t){ MX_ERR_HEADER, 0u };

    mx_llvmmos_copy_f32_words(affine_words, affine2x3, 6u);
    mx_llvmmos_copy_f32_words(sprite_words, sprite, 4u);
    result = mx_client_spr2l_corners(affine_words, sprite_words, raw_out);

    if (result.status == MX_OK && result.out_words == 8u && out != NULL)
        for (i = 0; i < 8u; ++i)
            out[i] = mx_client_llvmmos_f32_from_word(raw_out[i]);
    return result;
}

/* Writes an array of float vec3 values to XRAM as three packed float32 words
 * per record in the layout expected by MX_M3V3P2X. */
void mx_client_xram_write_vec3f_array(uint16_t xram_addr,
                                      const mx_vec3f_t *vecs,
                                      uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; ++i)
    {
        mx_llvmmos_xram_write_u32_seq(xram_addr, mx_client_llvmmos_word_from_f32(vecs[i].x).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
        mx_llvmmos_xram_write_u32_seq(xram_addr, mx_client_llvmmos_word_from_f32(vecs[i].y).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
        mx_llvmmos_xram_write_u32_seq(xram_addr, mx_client_llvmmos_word_from_f32(vecs[i].z).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
    }
}

/* Executes MX_M3V3P2X using a native float matrix and camera parameters. */
mx_client_result_t mx_client_m3v3p2x_f32(const float mat3[9],
                                         float persp_d,
                                         float screen_cx,
                                         float screen_cy,
                                         uint16_t xram_in,
                                         uint16_t xram_out,
                                         uint16_t count)
{
    mx_word_t mat3_words[9];
    mx_word_t camera_words[3];

    if (mat3 == NULL)
        return (mx_client_result_t){ MX_ERR_HEADER, 0u };

    mx_llvmmos_copy_f32_words(mat3_words, mat3, 9u);
    camera_words[0] = mx_client_llvmmos_word_from_f32(persp_d);
    camera_words[1] = mx_client_llvmmos_word_from_f32(screen_cx);
    camera_words[2] = mx_client_llvmmos_word_from_f32(screen_cy);
    return mx_client_m3v3p2x(mat3_words, camera_words, xram_in, xram_out, count);
}

/* Complete high-level float-friendly batch helper. */
mx_client_result_t mx_client_project_vec3f_batch_f32(const float mat3[9],
                                                     float persp_d,
                                                     float screen_cx,
                                                     float screen_cy,
                                                     uint16_t xram_base,
                                                     const mx_vec3f_t *vecs,
                                                     mx_point2i_t *points,
                                                     uint16_t count)
{
    mx_client_result_t result;
    uint32_t total_bytes;
    uint16_t xram_in;
    uint16_t xram_out;

    if ((count != 0u && mat3 == NULL) ||
        (count != 0u && vecs == NULL) ||
        (count != 0u && points == NULL))
        return (mx_client_result_t){ MX_ERR_HEADER, 0u };

    total_bytes = (uint32_t)count * 16u;
    if ((uint32_t)xram_base + total_bytes > 0x10000u)
        return (mx_client_result_t){ MX_ERR_BAD_XRAM, 0u };

    xram_in = xram_base;
    xram_out = (uint16_t)(xram_base + (uint16_t)count * 12u);

    mx_client_xram_write_vec3f_array(xram_in, vecs, count);
    result = mx_client_m3v3p2x_f32(mat3,
                                   persp_d,
                                   screen_cx,
                                   screen_cy,
                                   xram_in,
                                   xram_out,
                                   count);
    if (result.status != MX_OK)
        return result;

    mx_client_xram_read_point2i_array(xram_out, points, count);
    return result;
}
