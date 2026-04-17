/*
 * MATHVM batch dodecahedron debug program.
 *
 * Uploads the same 20 dodecahedron vertices to XRAM, runs the batch
 * projection for a few angles, and prints the resulting x/y pairs.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-License-Identifier: Unlicense
 */

#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mathvm_client.h"

#define XRAM_VERT_IN   0xE100u
#define XRAM_VERT_OUT  0xE200u

#define F32_0_0       0x00000000u
#define F32_200_0     0x43480000u
#define F32_320_0     0x43A00000u
#define F32_180_0     0x43340000u
#define F32_0_5       0x3F000000u
#define F32_COS30     0x3F5DB3D7u

#define F32_12_0      0x41400000u
#define F32_20_0      0x41A00000u
#define F32_32_0      0x42000000u
#define F32_NEG12_0   0xC1400000u
#define F32_NEG20_0   0xC1A00000u
#define F32_NEG32_0   0xC2000000u

#define DODECA_VERTS 20

static const uint32_t verts_bits[DODECA_VERTS][3] = {
    {F32_NEG20_0, F32_NEG20_0, F32_NEG20_0},
    {F32_20_0,    F32_NEG20_0, F32_NEG20_0},
    {F32_20_0,    F32_20_0,    F32_NEG20_0},
    {F32_NEG20_0, F32_20_0,    F32_NEG20_0},
    {F32_NEG20_0, F32_NEG20_0, F32_20_0},
    {F32_20_0,    F32_NEG20_0, F32_20_0},
    {F32_20_0,    F32_20_0,    F32_20_0},
    {F32_NEG20_0, F32_20_0,    F32_20_0},
    {F32_0_0,     F32_NEG12_0, F32_NEG32_0},
    {F32_0_0,     F32_12_0,    F32_NEG32_0},
    {F32_0_0,     F32_NEG12_0, F32_32_0},
    {F32_0_0,     F32_12_0,    F32_32_0},
    {F32_NEG12_0, F32_NEG32_0, F32_0_0},
    {F32_12_0,    F32_NEG32_0, F32_0_0},
    {F32_NEG12_0, F32_32_0,    F32_0_0},
    {F32_12_0,    F32_32_0,    F32_0_0},
    {F32_NEG32_0, F32_0_0,     F32_NEG12_0},
    {F32_32_0,    F32_0_0,     F32_NEG12_0},
    {F32_NEG32_0, F32_0_0,     F32_12_0},
    {F32_32_0,    F32_0_0,     F32_12_0},
};

static const uint32_t sin_tab_f32[91] = {
    0x00000000u, 0x3C800000u, 0x3D100000u, 0x3D500000u, 0x3D900000u, 0x3DB00000u,
    0x3DD80000u, 0x3DF80000u, 0x3E100000u, 0x3E200000u, 0x3E300000u, 0x3E440000u,
    0x3E540000u, 0x3E680000u, 0x3E780000u, 0x3E840000u, 0x3E8E0000u, 0x3E960000u,
    0x3E9E0000u, 0x3EA60000u, 0x3EB00000u, 0x3EB80000u, 0x3EC00000u, 0x3EC80000u,
    0x3ED00000u, 0x3ED80000u, 0x3EE00000u, 0x3EE80000u, 0x3EF00000u, 0x3EF80000u,
    0x3F000000u, 0x3F040000u, 0x3F080000u, 0x3F0B0000u, 0x3F0F0000u, 0x3F130000u,
    0x3F160000u, 0x3F1A0000u, 0x3F1E0000u, 0x3F210000u, 0x3F250000u, 0x3F280000u,
    0x3F2B0000u, 0x3F2F0000u, 0x3F320000u, 0x3F350000u, 0x3F380000u, 0x3F3B0000u,
    0x3F3E0000u, 0x3F410000u, 0x3F440000u, 0x3F470000u, 0x3F4A0000u, 0x3F4C0000u,
    0x3F4F0000u, 0x3F520000u, 0x3F540000u, 0x3F570000u, 0x3F590000u, 0x3F5B0000u,
    0x3F5E0000u, 0x3F600000u, 0x3F620000u, 0x3F640000u, 0x3F660000u, 0x3F680000u,
    0x3F6A0000u, 0x3F6C0000u, 0x3F6D0000u, 0x3F6F0000u, 0x3F710000u, 0x3F720000u,
    0x3F730000u, 0x3F750000u, 0x3F760000u, 0x3F770000u, 0x3F780000u, 0x3F790000u,
    0x3F7A0000u, 0x3F7B0000u, 0x3F7C0000u, 0x3F7D0000u, 0x3F7E0000u, 0x3F7E0000u,
    0x3F7F0000u, 0x3F7F0000u, 0x3F7F0000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x3F800000u,
};

#define VM_MATRIX_BASE   0u
#define VM_SIN_A         9u
#define VM_COS_A        10u
#define VM_PERSP_D      11u
#define VM_SCREEN_CX    12u
#define VM_SCREEN_CY    13u
#define VM_SIN30        14u
#define VM_COS30        15u
#define VM_LOCAL_WORDS  16u

static mx_word_t vm_locals[VM_LOCAL_WORDS];
static uint8_t vm_frame[16u + (VM_LOCAL_WORDS * 4u) + 128u];
static int proj_x[DODECA_VERTS];
static int proj_y[DODECA_VERTS];

static uint32_t xram_read_u32_seq(uint16_t addr)
{
    uint32_t value;

    RIA.addr0 = addr;
    RIA.step0 = 1;
    value  = (uint32_t)RIA.rw0;
    value |= (uint32_t)RIA.rw0 << 8;
    value |= (uint32_t)RIA.rw0 << 16;
    value |= (uint32_t)RIA.rw0 << 24;
    return value;
}

static uint32_t sin_bits(int deg)
{
    uint32_t bits;

    if (deg <= 90)
        return sin_tab_f32[deg];
    if (deg <= 180)
        return sin_tab_f32[180 - deg];
    if (deg <= 270)
    {
        bits = sin_tab_f32[deg - 180];
        return bits ^ 0x80000000u;
    }
    bits = sin_tab_f32[360 - deg];
    return bits ^ 0x80000000u;
}

static uint32_t cos_bits(int deg)
{
    deg += 90;
    if (deg >= 360)
        deg -= 360;
    return sin_bits(deg);
}

static void xram_write_u32_seq(uint16_t addr, uint32_t value)
{
    RIA.addr0 = addr;
    RIA.step0 = 1;
    RIA.rw0 = (uint8_t)(value & 0xFFu);
    RIA.rw0 = (uint8_t)((value >> 8) & 0xFFu);
    RIA.rw0 = (uint8_t)((value >> 16) & 0xFFu);
    RIA.rw0 = (uint8_t)(value >> 24);
}

static void upload_vertices(void)
{
    uint16_t addr = XRAM_VERT_IN;
    uint8_t i;

    for (i = 0; i < DODECA_VERTS; ++i)
    {
        xram_write_u32_seq(addr, verts_bits[i][0]); addr += 4u;
        xram_write_u32_seq(addr, verts_bits[i][1]); addr += 4u;
        xram_write_u32_seq(addr, verts_bits[i][2]); addr += 4u;
    }
}

static void read_projected_vertices(void)
{
    uint16_t addr = XRAM_VERT_OUT;
    uint8_t i;

    for (i = 0; i < DODECA_VERTS; ++i)
    {
        uint32_t raw = xram_read_u32_seq(addr);

        proj_x[i] = (int)(int16_t)(raw & 0xFFFFu);
        proj_y[i] = (int)(int16_t)(raw >> 16);
        addr += 4u;
    }
}

static void vm_append_u8(uint8_t *frame, uint8_t *len, uint8_t value)
{
    frame[(*len)++] = value;
}

static void vm_append_u16le(uint8_t *frame, uint8_t *len, uint16_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 8);
}

static void vm_append_u32le(uint8_t *frame, uint8_t *len, uint32_t value)
{
    frame[(*len)++] = (uint8_t)(value & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 8) & 0xFFu);
    frame[(*len)++] = (uint8_t)((value >> 16) & 0xFFu);
    frame[(*len)++] = (uint8_t)(value >> 24);
}

static void vm_append_header(uint8_t *frame,
                             uint8_t *len,
                             uint8_t prog_len,
                             uint8_t local_words,
                             uint8_t out_words,
                             uint8_t stack_words)
{
    vm_append_u8(frame, len, 0x4D);
    vm_append_u8(frame, len, 0x01);
    vm_append_u8(frame, len, MX_FLAG_USE_XRAM_IN | MX_FLAG_USE_XRAM_OUT);
    vm_append_u8(frame, len, MX_HEADER_BYTES);
    vm_append_u8(frame, len, prog_len);
    vm_append_u8(frame, len, local_words);
    vm_append_u8(frame, len, out_words);
    vm_append_u8(frame, len, stack_words);
    vm_append_u16le(frame, len, XRAM_VERT_IN);
    vm_append_u16le(frame, len, XRAM_VERT_OUT);
    vm_append_u16le(frame, len, DODECA_VERTS);
    vm_append_u16le(frame, len, 0x0000u);
}

static void vm_append_words(uint8_t *frame,
                            uint8_t *len,
                            const mx_word_t *words,
                            uint8_t word_count)
{
    uint8_t i;

    for (i = 0; i < word_count; ++i)
        vm_append_u32le(frame, len, words[i].u32);
}

static void vm_append_build_matrix(uint8_t *prog, uint8_t *len)
{
    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS_A);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x00);

    vm_append_u8(prog, len, MX_PUSHF); vm_append_u32le(prog, len, F32_0_0);
    vm_append_u8(prog, len, MX_STS);   vm_append_u8(prog, len, 0x01);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN_A);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x02);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN_A);
    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN30);
    vm_append_u8(prog, len, MX_FMUL);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x03);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS30);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x04);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS_A);
    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN30);
    vm_append_u8(prog, len, MX_FMUL);
    vm_append_u8(prog, len, MX_FNEG);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x05);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN_A);
    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS30);
    vm_append_u8(prog, len, MX_FMUL);
    vm_append_u8(prog, len, MX_FNEG);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x06);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_SIN30);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x07);

    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS_A);
    vm_append_u8(prog, len, MX_LDS); vm_append_u8(prog, len, VM_COS30);
    vm_append_u8(prog, len, MX_FMUL);
    vm_append_u8(prog, len, MX_STS); vm_append_u8(prog, len, 0x08);
}

static mx_client_result_t project_vertices(int angle)
{
    uint8_t prog[96];
    uint8_t prog_len = 0;
    uint8_t frame_len = 0;
    uint8_t i;
    mx_client_result_t call;

    vm_locals[VM_SIN_A].u32 = sin_bits(angle);
    vm_locals[VM_COS_A].u32 = cos_bits(angle);
    vm_locals[VM_PERSP_D].u32 = F32_200_0;
    vm_locals[VM_SCREEN_CX].u32 = F32_320_0;
    vm_locals[VM_SCREEN_CY].u32 = F32_180_0;
    vm_locals[VM_SIN30].u32 = F32_0_5;
    vm_locals[VM_COS30].u32 = F32_COS30;

    for (i = 0; i < VM_SIN_A; ++i)
        vm_locals[i].u32 = 0u;

    vm_append_build_matrix(prog, &prog_len);
    vm_append_u8(prog, &prog_len, MX_M3V3P2X);
    vm_append_u8(prog, &prog_len, VM_MATRIX_BASE);
    vm_append_u8(prog, &prog_len, VM_PERSP_D);
    vm_append_u8(prog, &prog_len, MX_HALT);

    vm_append_header(vm_frame, &frame_len, prog_len, VM_LOCAL_WORDS, 0u, 12u);
    vm_append_words(vm_frame, &frame_len, vm_locals, VM_LOCAL_WORDS);
    for (i = 0; i < prog_len; ++i)
        vm_append_u8(vm_frame, &frame_len, prog[i]);

    call = mx_client_call_frame(vm_frame, frame_len, NULL, 0u);
    if (call.status == MX_OK)
        read_projected_vertices();
    return call;
}

static void dump_angle(int angle)
{
    mx_client_result_t call;
    int xmin = 32767;
    int ymin = 32767;
    int xmax = -32768;
    int ymax = -32768;
    uint8_t i;

    call = project_vertices(angle);
    printf("\nangle=%d status=%u words=%u\n", angle, call.status, call.out_words);
    if (call.status != MX_OK)
        return;

    for (i = 0; i < DODECA_VERTS; ++i)
    {
        if (proj_x[i] < xmin) xmin = proj_x[i];
        if (proj_x[i] > xmax) xmax = proj_x[i];
        if (proj_y[i] < ymin) ymin = proj_y[i];
        if (proj_y[i] > ymax) ymax = proj_y[i];
    }

    printf("range x=%d..%d y=%d..%d\n", xmin, xmax, ymin, ymax);
    for (i = 0; i < 8u; ++i)
        printf("v%u: %d,%d\n", i, proj_x[i], proj_y[i]);

    puts("raw out bytes:");
    for (i = 0; i < 4u; ++i)
    {
        uint32_t raw = xram_read_u32_seq((uint16_t)(XRAM_VERT_OUT + (uint16_t)i * 4u));
        printf("o%u: %08lX\n", i, (unsigned long)raw);
    }
}

void main(void)
{
    puts("MATHVM dodecahedron batch debug");
    upload_vertices();
    puts("raw in words:");
    printf("in0.x=%08lX in0.y=%08lX in0.z=%08lX\n",
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 0u),
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 4u),
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 8u));
    printf("in1.x=%08lX in1.y=%08lX in1.z=%08lX\n",
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 12u),
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 16u),
           (unsigned long)xram_read_u32_seq(XRAM_VERT_IN + 20u));
    dump_angle(0);
    dump_angle(90);
    dump_angle(180);
}
