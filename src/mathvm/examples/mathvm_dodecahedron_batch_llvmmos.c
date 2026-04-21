/*
 * Float-friendly llvm-mos MATHVM batch dodecahedron animation.
 *
 * One MATHVM call per frame projects all 20 vertices of a regular
 * dodecahedron from XRAM input records to packed int16 x/y output records.
 *
 * Unlike the cc65 example, this version keeps geometry and transform math in
 * ordinary float form on the caller side and uses the llvm-mos client layer
 * to pack data into the current MATHVM ABI.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-License-Identifier: Unlicense
 */

#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "mathvm/mathvm_client_llvmmos.h"

#define KEYBOARD_ADDR 0xFFE0u

#define KEY_F4         0x3Du
#define KEY_LEFT_ALT   0xE2u
#define KEY_RIGHT_ALT  0xE6u

#define FB_WIDTH   640
#define FB_HEIGHT  360
#define FB_SIZE    28800u
#define FB_STRIDE  80u
#define FB_A       0x0000u
#define FB_B       0x7080u
#define CFG_ADDR   0xFF00u

#define CX  320
#define CY  180

#define XRAM_VERT_IN   0xE100u

#define DODECA_VERTS 20
#define DODECA_EDGES 30
#define DODECA_FACES 12
#define DODECA_FRAMES 360
#define TIMING_REPORT_FRAMES 60u
#define PRECOMP_BAR_X  120
#define PRECOMP_BAR_Y  172
#define PRECOMP_BAR_W  400
#define PRECOMP_BAR_H  16

#define CLEAR_MODE_FULL           0
#define CLEAR_MODE_DIRTY_SPANS    1
#define CLEAR_MODE_ERASE_GEOMETRY 2

#ifndef DODECA_CLEAR_MODE
#define DODECA_CLEAR_MODE CLEAR_MODE_DIRTY_SPANS
#endif

static const mx_vec3f_t verts[DODECA_VERTS] = {
    {-28.0f, -28.0f, -28.0f},
    { 28.0f, -28.0f, -28.0f},
    { 28.0f,  28.0f, -28.0f},
    {-28.0f,  28.0f, -28.0f},
    {-28.0f, -28.0f,  28.0f},
    { 28.0f, -28.0f,  28.0f},
    { 28.0f,  28.0f,  28.0f},
    {-28.0f,  28.0f,  28.0f},
    {  0.0f, -17.0f, -45.0f},
    {  0.0f,  17.0f, -45.0f},
    {  0.0f, -17.0f,  45.0f},
    {  0.0f,  17.0f,  45.0f},
    {-17.0f, -45.0f,   0.0f},
    { 17.0f, -45.0f,   0.0f},
    {-17.0f,  45.0f,   0.0f},
    { 17.0f,  45.0f,   0.0f},
    {-45.0f,   0.0f, -17.0f},
    { 45.0f,   0.0f, -17.0f},
    {-45.0f,   0.0f,  17.0f},
    { 45.0f,   0.0f,  17.0f},
};

static const unsigned char edges[DODECA_EDGES][2] = {
    {0,8}, {0,12}, {0,16}, {1,8}, {1,13}, {1,17},
    {2,9}, {2,15}, {2,17}, {3,9}, {3,14}, {3,16},
    {4,10}, {4,12}, {4,18}, {5,10}, {5,13}, {5,19},
    {6,11}, {6,15}, {6,19}, {7,11}, {7,14}, {7,18},
    {8,9}, {10,11}, {12,13}, {14,15}, {16,18}, {17,19},
};

static const unsigned char faces[DODECA_FACES][5] = {
    {0,8,1,13,12},
    {0,16,3,9,8},
    {0,12,4,18,16},
    {1,8,9,2,17},
    {1,17,19,5,13},
    {2,9,3,14,15},
    {2,15,6,19,17},
    {3,16,18,7,14},
    {4,12,13,5,10},
    {4,10,11,7,18},
    {5,19,6,11,10},
    {6,15,14,7,11},
};

static const unsigned char edge_faces[DODECA_EDGES][2] = {
    {0,1}, {0,2}, {1,2}, {0,3}, {0,4}, {3,4},
    {3,5}, {5,6}, {3,6}, {1,5}, {5,7}, {1,7},
    {8,9}, {2,8}, {2,9}, {8,10}, {4,8}, {4,10},
    {10,11}, {6,11}, {6,10}, {9,11}, {7,11}, {7,9},
    {1,3}, {9,10}, {0,8}, {5,11}, {2,7}, {4,6},
};

static const unsigned char bitmask[8] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

static int proj_x[DODECA_VERTS];
static int proj_y[DODECA_VERTS];
static mx_point2i_t precomputed_frames[DODECA_FRAMES][DODECA_VERTS];
static bool face_front[DODECA_FACES];
static bool vertex_visible[DODECA_VERTS];
#if DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
static bool edge_drawn[DODECA_EDGES];
#endif
static clock_t project_ticks_accum;
static clock_t frame_ticks_accum;
static uint8_t project_samples;
static uint8_t frame_samples;
static unsigned back_buf;
static uint16_t precompute_fill_px;

#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
typedef struct
{
    uint8_t min_byte[FB_HEIGHT];
    uint8_t max_byte[FB_HEIGHT];
} dirty_span_state_t;

static dirty_span_state_t dirty_spans[2];
#elif DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
typedef struct
{
    int16_t proj_x[DODECA_VERTS];
    int16_t proj_y[DODECA_VERTS];
    bool edge_drawn[DODECA_EDGES];
    bool vertex_visible[DODECA_VERTS];
    bool valid;
} frame_history_t;

static frame_history_t frame_history[2];
#endif

static bool key_down(unsigned char code)
{
    RIA.addr1 = KEYBOARD_ADDR + (unsigned)(code >> 3);
    RIA.step1 = 0;
    return (RIA.rw1 & (1u << (code & 7))) != 0;
}

static bool alt_f4_pressed(void)
{
    return key_down(KEY_F4)
        && (key_down(KEY_LEFT_ALT) || key_down(KEY_RIGHT_ALT));
}

static uint8_t back_buf_index(void)
{
    return (back_buf == FB_A) ? 0u : 1u;
}

#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
static void init_dirty_spans(void)
{
    uint8_t bank;
    unsigned y;

    for (bank = 0u; bank < 2u; ++bank)
    {
        for (y = 0; y < FB_HEIGHT; ++y)
        {
            dirty_spans[bank].min_byte[y] = 0xFFu;
            dirty_spans[bank].max_byte[y] = 0u;
        }
    }
}

static void mark_dirty_byte(unsigned x, unsigned y)
{
    dirty_span_state_t *state = &dirty_spans[back_buf_index()];
    uint8_t byte_index = (uint8_t)(x >> 3);

    if (state->min_byte[y] == 0xFFu || byte_index < state->min_byte[y])
        state->min_byte[y] = byte_index;
    if (byte_index > state->max_byte[y])
        state->max_byte[y] = byte_index;
}
#endif

static void plot_pixel(int x, int y, bool on)
{
    unsigned addr;
    uint8_t mask;

    if ((unsigned)x >= (unsigned)FB_WIDTH || (unsigned)y >= (unsigned)FB_HEIGHT)
        return;

#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
    if (on)
        mark_dirty_byte((unsigned)x, (unsigned)y);
#endif

    addr = back_buf
         + (((unsigned)y << 6) + ((unsigned)y << 4))
         + ((unsigned)x >> 3);
    RIA.addr0 = addr;
    mask = bitmask[x & 7];
    if (on)
        RIA.rw0 = RIA.rw0 | mask;
    else
        RIA.rw0 = RIA.rw0 & (uint8_t)~mask;
}

static bool point_reasonable(int x, int y)
{
    return x > -FB_WIDTH && x < (FB_WIDTH * 2) &&
           y > -FB_HEIGHT && y < (FB_HEIGHT * 2);
}

static bool face_is_front(uint8_t face_index)
{
    int32_t area2 = 0;
    uint8_t i;

    for (i = 0; i < 5u; ++i)
    {
        uint8_t a = faces[face_index][i];
        uint8_t b = faces[face_index][(uint8_t)((i + 1u) % 5u)];

        area2 += (int32_t)proj_x[a] * (int32_t)proj_y[b]
              -  (int32_t)proj_y[a] * (int32_t)proj_x[b];
    }

    return area2 < 0;
}

static void report_projection_time_sample(clock_t elapsed)
{
    unsigned long avg_ticks;
    unsigned long sec_whole;
    unsigned long sec_frac;

    project_ticks_accum += elapsed;
    ++project_samples;
    if (project_samples < TIMING_REPORT_FRAMES)
        return;

    avg_ticks = (unsigned long)project_ticks_accum / (unsigned long)project_samples;
    sec_whole = avg_ticks / (unsigned long)CLOCKS_PER_SEC;
    sec_frac = ((avg_ticks % (unsigned long)CLOCKS_PER_SEC) * 1000000UL)
             / (unsigned long)CLOCKS_PER_SEC;

    printf("mx_client_project_vec3f_batch_f32 avg: %lu.%06lu s\n",
           sec_whole,
           sec_frac);

    project_ticks_accum = 0;
    project_samples = 0u;
}

static void report_frame_time_sample(clock_t elapsed)
{
    unsigned long avg_ticks;
    unsigned long sec_whole;
    unsigned long sec_frac;

    frame_ticks_accum += elapsed;
    ++frame_samples;
    if (frame_samples < TIMING_REPORT_FRAMES)
        return;

    avg_ticks = (unsigned long)frame_ticks_accum / (unsigned long)frame_samples;
    sec_whole = avg_ticks / (unsigned long)CLOCKS_PER_SEC;
    sec_frac = ((avg_ticks % (unsigned long)CLOCKS_PER_SEC) * 1000000UL)
             / (unsigned long)CLOCKS_PER_SEC;

    printf("back buffer frame avg: %lu.%06lu s\n",
           sec_whole,
           sec_frac);

    frame_ticks_accum = 0;
    frame_samples = 0u;
}

static void draw_line_color(int x0, int y0, int x1, int y1, bool on)
{
    int dx;
    int dy;
    int adx;
    int ady;
    int sx;
    int sy;
    int err;

    dx  = x1 - x0;
    dy  = y1 - y0;
    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;
    sx  = dx < 0 ? -1 : 1;
    sy  = dy < 0 ? -1 : 1;

    plot_pixel(x0, y0, on);
    if (adx >= ady)
    {
        err = adx >> 1;
        while (x0 != x1)
        {
            err -= ady;
            if (err < 0)
            {
                y0 += sy;
                err += adx;
            }
            x0 += sx;
            plot_pixel(x0, y0, on);
        }
    }
    else
    {
        err = ady >> 1;
        while (y0 != y1)
        {
            err -= adx;
            if (err < 0)
            {
                x0 += sx;
                err += ady;
            }
            y0 += sy;
            plot_pixel(x0, y0, on);
        }
    }
}

static void draw_line(int x0, int y0, int x1, int y1)
{
    draw_line_color(x0, y0, x1, y1, true);
}

static void fill_rect(int x, int y, int w, int h, bool on)
{
    int yy;
    int xx;

    for (yy = 0; yy < h; ++yy)
    {
        for (xx = 0; xx < w; ++xx)
            plot_pixel(x + xx, y + yy, on);
    }
}

static void clear_fb_full(void)
{
    unsigned i;

    RIA.addr0 = back_buf;
    RIA.step0 = 1;
    for (i = 0; i < FB_SIZE / 8u; ++i)
    {
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
        RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0; RIA.rw0 = 0;
    }
}

#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
static void clear_fb_dirty_spans(void)
{
    dirty_span_state_t *state = &dirty_spans[back_buf_index()];
    unsigned y;

    for (y = 0; y < FB_HEIGHT; ++y)
    {
        uint8_t min_byte = state->min_byte[y];
        uint8_t max_byte = state->max_byte[y];
        uint8_t i;

        if (min_byte == 0xFFu)
            continue;

        RIA.addr0 = back_buf + (((unsigned)y << 6) + ((unsigned)y << 4)) + min_byte;
        RIA.step0 = 1;
        for (i = min_byte; i <= max_byte; ++i)
            RIA.rw0 = 0;

        state->min_byte[y] = 0xFFu;
        state->max_byte[y] = 0u;
    }
}
#elif DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
static void clear_fb_erase_geometry(void)
{
    frame_history_t *state = &frame_history[back_buf_index()];
    int e;

    if (!state->valid)
        return;

    RIA.step0 = 0;
    for (e = 0; e < DODECA_EDGES; ++e)
    {
        if (!state->edge_drawn[e])
            continue;

        draw_line_color(state->proj_x[edges[e][0]],
                        state->proj_y[edges[e][0]],
                        state->proj_x[edges[e][1]],
                        state->proj_y[edges[e][1]],
                        false);
    }

    for (e = 0; e < DODECA_VERTS; ++e)
    {
        if (state->vertex_visible[e])
            plot_pixel(state->proj_x[e], state->proj_y[e], false);
    }

    state->valid = false;
}

static void save_frame_history(void)
{
    frame_history_t *state = &frame_history[back_buf_index()];
    int i;

    for (i = 0; i < DODECA_VERTS; ++i)
    {
        state->proj_x[i] = (int16_t)proj_x[i];
        state->proj_y[i] = (int16_t)proj_y[i];
        state->vertex_visible[i] = vertex_visible[i];
    }
    for (i = 0; i < DODECA_EDGES; ++i)
        state->edge_drawn[i] = edge_drawn[i];

    state->valid = true;
}
#endif

static void clear_fb(void)
{
#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
    clear_fb_dirty_spans();
#elif DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
    clear_fb_erase_geometry();
#else
    clear_fb_full();
#endif
}

#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
static void reset_clear_state(void)
{
    init_dirty_spans();
}
#elif DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
static void reset_clear_state(void)
{
    frame_history[0].valid = false;
    frame_history[1].valid = false;
}
#else
static void reset_clear_state(void)
{
}
#endif

static void precompute_progress_begin(void)
{
    back_buf = FB_A;
    clear_fb_full();
    RIA.step0 = 0;
    draw_line_color(PRECOMP_BAR_X, PRECOMP_BAR_Y,
                    PRECOMP_BAR_X + PRECOMP_BAR_W - 1, PRECOMP_BAR_Y, true);
    draw_line_color(PRECOMP_BAR_X, PRECOMP_BAR_Y + PRECOMP_BAR_H - 1,
                    PRECOMP_BAR_X + PRECOMP_BAR_W - 1, PRECOMP_BAR_Y + PRECOMP_BAR_H - 1, true);
    draw_line_color(PRECOMP_BAR_X, PRECOMP_BAR_Y,
                    PRECOMP_BAR_X, PRECOMP_BAR_Y + PRECOMP_BAR_H - 1, true);
    draw_line_color(PRECOMP_BAR_X + PRECOMP_BAR_W - 1, PRECOMP_BAR_Y,
                    PRECOMP_BAR_X + PRECOMP_BAR_W - 1, PRECOMP_BAR_Y + PRECOMP_BAR_H - 1, true);
    precompute_fill_px = 0u;
}

static void precompute_progress_update(uint16_t completed)
{
    uint16_t target_fill = (uint16_t)(((uint32_t)completed * (PRECOMP_BAR_W - 2)) / DODECA_FRAMES);

    if (target_fill <= precompute_fill_px)
        return;

    RIA.step0 = 0;
    fill_rect(PRECOMP_BAR_X + 1 + (int)precompute_fill_px,
              PRECOMP_BAR_Y + 1,
              (int)(target_fill - precompute_fill_px),
              PRECOMP_BAR_H - 2,
              true);
    precompute_fill_px = target_fill;
}

static void precompute_progress_end(void)
{
    back_buf = FB_A;
    clear_fb_full();
    back_buf = FB_B;
    clear_fb_full();
    reset_clear_state();
}

static bool precompute_frames(void)
{
    int angle;

    precompute_progress_begin();
    for (angle = 0; angle < DODECA_FRAMES; ++angle)
    {
        const float radians = (float)angle * (3.14159265358979323846f / 180.0f);
        const float sin_a = sinf(radians);
        const float cos_a = cosf(radians);
        const float sin30 = 0.5f;
        const float cos30 = 0.86602540378443864676f;
        const float mat3[9] = {
             cos_a,            0.0f,           sin_a,
             sin_a * sin30,    cos30,         -cos_a * sin30,
            -sin_a * cos30,    sin30,          cos_a * cos30
        };
        mx_client_result_t call;
        clock_t t0;

        t0 = clock();
        call = mx_client_project_vec3f_batch_f32(mat3,
                                                 200.0f,
                                                 320.0f,
                                                 180.0f,
                                                 XRAM_VERT_IN,
                                                 verts,
                                                 precomputed_frames[angle],
                                                 DODECA_VERTS);
        precompute_progress_update((uint16_t)(angle + 1));
        report_projection_time_sample(clock() - t0);
        if (call.status != MX_OK || call.out_words != 0u)
            return false;
    }

    precompute_progress_end();
    return true;
}

static void load_precomputed_frame(uint16_t angle)
{
    uint8_t i;

    for (i = 0; i < DODECA_VERTS; ++i)
    {
        proj_x[i] = (int)precomputed_frames[angle][i].x;
        proj_y[i] = (int)precomputed_frames[angle][i].y;
    }
}

void main(void)
{
    int angle = 0;
    clock_t frame_t0;
    uint8_t v;
    int e;

    xreg_vga_canvas(4);

    back_buf = FB_A;
    clear_fb_full();
    back_buf = FB_B;
    clear_fb_full();
    reset_clear_state();

    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, x_wrap,           false);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, y_wrap,           false);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, x_pos_px,         0);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, y_pos_px,         0);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, width_px,         FB_WIDTH);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, height_px,        FB_HEIGHT);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, xram_data_ptr,    FB_A);
    xram0_struct_set(CFG_ADDR, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    xreg_vga_mode(3, 0, CFG_ADDR, 0, 0, 0);
    xreg_ria_keyboard(KEYBOARD_ADDR);
#if DODECA_CLEAR_MODE == CLEAR_MODE_DIRTY_SPANS
    puts("Clear mode: dirty spans");
#elif DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
    puts("Clear mode: erase geometry");
#else
    puts("Clear mode: full clear");
#endif
    puts("Precomputing 360 projected frames at 1 degree steps.");
    puts("Projection avg is reported during precompute.");
    if (!precompute_frames())
    {
        xreg_ria_keyboard(0xFFFF);
        exit(1);
    }
    puts("Animation uses precomputed vertex frames.");

    back_buf = FB_B;

    while (1)
    {
        if (alt_f4_pressed())
            break;

        frame_t0 = clock();
        clear_fb();
        load_precomputed_frame((uint16_t)angle);

        for (e = 0; e < DODECA_FACES; ++e)
            face_front[e] = face_is_front((uint8_t)e);

        for (e = 0; e < DODECA_VERTS; ++e)
            vertex_visible[e] = false;
#if DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
        for (e = 0; e < DODECA_EDGES; ++e)
            edge_drawn[e] = false;
#endif

        RIA.step0 = 0;
        for (e = 0; e < DODECA_EDGES; ++e)
        {
            unsigned char va = edges[e][0];
            unsigned char vb = edges[e][1];
            int ax = proj_x[edges[e][0]];
            int ay = proj_y[edges[e][0]];
            int bx = proj_x[edges[e][1]];
            int by = proj_y[edges[e][1]];

            if (!face_front[edge_faces[e][0]] &&
                !face_front[edge_faces[e][1]])
                continue;

            vertex_visible[va] = true;
            vertex_visible[vb] = true;
            if (point_reasonable(ax, ay) && point_reasonable(bx, by))
            {
                draw_line(ax, ay, bx, by);
#if DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
                edge_drawn[e] = true;
#endif
            }
        }

        for (e = 0; e < DODECA_VERTS; ++e)
        {
            if (vertex_visible[e])
                plot_pixel(proj_x[e], proj_y[e], true);
        }
#if DODECA_CLEAR_MODE == CLEAR_MODE_ERASE_GEOMETRY
        save_frame_history();
#endif
        report_frame_time_sample(clock() - frame_t0);

        v = RIA.vsync;
        while (RIA.vsync == v)
            ;

        xram0_struct_set(CFG_ADDR, vga_mode3_config_t, xram_data_ptr, back_buf);
        back_buf = (back_buf == FB_A) ? FB_B : FB_A;

        ++angle;
        if (angle >= DODECA_FRAMES)
            angle = 0;
    }

    xreg_ria_keyboard(0xFFFF);
    exit(0);
}
