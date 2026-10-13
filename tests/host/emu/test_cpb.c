/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The CPB model, driven one bus cycle at a time through sram_tick. The cases
 * follow the module firmware's own tests of its control plane.
 */

#include "core/wdc/cpb.h"
#include "core/wdc/sram.h"
#include "utest.h"
#include <string.h>

UTEST_MAIN();

#define SLOT16 0x8000

static void fresh(uint32_t delay)
{
    cpb_set_enabled(true);
    cpb_set_delay(delay);
    cpb_set_temperature(25.0);
    cpb_resb(false);
    memset(sram, 0, sizeof sram);
    cpb_init_memory(false, 0, 0);
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
        wr(CPB_REG_KNOCK, (uint8_t)*p);
    wr(CPB_REG_KNOCK, (uint8_t)cmd);
}

static void map(uint8_t slot, uint8_t block)
{
    wr((uint16_t)(CPB_REG_BANK_BASE + slot), block);
}

UTEST(cpb, the_identity_map_is_the_plain_machine)
{
    fresh(0);
    wr(0x1234, 0x55);
    ASSERT_EQ(sram[0x1234], 0x55);
    ASSERT_EQ(rd(0x1234), 0x55);
    ASSERT_EQ(*sram_view(0x1234), 0x55);
}

UTEST(cpb, a_bank_write_waits_for_the_gate)
{
    fresh(0);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
}

UTEST(cpb, a_slot_maps_to_a_block)
{
    fresh(0);
    knock('+');
    map(16, 100);
    ASSERT_EQ(sram[CPB_REG_BANK_BASE + 16], 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0x00);
    ASSERT_EQ(rd(SLOT16), 0xAA);
    ASSERT_EQ(*sram_view(SLOT16), 0xAA);
    map(16, 16);
    ASSERT_EQ(rd(SLOT16), 0x00);
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0xAA);
}

UTEST(cpb, a_low_block_aliases_the_plain_ram)
{
    fresh(0);
    sram[0x0800] = 0x42;
    knock('+');
    map(16, 1);
    ASSERT_EQ(rd(SLOT16), 0x42);
}

UTEST(cpb, block_31_is_refused)
{
    fresh(0);
    knock('+');
    map(5, 31);
    wr(0x2800, 0x77);
    ASSERT_EQ(sram[0x2800], 0x77);
}

UTEST(cpb, a_knock_restarts_on_a_hash)
{
    fresh(0);
    for (const char *p = "#!RP#!RP6502+"; *p; p++)
        wr(CPB_REG_KNOCK, (uint8_t)*p);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0x00);
}

UTEST(cpb, a_wrong_knock_leaves_the_gate_shut)
{
    fresh(0);
    for (const char *p = "#!RP6503+"; *p; p++)
        wr(CPB_REG_KNOCK, (uint8_t)*p);
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
}

UTEST(cpb, a_minus_knock_resets_the_maps_and_shuts_the_gate)
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

UTEST(cpb, tasks_have_their_own_maps)
{
    fresh(0);
    knock('+');
    wr(CPB_REG_EDIT, 1);
    map(16, 120);
    wr(SLOT16, 0x11);
    ASSERT_EQ(sram[SLOT16], 0x11);

    wr(CPB_REG_SWAPDONE, 0);
    wr(CPB_REG_SWITCH, 1);
    ASSERT_EQ(sram[CPB_REG_SWAPDONE], 1);
    ASSERT_EQ(rd(SLOT16), 0x00);
    wr(SLOT16, 0x22);

    wr(CPB_REG_SWITCH, 0);
    ASSERT_EQ(rd(SLOT16), 0x11);
    wr(CPB_REG_SWITCH, 1);
    ASSERT_EQ(rd(SLOT16), 0x22);
}

UTEST(cpb, the_pinned_slot_is_shared)
{
    fresh(0);
    knock('+');
    wr(0xF900, 0x5A);
    wr(CPB_REG_SWITCH, 7);
    ASSERT_EQ(rd(0xF900), 0x5A);
}

UTEST(cpb, resb_resets_on_its_falling_edge_only)
{
    fresh(0);
    uint8_t before = sram[CPB_REG_RESETS];
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);

    cpb_resb(true);
    ASSERT_EQ(sram[CPB_REG_RESETS], (uint8_t)(before + 1));
    ASSERT_EQ(rd(SLOT16), 0x00);
    cpb_resb(true);
    ASSERT_EQ(sram[CPB_REG_RESETS], (uint8_t)(before + 1));
    cpb_resb(false);

    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0x00);
    knock('+');
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0xAA);

    cpb_resb(true);
    ASSERT_EQ(sram[CPB_REG_RESETS], (uint8_t)(before + 2));
    cpb_resb(false);
}

UTEST(cpb, no_delay_takes_effect_on_the_next_cycle)
{
    fresh(0);
    sram[SLOT16] = 0x33;
    knock('+');
    map(16, 100);
    ASSERT_EQ(rd(SLOT16), 0x00);
}

UTEST(cpb, a_delay_keeps_the_old_map_for_that_many_cycles)
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

UTEST(cpb, a_delay_keeps_the_order)
{
    fresh(3);
    knock('+');
    wr(CPB_REG_EDIT, 1);
    map(16, 100);
    wr(CPB_REG_EDIT, 0);
    map(16, 101);
    idle(4);
    wr(SLOT16, 0x01);
    wr(CPB_REG_SWITCH, 1);
    idle(4);
    wr(SLOT16, 0x02);
    wr(CPB_REG_SWITCH, 0);
    idle(4);
    ASSERT_EQ(rd(SLOT16), 0x01);
    wr(CPB_REG_SWITCH, 1);
    idle(4);
    ASSERT_EQ(rd(SLOT16), 0x02);
}

UTEST(cpb, a_full_queue_loses_nothing)
{
    fresh(1000);
    knock('+');
    for (int i = 0; i < 300; i++)
        wr(CPB_REG_EDIT, (uint8_t)(i & 1));
    wr(CPB_REG_EDIT, 3);
    map(16, 100);
    idle(1001);
    wr(CPB_REG_SWITCH, 3);
    idle(1001);
    wr(SLOT16, 0x99);
    wr(CPB_REG_SWITCH, 0);
    idle(1001);
    ASSERT_EQ(rd(SLOT16), 0x00);
    wr(CPB_REG_SWITCH, 3);
    idle(1001);
    ASSERT_EQ(rd(SLOT16), 0x99);
}

UTEST(cpb, a_reset_flushes_the_queue_first)
{
    fresh(1000);
    wr(CPB_REG_SWAPDONE, 0);
    knock('+');
    wr(CPB_REG_SWITCH, 2);
    ASSERT_EQ(sram[CPB_REG_SWAPDONE], 0);
    cpb_resb(true);
    ASSERT_EQ(sram[CPB_REG_SWAPDONE], 1);
    cpb_resb(false);
}

UTEST(cpb, the_temperature_is_published_and_restored)
{
    cpb_set_enabled(true);
    cpb_set_delay(0);
    cpb_set_temperature(45.5);
    cpb_init_memory(false, 0, 0);
    ASSERT_EQ(sram[CPB_REG_TEMP_MIN], 0x5B);
    ASSERT_EQ(sram[CPB_REG_TEMP_MAX], 0x5B);
    wr(CPB_REG_TEMP_MIN, 0);
    wr(CPB_REG_TEMP_MAX, 0);
    knock('T');
    ASSERT_EQ(sram[CPB_REG_TEMP_MIN], 0x5B);
    wr(CPB_REG_TEMP_MAX, 0);
    knock('C');
    ASSERT_EQ(sram[CPB_REG_TEMP_MAX], 0x5B);
}

UTEST(cpb, the_temperature_saturates)
{
    cpb_set_enabled(true);
    cpb_set_temperature(200.0);
    cpb_init_memory(false, 0, 0);
    ASSERT_EQ(sram[CPB_REG_TEMP_MIN], 0xFF);
    cpb_set_temperature(-3.0);
    cpb_init_memory(false, 0, 0);
    ASSERT_EQ(sram[CPB_REG_TEMP_MIN], 0x00);
}

UTEST(cpb, a_savestate_carries_the_maps)
{
    static uint8_t blob[CPB_SST_SIZE];
    fresh(0);
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);

    sst_cursor_t c = {blob, blob + sizeof blob, false};
    cpb_sst_save(&c, 0);
    ASSERT_TRUE(sst_ok(&c));
    ASSERT_TRUE(c.at == c.end);

    knock('-');
    wr(SLOT16, 0x00);
    ASSERT_EQ(rd(SLOT16), 0x00);

    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_TRUE(cpb_sst_load(&c, 0));
    ASSERT_EQ(rd(SLOT16), 0xAA);
    map(16, 101);
    ASSERT_NE(rd(SLOT16), 0xAA);
}

UTEST(cpb, a_savestate_that_aliases_block_31_is_refused)
{
    static uint8_t blob[CPB_SST_SIZE];
    fresh(0);
    sst_cursor_t c = {blob, blob + sizeof blob, false};
    cpb_sst_save(&c, 0);
    blob[(CPB_BLOCKS - CPB_SLOTS) * CPB_BLOCK_SIZE + 3] = CPB_PINNED_SLOT;
    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_FALSE(cpb_sst_load(&c, 0));
}

UTEST(cpb, the_debugger_sees_the_state)
{
    fresh(0);
    knock('+');
    wr(CPB_REG_EDIT, 2);
    map(16, 100);
    wr(CPB_REG_SWITCH, 2);
    wr(SLOT16, 0xAA);
    for (const char *p = "#!RP"; *p; p++)
        wr(CPB_REG_KNOCK, (uint8_t)*p);

    cpb_dbg_t st;
    cpb_dbg_get(&st);
    ASSERT_TRUE(st.gate);
    ASSERT_EQ(st.active_task, 2);
    ASSERT_EQ(st.edit_task, 2);
    ASSERT_EQ(st.knock_pos, 4);
    ASSERT_EQ(st.temp, 50);
    ASSERT_EQ(st.active_map[16], 100);
    ASSERT_EQ(st.task_map[2][16], 100);
    ASSERT_EQ(st.task_map[0][16], 16);
    ASSERT_EQ(cpb_dbg_block(100)[0], 0xAA);
    ASSERT_TRUE(cpb_dbg_block(16) == &sram[SLOT16]);
}

UTEST(cpb, off_is_the_plain_machine)
{
    fresh(0);
    cpb_set_enabled(false);
    knock('+');
    map(16, 100);
    wr(SLOT16, 0xAA);
    ASSERT_EQ(sram[SLOT16], 0xAA);
    wr(0xFF10, 0x12);
    ASSERT_EQ(sram[0xFF10], 0x12);
    ASSERT_EQ(rd(0xFF10), 0xA5);
    ASSERT_TRUE(sram_view(SLOT16) == &sram[SLOT16]);
}
