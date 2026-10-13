#!/usr/bin/env python3
# Copyright (c) 2026 WojciechGw
#
# for Rumbledethumps' Picocomputer 6502
#
# SPDX-License-Identifier: BSD-3-Clause

"""A program for rp6502-emu --drive MSC0=<folder>, where the folder holds
hello.txt. It starts on that drive, opens the file by the drive's name, moves
onto the drive and
into a folder it makes, and opens the file again from the root, from the
parent, and from above the root, which no path can reach. Each result is the
high byte of the call's answer: 00 for success, FF for an error."""

import argparse

from rp6502_asm import (API_A, API_X, O_RDONLY, OP_CHDIR, OP_CHDRIVE,
                        OP_CLOSE, OP_EXIT, OP_GETCWD, OP_MKDIR, OP_OPEN,
                        XSTACK, Asm, putc, puthex, putnib)
from rp6502_rom import image

FD = 0x0200


def prog():
    p = Asm()
    p.jmp_abs("main")
    p.use(putc, putnib, puthex)
    p.symbol("main")

    def result(tag):
        p.say(tag + " ")
        p.txa()
        p.jsr_abs("puthex")
        p.say("\r\n")

    def call_path(op, path):
        p.push_str(path)
        p.call(op)

    def open_close(tag, path):
        p.push_str(path)
        p.store(API_A, O_RDONLY)
        p.call(OP_OPEN)
        p.sta_abs(FD)
        result(tag)
        p.lda_abs(FD)
        p.sta_abs(API_A)
        p.call(OP_CLOSE)

    def cwd(tag="cwd"):
        p.call(OP_GETCWD)
        p.say(tag + " ")
        loop, end = p.local("cwd"), p.local("cwd_end")
        p.symbol(loop)
        p.lda_abs(XSTACK)
        p.beq(end)
        p.jsr_abs("putc")
        p.jmp_abs(loop)
        p.symbol(end)
        p.say("\r\n")

    cwd("start")
    open_close("open", "MSC0:/hello.txt")
    call_path(OP_CHDRIVE, "MSC0:")
    result("chdrive")
    cwd()
    call_path(OP_MKDIR, "sub")
    call_path(OP_CHDIR, "sub")
    result("chdir")
    cwd()
    open_close("root", "/hello.txt")
    open_close("up", "../hello.txt")
    open_close("escape", "../../hello.txt")
    open_close("missing", "MSC1:/hello.txt")
    call_path(OP_CHDRIVE, "0:")
    result("number")
    call_path(OP_CHDRIVE, "1:")
    result("unmapped")

    p.store(API_A, 0)
    p.store(API_X, 0)
    p.call(OP_EXIT)
    p.stp()
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit")
    a = ap.parse_args()
    if a.emit:
        print(f"drive.rp6502 {image(prog()).write(a.emit)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
