/*
 * playpcm.c - WAV audio playback example for RP6502 (cc65 / C89)
 *
 * Plays test.wav using the PCM audio driver (XREG channel 0x102).
 * Requires: 16-bit signed stereo PCM, 44 100 Hz, standard 44-byte WAV header.
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
#include <stdint.h>

/* XRAM region for PCM driver.  Must be 4-byte aligned.
 * Total size: 4 (ctrl) + PCM_BUF_SIZE (ring) = 4100 bytes. */
#define PCM_BASE      0x4000u
#define PCM_LOG2      12            /* 4096-byte ring, ~23 ms at 44100 Hz */
#define PCM_BUF_SIZE  (1u << PCM_LOG2)
#define PCM_BUF_MASK  (PCM_BUF_SIZE - 1u)
#define PCM_RING_BASE (PCM_BASE + 4u)

/* Bytes consumed per 60 Hz VSYNC: 44100 * 4 / 60 = 2940 */
#define BYTES_PER_VSYNC 2940u

/* Maximum bytes to pre-fill (one guard frame kept empty) */
#define PCM_PREFILL (PCM_BUF_SIZE - 4u)

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

    /* Write to end of ring, then wrap and write remainder */
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
    unsigned long data_size, remaining;
    unsigned chunk, n;
    unsigned char last_vsync;
    unsigned char drain;

    fd = open("test.wav", O_RDONLY);
    if (fd < 0) {
        printf("Cannot open test.wav\n");
        return 1;
    }

    /* Validate a simple 44-byte WAV header (no LIST or extra chunks). */
    if (read(fd, hdr, 44) != 44
        || memcmp(hdr,      "RIFF", 4) != 0
        || memcmp(hdr +  8, "WAVE", 4) != 0
        || memcmp(hdr + 12, "fmt ", 4) != 0
        || memcmp(hdr + 36, "data", 4) != 0) {
        printf("test.wav: not a simple 44-byte-header WAV\n");
        close(fd);
        return 1;
    }
    if (read_le16(hdr + 20) != 1u       /* PCM, uncompressed */
        || read_le16(hdr + 22) != 2u    /* stereo             */
        || read_le32(hdr + 24) != 44100ul /* 44100 Hz          */
        || read_le16(hdr + 34) != 16u) { /* 16-bit            */
        printf("test.wav: need PCM stereo 44100 Hz 16-bit\n");
        close(fd);
        return 1;
    }

    data_size = read_le32(hdr + 40);
    printf("Playing %lu bytes...\n", data_size);

    /* --- Init XRAM control header ---------------------------------------- */
    RIA.addr0 = PCM_BASE;
    RIA.step0 = 1;
    RIA.rw0 = 0;        /* write_ptr lo  */
    RIA.rw0 = 0;        /* write_ptr hi  */
    RIA.rw0 = 1;        /* flags: play   */
    RIA.rw0 = PCM_LOG2; /* buf_size_log2 */

    wav_wp = 0;
    remaining = data_size;

    /* --- Pre-fill ring buffer before activating the driver --------------- */
    chunk = PCM_PREFILL;
    if ((unsigned long)chunk > remaining)
        chunk = (unsigned)remaining;
    n = ring_write(fd, chunk);
    remaining -= (unsigned long)n;

    /* --- Activate PCM driver --------------------------------------------- */
    xreg(0, 1, 2, PCM_BASE);

    last_vsync = RIA.vsync;

    /* --- Main loop: feed buffer once per VSYNC --------------------------- *
     *
     * At 44 100 Hz stereo 16-bit the driver consumes 2 940 bytes per 60 Hz
     * VSYNC frame.  Pre-filling PCM_PREFILL bytes and then writing exactly
     * BYTES_PER_VSYNC per frame keeps the buffer at a steady level without
     * needing to read the RP2350's internal read pointer.
     */
    while (remaining > 0) {
        while (RIA.vsync == last_vsync)
            ;
        last_vsync = RIA.vsync;

        chunk = BYTES_PER_VSYNC;
        if ((unsigned long)chunk > remaining)
            chunk = (unsigned)remaining;

        n = ring_write(fd, chunk);
        if (n > 0)
            remaining -= (unsigned long)n;
        else
            remaining = 0;
    }

    /* --- Wait for the ring buffer to drain ------------------------------- *
     * Buffer holds up to PCM_PREFILL bytes (~23 ms at 44100 Hz stereo).
     * Three VSYNC periods (50 ms) is more than sufficient.
     */
    drain = 3;
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
