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

#ifndef __BOARD_H
#define __BOARD_H

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/cyw43_arch.h>

enum {
    // fully lit, capslock is on
    BLINK_CAPSLOCK =                0b1111111111111111,

    // slow blink
    BLINK_SUSPENDED =               0b0000111100001111,

    // normal blink (all ok)
    BLINK_MOUNTED_WIFI_UP =         0b0011001100110011,

    // two quick blinks, not mounted
    BLINK_NOT_MOUNTED_WIFI_UP =     0b1010000000000000,

    // three quick blinks, wifi down
    BLINK_MOUNTED_WIFI_DOWN =       0b1010100000000000,

    // four quick blinks, not mounted & wifi down
    BLINK_NOT_MOUNTED_WIFI_DOWN =   0b1010101000000000,
};

#define BLINK_STATE_MS 500

extern uint16_t blink_state;

extern bool wifi_up;
extern bool usb_mounted;
extern bool usb_suspended;
extern bool capslock_on;

void update_blink_state(void);

#endif