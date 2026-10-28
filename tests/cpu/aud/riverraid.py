#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""An eight-bar loop, 96 BPM unless --bpm says otherwise, in the manner of
an 80s arcade flight shooter: PCM drums under OPL2 or PSG music. The main
drum bar streams from 6502 RAM and the fill that closes every eighth bar
from XRAM, both into the PCM ring. The sequencer steps every sixteenth of
PCM actually played, counted from read_ptr. Space switches the music
between the OPL2 and the PSG while the drums carry on; Q exits."""

import argparse
import math

from rp6502_asm import (API_OP, RIA_READY, RIA_RX, RW0_ADDR, RW0_DATA,
                        RW0_STEP, RW1_ADDR, RW1_DATA, RW1_STEP, Asm, putc)
from rp6502_rom import main

RATE = 16000
PCM_PEAK = 56

PCM_SRC = 0x4000

XPCM = 0x0000
XRING = XPCM + 8
RING_LOG2 = 13
XFILL = 0x2100
XOPL = 0xF000
XPSG = 0xF100
assert XRING + (1 << RING_LOG2) <= XFILL

# A bar is a whole number of pages, because the streamer copies pages, so
# the tempo is 240 * RATE / (pages * 256): 15000 / pages at 16 kHz. The most
# pages are what the main bar's RAM and the fill's XRAM both hold.
MAX_PAGES = min((0xFF00 - PCM_SRC) // 256, (XOPL - XFILL) // 256)
MAX_BPM = 200


def set_tempo(bpm):
    """The bar nearest bpm; returns the tempo it actually is."""
    global BAR, SIXTEENTH, PCM_PAGES, BPM
    per_page = 240 * RATE / 256
    PCM_PAGES = round(per_page / bpm)
    if not per_page / MAX_BPM <= PCM_PAGES <= MAX_PAGES:
        raise ValueError(f"{bpm} BPM is outside "
                         f"{per_page / MAX_PAGES:.1f}..{MAX_BPM}")
    BAR = PCM_PAGES * 256
    SIXTEENTH = BAR // 16
    BPM = per_page / PCM_PAGES
    return BPM


# 96 BPM exactly is 40000 samples a bar, which is not a whole number of
# pages; 156 pages is 96.15 BPM and divides into sixteenths of 2496.
set_tempo(96)

TABLES = 0x1000
BARS = 8
STEPS = BARS * 16
MODE_PSG = STEPS
assert MODE_PSG + STEPS <= 256 and MODE_PSG & (MODE_PSG - 1) == 0

ZP_WPH = 0x10
ZP_SRC = 0x11
ZP_RPL = 0x13
ZP_RPH = 0x14
ZP_LRPL = 0x15
ZP_LRPH = 0x16
ZP_POSL = 0x17
ZP_POSH = 0x18
ZP_STEP = 0x19
ZP_PTR = 0x1A
ZP_DL = 0x1C
ZP_DH = 0x1D
ZP_MODE = 0x1E  # 0 for the OPL2, MODE_PSG for the PSG
ZP_PAGE = 0x1F
ZP_TMP = 0x20
ZP_WPG = 0x21
ZP_WBAR = 0x22


class Lcg:
    def __init__(self, seed=1):
        self.x = seed

    def __call__(self):
        self.x = (self.x * 1103515245 + 12345) & 0x7FFFFFFF
        return self.x / 0x40000000 - 1.0


def drum_bars():
    """The main bar and the fill, normalised together."""
    noise = Lcg(0x6502)

    def kick(buf, at, level=1.0):
        phase = 0.0
        for i in range(min(3000, BAR - at)):
            t = i / RATE
            phase += 2 * math.pi * (45 + 115 * math.exp(-t / 0.025)) / RATE
            click = noise() * 0.3 if i < 40 else 0.0
            buf[at + i] += (math.sin(phase) * math.exp(-t / 0.13) + click) * level

    def snare(buf, at, level=1.0):
        """Gated: the noise holds for 120 ms and is then cut in 5 ms."""
        for i in range(min(2080, BAR - at)):
            t = i / RATE
            gate = 1.0 if t < 0.12 else max(0.0, 1.0 - (t - 0.12) / 0.005)
            buf[at + i] += (noise() * 0.8 * math.exp(-t / 0.25) * gate +
                            math.sin(2 * math.pi * 200 * t) * 0.5 *
                            math.exp(-t / 0.05)) * level

    def hat(buf, at, level, tau, length):
        prev = 0.0
        for i in range(min(length, BAR - at)):
            n = noise()
            buf[at + i] += (n - prev) * level * math.exp(-i / RATE / tau)
            prev = n

    def tom(buf, at, f):
        phase = 0.0
        for i in range(min(2400, BAR - at)):
            t = i / RATE
            phase += 2 * math.pi * f * (1 + 0.3 * math.exp(-t / 0.04)) / RATE
            buf[at + i] += math.sin(phase) * 0.9 * math.exp(-t / 0.15)

    main = [0.0] * BAR
    for s in range(16):
        at = s * SIXTEENTH
        if s in (0, 6, 8, 10):
            kick(main, at)
        if s in (4, 12):
            snare(main, at)
        if s == 14:
            hat(main, at, 0.22, 0.08, 2000)
        else:
            hat(main, at, 0.25 if s % 2 == 0 else 0.14, 0.012, 500)

    fill = [0.0] * BAR
    for s in range(16):
        at = s * SIXTEENTH
        if s in (0, 15):
            kick(fill, at)
        if s == 4:
            snare(fill, at)
        if 8 <= s <= 11:
            snare(fill, at, 0.6 + 0.13 * (s - 8))
        if s in (12, 13, 14):
            tom(fill, at, (220, 165, 120)[s - 12])
        if s < 8:
            hat(fill, at, 0.25 if s % 2 == 0 else 0.14, 0.012, 500)

    peak = max(abs(v) for v in main + fill)

    def pcm(buf):
        return bytes(max(-128, min(127, round(v / peak * PCM_PEAK))) & 0xFF
                     for v in buf)

    return pcm(main), pcm(fill)


# The song, in E minor. Each bar: the bass root, the power chord the stabs
# play, and the triad the arpeggio walks.
SONG = (
    (40, (52, 59, 64), (64, 67, 71)),  # Em
    (36, (48, 55, 60), (60, 64, 67)),  # C
    (43, (55, 62, 67), (67, 71, 74)),  # G
    (38, (50, 57, 62), (62, 66, 69)),  # D
    (40, (52, 59, 64), (64, 67, 71)),  # Em
    (36, (48, 55, 60), (60, 64, 67)),  # C
    (38, (50, 57, 62), (62, 66, 69)),  # D
    (35, (47, 54, 59), (59, 63, 66)),  # B
)
assert len(SONG) == BARS

REST = None

# (step in the bar, note or REST) for each bar; a note holds until the
# next event.
LEAD = (
    ((0, 71), (4, 76), (8, 78), (12, 79)),
    ((0, 79), (10, 78), (12, 76)),
    ((0, 74), (8, 71), (12, 74)),
    ((0, 78), (8, 76), (12, 74), (14, REST)),
    ((0, 76), (4, 79), (8, 83), (12, 81)),
    ((0, 79), (8, 76), (12, 79)),
    ((0, 81), (8, 78), (12, 74)),
    ((0, 75), (4, 78), (8, 83), (15, REST)),
)
assert len(LEAD) == BARS

BASS_STEPS = {0: 0, 2: 0, 4: 0, 6: 0, 7: 12, 8: 0, 10: 0, 12: 0, 14: 0, 15: 12}
STAB_STEPS = (0, 6, 12)
ARP_LINE = (0, 1, 2, 3, 2, 1, 0, 1) * 2  # 3 is the root an octave up
CRASH_NOTE = 96

CH_BASS, CH_STAB, CH_LEAD, CH_ARP, CH_CRASH = 0, 1, 4, 5, 6


def lead_events():
    """Step of the whole loop to the lead's event there."""
    ev = {}
    for bar, events in enumerate(LEAD):
        for at, note in events:
            ev[bar * 16 + at] = note
    return ev


LEAD_EV = lead_events()


def lead_at(s):
    """The lead note sounding at step s, or REST."""
    for k in range(s, s - STEPS, -1):
        if k % STEPS in LEAD_EV:
            return LEAD_EV[k % STEPS]
    return REST


def fnum(note):
    f = 440.0 * 2 ** ((note - 69) / 12)
    for block in range(8):
        n = round(f * 2 ** (20 - block) / 49716)
        if n < 1024:
            return block, n
    raise ValueError(note)


MOD = (0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10)

# (0x20, 0x40, 0x60, 0x80) for modulator then carrier, and 0xC0.
OPL_BASS = ((0x01, 0x1A, 0xF5, 0x46), (0x01, 0x08, 0xF4, 0x47), 0x0C)
OPL_STAB = ((0x21, 0x1C, 0xD3, 0x25), (0x01, 0x14, 0xD4, 0x36), 0x0A)
OPL_LEAD = ((0x61, 0x1E, 0xF2, 0x14), (0x61, 0x0E, 0xF2, 0x15), 0x0C)
OPL_ARP = ((0x02, 0x24, 0xF6, 0x88), (0x01, 0x22, 0xF5, 0x86), 0x06)
OPL_CRASH = ((0x0F, 0x00, 0xF2, 0x05), (0x0E, 0x10, 0xF3, 0x05), 0x0E)
OPL_VOICES = (OPL_BASS, OPL_STAB, OPL_STAB, OPL_STAB, OPL_LEAD, OPL_ARP,
              OPL_CRASH)


def opl_init():
    out = [0xBD, 0x40]  # vibrato depth 14 cents
    for ch, (mod, car, fb) in enumerate(OPL_VOICES):
        for op, regs in ((MOD[ch], mod), (MOD[ch] + 3, car)):
            for base, val in zip((0x20, 0x40, 0x60, 0x80), regs):
                out += (base + op, val)
        out += (0xC0 + ch, fb)
    return out


def opl_hi(note):
    block, n = fnum(note)
    return (block << 2) | (n >> 8), n & 0xFF


def opl_note(ch, note, prev=REST):
    """A key-off then a key-on, so a held note starts its envelope again.
    REST is the key-off alone, written with the note it releases."""
    if note is REST:
        if prev is REST:
            return []
        return [0xB0 + ch, opl_hi(prev)[0]]
    hi, lo = opl_hi(note)
    return [0xB0 + ch, hi, 0xA0 + ch, lo, 0xB0 + ch, 0x20 | hi]


# (wave_release, duty, vol_attack, vol_decay, pan) per channel. Volume 0 is
# the loudest, and a pan byte of 0x80 would drop the channel from the mix.
PSG_VOICES = (
    (0x23, 0xFF, 0x50, 0x85, 0x00),  # bass, saw
    (0x14, 0x80, 0xA0, 0xD4, 0xD0),  # stabs, square
    (0x14, 0x80, 0xA0, 0xD4, 0x00),
    (0x14, 0x80, 0xA0, 0xD4, 0x30),
    (0x25, 0xFF, 0x71, 0x73, 0x00),  # lead, saw
    (0x12, 0x40, 0xB0, 0xE3, 0x20),  # arpeggio, square 25%
    (0x49, 0xFF, 0x90, 0xF9, 0x00),  # crash, noise
)


def psg_init():
    out = []
    for ch, (wr, duty, va, vd, pan) in enumerate(PSG_VOICES):
        o = ch * 8
        out += (o + 2, duty, o + 3, va, o + 4, vd, o + 5, wr, o + 6, pan)
    return out


def psg_note(ch, note, prev=REST):
    """Gate off then on, which is what starts the attack. REST is the gate
    off alone."""
    o, pan = ch * 8, PSG_VOICES[ch][4]
    if note is REST:
        return [o + 6, pan] if prev is not REST else []
    f = round(440.0 * 2 ** ((note - 69) / 12) * 3)
    return [o + 6, pan, o + 0, f & 0xFF, o + 1, f >> 8, o + 6, pan | 1]


def stabs(note, bar):
    out = []
    for v, n in enumerate(SONG[bar][1]):
        out += note(CH_STAB + v, n)
    return out


def step(note, s):
    bar, i = divmod(s, 16)
    root, _, triad = SONG[bar]
    out = []
    if s == 0:
        out += note(CH_CRASH, CRASH_NOTE)
    if i in BASS_STEPS:
        out += note(CH_BASS, root + BASS_STEPS[i])
    if i in STAB_STEPS:
        out += stabs(note, bar)
    if s in LEAD_EV:
        out += note(CH_LEAD, LEAD_EV[s], lead_at(s - 1))
    a = ARP_LINE[i]
    out += note(CH_ARP, triad[0] + 12 if a == 3 else triad[a])
    return out


def resume(note, bar):
    """What a newly taken synth is given so the bar does not go hollow: the
    bar's stabs and the lead note it opens with."""
    out = stabs(note, bar)
    n = lead_at(bar * 16)
    if n is not REST:
        out += note(CH_LEAD, n)
    return out


def tables():
    """Lists of (offset in the synth's page, value) ending in $FF, then lo
    and hi pointer tables for each kind, OPL's entries before PSG's."""
    kinds = {
        "init": [opl_init(), psg_init()],
        "step": ([step(opl_note, s) for s in range(STEPS)] +
                 [step(psg_note, s) for s in range(STEPS)]),
        "resume": ([resume(opl_note, b) for b in range(BARS)] +
                   [resume(psg_note, b) for b in range(BARS)]),
    }
    blob, at = bytearray(), {}
    for kind, lists in kinds.items():
        at[kind] = []
        for lst in lists:
            assert len(lst) < 254 and 0xFF not in lst[::2]
            at[kind].append(TABLES + len(blob))
            blob += bytes(lst) + b"\xff"
    ptr = {}
    for kind, addrs in at.items():
        ptr[kind] = (TABLES + len(blob), TABLES + len(blob) + len(addrs))
        blob += bytes(a & 0xFF for a in addrs)
        blob += bytes(a >> 8 for a in addrs)
    assert TABLES + len(blob) <= PCM_SRC, f"tables end at ${TABLES + len(blob):04X}"
    return bytes(blob), ptr


def program(ptr):
    p = Asm()
    loop, exit_, key_done = p.local("loop"), p.local("exit"), p.local("key")

    p.store(RW0_STEP, 1)
    p.say(f"\r\nRIVER RAID {BPM:.0f} BPM - PCM drums + OPL2/PSG. "
          "Space switches, Q exits.\r\n")

    p.lda_imm(0)
    for zp in (ZP_WPH, ZP_SRC, ZP_LRPL, ZP_LRPH, ZP_POSL, ZP_POSH, ZP_STEP,
               ZP_MODE, ZP_WPG, ZP_WBAR):
        p.sta_zp(zp)
    p.jsr_abs("synth_start")

    for i, v in enumerate((0, 0, 0x03, RING_LOG2, RATE & 0xFF, RATE >> 8, 0, 0)):
        p.poke(XPCM + i, v)
    p.xreg(0, 1, 2, XPCM)
    p.jsr_abs("seq_step")

    p.symbol(loop)
    p.bit_abs(RIA_READY)
    p.bvc(key_done)
    p.lda_abs(RIA_RX)
    p.ora_imm(0x20)  # q and Q alike; a space stays a space
    p.cmp_imm(ord("q"))
    with p.branch("bne"):
        p.jmp_abs(exit_)
    p.cmp_imm(ord(" "))
    p.bne(key_done)
    p.jsr_abs("switch")
    p.symbol(key_done)
    p.jsr_abs("read_rp")

    # write_ptr is always on a page, so the fill's low byte is only a borrow.
    ring_pages = (1 << (RING_LOG2 - 8)) - 1
    p.sec()
    p.lda_imm(0)
    p.sbc_zp(ZP_RPL)
    p.lda_zp(ZP_WPH)
    p.sbc_zp(ZP_RPH)
    p.and_imm(ring_pages)
    p.cmp_imm(ring_pages)
    with p.branch("bcs"):
        p.jsr_abs("put_chunk")

    p.sec()
    p.lda_zp(ZP_RPL)
    p.sbc_zp(ZP_LRPL)
    p.sta_zp(ZP_DL)
    p.lda_zp(ZP_RPH)
    p.sbc_zp(ZP_LRPH)
    p.and_imm(ring_pages)
    p.sta_zp(ZP_DH)
    p.lda_zp(ZP_RPL)
    p.sta_zp(ZP_LRPL)
    p.lda_zp(ZP_RPH)
    p.sta_zp(ZP_LRPH)
    p.clc()
    p.lda_zp(ZP_POSL)
    p.adc_zp(ZP_DL)
    p.sta_zp(ZP_POSL)
    p.lda_zp(ZP_POSH)
    p.adc_zp(ZP_DH)
    p.sta_zp(ZP_POSH)

    p.lda_zp(ZP_POSL)
    p.cmp_imm(SIXTEENTH & 0xFF)
    p.lda_zp(ZP_POSH)
    p.sbc_imm(SIXTEENTH >> 8)
    with p.branch("bcs"):
        p.jmp_abs(loop)
    p.sec()
    p.lda_zp(ZP_POSL)
    p.sbc_imm(SIXTEENTH & 0xFF)
    p.sta_zp(ZP_POSL)
    p.lda_zp(ZP_POSH)
    p.sbc_imm(SIXTEENTH >> 8)
    p.sta_zp(ZP_POSH)
    p.jsr_abs("seq_step")
    p.jmp_abs(loop)

    p.symbol(exit_)
    p.store(API_OP, 0xFF)
    p.jmp_abs(p.here())

    # The RIA writes read_ptr a byte at a time, so the high byte is read on
    # both sides of the low one.
    p.symbol("read_rp")
    p.lda_imm(0)
    p.sta_abs(RW1_STEP)
    p.sta_abs(RW1_ADDR + 1)
    p.symbol("read_rp.again")
    p.store(RW1_ADDR, XPCM + 7)
    p.lda_abs(RW1_DATA)
    p.sta_zp(ZP_RPH)
    p.store(RW1_ADDR, XPCM + 6)
    p.lda_abs(RW1_DATA)
    p.sta_zp(ZP_RPL)
    p.store(RW1_ADDR, XPCM + 7)
    p.lda_abs(RW1_DATA)
    p.cmp_zp(ZP_RPH)
    p.bne("read_rp.again")
    p.rts()

    # The source follows what is being written, not what is being played:
    # the ring is a FIFO and both start at zero, so the fill comes out on
    # the sequencer's last bar. read_rp puts RW1 back to a step of zero.
    p.symbol("put_chunk")
    p.store(RW0_ADDR, XRING & 0xFF)
    p.lda_zp(ZP_WPH)
    p.clc()
    p.adc_imm(XRING >> 8)
    p.sta_abs(RW0_ADDR + 1)
    p.ldy_imm(0)
    p.lda_zp(ZP_WBAR)
    p.cmp_imm(BARS - 1)
    p.beq("put_chunk.fill")
    p.lda_zp(ZP_WPG)
    p.clc()
    p.adc_imm(PCM_SRC >> 8)
    p.sta_zp(ZP_SRC + 1)
    p.symbol("put_chunk.main")
    p.lda_izy(ZP_SRC)
    p.sta_abs(RW0_DATA)
    p.iny()
    p.bne("put_chunk.main")
    p.beq("put_chunk.next")
    p.symbol("put_chunk.fill")
    p.store(RW1_STEP, 1)
    p.store(RW1_ADDR, XFILL & 0xFF)
    p.lda_zp(ZP_WPG)
    p.clc()
    p.adc_imm(XFILL >> 8)
    p.sta_abs(RW1_ADDR + 1)
    p.symbol("put_chunk.copy")
    p.lda_abs(RW1_DATA)
    p.sta_abs(RW0_DATA)
    p.dey()
    p.bne("put_chunk.copy")
    p.symbol("put_chunk.next")
    p.inc_zp(ZP_WPG)
    p.lda_zp(ZP_WPG)
    p.cmp_imm(PCM_PAGES)
    with p.branch("bne"):
        p.store(ZP_WPG, 0)
        p.lda_zp(ZP_WBAR)
        p.clc()
        p.adc_imm(1)
        p.and_imm(BARS - 1)
        p.sta_zp(ZP_WBAR)
    p.lda_zp(ZP_WPH)
    p.clc()
    p.adc_imm(1)
    p.and_imm(ring_pages)
    p.sta_zp(ZP_WPH)
    p.store(RW0_ADDR, (XPCM + 1) & 0xFF)
    p.store(RW0_ADDR + 1, (XPCM + 1) >> 8)
    p.lda_zp(ZP_WPH)
    p.sta_abs(RW0_DATA)
    p.rts()

    def list_at(kind):
        """X indexes the kind's pointer tables."""
        lo, hi = ptr[kind]
        p.lda_abx(lo)
        p.sta_zp(ZP_PTR)
        p.lda_abx(hi)
        p.sta_zp(ZP_PTR + 1)

    def lsr(n):
        for _ in range(n):
            p.lsr_a()

    p.symbol("seq_step")
    p.lda_zp(ZP_STEP)
    p.clc()
    p.adc_zp(ZP_MODE)
    p.tax()
    list_at("step")
    p.jsr_abs("run_list")
    p.lda_zp(ZP_STEP)
    p.clc()
    p.adc_imm(1)
    p.and_imm(STEPS - 1)
    p.sta_zp(ZP_STEP)
    p.rts()

    # Taking one synth parks the other. The bar being played is resumed on
    # the new one, which would otherwise hold no stabs or lead until they
    # next strike.
    bar_shift = 4  # sixteen steps a bar
    p.symbol("switch")
    p.lda_zp(ZP_MODE)
    p.eor_imm(MODE_PSG)
    p.sta_zp(ZP_MODE)
    p.jsr_abs("synth_start")
    p.lda_zp(ZP_STEP)
    p.sec()
    p.sbc_imm(1)
    p.and_imm(STEPS - 1)
    lsr(bar_shift)
    p.sta_zp(ZP_TMP)
    p.lda_zp(ZP_MODE)
    lsr(bar_shift)
    p.clc()
    p.adc_zp(ZP_TMP)
    p.tax()
    list_at("resume")
    p.jmp_abs("run_list")

    p.symbol("synth_start")
    p.lda_zp(ZP_MODE)
    p.bne("synth_start.psg")
    p.xreg(0, 1, 1, XOPL)
    p.store(ZP_PAGE, XOPL >> 8)
    p.say("OPL2\r\n")
    p.jmp_abs("synth_start.init")
    p.symbol("synth_start.psg")
    p.xreg(0, 1, 0, XPSG)
    p.store(ZP_PAGE, XPSG >> 8)
    p.say("PSG\r\n")
    p.symbol("synth_start.init")
    p.lda_zp(ZP_MODE)
    lsr(MODE_PSG.bit_length() - 1)
    p.tax()
    list_at("init")
    p.jmp_abs("run_list")

    p.symbol("run_list")
    p.ldy_imm(0)
    p.symbol("run_list.next")
    p.lda_izy(ZP_PTR)
    p.cmp_imm(0xFF)
    with p.branch("bne"):
        p.rts()
    p.sta_abs(RW0_ADDR)
    p.lda_zp(ZP_PAGE)
    p.sta_abs(RW0_ADDR + 1)
    p.iny()
    p.lda_izy(ZP_PTR)
    p.sta_abs(RW0_DATA)
    p.iny()
    p.jmp_abs("run_list.next")

    p.use(putc)
    return p


def prog():
    blob, ptr = tables()
    p = program(ptr)
    assert p.here() <= TABLES, f"code ends at ${p.here():04X}"
    bar, fill = drum_bars()
    p.rom.add_binary_data(blob, TABLES)
    p.rom.add_binary_data(bar, PCM_SRC)
    p.rom.add_binary_data(fill, 0x10000 + XFILL)
    return p


# --emit <folder> is main's; --bpm may follow it.
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--bpm", type=float, default=96,
                help="tempo, rounded to a whole number of pages a bar")
try:
    set_tempo(ap.parse_known_args()[0].bpm)
except ValueError as e:
    ap.error(str(e))

ROMS = {"riverraid": prog}
main(globals())
