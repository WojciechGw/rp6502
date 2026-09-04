/*
 * Copyright (c) 2026 WojciechGw
 * 
 * for Rumbledethumps' Picocomputer 6502
 * 
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CORE_AUD_PCM_H_
#define _CORE_AUD_PCM_H_

/* PCM audio playback - stereo ring buffer in XRAM, resampled from a
 * program's chosen rate up to AUD_NATIVE_RATE.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* One stereo sample at AUD_NATIVE_RATE, and the engine advanced by one. */
void pcm_sample(int16_t *left, int16_t *right);

bool pcm_xreg(uint16_t word);

#endif /* _CORE_AUD_PCM_H_ */
