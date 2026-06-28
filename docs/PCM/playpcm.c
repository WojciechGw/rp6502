/*
 * playpcm.c - WAV audio playback example for RP6502 (cc65 / C89)
 *
 * Plays test.wav using the PCM audio driver (XREG channel 0x102).
 * Format is detected automatically from the WAV header.
 * Supported: AudioFormat=PCM, 1-2 channels, 8000/11025/22050/44100 Hz,
 *            16-bit signed or 8-bit unsigned.
 *
 * XREG: device=0 (RIA), channel=1 (audio), address=2 (PCM), word=XRAM base
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rp6502.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* XRAM region for PCM driver.  Must be 4-byte aligned.
 * Total: 8 (header) + PCM_BUF_SIZE (ring) bytes. */
#define PCM_BASE      0x4000u
#define PCM_LOG2      12u
#define PCM_BUF_SIZE  (1u << PCM_LOG2)
#define PCM_BUF_MASK  (PCM_BUF_SIZE - 1u)
#define PCM_RING_BASE (PCM_BASE + 8u)

/* 6502-local mirror of write_ptr */
static unsigned wav_wp;

/* ---- helpers ------------------------------------------------------------ */

static void wp_flush(void)
{
    RIA.addr0 = PCM_BASE;
    RIA.step0 = 1;
    RIA.rw0 = (unsigned char)(wav_wp);
    RIA.rw0 = (unsigned char)(wav_wp >> 8);
}

/*
 * Read 'count' bytes from fd directly into the PCM ring buffer in XRAM,
 * splitting the write at the ring boundary when necessary.
 * Returns actual bytes written (may be < count at EOF).
 */
static unsigned ring_write(int fd, unsigned count)
{
    unsigned to_end;
    int n1, n2;

    to_end = PCM_BUF_SIZE - wav_wp;

    if (count <= to_end) {
        n1 = read_xram(PCM_RING_BASE + wav_wp, count, fd);
        if (n1 > 0) {
            wav_wp = (wav_wp + (unsigned)n1) & PCM_BUF_MASK;
            wp_flush();
        }
        return (n1 > 0) ? (unsigned)n1 : 0u;
    }

    n1 = read_xram(PCM_RING_BASE + wav_wp, to_end, fd);
    if (n1 <= 0)
        return 0u;
    wav_wp = (wav_wp + (unsigned)n1) & PCM_BUF_MASK;
    if ((unsigned)n1 < to_end) {
        wp_flush();
        return (unsigned)n1;
    }

    n2 = read_xram(PCM_RING_BASE + wav_wp, count - to_end, fd);
    if (n2 > 0)
        wav_wp = (wav_wp + (unsigned)n2) & PCM_BUF_MASK;
    wp_flush();
    return (unsigned)n1 + ((n2 > 0) ? (unsigned)n2 : 0u);
}

/* ---- WAV header helpers ------------------------------------------------- */

static unsigned long read_le32(const unsigned char *p)
{
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

static unsigned read_le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    int fd;
    unsigned char hdr[44];
    unsigned long data_size, remaining, bres, wav_rate;
    unsigned chunk, n, drain, wav_channels, wav_bits, frame_sz;
    unsigned long bps;
    unsigned char last_vsync, pcm_fmt, rate_lo, rate_hi;

    fd = open("test.wav", O_RDONLY);
    if (fd < 0) {
        printf("Cannot open test.wav\n");
        return 1;
    }

    /* Validate header structure (simple 44-byte layout, no LIST chunks). */
    if (read(fd, hdr, 44) != 44
        || memcmp(hdr,      "RIFF", 4) != 0
        || memcmp(hdr +  8, "WAVE", 4) != 0
        || memcmp(hdr + 12, "fmt ", 4) != 0
        || memcmp(hdr + 36, "data", 4) != 0) {
        printf("test.wav: not a simple 44-byte-header WAV\n");
        close(fd);
        return 1;
    }

    wav_channels = read_le16(hdr + 22);
    wav_rate     = read_le32(hdr + 24);
    wav_bits     = read_le16(hdr + 34);

    if (read_le16(hdr + 20) != 1u
        || (wav_channels != 1u && wav_channels != 2u)
        || (wav_bits != 8u && wav_bits != 16u)
        || (wav_rate != 8000ul  && wav_rate != 11025ul && wav_rate != 16000ul
         && wav_rate != 22050ul && wav_rate != 32000ul && wav_rate != 44100ul)) {
        printf("test.wav: unsupported format\n");
        close(fd);
        return 1;
    }

    /* Derive PCM driver parameters from WAV fields.
     * WAV 8-bit is always unsigned; 16-bit is always signed. */
    pcm_fmt = 0;
    if (wav_channels == 1u) pcm_fmt |= 0x01u;  /* mono  */
    if (wav_bits    == 8u)  pcm_fmt |= 0x06u;  /* 8-bit unsigned */

    frame_sz = wav_channels * (wav_bits / 8u);
    bps      = wav_rate * (unsigned long)frame_sz;
    rate_lo  = (unsigned char)(wav_rate & 0xFFul);
    rate_hi  = (unsigned char)((wav_rate >> 8) & 0xFFul);

    data_size = read_le32(hdr + 40);
    printf("Playing %lu bytes, %u ch, %lu Hz, %u-bit...\n",
           data_size, wav_channels, wav_rate, wav_bits);

    /* --- Init XRAM control header (8 bytes) ------------------------------ */
    RIA.addr0 = PCM_BASE;
    RIA.step0 = 1;
    RIA.rw0 = 0;        /* write_ptr lo   */
    RIA.rw0 = 0;        /* write_ptr hi   */
    RIA.rw0 = pcm_fmt;  /* format         */
    RIA.rw0 = PCM_LOG2; /* buf_size_log2  */
    RIA.rw0 = rate_lo;  /* sample_rate lo */
    RIA.rw0 = rate_hi;  /* sample_rate hi */
    RIA.rw0 = 0;        /* reserved       */
    RIA.rw0 = 0;        /* reserved       */

    wav_wp    = 0;
    remaining = data_size;

    /* --- Pre-fill ring buffer (leave one guard frame empty) -------------- */
    chunk = PCM_BUF_SIZE - frame_sz;
    if ((unsigned long)chunk > remaining)
        chunk = (unsigned)remaining;
    n = ring_write(fd, chunk);
    remaining -= (unsigned long)n;

    /* --- Activate PCM driver --------------------------------------------- */
    xreg(0, 1, 2, PCM_BASE);

    last_vsync = RIA.vsync;
    bres = 0;

    /* --- Main loop: Bresenham accumulator feeds correct bytes per VSYNC -- *
     *
     * bres accumulates fractional bytes across frames, distributing them
     * evenly.  For rates where bps is an exact multiple of 60, bres stays
     * zero and chunk is constant each VSYNC.
     */
    while (remaining > 0) {
        while (RIA.vsync == last_vsync)
            ;
        last_vsync = RIA.vsync;

        bres += bps;
        chunk = (unsigned)(bres / 60ul);
        bres -= (unsigned long)chunk * 60ul;

        if ((unsigned long)chunk > remaining)
            chunk = (unsigned)remaining;

        n = ring_write(fd, chunk);
        if (n > 0)
            remaining -= (unsigned long)n;
        else
            remaining = 0;
    }

    /* --- Wait for the ring buffer to drain ------------------------------- */
    drain = (unsigned)((PCM_BUF_SIZE * 60ul + bps - 1ul) / bps) + 1u;
    while (drain > 0) {
        while (RIA.vsync == last_vsync)
            ;
        last_vsync = RIA.vsync;
        drain--;
    }

    /* --- Stop driver ----------------------------------------------------- */
    xreg(0, 1, 2, 0xFFFF);
    close(fd);
    printf("Done.\n");
    return 0;
}
