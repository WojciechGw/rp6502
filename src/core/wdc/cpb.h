/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The PICO-SRAM-BANKED module (CPB) in the 6502's SRAM socket: 512 KB in
 * 2 KB blocks, mapped into the 6502's 32 slots through one of 16 task maps
 * and driven by writes to $FEDD-$FEFF. Modelled after its firmware built with
 * SRAM_RESET_DETECT=RESB and the legacy knock protocol. Blocks 0-31 are
 * sram[], so the identity map is the plain machine.
 */

#ifndef _CORE_WDC_CPB_H_
#define _CORE_WDC_CPB_H_

#include "core/sys/sst.h"
#include <stdbool.h>
#include <stdint.h>

#define CPB_BLOCK_SHIFT 11
#define CPB_BLOCK_SIZE (1u << CPB_BLOCK_SHIFT)
#define CPB_BLOCKS 256
#define CPB_SLOTS 32
#define CPB_PINNED_SLOT 31
#define CPB_TASKS 16
#define CPB_QUEUE_LEN 256
#define CPB_DELAY_MAX 1000000u

#define CPB_REG_RESETS 0xFED5
#define CPB_REG_TEMP_MIN 0xFED6
#define CPB_REG_TEMP_MAX 0xFED7
#define CPB_REG_SWAPDONE 0xFEDD
#define CPB_REG_EDIT 0xFEDE
#define CPB_REG_SWITCH 0xFEDF
#define CPB_REG_BANK_BASE 0xFEE0
#define CPB_REG_BANK_LAST 0xFEFE
#define CPB_REG_KNOCK 0xFEFF

/* Read on every bus cycle, so it is a variable rather than a call. */
extern bool cpb_on;

/* Session settings, set before sys_init. */
void cpb_set_enabled(bool on);
void cpb_set_temperature(double celsius);
void cpb_set_delay(uint32_t cycles);

/* The ext blocks get the fill sram_init gives sram[]. */
void cpb_init_memory(bool random, uint8_t value, uint32_t seed);

void cpb_tick(uint16_t addr, bool read, uint8_t *data);

/* The byte the 6502 sees at addr through the live map. */
uint8_t *cpb_view(uint16_t addr);

/* The RESB line, low = asserted. */
void cpb_resb(bool low);

void cpb_task(void);

#define CPB_SST_SIZE ((CPB_BLOCKS - CPB_SLOTS) * CPB_BLOCK_SIZE + CPB_SLOTS + \
                      CPB_TASKS * CPB_SLOTS + 6 + 8 + 3 + CPB_QUEUE_LEN * 7)
void cpb_sst_save(sst_cursor_t *c, unsigned flags);
bool cpb_sst_load(sst_cursor_t *c, unsigned flags);

#define CPB_DRIVER DRIVER(nul_init, cpb_task, nul_task, nul_run, nul_stop, nul_break, \
    nul_config, nul_config, SST(CPB_, 1, CPB_SST_SIZE, cpb_sst_save, cpb_sst_load))

#endif /* _CORE_WDC_CPB_H_ */
