/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MATHVM_MATHVM_H_
#define _MATHVM_MATHVM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RIA_OP_MATHVM 0x80

#define MX_WORD_BYTES 4u
#define MX_HEADER_BYTES 16u
#define MX_MAX_LOCALS 48u
#define MX_MAX_STACK 24u
#define MX_MAX_PROG 160u
#define MX_MAX_OUT 8u
#define MX_MAX_STEPS 8192u
#define MX_MAX_FRAME (MX_HEADER_BYTES + (MX_MAX_LOCALS * MX_WORD_BYTES) + MX_MAX_PROG)

/* Generic 32-bit VM word viewed as unsigned, signed, or float32 data. */
typedef union
{
    uint32_t u32;
    int32_t i32;
    float f32;
} mx_word_t;

#pragma pack(push, 1)
/* Binary frame header passed to MATHVM before locals and bytecode. */
typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t hdr_size;

    uint8_t prog_len;
    uint8_t local_words;
    uint8_t out_words;
    uint8_t stack_words;

    uint16_t xram_in;
    uint16_t xram_out;

    uint16_t count;
    uint16_t reserved;
} mx_header_t;
#pragma pack(pop)

enum
{
    MX_FLAG_USE_XRAM_IN = 0x01,
    MX_FLAG_USE_XRAM_OUT = 0x02,
    MX_FLAG_RETURN_I16 = 0x04,
    MX_FLAG_SATURATE = 0x08,
    MX_FLAG_DEBUG = 0x10,
    MX_FLAG_RESERVED5 = 0x20,
    MX_FLAG_RESERVED6 = 0x40,
    MX_FLAG_RESERVED7 = 0x80,
};

typedef enum
{
    MX_OK = 0x00,
    MX_ERR_MAGIC = 0x01,
    MX_ERR_VERSION = 0x02,
    MX_ERR_HEADER = 0x03,
    MX_ERR_PROGRAM = 0x04,
    MX_ERR_BAD_OPCODE = 0x05,
    MX_ERR_STACK_OVF = 0x06,
    MX_ERR_STACK_UDF = 0x07,
    MX_ERR_BAD_LOCAL = 0x08,
    MX_ERR_BAD_XRAM = 0x09,
    MX_ERR_NUMERIC = 0x0A,
    MX_ERR_UNSUPPORTED = 0x0B,
} mx_status_t;

typedef enum
{
    MX_NOP = 0x00,      /* Do nothing and continue execution. */
    MX_HALT = 0x01,     /* Stop execution without returning stack data. */
    MX_RET = 0x02,      /* Return N words from the VM stack as the final result. */
    MX_MUL8U = 0x03,    /* Unsigned 8-bit multiply: a * b -> uint16/uint32 result. */
    MX_MUL16U = 0x04,   /* Unsigned 16-bit multiply: a * b -> uint32 result. */
    MX_MUL16S = 0x05,   /* Signed 16-bit multiply: a * b -> int32 result. */
    MX_DIV16U = 0x06,   /* Unsigned 16-bit divide: a / b -> packed quotient and remainder. */
    MX_SQRT32U = 0x07,  /* Unsigned integer square root: floor(sqrt(x)). */

    MX_PUSHF = 0x10,    /* Push an immediate float32 constant onto the stack. */
    MX_PUSHI = 0x11,    /* Push an immediate 32-bit integer word onto the stack. */
    MX_LDS = 0x12,      /* Load one scalar word from locals[index] onto the stack. */
    MX_STS = 0x13,      /* Store one scalar word from the stack into locals[index]. */
    MX_LDV2 = 0x14,     /* Load a 2-word vector from locals[index..index+1]. */
    MX_STV2 = 0x15,     /* Store a 2-word vector into locals[index..index+1]. */
    MX_LDV3 = 0x16,     /* Load a 3-word vector from locals[index..index+2]. */
    MX_STV3 = 0x17,     /* Store a 3-word vector into locals[index..index+2]. */
    MX_DUP = 0x18,      /* Duplicate the top stack word. */
    MX_DROP = 0x19,     /* Discard the top stack word. */
    MX_SWAP = 0x1A,     /* Swap the top two stack words. */
    MX_OVER = 0x1B,     /* Copy the second stack word to the top of the stack. */
    MX_LDD = 0x1C,      /* Load one double64 value as two 32-bit words from locals. */
    MX_STD = 0x1D,      /* Store one double64 value as two 32-bit words into locals. */

    MX_FADD = 0x20,     /* Float32 addition: a + b. */
    MX_FSUB = 0x21,     /* Float32 subtraction: a - b. */
    MX_FMUL = 0x22,     /* Float32 multiplication: a * b. */
    MX_FDIV = 0x23,     /* Float32 division: a / b. */
    MX_FMADD = 0x24,    /* Float32 fused multiply-add: a * b + c. */
    MX_FNEG = 0x25,     /* Float32 negation: -a. */
    MX_FABS = 0x26,     /* Float32 absolute value: abs(a). */
    MX_FSQRT = 0x27,    /* Float32 square root: sqrt(a). */
    MX_FSIN = 0x28,     /* Float32 sine: sin(a). */
    MX_FCOS = 0x29,     /* Float32 cosine: cos(a). */
    MX_FMIN = 0x2A,     /* Float32 minimum: min(a, b). */
    MX_FMAX = 0x2B,     /* Float32 maximum: max(a, b). */
    MX_FLOOR = 0x2C,    /* Float32 floor: largest integer <= a. */
    MX_FCEIL = 0x2D,    /* Float32 ceil: smallest integer >= a. */
    MX_FROUND = 0x2E,   /* Float32 round to nearest integer value. */
    MX_FTRUNC = 0x2F,   /* Float32 truncation toward zero. */

    MX_V2ADD = 0x30,    /* 2D vector addition: v + w. */
    MX_V2SUB = 0x31,    /* 2D vector subtraction: v - w. */
    MX_V2DOT = 0x32,    /* 2D dot product: v.x*w.x + v.y*w.y. */
    MX_V2SCALE = 0x33,  /* 2D vector scaling by a scalar factor. */
    MX_A2P2L = 0x34,    /* Apply a 2x3 affine transform to one 2D point from locals. */
    MX_V3ADD = 0x38,    /* 3D vector addition: v + w. */
    MX_V3SUB = 0x39,    /* 3D vector subtraction: v - w. */
    MX_V3DOT = 0x3A,    /* 3D dot product: v.x*w.x + v.y*w.y + v.z*w.z. */
    MX_V3SCALE = 0x3B,  /* 3D vector scaling by a scalar factor. */
    MX_CROSS3 = 0x3C,   /* 3D cross product: v x w. */
    MX_NORM3 = 0x3D,    /* Normalize a 3D vector to unit length. */
    MX_M3V3L = 0x3E,    /* Multiply a 3x3 matrix by a 3D vector from locals. */
    MX_M3M3L = 0x3F,    /* Multiply two 3x3 matrices from locals. */
    MX_FATAN2 = 0x40,   /* Float32 two-argument arctangent: atan2(y, x). */
    MX_FPOW = 0x41,     /* Float32 power: a ^ b. */
    MX_FLOG = 0x42,     /* Float32 natural logarithm: log(a). */
    MX_FEXP = 0x43,     /* Float32 exponential: exp(a). */
    MX_FTOI = 0x44,     /* Convert float32 to integer word. */
    MX_ITOF = 0x45,     /* Convert integer word to float32. */
    MX_DADD = 0x46,     /* Double64 addition: a + b. */
    MX_DMUL = 0x47,     /* Double64 multiplication: a * b. */

    MX_SPR2L = 0x48,    /* Transform one 2D sprite and return corners or bounding box. */
    MX_BBOX2 = 0x49,    /* Compute a 2D bounding box from point inputs. */
    MX_ROUND2I = 0x4A,  /* Round two float32 values to integer outputs. */
    MX_DDIV = 0x4B,     /* Double64 division: a / b. */
    MX_M3V3P2X = 0x4C,  /* Batch-project XRAM vec3 records through mat3 + perspective to int16 x/y. */

    MX_CMPZ = 0x60,     /* Compare one value with zero and push the boolean result. */
    MX_FCMPLT = 0x61,   /* Float32 comparison: a < b. */
    MX_FCMPGT = 0x62,   /* Float32 comparison: a > b. */
    MX_JMP = 0x63,      /* Unconditional relative jump. */
    MX_JZ = 0x64,       /* Relative jump if the popped condition equals zero. */
    MX_JNZ = 0x65,      /* Relative jump if the popped condition is non-zero. */
    MX_SELECT = 0x66,   /* Select between two values based on a condition word. */
} mx_opcode_t;

/* Fixed-size helper used to assemble one binary MATHVM frame in memory. */
typedef struct
{
    uint8_t data[MX_MAX_FRAME];
    uint16_t len;
} mx_frame_builder_t;

/* Complete VM execution state used by the firmware-side interpreter. */
typedef struct
{
    mx_header_t hdr;
    mx_word_t locals[MX_MAX_LOCALS];
    mx_word_t stack[MX_MAX_STACK];
    mx_word_t out[MX_MAX_OUT];
    uint8_t program[MX_MAX_PROG];
    uint8_t pc;
    uint8_t sp;
    uint8_t outc;
    uint8_t status;
    uint16_t steps;
} mx_vm_t;

/* Reset the frame builder to an empty frame. */
void mx_frame_builder_reset(mx_frame_builder_t *builder);
/* Append one raw byte to the frame being built. */
bool mx_frame_builder_append_u8(mx_frame_builder_t *builder, uint8_t value);
/* Append one 16-bit little-endian value to the frame. */
bool mx_frame_builder_append_u16(mx_frame_builder_t *builder, uint16_t value);
/* Append one 32-bit little-endian value to the frame. */
bool mx_frame_builder_append_u32(mx_frame_builder_t *builder, uint32_t value);
/* Append one float32 value encoded as one VM word. */
bool mx_frame_builder_append_f32(mx_frame_builder_t *builder, float value);
/* Append an arbitrary raw byte block to the frame. */
bool mx_frame_builder_append_bytes(mx_frame_builder_t *builder, const void *data, size_t size);
/* Append an array of VM words to the frame. */
bool mx_frame_builder_append_words(mx_frame_builder_t *builder, const mx_word_t *words, size_t count);
/* Append a complete MATHVM binary header structure. */
bool mx_frame_builder_append_header(mx_frame_builder_t *builder, const mx_header_t *header);
/* Return a pointer to the frame builder's byte buffer. */
const uint8_t *mx_frame_builder_data(const mx_frame_builder_t *builder);
/* Return the current frame size in bytes. */
uint16_t mx_frame_builder_size(const mx_frame_builder_t *builder);

/* Load and validate one binary frame into a VM instance. */
bool mx_load_frame(mx_vm_t *vm, const uint8_t *frame, uint16_t frame_len);
/* Execute the already loaded VM program until HALT, RET, or error. */
bool mx_exec(mx_vm_t *vm);
/* RIA OS entry point that pulls a frame from XSTACK and runs MATHVM. */
bool mathvm_api_op(void);

#endif /* _MATHVM_MATHVM_H_ */
