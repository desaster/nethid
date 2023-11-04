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

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/cyw43_arch.h>

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "board.h"
#include "config.h"

uint16_t blink_state = BLINK_NOT_MOUNTED_WIFI_DOWN;

// application can just update these global variables, and we'll take care of
// updating the blink state
bool wifi_up = false;
bool usb_mounted = false;
bool usb_suspended = false;
bool capslock_on = false;

void update_blink_state(void)
{
    static uint16_t prev_blink_state = 0;

    if (usb_suspended) {
        blink_state = BLINK_SUSPENDED;
    } else if (wifi_up && usb_mounted) {
        blink_state = BLINK_MOUNTED_WIFI_UP;
    } else if (wifi_up && !usb_mounted) {
        blink_state = BLINK_NOT_MOUNTED_WIFI_UP;
    } else if (!wifi_up && usb_mounted) {
        blink_state = BLINK_MOUNTED_WIFI_DOWN;
    } else {
        blink_state = BLINK_NOT_MOUNTED_WIFI_DOWN;
    }

    prev_blink_state = blink_state;
}