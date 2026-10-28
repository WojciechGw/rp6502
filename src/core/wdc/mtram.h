/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The mtRAM module in the 6502's SRAM socket: 512 KB in
 * 2 KB blocks, mapped into the 6502's 32 slots through one of 16 task maps
 * and driven by writes to $FEDD-$FEFF. Modelled after its firmware built with
 * SRAM_RESET_DETECT=RESB and the legacy knock protocol. Blocks 0-31 are
 * sram[], so the identity map is the plain machine.
 */

#ifndef _CORE_WDC_MTRAM_H_
#define _CORE_WDC_MTRAM_H_

#include "core/sys/sst.h"
#include <stdbool.h>
#include <stdint.h>

#define MTRAM_BLOCK_SHIFT 11
#define MTRAM_BLOCK_SIZE (1u << MTRAM_BLOCK_SHIFT)
#define MTRAM_BLOCKS 256
#define MTRAM_SLOTS 32
#define MTRAM_PINNED_SLOT 31
#define MTRAM_TASKS 16
#define MTRAM_QUEUE_LEN 256
#define MTRAM_DELAY_MAX 1000000u

/* Written only after a '+', 'T' or 'C' knock since the last reset; plain RAM
 * until then. */
#define MTRAM_REG_RESETS 0xFED5
#define MTRAM_REG_TEMP_MIN 0xFED6
#define MTRAM_REG_TEMP_MAX 0xFED7
#define MTRAM_REG_SWAPDONE 0xFEDD
#define MTRAM_REG_EDIT 0xFEDE
#define MTRAM_REG_SWITCH 0xFEDF
#define MTRAM_REG_BANK_BASE 0xFEE0
#define MTRAM_REG_BANK_LAST 0xFEFE
#define MTRAM_REG_KNOCK 0xFEFF

/* Read on every bus cycle, so it is a variable rather than a call. */
extern bool mtram_on;

/* Session settings, set before sys_init. */
void mtram_set_enabled(bool on);
void mtram_set_temperature(double celsius);
void mtram_set_delay(uint32_t cycles);

/* The ext blocks get the fill sram_init gives sram[]. */
void mtram_init_memory(bool random, uint8_t value, uint32_t seed);

void mtram_tick(uint16_t addr, bool read, uint8_t *data);

/* The byte the 6502 sees at addr through the live map. */
uint8_t *mtram_view(uint16_t addr);

/* The RESB line, low = asserted. */
void mtram_resb(bool low);

/* The module powering up: no resets counted, and RESB seen high, so the next
 * assert is counted as the first. */
void mtram_power_on(void);

void mtram_task(void);

/* What the debugger window shows. Nothing here changes the module. */
typedef struct
{
    bool gate, status_hidden;
    uint8_t active_task, edit_task, knock_pos, resets, temp;
    uint32_t delay;
    uint16_t queued;
    uint8_t active_map[MTRAM_SLOTS];
    uint8_t task_map[MTRAM_TASKS][MTRAM_SLOTS];
} mtram_dbg_t;
void mtram_dbg_get(mtram_dbg_t *out);
const uint8_t *mtram_dbg_block(uint8_t block);

#define MTRAM_SST_SIZE ((MTRAM_BLOCKS - MTRAM_SLOTS) * MTRAM_BLOCK_SIZE + MTRAM_SLOTS + \
                      MTRAM_TASKS * MTRAM_SLOTS + 7 + 8 + 3 + MTRAM_QUEUE_LEN * 7)
void mtram_sst_save(sst_cursor_t *c, unsigned flags);
bool mtram_sst_load(sst_cursor_t *c, unsigned flags);

#define MTRAM_DRIVER DRIVER(nul_init, mtram_task, nul_task, nul_run, nul_stop, nul_break, \
    nul_config, nul_config, SST(MTRM, 2, MTRAM_SST_SIZE, mtram_sst_save, mtram_sst_load))

#endif /* _CORE_WDC_MTRAM_H_ */
