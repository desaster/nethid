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

// Check if "force AP" flag is set in flash
bool ap_mode_check_force_flag(void);

// Clear the "force AP" flag in flash
void ap_mode_clear_force_flag(void);

// Set the "force AP" flag in flash (called before reboot)
void ap_mode_set_force_flag(void);

// Initialize and start AP mode with DHCP server
int ap_mode_start(void);

// Poll BOOTSEL button - call from main loop
void bootsel_task(void);

#endif
