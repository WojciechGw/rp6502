/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The debug window of the PICO-SRAM-BANKED module (CPB) in the SRAM socket:
 * the bus as the module sees it after the last cycle, its control state, the
 * live map, the sixteen task maps, and the 256 blocks with a read-only view of
 * one of them. BA11 to BA18 are not module pins; they are the block the live
 * map turns the address into, which is where the access lands.
 *
 * Like ui_ria.h, this header carries its implementation under CHIPS_UI_IMPL,
 * and the unit that defines that must have included ImGui already.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "chips/ui/ui_chip.h"
#include "chips/ui/ui_settings.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    const char *title;
    int x, y, w, h; /* initial geometry; a 0 width or height takes the default */
    bool open;
} ui_cpb_desc_t;

typedef struct
{
    const char *title;
    float init_x, init_y, init_w, init_h;
    bool open;
    bool valid;
    int sel_task;
    int sel_block;
    ui_chip_t chip;
} ui_cpb_t;

void ui_cpb_init(ui_cpb_t *win, const ui_cpb_desc_t *desc);
void ui_cpb_discard(ui_cpb_t *win);
void ui_cpb_draw(ui_cpb_t *win);
void ui_cpb_save_settings(ui_cpb_t *win, ui_settings_t *settings);
void ui_cpb_load_settings(ui_cpb_t *win, const ui_settings_t *settings);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_UI_IMPL
#include <cstdio>
#include <string.h>
#ifndef CHIPS_ASSERT
#include <assert.h>
#define CHIPS_ASSERT(c) assert(c)
#endif
#include "chips/chips/w65c02.h"
extern "C"
{
#include "core/sys/sys.h"
#include "core/wdc/cpb.h"
#include "core/wdc/cpu.h"
}

#define UI_CPB_PIN_RWB (1ULL << 24)
#define UI_CPB_PIN_PHI2 (1ULL << 25)
#define UI_CPB_PIN_CE2 (1ULL << 26)
#define UI_CPB_PIN_BA11 (1ULL << 27)

static const ui_chip_pin_t _ui_cpb_pins[] = {
    {"D0", 0, 1ULL << 16}, {"D1", 1, 1ULL << 17},
    {"D2", 2, 1ULL << 18}, {"D3", 3, 1ULL << 19},
    {"D4", 4, 1ULL << 20}, {"D5", 5, 1ULL << 21},
    {"D6", 6, 1ULL << 22}, {"D7", 7, 1ULL << 23},
    {"RWB", 9, UI_CPB_PIN_RWB}, {"PHI2", 10, UI_CPB_PIN_PHI2},
    {"CE2", 11, UI_CPB_PIN_CE2},
    {"BA11", 13, UI_CPB_PIN_BA11 << 0}, {"BA12", 14, UI_CPB_PIN_BA11 << 1},
    {"BA13", 15, UI_CPB_PIN_BA11 << 2}, {"BA14", 16, UI_CPB_PIN_BA11 << 3},
    {"BA15", 17, UI_CPB_PIN_BA11 << 4}, {"BA16", 18, UI_CPB_PIN_BA11 << 5},
    {"BA17", 19, UI_CPB_PIN_BA11 << 6}, {"BA18", 20, UI_CPB_PIN_BA11 << 7},
    {"A0", 22, 1ULL << 0}, {"A1", 23, 1ULL << 1},
    {"A2", 24, 1ULL << 2}, {"A3", 25, 1ULL << 3},
    {"A4", 26, 1ULL << 4}, {"A5", 27, 1ULL << 5},
    {"A6", 28, 1ULL << 6}, {"A7", 29, 1ULL << 7},
    {"A8", 30, 1ULL << 8}, {"A9", 31, 1ULL << 9},
    {"A10", 32, 1ULL << 10}, {"A11", 33, 1ULL << 11},
    {"A12", 34, 1ULL << 12}, {"A13", 35, 1ULL << 13},
    {"A14", 36, 1ULL << 14}, {"A15", 37, 1ULL << 15},
};

#define UI_CPB_REMAPPED ImVec4(1.0f, 0.75f, 0.2f, 1.0f)

void ui_cpb_init(ui_cpb_t *win, const ui_cpb_desc_t *desc)
{
    CHIPS_ASSERT(win && desc && desc->title);
    memset(win, 0, sizeof(*win));
    win->title = desc->title;
    win->init_x = (float)desc->x;
    win->init_y = (float)desc->y;
    win->init_w = (float)((desc->w == 0) ? 640 : desc->w);
    win->init_h = (float)((desc->h == 0) ? 560 : desc->h);
    win->open = desc->open;
    win->valid = true;
    ui_chip_desc_t cd;
    UI_CHIP_INIT_DESC(&cd, "CPB", 44, _ui_cpb_pins);
    ui_chip_init(&win->chip, &cd);
}

void ui_cpb_discard(ui_cpb_t *win)
{
    CHIPS_ASSERT(win && win->valid);
    win->valid = false;
}

static bool _ui_cpb_map_is_identity(const uint8_t *map)
{
    for (int s = 0; s < CPB_SLOTS; s++)
        if (map[s] != s)
            return false;
    return true;
}

/* One row per slot; a click on the block selects it for the block view. */
static void _ui_cpb_map_table(ui_cpb_t *win, const char *id, const uint8_t *map)
{
    if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Slot");
    ImGui::TableSetupColumn("6502");
    ImGui::TableSetupColumn("Block");
    ImGui::TableHeadersRow();
    for (int s = 0; s < CPB_SLOTS; s++)
    {
        unsigned lo = (unsigned)s << CPB_BLOCK_SHIFT;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%2d", s);
        ImGui::TableNextColumn();
        ImGui::Text("$%04X-$%04X", lo, lo + CPB_BLOCK_SIZE - 1);
        ImGui::TableNextColumn();
        char label[16];
        std::snprintf(label, sizeof label, "%3u%s", (unsigned)map[s],
                      s == CPB_PINNED_SLOT ? " pinned" : "");
        bool remapped = map[s] != s;
        if (remapped)
            ImGui::PushStyleColor(ImGuiCol_Text, UI_CPB_REMAPPED);
        ImGui::PushID(s);
        if (ImGui::Selectable(label, win->sel_block == map[s]))
            win->sel_block = map[s];
        ImGui::PopID();
        if (remapped)
            ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

static void _ui_cpb_status(const cpb_dbg_t *st, uint64_t cpu_pins)
{
    ImGui::Text("Gate:         %s", st->gate ? "OPEN" : "closed");
    ImGui::Text("Active task:  %u", (unsigned)st->active_task);
    ImGui::Text("Edit task:    %u", (unsigned)st->edit_task);
    ImGui::Text("Knock:        %.*s (%u/8)", (int)st->knock_pos, "#!RP6502",
                (unsigned)st->knock_pos);
    ImGui::Text("Resets:       %u ($FED5)", (unsigned)st->resets);
    ImGui::Text("Temperature:  %.1f C ($FED6/7 = $%02X)", st->temp / 2.0, (unsigned)st->temp);
    ImGui::Text("Delay:        %u cycles, %u queued", (unsigned)st->delay, (unsigned)st->queued);
    uint16_t addr = (uint16_t)(cpu_pins & 0xFFFF);
    uint8_t block = st->active_map[addr >> CPB_BLOCK_SHIFT];
    uint32_t phys = ((uint32_t)block << CPB_BLOCK_SHIFT) | (addr & (CPB_BLOCK_SIZE - 1));
    ImGui::Text("Bus:          %s $%04X -> block %u, $%05X", (cpu_pins & W65C02_RW) ? "R" : "W",
                (unsigned)addr, (unsigned)block, (unsigned)phys);
    ImGui::TextDisabled("CE2 is IORQB: NAND(A8..A15), low for $FF00-$FFFF");
}

static void _ui_cpb_blocks(ui_cpb_t *win, const cpb_dbg_t *st)
{
    bool in_active[CPB_BLOCKS] = {false};
    bool remapped[CPB_BLOCKS] = {false};
    bool in_other[CPB_BLOCKS] = {false};
    for (int s = 0; s < CPB_SLOTS; s++)
    {
        in_active[st->active_map[s]] = true;
        if (st->active_map[s] != s)
            remapped[st->active_map[s]] = true;
    }
    for (int t = 0; t < CPB_TASKS; t++)
        if (t != st->active_task)
            for (int s = 0; s < CPB_SLOTS; s++)
                in_other[st->task_map[t][s]] = true;

    const ImU32 c_pinned = IM_COL32(90, 130, 220, 255);
    const ImU32 c_remapped = IM_COL32(255, 190, 50, 255);
    const ImU32 c_active = IM_COL32(60, 170, 60, 255);
    const ImU32 c_other = IM_COL32(200, 90, 60, 255);
    const ImU32 c_unused = IM_COL32(70, 70, 70, 255);
    const float cell = 16.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (int b = 0; b < CPB_BLOCKS; b++)
    {
        if (b % 16)
            ImGui::SameLine(0.0f, 2.0f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(b);
        if (ImGui::InvisibleButton("##blk", ImVec2(cell, cell)))
            win->sel_block = b;
        ImU32 col = b == CPB_PINNED_SLOT ? c_pinned
                    : remapped[b]        ? c_remapped
                    : in_active[b]       ? c_active
                    : in_other[b]        ? c_other
                                         : c_unused;
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), col);
        if (b == win->sel_block)
            dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), IM_COL32_WHITE, 0.0f, 0, 2.0f);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Block %d: $%05X-$%05X", b, (unsigned)b << CPB_BLOCK_SHIFT,
                        ((unsigned)b << CPB_BLOCK_SHIFT) + CPB_BLOCK_SIZE - 1);
            int shown = 0;
            for (int t = 0; t < CPB_TASKS; t++)
                for (int s = 0; s < CPB_SLOTS; s++)
                    if (st->task_map[t][s] == b && (st->task_map[t][s] != s || t == st->active_task))
                    {
                        if (shown++ < 12)
                            ImGui::Text("task %2d%s slot %2d ($%04X)", t,
                                        t == st->active_task ? "*" : " ", s,
                                        (unsigned)s << CPB_BLOCK_SHIFT);
                    }
            if (shown > 12)
                ImGui::Text("... %d more", shown - 12);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    auto legend = [](ImU32 c, const char *text) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + h, p.y + h), c);
        ImGui::Dummy(ImVec2(h, h));
        ImGui::SameLine();
        ImGui::TextUnformatted(text);
    };
    legend(c_remapped, "live map, remapped");
    ImGui::SameLine();
    legend(c_active, "live map, identity");
    ImGui::SameLine();
    legend(c_pinned, "pinned");
    legend(c_other, "other tasks only");
    ImGui::SameLine();
    legend(c_unused, "unused");

    int b = win->sel_block;
    ImGui::Separator();
    ImGui::Text("Block %d ($%05X), read-only", b, (unsigned)b << CPB_BLOCK_SHIFT);
    const uint8_t *data = cpb_dbg_block((uint8_t)b);
    ImGui::BeginChild("##cpb_hex", ImVec2(0, 220), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(CPB_BLOCK_SIZE / 16);
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
            const uint8_t *r = data + row * 16;
            char hex[16 * 3 + 1], asc[17];
            for (int i = 0; i < 16; i++)
            {
                std::snprintf(hex + i * 3, 4, "%02X ", r[i]);
                asc[i] = (r[i] >= 0x20 && r[i] < 0x7F) ? (char)r[i] : '.';
            }
            asc[16] = 0;
            ImGui::Text("%03X: %s %s", row * 16, hex, asc);
        }
    ImGui::EndChild();
}

void ui_cpb_draw(ui_cpb_t *win)
{
    CHIPS_ASSERT(win && win->valid && win->title);
    if (!win->open)
        return;
    ImGui::SetNextWindowPos(ImVec2(win->init_x, win->init_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(win->init_w, win->init_h), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(win->title, &win->open))
    {
        cpb_dbg_t st;
        cpb_dbg_get(&st);
        /* The emulator runs whole cycles, so the pins are the data phase of the
         * last one: PHI2 high while the machine runs. */
        uint64_t cpu_pins = cpu_dbg_pins();
        uint16_t addr = (uint16_t)(cpu_pins & 0xFFFF);
        uint64_t p = cpu_pins & 0xFFFFFFull;
        if (cpu_pins & W65C02_RW)
            p |= UI_CPB_PIN_RWB;
        if (sys_running())
            p |= UI_CPB_PIN_PHI2;
        if (addr < 0xFF00)
            p |= UI_CPB_PIN_CE2;
        p |= (uint64_t)st.active_map[addr >> CPB_BLOCK_SHIFT] * UI_CPB_PIN_BA11;

        ImGui::BeginChild("##cpb_pins", ImVec2(176, 0), ImGuiChildFlags_Borders);
        ui_chip_draw(&win->chip, p);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##cpb_state", ImVec2(0, 0), ImGuiChildFlags_Borders);

        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
            _ui_cpb_status(&st, cpu_pins);

        if (ImGui::CollapsingHeader("Live map", ImGuiTreeNodeFlags_DefaultOpen))
            _ui_cpb_map_table(win, "##cpb_live", st.active_map);

        if (ImGui::CollapsingHeader("Task maps"))
        {
            char preview[32];
            std::snprintf(preview, sizeof preview, "Task %d", win->sel_task);
            if (ImGui::BeginCombo("##cpb_task", preview))
            {
                for (int t = 0; t < CPB_TASKS; t++)
                {
                    char item[40];
                    bool banked = !_ui_cpb_map_is_identity(st.task_map[t]);
                    std::snprintf(item, sizeof item, "Task %2d%s%s%s", t,
                                  t == st.active_task ? " active" : "",
                                  t == st.edit_task ? " edit" : "", banked ? " banked" : "");
                    if (banked)
                        ImGui::PushStyleColor(ImGuiCol_Text, UI_CPB_REMAPPED);
                    if (ImGui::Selectable(item, win->sel_task == t))
                        win->sel_task = t;
                    if (banked)
                        ImGui::PopStyleColor();
                }
                ImGui::EndCombo();
            }
            _ui_cpb_map_table(win, "##cpb_task_map", st.task_map[win->sel_task]);
        }

        if (ImGui::CollapsingHeader("Blocks", ImGuiTreeNodeFlags_DefaultOpen))
            _ui_cpb_blocks(win, &st);

        ImGui::EndChild();
    }
    ImGui::End();
}

void ui_cpb_save_settings(ui_cpb_t *win, ui_settings_t *settings)
{
    CHIPS_ASSERT(win && settings);
    ui_settings_add(settings, win->title, win->open);
}

void ui_cpb_load_settings(ui_cpb_t *win, const ui_settings_t *settings)
{
    CHIPS_ASSERT(win && settings);
    win->open = ui_settings_isopen(settings, win->title);
}
#endif /* CHIPS_UI_IMPL */
