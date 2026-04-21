/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mathvm/mathvm.h"
#include <string.h>

/* Reserve space for additional bytes in the frame builder buffer. */
static bool mx_frame_builder_reserve(mx_frame_builder_t *builder, size_t size)
{
    if (builder == NULL)
        return false;
    if ((size_t)builder->len + size > MX_MAX_FRAME)
        return false;
    return true;
}

/* Reset the frame builder to an empty state. */
void mx_frame_builder_reset(mx_frame_builder_t *builder)
{
    if (builder == NULL)
        return;
    builder->len = 0;
}

/* Append an arbitrary raw byte block to the builder buffer. */
bool mx_frame_builder_append_bytes(mx_frame_builder_t *builder, const void *data, size_t size)
{
    if (data == NULL)
        return size == 0;
    if (!mx_frame_builder_reserve(builder, size))
        return false;

    memcpy(&builder->data[builder->len], data, size);
    builder->len = (uint16_t)(builder->len + size);
    return true;
}

/* Append one raw byte to the builder buffer. */
bool mx_frame_builder_append_u8(mx_frame_builder_t *builder, uint8_t value)
{
    return mx_frame_builder_append_bytes(builder, &value, sizeof(value));
}

/* Append one 16-bit little-endian value to the builder buffer. */
bool mx_frame_builder_append_u16(mx_frame_builder_t *builder, uint16_t value)
{
    uint8_t raw[2];

    raw[0] = (uint8_t)(value & 0xFFu);
    raw[1] = (uint8_t)(value >> 8);
    return mx_frame_builder_append_bytes(builder, raw, sizeof(raw));
}

/* Append one 32-bit little-endian value to the builder buffer. */
bool mx_frame_builder_append_u32(mx_frame_builder_t *builder, uint32_t value)
{
    uint8_t raw[4];

    raw[0] = (uint8_t)(value & 0xFFu);
    raw[1] = (uint8_t)((value >> 8) & 0xFFu);
    raw[2] = (uint8_t)((value >> 16) & 0xFFu);
    raw[3] = (uint8_t)(value >> 24);
    return mx_frame_builder_append_bytes(builder, raw, sizeof(raw));
}

/* Append one float32 value encoded as a 32-bit VM word. */
bool mx_frame_builder_append_f32(mx_frame_builder_t *builder, float value)
{
    uint32_t raw;

    memcpy(&raw, &value, sizeof(raw));
    return mx_frame_builder_append_u32(builder, raw);
}

/* Append an array of VM words to the builder buffer. */
bool mx_frame_builder_append_words(mx_frame_builder_t *builder, const mx_word_t *words, size_t count)
{
    size_t i;

    if (words == NULL)
        return count == 0;
    for (i = 0; i < count; ++i)
        if (!mx_frame_builder_append_u32(builder, words[i].u32))
            return false;
    return true;
}

/* Append one complete MATHVM binary header to the builder buffer. */
bool mx_frame_builder_append_header(mx_frame_builder_t *builder, const mx_header_t *header)
{
    if (header == NULL)
        return false;
    return mx_frame_builder_append_bytes(builder, header, sizeof(*header));
}

const uint8_t *mx_frame_builder_data(const mx_frame_builder_t *builder)
{
    return builder == NULL ? NULL : builder->data;
}

/* Return the current builder size in bytes. */
uint16_t mx_frame_builder_size(const mx_frame_builder_t *builder)
{
    return builder == NULL ? 0 : builder->len;
}
