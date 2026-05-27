/*
 * Copyright (c) 2026 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RP6502_RIA_W
#include "net/wfi.h"
void wfi_task() {}
int wfi_status_response(char *, size_t, int) { return -1; }
void wfi_mon_scanwifi(const char *) {}
#else

#include "mon/mon.h"
#include "net/cyw.h"
#include "net/wfi.h"
#include "str/str.h"
#include "sys/cfg.h"
#include <pico/cyw43_arch.h>
#include <stdio.h>
#include <string.h>

#if defined(DEBUG_RIA_NET) || defined(DEBUG_RIA_NET_WFI)
#include <stdio.h>
#define DBG(...) printf(__VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

typedef enum
{
    wfi_state_off,
    wfi_state_connect,
    wfi_state_connecting,
    wfi_state_connected,
    wfi_state_connect_failed,
} wfi_state_t;
static wfi_state_t wfi_state;

static int wfi_retry_count;
static absolute_time_t wfi_retry_timer;
static char wfi_ssid[WFI_SSID_SIZE];
static char wfi_pass[WFI_PASS_SIZE];

#define WFI_SCAN_MAX_RESULTS 32
#define WFI_SCAN_TIMEOUT_SECS 15

typedef enum
{
    wfi_scan_state_idle,
    wfi_scan_state_running,
    wfi_scan_state_done,
    wfi_scan_state_cancelled,
} wfi_scan_state_t;

typedef struct
{
    char ssid[WFI_SSID_SIZE];
    uint8_t bssid[6];
    int16_t rssi;
    uint16_t channel;
    uint8_t auth_mode;
} wfi_scan_result_t;

static wfi_scan_state_t wfi_scan_state;
static wfi_scan_result_t wfi_scan_results[WFI_SCAN_MAX_RESULTS];
static size_t wfi_scan_count;
static size_t wfi_scan_seen;
static bool wfi_scan_temp_sta;
static bool wfi_scan_sorted;
static absolute_time_t wfi_scan_timeout_timer;

// Be aggressive 5 times then back off
#define WFI_RETRY_INITIAL_RETRIES 5
#define WFI_RETRY_INITIAL_SECS 2
#define WFI_RETRY_SECS 60

static bool wfi_scan_driver_active(void)
{
    return (wfi_scan_state == wfi_scan_state_running ||
            wfi_scan_state == wfi_scan_state_cancelled) &&
           cyw43_wifi_scan_active(&cyw43_state);
}

static void wfi_scan_poll_done(void)
{
    if ((wfi_scan_state == wfi_scan_state_running ||
         wfi_scan_state == wfi_scan_state_cancelled) &&
        !cyw43_wifi_scan_active(&cyw43_state))
    {
        if (wfi_scan_temp_sta)
        {
            cyw43_arch_disable_sta_mode();
            wfi_scan_temp_sta = false;
        }
        wfi_scan_state = wfi_scan_state == wfi_scan_state_running
                             ? wfi_scan_state_done
                             : wfi_scan_state_idle;
    }
}

static bool wfi_scan_blocks_wifi(void)
{
    return wfi_scan_state == wfi_scan_state_running ||
           wfi_scan_state == wfi_scan_state_cancelled;
}

static void wfi_scan_copy_ssid(char *dst, const cyw43_ev_scan_result_t *src)
{
    size_t len = src->ssid_len;
    if (len >= WFI_SSID_SIZE)
        len = WFI_SSID_SIZE - 1;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t ch = src->ssid[i];
        dst[i] = ch >= 32 && ch <= 126 ? (char)ch : '?';
    }
    dst[len] = 0;
}

static void wfi_scan_store_result(const cyw43_ev_scan_result_t *result)
{
    wfi_scan_result_t item;
    wfi_scan_copy_ssid(item.ssid, result);
    memcpy(item.bssid, result->bssid, sizeof(item.bssid));
    item.rssi = result->rssi;
    item.channel = result->channel;
    item.auth_mode = result->auth_mode;

    for (size_t i = 0; i < wfi_scan_count; i++)
    {
        if (!memcmp(wfi_scan_results[i].bssid, item.bssid, sizeof(item.bssid)))
        {
            wfi_scan_results[i] = item;
            return;
        }
    }

    if (wfi_scan_count < WFI_SCAN_MAX_RESULTS)
    {
        wfi_scan_results[wfi_scan_count++] = item;
        return;
    }

    size_t weakest = 0;
    for (size_t i = 1; i < wfi_scan_count; i++)
        if (wfi_scan_results[i].rssi < wfi_scan_results[weakest].rssi)
            weakest = i;
    if (item.rssi > wfi_scan_results[weakest].rssi)
        wfi_scan_results[weakest] = item;
}

static int wfi_scan_callback(void *env, const cyw43_ev_scan_result_t *result)
{
    (void)env;
    if (wfi_scan_state == wfi_scan_state_running)
    {
        wfi_scan_seen++;
        wfi_scan_store_result(result);
    }
    return 0;
}

static void wfi_scan_sort(void)
{
    if (wfi_scan_sorted)
        return;
    for (size_t i = 1; i < wfi_scan_count; i++)
    {
        wfi_scan_result_t item = wfi_scan_results[i];
        size_t j = i;
        while (j > 0 && item.rssi > wfi_scan_results[j - 1].rssi)
        {
            wfi_scan_results[j] = wfi_scan_results[j - 1];
            j--;
        }
        wfi_scan_results[j] = item;
    }
    wfi_scan_sorted = true;
}

static const char *wfi_scan_auth_text(uint8_t auth_mode, char *buf, size_t buf_size)
{
    switch (auth_mode)
    {
    case 0:
        return "Open";
    case 1:
        return "WEP";
    case 2:
    case 3:
        return "WPA";
    case 4:
    case 5:
        return "WPA2";
    case 6:
    case 7:
        return "WPA/WPA2";
    default:
        snprintf(buf, buf_size, "0x%02X", (unsigned)auth_mode);
        return buf;
    }
}

static bool wfi_scan_start(void)
{
    if (wfi_scan_state != wfi_scan_state_idle || cyw43_wifi_scan_active(&cyw43_state))
        return false;

    memset(wfi_scan_results, 0, sizeof(wfi_scan_results));
    wfi_scan_count = 0;
    wfi_scan_seen = 0;
    wfi_scan_sorted = false;
    wfi_scan_temp_sta = !(cyw43_state.itf_state & (1 << CYW43_ITF_STA));
    if (wfi_scan_temp_sta)
        cyw43_arch_enable_sta_mode();

    cyw43_wifi_scan_options_t opts = {0};
    wfi_scan_state = wfi_scan_state_running;
    wfi_scan_timeout_timer = make_timeout_time_ms(WFI_SCAN_TIMEOUT_SECS * 1000);
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, wfi_scan_callback))
    {
        wfi_scan_state = wfi_scan_state_idle;
        if (wfi_scan_temp_sta)
        {
            cyw43_arch_disable_sta_mode();
            wfi_scan_temp_sta = false;
        }
        return false;
    }
    return true;
}

static int wfi_scan_response(char *buf, size_t buf_size, int state)
{
    if (state < 0)
    {
        if (wfi_scan_state == wfi_scan_state_running)
            wfi_scan_state = wfi_scan_state_cancelled;
        else if (wfi_scan_state == wfi_scan_state_done)
            wfi_scan_state = wfi_scan_state_idle;
        return -1;
    }

    wfi_scan_poll_done();
    if (wfi_scan_state == wfi_scan_state_running)
    {
        if (time_reached(wfi_scan_timeout_timer))
        {
            wfi_scan_state = wfi_scan_state_cancelled;
            snprintf_utf8(buf, buf_size, STR_SCANWIFI_TIMEOUT);
            return -1;
        }
        return 0;
    }
    if (wfi_scan_state != wfi_scan_state_done)
        return -1;

    wfi_scan_sort();
    if (state == 0)
    {
        if (!wfi_scan_count)
        {
            wfi_scan_state = wfi_scan_state_idle;
            snprintf_utf8(buf, buf_size, STR_SCANWIFI_NO_NETWORKS);
            return -1;
        }
        snprintf_utf8(buf, buf_size, STR_SCANWIFI_HEADER);
        return 1;
    }

    size_t idx = (size_t)state - 1;
    if (idx < wfi_scan_count)
    {
        const wfi_scan_result_t *result = &wfi_scan_results[idx];
        char auth_buf[8];
        snprintf(buf, buf_size, STR_SCANWIFI_ROW,
                 result->bssid[0], result->bssid[1], result->bssid[2],
                 result->bssid[3], result->bssid[4], result->bssid[5],
                 result->rssi, (unsigned)result->channel,
                 wfi_scan_auth_text(result->auth_mode, auth_buf, sizeof(auth_buf)),
                 result->ssid);
        return state + 1;
    }

    wfi_scan_state = wfi_scan_state_idle;
    if (wfi_scan_seen > wfi_scan_count)
        snprintf(buf, buf_size, STR_SCANWIFI_TRUNCATED,
                 (unsigned)wfi_scan_count, (unsigned)wfi_scan_seen);
    return -1;
}

void wfi_shutdown(void)
{
    switch (wfi_state)
    {
    case wfi_state_connected:
    case wfi_state_connecting:
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        __attribute__((fallthrough));
    case wfi_state_connect:
    case wfi_state_connect_failed:
        cyw43_arch_disable_sta_mode();
        wfi_state = wfi_state_off;
        __attribute__((fallthrough));
    case wfi_state_off:
        break;
    }
    wfi_retry_count = 0;
}

static void wfi_retry_connect(void)
{
    int secs = wfi_retry_count < WFI_RETRY_INITIAL_RETRIES
                   ? WFI_RETRY_INITIAL_SECS
                   : WFI_RETRY_SECS;
    wfi_state = wfi_state_connect_failed;
    wfi_retry_timer = make_timeout_time_ms(secs * 1000);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
}

void wfi_task(void)
{
    wfi_scan_poll_done();

    switch (wfi_state)
    {
    case wfi_state_off:
        if (wfi_scan_blocks_wifi() || !cyw_get_rf_enable() || !wfi_ssid[0])
            break;
        cyw43_arch_enable_sta_mode();
        wfi_state = wfi_state_connect;
        break;
    case wfi_state_connect:
        DBG("NET WFI connecting\n");
        // Power management may be buggy, turn it off
        if (cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf))
            wfi_retry_connect();
        else if (cyw43_arch_wifi_connect_async(
                     wfi_ssid, wfi_get_pass(),
                     strlen(wfi_get_pass()) ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN))
            wfi_retry_connect();
        else
            wfi_state = wfi_state_connecting;
        break;
    case wfi_state_connecting:
        switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA))
        {
        case CYW43_LINK_DOWN:
        case CYW43_LINK_JOIN:
        case CYW43_LINK_NOIP:
            break;
        case CYW43_LINK_UP:
            DBG("NET WFI connected\n");
            wfi_retry_count = 0;
            wfi_state = wfi_state_connected;
            break;
        case CYW43_LINK_FAIL:
        case CYW43_LINK_NONET:
        case CYW43_LINK_BADAUTH:
            DBG("NET WFI connect failed\n");
            wfi_retry_connect();
            break;
        }
        break;
    case wfi_state_connect_failed:
        if (!wfi_scan_blocks_wifi() && time_reached(wfi_retry_timer))
        {
            wfi_retry_count++;
            wfi_state = wfi_state_connect;
        }
        break;
    case wfi_state_connected:
        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP)
        {
            DBG("NET WFI connection lost\n");
            wfi_retry_connect();
        }
        break;
    }
}

static const char *wfi_status_message(void)
{
    switch (wfi_state)
    {
    case wfi_state_off:
        if (!cyw_get_rf_enable())
            return STR_RF_OFF;
        else if (!wfi_ssid[0])
            return STR_WFI_NOT_CONFIGURED;
        else
            return STR_WFI_WAITING;
    case wfi_state_connect:
    case wfi_state_connecting:
        switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA))
        {
        case CYW43_LINK_JOIN:
            return STR_WFI_JOINING;
        case CYW43_LINK_NOIP:
            return STR_WFI_GETTING_IP;
        default:
            return STR_WFI_CONNECTING;
        }
    case wfi_state_connected:
        return STR_WFI_CONNECTED;
    case wfi_state_connect_failed:
        switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA))
        {
        case CYW43_LINK_NOIP:
            return STR_WFI_NO_IP_ADDRESS;
        case CYW43_LINK_NONET:
            return STR_WFI_SSID_NOT_FOUND;
        case CYW43_LINK_BADAUTH:
            return STR_WFI_AUTH_FAILED;
        default:
            return STR_WFI_CONNECT_FAILED;
        }
    }
    return STR_INTERNAL_ERROR;
}


void wfi_mon_scanwifi(const char *args)
{
    if (!str_parse_end(args))
    {
        mon_add_response_utf8(STR_ERR_INVALID_ARGUMENT);
        return;
    }
    if (!cyw_get_rf_enable())
    {
        mon_add_response_utf8(STR_RF_OFF);
        mon_add_response_utf8("\n");
        return;
    }
    if (wfi_connecting() || wfi_scan_driver_active() ||
        wfi_scan_state != wfi_scan_state_idle ||
        !wfi_scan_start())
    {
        mon_add_response_utf8(STR_SCANWIFI_BUSY);
        return;
    }
    mon_add_response_utf8(STR_SCANWIFI_SCANNING);
    mon_add_response_fn(wfi_scan_response);
}

int wfi_status_response(char *buf, size_t buf_size, int state)
{
    switch (state)
    {
    case 0:
    {
        snprintf_utf8(buf, buf_size, STR_STATUS_WIFI, wfi_status_message());
    }
    break;
    case 1:
    {
        uint8_t mac[6];
#if RP6502_CREATOR
        mac[0] = 0xBA;
        mac[1] = 0xDC;
        mac[2] = 0x0F;
        mac[3] = 0xFE;
        mac[4] = 0xEB;
        mac[5] = 0xAD;
#else
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
#endif
        snprintf(buf, buf_size, STR_STATUS_MAC,
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    break;
    case 2:
    {
        if (wfi_state == wfi_state_connected)
        {
            struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
            const ip4_addr_t *ip4 = netif_ip4_addr(netif);
            if (!ip4_addr_isany_val(*ip4))
                snprintf(buf, buf_size, STR_STATUS_IPV4, ip4addr_ntoa(ip4));
        }
    }
    break;
    case 3:
    {
        if (wfi_state == wfi_state_connected)
        {
            int32_t rssi;
            if (!cyw43_wifi_get_rssi(&cyw43_state, &rssi))
                snprintf(buf, buf_size, STR_STATUS_RSSI, (long)rssi);
        }
    }
    break;
    default:
        return -1;
    }
    return state + 1;
}

bool wfi_ready(void)
{
    return wfi_state == wfi_state_connected;
}

bool wfi_connecting(void)
{
    return wfi_state == wfi_state_connect ||
           wfi_state == wfi_state_connecting ||
           (wfi_state == wfi_state_connect_failed &&
            wfi_retry_count < WFI_RETRY_INITIAL_RETRIES);
}

void wfi_load_ssid(const char *str)
{
    size_t n = strlen(str);
    if (n < sizeof(wfi_ssid))
    {
        memcpy(wfi_ssid, str, n);
        wfi_ssid[n] = 0;
    }
}

bool wfi_set_ssid(const char *ssid)
{
    size_t len = strlen(ssid);
    if (len < sizeof(wfi_ssid))
    {
        if (strcmp(wfi_ssid, ssid))
        {
            wfi_pass[0] = 0;
            strncpy(wfi_ssid, ssid, sizeof(wfi_ssid));
            wfi_shutdown();
            cfg_save();
        }
        return true;
    }
    return false;
}

const char *wfi_get_ssid(void)
{
    return wfi_ssid;
}

void wfi_load_pass(const char *str)
{
    size_t n = strlen(str);
    if (n < sizeof(wfi_pass))
    {
        memcpy(wfi_pass, str, n);
        wfi_pass[n] = 0;
    }
}

bool wfi_set_pass(const char *pass)
{
    if (strlen(wfi_ssid) && strlen(pass) < sizeof(wfi_pass))
    {
        if (strcmp(wfi_pass, pass))
        {
            strncpy(wfi_pass, pass, sizeof(wfi_pass));
            wfi_shutdown();
            cfg_save();
        }
        return true;
    }
    return false;
}

const char *wfi_get_pass(void)
{
    return wfi_pass;
}

#endif /* RP6502_RIA_W */
