#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""A program for rp6502-emu --mtram: it opens the mtRAM control
gate, gives slot 16 ($8000) a different block in tasks 0 and 1, switches
between them, and reports what each task reads there, the core temperature,
the reset count and the signature, all from the register window
$FF00-$FF7F. Then it measures the DS18B20 on the 1-Wire line, searches for
the first device through the bus mailbox and has an unknown operation
refused. It halts with task 1 live, so a script can check the 6502's view
of $8000 through the map."""

from rp6502_asm import Asm, putc, putnib, puthex
from rp6502_rom import main

REG_BANK_BASE = 0xFF00
REG_KNOCK = 0xFF1F
REG_SWITCH = 0xFF20
REG_EDIT = 0xFF21
REG_SWAPDONE = 0xFF22
REG_RESETS = 0xFF23
REG_TEMP_CURR = 0xFF24
REG_DS_CTRL = 0xFF2A
REG_DS_COUNT = 0xFF2B
REG_DS_TEMP = 0xFF2C
REG_XFER_CTRL = 0xFF54
REG_XFER_STATUS = 0xFF55
REG_XFER_BUF = 0xFF5A
REG_SIGNATURE = 0xFF7D

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

    def report(tag, *addrs):
        p.say(tag + "=")
        for addr in addrs:
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
    report("TEMP", REG_TEMP_CURR + 1, REG_TEMP_CURR)
    report("RST", REG_RESETS)
    report("SIG", REG_SIGNATURE, REG_SIGNATURE + 1, REG_SIGNATURE + 2)

    def run(reg, value):
        p.store(reg, value)
        wait = p.local("zero")
        p.symbol(wait)
        p.lda_abs(reg)
        p.bne(wait)

    run(REG_DS_CTRL, 1)
    report("DS", REG_DS_COUNT, REG_DS_TEMP + 1, REG_DS_TEMP, REG_DS_TEMP + 3, REG_DS_TEMP + 2)
    run(REG_XFER_CTRL, 2)
    report("OW", REG_XFER_STATUS, REG_XFER_BUF, REG_XFER_BUF + 1)
    p.store(REG_XFER_STATUS, 0xFF)
    run(REG_XFER_CTRL, 4)
    report("BAD", REG_XFER_STATUS)

    p.say("DONE\r\n")
    p.stp()
    return p


ROMS = {"mtram": build}
main(globals())
