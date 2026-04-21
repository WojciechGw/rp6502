/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MATHVM_MATHVM_CLIENT_LLVM_MOS_H_
#define _MATHVM_MATHVM_CLIENT_LLVM_MOS_H_

#include "mathvm/mathvm_client.h"

/* Describes one 3D point using ordinary float coordinates.
 * This is the natural input type for toolchains such as llvm-mos that provide
 * usable float support on the caller side. */
typedef struct
{
    float x;
    float y;
    float z;
} mx_vec3f_t;

/* Packs one native float value into a raw MATHVM word. */
mx_word_t mx_client_llvmmos_word_from_f32(float value);

/* Unpacks one raw MATHVM word into a native float value. */
float mx_client_llvmmos_f32_from_word(mx_word_t value);

/* Executes a small scalar MATHVM program that computes sqrt(a*a + b*b).
 * The result is returned as a native float through `out`. */
mx_client_result_t mx_client_pitagoras_f32(float a, float b, float *out);

/* Executes the fixed M3V3L kernel using native float input and output arrays. */
mx_client_result_t mx_client_m3v3l_f32(const float mat3[9],
                                       const float vec3[3],
                                       float out[3]);

/* Executes the SPR2L kernel in bounding-box mode using native float arrays. */
mx_client_result_t mx_client_spr2l_bbox_f32(const float affine2x3[6],
                                            const float sprite[4],
                                            float out[4]);

/* Executes the SPR2L kernel in corner-output mode using native float arrays. */
mx_client_result_t mx_client_spr2l_corners_f32(const float affine2x3[6],
                                               const float sprite[4],
                                               float out[8]);

/* Writes an array of float vec3 values to XRAM as three packed float32 words
 * per record in the layout expected by MX_M3V3P2X. */
void mx_client_xram_write_vec3f_array(uint16_t xram_addr,
                                      const mx_vec3f_t *vecs,
                                      uint16_t count);

/* Executes MX_M3V3P2X using a native float matrix and camera parameters. */
mx_client_result_t mx_client_m3v3p2x_f32(const float mat3[9],
                                         float persp_d,
                                         float screen_cx,
                                         float screen_cy,
                                         uint16_t xram_in,
                                         uint16_t xram_out,
                                         uint16_t count);

/* Complete high-level float-friendly batch helper.
 * The function uploads float vec3 input points to XRAM, executes the batch
 * projection kernel, and reads back packed int16 screen points. */
mx_client_result_t mx_client_project_vec3f_batch_f32(const float mat3[9],
                                                     float persp_d,
                                                     float screen_cx,
                                                     float screen_cy,
                                                     uint16_t xram_base,
                                                     const mx_vec3f_t *vecs,
                                                     mx_point2i_t *points,
                                                     uint16_t count);

#endif /* _MATHVM_MATHVM_CLIENT_LLVM_MOS_H_ */
