# PCM Audio Playback — RP6502

## Overview

The `pcm` module (`src/ria/aud/pcm.c`) adds raw PCM streaming to the RP6502
audio system. The RP2350 reads samples from a ring buffer in XRAM and outputs
them via the shared 10-bit PWM stereo DAC, independently of the 6502 CPU.
BEL alert sounds are mixed in automatically, exactly as with PSG and OPL.

WAV header parsing and decoding (OGG, MP3) are the responsibility of the
6502-side application. The module plays raw PCM samples in any combination of
the supported formats below.

## Supported formats

| Parameter    | Supported values                                   |
|--------------|----------------------------------------------------|
| Sample rate  | 8 000 / 11 025 / 22 050 / 44 100 Hz               |
| Bit depth    | 16-bit signed or 8-bit (signed or unsigned)        |
| Channels     | stereo or mono (replicated to both outputs)        |
| Output       | 10-bit PWM stereo (GPIO 27 = R, GPIO 28 = L)       |

These cover the standard WAV AudioFormat=1 (PCM) variants without requiring
the 6502 to perform any sample conversion.

## XRAM layout

The 6502 application allocates a block of XRAM and passes its base address via
XREG. The base address must be **4-byte aligned**.

```
Offset          Type      Owner   Description
--------------  --------  ------  ------------------------------------------
+0..+1          uint16_t  6502    write_ptr — byte index into ring buffer
+2              uint8_t   6502    format — bit 0=mono, bit 1=8-bit, bit 2=unsigned
+3              uint8_t   6502    buf_size_log2 — 9..13; 0 or invalid → 10
+4..+5          uint16_t  6502    sample_rate LE — 8000/11025/16000/22050/32000/44100; 0 → 44100
+6..+7          —         —       reserved
+8 .. +8+N-1    uint8_t[] 6502    ring buffer (N = 1 << buf_size_log2 bytes)
```

Total XRAM consumed: `8 + (1 << buf_size_log2)` bytes.
Constraint: `(base & 3) == 0` and `base + 8 + (1 << buf_size_log2) <= 65536`.

### Format byte

| Bit | Value | Meaning                                        |
|-----|-------|------------------------------------------------|
| 0   | 0     | stereo (independent L and R samples)           |
| 0   | 1     | mono (single sample replicated to L and R)     |
| 1   | 0     | 16-bit signed (standard WAV 16-bit)            |
| 1   | 1     | 8-bit samples                                  |
| 2   | 0     | signed 8-bit (only meaningful when bit 1 = 1)  |
| 2   | 1     | unsigned 8-bit (standard WAV 8-bit convention) |

**Bytes per ring frame** by format value:

| format | channels | depth           | bytes/frame |
|--------|----------|-----------------|-------------|
| 0x00   | stereo   | 16-bit signed   | 4           |
| 0x01   | mono     | 16-bit signed   | 2           |
| 0x02   | stereo   | 8-bit signed    | 2           |
| 0x03   | mono     | 8-bit signed    | 1           |
| 0x06   | stereo   | 8-bit unsigned  | 2           |
| 0x07   | mono     | 8-bit unsigned  | 1           |

Stereo frames are interleaved `[L, R]`; mono frames are `[S]`.
16-bit values are little-endian.

### Ring buffer mechanics

- `write_ptr` — byte offset where the 6502 will write next; set in XRAM by the
  application and read every IRQ by the RP2350.
- `pcm_read_ptr` — internal to the RP2350; never written to XRAM.
- Mask: `(1 << buf_size_log2) - 1`. Power-of-two wrapping replaces modulo.
- Both pointers advance in multiples of `bytes/frame` (1, 2, or 4 depending on
  `format`). The ring size is always a power-of-two multiple of the frame size,
  so a single frame read never crosses the buffer boundary.
- Buffer empty: `write_ptr == pcm_read_ptr` → IRQ outputs silence.
- Buffer full (one guard frame reserved): `write_ptr == (pcm_read_ptr - frame_size) & mask`.
  Maximum usable content: `(1 << buf_size_log2) - frame_size` bytes.

### Choosing buf_size_log2

Consumption rate depends on format. Examples at 60 Hz VSYNC (16.7 ms/frame):

| Format                      | B/s        | B/frame (60 Hz) |
|-----------------------------|------------|-----------------|
| 44 100 Hz stereo 16-bit     | 176 400    | 2 940           |
| 44 100 Hz mono 16-bit       |  88 200    | 1 470           |
| 22 050 Hz stereo 16-bit     |  88 200    | 1 470           |
| 22 050 Hz mono 8-bit        |  22 050    |   368           |

| log2 | Ring size | Headroom @ 44100 stereo | Typical use                        |
|------|-----------|-------------------------|------------------------------------|
|  9   |   512 B   |  ~2.9 ms                | Minimum; tight main-loop feeding   |
| 10   |  1 024 B  |  ~5.8 ms                | Main-loop feeding (default)        |
| 11   |  2 048 B  | ~11.6 ms                |                                    |
| 12   |  4 096 B  | ~23.2 ms                | Once-per-VSYNC at 60 Hz (≈16.7 ms)|
| 13   |  8 192 B  | ~46.4 ms                | Generous VSYNC margin              |

At 600 KB/s external memory a 512-byte sector read takes ~0.85 ms; any
buf_size_log2 ≥ 9 provides adequate read margin at all supported rates.

For once-per-VSYNC feeding at 60 Hz with 44 100 Hz stereo, log2 = 12 (≈23 ms)
covers the 16.7 ms frame with margin. Lower rates or mono need smaller buffers.

## XREG activation

XREG device = RIA (0), channel = 1 (audio), address = 2 (PCM).

```c
#include <rp6502.h>

xreg(0, 1, 2, pcm_xram_base);   /* start */
xreg(0, 1, 2, 0xFFFF);          /* stop  */
```

`format`, `buf_size_log2`, and `sample_rate` are read from XRAM at the moment
`xreg()` is called. Set them **before** calling `xreg()`. The driver resets
`pcm_read_ptr` to zero on every activation.

## Activation sequence

```c
/* 1. Write control header to XRAM (8 bytes) */
RIA.addr0 = PCM_BASE;
RIA.step0 = 1;
RIA.rw0 = 0;              /* write_ptr lo            */
RIA.rw0 = 0;              /* write_ptr hi            */
RIA.rw0 = PCM_FORMAT;     /* format byte             */
RIA.rw0 = PCM_LOG2;       /* buf_size_log2           */
RIA.rw0 = PCM_RATE & 0xFF;/* sample_rate lo          */
RIA.rw0 = PCM_RATE >> 8;  /* sample_rate hi          */
RIA.rw0 = 0;              /* reserved                */
RIA.rw0 = 0;              /* reserved                */

/* 2. Pre-fill ring buffer (leave one guard frame empty) */
read_xram(PCM_BASE + 8, (1 << PCM_LOG2) - frame_size, fd);

/* 3. Update write_ptr in XRAM to reflect pre-filled bytes */
prefill = (1 << PCM_LOG2) - frame_size;
RIA.addr0 = PCM_BASE;
RIA.step0 = 1;
RIA.rw0 = (unsigned char)(prefill);
RIA.rw0 = (unsigned char)(prefill >> 8);

/* 4. Start driver */
xreg(0, 1, 2, PCM_BASE);

/* 5. Feed bytes_per_vsync bytes each VSYNC; handle ring wrap */
```

## Format configuration reference

`playpcm.c` detects format automatically from the WAV header:

```c
wav_channels = read_le16(hdr + 22);   /* 1 or 2 */
wav_rate     = read_le32(hdr + 24);   /* 8000 / 11025 / 16000 / 22050 / 32000 / 44100 */
wav_bits     = read_le16(hdr + 34);   /* 8 or 16 */

pcm_fmt = 0;
if (wav_channels == 1) pcm_fmt |= 0x01;  /* mono          */
if (wav_bits    == 8)  pcm_fmt |= 0x06;  /* 8-bit unsigned*/

frame_sz = wav_channels * (wav_bits / 8);
bps      = wav_rate * (unsigned long)frame_sz;
```

The `format` and `sample_rate` fields are written to the XRAM header before
calling `xreg()`. No manual configuration is required.

Bytes consumed per 60 Hz VSYNC (`bps / 60`) for common WAV variants:

| WAV file                        | format | bytes/s  | bytes/VSYNC @60 Hz |
|---------------------------------|:------:|:--------:|:------------------:|
| 44 100 Hz stereo 16-bit signed  | `0x00` | 176 400  | 2940               |
| 44 100 Hz mono   16-bit signed  | `0x01` |  88 200  | 1470               |
| 44 100 Hz stereo  8-bit unsigned| `0x06` |  88 200  | 1470               |
| 44 100 Hz mono    8-bit unsigned| `0x07` |  44 100  | 735                |
| 32 000 Hz stereo 16-bit signed  | `0x00` | 128 000  | 2133.3 ✗           |
| 32 000 Hz mono    8-bit unsigned| `0x07` |  32 000  | 533.3 ✗            |
| 22 050 Hz stereo 16-bit signed  | `0x00` |  88 200  | 1470               |
| 22 050 Hz mono   16-bit signed  | `0x01` |  44 100  | 735                |
| 22 050 Hz stereo  8-bit unsigned| `0x06` |  44 100  | 735                |
| 22 050 Hz mono    8-bit unsigned| `0x07` |  22 050  | 367.5 ✗            |
| 16 000 Hz stereo 16-bit signed  | `0x00` |  64 000  | 1066.7 ✗           |
| 16 000 Hz mono    8-bit unsigned| `0x07` |  16 000  | 266.7 ✗            |
| 11 025 Hz stereo 16-bit signed  | `0x00` |  44 100  | 735                |
| 11 025 Hz mono   16-bit signed  | `0x01` |  22 050  | 367.5 ✗            |
| 8 000 Hz stereo 16-bit signed   | `0x00` |  32 000  | 533.3 ✗            |
| 8 000 Hz mono    8-bit unsigned | `0x07` |   8 000  | 133.3 ✗            |

Rows marked ✗ have a fractional bytes-per-VSYNC. `playpcm.c` handles all rows
with a Bresenham accumulator that distributes the fractional part evenly:

```c
bres += bps;                           /* accumulate bytes this second */
chunk = (unsigned)(bres / 60ul);       /* integer bytes to write now   */
bres -= (unsigned long)chunk * 60ul;   /* carry remainder forward      */
```

For exact-integer rows, `bres` stays zero and `chunk` is constant each VSYNC.

The drain period (VSYNC frames to wait after the last write) is:
```
drain = ceil((1 << buf_size_log2) * 60 / bps) + 1
```

## Audio playback and the 6502 main loop

The RP2350 IRQ runs at the configured `sample_rate` fully autonomously. VSYNC,
NMI, and IRQ on the 6502 side do not interrupt or pause playback.

The 6502 application must keep the ring buffer supplied. The consumption rate
is `sample_rate × bytes_per_frame` bytes/second (see the format table above).
Pre-filling the buffer before activation and then writing the Bresenham-computed
number of bytes each VSYNC keeps the buffer at a steady level without tracking
the RP2350's internal read pointer.

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
