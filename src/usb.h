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

#ifndef __USB_H
#define __USB_H

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/cyw43_arch.h>

#include "tusb.h"
#include "usb_descriptors.h"

void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_suspend_cb(bool remote_wakeup_en);
void tud_resume_cb(void);
void press_key(uint16_t key);
void depress_key(uint16_t key);
void move_mouse(uint8_t buttons, int8_t x, int8_t y, int8_t vertical, int8_t horizontal);
void press_consumer(uint16_t code);
void release_consumer(void);
void press_system(uint16_t code);
void release_system(void);

#endif