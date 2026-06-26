# PCM Audio Playback — RP6502

## Overview

The `pcm` module (`src/ria/aud/pcm.c`) adds raw PCM streaming to the RP6502
audio system. The RP2350 reads 16-bit stereo samples from a ring buffer in XRAM
and outputs them via the shared 10-bit PWM at 44 100 Hz, independently of the
6502 CPU. BEL alert sounds are mixed in automatically, exactly as with PSG and
OPL.

WAV header parsing, decoding (OGG, MP3), and sample format conversion are the
responsibility of the 6502-side application. The module plays only raw signed
16-bit stereo samples.

## Input format

| Parameter      | Value                                       |
|----------------|---------------------------------------------|
| Sample type    | `int16_t`, little-endian                    |
| Channels       | 2 (stereo, interleaved L/R)                 |
| Sample rate    | 44 100 Hz                                   |
| Output         | 10-bit PWM stereo (GPIO 27 = R, GPIO 28 = L)|

WAV files that match this format have:
- AudioFormat = 1 (PCM)
- NumChannels = 2
- SampleRate  = 44100
- BitsPerSample = 16

## XRAM layout

The 6502 application allocates a block of XRAM and passes its base address via
XREG. The base address must be **4-byte aligned**.

```
Offset          Type      Owner   Description
--------------  --------  ------  ------------------------------------------
+0..+1          uint16_t  6502    write_ptr — byte index into ring buffer
+2              uint8_t   6502    flags — bit 0 = play
+3              uint8_t   6502    buf_size_log2 — 9..13; 0 or invalid → 10
+4 .. +4+N-1    uint8_t[] 6502    ring buffer (N = 1 << buf_size_log2 bytes)
```

Total XRAM consumed: `4 + (1 << buf_size_log2)` bytes.
Constraint: `(base & 3) == 0` and `base + 4 + (1 << buf_size_log2) <= 65536`.

### Ring buffer mechanics

- `write_ptr` — byte offset where the 6502 will write next; set in XRAM by the
  application and read every IRQ by the RP2350.
- `pcm_read_ptr` — internal to the RP2350; never written to XRAM.
- Mask: `(1 << buf_size_log2) - 1`. Power-of-two wrapping replaces modulo.
- Both pointers advance in multiples of 4 (one stereo frame = L_lo L_hi R_lo R_hi).
  Because the ring size is always a power-of-two multiple of 4, a single frame
  read never crosses the buffer boundary.
- Buffer empty: `write_ptr == pcm_read_ptr` → IRQ outputs silence.
- Buffer full (one guard frame reserved): `write_ptr == (pcm_read_ptr - 4) & mask`.
  Maximum usable content: `(1 << buf_size_log2) - 4` bytes.

### Choosing buf_size_log2

Audio consumption rate: 44 100 Hz × 4 B/frame = **176 400 B/s ≈ 176 B/ms**.

| log2 | Ring size | Headroom  | Typical use                        |
|------|-----------|-----------|------------------------------------|
|  9   |   512 B   |  ~2.9 ms  | Minimum; tight main-loop feeding   |
| 10   |  1 024 B  |  ~5.8 ms  | Main-loop feeding (default)        |
| 11   |  2 048 B  | ~11.6 ms  |                                    |
| 12   |  4 096 B  | ~23.2 ms  | Once-per-VSYNC at 60 Hz (≈16.7 ms)|
| 13   |  8 192 B  | ~46.4 ms  | Generous VSYNC margin              |

At 600 KB/s external memory: a 512-byte sector read takes ~0.85 ms, consuming
~150 bytes of audio, so any size ≥ 512 B provides adequate read margin.

For once-per-VSYNC feeding at 60 Hz, log2 = 12 (≈23 ms) covers the 16.7 ms
frame time with margin. Smaller buffers require feeding multiple times per frame.

## XREG activation

XREG device = RIA (0), channel = 1 (audio), address = 2 (PCM).

```c
#include <rp6502.h>

xreg(0, 1, 2, pcm_xram_base);   /* start */
xreg(0, 1, 2, 0xFFFF);          /* stop  */
```

`buf_size_log2` is read from `xram[base + 3]` at the moment `xreg()` is called.
Set it in XRAM **before** calling `xreg()`. The driver resets `pcm_read_ptr` to
zero on every activation.

## Activation sequence

```c
/* 1. Write control header to XRAM */
RIA.addr0 = PCM_BASE;
RIA.step0 = 1;
RIA.rw0 = 0;           /* write_ptr lo         */
RIA.rw0 = 0;           /* write_ptr hi         */
RIA.rw0 = 1;           /* flags: play          */
RIA.rw0 = PCM_LOG2;    /* buf_size_log2        */

/* 2. Pre-fill ring buffer (leave one 4-byte guard frame empty) */
read_xram(PCM_BASE + 4, (1 << PCM_LOG2) - 4, fd);

/* 3. Update write_ptr in XRAM to reflect pre-filled bytes */
prefill = (1 << PCM_LOG2) - 4;
RIA.addr0 = PCM_BASE;
RIA.step0 = 1;
RIA.rw0 = (unsigned char)(prefill);
RIA.rw0 = (unsigned char)(prefill >> 8);

/* 4. Start driver */
xreg(0, 1, 2, PCM_BASE);

/* 5. Feed BYTES_PER_VSYNC bytes each VSYNC; handle ring wrap */
```

## Audio playback and the 6502 main loop

The RP2350 IRQ runs at 44 100 Hz fully autonomously. VSYNC, NMI, and IRQ on
the 6502 side do not interrupt or pause playback.

The 6502 application must keep the ring buffer supplied. At 44 100 Hz stereo
the driver consumes exactly 2 940 bytes per 60 Hz VSYNC frame. Pre-filling the
buffer before activation and then writing 2 940 bytes each VSYNC keeps the
buffer at a steady level without tracking the RP2350's internal read pointer.

If the feeding point is the main loop rather than VSYNC, any buf_size_log2 ≥ 9
is sufficient, provided the 6502 feeds at least 2 940 bytes between any two
natural loop iterations before the current buffer empties.

## Stopping

```c
xreg(0, 1, 2, 0xFFFF);
```

After the last audio byte has been written to XRAM, wait for the remaining
buffer content to drain (≈ `(1 << buf_size_log2) / 176` ms) before stopping
the driver. A few extra VSYNC periods of waiting is the simplest approach.

Calling any other audio XREG (PSG 0x100, OPL 0x101) automatically replaces
the PCM driver via `aud_setup()`.

## See also

- `docs/PCM/playpcm.c` — complete cc65 example playing `test.wav`
- `src/ria/aud/pcm.c` — RP2350-side implementation
- `src/ria/aud/aud.h` — shared audio constants (`AUD_PWM_BITS`, `AUD_PWM_CENTER`)
