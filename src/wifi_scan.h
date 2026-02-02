/*
 * WiFi network scanning module for NetHID
 *
 * Provides async WiFi scanning with cached results for the web UI.
 */

#ifndef __WIFI_SCAN_H
#define __WIFI_SCAN_H

#include <stdbool.h>
#include <stdint.h>

// Maximum networks to store (constrained by HTTP response buffer)
#define WIFI_SCAN_MAX_NETWORKS 8

// Network info structure
typedef struct {
    char ssid[33];      // 32 chars + null terminator
    int16_t rssi;       // Signal strength in dBm
    uint8_t auth_mode;  // CYW43_AUTH_* values
    uint8_t channel;
} wifi_network_t;

// Scan state structure
typedef struct {
    wifi_network_t networks[WIFI_SCAN_MAX_NETWORKS];
    uint8_t count;
    bool scanning;
} wifi_scan_state_t;

// Initialize the scan module
void wifi_scan_init(void);

// Start a new scan (non-blocking)
// Returns 0 on success, negative on error
int wifi_scan_start(void);

// Check if scan is currently active
bool wifi_scan_is_active(void);

// Get scan results (read-only pointer)
const wifi_scan_state_t *wifi_scan_get_results(void);

// Poll for scan completion - call from main loop
void wifi_scan_poll(void);

#endif
