#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""A program for rp6502-emu --mtram: it opens the mtRAM control
gate, gives slot 16 ($8000) a different block in tasks 0 and 1, switches
between them, and reports what each task reads there, the core temperature
and the reset count. It halts with task 1 live, so a script can check the
6502's view of $8000 through the map."""

from rp6502_asm import Asm, putc, putnib, puthex
from rp6502_rom import main

REG_RESETS = 0xFED5
REG_TEMP_MIN = 0xFED6
REG_SWAPDONE = 0xFEDD
REG_EDIT = 0xFEDE
REG_SWITCH = 0xFEDF
REG_BANK_BASE = 0xFEE0
REG_KNOCK = 0xFEFF

SLOT16 = 0x8000


def build():
    p = Asm()
    p.jmp_abs("main")
    p.use(putc, putnib, puthex)
    p.symbol("main")

    def knock(cmd):
        for c in b"#!RP6502" + cmd.encode():
            p.store(REG_KNOCK, c)

    def switch(task):
        p.store(REG_SWAPDONE, 0)
        p.store(REG_SWITCH, task)
        wait = p.local("swap")
        p.symbol(wait)
        p.lda_abs(REG_SWAPDONE)
        p.beq(wait)

    def report(tag, addr):
        p.say(tag + "=")
        p.lda_abs(addr)
        p.jsr_abs("puthex")
        p.say("\r\n")

    knock("+")
    p.store(REG_BANK_BASE + 16, 100)
    p.store(SLOT16, 0xC3)
    p.store(REG_EDIT, 1)
    p.store(REG_BANK_BASE + 16, 101)
    p.store(REG_EDIT, 0)
    switch(1)
    p.store(SLOT16, 0x3C)

    switch(0)
    report("T0", SLOT16)
    switch(1)
    report("T1", SLOT16)
    report("TEMP", REG_TEMP_MIN)
    report("RST", REG_RESETS)

    p.say("DONE\r\n")
    p.stp()
    return p


ROMS = {"mtram": build}
main(globals())
