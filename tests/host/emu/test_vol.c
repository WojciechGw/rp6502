/*
 * Copyright (c) 2026 WojciechGw
 *
 * for Rumbledethumps' Picocomputer 6502
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The drive table behind --drive: names, which drive holds a host path, and
 * the host path a guest path on a drive comes to.
 */

#include "osal/vol.h"
#include "utest.h"
#include <stdlib.h>
#include <string.h>

UTEST_MAIN();

#ifdef _WIN32
#define ROOT "C:\\rp\\usb"
#define S "\\"
#else
#define ROOT "/rp/usb"
#define S "/"
#endif

static vol_t *fresh(void)
{
    vol_clear();
    const char *rest;
    if (vol_add("MSC0", ROOT))
        return NULL;
    return vol_named("MSC0:", &rest);
}

static bool host_is(const vol_t *v, const char *cwd, const char *rest, const char *want)
{
    api_errno err = API_EINVAL;
    char *got = vol_host_path(v, cwd, rest, &err);
    bool same = got && !strcmp(got, want);
    free(got);
    return same;
}

UTEST(vol, names_follow_the_rules)
{
    vol_clear();
    ASSERT_TRUE(vol_add("C", ROOT) != NULL);
    ASSERT_TRUE(vol_add("1AB", ROOT) != NULL);
    ASSERT_TRUE(vol_add("MS-C", ROOT) != NULL);
    ASSERT_TRUE(vol_add("TOOLONGNAME", ROOT) != NULL);
    ASSERT_TRUE(vol_add("fs", ROOT) != NULL);
    ASSERT_TRUE(vol_add("Save", ROOT) != NULL);
    ASSERT_TRUE(vol_add("ROM", ROOT) != NULL);
    ASSERT_TRUE(vol_add("msc0", ROOT) == NULL);
    ASSERT_TRUE(vol_add("MSC0", ROOT) != NULL);
    ASSERT_EQ(vol_count(), 1);
}

UTEST(vol, ten_drives_at_most)
{
    vol_clear();
    char name[] = "USB0";
    for (int i = 0; i < VOL_MAX; i++)
    {
        name[3] = (char)('0' + i);
        ASSERT_TRUE(vol_add(name, ROOT) == NULL);
    }
    ASSERT_TRUE(vol_add("MORE", ROOT) != NULL);
}

UTEST(vol, the_first_drive_is_first_and_none_is_current_until_set)
{
    vol_clear();
    ASSERT_TRUE(vol_first() == NULL);
    ASSERT_TRUE(vol_add("MSC0", ROOT) == NULL);
    ASSERT_TRUE(vol_add("MSC1", ROOT S "b") == NULL);
    ASSERT_STREQ(vol_name(vol_first()), "MSC0");
    ASSERT_TRUE(vol_current() == NULL);
    vol_set_current(vol_first());
    ASSERT_TRUE(vol_current() == vol_first());
    vol_clear();
    ASSERT_TRUE(vol_current() == NULL);
}

UTEST(vol, a_name_is_matched_without_case)
{
    vol_t *v = fresh();
    ASSERT_TRUE(v != NULL);
    ASSERT_STREQ(vol_name(v), "MSC0");
    const char *rest;
    ASSERT_TRUE(vol_named("msc0:/x", &rest) == v);
    ASSERT_STREQ(rest, "/x");
    ASSERT_TRUE(vol_named("MSC0x:", &rest) == NULL);
    ASSERT_TRUE(vol_named("MSC:", &rest) == NULL);
    ASSERT_TRUE(vol_named("MSC0", &rest) == NULL);
}

UTEST(vol, a_number_names_an_msc_drive)
{
    vol_clear();
    ASSERT_TRUE(vol_add("USB", ROOT) == NULL);
    ASSERT_TRUE(vol_add("MSC3", ROOT S "three") == NULL);
    const char *rest;
    vol_t *v = vol_named("3:/x", &rest);
    ASSERT_TRUE(v != NULL);
    ASSERT_STREQ(vol_name(v), "MSC3");
    ASSERT_STREQ(rest, "/x");
    ASSERT_TRUE(vol_named("3:", &rest) == v);
    ASSERT_STREQ(rest, "");
    ASSERT_TRUE(vol_named("0:", &rest) == NULL);
    ASSERT_TRUE(vol_named("33:", &rest) == NULL);
}

UTEST(vol, a_folder_holds_what_is_below_it)
{
    vol_t *v = fresh();
    const char *rest;
    ASSERT_TRUE(vol_holding(ROOT, &rest) == v);
    ASSERT_STREQ(rest, "");
    ASSERT_TRUE(vol_holding(ROOT S, &rest) == v);
    ASSERT_STREQ(rest, "");
    ASSERT_TRUE(vol_holding(ROOT S "a" S "b", &rest) == v);
    ASSERT_STREQ(rest, "a" S "b");
    ASSERT_TRUE(vol_holding(ROOT "x", &rest) == NULL);
    ASSERT_TRUE(vol_holding(S "rp", &rest) == NULL);
#ifdef _WIN32
    ASSERT_TRUE(vol_holding("c:/RP/usb/a", &rest) == v);
    ASSERT_STREQ(rest, "a");
#endif
}

UTEST(vol, the_deeper_drive_holds_its_own_folder)
{
    vol_clear();
    ASSERT_TRUE(vol_add("MSC0", ROOT) == NULL);
    ASSERT_TRUE(vol_add("MSC1", ROOT S "inner") == NULL);
    const char *rest;
    vol_t *inner = vol_holding(ROOT S "inner" S "f", &rest);
    ASSERT_TRUE(inner != NULL);
    ASSERT_STREQ(vol_name(inner), "MSC1");
    ASSERT_STREQ(rest, "f");
}

UTEST(vol, a_rooted_path_starts_at_the_root)
{
    vol_t *v = fresh();
    ASSERT_TRUE(host_is(v, NULL, "/a/b", ROOT S "a" S "b"));
    ASSERT_TRUE(host_is(v, NULL, "/", ROOT));
    ASSERT_TRUE(host_is(v, ROOT S "x", "/a", ROOT S "a"));
}

UTEST(vol, a_relative_path_starts_at_the_working_directory)
{
    vol_t *v = fresh();
    ASSERT_TRUE(host_is(v, ROOT S "a", "b", ROOT S "a" S "b"));
    ASSERT_TRUE(host_is(v, ROOT S "a", "", ROOT S "a"));
    ASSERT_TRUE(host_is(v, NULL, "b", ROOT S "b"));
    vol_set_cwd(v, ROOT S "kept");
    ASSERT_TRUE(host_is(v, NULL, "b", ROOT S "kept" S "b"));
}

UTEST(vol, dots_are_resolved_inside_the_drive)
{
    vol_t *v = fresh();
    ASSERT_TRUE(host_is(v, NULL, "/./a//b/", ROOT S "a" S "b"));
    ASSERT_TRUE(host_is(v, NULL, "/a/../b", ROOT S "b"));
    ASSERT_TRUE(host_is(v, ROOT S "a", "..", ROOT));
}

UTEST(vol, dots_cannot_climb_out_of_the_drive)
{
    vol_t *v = fresh();
    api_errno err = API_EINVAL;
    ASSERT_TRUE(vol_host_path(v, NULL, "/..", &err) == NULL);
    ASSERT_EQ(err, API_ENOENT);
    ASSERT_TRUE(vol_host_path(v, ROOT S "a", "../..", &err) == NULL);
    ASSERT_TRUE(vol_host_path(v, NULL, "/a/../../b", &err) == NULL);
}

UTEST(vol, the_guest_sees_the_drive_name)
{
    fresh();
    bool held;
    char *g = vol_guest_path(ROOT S "a" S "b", &held);
    ASSERT_TRUE(held);
    ASSERT_STREQ(g, "MSC0:/a/b");
    free(g);
    g = vol_guest_path(ROOT, &held);
    ASSERT_TRUE(held);
    ASSERT_STREQ(g, "MSC0:/");
    free(g);
    g = vol_guest_path(S "elsewhere", &held);
    ASSERT_FALSE(held);
    ASSERT_TRUE(g == NULL);
}
