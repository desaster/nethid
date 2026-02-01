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
#include <string.h>
#include <pico/cyw43_arch.h>

#include <pico/util/queue.h>

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "board.h"
#include "config.h"

queue_t fifo_keyboard;
queue_t fifo_mouse;

uint8_t keycodes[6] = { 0, 0, 0, 0, 0, 0 };
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t vertical;
    int8_t horizontal;
} mouse_data;

//
// Device callbacks
//

// Invoked when device is mounted
void tud_mount_cb(void)
{
    // initialize a fifo queue of hid reports
    queue_init(&fifo_keyboard, sizeof(uint8_t[6]), 32);
    queue_init(&fifo_mouse, sizeof(mouse_data), 128);
    usb_mounted = true;
    update_blink_state();
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    queue_free(&fifo_keyboard);
    queue_free(&fifo_mouse);
    usb_mounted = false;
    update_blink_state();
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    usb_suspended = true;
    update_blink_state();
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    usb_suspended = false;
    update_blink_state();
}

//
// Public functions for keypresses
//

void press_key(uint16_t key)
{
    bool keys_changed = false;

    // check that key isn't already pressed
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] == key) {
            return;
        }
    }

    // find a slot for they and set it as pressed
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] == 0) {
            keycodes[i] = key;
            keys_changed = true;
            break;
        }
    }

    if (keys_changed) {
        // printf("Adding press to queue (%d): ", queue_get_level(&report_fifo));
        // for (int i = 0; i < 6; i++) {
        //     printf("%02x ", keycodes[i]);
        // }
        // printf("\r\n");
        if (!queue_try_add(&fifo_keyboard, keycodes)) {
            printf("HID report queue full!\r\n");
        }
    }
}

void depress_key(uint16_t key)
{
    bool keys_changed = false;

    // find the key and set it as released
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] == key) {
            keycodes[i] = 0;
            keys_changed = true;
            break;
        }
    }

    if (keys_changed) {
        // printf("Adding depress to queue (%d): ", queue_get_level(&report_fifo));
        // for (int i = 0; i < 6; i++) {
        //     printf("%02x ", keycodes[i]);
        // }
        // printf("\r\n");

        if (!queue_try_add(&fifo_keyboard, keycodes)) {
            printf("HID report queue full!\r\n");
        }
    }
}

void move_mouse(uint8_t buttons, int8_t x, int8_t y, int8_t vertical, int8_t horizontal)
{
    mouse_data data = {
        .buttons = buttons,
        .x = x,
        .y = y,
        .vertical = vertical,
        .horizontal = horizontal
    };
    if (!queue_try_add(&fifo_mouse, &data)) {
        printf("Mouse report queue full!\r\n");
    }
}

//
// private function for sending updated usb packet
//

static void send_events(int report_id)
{
    uint8_t new_keycodes[6] = { 0, 0, 0, 0, 0, 0 };
    mouse_data new_mouse_data = { 
        .buttons = 0,
        .x = 0,
        .y = 0,
        .vertical = 0,
        .horizontal = 0
    };

    if (report_id == REPORT_ID_KEYBOARD && !queue_is_empty(&fifo_keyboard)) {
        if (queue_try_remove(&fifo_keyboard, &new_keycodes)) {
            // printf("Sending keyboard data: ");
            // for (int i = 0; i < 6; i++) {
            //     printf("%02x ", new_keycodes[i]);
            // }
            // printf("\r\n");
            tud_hid_keyboard_report(
                    REPORT_ID_KEYBOARD,
                    0,  // modifiers
                    new_keycodes);
        }
    } else if (report_id == REPORT_ID_MOUSE && !queue_is_empty(&fifo_mouse)) {
        if (queue_try_remove(&fifo_mouse, &new_mouse_data)) {
            // printf("Sending mouse data: xrel: %d, yrel: %d\r\n",
            //         new_mouse_data.x,
            //         new_mouse_data.y);
            tud_hid_mouse_report(
                    REPORT_ID_MOUSE,
                    new_mouse_data.buttons,
                    new_mouse_data.x,
                    new_mouse_data.y,
                    new_mouse_data.vertical,
                    new_mouse_data.horizontal);
        }
    } else {
        // printf("Nothing more to send for id %d\r\n", report_id);
    }
}

// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
    // Poll every 10ms
    const uint32_t interval_ms = 10;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms) {
        return; // not enough time
    }

    start_ms += interval_ms;

    // skip if hid is not ready yet
    if (!tud_hid_ready()) {
        return;
    }

    if (tud_suspended() &&
            (!queue_is_empty(&fifo_keyboard) ||
             !queue_is_empty(&fifo_mouse))) {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        tud_remote_wakeup();

        return;
    }

    // try sending whichever we have in queue
    if (!queue_is_empty(&fifo_keyboard)) {
        send_events(REPORT_ID_KEYBOARD);
    } else if (!queue_is_empty(&fifo_mouse)) {
        send_events(REPORT_ID_MOUSE);
    }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(
    uint8_t instance,
    uint8_t const* report,
    uint16_t len)
{
    (void) instance;
    (void) len;

    // keep sending...
    send_events(report[0]);
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t* buffer,
    uint16_t reqlen)
{
    // TODO not Implemented
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const* buffer,
    uint16_t bufsize)
{
    (void) instance;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == REPORT_ID_KEYBOARD) {
            // bufsize should be (at least) 1
            if (bufsize < 1) {
                return;
            }

            uint8_t const kbd_leds = buffer[0];

            if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
                capslock_on = true;
                update_blink_state();
            } else {
                capslock_on = false;
                update_blink_state();
            }
        }
    }
}