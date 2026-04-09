/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/api.h"
#include "api/mth.h"
#include <math.h>
#include <string.h>

/* Type-punning helpers — safe via memcpy (no UB) */

static inline float bits_to_f32(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static inline uint32_t f32_to_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static inline bool mth_pop_double(double *d)
{
    return api_pop_n(d, sizeof(double));
}

/* Pop exactly 8 bytes — fails if stack does not have exactly 8 bytes left.
 * Used for the last (first-pushed) double argument.
 */
static bool mth_pop_double_end(double *d)
{
    if (xstack_ptr != XSTACK_SIZE - sizeof(double))
        return false;
    memcpy(d, &xstack[xstack_ptr], sizeof(double));
    xstack_ptr = XSTACK_SIZE;
    return true;
}

static inline bool mth_push_double(double d)
{
    return api_push_n(&d, sizeof(double));
}

/**********************/
/* Integer operations */
/**********************/

/* $30: u8 * u8 -> u16
 * API_A = b (fastcall), xstack = a
 */
bool mth_api_mul8(void)
{
    uint8_t b = API_A;
    uint8_t a;
    if (!api_pop_uint8_end(&a))
        return api_return_errno(API_EINVAL);
    return api_return_ax((uint16_t)a * b);
}

/* $31: u16 * u16 -> u32
 * API_AX = b (fastcall), xstack = a
 */
bool mth_api_mul16(void)
{
    uint16_t b = API_AX;
    uint16_t a;
    if (!api_pop_uint16_end(&a))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg((uint32_t)a * b);
}

/* $32: s16 * s16 -> s32
 * API_AX = b (fastcall), xstack = a
 */
bool mth_api_muls16(void)
{
    int16_t b = (int16_t)API_AX;
    int16_t a;
    if (!api_pop_int16_end(&a))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg((uint32_t)((int32_t)a * b));
}

/* $33: u32 / u16 -> quot(u16) | rem(u16)<<16
 * API_AX = divisor (fastcall), xstack = dividend (u32)
 */
bool mth_api_div16(void)
{
    uint16_t divisor = API_AX;
    if (divisor == 0)
        return api_return_errno(API_EDOM);
    uint32_t dividend;
    if (!api_pop_uint32_end(&dividend))
        return api_return_errno(API_EINVAL);
    uint16_t quotient = (uint16_t)(dividend / divisor);
    uint16_t remainder = (uint16_t)(dividend % divisor);
    return api_return_axsreg((uint32_t)quotient | ((uint32_t)remainder << 16));
}

/* $34: sqrt(u32) -> u16
 * API_AXSREG = n (fastcall)
 */
bool mth_api_sqrt32(void)
{
    return api_return_ax((uint16_t)sqrtf((float)API_AXSREG));
}

/***********************/
/* Float32 operations  */
/***********************/

/* $38: f32 + f32 -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fadd(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(bits_to_f32(a_bits) + b));
}

/* $39: f32 - f32 -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fsub(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(bits_to_f32(a_bits) - b));
}

/* $3A: f32 * f32 -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fmul(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(bits_to_f32(a_bits) * b));
}

/* $3B: f32 / f32 -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fdiv(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(bits_to_f32(a_bits) / b));
}

/* $3C: sqrt(f32) -> f32
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_fsqrt(void)
{
    return api_return_axsreg(f32_to_bits(sqrtf(bits_to_f32(API_AXSREG))));
}

/* $3D: sin(f32) -> f32
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_fsin(void)
{
    return api_return_axsreg(f32_to_bits(sinf(bits_to_f32(API_AXSREG))));
}

/* $3E: cos(f32) -> f32
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_fcos(void)
{
    return api_return_axsreg(f32_to_bits(cosf(bits_to_f32(API_AXSREG))));
}

/* $3F: atan2(a, b) -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fatan2(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(atan2f(bits_to_f32(a_bits), b)));
}

/* $40: pow(a, b) -> f32
 * API_AXSREG = b bits (fastcall), xstack = a bits
 */
bool mth_api_fpow(void)
{
    float b = bits_to_f32(API_AXSREG);
    uint32_t a_bits;
    if (!api_pop_uint32_end(&a_bits))
        return api_return_errno(API_EINVAL);
    return api_return_axsreg(f32_to_bits(powf(bits_to_f32(a_bits), b)));
}

/* $41: log(f32) -> f32  (natural logarithm)
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_flog(void)
{
    return api_return_axsreg(f32_to_bits(logf(bits_to_f32(API_AXSREG))));
}

/* $42: exp(f32) -> f32
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_fexp(void)
{
    return api_return_axsreg(f32_to_bits(expf(bits_to_f32(API_AXSREG))));
}

/* $43: (s32)f32  — truncate float to signed integer
 * API_AXSREG = a bits (fastcall)
 */
bool mth_api_ftoi(void)
{
    return api_return_axsreg((uint32_t)(int32_t)bits_to_f32(API_AXSREG));
}

/* $44: (f32)s32  — convert signed integer to float
 * API_AXSREG = a (s32, fastcall)
 */
bool mth_api_itof(void)
{
    return api_return_axsreg(f32_to_bits((float)(int32_t)API_AXSREG));
}

/***********************/
/* Double64 operations */
/***********************/

/* $48: f64 + f64 -> f64
 * xstack (top): b (8 bytes), below: a (8 bytes)
 * Result pushed to xstack; api_return_ax(0) signals success.
 */
bool mth_api_dadd(void)
{
    double b, a;
    if (!mth_pop_double(&b))
        return api_return_errno(API_EINVAL);
    if (!mth_pop_double_end(&a))
        return api_return_errno(API_EINVAL);
    if (!mth_push_double(a + b))
        return api_return_errno(API_ENOMEM);
    return api_return_ax(0);
}

/* $49: f64 * f64 -> f64
 * xstack (top): b (8 bytes), below: a (8 bytes)
 * Result pushed to xstack; api_return_ax(0) signals success.
 */
bool mth_api_dmul(void)
{
    double b, a;
    if (!mth_pop_double(&b))
        return api_return_errno(API_EINVAL);
    if (!mth_pop_double_end(&a))
        return api_return_errno(API_EINVAL);
    if (!mth_push_double(a * b))
        return api_return_errno(API_ENOMEM);
    return api_return_ax(0);
}

/* $4A: f64 / f64 -> f64
 * xstack (top): b (8 bytes), below: a (8 bytes)
 * Result pushed to xstack; api_return_ax(0) signals success.
 */
bool mth_api_ddiv(void)
{
    double b, a;
    if (!mth_pop_double(&b))
        return api_return_errno(API_EINVAL);
    if (!mth_pop_double_end(&a))
        return api_return_errno(API_EINVAL);
    if (!mth_push_double(a / b))
        return api_return_errno(API_ENOMEM);
    return api_return_ax(0);
}
