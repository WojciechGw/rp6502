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

#define VU_PEEK 32u  /* bytes read from ring per VSYNC for level metering */
#define VU_BAR  10u  /* bar chart width in characters */

typedef struct {
    unsigned char fmt;
    unsigned char frame_sz;
    unsigned char rate_lo;
    unsigned char rate_hi;
} pcm_params_t;

static unsigned      wav_wp;
static unsigned char last_vsync;
static unsigned char vu_peak_l;
static unsigned char vu_peak_r;

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

/*
 * Read read_ptr written by RP2350 at +6..+7.  Called at drain time only
 * (once per VSYNC) so torn-read probability is negligible.
 */
static unsigned pcm_rptr(void)
{
    unsigned lo, hi;
    RIA.addr0 = PCM_BASE + 6;
    RIA.step0 = 1;
    lo = RIA.rw0;
    hi = RIA.rw0;
    return lo | (hi << 8);
}

static void wait_vsync(void)
{
    while (RIA.vsync == last_vsync)
        ;
    last_vsync = RIA.vsync;
}

/*
 * Bresenham feeder: called once per VSYNC.
 * Distributes fractional bytes/VSYNC evenly across frames.
 */
static void feed_vsync(int fd, unsigned long bps,
                       unsigned long *remaining, unsigned long *bres)
{
    unsigned chunk, n;
    *bres += bps;
    chunk = (unsigned)(*bres / 60ul);
    *bres -= (unsigned long)chunk * 60ul;
    if ((unsigned long)chunk > *remaining)
        chunk = (unsigned)*remaining;
    n = ring_write(fd, chunk);
    if (n > 0)
        *remaining -= (unsigned long)n;
    else
        *remaining = 0;
}

/* ---- VU meter ------------------------------------------------------------ */

/*
 * Read VU_PEEK bytes from the ring buffer in XRAM (just before wav_wp),
 * compute per-channel peak (0-255) and apply decay to vu_peak_l/r.
 */
static void vu_sample(unsigned char fmt, unsigned char frame_sz)
{
    static unsigned char buf[VU_PEEK];
    unsigned i, peek, uv;
    unsigned char new_l, new_r, lv, rv;
    int s;

    peek = (wav_wp >= VU_PEEK) ? wav_wp - VU_PEEK : 0u;
    RIA.addr0 = PCM_RING_BASE + peek;
    RIA.step0 = 1;
    for (i = 0; i < VU_PEEK; i++)
        buf[i] = RIA.rw0;

    new_l = 0; new_r = 0;
    for (i = 0; i + frame_sz <= VU_PEEK; i += frame_sz) {
        if (fmt & 2) {
            /* 8-bit */
            if (fmt & 4) {
                /* unsigned (standard WAV 8-bit) */
                uv = (buf[i] >= 128u) ? buf[i] - 128u : 128u - buf[i];
            } else {
                /* signed */
                s  = (int)(signed char)buf[i];
                uv = (s < 0) ? (unsigned)(-s) : (unsigned)s;
            }
            lv = (unsigned char)(uv > 127u ? 255u : uv * 2u);
            if (fmt & 1) {
                rv = lv;
            } else {
                if (fmt & 4) {
                    uv = (buf[i+1] >= 128u) ? buf[i+1] - 128u : 128u - buf[i+1];
                } else {
                    s  = (int)(signed char)buf[i+1];
                    uv = (s < 0) ? (unsigned)(-s) : (unsigned)s;
                }
                rv = (unsigned char)(uv > 127u ? 255u : uv * 2u);
            }
        } else {
            /* 16-bit signed */
            s = (int)((unsigned)buf[i] | ((unsigned)buf[i+1] << 8));
            if (s < -32767) s = -32767;
            uv = (s < 0) ? (unsigned)(-s) : (unsigned)s;
            lv = (unsigned char)(uv >> 7);
            if (fmt & 1) {
                rv = lv;
            } else {
                s = (int)((unsigned)buf[i+2] | ((unsigned)buf[i+3] << 8));
                if (s < -32767) s = -32767;
                uv = (s < 0) ? (unsigned)(-s) : (unsigned)s;
                rv = (unsigned char)(uv >> 7);
            }
        }
        if (lv > new_l) new_l = lv;
        if (rv > new_r) new_r = rv;
    }

    if (new_l > vu_peak_l) vu_peak_l = new_l;
    else if (vu_peak_l)    vu_peak_l = (unsigned char)(vu_peak_l * 7u / 8u);
    if (new_r > vu_peak_r) vu_peak_r = new_r;
    else if (vu_peak_r)    vu_peak_r = (unsigned char)(vu_peak_r * 7u / 8u);
}

/* Draw two bar lines in place (moves cursor up 2, overwrites). */
static void vu_draw(void)
{
    /* 5 (cursor) + 2 * (3 + 4 + VU_BAR + 4 + VU_BAR + 6) worst case */
    static char buf[5 + 2 * (3 + 4 + VU_BAR + 4 + VU_BAR + 6)];
    char *p = buf;
    unsigned i, bar_l, bar_r;

    bar_l = (unsigned)vu_peak_l * VU_BAR / 255u;
    bar_r = (unsigned)vu_peak_r * VU_BAR / 255u;

    *p++ = '\033'; *p++ = '['; *p++ = '2'; *p++ = 'A'; *p++ = '\r';

    *p++ = 'L'; *p++ = ' '; *p++ = '[';
    *p++ = '\033'; *p++ = '['; *p++ = '3'; *p++ = '2'; *p++ = 'm';
    for (i = 0; i < bar_l; i++) *p++ = '#';
    *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm';
    for (i = bar_l; i < VU_BAR; i++) *p++ = ' ';
    *p++ = ']'; *p++ = '\033'; *p++ = '['; *p++ = 'K'; *p++ = '\r'; *p++ = '\n';

    *p++ = 'R'; *p++ = ' '; *p++ = '[';
    *p++ = '\033'; *p++ = '['; *p++ = '3'; *p++ = '2'; *p++ = 'm';
    for (i = 0; i < bar_r; i++) *p++ = '#';
    *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm';
    for (i = bar_r; i < VU_BAR; i++) *p++ = ' ';
    *p++ = ']'; *p++ = '\033'; *p++ = '['; *p++ = 'K'; *p++ = '\r'; *p++ = '\n';

    write(STDOUT_FILENO, buf, (unsigned)(p - buf));
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    int fd;
    unsigned wav_channels, wav_bits;
    unsigned long wav_rate, data_size, remaining, bres, bps;
    pcm_params_t p;

    if (wav_open("test.wav", &fd, &wav_channels, &wav_rate, &wav_bits, &data_size) != 0)
        return 1;

    pcm_derive(wav_channels, wav_bits, wav_rate, &p);
    bps = wav_rate * (unsigned long)p.frame_sz;
    printf("Playing %lu bytes, %u ch, %lu Hz, %u-bit...\n",
           data_size, wav_channels, wav_rate, wav_bits);

    wav_wp    = 0;
    remaining = data_size;
    pcm_header_write(p.fmt, PCM_LOG2, p.rate_lo, p.rate_hi);
    pcm_prefill(fd, p.frame_sz, &remaining);

    printf("\033[?25l\r\n\r\n");   /* hide cursor; two blank lines for VU bars */
    xreg(0, 1, 2, PCM_BASE);

    last_vsync = RIA.vsync;
    bres = 0;
    while (remaining > 0) {
        wait_vsync();
        feed_vsync(fd, bps, &remaining, &bres);
        vu_sample(p.fmt, p.frame_sz);
        vu_draw();
    }

    while (pcm_rptr() != wav_wp) {
        wait_vsync();
        vu_sample(p.fmt, p.frame_sz);
        vu_draw();
    }

    vu_peak_l = vu_peak_r = 0;
    vu_draw();

    printf("\033[?25h");           /* show cursor */
    xreg(0, 1, 2, 0xFFFF);
    close(fd);
    printf("Done.\n");
    return 0;
}
