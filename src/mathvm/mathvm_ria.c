/*
 * Copyright (c) 2026 WojciechGw
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/api.h"
#include "mathvm/mathvm.h"
#include <math.h>
#include <string.h>

static uint32_t rd_u32le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool mx_read_u8(mx_vm_t *vm, uint8_t *value)
{
    if (vm->pc >= vm->hdr.prog_len)
    {
        vm->status = MX_ERR_PROGRAM;
        return false;
    }
    *value = vm->program[vm->pc++];
    return true;
}

static bool mx_read_i8(mx_vm_t *vm, int8_t *value)
{
    uint8_t raw;

    if (!mx_read_u8(vm, &raw))
        return false;
    *value = (int8_t)raw;
    return true;
}

static bool mx_read_u32(mx_vm_t *vm, uint32_t *value)
{
    if ((uint16_t)vm->pc + 4u > vm->hdr.prog_len)
    {
        vm->status = MX_ERR_PROGRAM;
        return false;
    }
    *value = rd_u32le(&vm->program[vm->pc]);
    vm->pc = (uint8_t)(vm->pc + 4u);
    return true;
}

static bool mx_push(mx_vm_t *vm, mx_word_t value)
{
    if (vm->sp >= vm->hdr.stack_words || vm->sp >= MX_MAX_STACK)
    {
        vm->status = MX_ERR_STACK_OVF;
        return false;
    }
    vm->stack[vm->sp++] = value;
    return true;
}

static bool mx_pop(mx_vm_t *vm, mx_word_t *value)
{
    if (vm->sp == 0)
    {
        vm->status = MX_ERR_STACK_UDF;
        return false;
    }
    *value = vm->stack[--vm->sp];
    return true;
}

static bool mx_push_u64(mx_vm_t *vm, uint64_t value)
{
    mx_word_t lo;
    mx_word_t hi;

    lo.u32 = (uint32_t)(value & 0xFFFFFFFFu);
    hi.u32 = (uint32_t)(value >> 32);
    if (!mx_push(vm, lo) || !mx_push(vm, hi))
        return false;
    return true;
}

static bool mx_pop_u64(mx_vm_t *vm, uint64_t *value)
{
    mx_word_t lo;
    mx_word_t hi;

    if (!mx_pop(vm, &hi) || !mx_pop(vm, &lo))
        return false;
    *value = (uint64_t)lo.u32 | ((uint64_t)hi.u32 << 32);
    return true;
}

static double mx_bits_to_d64(uint64_t bits)
{
    double value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t mx_d64_to_bits(double value)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool mx_check_local(mx_vm_t *vm, uint8_t idx, uint8_t count)
{
    if ((uint16_t)idx + count > vm->hdr.local_words || (uint16_t)idx + count > MX_MAX_LOCALS)
    {
        vm->status = MX_ERR_BAD_LOCAL;
        return false;
    }
    return true;
}

static bool mx_check_jump_target(mx_vm_t *vm, int8_t rel)
{
    int target = (int)vm->pc + rel;

    if (target < 0 || target >= vm->hdr.prog_len)
    {
        vm->status = MX_ERR_PROGRAM;
        return false;
    }
    vm->pc = (uint8_t)target;
    return true;
}

static bool mx_pack_i16_output(mx_vm_t *vm)
{
    uint8_t i;

    if ((vm->hdr.flags & MX_FLAG_RETURN_I16) == 0)
        return true;

    for (i = 0; i < vm->outc; ++i)
    {
        float f = vm->out[i].f32;
        long rounded;

        if (!isfinite(f))
        {
            vm->status = MX_ERR_NUMERIC;
            return false;
        }

        rounded = lroundf(f);
        if (rounded < INT16_MIN || rounded > INT16_MAX)
        {
            if ((vm->hdr.flags & MX_FLAG_SATURATE) == 0)
            {
                vm->status = MX_ERR_NUMERIC;
                return false;
            }
            if (rounded < INT16_MIN)
                rounded = INT16_MIN;
            else
                rounded = INT16_MAX;
        }

        vm->out[i].u32 = (uint16_t)(int16_t)rounded;
    }

    return true;
}

static void mx_transform_point2(float a, float b, float tx,
                                float c, float d, float ty,
                                float x, float y, float out[2])
{
    out[0] = fmaf(a, x, fmaf(b, y, tx));
    out[1] = fmaf(c, x, fmaf(d, y, ty));
}

static bool mx_exec_spr2l(mx_vm_t *vm, uint8_t ab, uint8_t sb, uint8_t flags)
{
    float a;
    float b;
    float tx;
    float c;
    float d;
    float ty;
    float w;
    float h;
    float ax;
    float ay;
    float p[8];
    uint8_t i;

    if ((flags & 0x03u) == 0 || (flags & 0x03u) == 0x03u || (flags & 0xF8u) != 0)
    {
        vm->status = MX_ERR_PROGRAM;
        return false;
    }
    if (!mx_check_local(vm, ab, 6) || !mx_check_local(vm, sb, 4))
        return false;

    a = vm->locals[ab + 0].f32;
    b = vm->locals[ab + 1].f32;
    tx = vm->locals[ab + 2].f32;
    c = vm->locals[ab + 3].f32;
    d = vm->locals[ab + 4].f32;
    ty = vm->locals[ab + 5].f32;

    w = vm->locals[sb + 0].f32;
    h = vm->locals[sb + 1].f32;
    ax = vm->locals[sb + 2].f32;
    ay = vm->locals[sb + 3].f32;

    mx_transform_point2(a, b, tx, c, d, ty, -ax * w, -ay * h, &p[0]);
    mx_transform_point2(a, b, tx, c, d, ty, (1.0f - ax) * w, -ay * h, &p[2]);
    mx_transform_point2(a, b, tx, c, d, ty, (1.0f - ax) * w, (1.0f - ay) * h, &p[4]);
    mx_transform_point2(a, b, tx, c, d, ty, -ax * w, (1.0f - ay) * h, &p[6]);

    if (flags & 0x04u)
        for (i = 0; i < 8; ++i)
            p[i] = roundf(p[i]);

    if (flags & 0x02u)
    {
        float xmin = p[0];
        float ymin = p[1];
        float xmax = p[0];
        float ymax = p[1];
        mx_word_t out;

        for (i = 2; i < 8; i += 2)
        {
            if (p[i] < xmin)
                xmin = p[i];
            if (p[i] > xmax)
                xmax = p[i];
            if (p[i + 1] < ymin)
                ymin = p[i + 1];
            if (p[i + 1] > ymax)
                ymax = p[i + 1];
        }

        out.f32 = xmin;
        if (!mx_push(vm, out))
            return false;
        out.f32 = ymin;
        if (!mx_push(vm, out))
            return false;
        out.f32 = xmax;
        if (!mx_push(vm, out))
            return false;
        out.f32 = ymax;
        if (!mx_push(vm, out))
            return false;
        return true;
    }

    for (i = 0; i < 8; ++i)
    {
        mx_word_t out;

        out.f32 = p[i];
        if (!mx_push(vm, out))
            return false;
    }

    return true;
}

static uint16_t mx_xstack_pull_frame(uint8_t *frame, size_t frame_cap)
{
    size_t frame_len = XSTACK_SIZE - xstack_ptr;

    if (frame_len == 0 || frame_len > frame_cap)
    {
        xstack_ptr = XSTACK_SIZE;
        return 0;
    }

    memcpy(frame, &xstack[xstack_ptr], frame_len);
    xstack_ptr = XSTACK_SIZE;
    return (uint16_t)frame_len;
}

static bool mx_xstack_push_output(const mx_vm_t *vm)
{
    uint8_t i = vm->outc;

    while (i-- > 0)
        if (!api_push_uint32(&vm->out[i].u32))
            return false;
    return true;
}

static bool mx_return_status(mx_status_t status, uint8_t out_words)
{
    return api_return_ax((uint16_t)status | ((uint16_t)out_words << 8));
}

bool mx_load_frame(mx_vm_t *vm, const uint8_t *frame, uint16_t frame_len)
{
    uint16_t need;
    uint8_t bad_flags;

    memset(vm, 0, sizeof(*vm));

    if (frame_len < sizeof(mx_header_t))
    {
        vm->status = MX_ERR_HEADER;
        return false;
    }

    memcpy(&vm->hdr, frame, sizeof(vm->hdr));

    if (vm->hdr.magic != 0x4Du)
    {
        vm->status = MX_ERR_MAGIC;
        return false;
    }
    if (vm->hdr.version != 1u)
    {
        vm->status = MX_ERR_VERSION;
        return false;
    }
    if (vm->hdr.hdr_size != sizeof(mx_header_t))
    {
        vm->status = MX_ERR_HEADER;
        return false;
    }

    bad_flags = vm->hdr.flags & (MX_FLAG_USE_XRAM_IN |
                                 MX_FLAG_USE_XRAM_OUT |
                                 MX_FLAG_DEBUG |
                                 MX_FLAG_RESERVED5 |
                                 MX_FLAG_RESERVED6 |
                                 MX_FLAG_RESERVED7);
    if (bad_flags != 0u)
    {
        vm->status = MX_ERR_UNSUPPORTED;
        return false;
    }
    if ((vm->hdr.flags & MX_FLAG_RETURN_I16) != 0u &&
        (vm->hdr.flags & MX_FLAG_SATURATE) == 0u)
    {
        vm->status = MX_ERR_HEADER;
        return false;
    }
    if (vm->hdr.count != 1u ||
        vm->hdr.xram_in != 0xFFFFu ||
        vm->hdr.xram_out != 0xFFFFu)
    {
        vm->status = MX_ERR_UNSUPPORTED;
        return false;
    }
    if (vm->hdr.local_words > MX_MAX_LOCALS ||
        vm->hdr.stack_words > MX_MAX_STACK ||
        vm->hdr.prog_len > MX_MAX_PROG ||
        vm->hdr.out_words > MX_MAX_OUT ||
        vm->hdr.stack_words == 0u)
    {
        vm->status = MX_ERR_HEADER;
        return false;
    }

    need = (uint16_t)sizeof(mx_header_t) +
           ((uint16_t)vm->hdr.local_words * MX_WORD_BYTES) +
           vm->hdr.prog_len;
    if (frame_len < need)
    {
        vm->status = MX_ERR_HEADER;
        return false;
    }

    memcpy(vm->locals, frame + sizeof(mx_header_t), (size_t)vm->hdr.local_words * MX_WORD_BYTES);
    memcpy(vm->program,
           frame + sizeof(mx_header_t) + ((size_t)vm->hdr.local_words * MX_WORD_BYTES),
           vm->hdr.prog_len);

    vm->status = MX_OK;
    return true;
}

bool mx_exec(mx_vm_t *vm)
{
    while (vm->pc < vm->hdr.prog_len)
    {
        mx_word_t a;
        mx_word_t b;
        mx_word_t c;
        uint8_t op = vm->program[vm->pc++];

        if (vm->steps == MX_MAX_STEPS)
        {
            vm->status = MX_ERR_PROGRAM;
            return false;
        }
        ++vm->steps;

        switch (op)
        {
        case MX_NOP:
            break;

        case MX_HALT:
            vm->outc = 0;
            vm->status = MX_OK;
            return true;

        case MX_MUL8U:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.u32 = (uint16_t)((uint8_t)a.u32 * (uint8_t)b.u32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_MUL16U:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.u32 = (uint32_t)((uint16_t)a.u32 * (uint16_t)b.u32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_MUL16S:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.i32 = (int32_t)((int16_t)a.u32 * (int16_t)b.u32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_DIV16U: {
            uint16_t divisor;
            uint16_t quotient;
            uint16_t remainder;

            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            divisor = (uint16_t)b.u32;
            if (divisor == 0u)
            {
                vm->status = MX_ERR_NUMERIC;
                return false;
            }
            quotient = (uint16_t)(a.u32 / divisor);
            remainder = (uint16_t)(a.u32 % divisor);
            a.u32 = (uint32_t)quotient | ((uint32_t)remainder << 16);
            if (!mx_push(vm, a))
                return false;
            break;
        }

        case MX_SQRT32U:
            if (!mx_pop(vm, &a))
                return false;
            a.u32 = (uint16_t)sqrtf((float)a.u32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_RET: {
            uint8_t count;
            uint8_t i;

            if (!mx_read_u8(vm, &count))
                return false;
            if (count > vm->sp || count > vm->hdr.out_words || count > MX_MAX_OUT)
            {
                vm->status = MX_ERR_PROGRAM;
                return false;
            }

            for (i = 0; i < count; ++i)
                vm->out[i] = vm->stack[vm->sp - count + i];
            vm->outc = count;
            vm->status = MX_OK;
            return true;
        }

        case MX_PUSHF: {
            uint32_t raw;

            if (!mx_read_u32(vm, &raw))
                return false;
            a.u32 = raw;
            if (!mx_push(vm, a))
                return false;
            break;
        }

        case MX_PUSHI: {
            uint32_t raw;

            if (!mx_read_u32(vm, &raw))
                return false;
            a.u32 = raw;
            if (!mx_push(vm, a))
                return false;
            break;
        }

        case MX_LDS: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 1))
                return false;
            if (!mx_push(vm, vm->locals[idx]))
                return false;
            break;
        }

        case MX_STS: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 1) || !mx_pop(vm, &a))
                return false;
            vm->locals[idx] = a;
            break;
        }

        case MX_LDV2: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 2))
                return false;
            if (!mx_push(vm, vm->locals[idx + 0]) || !mx_push(vm, vm->locals[idx + 1]))
                return false;
            break;
        }

        case MX_LDV3: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 3))
                return false;
            if (!mx_push(vm, vm->locals[idx + 0]) ||
                !mx_push(vm, vm->locals[idx + 1]) ||
                !mx_push(vm, vm->locals[idx + 2]))
                return false;
            break;
        }

        case MX_LDD: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 2))
                return false;
            if (!mx_push(vm, vm->locals[idx + 0]) || !mx_push(vm, vm->locals[idx + 1]))
                return false;
            break;
        }

        case MX_STD: {
            uint8_t idx;

            if (!mx_read_u8(vm, &idx) || !mx_check_local(vm, idx, 2))
                return false;
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            vm->locals[idx + 0] = a;
            vm->locals[idx + 1] = b;
            break;
        }

        case MX_DUP:
            if (vm->sp == 0)
            {
                vm->status = MX_ERR_STACK_UDF;
                return false;
            }
            if (!mx_push(vm, vm->stack[vm->sp - 1]))
                return false;
            break;

        case MX_DROP:
            if (!mx_pop(vm, &a))
                return false;
            break;

        case MX_SWAP:
            if (!mx_pop(vm, &a) || !mx_pop(vm, &b))
                return false;
            if (!mx_push(vm, a) || !mx_push(vm, b))
                return false;
            break;

        case MX_FADD:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = a.f32 + b.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FSUB:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = a.f32 - b.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FMUL:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = a.f32 * b.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FDIV:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = a.f32 / b.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FMADD:
            if (!mx_pop(vm, &c) || !mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = fmaf(a.f32, b.f32, c.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FNEG:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = -a.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FABS:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = fabsf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FSQRT:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = sqrtf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FSIN:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = sinf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FCOS:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = cosf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FATAN2:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = atan2f(a.f32, b.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FPOW:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = powf(a.f32, b.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FLOG:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = logf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FEXP:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = expf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FTOI:
            if (!mx_pop(vm, &a))
                return false;
            a.i32 = (int32_t)a.f32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_ITOF:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = (float)a.i32;
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FMIN:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = fminf(a.f32, b.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FMAX:
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a))
                return false;
            a.f32 = fmaxf(a.f32, b.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FROUND:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = roundf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_FTRUNC:
            if (!mx_pop(vm, &a))
                return false;
            a.f32 = truncf(a.f32);
            if (!mx_push(vm, a))
                return false;
            break;

        case MX_DADD:
        case MX_DMUL:
        case MX_DDIV: {
            uint64_t ua;
            uint64_t ub;
            double da;
            double db;
            double dr;

            if (!mx_pop_u64(vm, &ub) || !mx_pop_u64(vm, &ua))
                return false;
            da = mx_bits_to_d64(ua);
            db = mx_bits_to_d64(ub);

            switch (op)
            {
            case MX_DADD:
                dr = da + db;
                break;
            case MX_DMUL:
                dr = da * db;
                break;
            default:
                dr = da / db;
                break;
            }

            if (!mx_push_u64(vm, mx_d64_to_bits(dr)))
                return false;
            break;
        }

        case MX_M3V3L: {
            uint8_t mb;
            uint8_t vb;
            mx_word_t rx;
            mx_word_t ry;
            mx_word_t rz;

            if (!mx_read_u8(vm, &mb) || !mx_read_u8(vm, &vb))
                return false;
            if (!mx_check_local(vm, mb, 9) || !mx_check_local(vm, vb, 3))
                return false;

            rx.f32 = fmaf(vm->locals[mb + 0].f32, vm->locals[vb + 0].f32,
                          fmaf(vm->locals[mb + 1].f32, vm->locals[vb + 1].f32,
                               vm->locals[mb + 2].f32 * vm->locals[vb + 2].f32));
            ry.f32 = fmaf(vm->locals[mb + 3].f32, vm->locals[vb + 0].f32,
                          fmaf(vm->locals[mb + 4].f32, vm->locals[vb + 1].f32,
                               vm->locals[mb + 5].f32 * vm->locals[vb + 2].f32));
            rz.f32 = fmaf(vm->locals[mb + 6].f32, vm->locals[vb + 0].f32,
                          fmaf(vm->locals[mb + 7].f32, vm->locals[vb + 1].f32,
                               vm->locals[mb + 8].f32 * vm->locals[vb + 2].f32));

            if (!mx_push(vm, rx) || !mx_push(vm, ry) || !mx_push(vm, rz))
                return false;
            break;
        }

        case MX_A2P2L: {
            uint8_t ab;
            uint8_t pb;
            float out[2];

            if (!mx_read_u8(vm, &ab) || !mx_read_u8(vm, &pb))
                return false;
            if (!mx_check_local(vm, ab, 6) || !mx_check_local(vm, pb, 2))
                return false;

            mx_transform_point2(vm->locals[ab + 0].f32, vm->locals[ab + 1].f32, vm->locals[ab + 2].f32,
                                vm->locals[ab + 3].f32, vm->locals[ab + 4].f32, vm->locals[ab + 5].f32,
                                vm->locals[pb + 0].f32, vm->locals[pb + 1].f32, out);
            a.f32 = out[0];
            b.f32 = out[1];
            if (!mx_push(vm, a) || !mx_push(vm, b))
                return false;
            break;
        }

        case MX_SPR2L: {
            uint8_t ab;
            uint8_t sb;
            uint8_t flags;

            if (!mx_read_u8(vm, &ab) || !mx_read_u8(vm, &sb) || !mx_read_u8(vm, &flags))
                return false;
            if (!mx_exec_spr2l(vm, ab, sb, flags))
                return false;
            break;
        }

        case MX_JMP: {
            int8_t rel;

            if (!mx_read_i8(vm, &rel) || !mx_check_jump_target(vm, rel))
                return false;
            break;
        }

        case MX_JZ:
        case MX_JNZ: {
            int8_t rel;

            if (!mx_read_i8(vm, &rel) || !mx_pop(vm, &a))
                return false;
            if (((op == MX_JZ) && a.f32 == 0.0f) || ((op == MX_JNZ) && a.f32 != 0.0f))
                if (!mx_check_jump_target(vm, rel))
                    return false;
            break;
        }

        default:
            vm->status = MX_ERR_UNSUPPORTED;
            return false;
        }
    }

    vm->status = MX_ERR_PROGRAM;
    return false;
}

bool mathvm_api_op(void)
{
    mx_vm_t vm;
    uint8_t frame[MX_MAX_FRAME];
    uint16_t frame_len = mx_xstack_pull_frame(frame, sizeof(frame));

    if (frame_len == 0)
        return mx_return_status(MX_ERR_HEADER, 0);
    if (!mx_load_frame(&vm, frame, frame_len))
        return mx_return_status((mx_status_t)vm.status, 0);
    if (!mx_exec(&vm))
        return mx_return_status((mx_status_t)vm.status, 0);
    if (!mx_pack_i16_output(&vm))
        return mx_return_status((mx_status_t)vm.status, 0);
    if (!mx_xstack_push_output(&vm))
        return mx_return_status(MX_ERR_PROGRAM, 0);

    return mx_return_status(MX_OK, vm.outc);
}
