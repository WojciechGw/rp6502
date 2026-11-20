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

static int16_t mtram_temp = 2500;
static uint32_t mtram_delay;

static const uint8_t mtram_knock_prefix[8] = {'#', '!', 'R', 'P', '6', '5', '0', '2'};

void mtram_set_enabled(bool on)
{
    mtram_on = on;
}

/* The module's encoding: hundredths of a degree, rounded, saturating at both
 * ends of int16. */
void mtram_set_temperature(double celsius)
{
    double v = celsius * 100.0;
    v += v < 0.0 ? -0.5 : 0.5;
    if (v < -32768.0)
        mtram_temp = INT16_MIN;
    else if (v > 32767.0)
        mtram_temp = INT16_MAX;
    else
        mtram_temp = (int16_t)v;
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

static void mtram_store16(uint16_t reg, int16_t v)
{
    sram[reg] = (uint8_t)v;
    sram[reg + 1] = (uint8_t)((uint16_t)v >> 8);
}

/* __DATE__ is "Nov 14 2026". */
static uint8_t mtram_build_month(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (uint8_t m = 0; m < 12; m++)
        if (!memcmp(&months[m * 3], __DATE__, 3))
            return (uint8_t)(m + 1);
    return 0;
}

/* The info block and the signature, $FF70-$FF7F, which the module writes and
 * puts back together. The emulator says what it models -- the production
 * image's options, 512 KB -- with the emulator's bit, and leaves out what it
 * has no counterpart for: voltage 0, clock 0, no settle or guard. */
static void mtram_signature_write(void)
{
    const char *d = __DATE__;
    sram[MTRAM_REG_INFO_VREG] = 0;
    sram[MTRAM_REG_INFO_DATE] = (uint8_t)((d[9] - '0') * 10 + (d[10] - '0'));
    sram[MTRAM_REG_INFO_DATE + 1] = mtram_build_month();
    sram[MTRAM_REG_INFO_DATE + 2] =
        (uint8_t)((d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0'));
    sram[MTRAM_REG_INFO_OPT_B] = MTRAM_INFO_B_EMULATOR;
    sram[MTRAM_REG_INFO_OPT_A] = MTRAM_INFO_A_PHI2_SYNC | MTRAM_INFO_A_SYNC_BYP |
                                 MTRAM_INFO_A_RESB | MTRAM_INFO_A_TEMP |
                                 MTRAM_INFO_A_SPARSE | MTRAM_INFO_A_ONEWIRE;
    sram[MTRAM_REG_INFO_GUARD] = 0xFF;
    sram[MTRAM_REG_INFO_SETTLE] = 0xFF;
    sram[MTRAM_REG_INFO_RAM_KB] = (uint8_t)(MTRAM_BLOCKS * 2);
    sram[MTRAM_REG_INFO_RAM_KB + 1] = (uint8_t)(MTRAM_BLOCKS * 2 >> 8);
    sram[MTRAM_REG_INFO_CLOCK] = 0;
    sram[MTRAM_REG_INFO_CLOCK + 1] = 0;
    sram[MTRAM_REG_INFO] = MTRAM_INFO_VERSION;
    sram[MTRAM_REG_SIGNATURE] = 'm';
    sram[MTRAM_REG_SIGNATURE + 1] = 't';
    sram[MTRAM_REG_SIGNATURE + 2] = 'R';
}

/* A temperature that never changes is its own range, so 'C' has nothing to
 * clear. The module writes the signature with every measurement. */
static void mtram_publish_temperature(void)
{
    mtram_store16(MTRAM_REG_TEMP_CURR, mtram_temp);
    mtram_store16(MTRAM_REG_TEMP_MIN, mtram_temp);
    mtram_store16(MTRAM_REG_TEMP_MAX, mtram_temp);
    mtram_signature_write();
}

/* The 1-Wire line of SRAM_ONEWIRE, with the DS18B20 a session puts on it.
 *
 * The devices hear and answer a byte at a time, which is all the mailbox can
 * send them; only the search, the module's own, goes bit by bit. A job, a
 * measurement or a mailbox operation, runs whole when it starts and is
 * published, its control byte cleared, after the PHI2 cycles the line would
 * have taken: a reset, time slots and, for a measurement, the conversion the
 * module waits out. A request while the other job has the line waits for it,
 * as on the module, and one while its own kind runs is dropped. */

#define MTRAM_OW_RESET_US 960u
#define MTRAM_OW_SLOT_US 70u
#define MTRAM_OW_BADOP_US 100u
#define MTRAM_DS_CONVERT_US 750000u
#define MTRAM_DS_FAMILY 0x28
#define MTRAM_DS_SEARCH_PASSES (2u * MTRAM_DS_MAX)

_Static_assert(MTRAM_REG_DS_ROM + 8 * MTRAM_DS_MAX <= MTRAM_REG_XFER_CTRL &&
                   MTRAM_REG_XFER_BUF + MTRAM_XFER_BUF_MAX <= MTRAM_REG_INFO_FIRST,
               "the sensors' registers, the mailbox and the info block are apart");

enum
{
    MTRAM_OW_IDLE,    /* not addressed: hears nothing, sends ones */
    MTRAM_OW_ROMCMD,  /* after a reset */
    MTRAM_OW_MATCH,   /* comparing Match ROM's code */
    MTRAM_OW_FUNC,    /* addressed, waiting for a function command */
    MTRAM_OW_SEND,    /* the ROM code or the scratchpad */
    MTRAM_OW_WRITE,   /* Write Scratchpad's TH, TL and configuration */
    MTRAM_OW_CONVERT, /* read slots say whether the conversion is done */
    MTRAM_OW_DONE,    /* read slots give ones */
    MTRAM_OW_STATES
};

enum
{
    MTRAM_OW_JOB_NONE,
    MTRAM_OW_JOB_DS,
    MTRAM_OW_JOB_MB
};

typedef struct
{
    uint8_t state, pos;
    bool send_rom;
    bool converting;
    uint8_t scratch[9];
    uint8_t eeprom[3]; /* TH, TL, configuration */
    uint32_t done;     /* the cycle a conversion ends */
} mtram_ds_t;

static unsigned mtram_ds_count;
static int16_t mtram_ds_raw[MTRAM_OW_DEVICES]; /* sixteenths of a degree */
static mtram_ds_t mtram_ds[MTRAM_OW_DEVICES];

/* The line's job, the requests waiting for it, the mailbox search's place, and
 * what the job publishes when it ends. */
typedef struct
{
    uint8_t job;
    uint32_t due;
    bool ds_waiting;
    uint8_t mb_waiting;
    uint8_t mb_last_disc;
    uint8_t mb_rom[8];
    bool mb_end;
    uint8_t ds_count;
    uint16_t ds_temp[MTRAM_DS_MAX];
    uint8_t ds_rom[MTRAM_DS_MAX][8];
    uint8_t mb_status, mb_len;
    uint8_t mb_buf[MTRAM_XFER_BUF_MAX];
} mtram_ow_t;
static mtram_ow_t mtram_ow;

void mtram_set_ds18b20(const double *celsius, unsigned n)
{
    if (n > MTRAM_OW_DEVICES)
        n = MTRAM_OW_DEVICES;
    for (unsigned i = 0; i < n; i++)
    {
        double v = celsius[i] * 16.0;
        mtram_ds_raw[i] = (int16_t)(v + (v < 0.0 ? -0.5 : 0.5));
    }
    mtram_ds_count = n;
}

/* 0 over a ROM code or a scratchpad with its CRC byte. */
static uint8_t mtram_ow_crc(const uint8_t *p, unsigned n)
{
    uint8_t crc = 0;
    while (n--)
    {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            crc = (uint8_t)((crc >> 1) ^ ((crc & 1) ? 0x8C : 0));
    }
    return crc;
}

/* Byte 1 is the index with its bits reversed: a search takes the 0 way first,
 * lowest bit first, so it finds the sensors in the order they were given. */
static void mtram_ds_rom(unsigned i, uint8_t rom[8])
{
    uint8_t r = 0;
    for (unsigned b = 0; b < 8; b++)
        if ((i >> b) & 1)
            r |= (uint8_t)(0x80 >> b);
    rom[0] = MTRAM_DS_FAMILY;
    rom[1] = r;
    rom[2] = 'm';
    rom[3] = 't';
    rom[4] = 'R';
    rom[5] = 0;
    rom[6] = 0;
    rom[7] = mtram_ow_crc(rom, 7);
}

static void mtram_ds_seal(mtram_ds_t *d)
{
    d->scratch[8] = mtram_ow_crc(d->scratch, 8);
}

/* 85 °C, alarms at 75 and 70, 12 bits: how a sensor comes up. */
static void mtram_ds_power_on(mtram_ds_t *d)
{
    static const uint8_t pad[8] = {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    memset(d, 0, sizeof *d);
    memcpy(d->scratch, pad, sizeof pad);
    memcpy(d->eeprom, &pad[2], sizeof d->eeprom);
    mtram_ds_seal(d);
}

/* 9 to 12 bits, from the configuration byte. */
static unsigned mtram_ds_bits(const mtram_ds_t *d)
{
    return 9u + ((d->scratch[4] >> 5) & 3u);
}

/* The bits below the resolution are left out of the reading. */
static void mtram_ds_convert_end(unsigned i)
{
    mtram_ds_t *d = &mtram_ds[i];
    uint16_t raw = (uint16_t)mtram_ds_raw[i] & (uint16_t)~((1u << (12u - mtram_ds_bits(d))) - 1u);
    d->scratch[0] = (uint8_t)raw;
    d->scratch[1] = (uint8_t)(raw >> 8);
    mtram_ds_seal(d);
    d->converting = false;
}

static void mtram_ds_settle(unsigned i)
{
    if (mtram_ds[i].converting && (int32_t)(mtram_cycle - mtram_ds[i].done) >= 0)
        mtram_ds_convert_end(i);
}

static uint32_t mtram_ow_cycles(uint32_t us)
{
    uint64_t c = (uint64_t)us * phi2_get_khz_run() / 1000u;
    return c ? (uint32_t)c : 1u;
}

/* True when a device answers with its presence pulse. */
static bool mtram_ow_reset(uint32_t *us)
{
    *us += MTRAM_OW_RESET_US;
    for (unsigned i = 0; i < mtram_ds_count; i++)
    {
        mtram_ds_settle(i);
        mtram_ds[i].state = MTRAM_OW_ROMCMD;
        mtram_ds[i].pos = 0;
    }
    return mtram_ds_count != 0;
}

static uint8_t mtram_ds_out(unsigned i)
{
    mtram_ds_t *d = &mtram_ds[i];
    if (d->state == MTRAM_OW_SEND)
    {
        uint8_t rom[8];
        if (!d->send_rom)
            return d->pos < 9 ? d->scratch[d->pos] : 0xFF;
        mtram_ds_rom(i, rom);
        return d->pos < 8 ? rom[d->pos] : 0xFF;
    }
    if (d->state == MTRAM_OW_CONVERT)
    {
        mtram_ds_settle(i);
        return d->converting ? 0x00 : 0xFF;
    }
    return 0xFF;
}

/* Search ROM and Alarm Search answer slot by slot, which a byte cannot ask
 * for, so the device leaves the transaction as for any command it lacks. */
static void mtram_ds_in(unsigned i, uint8_t line)
{
    mtram_ds_t *d = &mtram_ds[i];
    uint8_t rom[8];
    switch (d->state)
    {
    case MTRAM_OW_ROMCMD:
        d->pos = 0;
        d->state = line == 0x33   ? MTRAM_OW_SEND
                   : line == 0x55 ? MTRAM_OW_MATCH
                   : line == 0xCC ? MTRAM_OW_FUNC
                                  : MTRAM_OW_IDLE;
        d->send_rom = true;
        break;
    case MTRAM_OW_MATCH:
        mtram_ds_rom(i, rom);
        if (line != rom[d->pos])
            d->state = MTRAM_OW_IDLE;
        else if (++d->pos == 8)
            d->state = MTRAM_OW_FUNC;
        break;
    case MTRAM_OW_FUNC:
        d->pos = 0;
        switch (line)
        {
        case 0x44: /* Convert T: 93.75 ms at 9 bits, twice that for each more */
            d->converting = true;
            d->done = mtram_cycle + mtram_ow_cycles(93750u << (mtram_ds_bits(d) - 9u));
            d->state = MTRAM_OW_CONVERT;
            break;
        case 0xBE: /* Read Scratchpad */
            mtram_ds_settle(i);
            d->send_rom = false;
            d->state = MTRAM_OW_SEND;
            break;
        case 0x4E: /* Write Scratchpad */
            d->state = MTRAM_OW_WRITE;
            break;
        case 0x48: /* Copy Scratchpad */
            memcpy(d->eeprom, &d->scratch[2], sizeof d->eeprom);
            d->state = MTRAM_OW_DONE;
            break;
        case 0xB8: /* Recall E2 */
            memcpy(&d->scratch[2], d->eeprom, sizeof d->eeprom);
            mtram_ds_seal(d);
            d->state = MTRAM_OW_DONE;
            break;
        case 0xB4: /* Read Power Supply: powered from VDD, so ones */
            d->state = MTRAM_OW_DONE;
            break;
        default:
            d->state = MTRAM_OW_IDLE;
        }
        break;
    case MTRAM_OW_SEND:
        if (d->pos < (d->send_rom ? 8 : 9))
            d->pos++;
        break;
    case MTRAM_OW_WRITE:
        /* Only the resolution bits of the configuration byte can change. */
        d->scratch[2 + d->pos] = d->pos == 2 ? (uint8_t)((line & 0x60) | 0x1F) : line;
        mtram_ds_seal(d);
        if (++d->pos == 3)
            d->state = MTRAM_OW_IDLE;
        break;
    }
}

/* Eight slots: the line is the master's byte ANDed with every device's. */
static uint8_t mtram_ow_byte(uint8_t master, uint32_t *us)
{
    uint8_t line = master;
    *us += 8 * MTRAM_OW_SLOT_US;
    for (unsigned i = 0; i < mtram_ds_count; i++)
        line &= mtram_ds_out(i);
    for (unsigned i = 0; i < mtram_ds_count; i++)
        mtram_ds_in(i, line);
    return line;
}

/* One pass of the module's search: a reset, Search ROM, then every bit of the
 * code, the way the last pass went up to where it turned, 1 there and 0 past
 * it. 0 with the code in work and the last 0 taken in last_zero, or the
 * OW_ST_* bits of a pass that found nothing. The device found is left
 * addressed, as on the line. */
static uint8_t mtram_ow_search(uint8_t work[8], uint8_t last_disc, uint8_t *last_zero,
                               uint32_t *us)
{
    bool in[MTRAM_OW_DEVICES];
    uint8_t rom[MTRAM_OW_DEVICES][8];
    if (!mtram_ow_reset(us))
        return MTRAM_OW_ST_NOPRESENCE | MTRAM_OW_ST_END;
    *us += 8 * MTRAM_OW_SLOT_US;
    for (unsigned i = 0; i < mtram_ds_count; i++)
    {
        in[i] = true;
        mtram_ds_rom(i, rom[i]);
        mtram_ds[i].state = MTRAM_OW_IDLE;
    }
    *last_zero = 0;
    for (unsigned bit = 0; bit < 64; bit++)
    {
        bool any0 = false, any1 = false, dir;
        uint8_t mask = (uint8_t)(1u << (bit & 7));
        uint8_t *byte = &work[bit >> 3];
        unsigned n = bit + 1;
        *us += 3 * MTRAM_OW_SLOT_US;
        for (unsigned i = 0; i < mtram_ds_count; i++)
            if (in[i])
            {
                if (rom[i][bit >> 3] & mask)
                    any1 = true;
                else
                    any0 = true;
            }
        if (!any0 && !any1)
            return MTRAM_OW_ST_END;
        if (any0 && any1)
        {
            if (n < last_disc)
                dir = (*byte & mask) != 0;
            else
                dir = n == last_disc;
            if (!dir)
                *last_zero = (uint8_t)n;
        }
        else
            dir = any1;
        if (dir)
            *byte |= mask;
        else
            *byte &= (uint8_t)~mask;
        for (unsigned i = 0; i < mtram_ds_count; i++)
            if (in[i] && ((rom[i][bit >> 3] & mask) != 0) != dir)
                in[i] = false;
    }
    for (unsigned i = 0; i < mtram_ds_count; i++)
        if (in[i])
            mtram_ds[i].state = MTRAM_OW_FUNC;
    return 0;
}

/* The module's measurement: a search for every DS18B20 with a good CRC, at
 * most DS_MAX in 2 * DS_MAX passes; Skip ROM, Convert T and the 750 ms wait;
 * then Match ROM and Read Scratchpad for each, a temperature only from a
 * scratchpad with a good CRC and a configuration byte. */
static void mtram_ow_ds_begin(void)
{
    uint32_t us = 0;
    uint8_t work[8] = {0}, last_disc = 0, last_zero;
    unsigned passes = 0;
    mtram_ow.ds_count = 0;
    while (!mtram_ow_search(work, last_disc, &last_zero, &us))
    {
        if (work[0] == MTRAM_DS_FAMILY && !mtram_ow_crc(work, 8))
            memcpy(mtram_ow.ds_rom[mtram_ow.ds_count++], work, 8);
        last_disc = last_zero;
        if (!last_disc || mtram_ow.ds_count == MTRAM_DS_MAX ||
            ++passes == MTRAM_DS_SEARCH_PASSES)
            break;
    }
    if (mtram_ow.ds_count)
    {
        mtram_ow_reset(&us);
        mtram_ow_byte(0xCC, &us);
        mtram_ow_byte(0x44, &us);
        us += MTRAM_DS_CONVERT_US;
        for (unsigned i = 0; i < mtram_ds_count; i++)
            if (mtram_ds[i].converting)
                mtram_ds_convert_end(i);
    }
    for (unsigned s = 0; s < mtram_ow.ds_count; s++)
    {
        uint8_t pad[9];
        uint16_t value = MTRAM_DS_TEMP_NONE;
        mtram_ow_reset(&us);
        mtram_ow_byte(0x55, &us);
        for (unsigned k = 0; k < 8; k++)
            mtram_ow_byte(mtram_ow.ds_rom[s][k], &us);
        mtram_ow_byte(0xBE, &us);
        for (unsigned k = 0; k < 9; k++)
            pad[k] = mtram_ow_byte(0xFF, &us);
        if (!mtram_ow_crc(pad, 9) && pad[4])
        {
            int16_t raw = (int16_t)(pad[0] | (pad[1] << 8));
            value = (uint16_t)(raw * 25 / 4);
        }
        mtram_ow.ds_temp[s] = value;
    }
    mtram_ow.job = MTRAM_OW_JOB_DS;
    mtram_ow.due = mtram_cycle + mtram_ow_cycles(us);
}

/* An operation the module does not have is answered after MTRAM_OW_BADOP_US
 * with the line untouched. */
static void mtram_ow_mb_begin(uint8_t op)
{
    uint32_t us = 0;
    mtram_ow.mb_status = 0;
    mtram_ow.mb_len = 0;
    if (op == MTRAM_OW_OP_FIRST || op == MTRAM_OW_OP_NEXT)
    {
        if (op == MTRAM_OW_OP_FIRST)
        {
            mtram_ow.mb_last_disc = 0;
            mtram_ow.mb_end = false;
        }
        if (mtram_ow.mb_end)
            mtram_ow.mb_status = mtram_ow_reset(&us)
                                     ? MTRAM_OW_ST_END
                                     : MTRAM_OW_ST_NOPRESENCE | MTRAM_OW_ST_END;
        else
        {
            uint8_t work[8], last_zero;
            memcpy(work, mtram_ow.mb_rom, 8);
            mtram_ow.mb_status = mtram_ow_search(work, mtram_ow.mb_last_disc, &last_zero, &us);
            if (!mtram_ow.mb_status)
            {
                memcpy(mtram_ow.mb_rom, work, 8);
                memcpy(mtram_ow.mb_buf, work, 8);
                mtram_ow.mb_len = 8;
                mtram_ow.mb_last_disc = last_zero;
                mtram_ow.mb_end = !last_zero;
            }
        }
    }
    else if (op == MTRAM_OW_OP_TRANSACT)
    {
        unsigned tx = sram[MTRAM_REG_XFER_TXLEN], rx = sram[MTRAM_REG_XFER_RXLEN];
        if (tx > MTRAM_XFER_BUF_MAX)
            tx = MTRAM_XFER_BUF_MAX;
        if (rx > MTRAM_XFER_BUF_MAX)
            rx = MTRAM_XFER_BUF_MAX;
        if (!((sram[MTRAM_REG_XFER_FLAGS] & MTRAM_OW_FLAG_NORESET) && (tx || rx)) &&
            !mtram_ow_reset(&us))
            mtram_ow.mb_status = MTRAM_OW_ST_NOPRESENCE;
        else
        {
            for (unsigned i = 0; i < tx + rx; i++)
            {
                uint8_t got = mtram_ow_byte(i < tx ? sram[MTRAM_REG_XFER_BUF + i] : 0xFF, &us);
                if (i >= tx)
                    mtram_ow.mb_buf[i - tx] = got;
            }
            mtram_ow.mb_len = (uint8_t)rx;
        }
    }
    else
    {
        mtram_ow.mb_status = MTRAM_XFER_ST_BADOP;
        us = MTRAM_OW_BADOP_US;
    }
    mtram_ow.job = MTRAM_OW_JOB_MB;
    mtram_ow.due = mtram_cycle + mtram_ow_cycles(us);
}

static void mtram_ow_finish(void)
{
    if (mtram_ow.job == MTRAM_OW_JOB_DS)
    {
        for (unsigned s = 0; s < mtram_ow.ds_count; s++)
        {
            mtram_store16((uint16_t)(MTRAM_REG_DS_TEMP + 2 * s), (int16_t)mtram_ow.ds_temp[s]);
            memcpy(&sram[MTRAM_REG_DS_ROM + 8 * s], mtram_ow.ds_rom[s], 8);
        }
        sram[MTRAM_REG_DS_COUNT] = mtram_ow.ds_count;
        sram[MTRAM_REG_DS_CTRL] = 0;
        mtram_ow.job = MTRAM_OW_JOB_NONE;
        if (mtram_ow.mb_waiting)
        {
            uint8_t op = mtram_ow.mb_waiting;
            mtram_ow.mb_waiting = 0;
            mtram_ow_mb_begin(op);
        }
        return;
    }
    memcpy(&sram[MTRAM_REG_XFER_BUF], mtram_ow.mb_buf, mtram_ow.mb_len);
    sram[MTRAM_REG_XFER_STATUS] = mtram_ow.mb_status;
    sram[MTRAM_REG_XFER_CTRL] = 0;
    mtram_ow.job = MTRAM_OW_JOB_NONE;
    if (mtram_ow.ds_waiting)
    {
        mtram_ow.ds_waiting = false;
        mtram_ow_ds_begin();
    }
}

static void mtram_ow_ds_request(uint8_t data)
{
    if (!data || mtram_ow.ds_waiting)
        return;
    if (mtram_ow.job == MTRAM_OW_JOB_NONE)
        mtram_ow_ds_begin();
    else if (mtram_ow.job == MTRAM_OW_JOB_MB)
        mtram_ow.ds_waiting = true;
}

static void mtram_ow_mb_request(uint8_t data)
{
    if (!data || mtram_ow.mb_waiting)
        return;
    if (mtram_ow.job == MTRAM_OW_JOB_NONE)
        mtram_ow_mb_begin(data);
    else if (mtram_ow.job == MTRAM_OW_JOB_DS)
        mtram_ow.mb_waiting = data;
}

static void mtram_ow_power_on(void)
{
    memset(&mtram_ow, 0, sizeof mtram_ow);
    for (unsigned i = 0; i < MTRAM_OW_DEVICES; i++)
        mtram_ds_power_on(&mtram_ds[i]);
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
        break;
    case '-':
        mtram_reset_all_maps();
        mtram_gate = false;
        break;
    case 'T':
    case 'C':
        mtram_publish_temperature();
        break;
    }
}

static bool mtram_is_control(uint16_t addr)
{
    return (addr >= MTRAM_REG_BANK_BASE && addr <= MTRAM_REG_EDIT) ||
           addr == MTRAM_REG_DS_CTRL || addr == MTRAM_REG_XFER_CTRL ||
           (addr >= MTRAM_REG_INFO_FIRST && addr <= MTRAM_REG_WINDOW_LAST);
}

static void mtram_control(uint16_t addr, uint8_t data)
{
    if (addr >= MTRAM_REG_INFO_FIRST)
    {
        mtram_signature_write();
        return;
    }
    if (addr == MTRAM_REG_KNOCK)
    {
        mtram_knock_feed(data);
        return;
    }
    /* Neither touches a map, so the gate does not hold them back. */
    if (addr == MTRAM_REG_DS_CTRL)
    {
        mtram_ow_ds_request(data);
        return;
    }
    if (addr == MTRAM_REG_XFER_CTRL)
    {
        mtram_ow_mb_request(data);
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
    mtram_active_task = 0;
    mtram_edit_task = 0;
    mtram_knock_pos = 0;
    mtram_ow_power_on();
    sram[MTRAM_REG_RESETS] = mtram_resets;
    mtram_publish_temperature();
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
        if (mtram_is_control(addr))
            mtram_post(addr, *data);
    }
    else if (addr <= MTRAM_REG_WINDOW_LAST)
        *data = *p;
    while (mtram_queue_count &&
           (int32_t)(mtram_cycle - mtram_queue[mtram_queue_head].due) >= 0)
        mtram_queue_pop();
    if (mtram_ow.job && (int32_t)(mtram_cycle - mtram_ow.due) >= 0)
        mtram_ow_finish();
}

uint8_t *mtram_view(uint16_t addr)
{
    return &mtram_slot[addr >> MTRAM_BLOCK_SHIFT][addr & (MTRAM_BLOCK_SIZE - 1)];
}

/* RESB is asserted again on every stop, so only the falling edge counts, as
 * the module's debounced tap does. Memory survives; the maps reset, and the
 * count and the signature are written in the window. The maps reset with the
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
    mtram_active_task = 0;
    mtram_edit_task = 0;
    mtram_knock_pos = 0;
    if (!mtram_on)
        return;
    sram[MTRAM_REG_RESETS] = ++mtram_resets;
    mtram_signature_write();
}

void mtram_power_on(void)
{
    mtram_resets = 0;
    mtram_resb_low = false;
}

/* The module republishes its temperature once a second, which puts back what
 * a program wrote over it. A conversion is settled here too, long before its
 * end cycle could wrap. */
void mtram_task(void)
{
    if (!mtram_on)
        return;
    for (unsigned i = 0; i < mtram_ds_count; i++)
        mtram_ds_settle(i);
    if (mtram_cycle - mtram_temp_cycle >= (uint32_t)phi2_get_khz_run() * 1000u)
    {
        mtram_temp_cycle = mtram_cycle;
        mtram_publish_temperature();
    }
}

void mtram_dbg_get(mtram_dbg_t *out)
{
    out->gate = mtram_gate;
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
    for (unsigned i = 0; i < MTRAM_OW_DEVICES; i++)
    {
        const mtram_ds_t *d = &mtram_ds[i];
        sst_put_u8(c, d->state);
        sst_put_u8(c, d->pos);
        sst_put_bool(c, d->send_rom);
        sst_put_bool(c, d->converting);
        sst_put(c, d->scratch, sizeof d->scratch);
        sst_put(c, d->eeprom, sizeof d->eeprom);
        sst_put_u32(c, d->done);
    }
    sst_put_u8(c, mtram_ow.job);
    sst_put_u32(c, mtram_ow.due);
    sst_put_bool(c, mtram_ow.ds_waiting);
    sst_put_u8(c, mtram_ow.mb_waiting);
    sst_put_u8(c, mtram_ow.mb_last_disc);
    sst_put(c, mtram_ow.mb_rom, sizeof mtram_ow.mb_rom);
    sst_put_bool(c, mtram_ow.mb_end);
    sst_put_u8(c, mtram_ow.ds_count);
    for (unsigned s = 0; s < MTRAM_DS_MAX; s++)
        sst_put_u16(c, mtram_ow.ds_temp[s]);
    sst_put(c, mtram_ow.ds_rom, sizeof mtram_ow.ds_rom);
    sst_put_u8(c, mtram_ow.mb_status);
    sst_put_u8(c, mtram_ow.mb_len);
    sst_put(c, mtram_ow.mb_buf, sizeof mtram_ow.mb_buf);
}

/* The devices' line state and the line's job. Which devices are on the line
 * is the session's, as the core temperature is. */
static bool mtram_ow_sst_load(sst_cursor_t *c, mtram_ds_t ds[MTRAM_OW_DEVICES], mtram_ow_t *ow)
{
    for (unsigned i = 0; i < MTRAM_OW_DEVICES; i++)
    {
        mtram_ds_t *d = &ds[i];
        d->state = sst_get_u8(c);
        d->pos = sst_get_u8(c);
        d->send_rom = sst_get_bool(c);
        d->converting = sst_get_bool(c);
        sst_get(c, d->scratch, sizeof d->scratch);
        sst_get(c, d->eeprom, sizeof d->eeprom);
        d->done = sst_get_u32(c);
        if (d->state >= MTRAM_OW_STATES || d->pos > 9)
            return false;
    }
    ow->job = sst_get_u8(c);
    ow->due = sst_get_u32(c);
    ow->ds_waiting = sst_get_bool(c);
    ow->mb_waiting = sst_get_u8(c);
    ow->mb_last_disc = sst_get_u8(c);
    sst_get(c, ow->mb_rom, sizeof ow->mb_rom);
    ow->mb_end = sst_get_bool(c);
    ow->ds_count = sst_get_u8(c);
    for (unsigned s = 0; s < MTRAM_DS_MAX; s++)
        ow->ds_temp[s] = sst_get_u16(c);
    sst_get(c, ow->ds_rom, sizeof ow->ds_rom);
    ow->mb_status = sst_get_u8(c);
    ow->mb_len = sst_get_u8(c);
    sst_get(c, ow->mb_buf, sizeof ow->mb_buf);
    return ow->job <= MTRAM_OW_JOB_MB && ow->mb_last_disc <= 64 &&
           ow->ds_count <= MTRAM_DS_MAX && ow->mb_len <= MTRAM_XFER_BUF_MAX;
}

static bool mtram_map_valid(const uint8_t *map)
{
    for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
        if ((s == MTRAM_PINNED_SLOT) != (map[s] == MTRAM_PINNED_SLOT))
            return false;
    return true;
}

/* The blocks go straight into mtram_ext, as sram's table does with sram[]; a
 * refused load is followed by sys_reset. */
bool mtram_sst_load(sst_cursor_t *c, unsigned flags)
{
    (void)flags;
    uint8_t active_map[MTRAM_SLOTS];
    uint8_t task_map[MTRAM_TASKS][MTRAM_SLOTS];
    mtram_event_t queue[MTRAM_QUEUE_LEN];
    mtram_ds_t ds[MTRAM_OW_DEVICES];
    mtram_ow_t ow;
    sst_get(c, mtram_ext, sizeof mtram_ext);
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
    for (unsigned i = 0; i < MTRAM_QUEUE_LEN; i++)
    {
        queue[i].due = sst_get_u32(c);
        queue[i].addr = sst_get_u16(c);
        queue[i].data = sst_get_u8(c);
    }
    bool ow_valid = mtram_ow_sst_load(c, ds, &ow);
    if (!sst_ok(c) || !ow_valid || !mtram_map_valid(active_map) ||
        active_task >= MTRAM_TASKS || edit_task >= MTRAM_TASKS ||
        knock_pos > sizeof mtram_knock_prefix || queue_count > MTRAM_QUEUE_LEN)
        return false;
    for (unsigned t = 0; t < MTRAM_TASKS; t++)
        if (!mtram_map_valid(task_map[t]))
            return false;
    for (unsigned i = 0; i < queue_count; i++)
    {
        if (!mtram_is_control(queue[(uint8_t)(queue_head + i)].addr))
            return false;
    }
    for (uint8_t s = 0; s < MTRAM_SLOTS; s++)
        mtram_map_slot(s, active_map[s]);
    memcpy(mtram_task_map, task_map, sizeof mtram_task_map);
    mtram_gate = gate;
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
    memcpy(mtram_ds, ds, sizeof mtram_ds);
    mtram_ow = ow;
    return true;
}

#define MTRAM_SST(X)                                                        \
    X(FN, mtram_sst_save, mtram_sst_load, MTRAM_SST_SIZE, 4)                \
    X(IN_FN, mtram_ext)                                                     \
    X(IN_FN, mtram_active_map)                                              \
    X(IN_FN, mtram_task_map)                                                \
    X(IN_FN, mtram_gate)                                                    \
    X(IN_FN, mtram_active_task)                                             \
    X(IN_FN, mtram_edit_task)                                               \
    X(IN_FN, mtram_knock_pos)                                               \
    X(IN_FN, mtram_resets)                                                  \
    X(IN_FN, mtram_resb_low)                                                \
    X(IN_FN, mtram_cycle)                                                   \
    X(IN_FN, mtram_temp_cycle)                                              \
    X(IN_FN, mtram_queue)                                                   \
    X(IN_FN, mtram_queue_head)                                              \
    X(IN_FN, mtram_queue_count)                                             \
    X(IN_FN, mtram_ds)                                                      \
    X(IN_FN, mtram_ow)                                                      \
    X(SKIP, mtram_slot, "the load rebuilds it from the active map")         \
    X(SKIP, mtram_on, "a session's mtRAM setting")                          \
    X(SKIP, mtram_temp, "a session's mtRAM setting")                        \
    X(SKIP, mtram_delay, "a session's mtRAM setting")                       \
    X(SKIP, mtram_ds_count, "a session's mtRAM setting")                    \
    X(SKIP, mtram_ds_raw, "a session's mtRAM setting")
SST_TABLE(mtram, MTRAM_SST)
