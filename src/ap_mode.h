/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Upi Tamminen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef __AP_MODE_H
#define __AP_MODE_H

#include <stdbool.h>
#include <stdint.h>

// AP mode configuration
#define AP_SSID_PREFIX "NetHID-"
#define AP_PASSWORD "nethid123"

// BOOTSEL hold time in milliseconds
#define BOOTSEL_HOLD_TIME_MS 5000

// WiFi credential limits (matching IEEE 802.11 standards)
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

// Device settings limits
#define HOSTNAME_MAX_LEN 32

// Check if "force AP" flag is set in flash
bool ap_mode_check_force_flag(void);

// Clear the "force AP" flag in flash
void ap_mode_clear_force_flag(void);

// Set the "force AP" flag in flash (called before reboot)
void ap_mode_set_force_flag(void);

// Check if valid WiFi credentials are stored in flash
bool wifi_credentials_exist(void);

// Get stored WiFi credentials
// Returns true if valid credentials found, false otherwise
// ssid buffer must be at least WIFI_SSID_MAX_LEN+1 bytes
// password buffer must be at least WIFI_PASSWORD_MAX_LEN+1 bytes
bool wifi_credentials_get(char *ssid, char *password);

// Store WiFi credentials to flash
// Returns true on success, false on failure
bool wifi_credentials_set(const char *ssid, const char *password);

// Get only the SSID (for status display, without exposing password)
bool wifi_credentials_get_ssid(char *ssid);

// Device settings API
// Get hostname - returns configured hostname or MAC-based default
// hostname buffer must be at least HOSTNAME_MAX_LEN+1 bytes
// Returns true if configured hostname was found, false if using default
bool settings_get_hostname(char *hostname);

// Set hostname - validates and stores to flash
// Returns true on success, false on invalid hostname
bool settings_set_hostname(const char *hostname);

// Check if hostname is the auto-generated default
bool settings_hostname_is_default(void);

// Initialize and start AP mode with DHCP server
int ap_mode_start(void);

// Poll BOOTSEL button - call from main loop
void bootsel_task(void);

#endif
