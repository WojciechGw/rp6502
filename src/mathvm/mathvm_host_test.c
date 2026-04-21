/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal host-side test for MATHVM frame building.
 *
 * Example host build:
 *   cc -std=c11 -Wall -Wextra -Wsign-compare -Isrc/ria -Isrc \
 *      src/mathvm/mathvm_host.c src/mathvm/mathvm_host_test.c \
 *      -o /tmp/mathvm_host_test
 */

#include "mathvm/mathvm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Print one byte buffer as space-separated hexadecimal values. */
static void dump_hex(const uint8_t *data, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i)
    {
        printf("%02X", data[i]);
        if (i + 1u != size)
            putchar(' ');
    }
    putchar('\n');
}

/* Fill locals for the M3V3L frame-building example. */
static void fill_m3v3l_locals(mx_word_t *locals)
{
    /* mat3 row-major */
    locals[0].f32 = 1.0f;
    locals[1].f32 = 2.0f;
    locals[2].f32 = 3.0f;
    locals[3].f32 = 4.0f;
    locals[4].f32 = 5.0f;
    locals[5].f32 = 6.0f;
    locals[6].f32 = 7.0f;
    locals[7].f32 = 8.0f;
    locals[8].f32 = 9.0f;

    /* vec3 */
    locals[9].f32 = 10.0f;
    locals[10].f32 = 20.0f;
    locals[11].f32 = 30.0f;
}

/* Fill locals for the SPR2L frame-building examples. */
static void fill_spr2l_locals(mx_word_t *locals)
{
    /* affine 2x3 */
    locals[0].f32 = 1.0f;
    locals[1].f32 = 0.0f;
    locals[2].f32 = 100.0f;
    locals[3].f32 = 0.0f;
    locals[4].f32 = 1.0f;
    locals[5].f32 = 50.0f;

    /* sprite descriptor */
    locals[6].f32 = 16.0f;
    locals[7].f32 = 8.0f;
    locals[8].f32 = 0.5f;
    locals[9].f32 = 0.5f;
}

/* Print the expected 6502-side OS $80 call sequence for one frame. */
static void print_os80_call_sequence(void)
{
    puts("6502 call sequence for OS $80:");
    puts("  1. Push the frame to $FFEC from the last byte down to the first.");
    puts("  2. Write $80 to $FFEF.");
    puts("  3. JSR $FFF1 and wait until BUSY clears.");
    puts("  4. Read A=status, X=returned word count.");
    puts("  5. Pull 4*X bytes from $FFEC as little-endian words.");
}

/* Initialize a valid baseline frame header for one test case. */
static void init_valid_header(mx_header_t *hdr,
                              uint8_t prog_len,
                              uint8_t local_words,
                              uint8_t out_words,
                              uint8_t stack_words)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = 0x4D;
    hdr->version = 1;
    hdr->flags = 0;
    hdr->hdr_size = sizeof(*hdr);
    hdr->prog_len = prog_len;
    hdr->local_words = local_words;
    hdr->out_words = out_words;
    hdr->stack_words = stack_words;
    hdr->xram_in = 0xFFFFu;
    hdr->xram_out = 0xFFFFu;
    hdr->count = 1;
    hdr->reserved = 0;
}

/* Build one complete frame from header, locals, and bytecode. */
static void build_frame(mx_frame_builder_t *builder,
                        const mx_header_t *hdr,
                        const mx_word_t *locals,
                        size_t local_count,
                        const uint8_t *program,
                        size_t program_size)
{
    mx_frame_builder_reset(builder);
    assert(mx_frame_builder_append_header(builder, hdr));
    assert(mx_frame_builder_append_words(builder, locals, local_count));
    assert(mx_frame_builder_append_bytes(builder, program, program_size));
}

/* Build, load, and execute one frame in a local VM instance. */
static void execute_frame(mx_vm_t *vm,
                          mx_header_t hdr,
                          const mx_word_t *locals,
                          size_t local_count,
                          const uint8_t *program,
                          size_t program_size)
{
    mx_frame_builder_t builder;

    build_frame(&builder, &hdr, locals, local_count, program, program_size);
    assert(mx_load_frame(vm, mx_frame_builder_data(&builder), mx_frame_builder_size(&builder)));
    assert(mx_exec(vm));
}

/* Store one host double64 value into two local VM words. */
static void store_d64(mx_word_t *locals, uint8_t idx, double value)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    locals[idx + 0].u32 = (uint32_t)(bits & 0xFFFFFFFFu);
    locals[idx + 1].u32 = (uint32_t)(bits >> 32);
}

/* Reconstruct one host double64 value from two VM words. */
static double load_d64(const mx_word_t *words)
{
    uint64_t bits = (uint64_t)words[0].u32 | ((uint64_t)words[1].u32 << 32);
    double value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* Assert that frame loading fails with the expected status code. */
static void expect_load_error(const char *name,
                              mx_header_t hdr,
                              const mx_word_t *locals,
                              size_t local_count,
                              const uint8_t *program,
                              size_t program_size,
                              mx_status_t expected_status)
{
    mx_frame_builder_t builder;
    mx_vm_t vm;

    build_frame(&builder, &hdr, locals, local_count, program, program_size);
    assert(!mx_load_frame(&vm, mx_frame_builder_data(&builder), mx_frame_builder_size(&builder)));
    assert(vm.status == expected_status);
    printf("Negative test passed: %s -> status=%u\n", name, (unsigned)vm.status);
}

/* Assert that execution fails with the expected status code. */
static void expect_exec_error(const char *name,
                              mx_header_t hdr,
                              const mx_word_t *locals,
                              size_t local_count,
                              const uint8_t *program,
                              size_t program_size,
                              mx_status_t expected_status)
{
    mx_frame_builder_t builder;
    mx_vm_t vm;

    build_frame(&builder, &hdr, locals, local_count, program, program_size);
    assert(mx_load_frame(&vm, mx_frame_builder_data(&builder), mx_frame_builder_size(&builder)));
    assert(!mx_exec(&vm));
    assert(vm.status == expected_status);
    printf("Negative test passed: %s -> status=%u\n", name, (unsigned)vm.status);
}

/* Test host-side frame building for the M3V3L example. */
static void test_m3v3l(void)
{
    mx_frame_builder_t builder;
    mx_header_t hdr;
    mx_word_t locals[12];
    const uint8_t program[] = {
        MX_M3V3L, 0x00, 0x09,
        MX_RET, 0x03,
    };
    const uint8_t expected_header[] = {
        0x4D, 0x01, 0x00, 0x10,
        0x05, 0x0C, 0x03, 0x08,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x00, 0x00,
    };
    const size_t expected_frame_size =
        sizeof(expected_header) + sizeof(locals) + sizeof(program);

    memset(&hdr, 0, sizeof(hdr));
    memset(locals, 0, sizeof(locals));

    hdr.magic = 0x4D;
    hdr.version = 1;
    hdr.flags = 0;
    hdr.hdr_size = sizeof(hdr);
    hdr.prog_len = sizeof(program);
    hdr.local_words = 12;
    hdr.out_words = 3;
    hdr.stack_words = 8;
    hdr.xram_in = 0xFFFFu;
    hdr.xram_out = 0xFFFFu;
    hdr.count = 1;
    hdr.reserved = 0;

    fill_m3v3l_locals(locals);

    mx_frame_builder_reset(&builder);
    assert(mx_frame_builder_append_header(&builder, &hdr));
    assert(mx_frame_builder_append_words(&builder, locals, 12));
    assert(mx_frame_builder_append_bytes(&builder, program, sizeof(program)));

    assert(mx_frame_builder_size(&builder) == expected_frame_size);
    assert(memcmp(mx_frame_builder_data(&builder), expected_header, sizeof(expected_header)) == 0);
    assert(memcmp(mx_frame_builder_data(&builder) + expected_frame_size - sizeof(program),
                  program, sizeof(program)) == 0);

    puts("MATHVM host-side test: M3V3L frame built successfully.");
    printf("Frame size: %u bytes\n", mx_frame_builder_size(&builder));
    printf("Program bytes: ");
    dump_hex(program, sizeof(program));
    printf("Frame bytes: ");
    dump_hex(mx_frame_builder_data(&builder), mx_frame_builder_size(&builder));
    puts("");
    puts("Expected logical payload:");
    puts("  locals[0..8]  = mat3");
    puts("  locals[9..11] = vec3");
    puts("  program       = M3V3L 0,9 ; RET 3");
    puts("  if executed, output should be [140.0, 320.0, 500.0].");
}

/* Test host-side frame building for the SPR2L bbox example. */
static void test_spr2l(void)
{
    mx_frame_builder_t builder;
    mx_header_t hdr;
    mx_word_t locals[10];
    const uint8_t program[] = {
        MX_SPR2L, 0x00, 0x06, 0x02,
        MX_RET, 0x04,
    };
    const uint8_t expected_header[] = {
        0x4D, 0x01, 0x00, 0x10,
        0x06, 0x0A, 0x04, 0x08,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x00, 0x00,
    };
    const size_t expected_frame_size =
        sizeof(expected_header) + sizeof(locals) + sizeof(program);

    memset(&hdr, 0, sizeof(hdr));
    memset(locals, 0, sizeof(locals));

    hdr.magic = 0x4D;
    hdr.version = 1;
    hdr.flags = 0;
    hdr.hdr_size = sizeof(hdr);
    hdr.prog_len = sizeof(program);
    hdr.local_words = 10;
    hdr.out_words = 4;
    hdr.stack_words = 8;
    hdr.xram_in = 0xFFFFu;
    hdr.xram_out = 0xFFFFu;
    hdr.count = 1;
    hdr.reserved = 0;

    fill_spr2l_locals(locals);

    mx_frame_builder_reset(&builder);
    assert(mx_frame_builder_append_header(&builder, &hdr));
    assert(mx_frame_builder_append_words(&builder, locals, 10));
    assert(mx_frame_builder_append_bytes(&builder, program, sizeof(program)));

    assert(mx_frame_builder_size(&builder) == expected_frame_size);
    assert(memcmp(mx_frame_builder_data(&builder), expected_header, sizeof(expected_header)) == 0);
    assert(memcmp(mx_frame_builder_data(&builder) + expected_frame_size - sizeof(program),
                  program, sizeof(program)) == 0);

    puts("MATHVM host-side test: SPR2L frame built successfully.");
    printf("Frame size: %u bytes\n", mx_frame_builder_size(&builder));
    printf("Program bytes: ");
    dump_hex(program, sizeof(program));
    printf("Frame bytes: ");
    dump_hex(mx_frame_builder_data(&builder), mx_frame_builder_size(&builder));
    puts("");
    puts("Expected logical payload:");
    puts("  locals[0..5] = affine2x3");
    puts("  locals[6..9] = sprite descriptor");
    puts("  program      = SPR2L 0,6,0x02 ; RET 4");
    puts("  if executed, bbox should be [92.0, 46.0, 108.0, 54.0].");
}

/* Test host-side frame building for the SPR2L corners example. */
static void test_spr2l_corners(void)
{
    mx_frame_builder_t builder;
    mx_header_t hdr;
    mx_word_t locals[10];
    const uint8_t program[] = {
        MX_SPR2L, 0x00, 0x06, 0x01,
        MX_RET, 0x08,
    };
    const uint8_t expected_header[] = {
        0x4D, 0x01, 0x00, 0x10,
        0x06, 0x0A, 0x08, 0x08,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x00, 0x00,
    };
    const size_t expected_frame_size =
        sizeof(expected_header) + sizeof(locals) + sizeof(program);

    memset(&hdr, 0, sizeof(hdr));
    memset(locals, 0, sizeof(locals));

    hdr.magic = 0x4D;
    hdr.version = 1;
    hdr.flags = 0;
    hdr.hdr_size = sizeof(hdr);
    hdr.prog_len = sizeof(program);
    hdr.local_words = 10;
    hdr.out_words = 8;
    hdr.stack_words = 8;
    hdr.xram_in = 0xFFFFu;
    hdr.xram_out = 0xFFFFu;
    hdr.count = 1;
    hdr.reserved = 0;

    fill_spr2l_locals(locals);

    mx_frame_builder_reset(&builder);
    assert(mx_frame_builder_append_header(&builder, &hdr));
    assert(mx_frame_builder_append_words(&builder, locals, 10));
    assert(mx_frame_builder_append_bytes(&builder, program, sizeof(program)));

    assert(mx_frame_builder_size(&builder) == expected_frame_size);
    assert(memcmp(mx_frame_builder_data(&builder), expected_header, sizeof(expected_header)) == 0);
    assert(memcmp(mx_frame_builder_data(&builder) + expected_frame_size - sizeof(program),
                  program, sizeof(program)) == 0);

    puts("MATHVM host-side test: SPR2L corners frame built successfully.");
    printf("Frame size: %u bytes\n", mx_frame_builder_size(&builder));
    printf("Program bytes: ");
    dump_hex(program, sizeof(program));
    printf("Frame bytes: ");
    dump_hex(mx_frame_builder_data(&builder), mx_frame_builder_size(&builder));
    puts("");
    puts("Expected logical payload:");
    puts("  locals[0..5] = affine2x3");
    puts("  locals[6..9] = sprite descriptor");
    puts("  program      = SPR2L 0,6,0x01 ; RET 8");
    puts("  if executed, corners should be:");
    puts("    [92.0, 46.0, 108.0, 46.0, 108.0, 54.0, 92.0, 54.0]");
}

/* Test execution of the opcode set that replaces the legacy mth operations. */
static void test_mth_compat_execution(void)
{
    mx_vm_t vm;
    mx_header_t hdr;
    mx_word_t locals[4];
    static const uint8_t float_program[] = {
        MX_LDS, 0x00,
        MX_LDS, 0x01,
        MX_FATAN2,
        MX_LDS, 0x02,
        MX_FPOW,
        MX_LDS, 0x03,
        MX_FEXP,
        MX_FLOG,
        MX_PUSHI, 0xD0, 0xFF, 0xFF, 0xFF,
        MX_ITOF,
        MX_PUSHF, 0x00, 0x00, 0x70, 0xC0,
        MX_FTOI,
        MX_RET, 0x04,
    };
    static const uint8_t int_program[] = {
        MX_PUSHI, 0x06, 0x00, 0x00, 0x00,
        MX_PUSHI, 0x07, 0x00, 0x00, 0x00,
        MX_MUL8U,
        MX_PUSHI, 0x2C, 0x01, 0x00, 0x00,
        MX_PUSHI, 0x05, 0x00, 0x00, 0x00,
        MX_MUL16U,
        MX_PUSHI, 0xF9, 0xFF, 0xFF, 0xFF,
        MX_PUSHI, 0x09, 0x00, 0x00, 0x00,
        MX_MUL16S,
        MX_PUSHI, 0xE8, 0x03, 0x00, 0x00,
        MX_PUSHI, 0x40, 0x00, 0x00, 0x00,
        MX_DIV16U,
        MX_PUSHI, 0x51, 0x00, 0x00, 0x00,
        MX_SQRT32U,
        MX_RET, 0x05,
    };
    static const uint8_t double_program[] = {
        MX_LDD, 0x00,
        MX_LDD, 0x02,
        MX_DADD,
        MX_LDD, 0x00,
        MX_LDD, 0x02,
        MX_DMUL,
        MX_LDD, 0x02,
        MX_LDD, 0x00,
        MX_DDIV,
        MX_RET, 0x06,
    };

    memset(locals, 0, sizeof(locals));
    init_valid_header(&hdr, sizeof(float_program), 4, 4, 8);
    locals[0].u32 = 0x3F800000u;
    locals[1].u32 = 0x3F800000u;
    locals[2].u32 = 0x41000000u;
    locals[3].u32 = 0x3F800000u;
    execute_frame(&vm, hdr, locals, 4, float_program, sizeof(float_program));
    assert(vm.status == MX_OK);
    assert(vm.outc == 4);
    assert(vm.out[0].u32 == 0x41000000u);
    assert(vm.out[1].u32 == 0x3F800000u);
    assert(vm.out[2].u32 == 0xC2280000u);
    assert(vm.out[3].u32 == 0xFFFFFFFDu);

    memset(locals, 0, sizeof(locals));
    init_valid_header(&hdr, sizeof(int_program), 0, 5, 16);
    execute_frame(&vm, hdr, locals, 0, int_program, sizeof(int_program));
    assert(vm.status == MX_OK);
    assert(vm.outc == 5);
    assert(vm.out[0].u32 == 42u);
    assert(vm.out[1].u32 == 1500u);
    assert(vm.out[2].i32 == -63);
    assert(vm.out[3].u32 == 0x0028000Fu);
    assert(vm.out[4].u32 == 9u);

    memset(locals, 0, sizeof(locals));
    init_valid_header(&hdr, sizeof(double_program), 4, 6, 12);
    store_d64(locals, 0, 1.5);
    store_d64(locals, 2, 2.25);
    execute_frame(&vm, hdr, locals, 4, double_program, sizeof(double_program));
    assert(vm.status == MX_OK);
    assert(vm.outc == 6);
    assert(load_d64(&vm.out[0]) == 3.75);
    assert(load_d64(&vm.out[2]) == 3.375);
    assert(load_d64(&vm.out[4]) == 1.5);

    puts("MATHVM host-side test: mth compatibility execution passed.");
}

/* Test negative v1 load-time and execution-time error cases. */
static void test_negative_v1(void)
{
    mx_header_t hdr;
    mx_word_t locals[2];
    const uint8_t nop_ret0[] = {
        MX_RET, 0x00,
    };
    const uint8_t drop_only[] = {
        MX_DROP,
    };
    const uint8_t bad_lds[] = {
        MX_LDS, 0x01,
    };

    memset(locals, 0, sizeof(locals));

    init_valid_header(&hdr, sizeof(nop_ret0), 0, 0, 1);
    hdr.magic = 0x00;
    expect_load_error("bad magic",
                      hdr,
                      locals,
                      0,
                      nop_ret0,
                      sizeof(nop_ret0),
                      MX_ERR_MAGIC);

    init_valid_header(&hdr, sizeof(nop_ret0), 0, 0, 1);
    hdr.hdr_size = (uint8_t)(sizeof(hdr) - 1u);
    expect_load_error("bad header",
                      hdr,
                      locals,
                      0,
                      nop_ret0,
                      sizeof(nop_ret0),
                      MX_ERR_HEADER);

    init_valid_header(&hdr, sizeof(nop_ret0), 0, 0, 1);
    hdr.flags = MX_FLAG_DEBUG;
    expect_load_error("unsupported flag",
                      hdr,
                      locals,
                      0,
                      nop_ret0,
                      sizeof(nop_ret0),
                      MX_ERR_UNSUPPORTED);

    init_valid_header(&hdr, sizeof(drop_only), 0, 0, 1);
    expect_exec_error("stack underflow",
                      hdr,
                      locals,
                      0,
                      drop_only,
                      sizeof(drop_only),
                      MX_ERR_STACK_UDF);

    init_valid_header(&hdr, sizeof(bad_lds), 1, 1, 2);
    locals[0].f32 = 123.0f;
    expect_exec_error("bad local",
                      hdr,
                      locals,
                      1,
                      bad_lds,
                      sizeof(bad_lds),
                      MX_ERR_BAD_LOCAL);
}

/* Run the complete host-side test suite and print frame examples. */
int main(void)
{
    printf("OS code: $%02X\n\n", RIA_OP_MATHVM);
    test_m3v3l();
    puts("");
    test_spr2l();
    puts("");
    test_spr2l_corners();
    puts("");
    test_mth_compat_execution();
    puts("");
    test_negative_v1();
    puts("");
    print_os80_call_sequence();

    return 0;
}
