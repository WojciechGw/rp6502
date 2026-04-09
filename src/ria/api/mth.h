/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 WojciechGw
 *
 */

#ifndef _RIA_API_MTH_H_
#define _RIA_API_MTH_H_

/* Math coprocessor — exposes RP2350 Cortex-M33 FPU to the 6502.
 *
 * Opcodes $30-$34: Integer arithmetic
 * Opcodes $38-$44: Float32 (IEEE 754 single precision)
 * Opcodes $48-$4A: Double64 (IEEE 754 double precision)
 *
 * Calling convention follows the RIA API standard:
 *   - Last argument is fastcall (API_A / API_AX / API_AXSREG)
 *   - Earlier arguments are on xstack (pushed in order, popped in reverse)
 *   - 32-bit results returned via api_return_axsreg()
 *   - Double results pushed to xstack, api_return_ax(0) signals success
 */

#include <stdbool.h>

/* Integer operations */

bool mth_api_mul8(void);   /* $30: u8 * u8 -> u16  */
bool mth_api_mul16(void);  /* $31: u16 * u16 -> u32 */
bool mth_api_muls16(void); /* $32: s16 * s16 -> s32 */
bool mth_api_div16(void);  /* $33: u32 / u16 -> quot(u16) | rem(u16)<<16 */
bool mth_api_sqrt32(void); /* $34: sqrt(u32) -> u16 */

/* Float32 operations */

bool mth_api_fadd(void);   /* $38: f32 + f32 -> f32 */
bool mth_api_fsub(void);   /* $39: f32 - f32 -> f32 */
bool mth_api_fmul(void);   /* $3A: f32 * f32 -> f32 */
bool mth_api_fdiv(void);   /* $3B: f32 / f32 -> f32 */
bool mth_api_fsqrt(void);  /* $3C: sqrt(f32) -> f32 */
bool mth_api_fsin(void);   /* $3D: sin(f32) -> f32  */
bool mth_api_fcos(void);   /* $3E: cos(f32) -> f32  */
bool mth_api_fatan2(void); /* $3F: atan2(f32,f32) -> f32 */
bool mth_api_fpow(void);   /* $40: pow(f32,f32) -> f32  */
bool mth_api_flog(void);   /* $41: log(f32) -> f32  */
bool mth_api_fexp(void);   /* $42: exp(f32) -> f32  */
bool mth_api_ftoi(void);   /* $43: (s32)f32 */
bool mth_api_itof(void);   /* $44: (f32)s32 */

/* Double64 operations */

bool mth_api_dadd(void);   /* $48: f64 + f64 -> f64 (via xstack) */
bool mth_api_dmul(void);   /* $49: f64 * f64 -> f64 (via xstack) */
bool mth_api_ddiv(void);   /* $4A: f64 / f64 -> f64 (via xstack) */

#endif /* _RIA_API_MTH_H_ */
