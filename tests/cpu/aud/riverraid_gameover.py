#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""The game-over jingle for riverraid.rp6502: four bars played once, a
falling E minor line on the OPL2 or the PSG over a PCM explosion and two
low booms, slowing through the last bar into a held chord.

The PCM is the jingle's whole timeline rendered here and cut into pages.
Silent and repeated pages are stored once, and a table of page numbers
plays them back in order, so ten seconds of 16 kHz fit in 6502 RAM. The
sequencer steps on PCM actually played, counted from read_ptr, and each
step has its own length, which is what makes the ritardando. Once the
jingle has rung out, Space plays it again on the other synth, R plays it
again and Q exits."""

import argparse
import math

from rp6502_asm import (API_OP, RIA_READY, RIA_RX, RW0_ADDR, RW0_DATA,
                        RW0_STEP, RW1_ADDR, RW1_DATA, RW1_STEP, Asm, putc)
from rp6502_rom import main

RATE = 16000
DEFAULT_BPM = 15000 / 156  # riverraid's 96.15, a sixteenth of 2496
MIN_BPM, MAX_BPM = 40, 240
PCM_PEAK = 72
TAIL = RATE * 3 // 2  # for the last chord's release

TABLES = 0x1000
PCM_SRC = 0x2000
PCM_TOP = 0xFF00

XPCM = 0x0000
XRING = XPCM + 8
RING_LOG2 = 13
XOPL = 0xF000
XPSG = 0xF100

BARS = 4
STEPS = BARS * 16
MODE_PSG = STEPS
assert MODE_PSG + STEPS < 256 and MODE_PSG & (MODE_PSG - 1) == 0

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
ZP_TPTR = 0x20
ZP_TLEFT = 0x22


def set_tempo(bpm):
    """Three bars in time at bpm, then the last one slowing to near twice
    as long a step. The jingle is a timeline rather than a loop of pages,
    so a sixteenth is any whole number of samples. Returns the tempo the
    rounding leaves."""
    global SIXTEENTH, LENGTHS, START, EXPLOSION_AT, BOOM_AT, FINAL_AT, BPM
    if not MIN_BPM <= bpm <= MAX_BPM:
        raise ValueError(f"{bpm} BPM is outside {MIN_BPM}..{MAX_BPM}")
    SIXTEENTH = round(RATE * 60 / (bpm * 4))
    LENGTHS = [SIXTEENTH] * 48
    for i in range(16):
        LENGTHS.append(round(SIXTEENTH * (1 + 0.9 * (i / 15) ** 2)))
    assert len(LENGTHS) == STEPS and max(LENGTHS) < 0x10000
    START = [sum(LENGTHS[:s]) for s in range(STEPS + 1)]
    # Where the PCM's three sounds start: the explosion with the first
    # note, a boom on the last bar and a longer one under the final chord.
    EXPLOSION_AT = START[0]
    BOOM_AT = START[48]
    FINAL_AT = START[56]
    BPM = RATE * 60 / (SIXTEENTH * 4)
    return BPM


set_tempo(DEFAULT_BPM)

REST = None

# Step to note, for each channel. A note holds until the channel's next
# event, and every channel is released when the jingle ends.
BASS = {0: 40, 8: 38, 16: 36, 24: 35, 32: 33, 40: 31, 48: 35, 56: 28}
PAD = (
    {0: 55, 16: 52, 32: 52, 48: 51, 56: 52},  # Em C Am B Em
    {0: 59, 16: 55, 32: 57, 48: 54, 56: 55},
    {0: 64, 16: 60, 32: 60, 48: 59, 56: 59},
)
LEAD = {0: 83, 4: 81, 8: 79, 12: 76,
        16: 79, 20: 76, 24: 74, 28: 72,
        32: 76, 36: 74, 40: 72, 44: 69,
        48: 66, 52: 63, 56: 64}
CHANNELS = (BASS,) + PAD + (LEAD,)


class Lcg:
    def __init__(self, seed=1):
        self.x = seed

    def __call__(self):
        self.x = (self.x * 1103515245 + 12345) & 0x7FFFFFFF
        return self.x / 0x40000000 - 1.0


def timeline():
    """The jingle's PCM, page-padded, with at least one silent page last so
    the ring drains to silence."""
    n = START[STEPS] + TAIL
    n = (n // 256 + 2) * 256
    buf = [0.0] * n
    noise = Lcg(0x6502)

    def explosion(at):
        lp, rumble = 0.0, 0.0
        for i in range(min(round(RATE * 1.3), n - at)):
            t = i / RATE
            cut = 200 + 3800 * math.exp(-t / 0.15)
            a = 1 - math.exp(-2 * math.pi * cut / RATE)
            lp += a * (noise() - lp)
            rumble += 2 * math.pi * (38 + 14 * math.exp(-t / 0.3)) / RATE
            crack = noise() * 0.8 if i < 320 else 0.0
            env = math.exp(-t / 0.32)
            buf[at + i] += ((lp * 1.4 + math.sin(rumble) * 0.6) * env +
                            crack * math.exp(-t / 0.01))

    def boom(at, length, tau, level):
        phase, lp = 0.0, 0.0
        for i in range(min(round(RATE * length), n - at)):
            t = i / RATE
            phase += 2 * math.pi * (40 + 35 * math.exp(-t / 0.06)) / RATE
            lp += 0.05 * (noise() - lp)
            buf[at + i] += ((math.sin(phase) + lp * 0.8) * level *
                            math.exp(-t / tau))

    explosion(EXPLOSION_AT)
    boom(BOOM_AT, 0.5, 0.12, 0.7)
    boom(FINAL_AT, 0.9, 0.22, 0.9)

    peak = max(abs(v) for v in buf)
    return bytes(max(-128, min(127, round(v / peak * PCM_PEAK))) & 0xFF
                 for v in buf)


def pages():
    """The pages stored once each, and the page number of each page of the
    timeline in order."""
    pcm = timeline()
    store, order, seen = bytearray(), [], {}
    for at in range(0, len(pcm), 256):
        page = pcm[at:at + 256]
        if page not in seen:
            seen[page] = (PCM_SRC >> 8) + len(store) // 256
            store += page
        order.append(seen[page])
    assert PCM_SRC + len(store) <= PCM_TOP, \
        f"{len(store) // 256} distinct pages do not fit"
    return bytes(store), order


def fnum(note):
    f = 440.0 * 2 ** ((note - 69) / 12)
    for block in range(8):
        n = round(f * 2 ** (20 - block) / 49716)
        if n < 1024:
            return block, n
    raise ValueError(note)


MOD = (0x00, 0x01, 0x02, 0x08, 0x09)

# (0x20, 0x40, 0x60, 0x80) for modulator then carrier, and 0xC0. Every
# voice holds while keyed, so the notes last as long as the score says.
OPL_BASS = ((0x21, 0x1A, 0xF4, 0x24), (0x21, 0x08, 0xF3, 0x25), 0x0A)
OPL_PAD = ((0x22, 0x26, 0x62, 0x14), (0x21, 0x1C, 0x52, 0x14), 0x01)
OPL_LEAD = ((0x61, 0x1E, 0xF2, 0x14), (0x61, 0x0E, 0xF2, 0x15), 0x0C)
OPL_VOICES = (OPL_BASS, OPL_PAD, OPL_PAD, OPL_PAD, OPL_LEAD)


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


def opl_note(ch, note):
    hi, lo = opl_hi(note)
    return [0xB0 + ch, hi, 0xA0 + ch, lo, 0xB0 + ch, 0x20 | hi]


def opl_off(ch, note):
    return [0xB0 + ch, opl_hi(note)[0]]


# (wave_release, duty, vol_attack, vol_decay, pan) per channel. Volume 0 is
# the loudest, and a pan byte of 0x80 would drop the channel from the mix.
PSG_VOICES = (
    (0x38, 0xFF, 0x40, 0x55, 0x00),  # bass, triangle
    (0x29, 0xFF, 0x95, 0xA8, 0xD0),  # pad, saw
    (0x29, 0xFF, 0x95, 0xA8, 0x00),
    (0x29, 0xFF, 0x95, 0xA8, 0x30),
    (0x29, 0xFF, 0x61, 0x63, 0x00),  # lead, saw
)


def psg_init():
    out = []
    for ch, (wr, duty, va, vd, pan) in enumerate(PSG_VOICES):
        o = ch * 8
        out += (o + 2, duty, o + 3, va, o + 4, vd, o + 5, wr, o + 6, pan)
    return out


def psg_note(ch, note):
    """Gate off then on, which is what starts the attack."""
    f = round(440.0 * 2 ** ((note - 69) / 12) * 3)
    o, pan = ch * 8, PSG_VOICES[ch][4]
    return [o + 6, pan, o + 0, f & 0xFF, o + 1, f >> 8, o + 6, pan | 1]


def psg_off(ch, note):
    o, pan = ch * 8, PSG_VOICES[ch][4]
    return [o + 6, pan]


def step(note, s):
    out = []
    for ch, events in enumerate(CHANNELS):
        if s in events:
            out += note(ch, events[s])
    return out


def end(off):
    """Every channel released from the note it last played."""
    out = []
    for ch, events in enumerate(CHANNELS):
        out += off(ch, events[max(events)])
    return out


def tables(order):
    """The synth lists, their pointer tables, the step lengths and the page
    order. Each step length sits one entry on from its step, because it is
    looked up after the step has been played and STEP moved on."""
    kinds = {
        "init": [opl_init(), psg_init()],
        "step": ([step(opl_note, s) for s in range(STEPS)] +
                 [step(psg_note, s) for s in range(STEPS)]),
        "end": [end(opl_off), end(psg_off)],
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
    lengths = [0] + LENGTHS
    ptr["len"] = (TABLES + len(blob), TABLES + len(blob) + len(lengths))
    blob += bytes(n & 0xFF for n in lengths)
    blob += bytes(n >> 8 for n in lengths)
    ptr["order"] = TABLES + len(blob)
    blob += bytes(order)
    assert TABLES + len(blob) <= PCM_SRC, f"tables end at ${TABLES + len(blob):04X}"
    return bytes(blob), ptr


def program(ptr, npages):
    p = Asm()
    play, loop, exit_ = p.local("play"), p.local("loop"), p.local("exit")
    key_done, done, again = p.local("key"), p.local("done"), p.local("again")
    ring_pages = (1 << (RING_LOG2 - 8)) - 1

    def lsr(n):
        for _ in range(n):
            p.lsr_a()

    def list_at(kind):
        """X indexes the kind's pointer tables."""
        lo, hi = ptr[kind]
        p.lda_abx(lo)
        p.sta_zp(ZP_PTR)
        p.lda_abx(hi)
        p.sta_zp(ZP_PTR + 1)

    p.store(RW0_STEP, 1)
    p.say(f"\r\nRIVER RAID - game over, {BPM:.0f} BPM. Q exits.\r\n")
    p.store(ZP_MODE, 0)

    p.symbol(play)
    p.lda_imm(0)
    for zp in (ZP_WPH, ZP_SRC, ZP_LRPL, ZP_LRPH, ZP_POSL, ZP_POSH, ZP_STEP):
        p.sta_zp(zp)
    p.store(ZP_TPTR, ptr["order"] & 0xFF)
    p.store(ZP_TPTR + 1, ptr["order"] >> 8)
    p.store(ZP_TLEFT, npages & 0xFF)
    p.store(ZP_TLEFT + 1, npages >> 8)
    p.jsr_abs("synth_start")
    for i, v in enumerate((0, 0, 0x03, RING_LOG2, RATE & 0xFF, RATE >> 8, 0, 0)):
        p.poke(XPCM + i, v)
    p.xreg(0, 1, 2, XPCM)
    p.jsr_abs("seq_step")

    p.symbol(loop)
    p.bit_abs(RIA_READY)
    p.bvc(key_done)
    p.lda_abs(RIA_RX)
    p.ora_imm(0x20)
    p.cmp_imm(ord("q"))
    with p.branch("bne"):
        p.jmp_abs(exit_)
    p.symbol(key_done)
    p.jsr_abs("read_rp")

    # write_ptr is always on a page, so the fill's low byte is only a borrow.
    p.lda_zp(ZP_TLEFT)
    p.ora_zp(ZP_TLEFT + 1)
    with p.branch("beq"):
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

    # STEP runs one past the last step once the end list has been played.
    p.lda_zp(ZP_STEP)
    p.cmp_imm(STEPS + 1)
    with p.branch("bcc"):
        p.jmp_abs(done)
    p.ldx_zp(ZP_STEP)
    len_lo, len_hi = ptr["len"]
    p.lda_zp(ZP_POSL)
    p.cmp_abx(len_lo)
    p.lda_zp(ZP_POSH)
    p.sbc_abx(len_hi)
    with p.branch("bcs"):
        p.jmp_abs(loop)
    p.sec()
    p.lda_zp(ZP_POSL)
    p.sbc_abx(len_lo)
    p.sta_zp(ZP_POSL)
    p.lda_zp(ZP_POSH)
    p.sbc_abx(len_hi)
    p.sta_zp(ZP_POSH)
    p.jsr_abs("seq_step")
    p.jmp_abs(loop)

    # Done once every page has been written and the ring has played dry.
    p.symbol(done)
    p.lda_zp(ZP_TLEFT)
    p.ora_zp(ZP_TLEFT + 1)
    with p.branch("beq"):
        p.jmp_abs(loop)
    p.lda_zp(ZP_RPL)
    with p.branch("beq"):
        p.jmp_abs(loop)
    p.lda_zp(ZP_RPH)
    p.cmp_zp(ZP_WPH)
    with p.branch("beq"):
        p.jmp_abs(loop)
    p.xreg(0, 1, 2, 0xFFFF)
    p.say("Space: again on the other synth, R: again, Q: exit.\r\n")
    p.symbol(again)
    p.bit_abs(RIA_READY)
    p.bvc(again)
    p.lda_abs(RIA_RX)
    p.ora_imm(0x20)
    p.cmp_imm(ord("q"))
    p.beq(exit_)
    p.cmp_imm(ord("r"))
    with p.branch("bne"):
        p.jmp_abs(play)
    p.cmp_imm(ord(" "))
    p.bne(again)
    p.lda_zp(ZP_MODE)
    p.eor_imm(MODE_PSG)
    p.sta_zp(ZP_MODE)
    p.jmp_abs(play)

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

    # The next page of the timeline is whichever stored page the order
    # table names.
    p.symbol("put_chunk")
    p.store(RW0_ADDR, XRING & 0xFF)
    p.lda_zp(ZP_WPH)
    p.clc()
    p.adc_imm(XRING >> 8)
    p.sta_abs(RW0_ADDR + 1)
    p.ldy_imm(0)
    p.lda_izy(ZP_TPTR)
    p.sta_zp(ZP_SRC + 1)
    p.symbol("put_chunk.copy")
    p.lda_izy(ZP_SRC)
    p.sta_abs(RW0_DATA)
    p.iny()
    p.bne("put_chunk.copy")
    p.inc16(ZP_TPTR, ZP_TPTR + 1)
    p.lda_zp(ZP_TLEFT)
    with p.branch("bne"):
        p.dec_zp(ZP_TLEFT + 1)
    p.dec_zp(ZP_TLEFT)
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

    p.symbol("seq_step")
    p.lda_zp(ZP_STEP)
    p.cmp_imm(STEPS)
    p.beq("seq_step.end")
    p.clc()
    p.adc_zp(ZP_MODE)
    p.tax()
    list_at("step")
    p.jsr_abs("run_list")
    p.inc_zp(ZP_STEP)
    p.rts()
    p.symbol("seq_step.end")
    p.lda_zp(ZP_MODE)
    lsr(MODE_PSG.bit_length() - 1)
    p.tax()
    list_at("end")
    p.jsr_abs("run_list")
    p.inc_zp(ZP_STEP)
    p.rts()

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
    store, order = pages()
    blob, ptr = tables(order)
    p = program(ptr, len(order))
    assert p.here() <= TABLES, f"code ends at ${p.here():04X}"
    p.rom.add_binary_data(blob, TABLES)
    p.rom.add_binary_data(store, PCM_SRC)
    return p


# --emit <folder> is main's; --bpm may follow it.
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--bpm", type=float, default=DEFAULT_BPM,
                help="tempo of the first three bars; the default is "
                "riverraid's")
try:
    set_tempo(ap.parse_known_args()[0].bpm)
except ValueError as e:
    ap.error(str(e))

ROMS = {"riverraid_gameover": prog}
main(globals())
