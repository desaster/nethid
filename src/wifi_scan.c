/*
 * WiFi network scanning module for NetHID
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "wifi_scan.h"

// Temporary buffer for collecting results during scan (before dedup/sort)
#define TEMP_BUFFER_SIZE (WIFI_SCAN_MAX_NETWORKS * 2)

static wifi_scan_state_t scan_state = {0};
static wifi_network_t temp_results[TEMP_BUFFER_SIZE];
static uint8_t temp_count = 0;
static bool scan_in_progress = false;

// Callback invoked by cyw43 driver for each scan result
static int scan_result_callback(void *env, const cyw43_ev_scan_result_t *result)
{
    (void)env;

    if (!result) {
        return 0;
    }

    if (temp_count >= TEMP_BUFFER_SIZE) {
        return 0;
    }

    // Extract SSID (not null-terminated in scan result)
    size_t ssid_len = result->ssid_len;
    if (ssid_len > 32) {
        ssid_len = 32;
    }

    // Skip empty SSIDs (hidden networks)
    if (ssid_len == 0) {
        return 0;
    }

    char ssid[33];
    memcpy(ssid, result->ssid, ssid_len);
    ssid[ssid_len] = '\0';

    // Skip if SSID is empty after copy
    if (ssid[0] == '\0') {
        return 0;
    }

    // Check for duplicate - keep stronger signal
    for (int i = 0; i < temp_count; i++) {
        if (strcmp(temp_results[i].ssid, ssid) == 0) {
            if (result->rssi > temp_results[i].rssi) {
                temp_results[i].rssi = result->rssi;
                temp_results[i].channel = result->channel;
                temp_results[i].auth_mode = result->auth_mode;
            }
            return 0;
        }
    }

    // Add new network
    strncpy(temp_results[temp_count].ssid, ssid, 32);
    temp_results[temp_count].ssid[32] = '\0';
    temp_results[temp_count].rssi = result->rssi;
    temp_results[temp_count].auth_mode = result->auth_mode;
    temp_results[temp_count].channel = result->channel;
    temp_count++;

    return 0;
}

// Sort temp results by RSSI (strongest first) and copy to final results
static void finalize_scan_results(void)
{
    // Simple bubble sort (small array)
    for (int i = 0; i < temp_count - 1; i++) {
        for (int j = 0; j < temp_count - i - 1; j++) {
            if (temp_results[j].rssi < temp_results[j + 1].rssi) {
                wifi_network_t tmp = temp_results[j];
                temp_results[j] = temp_results[j + 1];
                temp_results[j + 1] = tmp;
            }
        }
    }

    // Copy top N to final results
    scan_state.count = (temp_count > WIFI_SCAN_MAX_NETWORKS) ?
                       WIFI_SCAN_MAX_NETWORKS : temp_count;
    memcpy(scan_state.networks, temp_results,
           scan_state.count * sizeof(wifi_network_t));

    scan_state.scanning = false;
    scan_in_progress = false;

    printf("[wifi_scan] Scan complete, found %d networks\n", scan_state.count);
}

void wifi_scan_init(void)
{
    memset(&scan_state, 0, sizeof(scan_state));
    temp_count = 0;
    scan_in_progress = false;
}

int wifi_scan_start(void)
{
    // Don't start if already scanning
    if (scan_in_progress || cyw43_wifi_scan_active(&cyw43_state)) {
        printf("[wifi_scan] Scan already in progress\n");
        return -1;
    }

    // Clear temp buffer for new scan
    temp_count = 0;
    scan_in_progress = true;
    scan_state.scanning = true;

    cyw43_wifi_scan_options_t scan_options = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result_callback);

    if (err != 0) {
        printf("[wifi_scan] Failed to start scan: %d\n", err);
        scan_in_progress = false;
        scan_state.scanning = false;
        return err;
    }

    printf("[wifi_scan] Scan started\n");
    return 0;
}

bool wifi_scan_is_active(void)
{
    return scan_in_progress || cyw43_wifi_scan_active(&cyw43_state);
}

const wifi_scan_state_t *wifi_scan_get_results(void)
{
    return &scan_state;
}

void wifi_scan_poll(void)
{
    // Check if scan just completed
    if (scan_in_progress && !cyw43_wifi_scan_active(&cyw43_state)) {
        finalize_scan_results();
    }
}
