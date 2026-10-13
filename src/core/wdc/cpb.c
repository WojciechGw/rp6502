/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/wdc/cpb.h"
#include "core/sys/random.h"
#include "core/wdc/phi2.h"
#include "core/wdc/sram.h"
#include <string.h>

_Static_assert(CPB_QUEUE_LEN == 256, "the uint8_t queue head wraps the ring");

bool cpb_on;

/* Rows of one block rather than one 448 KB array, for the same reason xram.c
 * splits its 64 KB: a single large array crashes the debugger. */
static uint8_t cpb_ext[CPB_BLOCKS - CPB_SLOTS][CPB_BLOCK_SIZE];

static uint8_t *cpb_slot[CPB_SLOTS];
static uint8_t cpb_active_map[CPB_SLOTS];
static uint8_t cpb_task_map[CPB_TASKS][CPB_SLOTS];
static bool cpb_gate;
static uint8_t cpb_active_task;
static uint8_t cpb_edit_task;
static uint8_t cpb_knock_pos;
static uint8_t cpb_resets;
static bool cpb_resb_low;
static uint32_t cpb_cycle;
static uint32_t cpb_temp_cycle;

typedef struct
{
    uint32_t due;
    uint16_t addr;
    uint8_t data;
} cpb_event_t;

static cpb_event_t cpb_queue[CPB_QUEUE_LEN];
static uint8_t cpb_queue_head;
static uint16_t cpb_queue_count;

static uint8_t cpb_temp = 50;
static uint32_t cpb_delay;

static const uint8_t cpb_knock_prefix[8] = {'#', '!', 'R', 'P', '6', '5', '0', '2'};

void cpb_set_enabled(bool on)
{
    cpb_on = on;
}

/* The module's encoding: degrees x 2, rounded, saturating at both ends. */
void cpb_set_temperature(double celsius)
{
    double half_steps = celsius * 2.0 + 0.5;
    if (half_steps < 0.0)
        cpb_temp = 0;
    else if (half_steps > 255.0)
        cpb_temp = 255;
    else
        cpb_temp = (uint8_t)half_steps;
}

void cpb_set_delay(uint32_t cycles)
{
    cpb_delay = cycles;
}

static uint8_t *cpb_block(uint8_t block)
{
    if (block < CPB_SLOTS)
        return &sram[(uint32_t)block << CPB_BLOCK_SHIFT];
    return cpb_ext[block - CPB_SLOTS];
}

static void cpb_map_slot(uint8_t slot, uint8_t block)
{
    cpb_active_map[slot] = block;
    cpb_slot[slot] = cpb_block(block);
}

static void cpb_reset_all_maps(void)
{
    for (uint8_t s = 0; s < CPB_SLOTS; s++)
    {
        for (uint8_t t = 0; t < CPB_TASKS; t++)
            cpb_task_map[t][s] = s;
        cpb_map_slot(s, s);
    }
}

static void cpb_publish_temperature(void)
{
    sram[CPB_REG_TEMP_MIN] = cpb_temp;
    sram[CPB_REG_TEMP_MAX] = cpb_temp;
}

static void cpb_knock_feed(uint8_t data)
{
    if (cpb_knock_pos < sizeof cpb_knock_prefix)
    {
        if (data == cpb_knock_prefix[cpb_knock_pos])
            cpb_knock_pos++;
        else
            cpb_knock_pos = data == cpb_knock_prefix[0] ? 1 : 0;
        return;
    }
    cpb_knock_pos = 0;
    switch (data)
    {
    case '+':
        cpb_gate = true;
        break;
    case '-':
        cpb_reset_all_maps();
        cpb_gate = false;
        break;
    case 'T':
    case 'C':
        cpb_publish_temperature();
        break;
    }
}

static void cpb_control(uint16_t addr, uint8_t data)
{
    if (addr == CPB_REG_KNOCK)
    {
        cpb_knock_feed(data);
        return;
    }
    if (!cpb_gate)
        return;
    if (addr == CPB_REG_SWITCH)
    {
        uint8_t next = data & 0x0F;
        for (uint8_t s = 0; s < CPB_SLOTS; s++)
            cpb_map_slot(s, cpb_task_map[next][s]);
        cpb_active_task = next;
        sram[CPB_REG_SWAPDONE] = 1;
    }
    else if (addr == CPB_REG_EDIT)
        cpb_edit_task = data & 0x0F;
    else if (addr >= CPB_REG_BANK_BASE && addr <= CPB_REG_BANK_LAST)
    {
        /* Block 31 backs the pinned slot, and no other slot may alias it. */
        if (data == CPB_PINNED_SLOT)
            return;
        uint8_t slot = (uint8_t)(addr - CPB_REG_BANK_BASE);
        cpb_task_map[cpb_edit_task][slot] = data;
        if (cpb_edit_task == cpb_active_task)
            cpb_map_slot(slot, data);
    }
}

static void cpb_queue_pop(void)
{
    const cpb_event_t *e = &cpb_queue[cpb_queue_head++];
    cpb_queue_count--;
    cpb_control(e->addr, e->data);
}

static void cpb_queue_drain(void)
{
    while (cpb_queue_count)
        cpb_queue_pop();
}

/* A full queue gives up its oldest event early rather than losing one. */
static void cpb_post(uint16_t addr, uint8_t data)
{
    if (!cpb_delay)
    {
        cpb_control(addr, data);
        return;
    }
    if (cpb_queue_count == CPB_QUEUE_LEN)
        cpb_queue_pop();
    cpb_event_t *e = &cpb_queue[(uint8_t)(cpb_queue_head + cpb_queue_count)];
    e->due = cpb_cycle + cpb_delay;
    e->addr = addr;
    e->data = data;
    cpb_queue_count++;
}

void cpb_init_memory(bool random, uint8_t value, uint32_t seed)
{
    if (!cpb_on)
        return;
    if (random)
    {
        /* Three times the golden-ratio salt of sram_init, so the three fills
         * of one seed start apart. */
        uint32_t state = seed ^ 0xDAA66D2Bu;
        sys_random_fill(cpb_ext, sizeof cpb_ext, &state);
    }
    else
        memset(cpb_ext, value, sizeof cpb_ext);
    cpb_queue_count = 0;
    cpb_reset_all_maps();
    cpb_gate = false;
    cpb_active_task = 0;
    cpb_edit_task = 0;
    cpb_knock_pos = 0;
    sram[CPB_REG_RESETS] = cpb_resets;
    cpb_publish_temperature();
    cpb_temp_cycle = cpb_cycle;
}

/* A control event takes effect after the cycle that is due, so with a delay
 * of N the N cycles after the write still see the old maps. */
void cpb_tick(uint16_t addr, bool read, uint8_t *data)
{
    uint8_t *p = &cpb_slot[addr >> CPB_BLOCK_SHIFT][addr & (CPB_BLOCK_SIZE - 1)];
    ++cpb_cycle;
    if (!read)
    {
        *p = *data;
        if (addr >= CPB_REG_EDIT && addr <= CPB_REG_KNOCK)
            cpb_post(addr, *data);
    }
    else if (addr <= SRAM_MMAP_HI)
        *data = *p;
    while (cpb_queue_count &&
           (int32_t)(cpb_cycle - cpb_queue[cpb_queue_head].due) >= 0)
        cpb_queue_pop();
}

uint8_t *cpb_view(uint16_t addr)
{
    return &cpb_slot[addr >> CPB_BLOCK_SHIFT][addr & (CPB_BLOCK_SIZE - 1)];
}

/* RESB is asserted again on every stop, so only the falling edge counts, as
 * the module's debounced tap does. Memory survives; only the maps reset. The
 * maps reset with the module off too, so a savestate always carries valid
 * ones. */
void cpb_resb(bool low)
{
    bool edge = low && !cpb_resb_low;
    cpb_resb_low = low;
    if (!edge)
        return;
    cpb_queue_drain();
    cpb_reset_all_maps();
    cpb_gate = false;
    cpb_active_task = 0;
    cpb_edit_task = 0;
    cpb_knock_pos = 0;
    if (cpb_on)
        sram[CPB_REG_RESETS] = ++cpb_resets;
}

void cpb_power_on(void)
{
    cpb_resets = 0;
    cpb_resb_low = false;
}

/* The module republishes its temperature once a second, which puts back what
 * a program wrote over it. */
void cpb_task(void)
{
    if (!cpb_on)
        return;
    if (cpb_cycle - cpb_temp_cycle >= (uint32_t)phi2_get_khz_run() * 1000u)
    {
        cpb_temp_cycle = cpb_cycle;
        cpb_publish_temperature();
    }
}

void cpb_dbg_get(cpb_dbg_t *out)
{
    out->gate = cpb_gate;
    out->active_task = cpb_active_task;
    out->edit_task = cpb_edit_task;
    out->knock_pos = cpb_knock_pos;
    out->resets = cpb_resets;
    out->temp = cpb_temp;
    out->delay = cpb_delay;
    out->queued = cpb_queue_count;
    memcpy(out->active_map, cpb_active_map, sizeof out->active_map);
    memcpy(out->task_map, cpb_task_map, sizeof out->task_map);
}

const uint8_t *cpb_dbg_block(uint8_t block)
{
    return cpb_block(block);
}

void cpb_sst_save(sst_cursor_t *c, unsigned flags)
{
    (void)flags;
    sst_put(c, cpb_ext, sizeof cpb_ext);
    sst_put(c, cpb_active_map, sizeof cpb_active_map);
    sst_put(c, cpb_task_map, sizeof cpb_task_map);
    sst_put_bool(c, cpb_gate);
    sst_put_u8(c, cpb_active_task);
    sst_put_u8(c, cpb_edit_task);
    sst_put_u8(c, cpb_knock_pos);
    sst_put_u8(c, cpb_resets);
    sst_put_bool(c, cpb_resb_low);
    sst_put_u32(c, cpb_cycle);
    sst_put_u32(c, cpb_temp_cycle);
    sst_put_u8(c, cpb_queue_head);
    sst_put_u16(c, cpb_queue_count);
    for (unsigned i = 0; i < CPB_QUEUE_LEN; i++)
    {
        sst_put_u32(c, cpb_queue[i].due);
        sst_put_u16(c, cpb_queue[i].addr);
        sst_put_u8(c, cpb_queue[i].data);
    }
}

static bool cpb_map_valid(const uint8_t *map)
{
    for (uint8_t s = 0; s < CPB_SLOTS; s++)
        if ((s == CPB_PINNED_SLOT) != (map[s] == CPB_PINNED_SLOT))
            return false;
    return true;
}

/* The blocks go straight into cpb_ext, as sram_sst_load does with sram[]; a
 * refused load is rolled back by sst.c. */
bool cpb_sst_load(sst_cursor_t *c, unsigned flags)
{
    (void)flags;
    uint8_t active_map[CPB_SLOTS];
    uint8_t task_map[CPB_TASKS][CPB_SLOTS];
    cpb_event_t queue[CPB_QUEUE_LEN];
    sst_get(c, cpb_ext, sizeof cpb_ext);
    sst_get(c, active_map, sizeof active_map);
    sst_get(c, task_map, sizeof task_map);
    bool gate = sst_get_bool(c);
    uint8_t active_task = sst_get_u8(c);
    uint8_t edit_task = sst_get_u8(c);
    uint8_t knock_pos = sst_get_u8(c);
    uint8_t resets = sst_get_u8(c);
    bool resb_low = sst_get_bool(c);
    uint32_t cycle = sst_get_u32(c);
    uint32_t temp_cycle = sst_get_u32(c);
    uint8_t queue_head = sst_get_u8(c);
    uint16_t queue_count = sst_get_u16(c);
    for (unsigned i = 0; i < CPB_QUEUE_LEN; i++)
    {
        queue[i].due = sst_get_u32(c);
        queue[i].addr = sst_get_u16(c);
        queue[i].data = sst_get_u8(c);
    }
    if (!sst_ok(c) || !cpb_map_valid(active_map) ||
        active_task >= CPB_TASKS || edit_task >= CPB_TASKS ||
        knock_pos > sizeof cpb_knock_prefix || queue_count > CPB_QUEUE_LEN)
        return false;
    for (unsigned t = 0; t < CPB_TASKS; t++)
        if (!cpb_map_valid(task_map[t]))
            return false;
    for (unsigned i = 0; i < queue_count; i++)
    {
        uint16_t addr = queue[(uint8_t)(queue_head + i)].addr;
        if (addr < CPB_REG_EDIT || addr > CPB_REG_KNOCK)
            return false;
    }
    for (uint8_t s = 0; s < CPB_SLOTS; s++)
        cpb_map_slot(s, active_map[s]);
    memcpy(cpb_task_map, task_map, sizeof cpb_task_map);
    cpb_gate = gate;
    cpb_active_task = active_task;
    cpb_edit_task = edit_task;
    cpb_knock_pos = knock_pos;
    cpb_resets = resets;
    cpb_resb_low = resb_low;
    cpb_cycle = cycle;
    cpb_temp_cycle = temp_cycle;
    cpb_queue_head = queue_head;
    cpb_queue_count = queue_count;
    memcpy(cpb_queue, queue, sizeof cpb_queue);
    return true;
}
