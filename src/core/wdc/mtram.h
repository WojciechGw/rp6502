/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The mtRAM module in the 6502's SRAM socket: 512 KB in
 * 2 KB blocks, mapped into the 6502's 32 slots through one of 16 task maps
 * and driven by its register window $FF00-$FF7F in the I/O page. Modelled
 * after its firmware built with SRAM_PHI2_SYNC, SRAM_RESET_DETECT=RESB and
 * the legacy knock protocol, on a board whose VIA is decoded to $FFD0-$FFDF.
 * Blocks 0-31 are sram[], so the identity map is the plain machine, and the
 * window's bytes are block 31's.
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

#define MTRAM_REG_WINDOW_BASE 0xFF00
#define MTRAM_REG_WINDOW_LAST 0xFF7F
#define MTRAM_REG_BANK_BASE 0xFF00
#define MTRAM_REG_BANK_LAST 0xFF1E
#define MTRAM_REG_KNOCK 0xFF1F
#define MTRAM_REG_SWITCH 0xFF20
#define MTRAM_REG_EDIT 0xFF21
#define MTRAM_REG_SWAPDONE 0xFF22
#define MTRAM_REG_RESETS 0xFF23
/* Signed 16-bit hundredths of a degree, low byte first. */
#define MTRAM_REG_TEMP_CURR 0xFF24
#define MTRAM_REG_TEMP_MIN 0xFF26
#define MTRAM_REG_TEMP_MAX 0xFF28
/* "mtR", which open bus cannot read as. */
#define MTRAM_REG_SIGNATURE 0xFF7D

#define MTRAM_TEMP_MIN_C -40.0
#define MTRAM_TEMP_MAX_C 125.0

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
    bool gate;
    uint8_t active_task, edit_task, knock_pos, resets;
    int16_t temp;
    uint32_t delay;
    uint16_t queued;
    uint8_t active_map[MTRAM_SLOTS];
    uint8_t task_map[MTRAM_TASKS][MTRAM_SLOTS];
} mtram_dbg_t;
void mtram_dbg_get(mtram_dbg_t *out);
const uint8_t *mtram_dbg_block(uint8_t block);

#define MTRAM_SST_SIZE ((MTRAM_BLOCKS - MTRAM_SLOTS) * MTRAM_BLOCK_SIZE + MTRAM_SLOTS + \
                      MTRAM_TASKS * MTRAM_SLOTS + 6 + 8 + 3 + MTRAM_QUEUE_LEN * 7)
void mtram_sst_save(sst_cursor_t *c, unsigned flags);
bool mtram_sst_load(sst_cursor_t *c, unsigned flags);

#define MTRAM_DRIVER DRIVER(nul_init, mtram_task, nul_task, nul_run, nul_stop, nul_break, \
    nul_config, nul_config, SST(MTRM, 3, MTRAM_SST_SIZE, mtram_sst_save, mtram_sst_load))

#endif /* _CORE_WDC_MTRAM_H_ */
