/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "osal/vol.h"
#include "core/str/oem.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef _WIN32
#define VOL_SEP '\\'
#define vol_is_sep(c) ((c) == '/' || (c) == '\\')
#else
#define VOL_SEP '/'
#define vol_is_sep(c) ((c) == '/')
#endif

struct vol
{
    char name[VOL_NAME_MAX + 1];
    char *root;
    char *cwd;
};

static struct vol vols[VOL_MAX];
static int vol_n;
static vol_t *vol_cur;

/* Windows matches names without case and takes either slash. */
static bool vol_same(char a, char b)
{
#ifdef _WIN32
    if (vol_is_sep(a) && vol_is_sep(b))
        return true;
    return tolower((unsigned char)a) == tolower((unsigned char)b);
#else
    return a == b;
#endif
}

/* Trailing separators come off, except the one that is the whole of a
 * filesystem root: "/" or "C:\". */
static size_t vol_root_len(const char *root)
{
    size_t n = strlen(root);
    while (n > 1 && vol_is_sep(root[n - 1]) && !(n == 3 && root[1] == ':'))
        n--;
    return n;
}

static bool vol_root_ends_sep(const char *root)
{
    size_t n = strlen(root);
    return n && vol_is_sep(root[n - 1]);
}

const char *vol_add(const char *name, const char *root)
{
    size_t n = strlen(name);
    if (n < 2 || n > VOL_NAME_MAX || !isalpha((unsigned char)name[0]))
        return "a drive name is 2 to 8 letters and digits, starting with a letter";
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)name[i]))
            return "a drive name is 2 to 8 letters and digits, starting with a letter";
    if (!strcasecmp(name, "FS") || !strcasecmp(name, "SAVE") || !strcasecmp(name, "ROM"))
        return "that name belongs to the emulator";
    for (int i = 0; i < vol_n; i++)
        if (!strcasecmp(vols[i].name, name))
            return "that drive is already mapped";
    if (vol_n == VOL_MAX)
        return "too many drives";
    struct vol *v = &vols[vol_n];
    size_t rl = vol_root_len(root);
    v->root = malloc(rl + 1);
    v->cwd = malloc(rl + 1);
    if (!v->root || !v->cwd)
    {
        free(v->root), free(v->cwd);
        v->root = v->cwd = NULL;
        return "out of memory";
    }
    memcpy(v->root, root, rl);
    v->root[rl] = 0;
    strcpy(v->cwd, v->root);
    for (size_t i = 0; i <= n; i++)
        v->name[i] = (char)toupper((unsigned char)name[i]);
    vol_n++;
    return NULL;
}

void vol_clear(void)
{
    for (int i = 0; i < vol_n; i++)
    {
        free(vols[i].root), free(vols[i].cwd);
        vols[i].root = vols[i].cwd = NULL;
    }
    vol_n = 0;
    vol_cur = NULL;
}

int vol_count(void)
{
    return vol_n;
}

vol_t *vol_first(void)
{
    return vol_n ? &vols[0] : NULL;
}

vol_t *vol_current(void)
{
    return vol_cur;
}

void vol_set_current(vol_t *v)
{
    vol_cur = v;
}

/* FatFs on the Pico also takes a volume by its number, "0:" for "MSC0:". */
vol_t *vol_named(const char *path, const char **rest)
{
    if (path[0] >= '0' && path[0] <= '9' && path[1] == ':')
    {
        for (int i = 0; i < vol_n; i++)
            if (!strncmp(vols[i].name, "MSC", 3) && vols[i].name[3] == path[0] &&
                !vols[i].name[4])
            {
                *rest = path + 2;
                return &vols[i];
            }
        return NULL;
    }
    for (int i = 0; i < vol_n; i++)
    {
        size_t n = strlen(vols[i].name);
        if (!strncasecmp(path, vols[i].name, n) && path[n] == ':')
        {
            *rest = path + n + 1;
            return &vols[i];
        }
    }
    return NULL;
}

/* The longest root wins, so a drive mapped inside another's folder holds what
 * is below its own root. */
vol_t *vol_holding(const char *host, const char **rest)
{
    vol_t *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < vol_n; i++)
    {
        const char *root = vols[i].root;
        size_t rl = strlen(root);
        if (rl <= best_len && best)
            continue;
        size_t k = 0;
        while (k < rl && host[k] && vol_same(host[k], root[k]))
            k++;
        if (k < rl)
            continue;
        if (!vol_root_ends_sep(root) && host[rl] && !vol_is_sep(host[rl]))
            continue;
        best = &vols[i];
        best_len = rl;
    }
    if (best)
    {
        const char *r = host + best_len;
        while (vol_is_sep(*r))
            r++;
        *rest = r;
    }
    return best;
}

const char *vol_name(const vol_t *v)
{
    return v->name;
}

const char *vol_cwd(const vol_t *v)
{
    return v->cwd;
}

void vol_set_cwd(vol_t *v, const char *host)
{
    char *copy = strdup(host);
    if (!copy)
        return;
    free(v->cwd);
    v->cwd = copy;
}

/* The path is built from the drive's root, so ".." is resolved here rather
 * than by the host, which would follow it above the root. */
char *vol_host_path(const vol_t *v, const char *cwd, const char *rest, api_errno *err)
{
    const char *root = v->root;
    size_t rl = strlen(root);
    const char *from = "";
    if (!vol_is_sep(rest[0]))
    {
        const char *c = cwd ? cwd : v->cwd;
        if (strlen(c) >= rl)
            from = c + rl;
    }
    char *out = malloc(rl + strlen(from) + strlen(rest) + 3);
    if (!out)
    {
        *err = API_ENOMEM;
        return NULL;
    }
    memcpy(out, root, rl);
    size_t len = rl;
    bool root_sep = vol_root_ends_sep(root);
    const char *parts[2] = {from, rest};
    for (int i = 0; i < 2; i++)
    {
        const char *p = parts[i];
        while (*p)
        {
            while (vol_is_sep(*p))
                p++;
            const char *s = p;
            while (*p && !vol_is_sep(*p))
                p++;
            size_t n = (size_t)(p - s);
            if (!n || (n == 1 && s[0] == '.'))
                continue;
            if (n == 2 && s[0] == '.' && s[1] == '.')
            {
                if (len == rl)
                {
                    free(out);
                    *err = API_ENOENT;
                    return NULL;
                }
                while (len > rl && !vol_is_sep(out[len - 1]))
                    len--;
                if (len > rl)
                    len--;
                continue;
            }
            if (len > rl || !root_sep)
                out[len++] = VOL_SEP;
            memcpy(out + len, s, n);
            len += n;
        }
    }
    out[len] = 0;
    return out;
}

char *vol_guest_path(const char *host, bool *held)
{
    const char *rest;
    vol_t *v = vol_holding(host, &rest);
    *held = v != NULL;
#ifndef _WIN32
    /* A '\' in a host name would read as a separator on the way back. */
    if (v && strchr(rest, '\\'))
        return NULL;
#endif
    if (!v || !oem_maps_utf8(rest))
        return NULL;
    size_t nl = strlen(v->name);
    /* oem_from_utf8 writes one byte per UTF-8 sequence, so the UTF-8 length
     * bounds the answer. */
    size_t sz = nl + 2 + strlen(rest) + 1;
    char *g = malloc(sz);
    if (!g)
        return NULL;
    memcpy(g, v->name, nl);
    g[nl] = ':';
    g[nl + 1] = '/';
    oem_from_utf8(rest, g + nl + 2, sz - nl - 2);
    for (char *p = g + nl + 2; *p; p++)
        if (*p == '\\')
            *p = '/';
    return g;
}
