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
#include "core/wdc/phi2.h"
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

static int16_t reg16(uint16_t reg)
{
    return (int16_t)(sram[reg] | (sram[reg + 1] << 8));
}

static bool signed_off(void)
{
    return rd(MTRAM_REG_SIGNATURE) == 'm' && rd(MTRAM_REG_SIGNATURE + 1) == 't' &&
           rd(MTRAM_REG_SIGNATURE + 2) == 'R';
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

/* The window has no program's RAM to hide from, so the count is written at
 * every reset and the temperature from the start, with no knock. */
UTEST(mtram, the_status_is_always_published)
{
    fresh(0);
    mtram_power_on();
    memset(&sram[MTRAM_REG_WINDOW_BASE], 0, 0x80);
    mtram_init_memory(false, 0, 0);
    ASSERT_EQ(rd(MTRAM_REG_RESETS), 0);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_CURR), 2500);
    ASSERT_TRUE(signed_off());
    for (int i = 0; i < 10; i++)
    {
        mtram_resb(true);
        mtram_resb(false);
    }
    ASSERT_EQ(rd(MTRAM_REG_RESETS), 10);
    wr(MTRAM_REG_RESETS, 0xC3);
    ASSERT_EQ(rd(MTRAM_REG_RESETS), 0xC3);
    mtram_resb(true);
    mtram_resb(false);
    ASSERT_EQ(rd(MTRAM_REG_RESETS), 11);
}

UTEST(mtram, the_window_answers_reads)
{
    fresh(0);
    ASSERT_EQ(rd(MTRAM_REG_SIGNATURE), 'm');
    ASSERT_EQ(rd(MTRAM_REG_WINDOW_LAST), 'R');
    wr(0xFF40, 0x5A);
    ASSERT_EQ(rd(0xFF40), 0x5A);
    ASSERT_EQ(rd(0xFF80), 0xA5);
    mtram_set_enabled(false);
    ASSERT_EQ(rd(MTRAM_REG_SIGNATURE), 0xA5);
}

UTEST(mtram, the_old_registers_are_plain_ram)
{
    fresh(0);
    for (const char *p = "#!RP6502+"; *p; p++)
        wr(0xFEFF, (uint8_t)*p);
    wr(0xFEF0, 100);
    ASSERT_FALSE(state().gate);
    ASSERT_EQ(rd(0xFEF0), 100);
    ASSERT_EQ(state().active_map[16], 16);
}

UTEST(mtram, the_signature_comes_back_after_a_write)
{
    fresh(0);
    wr(MTRAM_REG_SIGNATURE + 1, 0x00);
    ASSERT_TRUE(signed_off());

    fresh(4);
    wr(MTRAM_REG_SIGNATURE, 0x00);
    ASSERT_EQ(rd(MTRAM_REG_SIGNATURE), 0x00);
    idle(4);
    ASSERT_TRUE(signed_off());
}

static unsigned rd16(uint16_t reg)
{
    return rd(reg) | (unsigned)rd((uint16_t)(reg + 1)) << 8;
}

UTEST(mtram, the_info_block_says_what_answers)
{
    fresh(0);
    ASSERT_EQ(rd(MTRAM_REG_INFO), MTRAM_INFO_VERSION);
    ASSERT_EQ(rd16(MTRAM_REG_INFO_RAM_KB), 512u);
    ASSERT_EQ(rd16(MTRAM_REG_INFO_CLOCK), 0u);
    ASSERT_EQ(rd(MTRAM_REG_INFO_VREG), 0);
    ASSERT_EQ(rd(MTRAM_REG_INFO_SETTLE), 0xFF);
    ASSERT_EQ(rd(MTRAM_REG_INFO_GUARD), 0xFF);
    ASSERT_EQ(rd(MTRAM_REG_INFO_OPT_B), MTRAM_INFO_B_EMULATOR);
    ASSERT_EQ(rd(MTRAM_REG_INFO_OPT_A), 0xD7);
    uint8_t month = rd(MTRAM_REG_INFO_DATE + 1);
    uint8_t day = rd(MTRAM_REG_INFO_DATE + 2);
    ASSERT_TRUE(rd(MTRAM_REG_INFO_DATE) >= 26);
    ASSERT_TRUE(month >= 1 && month <= 12 && day >= 1 && day <= 31);

    // Written over anywhere in $FF70-$FF7F, it all comes back.
    wr(MTRAM_REG_INFO_DATE + 1, 0x00);
    ASSERT_EQ(rd(MTRAM_REG_INFO_DATE + 1), month);
    wr(MTRAM_REG_INFO_FIRST, 0xEE);
    ASSERT_EQ(rd(MTRAM_REG_INFO_VREG), 0);
    wr(MTRAM_REG_INFO, 0x00);
    ASSERT_EQ(rd(MTRAM_REG_INFO), MTRAM_INFO_VERSION);
    ASSERT_TRUE(signed_off());

    // The byte below it is a plain one.
    wr(MTRAM_REG_INFO_FIRST - 1, 0x11);
    ASSERT_EQ(rd(MTRAM_REG_INFO_FIRST - 1), 0x11);
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
    ASSERT_EQ(sram[MTRAM_REG_TEMP_CURR], 0xC6);
    ASSERT_EQ(sram[MTRAM_REG_TEMP_CURR + 1], 0x11);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_MIN), 4550);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_MAX), 4550);
    wr(MTRAM_REG_TEMP_MIN, 0);
    wr(MTRAM_REG_TEMP_MAX, 0);
    knock('T');
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_MIN), 4550);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_MAX), 4550);
    wr(MTRAM_REG_TEMP_CURR, 0);
    knock('C');
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_CURR), 4550);
}

UTEST(mtram, the_temperature_is_signed_hundredths)
{
    mtram_set_enabled(true);
    mtram_set_temperature(-12.34);
    mtram_init_memory(false, 0, 0);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_CURR), -1234);
    mtram_set_temperature(23.47);
    mtram_init_memory(false, 0, 0);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_CURR), 2347);
    mtram_set_temperature(400.0);
    mtram_init_memory(false, 0, 0);
    ASSERT_EQ(reg16(MTRAM_REG_TEMP_CURR), INT16_MAX);
    mtram_set_temperature(25.0);
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
    ASSERT_FALSE(state().gate);

    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_TRUE(mtram_sst_load(&c, 0));
    ASSERT_TRUE(state().gate);
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
    ASSERT_EQ(st.temp, 2500);
    ASSERT_EQ(st.active_map[16], 100);
    ASSERT_EQ(st.task_map[2][16], 100);
    ASSERT_EQ(st.task_map[0][16], 16);
    ASSERT_EQ(mtram_dbg_block(100)[0], 0xAA);
    ASSERT_TRUE(mtram_dbg_block(16) == &sram[SLOT16]);
}

/* The 1-Wire line. At 1000 kHz a cycle is a microsecond. */

#define OW_KHZ 1000
#define OW_CONVERT 750000u

static void ow_fresh(unsigned n, const double *celsius)
{
    phi2_set_khz_run(OW_KHZ);
    mtram_set_ds18b20(celsius, n);
    fresh(0);
}

/* Cycles until reg reads 0, or 0 if it never does within two seconds. */
static unsigned ow_wait(uint16_t reg)
{
    for (unsigned n = 1; n <= 2000000; n++)
        if (!rd(reg))
            return n;
    return 0;
}

static void mb_buf(const uint8_t *tx, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        wr((uint16_t)(MTRAM_REG_XFER_BUF + i), tx[i]);
}

/* STATUS of a transaction, or 0xEE if it never ended. */
static uint8_t mb_transact(uint8_t flags, const uint8_t *tx, unsigned n_tx, unsigned n_rx)
{
    mb_buf(tx, n_tx);
    wr(MTRAM_REG_XFER_FLAGS, flags);
    wr(MTRAM_REG_XFER_TXLEN, (uint8_t)n_tx);
    wr(MTRAM_REG_XFER_RXLEN, (uint8_t)n_rx);
    wr(MTRAM_REG_XFER_CTRL, MTRAM_OW_OP_TRANSACT);
    return ow_wait(MTRAM_REG_XFER_CTRL) ? rd(MTRAM_REG_XFER_STATUS) : 0xEE;
}

static uint8_t mb_op(uint8_t op)
{
    wr(MTRAM_REG_XFER_CTRL, op);
    return ow_wait(MTRAM_REG_XFER_CTRL) ? rd(MTRAM_REG_XFER_STATUS) : 0xEE;
}

static uint8_t ow_crc(const uint8_t *p, unsigned n)
{
    uint8_t crc = 0;
    while (n--)
    {
        uint8_t in = *p++;
        for (unsigned i = 0; i < 8; i++, in >>= 1)
        {
            bool mix = (crc ^ in) & 1;
            crc >>= 1;
            if (mix)
                crc ^= 0x8C;
        }
    }
    return crc;
}

static void buf(uint8_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        out[i] = rd((uint16_t)(MTRAM_REG_XFER_BUF + i));
}

UTEST(mtram, a_measurement_reads_every_sensor)
{
    const double t[] = {21.5, -10.125};
    ow_fresh(2, t);
    wr(MTRAM_REG_DS_CTRL, 1);
    ASSERT_EQ(rd(MTRAM_REG_DS_CTRL), 1);
    unsigned took = ow_wait(MTRAM_REG_DS_CTRL);
    ASSERT_TRUE(took > OW_CONVERT && took < OW_CONVERT + 100000);
    ASSERT_EQ(rd(MTRAM_REG_DS_COUNT), 2);
    ASSERT_EQ(reg16(MTRAM_REG_DS_TEMP), 2150);
    ASSERT_EQ(reg16(MTRAM_REG_DS_TEMP + 2), -1012); /* -162 sixteenths, as the module divides */
    for (unsigned s = 0; s < 2; s++)
    {
        const uint8_t *rom = &sram[MTRAM_REG_DS_ROM + 8 * s];
        ASSERT_EQ(rom[0], 0x28);
        ASSERT_EQ(ow_crc(rom, 8), 0);
        ASSERT_EQ(rom[1], s ? 0x80 : 0x00); /* in the order given */
    }
}

UTEST(mtram, a_measurement_without_sensors_finds_none)
{
    ow_fresh(0, NULL);
    wr(MTRAM_REG_DS_COUNT, 9);
    wr(MTRAM_REG_DS_CTRL, 1);
    unsigned took = ow_wait(MTRAM_REG_DS_CTRL);
    ASSERT_TRUE(took > 0 && took < 2000);
    ASSERT_EQ(rd(MTRAM_REG_DS_COUNT), 0);
}

UTEST(mtram, a_measurement_takes_four_sensors)
{
    const double t[] = {1, 2, 3, 4, 5, 6};
    ow_fresh(6, t);
    wr(MTRAM_REG_DS_CTRL, 1);
    ASSERT_TRUE(ow_wait(MTRAM_REG_DS_CTRL) > 0);
    ASSERT_EQ(rd(MTRAM_REG_DS_COUNT), MTRAM_DS_MAX);
    for (int s = 0; s < MTRAM_DS_MAX; s++)
        ASSERT_EQ(reg16((uint16_t)(MTRAM_REG_DS_TEMP + 2 * s)), 100 * (s + 1));
}

UTEST(mtram, a_write_while_measuring_starts_nothing)
{
    const double t[] = {20};
    ow_fresh(1, t);
    wr(MTRAM_REG_DS_CTRL, 1);
    unsigned whole = ow_wait(MTRAM_REG_DS_CTRL);
    wr(MTRAM_REG_DS_CTRL, 1);
    idle(1000);
    wr(MTRAM_REG_DS_CTRL, 1);
    unsigned rest = ow_wait(MTRAM_REG_DS_CTRL);
    ASSERT_TRUE(whole > OW_CONVERT);
    ASSERT_EQ(rest + 1001, whole);
}

UTEST(mtram, the_mailbox_search_lists_the_line)
{
    const double t[] = {20, 21, 22};
    ow_fresh(3, t);
    uint8_t rom[8];
    for (unsigned s = 0; s < 3; s++)
    {
        ASSERT_EQ(mb_op(s ? MTRAM_OW_OP_NEXT : MTRAM_OW_OP_FIRST), 0);
        buf(rom, 8);
        ASSERT_EQ(rom[0], 0x28);
        ASSERT_EQ(ow_crc(rom, 8), 0);
        ASSERT_EQ(rom[1], (uint8_t)(s == 0 ? 0x00 : s == 1 ? 0x80 : 0x40));
    }
    ASSERT_EQ(mb_op(MTRAM_OW_OP_NEXT), MTRAM_OW_ST_END);
    ASSERT_EQ(mb_op(MTRAM_OW_OP_FIRST), 0);
    buf(rom, 8);
    ASSERT_EQ(rom[1], 0x00);

    ow_fresh(0, NULL);
    ASSERT_EQ(mb_op(MTRAM_OW_OP_FIRST), MTRAM_OW_ST_NOPRESENCE | MTRAM_OW_ST_END);
    ASSERT_EQ(mb_transact(0, NULL, 0, 1), MTRAM_OW_ST_NOPRESENCE);
}

UTEST(mtram, the_mailbox_reads_a_scratchpad)
{
    const double t[] = {30.0};
    const uint8_t read[] = {0xCC, 0xBE};
    const uint8_t convert[] = {0xCC, 0x44};
    uint8_t pad[9];
    ow_fresh(1, t);

    /* Before any conversion: the 85 °C it powers up with. */
    ASSERT_EQ(mb_transact(0, read, 2, 9), 0);
    buf(pad, 9);
    ASSERT_EQ(pad[0], 0x50);
    ASSERT_EQ(pad[1], 0x05);
    ASSERT_EQ(ow_crc(pad, 9), 0);

    /* Busy reads 0 until the conversion is done, and the reading is there. */
    ASSERT_EQ(mb_transact(0, convert, 2, 1), 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF), 0x00);
    idle(OW_CONVERT);
    ASSERT_EQ(mb_transact(MTRAM_OW_FLAG_NORESET, NULL, 0, 1), 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF), 0xFF);
    ASSERT_EQ(mb_transact(0, read, 2, 9), 0);
    buf(pad, 9);
    ASSERT_EQ(pad[0] | pad[1] << 8, 480);
    ASSERT_EQ(ow_crc(pad, 9), 0);

    /* The command and the read in two transactions, the second without a reset. */
    ASSERT_EQ(mb_transact(0, read, 1, 0), 0);
    ASSERT_EQ(mb_transact(MTRAM_OW_FLAG_NORESET, &read[1], 1, 2), 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF), 0xE0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF + 1), 0x01);
}

UTEST(mtram, match_rom_picks_one_sensor)
{
    const double t[] = {10.0, 40.0};
    const uint8_t convert[] = {0xCC, 0x44};
    uint8_t tx[10] = {0x55};
    ow_fresh(2, t);
    ASSERT_EQ(mb_transact(0, convert, 2, 0), 0);
    idle(OW_CONVERT);
    ASSERT_EQ(mb_op(MTRAM_OW_OP_FIRST), 0);
    ASSERT_EQ(mb_op(MTRAM_OW_OP_NEXT), 0);
    buf(&tx[1], 8);
    tx[9] = 0xBE;
    ASSERT_EQ(mb_transact(0, tx, 10, 2), 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF) | rd(MTRAM_REG_XFER_BUF + 1) << 8, 640);
}

UTEST(mtram, the_resolution_is_written_and_kept)
{
    const double t[] = {21.6875}; /* 347 sixteenths */
    const uint8_t write[] = {0xCC, 0x4E, 0x4B, 0x46, 0x1F};
    const uint8_t convert[] = {0xCC, 0x44};
    const uint8_t read[] = {0xCC, 0xBE};
    ow_fresh(1, t);
    ASSERT_EQ(mb_transact(0, write, 5, 0), 0);
    ASSERT_EQ(mb_transact(0, convert, 2, 0), 0);
    idle(93750); /* 9 bits */
    ASSERT_EQ(mb_transact(0, read, 2, 5), 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF) | rd(MTRAM_REG_XFER_BUF + 1) << 8, 344);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF + 4), 0x1F);
}

UTEST(mtram, the_mailbox_refuses_an_operation_it_lacks)
{
    const double t[] = {20};
    ow_fresh(1, t);
    wr(MTRAM_REG_XFER_BUF, 0x5A);
    wr(MTRAM_REG_XFER_STATUS, 0xFF);
    wr(MTRAM_REG_XFER_CTRL, 4);
    idle(90);
    ASSERT_EQ(rd(MTRAM_REG_XFER_CTRL), 4);
    unsigned took = ow_wait(MTRAM_REG_XFER_CTRL);
    ASSERT_TRUE(took > 0 && took < 20);
    ASSERT_EQ(rd(MTRAM_REG_XFER_STATUS), MTRAM_XFER_ST_BADOP);
    ASSERT_EQ(rd(MTRAM_REG_XFER_BUF), 0x5A);
}

UTEST(mtram, the_mailbox_and_a_measurement_share_the_line)
{
    const double t[] = {20};
    ow_fresh(1, t);
    wr(MTRAM_REG_DS_CTRL, 1);
    wr(MTRAM_REG_XFER_CTRL, 4);
    ASSERT_TRUE(ow_wait(MTRAM_REG_DS_CTRL) > OW_CONVERT);
    ASSERT_EQ(rd(MTRAM_REG_XFER_CTRL), 4);
    ASSERT_TRUE(ow_wait(MTRAM_REG_XFER_CTRL) > 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_STATUS), MTRAM_XFER_ST_BADOP);

    wr(MTRAM_REG_XFER_CTRL, MTRAM_OW_OP_FIRST);
    wr(MTRAM_REG_DS_CTRL, 1);
    ASSERT_TRUE(ow_wait(MTRAM_REG_XFER_CTRL) > 0);
    ASSERT_EQ(rd(MTRAM_REG_XFER_STATUS), 0);
    ASSERT_EQ(rd(MTRAM_REG_DS_CTRL), 1);
    ASSERT_TRUE(ow_wait(MTRAM_REG_DS_CTRL) > 0);
    ASSERT_EQ(rd(MTRAM_REG_DS_COUNT), 1);
}

UTEST(mtram, a_savestate_carries_a_measurement)
{
    static uint8_t blob[MTRAM_SST_SIZE];
    const double t[] = {20, 25};
    ow_fresh(2, t);
    wr(MTRAM_REG_DS_CTRL, 1);
    idle(1000);
    sst_cursor_t c = {blob, blob + sizeof blob, false};
    mtram_sst_save(&c, 0);
    ASSERT_TRUE(sst_ok(&c));
    ASSERT_TRUE(c.at == c.end);

    ow_fresh(2, t);
    c = (sst_cursor_t){blob, blob + sizeof blob, false};
    ASSERT_TRUE(mtram_sst_load(&c, 0));
    sram[MTRAM_REG_DS_CTRL] = 1;
    ASSERT_TRUE(ow_wait(MTRAM_REG_DS_CTRL) > 0);
    ASSERT_EQ(rd(MTRAM_REG_DS_COUNT), 2);
    ASSERT_EQ(reg16(MTRAM_REG_DS_TEMP + 2), 2500);
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
