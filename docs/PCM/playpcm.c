/*
 * playpcm.c - WAV audio playback example for RP6502 (cc65 / C89)
 *
 * Plays test.wav using the PCM audio driver (XREG channel 0x102).
 * Format is detected automatically from the WAV header.
 * Supported: AudioFormat=PCM, 1-2 channels,
 *            8000/11025/16000/22050/32000/44100 Hz,
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

typedef struct {
    unsigned char fmt;
    unsigned char frame_sz;
    unsigned char rate_lo;
    unsigned char rate_hi;
} pcm_params_t;

static unsigned wav_wp;

/* ---- ring buffer --------------------------------------------------------- */

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

/* ---- playback helpers --------------------------------------------------- */

static int wav_open(const char *name, int *fd_out,
                    unsigned *channels, unsigned long *rate,
                    unsigned *bits, unsigned long *data_size)
{
    unsigned char hdr[44];
    int fd;

    fd = open(name, O_RDONLY);
    if (fd < 0) {
        printf("Cannot open %s\n", name);
        return -1;
    }
    if (read(fd, hdr, 44) != 44
        || memcmp(hdr,      "RIFF", 4) != 0
        || memcmp(hdr +  8, "WAVE", 4) != 0
        || memcmp(hdr + 12, "fmt ", 4) != 0
        || memcmp(hdr + 36, "data", 4) != 0) {
        printf("%s: not a simple 44-byte-header WAV\n", name);
        close(fd);
        return -1;
    }
    *channels  = read_le16(hdr + 22);
    *rate      = read_le32(hdr + 24);
    *bits      = read_le16(hdr + 34);
    *data_size = read_le32(hdr + 40);
    if (read_le16(hdr + 20) != 1u
        || (*channels != 1u && *channels != 2u)
        || (*bits != 8u && *bits != 16u)
        || (*rate != 8000ul  && *rate != 11025ul && *rate != 16000ul
         && *rate != 22050ul && *rate != 32000ul && *rate != 44100ul)) {
        printf("%s: unsupported format\n", name);
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}

static void pcm_derive(unsigned channels, unsigned bits, unsigned long rate,
                       pcm_params_t *p)
{
    p->fmt    = 0;
    if (channels == 1u) p->fmt |= 0x01u;
    if (bits    == 8u)  p->fmt |= 0x06u;
    p->frame_sz = (unsigned char)(channels * (bits / 8u));
    p->rate_lo  = (unsigned char)(rate & 0xFFul);
    p->rate_hi  = (unsigned char)((rate >> 8) & 0xFFul);
}

static void pcm_header_write(unsigned char fmt, unsigned char log2,
                             unsigned char rate_lo, unsigned char rate_hi)
{
    RIA.addr0 = PCM_BASE;
    RIA.step0 = 1;
    RIA.rw0 = 0;        /* write_ptr lo   */
    RIA.rw0 = 0;        /* write_ptr hi   */
    RIA.rw0 = fmt;      /* format         */
    RIA.rw0 = log2;     /* buf_size_log2  */
    RIA.rw0 = rate_lo;  /* sample_rate lo */
    RIA.rw0 = rate_hi;  /* sample_rate hi */
    RIA.rw0 = 0;        /* read_ptr lo    */
    RIA.rw0 = 0;        /* read_ptr hi    */
}

static void pcm_prefill(int fd, unsigned frame_sz, unsigned long *remaining)
{
    unsigned chunk = PCM_BUF_SIZE - frame_sz;
    unsigned n;
    if ((unsigned long)chunk > *remaining)
        chunk = (unsigned)*remaining;
    n = ring_write(fd, chunk);
    *remaining -= (unsigned long)n;
}

static unsigned pcm_rptr(void)
{
    unsigned lo, hi;
    RIA.addr0 = PCM_BASE + 6;
    RIA.step0 = 1;
    lo = RIA.rw0;
    hi = RIA.rw0;
    return lo | (hi << 8);
}

static unsigned pcm_free(unsigned frame_sz)
{
    unsigned free = (pcm_rptr() - wav_wp - frame_sz) & PCM_BUF_MASK;
    return free & ~((unsigned)(frame_sz - 1u));
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    int fd;
    unsigned wav_channels, wav_bits, chunk, n;
    unsigned long wav_rate, data_size, remaining;
    pcm_params_t p;

    if (wav_open("test.wav", &fd, &wav_channels, &wav_rate, &wav_bits, &data_size) != 0)
        return 1;

    pcm_derive(wav_channels, wav_bits, wav_rate, &p);
    printf("Playing %lu bytes, %u ch, %lu Hz, %u-bit...\n",
           data_size, wav_channels, wav_rate, wav_bits);

    wav_wp    = 0;
    remaining = data_size;
    pcm_header_write(p.fmt, PCM_LOG2, p.rate_lo, p.rate_hi);
    pcm_prefill(fd, p.frame_sz, &remaining);
    xreg(0, 1, 2, PCM_BASE);

    while (remaining > 0) {
        chunk = pcm_free(p.frame_sz);
        if (chunk > 0) {
            if ((unsigned long)chunk > remaining)
                chunk = (unsigned)remaining;
            n = ring_write(fd, chunk);
            if (n > 0) remaining -= (unsigned long)n; else remaining = 0;
        }
    }

    while (pcm_rptr() != wav_wp)
        ;

    xreg(0, 1, 2, 0xFFFF);
    close(fd);
    printf("Done.\n");
    return 0;
}
