/*
 * MATHVM Math Coprocessor Test
 *
 * Based on rp6502_examples/src/MTHexamples/mathcop.c, but runs the checks
 * through OS $80 and MATHVM bytecode.
 *
 * This file no longer depends on the detached legacy mth backend.
 */

#include "mathvm_client.h"
#include <stdio.h>
#include <time.h>

#define F32_0_0          0x00000000UL
#define F32_1_0          0x3F800000UL
#define F32_2_0          0x40000000UL
#define F32_3_0          0x40400000UL
#define F32_3_5          0x40600000UL
#define F32_4_0          0x40800000UL
#define F32_5_0          0x40A00000UL
#define F32_5_5          0x40B00000UL
#define F32_7_0          0x40E00000UL
#define F32_8_0          0x41000000UL
#define F32_91_0         0x42B60000UL
#define F32_PI_OVER_180  0x3C8EFA35UL
#define F32_10000        0x461C4000UL
#define F32_NEG_2_0      0xC0000000UL

static unsigned int passed;
static unsigned int failed;

static void append_u8(uint8_t *frame, uint16_t *len, uint8_t value)
{
    frame[(*len)++] = value;
}

static void append_u16le(uint8_t *frame, uint16_t *len, uint16_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 8);
}

static void append_u32le(uint8_t *frame, uint16_t *len, uint32_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 8) & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 16) & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 24);
}

static void append_header(uint8_t *frame,
                          uint16_t *len,
                          uint8_t prog_len,
                          uint8_t out_words,
                          uint8_t stack_words)
{
    append_u8(frame, len, 0x4D);
    append_u8(frame, len, 0x01);
    append_u8(frame, len, 0x00);
    append_u8(frame, len, MX_HEADER_BYTES);
    append_u8(frame, len, prog_len);
    append_u8(frame, len, 0x00);
    append_u8(frame, len, out_words);
    append_u8(frame, len, stack_words);
    append_u16le(frame, len, 0xFFFFu);
    append_u16le(frame, len, 0xFFFFu);
    append_u16le(frame, len, 0x0001u);
    append_u16le(frame, len, 0x0000u);
}

static mx_client_result_t run_unary_u32(uint8_t op, uint32_t arg, mx_word_t *out)
{
    uint8_t frame[32];
    uint16_t len = 0;

    append_header(frame, &len, 8u, 1u, 4u);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, arg);
    append_u8(frame, &len, op);
    append_u8(frame, &len, MX_RET);
    append_u8(frame, &len, 0x01);

    return mx_client_call_frame(frame, len, out, 1u);
}

static mx_client_result_t run_binary_u32(uint8_t op, uint32_t a, uint32_t b, mx_word_t *out)
{
    uint8_t frame[40];
    uint16_t len = 0;

    append_header(frame, &len, 13u, 1u, 4u);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, a);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, b);
    append_u8(frame, &len, op);
    append_u8(frame, &len, MX_RET);
    append_u8(frame, &len, 0x01);

    return mx_client_call_frame(frame, len, out, 1u);
}

static mx_client_result_t run_unary_f32(uint8_t op, uint32_t arg_bits, mx_word_t *out)
{
    uint8_t frame[32];
    uint16_t len = 0;

    append_header(frame, &len, 8u, 1u, 4u);
    append_u8(frame, &len, MX_PUSHF);
    append_u32le(frame, &len, arg_bits);
    append_u8(frame, &len, op);
    append_u8(frame, &len, MX_RET);
    append_u8(frame, &len, 0x01);

    return mx_client_call_frame(frame, len, out, 1u);
}

static mx_client_result_t run_binary_f32(uint8_t op, uint32_t a_bits, uint32_t b_bits, mx_word_t *out)
{
    uint8_t frame[40];
    uint16_t len = 0;

    append_header(frame, &len, 13u, 1u, 4u);
    append_u8(frame, &len, MX_PUSHF);
    append_u32le(frame, &len, a_bits);
    append_u8(frame, &len, MX_PUSHF);
    append_u32le(frame, &len, b_bits);
    append_u8(frame, &len, op);
    append_u8(frame, &len, MX_RET);
    append_u8(frame, &len, 0x01);

    return mx_client_call_frame(frame, len, out, 1u);
}

static mx_client_result_t run_binary_d64(uint8_t op,
                                         uint32_t alo,
                                         uint32_t ahi,
                                         uint32_t blo,
                                         uint32_t bhi,
                                         mx_word_t *out)
{
    uint8_t frame[64];
    uint16_t len = 0;

    append_header(frame, &len, 23u, 2u, 8u);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, alo);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, ahi);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, blo);
    append_u8(frame, &len, MX_PUSHI);
    append_u32le(frame, &len, bhi);
    append_u8(frame, &len, op);
    append_u8(frame, &len, MX_RET);
    append_u8(frame, &len, 0x02);

    return mx_client_call_frame(frame, len, out, 2u);
}

static void check_int(const char *name, int result, int expected)
{
    if (result == expected)
    {
        printf("PASS: %s\n", name);
        ++passed;
    }
    else
    {
        printf("FAIL: %s  got=%d  exp=%d\n", name, result, expected);
        ++failed;
    }
}

static void check_long(const char *name, unsigned long result, unsigned long expected)
{
    if (result == expected)
    {
        printf("PASS: %s\n", name);
        ++passed;
    }
    else
    {
        printf("FAIL: %s  got=%08lX  exp=%08lX\n", name, result, expected);
        ++failed;
    }
}

static long bhaskara_sin10000(int deg)
{
    long p = (long)deg * (180 - deg);
    return 40000L * p / (40500L - p);
}

static long bhaskara_cos10000(int deg)
{
    return bhaskara_sin10000(90 - deg);
}

static long cpu_sum_trig10000(uint8_t trig_op)
{
    long sum = 0;
    int deg;

    for (deg = 0; deg <= 90; ++deg)
        sum += (trig_op == MX_FSIN) ? bhaskara_sin10000(deg) : bhaskara_cos10000(deg);

    return sum;
}

static uint8_t build_sum_trig_frame(uint8_t trig_op, uint8_t *frame)
{
    uint8_t prog[80];
    uint16_t prog_len = 0;
    uint16_t frame_len = 0;
    uint16_t loop_start;
    uint16_t rel_pos;
    int8_t rel;

    loop_start = prog_len;

    append_u8(prog, &prog_len, MX_LDS);  append_u8(prog, &prog_len, 0x01);
    append_u8(prog, &prog_len, MX_LDS);  append_u8(prog, &prog_len, 0x00);
    append_u8(prog, &prog_len, MX_PUSHF); append_u32le(prog, &prog_len, F32_PI_OVER_180);
    append_u8(prog, &prog_len, MX_FMUL);
    append_u8(prog, &prog_len, trig_op);
    append_u8(prog, &prog_len, MX_PUSHF); append_u32le(prog, &prog_len, F32_10000);
    append_u8(prog, &prog_len, MX_FMUL);
    append_u8(prog, &prog_len, MX_FADD);
    append_u8(prog, &prog_len, MX_STS);  append_u8(prog, &prog_len, 0x01);

    append_u8(prog, &prog_len, MX_LDS);  append_u8(prog, &prog_len, 0x00);
    append_u8(prog, &prog_len, MX_PUSHF); append_u32le(prog, &prog_len, F32_1_0);
    append_u8(prog, &prog_len, MX_FADD);
    append_u8(prog, &prog_len, MX_STS);  append_u8(prog, &prog_len, 0x00);

    append_u8(prog, &prog_len, MX_LDS);  append_u8(prog, &prog_len, 0x02);
    append_u8(prog, &prog_len, MX_PUSHF); append_u32le(prog, &prog_len, F32_1_0);
    append_u8(prog, &prog_len, MX_FSUB);
    append_u8(prog, &prog_len, MX_DUP);
    append_u8(prog, &prog_len, MX_STS);  append_u8(prog, &prog_len, 0x02);
    append_u8(prog, &prog_len, MX_JNZ);
    rel_pos = prog_len;
    append_u8(prog, &prog_len, 0x00);

    append_u8(prog, &prog_len, MX_LDS);  append_u8(prog, &prog_len, 0x01);
    append_u8(prog, &prog_len, MX_RET);  append_u8(prog, &prog_len, 0x01);

    rel = (int8_t)((int)loop_start - (int)(rel_pos + 1u));
    prog[rel_pos] = (uint8_t)rel;

    append_u8(frame, &frame_len, 0x4D);
    append_u8(frame, &frame_len, 0x01);
    append_u8(frame, &frame_len, 0x00);
    append_u8(frame, &frame_len, MX_HEADER_BYTES);
    append_u8(frame, &frame_len, prog_len);
    append_u8(frame, &frame_len, 0x03);
    append_u8(frame, &frame_len, 0x01);
    append_u8(frame, &frame_len, 0x10);
    append_u16le(frame, &frame_len, 0xFFFFu);
    append_u16le(frame, &frame_len, 0xFFFFu);
    append_u16le(frame, &frame_len, 0x0001u);
    append_u16le(frame, &frame_len, 0x0000u);

    append_u32le(frame, &frame_len, F32_0_0);
    append_u32le(frame, &frame_len, F32_0_0);
    append_u32le(frame, &frame_len, F32_91_0);

    {
        uint8_t i;
        for (i = 0; i < prog_len; ++i)
            append_u8(frame, &frame_len, prog[i]);
    }

    return (uint8_t)frame_len;
}

static uint32_t mathvm_sum_trig10000(uint8_t trig_op)
{
    uint8_t frame[128];
    uint8_t frame_len;
    mx_word_t out[1];
    mx_client_result_t call;

    frame_len = build_sum_trig_frame(trig_op, frame);
    call = mx_client_call_frame(frame, frame_len, out, 1u);
    if (call.status != 0 || call.out_words != 1)
        return 0xFFFFFFFFUL;
    return out[0].u32;
}

static void benchmark_one(const char *name, uint8_t trig_op)
{
    clock_t t_start;
    clock_t t_cpu;
    clock_t t_mathvm;
    unsigned long ratio_tenths;
    long cpu_sum;
    uint32_t mathvm_sum;

    puts("");
    printf("-- Benchmark: %s sum(0..90) --\n", name);

    t_start = clock();
    cpu_sum = cpu_sum_trig10000(trig_op);
    t_cpu = clock() - t_start;

    t_start = clock();
    mathvm_sum = mathvm_sum_trig10000(trig_op);
    t_mathvm = clock() - t_start;

    printf("pure CPU      : %lu ms\n",
           (unsigned long)t_cpu * 1000UL / (unsigned long)CLOCKS_PER_SEC);
    printf("one MATHVM call: %lu ms\n",
           (unsigned long)t_mathvm * 1000UL / (unsigned long)CLOCKS_PER_SEC);
    printf("CPU sum (x10000): %ld\n", cpu_sum);
    printf("MATHVM sum bits: %08lX\n", (unsigned long)mathvm_sum);

    if (t_mathvm > 0)
    {
        ratio_tenths = ((unsigned long)t_cpu * 10UL + (unsigned long)t_mathvm / 2UL) /
                       (unsigned long)t_mathvm;
        printf("CPU/MATHVM ratio: %lu.%lux\n", ratio_tenths / 10UL, ratio_tenths % 10UL);
    }
}

int main(void)
{
    mx_word_t out[2];
    mx_client_result_t call;

    puts("MATHVM Math Coprocessor Test");
    puts("===========================");
    passed = 0;
    failed = 0;

    puts("\n-- Integer --");

    call = run_binary_u32(MX_MUL8U, 3u, 7u, out);
    check_int("MUL8   3*7=21", (call.status == 0) ? (int)out[0].u32 : -1, 21);

    call = run_binary_u32(MX_MUL8U, 255u, 255u, out);
    check_int("MUL8   255*255=65025", (call.status == 0) ? (int)out[0].u32 : -1, (int)65025u);

    call = run_binary_u32(MX_MUL16U, 200u, 300u, out);
    check_long("MUL16  200*300=60000", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 60000UL);

    call = run_binary_u32(MX_MUL16U, 1000u, 1000u, out);
    check_long("MUL16  1000*1000=1000000", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 1000000UL);

    call = run_binary_u32(MX_MUL16S, (uint32_t)(int32_t)-5, (uint32_t)(int32_t)-3, out);
    check_long("MULS16 -5*-3=15", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 15UL);

    call = run_binary_u32(MX_MUL16S, 7u, (uint32_t)(int32_t)-4, out);
    check_long("MULS16 7*-4=-28", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, (unsigned long)-28L);

    call = run_binary_u32(MX_DIV16U, 100u, 7u, out);
    check_long("DIV16  100/7=14r2", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 0x0002000EUL);

    call = run_binary_u32(MX_DIV16U, 65535u, 256u, out);
    check_long("DIV16  65535/256=255r255", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 0x00FF00FFUL);

    call = run_unary_u32(MX_SQRT32U, 144u, out);
    check_int("SQRT32 sqrt(144)=12", (call.status == 0) ? (int)out[0].u32 : -1, 12);

    call = run_unary_u32(MX_SQRT32U, 10000u, out);
    check_int("SQRT32 sqrt(10000)=100", (call.status == 0) ? (int)out[0].u32 : -1, 100);

    puts("\n-- Float32 --");

    call = run_binary_f32(MX_FADD, F32_2_0, F32_3_5, out);
    check_long("FADD   2.0+3.5=5.5", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_5_5);

    call = run_binary_f32(MX_FSUB, F32_7_0, F32_3_5, out);
    check_long("FSUB   7.0-3.5=3.5", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_3_5);

    call = run_binary_f32(MX_FMUL, F32_2_0, F32_3_5, out);
    check_long("FMUL   2.0*3.5=7.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_7_0);

    call = run_binary_f32(MX_FDIV, F32_7_0, F32_2_0, out);
    check_long("FDIV   7.0/2.0=3.5", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_3_5);

    call = run_unary_f32(MX_FSQRT, F32_4_0, out);
    check_long("FSQRT  sqrt(4.0)=2.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_2_0);

    call = run_unary_f32(MX_FSIN, F32_0_0, out);
    check_long("FSIN   sin(0.0)=0.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_0_0);

    call = run_unary_f32(MX_FCOS, F32_0_0, out);
    check_long("FCOS   cos(0.0)=1.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_1_0);

    call = run_binary_f32(MX_FATAN2, F32_0_0, F32_1_0, out);
    check_long("FATAN2 atan2(0,1)=0.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_0_0);

    call = run_binary_f32(MX_FPOW, F32_2_0, F32_3_0, out);
    check_long("FPOW   pow(2,3)=8.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_8_0);

    call = run_unary_f32(MX_FLOG, F32_1_0, out);
    check_long("FLOG   log(1.0)=0.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_0_0);

    call = run_unary_f32(MX_FEXP, F32_0_0, out);
    check_long("FEXP   exp(0.0)=1.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_1_0);

    call = run_unary_f32(MX_FTOI, F32_3_0, out);
    check_long("FTOI   (int)3.0=3", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 3UL);

    call = run_unary_f32(MX_FTOI, F32_NEG_2_0, out);
    check_long("FTOI   (int)-2.0=-2", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, (unsigned long)-2L);

    call = run_unary_u32(MX_ITOF, 5u, out);
    check_long("ITOF   (float)5=5.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_5_0);

    call = run_unary_u32(MX_ITOF, 0u, out);
    check_long("ITOF   (float)0=0.0", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, F32_0_0);

    puts("\n-- Double64 --");

    call = run_binary_d64(MX_DADD, 0x00000000UL, 0x3FF00000UL, 0x00000000UL, 0x3FF00000UL, out);
    check_long("DADD   1.0+1.0=2.0 lo", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 0x00000000UL);
    check_long("DADD   1.0+1.0=2.0 hi", (call.status == 0) ? out[1].u32 : 0xFFFFFFFFUL, 0x40000000UL);

    call = run_binary_d64(MX_DMUL, 0x00000000UL, 0x3FF80000UL, 0x00000000UL, 0x40000000UL, out);
    check_long("DMUL   2.0*1.5=3.0 lo", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 0x00000000UL);
    check_long("DMUL   2.0*1.5=3.0 hi", (call.status == 0) ? out[1].u32 : 0xFFFFFFFFUL, 0x40080000UL);

    call = run_binary_d64(MX_DDIV, 0x00000000UL, 0x40080000UL, 0x00000000UL, 0x3FF80000UL, out);
    check_long("DDIV   3.0/1.5=2.0 lo", (call.status == 0) ? out[0].u32 : 0xFFFFFFFFUL, 0x00000000UL);
    check_long("DDIV   3.0/1.5=2.0 hi", (call.status == 0) ? out[1].u32 : 0xFFFFFFFFUL, 0x40000000UL);

    benchmark_one("sine", MX_FSIN);
    benchmark_one("cosine", MX_FCOS);

    puts("");
    printf("Results: %u passed, %u failed\n", passed, failed);
    return 0;
}
