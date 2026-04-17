/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MATHVM_MATHVM_CLIENT_H_
#define _MATHVM_MATHVM_CLIENT_H_

#include "mathvm/mathvm.h"

typedef struct
{
    uint8_t status;
    uint8_t out_words;
} mx_client_result_t;

mx_client_result_t mx_client_call_frame(const uint8_t *frame,
                                        uint8_t frame_len,
                                        mx_word_t *out,
                                        uint8_t out_cap_words);

mx_client_result_t mx_client_m3v3l(const mx_word_t mat3[9],
                                   const mx_word_t vec3[3],
                                   mx_word_t out[3]);

mx_client_result_t mx_client_spr2l_bbox(const mx_word_t affine2x3[6],
                                        const mx_word_t sprite[4],
                                        mx_word_t out[4]);

mx_client_result_t mx_client_spr2l_corners(const mx_word_t affine2x3[6],
                                           const mx_word_t sprite[4],
                                           mx_word_t out[8]);

#endif /* _MATHVM_MATHVM_CLIENT_H_ */
