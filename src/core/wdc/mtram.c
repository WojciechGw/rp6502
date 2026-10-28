/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/wdc/mtram.h"
#include "core/sys/random.h"
#include "core/wdc/phi2.h"
#include "core/wdc/sram.h"
#include <string.h>

_Static_assert(MTRAM_QUEUE_LEN == 256, "the uint8_t queue head wraps the ring");

bool mtram_on;

/* Rows of one block rather than one 448 KB array, for the same reason xram.c
 * splits its 64 KB: a single large array crashes the debugger. */
static uint8_t mtram_ext[MTRAM_BLOCKS - MTRAM_SLOTS][MTRAM_BLOCK_SIZE];

static uint8_t *mtram_slot[MTRAM_SLOTS];
static uint8_t mtram_active_map[MTRAM_SLOTS];
static uint8_t mtram_task_map[MTRAM_TASKS][MTRAM_SLOTS];
static bool mtram_gate;
static bool mtram_status_hidden;
static uint8_t mtram_active_task;
static uint8_t mtram_edit_task;
static uint8_t mtram_knock_pos;
static uint8_t mtram_resets;
static bool mtram_resb_low;
static uint32_t mtram_cycle;
static uint32_t mtram_temp_cycle;

typedef struct
{
    uint32_t due;
    uint16_t addr;
    uint8_t data;
} mtram_event_t;

static mtram_event_t mtram_queue[MTRAM_QUEUE_LEN];
static uint8_t mtram_queue_head;
static uint16_t mtram_queue_count;

static uint8_t mtram_temp = 50;
static uint32_t mtram_delay;

static const uint8_t mtram_knock_prefix[8] = {'#', '!', 'R', 'P', '6', '5', '0', '2'};

void mtram_set_enabled(bool on)
{
    mtram_on = on;
}

/* The module's encoding: degrees x 2, rounded, saturating at both ends. */
void mtram_set_temperature(double celsius)
{
    double half_steps = celsius * 2.0 + 0.5;
    if (half_steps < 0.0)
        mtram_temp = 0;
    else if (half_steps > 255.0)
        mtram_temp = 255;
    else
        mtram_temp = (uint8_t)half_steps;
}

void mtram_set_delay(uint32_t cycles)
{
    mtram_delay = cycles;
}

static uint8_t *mtram_block(uint8_t block)
{
    if (block < MTRAM_SLOTS)
        return &sram[(uint32_t)block << MTRAM_BLOCK_SHIFT];
    return mtram_ext[block - MTRAM_SLOTS];
}

static void mtram_map_slot(uint8_t slot, uint8_t block)
{
    mtram_active_map[slot] = block;
    mtram_slot[slot] = mtram_block(block);
}

static void mtram_reset_all_maps(void)
{
    for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
    {
        for (uint8_t t = 0; t < MTRAM_TASKS; t++)
            mtram_task_map[t][s] = s;
        mtram_map_slot(s, s);
    }
}

static void mtram_publish_temperature(void)
{
    sram[MTRAM_REG_TEMP_MIN] = mtram_temp;
    sram[MTRAM_REG_TEMP_MAX] = mtram_temp;
}

/* The first '+', 'T' or 'C' since the last reset. Until then $FED5-$FED7 are
 * the program's RAM, so a loader's verify finds what its write left there. */
static void mtram_status_show(void)
{
    if (!mtram_status_hidden)
        return;
    mtram_status_hidden = false;
    sram[MTRAM_REG_RESETS] = mtram_resets;
    mtram_publish_temperature();
}

static void mtram_knock_feed(uint8_t data)
{
    if (mtram_knock_pos < sizeof mtram_knock_prefix)
    {
        if (data == mtram_knock_prefix[mtram_knock_pos])
            mtram_knock_pos++;
        else
            mtram_knock_pos = data == mtram_knock_prefix[0] ? 1 : 0;
        return;
    }
    mtram_knock_pos = 0;
    switch (data)
    {
    case '+':
        mtram_gate = true;
        mtram_status_show();
        break;
    case '-':
        mtram_reset_all_maps();
        mtram_gate = false;
        break;
    case 'T':
    case 'C':
        mtram_status_show();
        mtram_publish_temperature();
        break;
    }
}

static void mtram_control(uint16_t addr, uint8_t data)
{
    if (addr == MTRAM_REG_KNOCK)
    {
        mtram_knock_feed(data);
        return;
    }
    if (!mtram_gate)
        return;
    if (addr == MTRAM_REG_SWITCH)
    {
        uint8_t next = data & 0x0F;
        for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
            mtram_map_slot(s, mtram_task_map[next][s]);
        mtram_active_task = next;
        sram[MTRAM_REG_SWAPDONE] = 1;
    }
    else if (addr == MTRAM_REG_EDIT)
        mtram_edit_task = data & 0x0F;
    else if (addr >= MTRAM_REG_BANK_BASE && addr <= MTRAM_REG_BANK_LAST)
    {
        /* Block 31 backs the pinned slot, and no other slot may alias it. */
        if (data == MTRAM_PINNED_SLOT)
            return;
        uint8_t slot = (uint8_t)(addr - MTRAM_REG_BANK_BASE);
        mtram_task_map[mtram_edit_task][slot] = data;
        if (mtram_edit_task == mtram_active_task)
            mtram_map_slot(slot, data);
    }
}

static void mtram_queue_pop(void)
{
    const mtram_event_t *e = &mtram_queue[mtram_queue_head++];
    mtram_queue_count--;
    mtram_control(e->addr, e->data);
}

static void mtram_queue_drain(void)
{
    while (mtram_queue_count)
        mtram_queue_pop();
}

/* A full queue gives up its oldest event early rather than losing one. */
static void mtram_post(uint16_t addr, uint8_t data)
{
    if (!mtram_delay)
    {
        mtram_control(addr, data);
        return;
    }
    if (mtram_queue_count == MTRAM_QUEUE_LEN)
        mtram_queue_pop();
    mtram_event_t *e = &mtram_queue[(uint8_t)(mtram_queue_head + mtram_queue_count)];
    e->due = mtram_cycle + mtram_delay;
    e->addr = addr;
    e->data = data;
    mtram_queue_count++;
}

void mtram_init_memory(bool random, uint8_t value, uint32_t seed)
{
    if (!mtram_on)
        return;
    if (random)
    {
        /* Three times the golden-ratio salt of sram_init, so the three fills
         * of one seed start apart. */
        uint32_t state = seed ^ 0xDAA66D2Bu;
        sys_random_fill(mtram_ext, sizeof mtram_ext, &state);
    }
    else
        memset(mtram_ext, value, sizeof mtram_ext);
    mtram_queue_count = 0;
    mtram_reset_all_maps();
    mtram_gate = false;
    mtram_status_hidden = true;
    mtram_active_task = 0;
    mtram_edit_task = 0;
    mtram_knock_pos = 0;
    mtram_temp_cycle = mtram_cycle;
}

/* A control event takes effect after the cycle that is due, so with a delay
 * of N the N cycles after the write still see the old maps. */
void mtram_tick(uint16_t addr, bool read, uint8_t *data)
{
    uint8_t *p = &mtram_slot[addr >> MTRAM_BLOCK_SHIFT][addr & (MTRAM_BLOCK_SIZE - 1)];
    ++mtram_cycle;
    if (!read)
    {
        *p = *data;
        if (addr >= MTRAM_REG_EDIT && addr <= MTRAM_REG_KNOCK)
            mtram_post(addr, *data);
    }
    else if (addr <= SRAM_MMAP_HI)
        *data = *p;
    while (mtram_queue_count &&
           (int32_t)(mtram_cycle - mtram_queue[mtram_queue_head].due) >= 0)
        mtram_queue_pop();
}

uint8_t *mtram_view(uint16_t addr)
{
    return &mtram_slot[addr >> MTRAM_BLOCK_SHIFT][addr & (MTRAM_BLOCK_SIZE - 1)];
}

/* RESB is asserted again on every stop, so only the falling edge counts, as
 * the module's debounced tap does. Memory survives and no byte is written;
 * only the maps reset and the status bytes hide. The maps reset with the
 * module off too, so a savestate always carries valid ones. */
void mtram_resb(bool low)
{
    bool edge = low && !mtram_resb_low;
    mtram_resb_low = low;
    if (!edge)
        return;
    mtram_queue_drain();
    mtram_reset_all_maps();
    mtram_gate = false;
    mtram_status_hidden = true;
    mtram_active_task = 0;
    mtram_edit_task = 0;
    mtram_knock_pos = 0;
    if (mtram_on)
        ++mtram_resets;
}

void mtram_power_on(void)
{
    mtram_resets = 0;
    mtram_resb_low = false;
}

/* The module republishes its temperature once a second, which puts back what
 * a program wrote over it. */
void mtram_task(void)
{
    if (!mtram_on)
        return;
    if (mtram_cycle - mtram_temp_cycle >= (uint32_t)phi2_get_khz_run() * 1000u)
    {
        mtram_temp_cycle = mtram_cycle;
        if (!mtram_status_hidden)
            mtram_publish_temperature();
    }
}

void mtram_dbg_get(mtram_dbg_t *out)
{
    out->gate = mtram_gate;
    out->status_hidden = mtram_status_hidden;
    out->active_task = mtram_active_task;
    out->edit_task = mtram_edit_task;
    out->knock_pos = mtram_knock_pos;
    out->resets = mtram_resets;
    out->temp = mtram_temp;
    out->delay = mtram_delay;
    out->queued = mtram_queue_count;
    memcpy(out->active_map, mtram_active_map, sizeof out->active_map);
    memcpy(out->task_map, mtram_task_map, sizeof out->task_map);
}

const uint8_t *mtram_dbg_block(uint8_t block)
{
    return mtram_block(block);
}

void mtram_sst_save(sst_cursor_t *c, unsigned flags)
{
    (void)flags;
    sst_put(c, mtram_ext, sizeof mtram_ext);
    sst_put(c, mtram_active_map, sizeof mtram_active_map);
    sst_put(c, mtram_task_map, sizeof mtram_task_map);
    sst_put_bool(c, mtram_gate);
    sst_put_bool(c, mtram_status_hidden);
    sst_put_u8(c, mtram_active_task);
    sst_put_u8(c, mtram_edit_task);
    sst_put_u8(c, mtram_knock_pos);
    sst_put_u8(c, mtram_resets);
    sst_put_bool(c, mtram_resb_low);
    sst_put_u32(c, mtram_cycle);
    sst_put_u32(c, mtram_temp_cycle);
    sst_put_u8(c, mtram_queue_head);
    sst_put_u16(c, mtram_queue_count);
    for (unsigned i = 0; i < MTRAM_QUEUE_LEN; i++)
    {
        sst_put_u32(c, mtram_queue[i].due);
        sst_put_u16(c, mtram_queue[i].addr);
        sst_put_u8(c, mtram_queue[i].data);
    }
}

static bool mtram_map_valid(const uint8_t *map)
{
    for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
        if ((s == MTRAM_PINNED_SLOT) != (map[s] == MTRAM_PINNED_SLOT))
            return false;
    return true;
}

/* The blocks go straight into mtram_ext, as sram_sst_load does with sram[]; a
 * refused load is rolled back by sst.c. */
bool mtram_sst_load(sst_cursor_t *c, unsigned flags)
{
    (void)flags;
    uint8_t active_map[MTRAM_SLOTS];
    uint8_t task_map[MTRAM_TASKS][MTRAM_SLOTS];
    mtram_event_t queue[MTRAM_QUEUE_LEN];
    sst_get(c, mtram_ext, sizeof mtram_ext);
    sst_get(c, active_map, sizeof active_map);
    sst_get(c, task_map, sizeof task_map);
    bool gate = sst_get_bool(c);
    bool status_hidden = sst_get_bool(c);
    uint8_t active_task = sst_get_u8(c);
    uint8_t edit_task = sst_get_u8(c);
    uint8_t knock_pos = sst_get_u8(c);
    uint8_t resets = sst_get_u8(c);
    bool resb_low = sst_get_bool(c);
    uint32_t cycle = sst_get_u32(c);
    uint32_t temp_cycle = sst_get_u32(c);
    uint8_t queue_head = sst_get_u8(c);
    uint16_t queue_count = sst_get_u16(c);
    for (unsigned i = 0; i < MTRAM_QUEUE_LEN; i++)
    {
        queue[i].due = sst_get_u32(c);
        queue[i].addr = sst_get_u16(c);
        queue[i].data = sst_get_u8(c);
    }
    if (!sst_ok(c) || !mtram_map_valid(active_map) ||
        active_task >= MTRAM_TASKS || edit_task >= MTRAM_TASKS ||
        knock_pos > sizeof mtram_knock_prefix || queue_count > MTRAM_QUEUE_LEN)
        return false;
    for (unsigned t = 0; t < MTRAM_TASKS; t++)
        if (!mtram_map_valid(task_map[t]))
            return false;
    for (unsigned i = 0; i < queue_count; i++)
    {
        uint16_t addr = queue[(uint8_t)(queue_head + i)].addr;
        if (addr < MTRAM_REG_EDIT || addr > MTRAM_REG_KNOCK)
            return false;
    }
    for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
        mtram_map_slot(s, active_map[s]);
    memcpy(mtram_task_map, task_map, sizeof mtram_task_map);
    mtram_gate = gate;
    mtram_status_hidden = status_hidden;
    mtram_active_task = active_task;
    mtram_edit_task = edit_task;
    mtram_knock_pos = knock_pos;
    mtram_resets = resets;
    mtram_resb_low = resb_low;
    mtram_cycle = cycle;
    mtram_temp_cycle = temp_cycle;
    mtram_queue_head = queue_head;
    mtram_queue_count = queue_count;
    memcpy(mtram_queue, queue, sizeof mtram_queue);
    return true;
}
