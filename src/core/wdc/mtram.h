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
 * after its firmware built with SRAM_PHI2_SYNC, SRAM_RESET_DETECT=RESB,
 * SRAM_ONEWIRE and the legacy knock protocol, on a board whose VIA is decoded
 * to $FFD0-$FFDF.
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
/* The 1-Wire line of SRAM_ONEWIRE, on in the production image. A non-zero
 * write to DS_CTRL measures every DS18B20 on it: COUNT sensors, their
 * temperatures as signed hundredths, low byte first, DS_TEMP_NONE for a bad
 * read, and their ROM codes, then DS_CTRL reads 0. */
#define MTRAM_REG_DS_CTRL 0xFF2A
#define MTRAM_REG_DS_COUNT 0xFF2B
#define MTRAM_REG_DS_TEMP 0xFF2C
#define MTRAM_REG_DS_ROM 0xFF34
#define MTRAM_DS_MAX 4
#define MTRAM_DS_TEMP_NONE 0x8000
/* The bus mailbox: the line byte by byte for a program. An OW_OP_* written to
 * XFER_CTRL runs with FLAGS, TXLEN, RXLEN and BUF, then STATUS is written and
 * XFER_CTRL reads 0. */
#define MTRAM_REG_XFER_CTRL 0xFF54
#define MTRAM_REG_XFER_STATUS 0xFF55
#define MTRAM_REG_XFER_FLAGS 0xFF56
#define MTRAM_REG_XFER_TXLEN 0xFF58
#define MTRAM_REG_XFER_RXLEN 0xFF59
#define MTRAM_REG_XFER_BUF 0xFF5A
#define MTRAM_XFER_BUF_MAX 22
#define MTRAM_OW_OP_TRANSACT 1
#define MTRAM_OW_OP_FIRST 2
#define MTRAM_OW_OP_NEXT 3
#define MTRAM_OW_ST_NOPRESENCE 0x01
#define MTRAM_OW_ST_END 0x02
#define MTRAM_XFER_ST_BADOP 0x40
#define MTRAM_OW_FLAG_NORESET 0x01
/* DS18B20 a session can put on the line, and the range they measure. */
#define MTRAM_OW_DEVICES 8
#define MTRAM_DS_TEMP_MIN_C -55.0
#define MTRAM_DS_TEMP_MAX_C 125.0
/* The info block, $FF70-$FF7C: what answers there. Voltage in 10 mV, the
 * build date (year - 2000, month, day), option bits B and A, guard and settle
 * cycles ($FF for none), RAM in KB and clock in MHz (16 bits each, low byte
 * first, clock 0 for unknown), and the block's format in $FF7C. */
#define MTRAM_REG_INFO_FIRST 0xFF70
#define MTRAM_REG_INFO_VREG 0xFF70
#define MTRAM_REG_INFO_DATE 0xFF71
#define MTRAM_REG_INFO_OPT_B 0xFF74
#define MTRAM_REG_INFO_OPT_A 0xFF75
#define MTRAM_REG_INFO_GUARD 0xFF76
#define MTRAM_REG_INFO_SETTLE 0xFF77
#define MTRAM_REG_INFO_RAM_KB 0xFF78
#define MTRAM_REG_INFO_CLOCK 0xFF7A
#define MTRAM_REG_INFO 0xFF7C
#define MTRAM_INFO_VERSION 1
#define MTRAM_INFO_A_PHI2_SYNC 0x01
#define MTRAM_INFO_A_SYNC_BYP 0x02
#define MTRAM_INFO_A_RESB 0x04
#define MTRAM_INFO_A_TEMP 0x10
#define MTRAM_INFO_A_SPARSE 0x40
#define MTRAM_INFO_A_ONEWIRE 0x80
#define MTRAM_INFO_B_EMULATOR 0x80
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
/* The DS18B20 on the 1-Wire line, n of them at most MTRAM_OW_DEVICES, found
 * by a search in the order given; none leaves the line empty. */
void mtram_set_ds18b20(const double *celsius, unsigned n);

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

/* Each device's line state, then the line's job, its waiting requests, the
 * mailbox search's place and the results the job publishes when it ends. */
#define MTRAM_OW_SST_SIZE (MTRAM_OW_DEVICES * 20 + 8 + 8 + 1 + 1 + 10 * MTRAM_DS_MAX + \
                           2 + MTRAM_XFER_BUF_MAX)
#define MTRAM_SST_SIZE ((MTRAM_BLOCKS - MTRAM_SLOTS) * MTRAM_BLOCK_SIZE + MTRAM_SLOTS + \
                      MTRAM_TASKS * MTRAM_SLOTS + 6 + 8 + 3 + MTRAM_QUEUE_LEN * 7 +   \
                      MTRAM_OW_SST_SIZE)
void mtram_sst_save(sst_cursor_t *c, unsigned flags);
bool mtram_sst_load(sst_cursor_t *c, unsigned flags);
extern const sst_table_t mtram_sst;

#define MTRAM_DRIVER DRIVER(                                  \
    TASK_PUMPED(mtram_task)                                   \
    SST("MTRM", mtram))

#endif /* _CORE_WDC_MTRAM_H_ */
