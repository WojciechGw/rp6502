/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The mtRAM model, driven one bus cycle at a time through sram_tick. The cases
 * follow the module firmware's own tests of its control plane.
 */

#include "core/wdc/mtram.h"
#include "core/wdc/sram.h"
#include "utest.h"
#include <string.h>

UTEST_MAIN();

#define SLOT16 0x8000

static void fresh(uint32_t delay)
{
    mtram_set_enabled(true);
    mtram_set_delay(delay);
    mtram_set_temperature(25.0);
    mtram_resb(false);
    memset(sram, 0, sizeof sram);
    mtram_init_memory(false, 0, 0);
}

static void wr(uint16_t addr, uint8_t data)
{
    sram_tick(addr, false, &data);
}

static uint8_t rd(uint16_t addr)
{
    uint8_t data = 0xA5;
    sram_tick(addr, true, &data);
    return data;
}

static void idle(int cycles)
{
    while (cycles-- > 0)
        rd(0x0000);
}

static void knock(char cmd)
{
    for (const char *p = "#!RP6502"; *p; p++)
        wr(MTRAM_REG_KNOCK, (uint8_t)*p);
    wr(MTRAM_REG_KNOCK, (uint8_t)cmd);
}

static void map(uint8_t slot, uint8_t block)
{
    wr((uint16_t)(MTRAM_REG_BANK_BASE + slot), block);
}

static mtram_dbg_t state(void)
{
    mtram_dbg_t st;
    mtram_dbg_get(&st);
    return st;
}

UTEST(mtram, the_identity_map_is_the_plain_machine)
{
    fresh(0);
    wr(0x1234, 0x55);
    ASSERT_EQ(sram[0x1234], 0x55);
    ASSERT_EQ(rd(0x1234), 0x55);
    ASSERT_EQ(*sram_view(0x1234), 0x55);
}

UTEST(mtram, a_bank_write_waits_for_the_gate)
{
    fresh(0);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
}

UTEST(mtram, a_slot_maps_to_a_block)
{
    fresh(0);
    knock('+');
    map(16, 100);
    ASSERT_EQ(sram[MTRAM_REG_BANK_BASE + 16], 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0x00);
    ASSERT_EQ(rd(SLOT16), 0xAA);
    ASSERT_EQ(*sram_view(SLOT16), 0xAA);
    map(16, 16);
    ASSERT_EQ(rd(SLOT16), 0x00);
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0xAA);
}

UTEST(mtram, a_low_block_aliases_the_plain_ram)
{
    fresh(0);
    sram[0x0800] = 0x42;
    knock('+');
    map(16, 1);
    ASSERT_EQ(rd(SLOT16), 0x42);
}

UTEST(mtram, block_31_is_refused)
{
    fresh(0);
    knock('+');
    map(5, 31);
    wr(0x2800, 0x77);
    ASSERT_EQ(sram[0x2800], 0x77);
}

UTEST(mtram, a_knock_restarts_on_a_hash)
{
    fresh(0);
    for (const char *p = "#!RP#!RP6502+"; *p; p++)
        wr(MTRAM_REG_KNOCK, (uint8_t)*p);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0x00);
}

UTEST(mtram, a_wrong_knock_leaves_the_gate_shut)
{
    fresh(0);
    for (const char *p = "#!RP6503+"; *p; p++)
        wr(MTRAM_REG_KNOCK, (uint8_t)*p);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
}

UTEST(mtram, a_minus_knock_resets_the_maps_and_shuts_the_gate)
{
    fresh(0);
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);
    knock('-');
    ASSERT_EQ(rd(SLOT16), 0x00);
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0x00);
}

UTEST(mtram, tasks_have_their_own_maps)
{
    fresh(0);
    knock('+');
    wr(MTRAM_REG_EDIT, 1);
    map(16, 120);
    wr(SLOT16, 0x11);
    ASSERT_EQ(sram[SLOT16], 0x11);

    wr(MTRAM_REG_SWAPDONE, 0);
    wr(MTRAM_REG_SWITCH, 1);
    ASSERT_EQ(sram[MTRAM_REG_SWAPDONE], 1);
    ASSERT_EQ(rd(SLOT16), 0x00);
    wr(SLOT16, 0x22);

    wr(MTRAM_REG_SWITCH, 0);
    ASSERT_EQ(rd(SLOT16), 0x11);
    wr(MTRAM_REG_SWITCH, 1);
    ASSERT_EQ(rd(SLOT16), 0x22);
}

UTEST(mtram, the_pinned_slot_is_shared)
{
    fresh(0);
    knock('+');
    wr(0xF900, 0x5A);
    wr(MTRAM_REG_SWITCH, 7);
    ASSERT_EQ(rd(0xF900), 0x5A);
}

UTEST(mtram, resb_resets_on_its_falling_edge_only)
{
    fresh(0);
    uint8_t before = state().resets;
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);

    mtram_resb(true);
    ASSERT_EQ(state().resets, (uint8_t)(before + 1));
    ASSERT_EQ(rd(SLOT16), 0x00);
    mtram_resb(true);
    ASSERT_EQ(state().resets, (uint8_t)(before + 1));
    mtram_resb(false);

    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0x00);
    knock('+');
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0xAA);

    mtram_resb(true);
    ASSERT_EQ(state().resets, (uint8_t)(before + 2));
    mtram_resb(false);
}

/* The module firmware's test_status_hidden(): a program that never knocks
 * keeps $FED5 across the loader's resets. */
UTEST(mtram, the_status_bytes_wait_for_a_knock)
{
    fresh(0);
    mtram_power_on();
    ASSERT_TRUE(state().status_hidden);
    for (int i = 0; i < 10; i++)
    {
        mtram_resb(true);
        mtram_resb(false);
    }
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 0);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0);
    wr(MTRAM_REG_RESETS, 0xC3);
    for (int i = 0; i < 100; i++)
    {
        mtram_resb(true);
        mtram_resb(false);
    }
    ASSERT_EQ(state().resets, 110);
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 0xC3);

    knock('B');
    ASSERT_TRUE(state().status_hidden);
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 0xC3);

    knock('+');
    ASSERT_FALSE(state().status_hidden);
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 110);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 50);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MAX], 50);

    wr(MTRAM_REG_RESETS, 0x11);
    knock('+');
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 0x11);
    knock('-');
    ASSERT_FALSE(state().status_hidden);

    mtram_resb(true);
    ASSERT_TRUE(state().status_hidden);
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 0x11);
    mtram_resb(false);

    knock('T');
    ASSERT_FALSE(state().status_hidden);
    ASSERT_EQ(sram[MTRAM_REG_RESETS], 111);
}

UTEST(mtram, no_delay_takes_effect_on_the_next_cycle)
{
    fresh(0);
    sram[SLOT16] = 0x33;
    knock('+');
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0x00);
}

UTEST(mtram, a_delay_keeps_the_old_map_for_that_many_cycles)
{
    fresh(5);
    sram[SLOT16] = 0x33;
    knock('+');
    idle(6);
    map(16, 100);
    for (int i = 0; i < 5; i++)
        ASSERT_EQ(rd(SLOT16), 0x33);
    ASSERT_EQ(rd(SLOT16), 0x00);
}

UTEST(mtram, a_delay_keeps_the_order)
{
    fresh(3);
    knock('+');
    wr(MTRAM_REG_EDIT, 1);
    map(16, 100);
    wr(MTRAM_REG_EDIT, 0);
    map(16, 101);
    idle(4);
    wr(SLOT16, 0x01);
    wr(MTRAM_REG_SWITCH, 1);
    idle(4);
    wr(SLOT16, 0x02);
    wr(MTRAM_REG_SWITCH, 0);
    idle(4);
    ASSERT_EQ(rd(SLOT16), 0x01);
    wr(MTRAM_REG_SWITCH, 1);
    idle(4);
    ASSERT_EQ(rd(SLOT16), 0x02);
}

UTEST(mtram, a_full_queue_loses_nothing)
{
    fresh(1000);
    knock('+');
    for (int i = 0; i < 300; i++)
        wr(MTRAM_REG_EDIT, (uint8_t)(i & 1));
    wr(MTRAM_REG_EDIT, 3);
    map(16, 100);
    idle(1001);
    wr(MTRAM_REG_SWITCH, 3);
    idle(1001);
    wr(SLOT16, 0x99);
    wr(MTRAM_REG_SWITCH, 0);
    idle(1001);
    ASSERT_EQ(rd(SLOT16), 0x00);
    wr(MTRAM_REG_SWITCH, 3);
    idle(1001);
    ASSERT_EQ(rd(SLOT16), 0x99);
}

UTEST(mtram, a_reset_flushes_the_queue_first)
{
    fresh(1000);
    wr(MTRAM_REG_SWAPDONE, 0);
    knock('+');
    wr(MTRAM_REG_SWITCH, 2);
    ASSERT_EQ(sram[MTRAM_REG_SWAPDONE], 0);
    mtram_resb(true);
    ASSERT_EQ(sram[MTRAM_REG_SWAPDONE], 1);
    mtram_resb(false);
}

UTEST(mtram, the_temperature_is_published_and_restored)
{
    mtram_set_enabled(true);
    mtram_set_delay(0);
    mtram_set_temperature(45.5);
    memset(sram, 0, sizeof sram);
    mtram_init_memory(false, 0, 0);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MAX], 0);
    knock('T');
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0x5B);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MAX], 0x5B);
    wr(MTRAM_REG_TEMP_MIN, 0);
    wr(MTRAM_REG_TEMP_MAX, 0);
    knock('T');
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0x5B);
    wr(MTRAM_REG_TEMP_MAX, 0);
    knock('C');
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MAX], 0x5B);
}

UTEST(mtram, the_temperature_saturates)
{
    mtram_set_enabled(true);
    mtram_set_temperature(200.0);
    mtram_init_memory(false, 0, 0);
    knock('T');
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0xFF);
    mtram_set_temperature(-3.0);
    mtram_init_memory(false, 0, 0);
    knock('T');
    ASSERT_EQ(sram[MTRAM_REG_TEMP_MIN], 0x00);
}

UTEST(mtram, a_savestate_carries_the_maps)
{
    static uint8_t blob[MTRAM_SST_SIZE];
    fresh(0);
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);

    sst_cursor_t c = {blob, blob + sizeof blob, false};
    mtram_sst_save(&c, 0);
    ASSERT_TRUE(sst_ok(&c));
    ASSERT_TRUE(c.at == c.end);

    knock('-');
    wr(SLOT16, 0x00);
    ASSERT_EQ(rd(SLOT16), 0x00);
    mtram_resb(true);
    mtram_resb(false);
    ASSERT_TRUE(state().status_hidden);

    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_TRUE(mtram_sst_load(&c, 0));
    ASSERT_FALSE(state().status_hidden);
    ASSERT_EQ(rd(SLOT16), 0xAA);
    map(16, 101);
    ASSERT_NE(rd(SLOT16), 0xAA);
}

UTEST(mtram, a_savestate_that_aliases_block_31_is_refused)
{
    static uint8_t blob[MTRAM_SST_SIZE];
    fresh(0);
    sst_cursor_t c = {blob, blob + sizeof blob, false};
    mtram_sst_save(&c, 0);
    blob[(MTRAM_BLOCKS - MTRAM_SLOTS) * MTRAM_BLOCK_SIZE + 3] = MTRAM_PINNED_SLOT;
    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_FALSE(mtram_sst_load(&c, 0));
}

UTEST(mtram, a_power_on_counts_resets_from_zero)
{
    fresh(0);
    mtram_resb(true);
    mtram_resb(false);
    mtram_resb(true);
    mtram_power_on();
    mtram_resb(true);
    ASSERT_EQ(state().resets, 1);
    mtram_resb(false);
}

UTEST(mtram, the_debugger_sees_the_state)
{
    fresh(0);
    knock('+');
    wr(MTRAM_REG_EDIT, 2);
    map(16, 100);
    wr(MTRAM_REG_SWITCH, 2);
    wr(SLOT16, 0xAA);
    for (const char *p = "#!RP"; *p; p++)
        wr(MTRAM_REG_KNOCK, (uint8_t)*p);

    mtram_dbg_t st;
    mtram_dbg_get(&st);
    ASSERT_TRUE(st.gate);
    ASSERT_EQ(st.active_task, 2);
    ASSERT_EQ(st.edit_task, 2);
    ASSERT_EQ(st.knock_pos, 4);
    ASSERT_EQ(st.temp, 50);
    ASSERT_EQ(st.active_map[16], 100);
    ASSERT_EQ(st.task_map[2][16], 100);
    ASSERT_EQ(st.task_map[0][16], 16);
    ASSERT_EQ(mtram_dbg_block(100)[0], 0xAA);
    ASSERT_TRUE(mtram_dbg_block(16) == &sram[SLOT16]);
}

UTEST(mtram, off_is_the_plain_machine)
{
    fresh(0);
    mtram_set_enabled(false);
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
    wr(0xFF10, 0x12);
    ASSERT_EQ(sram[0xFF10], 0x12);
    ASSERT_EQ(rd(0xFF10), 0xA5);
    ASSERT_TRUE(sram_view(SLOT16) == &sram[SLOT16]);
}
