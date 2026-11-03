/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Host folders that a program reaches as drives with names of their own, such
 * as the Pico's "MSC0:". Host paths here are UTF-8 and absolute; guest paths
 * are in the 6502's code page.
 */

#ifndef _OSAL_VOL_H_
#define _OSAL_VOL_H_

#include "core/api/api.h"
#include <stdbool.h>

#define VOL_MAX 10
#define VOL_NAME_MAX 8

typedef struct vol vol_t;

/* NULL on success, else why name or root cannot be a drive. */
const char *vol_add(const char *name, const char *root);
void vol_clear(void);
int vol_count(void);
vol_t *vol_first(void);

/* The drive the program is on, or NULL for the host's. Being on a drive does
 * not move the host's working directory, so host paths given to the emulator
 * keep meaning what they meant at its start. */
vol_t *vol_current(void);
void vol_set_current(vol_t *v);

/* The drive a path starts with, by name and colon, or by number for MSC0 to
 * MSC9 as the Pico takes them: "0:" is "MSC0:". rest is what follows. */
vol_t *vol_named(const char *path, const char **rest);

/* The drive whose folder holds host; rest is the part below its root, "" at
 * the root itself. */
vol_t *vol_holding(const char *host, const char **rest);

const char *vol_name(const vol_t *v);
const char *vol_cwd(const vol_t *v);
void vol_set_cwd(vol_t *v, const char *host);

/* The host path of rest on v, which starts at the root when rest starts with
 * a separator and at cwd otherwise; cwd is a host path held by v, or NULL for
 * the drive's own working directory. A ".." above the root is API_ENOENT, as
 * FatFs has it. */
char *vol_host_path(const vol_t *v, const char *cwd, const char *rest, api_errno *err);

/* The guest's "NAME:/rest" for host. *held says whether a drive holds host;
 * the answer is NULL when none does, or when a character has no byte in the
 * code page. */
char *vol_guest_path(const char *host, bool *held);

#endif /* _OSAL_VOL_H_ */
