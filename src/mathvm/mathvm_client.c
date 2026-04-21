/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mathvm/mathvm_client.h"
#include <string.h>

#define RIA_XSTACK (*(volatile uint8_t *)0xFFEC)
#define RIA_RW0    (*(volatile uint8_t *)0xFFE4)
#define RIA_STEP0  (*(volatile int8_t *)0xFFE5)
#define RIA_ADDR0  (*(volatile uint16_t *)0xFFE6)
#define RIA_OP     (*(volatile uint8_t *)0xFFEF)

typedef unsigned int (*ria_wait_fn_t)(void);
static const ria_wait_fn_t ria_wait = (ria_wait_fn_t)0xFFF1;
static uint8_t mx_batch_frame[MX_MAX_FRAME];

/* Builds a result object that carries only an error status and no payload. */
static mx_client_result_t mx_client_error_result(uint8_t status)
{
    mx_client_result_t result;

    result.status = status;
    result.out_words = 0u;
    return result;
}

/* Returns the index of the highest set bit in a non-zero unsigned value.
 * The result is used to normalize integer magnitudes into float32 format
 * without relying on a host-side floating-point implementation. */
static uint8_t mx_u32_msb_index(uint32_t value)
{
    uint8_t index = 0u;

    while ((value >>= 1) != 0u)
        ++index;
    return index;
}

/* Converts an unsigned fixed-point magnitude to raw float32 bits.
 * `frac_bits` specifies how many low bits of `magnitude` belong to the
 * fractional part, so the function can encode both integers and Q8.8 values. */
static uint32_t mx_u32_to_f32_bits_scaled(uint32_t magnitude, uint8_t frac_bits)
{
    uint8_t msb;
    int16_t exponent;
    uint32_t mantissa;

    if (magnitude == 0u)
        return 0u;

    msb = mx_u32_msb_index(magnitude);
    exponent = (int16_t)msb - (int16_t)frac_bits + 127;
    if (msb <= 23u)
        mantissa = magnitude << (23u - msb);
    else
        mantissa = magnitude >> (msb - 23u);

    return ((uint32_t)exponent << 23) | (mantissa & 0x007FFFFFu);
}

/* Converts a signed fixed-point integer to one MATHVM word containing raw
 * float32 bits. The caller controls the fixed-point scale via `frac_bits`. */
static mx_word_t mx_word_from_i32_scaled(int32_t value, uint8_t frac_bits)
{
    mx_word_t word;
    uint32_t magnitude;

    if (value < 0)
    {
        magnitude = (uint32_t)(-(value + 1)) + 1u;
        word.u32 = 0x80000000u | mx_u32_to_f32_bits_scaled(magnitude, frac_bits);
    }
    else
    {
        magnitude = (uint32_t)value;
        word.u32 = mx_u32_to_f32_bits_scaled(magnitude, frac_bits);
    }
    return word;
}

/* Writes one 32-bit little-endian value to XRAM using the sequential MMIO
 * access port exposed by RIA. */
static void mx_xram_write_u32_seq(uint16_t addr, uint32_t value)
{
    RIA_ADDR0 = addr;
    RIA_STEP0 = 1;
    RIA_RW0 = (uint8_t)(value & 0xFFu);
    RIA_RW0 = (uint8_t)((value >> 8) & 0xFFu);
    RIA_RW0 = (uint8_t)((value >> 16) & 0xFFu);
    RIA_RW0 = (uint8_t)(value >> 24);
}

/* Reads one 32-bit little-endian value from XRAM using the sequential MMIO
 * access port exposed by RIA. */
static uint32_t mx_xram_read_u32_seq(uint16_t addr)
{
    uint32_t value;

    RIA_ADDR0 = addr;
    RIA_STEP0 = 1;
    value  = (uint32_t)RIA_RW0;
    value |= (uint32_t)RIA_RW0 << 8;
    value |= (uint32_t)RIA_RW0 << 16;
    value |= (uint32_t)RIA_RW0 << 24;
    return value;
}

/* Appends one byte to a frame buffer and advances its current length. */
static void mx_append_u8(uint8_t *frame, uint16_t *len, uint8_t value)
{
    frame[(*len)++] = value;
}

/* Appends one 16-bit little-endian value to a frame buffer. */
static void mx_append_u16le(uint8_t *frame, uint16_t *len, uint16_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 8);
}

/* Appends one 32-bit little-endian value to a frame buffer. */
static void mx_append_u32le(uint8_t *frame, uint16_t *len, uint32_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 8) & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 16) & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 24);
}

/* Appends a complete MATHVM frame header using the current ABI layout.
 * The header fields are written exactly as the firmware expects them on
 * XSTACK, including the trailing reserved word. */
static void mx_append_header(uint8_t *frame,
                             uint16_t *len,
                             uint8_t flags,
                             uint8_t prog_len,
                             uint8_t local_words,
                             uint8_t out_words,
                             uint8_t stack_words,
                             uint16_t xram_in,
                             uint16_t xram_out,
                             uint16_t count)
{
    mx_append_u8(frame, len, 0x4D);
    mx_append_u8(frame, len, 0x01);
    mx_append_u8(frame, len, flags);
    mx_append_u8(frame, len, MX_HEADER_BYTES);
    mx_append_u8(frame, len, prog_len);
    mx_append_u8(frame, len, local_words);
    mx_append_u8(frame, len, out_words);
    mx_append_u8(frame, len, stack_words);
    mx_append_u16le(frame, len, xram_in);
    mx_append_u16le(frame, len, xram_out);
    mx_append_u16le(frame, len, count);
    mx_append_u16le(frame, len, 0x0000u);
}

/* Appends a contiguous array of MATHVM words to a frame buffer. */
static void mx_append_words(uint8_t *frame,
                            uint16_t *len,
                            const mx_word_t *words,
                            uint8_t word_count)
{
    uint8_t i;

    for (i = 0; i < word_count; ++i)
        mx_append_u32le(frame, len, words[i].u32);
}

/* Pushes a finished frame onto XSTACK in reverse byte order.
 * RIA consumes frames from the top of the stack, so the last byte must be
 * written first and the first byte last. */
static void mx_push_frame_reverse(const uint8_t *frame, uint16_t frame_len)
{
    while (frame_len > 0)
        RIA_XSTACK = frame[--frame_len];
}

/* Discards a number of 32-bit words from XSTACK without decoding them.
 * This is used when the caller did not provide an output buffer or when the
 * buffer is too small for the produced payload. */
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

/* Reads a number of 32-bit little-endian result words back from XSTACK and
 * stores them into the caller's output buffer. */
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

/* Parses one signed decimal string into Q16.16 fixed-point form.
 * The parser accepts an optional sign and up to four fractional digits, which
 * keeps the conversion precise enough for human-entered demo values while
 * staying inside plain 32-bit integer arithmetic. */
static int mx_parse_decimal_q16_16(const char *text, int32_t *out_value)
{
    uint8_t negative = 0u;
    uint8_t got_digit = 0u;
    uint8_t frac_digits = 0u;
    uint32_t whole = 0u;
    uint32_t frac = 0u;
    uint32_t scale = 1u;
    uint32_t frac_scaled;
    uint32_t combined;

    if (text == 0 || out_value == 0)
        return 0;

    if (*text == '-')
    {
        negative = 1u;
        ++text;
    }
    else if (*text == '+')
    {
        ++text;
    }

    while (*text >= '0' && *text <= '9')
    {
        got_digit = 1u;
        if (whole > 32767u)
            return 0;
        whole = whole * 10u + (uint32_t)(*text - '0');
        if (whole > 32767u)
            return 0;
        ++text;
    }

    if (*text == '.')
    {
        ++text;
        while (*text >= '0' && *text <= '9')
        {
            got_digit = 1u;
            if (frac_digits < 4u)
            {
                frac = frac * 10u + (uint32_t)(*text - '0');
                scale *= 10u;
                ++frac_digits;
            }
            ++text;
        }
    }

    if (!got_digit || *text != '\0')
        return 0;

    frac_scaled = (uint32_t)((frac * 65536u + (scale / 2u)) / scale);
    combined = (whole << 16) + frac_scaled;

    if (!negative)
    {
        if (combined > 0x7FFFFFFFu)
            return 0;
        *out_value = (int32_t)combined;
    }
    else
    {
        if (combined > 0x80000000u)
            return 0;
        if (combined == 0x80000000u)
            *out_value = (int32_t)0x80000000u;
        else
            *out_value = -(int32_t)combined;
    }

    return 1;
}

/* Appends one character to a bounded text buffer and keeps room for a final
 * terminating NUL byte. */
static void mx_buffer_putc(char **cursor, char *end, char value)
{
    if (*cursor < end)
        *(*cursor)++ = value;
}

/* Appends one unsigned decimal integer to a bounded text buffer. */
static void mx_buffer_put_u32(char **cursor, char *end, uint32_t value)
{
    char digits[10];
    uint8_t count = 0u;

    do
    {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (count > 0u)
        mx_buffer_putc(cursor, end, digits[--count]);
}

/* Executes one raw MATHVM frame through OS $80.
 * The function handles XSTACK push/pop details, waits for completion through
 * the RIA entry point, and copies any direct return words into `out`. */
mx_client_result_t mx_client_call_frame(const uint8_t *frame,
                                        uint16_t frame_len,
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

/* Builds and executes a tiny scalar program that computes
 * sqrt(a*a + b*b) from two human-entered decimal strings. */
mx_client_result_t mx_client_pitagoras_i16(const char *a_text,
                                           const char *b_text,
                                           mx_word_t out[1])
{
    int32_t a_q16_16;
    int32_t b_q16_16;
    uint8_t frame[48];
    uint16_t len = 0u;

    if (out == 0)
        return mx_client_error_result(MX_ERR_HEADER);
    if (!mx_parse_decimal_q16_16(a_text, &a_q16_16))
        return mx_client_error_result(MX_ERR_HEADER);
    if (!mx_parse_decimal_q16_16(b_text, &b_q16_16))
        return mx_client_error_result(MX_ERR_HEADER);

    mx_append_header(frame, &len, 0x00u, 26u, 0u, 1u, 4u, 0xFFFFu, 0xFFFFu, 0x0001u);

    mx_append_u8(frame, &len, MX_PUSHF);
    mx_append_u32le(frame, &len, mx_word_from_i32_scaled(a_q16_16, 16u).u32);
    mx_append_u8(frame, &len, MX_PUSHF);
    mx_append_u32le(frame, &len, mx_word_from_i32_scaled(a_q16_16, 16u).u32);
    mx_append_u8(frame, &len, MX_FMUL);

    mx_append_u8(frame, &len, MX_PUSHF);
    mx_append_u32le(frame, &len, mx_word_from_i32_scaled(b_q16_16, 16u).u32);
    mx_append_u8(frame, &len, MX_PUSHF);
    mx_append_u32le(frame, &len, mx_word_from_i32_scaled(b_q16_16, 16u).u32);
    mx_append_u8(frame, &len, MX_FMUL);

    mx_append_u8(frame, &len, MX_FADD);
    mx_append_u8(frame, &len, MX_FSQRT);
    mx_append_u8(frame, &len, MX_RET);
    mx_append_u8(frame, &len, 0x01);

    return mx_client_call_frame(frame, len, out, 1u);
}

/* Converts raw float32 bits to a signed Q16.16 fixed-point approximation. */
int32_t mx_client_f32_to_q16_16(uint32_t bits)
{
    uint32_t sign = bits >> 31;
    uint32_t exp_raw = (bits >> 23) & 0xFFu;
    uint32_t frac = bits & 0x007FFFFFu;
    uint32_t sig;
    int16_t shift;
    uint32_t magnitude;

    if (exp_raw == 0u)
    {
        if (frac == 0u)
            return 0;
        sig = frac;
        shift = -133;
    }
    else
    {
        sig = 0x00800000u | frac;
        shift = (int16_t)exp_raw - 134;
    }

    if (shift >= 0)
    {
        if (shift > 7)
            magnitude = 0x7FFFFFFFu;
        else
            magnitude = sig << shift;
    }
    else
    {
        uint8_t rshift = (uint8_t)(-shift);

        if (rshift >= 32u)
            magnitude = 0u;
        else
            magnitude = (sig + (1u << (rshift - 1u))) >> rshift;
    }

    if (sign == 0u)
        return (int32_t)magnitude;
    if (magnitude == 0x80000000u)
        return (int32_t)0x80000000u;
    return -(int32_t)magnitude;
}

/* Formats one signed Q16.16 value as a decimal string with four fractional
 * digits. */
void mx_client_format_q16_16(char *buffer, uint8_t buffer_size, int32_t value)
{
    char *cursor;
    char *end;
    uint32_t magnitude;
    uint32_t whole;
    uint32_t frac;

    if (buffer == 0 || buffer_size == 0u)
        return;

    cursor = buffer;
    end = buffer + buffer_size - 1u;

    if (value < 0)
    {
        mx_buffer_putc(&cursor, end, '-');
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    whole = magnitude >> 16;
    frac = (((magnitude & 0xFFFFu) * 10000u) + 32768u) >> 16;
    if (frac >= 10000u)
    {
        ++whole;
        frac -= 10000u;
    }

    mx_buffer_put_u32(&cursor, end, whole);
    mx_buffer_putc(&cursor, end, '.');
    mx_buffer_putc(&cursor, end, (char)('0' + ((frac / 1000u) % 10u)));
    mx_buffer_putc(&cursor, end, (char)('0' + ((frac / 100u) % 10u)));
    mx_buffer_putc(&cursor, end, (char)('0' + ((frac / 10u) % 10u)));
    mx_buffer_putc(&cursor, end, (char)('0' + (frac % 10u)));
    *cursor = '\0';
}

/* Formats one raw float32 result word as a decimal string with four
 * fractional digits. */
void mx_client_format_f32(char *buffer, uint8_t buffer_size, mx_word_t value)
{
    mx_client_format_q16_16(buffer, buffer_size, mx_client_f32_to_q16_16(value.u32));
}

/* Uploads an array of signed integer vec3 values to XRAM in the exact record
 * layout expected by the batch projection kernel: three float32 words per
 * element stored as x, y, z. */
void mx_client_xram_write_vec3i_array(uint16_t xram_addr,
                                      const mx_vec3i_t *vecs,
                                      uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; ++i)
    {
        mx_xram_write_u32_seq(xram_addr, mx_word_from_i32_scaled(vecs[i].x, 0u).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
        mx_xram_write_u32_seq(xram_addr, mx_word_from_i32_scaled(vecs[i].y, 0u).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
        mx_xram_write_u32_seq(xram_addr, mx_word_from_i32_scaled(vecs[i].z, 0u).u32);
        xram_addr = (uint16_t)(xram_addr + 4u);
    }
}

/* Reads packed int16 screen-space points from XRAM into a friendly array of
 * `mx_point2i_t` records. */
void mx_client_xram_read_point2i_array(uint16_t xram_addr,
                                       mx_point2i_t *points,
                                       uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; ++i)
    {
        uint32_t raw = mx_xram_read_u32_seq(xram_addr);

        points[i].x = (int16_t)(raw & 0xFFFFu);
        points[i].y = (int16_t)(raw >> 16);
        xram_addr = (uint16_t)(xram_addr + 4u);
    }
}

/* Converts a batch descriptor into a concrete binary frame and executes it.
 * This is the generic client-side batching layer used by higher-level
 * helpers that want XRAM-backed processing without manually assembling bytes. */
mx_client_result_t mx_client_call_batch(const mx_client_batch_desc_t *desc,
                                        mx_word_t *out,
                                        uint8_t out_cap_words)
{
    uint16_t frame_len;

    if (desc == NULL)
        return mx_client_error_result(MX_ERR_HEADER);
    if (desc->program == NULL)
        return mx_client_error_result(MX_ERR_HEADER);
    if (desc->local_words != 0u && desc->locals == NULL)
        return mx_client_error_result(MX_ERR_HEADER);

    if (desc->local_words > MX_MAX_LOCALS)
        return mx_client_error_result(MX_ERR_HEADER);
    if (desc->prog_len > MX_MAX_PROG)
        return mx_client_error_result(MX_ERR_HEADER);
    if (desc->out_words > MX_MAX_OUT)
        return mx_client_error_result(MX_ERR_HEADER);
    if (desc->stack_words > MX_MAX_STACK)
        return mx_client_error_result(MX_ERR_HEADER);

    frame_len = MX_HEADER_BYTES;
    frame_len = (uint16_t)(frame_len + (uint16_t)desc->local_words * MX_WORD_BYTES);
    frame_len = (uint16_t)(frame_len + desc->prog_len);
    if (frame_len > MX_MAX_FRAME)
        return mx_client_error_result(MX_ERR_HEADER);

    frame_len = 0u;
    mx_append_header(mx_batch_frame, &frame_len,
                     desc->flags,
                     desc->prog_len,
                     desc->local_words,
                     desc->out_words,
                     desc->stack_words,
                     desc->xram_in,
                     desc->xram_out,
                     desc->count);
    if (desc->local_words != 0u)
        mx_append_words(mx_batch_frame, &frame_len, desc->locals, desc->local_words);
    memcpy(&mx_batch_frame[frame_len], desc->program, desc->prog_len);
    frame_len = (uint16_t)(frame_len + desc->prog_len);

    return mx_client_call_frame(mx_batch_frame, frame_len, out, out_cap_words);
}

/* Executes the fixed M3V3L kernel by building the complete frame locally.
 * The matrix and vector are copied into the locals area and the bytecode
 * returns the three transformed float32 components directly through XSTACK. */
mx_client_result_t mx_client_m3v3l(const mx_word_t mat3[9],
                                   const mx_word_t vec3[3],
                                   mx_word_t out[3])
{
    uint8_t frame[16u + (12u * 4u) + 5u];
    uint16_t len = 0;

    mx_append_header(frame, &len, 0x00u, 5u, 12u, 3u, 8u, 0xFFFFu, 0xFFFFu, 0x0001u);
    mx_append_words(frame, &len, mat3, 9u);
    mx_append_words(frame, &len, vec3, 3u);
    mx_append_u8(frame, &len, MX_M3V3L);
    mx_append_u8(frame, &len, 0x00);
    mx_append_u8(frame, &len, 0x09);
    mx_append_u8(frame, &len, MX_RET);
    mx_append_u8(frame, &len, 0x03);

    return mx_client_call_frame(frame, len, out, 3u);
}

/* Executes the sprite transform kernel in bounding-box mode.
 * The helper packs the affine transform and sprite descriptor into locals and
 * returns four float32 values describing the resulting screen-space box. */
mx_client_result_t mx_client_spr2l_bbox(const mx_word_t affine2x3[6],
                                        const mx_word_t sprite[4],
                                        mx_word_t out[4])
{
    uint8_t frame[16u + (10u * 4u) + 6u];
    uint16_t len = 0;

    mx_append_header(frame, &len, 0x00u, 6u, 10u, 4u, 8u, 0xFFFFu, 0xFFFFu, 0x0001u);
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

/* Executes the sprite transform kernel in corner-output mode.
 * The helper returns eight float32 values representing four transformed
 * sprite corners. */
mx_client_result_t mx_client_spr2l_corners(const mx_word_t affine2x3[6],
                                           const mx_word_t sprite[4],
                                           mx_word_t out[8])
{
    uint8_t frame[16u + (10u * 4u) + 6u];
    uint16_t len = 0;

    mx_append_header(frame, &len, 0x00u, 6u, 10u, 8u, 8u, 0xFFFFu, 0xFFFFu, 0x0001u);
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

/* Executes the MX_M3V3P2X batch kernel from already prepared float32 locals.
 * This is the lowest-level projection helper that still hides frame assembly
 * while leaving matrix and camera preparation to the caller. */
mx_client_result_t mx_client_m3v3p2x(const mx_word_t mat3[9],
                                     const mx_word_t camera[3],
                                     uint16_t xram_in,
                                     uint16_t xram_out,
                                     uint16_t count)
{
    static const uint8_t program[] = {
        MX_M3V3P2X, 0x00, 0x09, MX_HALT
    };
    mx_word_t locals[12];
    mx_client_batch_desc_t desc;

    memcpy(&locals[0], mat3, 9u * sizeof(mx_word_t));
    memcpy(&locals[9], camera, 3u * sizeof(mx_word_t));

    desc.flags = MX_FLAG_USE_XRAM_IN | MX_FLAG_USE_XRAM_OUT;
    desc.xram_in = xram_in;
    desc.xram_out = xram_out;
    desc.count = count;
    desc.locals = locals;
    desc.local_words = 12u;
    desc.program = program;
    desc.prog_len = sizeof(program);
    desc.out_words = 0u;
    desc.stack_words = 12u;

    return mx_client_call_batch(&desc, NULL, 0u);
}

/* Executes the MX_M3V3P2X batch kernel from Q8.8 matrix coefficients and
 * integer camera parameters. The helper performs the required fixed-point to
 * float32 conversion on the caller's behalf. */
mx_client_result_t mx_client_m3v3p2x_q8_8(const int16_t mat3_q8_8[9],
                                          int16_t persp_d,
                                          int16_t screen_cx,
                                          int16_t screen_cy,
                                          uint16_t xram_in,
                                          uint16_t xram_out,
                                          uint16_t count)
{
    mx_word_t mat3_words[9];
    mx_word_t camera_words[3];
    uint8_t i;

    for (i = 0; i < 9u; ++i)
        mat3_words[i] = mx_word_from_i32_scaled(mat3_q8_8[i], 8u);
    camera_words[0] = mx_word_from_i32_scaled(persp_d, 0u);
    camera_words[1] = mx_word_from_i32_scaled(screen_cx, 0u);
    camera_words[2] = mx_word_from_i32_scaled(screen_cy, 0u);

    return mx_client_m3v3p2x(mat3_words, camera_words, xram_in, xram_out, count);
}

/* Multiplies two Q8.8 fixed-point values and rounds the result back to Q8.8. */
static int16_t mx_mul_q8_8(int16_t a, int16_t b)
{
    int32_t product = (int32_t)a * (int32_t)b;

    if (product >= 0)
        return (int16_t)((product + 128) >> 8);

    product = -product;
    return (int16_t)(-((product + 128) >> 8));
}

/* Returns an approximate sine in Q8.8 form for an integer angle in degrees.
 * The function uses a 0..90 degree lookup table and symmetry rules for the
 * remaining quadrants to avoid floating-point math on cc65. */
static int16_t mx_sin_deg_q8_8(int angle_deg)
{
    static const uint16_t sin_tab[91] = {
          0,   4,   9,  13,  18,  22,  27,  31,  36,  40,
         44,  49,  53,  58,  62,  66,  71,  75,  79,  83,
         88,  92,  96, 100, 104, 108, 112, 116, 120, 124,
        128, 132, 136, 139, 143, 147, 150, 154, 158, 161,
        165, 168, 171, 175, 178, 181, 184, 187, 190, 193,
        196, 199, 202, 204, 207, 210, 212, 215, 217, 219,
        222, 224, 226, 228, 230, 232, 234, 236, 237, 239,
        241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
        252, 253, 254, 254, 255, 255, 255, 256, 256, 256,
        256
    };

    while (angle_deg < 0)
        angle_deg += 360;
    while (angle_deg >= 360)
        angle_deg -= 360;

    if (angle_deg <= 90)
        return (int16_t)sin_tab[angle_deg];
    if (angle_deg <= 180)
        return (int16_t)sin_tab[180 - angle_deg];
    if (angle_deg <= 270)
        return (int16_t)(-(int16_t)sin_tab[angle_deg - 180]);
    return (int16_t)(-(int16_t)sin_tab[360 - angle_deg]);
}

/* Returns an approximate cosine in Q8.8 form for an integer angle in degrees. */
static int16_t mx_cos_deg_q8_8(int angle_deg)
{
    return mx_sin_deg_q8_8(angle_deg + 90);
}

/* Builds the standard Y-axis rotation plus 30-degree tilt matrix and executes
 * the MX_M3V3P2X batch kernel with that transform. This gives cc65 callers a
 * compact, integer-only way to project rotating wireframe geometry. */
mx_client_result_t mx_client_m3v3p2x_yrot30(int angle_deg,
                                            int16_t persp_d,
                                            int16_t screen_cx,
                                            int16_t screen_cy,
                                            uint16_t xram_in,
                                            uint16_t xram_out,
                                            uint16_t count)
{
    int16_t mat3_q8_8[9];
    int16_t sin_a = mx_sin_deg_q8_8(angle_deg);
    int16_t cos_a = mx_cos_deg_q8_8(angle_deg);
    const int16_t sin30 = 128;
    const int16_t cos30 = 222;

    mat3_q8_8[0] = cos_a;
    mat3_q8_8[1] = 0;
    mat3_q8_8[2] = sin_a;
    mat3_q8_8[3] = mx_mul_q8_8(sin_a, sin30);
    mat3_q8_8[4] = cos30;
    mat3_q8_8[5] = (int16_t)-mx_mul_q8_8(cos_a, sin30);
    mat3_q8_8[6] = (int16_t)-mx_mul_q8_8(sin_a, cos30);
    mat3_q8_8[7] = sin30;
    mat3_q8_8[8] = mx_mul_q8_8(cos_a, cos30);

    return mx_client_m3v3p2x_q8_8(mat3_q8_8,
                                   persp_d,
                                   screen_cx,
                                   screen_cy,
                                   xram_in,
                                   xram_out,
                                   count);
}

/* Complete high-level batch helper for the common demo path.
 * The function validates the XRAM window, uploads integer vec3 input points,
 * executes the built-in Y-rotation projection kernel, and reads back packed
 * int16 screen coordinates into a caller-friendly array. */
mx_client_result_t mx_client_project_vec3i_batch_yrot30(int angle_deg,
                                                        int16_t persp_d,
                                                        int16_t screen_cx,
                                                        int16_t screen_cy,
                                                        uint16_t xram_base,
                                                        const mx_vec3i_t *vecs,
                                                        mx_point2i_t *points,
                                                        uint16_t count)
{
    mx_client_result_t result;
    uint32_t total_bytes;
    uint16_t xram_in;
    uint16_t xram_out;

    if ((count != 0u && vecs == NULL) || (count != 0u && points == NULL))
        return mx_client_error_result(MX_ERR_HEADER);

    total_bytes = (uint32_t)count * 16u;
    if ((uint32_t)xram_base + total_bytes > 0x10000u)
        return mx_client_error_result(MX_ERR_BAD_XRAM);

    xram_in = xram_base;
    xram_out = (uint16_t)(xram_base + (uint16_t)count * 12u);

    mx_client_xram_write_vec3i_array(xram_in, vecs, count);
    result = mx_client_m3v3p2x_yrot30(angle_deg,
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
