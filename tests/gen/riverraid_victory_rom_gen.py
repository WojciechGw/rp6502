#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""The victory jingle for riverraid.rp6502: four bars played once, an E
major brass fanfare, I IV V I, on the OPL2 or the PSG over PCM cymbals,
march hits and a snare roll that broadens into the held final chord.

The engine is riverraid_gameover's. The PCM is the jingle's whole timeline
rendered here and cut into pages; silent and repeated pages are stored
once, and a table of page numbers plays them back in order. The sequencer
steps on PCM actually played, counted from read_ptr, and each step has its
own length, which is what broadens the roll. Once the jingle has rung out,
Space plays it again on the other synth, R plays it again and Q exits."""

import argparse
import math

from rp6502_asm import (API_OP, RIA_READY, RIA_RX, RW0_ADDR, RW0_DATA,
                        RW0_STEP, RW1_ADDR, RW1_DATA, RW1_STEP, Asm, putc)
from rp6502_rom import image

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


ROLL = range(44, 48)  # the last beat of bar three
KICKS = (0, 16, 32, 48)
SNARES = (8, 24, 40)


def set_tempo(bpm):
    """Bars in time at bpm, but for the roll that closes bar three, which
    broadens by up to a third into the final chord. The jingle is a
    timeline rather than a loop of pages, so a sixteenth is any whole
    number of samples. Returns the tempo the rounding leaves."""
    global SIXTEENTH, LENGTHS, START, BPM
    if not MIN_BPM <= bpm <= MAX_BPM:
        raise ValueError(f"{bpm} BPM is outside {MIN_BPM}..{MAX_BPM}")
    SIXTEENTH = round(RATE * 60 / (bpm * 4))
    LENGTHS = [SIXTEENTH] * STEPS
    for k, s in enumerate(ROLL):
        LENGTHS[s] = round(SIXTEENTH * (1 + 0.33 * (k + 1) / len(ROLL)))
    assert len(LENGTHS) == STEPS and max(LENGTHS) < 0x10000
    START = [sum(LENGTHS[:s]) for s in range(STEPS + 1)]
    BPM = RATE * 60 / (SIXTEENTH * 4)
    return BPM


set_tempo(DEFAULT_BPM)

REST = None

# Step to note, for each channel. A note holds until the channel's next
# event, and every channel is released when the jingle ends.
BASS = {0: 40, 4: 47, 8: 40, 12: 47,    # E
        16: 45, 20: 52, 24: 45, 28: 52,  # A
        32: 47, 36: 54, 40: 47, 44: 42,  # B
        48: 40}                          # E, held
PAD = (
    {0: 64, 3: 64, 4: 64, 8: 64, 16: 64, 19: 64, 20: 64, 24: 64,
     32: 63, 35: 63, 36: 63, 40: 63, 48: 64},
    {0: 68, 3: 68, 4: 68, 8: 68, 16: 69, 19: 69, 20: 69, 24: 69,
     32: 66, 35: 66, 36: 66, 40: 66, 48: 68},
    {0: 71, 3: 71, 4: 71, 8: 71, 16: 73, 19: 73, 20: 73, 24: 73,
     32: 71, 35: 71, 36: 71, 40: 71, 48: 71},
)
LEAD = {0: 76, 3: 76, 4: 80, 6: 83, 12: 80, 14: 83,
        16: 85, 22: 83, 24: 81, 28: 85,
        32: 83, 36: 81, 38: 80, 40: 78, 44: 75, 46: 78,
        48: 88}
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

    def kick(at, level=1.0):
        phase = 0.0
        for i in range(min(round(RATE * 0.25), n - at)):
            t = i / RATE
            phase += 2 * math.pi * (50 + 110 * math.exp(-t / 0.03)) / RATE
            buf[at + i] += math.sin(phase) * level * math.exp(-t / 0.08)

    def snare(at, level=1.0, length=0.12):
        for i in range(min(round(RATE * length), n - at)):
            t = i / RATE
            buf[at + i] += (noise() * 0.7 * math.exp(-t / 0.05) +
                            math.sin(2 * math.pi * 210 * t) * 0.4 *
                            math.exp(-t / 0.03)) * level

    def crash(at, length, level):
        """High-passed noise with a slow decay, which is all a cymbal is at
        16 kHz."""
        prev = 0.0
        for i in range(min(round(RATE * length), n - at)):
            t = i / RATE
            x = noise()
            buf[at + i] += ((x - prev * 0.6) * level *
                            math.exp(-t / (length / 3)))
            prev = x

    crash(START[0], 0.6, 0.6)
    for s in KICKS:
        kick(START[s], 1.0 if s in (0, 48) else 0.7)
    for s in SNARES:
        snare(START[s], 0.6)
    # Thirty-seconds through the broadening beat, rising to the downbeat.
    for s in ROLL:
        for half in range(2):
            at = START[s] + half * LENGTHS[s] // 2
            snare(at, 0.35 + 0.6 * (at - START[ROLL[0]]) /
                  (START[ROLL[-1] + 1] - START[ROLL[0]]), 0.08)
    crash(START[48], 1.3, 0.85)

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
# voice holds while keyed, so the notes last as long as the score says;
# the brass attacks fast and sits a little under its peak.
OPL_BASS = ((0x21, 0x1A, 0xF4, 0x24), (0x21, 0x08, 0xF3, 0x25), 0x0A)
OPL_BRASS = ((0x21, 0x1C, 0xD3, 0x25), (0x21, 0x16, 0xE4, 0x26), 0x0A)
OPL_LEAD = ((0x61, 0x1A, 0xF2, 0x14), (0x61, 0x0C, 0xF3, 0x15), 0x0C)
OPL_VOICES = (OPL_BASS, OPL_BRASS, OPL_BRASS, OPL_BRASS, OPL_LEAD)


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
    (0x36, 0xFF, 0x40, 0x55, 0x00),  # bass, triangle
    (0x16, 0x80, 0x90, 0xB3, 0xD0),  # brass, square
    (0x16, 0x80, 0x90, 0xB3, 0x00),
    (0x16, 0x80, 0x90, 0xB3, 0x30),
    (0x28, 0xFF, 0x60, 0x63, 0x00),  # lead, saw
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
    p.say(f"\r\nRIVER RAID - victory, {BPM:.0f} BPM. Q exits.\r\n")
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


def rom():
    store, order = pages()
    blob, ptr = tables(order)
    p = program(ptr, len(order))
    code = p.code()
    assert p.org + len(code) <= TABLES, f"code ends at ${p.org + len(code):04X}"
    return image(p).record(TABLES, blob).record(PCM_SRC, store), store, order


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--emit", required=True)
    ap.add_argument("--bpm", type=float, default=DEFAULT_BPM,
                    help="tempo of the first three bars; the default is "
                    "riverraid's")
    a = ap.parse_args()
    try:
        bpm = set_tempo(a.bpm)
    except ValueError as e:
        ap.error(str(e))
    r, store, order = rom()
    print(f"riverraid_victory.rp6502 {bpm:.2f} BPM, "
          f"{START[STEPS] / RATE:.1f} s, "
          f"{len(order)} pages from {len(store) // 256} stored, "
          f"{r.write(a.emit)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
