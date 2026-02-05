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
#include "websocket/websocket.h"

queue_t fifo_keyboard;
queue_t fifo_consumer;
queue_t fifo_system;
queue_t fifo_mouse_btn;  // queued button state transitions
static bool queues_initialized = false;

// Mouse accumulator: movement deltas are summed and drained ±127 per HID report.
// Button state changes are queued separately so rapid press+release aren't merged.
static struct {
    int32_t dx;
    int32_t dy;
    int32_t vertical;
    int32_t horizontal;
    uint8_t buttons;
    uint8_t last_sent_buttons;
} mouse_acc = {0};

static bool mouse_has_pending(void)
{
    return !queue_is_empty(&fifo_mouse_btn) ||
           mouse_acc.dx != 0 || mouse_acc.dy != 0 ||
           mouse_acc.vertical != 0 || mouse_acc.horizontal != 0 ||
           mouse_acc.buttons != mouse_acc.last_sent_buttons;
}

static bool remote_wakeup_enabled = false;

uint8_t keycodes[6] = { 0, 0, 0, 0, 0, 0 };

//
// Device callbacks
//

// Invoked when device is mounted
void tud_mount_cb(void)
{
    printf("USB: Mount callback\r\n");

    // Free old queues first to prevent memory leak if mount fires
    // without a preceding unmount (e.g. during USB bus reset)
    if (queues_initialized) {
        queue_free(&fifo_keyboard);
        queue_free(&fifo_consumer);
        queue_free(&fifo_system);
        queue_free(&fifo_mouse_btn);
    }

    // initialize fifo queues for discrete HID reports
    queue_init(&fifo_keyboard, sizeof(uint8_t[6]), 32);
    queue_init(&fifo_consumer, sizeof(uint16_t), 32);
    queue_init(&fifo_system, sizeof(uint8_t), 32);
    queue_init(&fifo_mouse_btn, sizeof(uint8_t), 8);
    queues_initialized = true;

    memset(&mouse_acc, 0, sizeof(mouse_acc));
    usb_mounted = true;
    usb_suspended = false;
    remote_wakeup_enabled = false;
    update_blink_state();
    websocket_send_status();
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    printf("USB: Unmount callback\r\n");
    queue_free(&fifo_keyboard);
    queue_free(&fifo_consumer);
    queue_free(&fifo_system);
    queue_free(&fifo_mouse_btn);
    queues_initialized = false;
    memset(&mouse_acc, 0, sizeof(mouse_acc));
    usb_mounted = false;
    remote_wakeup_enabled = false;
    update_blink_state();
    websocket_send_status();
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    printf("USB: Suspend (remote_wakeup_en=%d)\r\n", remote_wakeup_en);
    remote_wakeup_enabled = remote_wakeup_en;
    usb_suspended = true;
    update_blink_state();
    websocket_send_status();
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    printf("USB: Resume callback\r\n");
    usb_suspended = false;
    update_blink_state();
    websocket_send_status();
}

//
// Public functions for keypresses
//

void press_key(uint16_t key)
{
    if (!usb_mounted) return;

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
    if (!usb_mounted) return;

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

void move_mouse(uint8_t buttons, int16_t x, int16_t y, int16_t vertical, int16_t horizontal)
{
    if (!usb_mounted) return;

    if (buttons != mouse_acc.buttons) {
        queue_try_add(&fifo_mouse_btn, &buttons);
    }
    mouse_acc.buttons = buttons;
    mouse_acc.dx += x;
    mouse_acc.dy += y;
    mouse_acc.vertical += vertical;
    mouse_acc.horizontal += horizontal;
}

void press_consumer(uint16_t code)
{
    if (!usb_mounted) return;

    if (!queue_try_add(&fifo_consumer, &code)) {
        printf("Consumer report queue full!\r\n");
    }
}

void release_consumer(void)
{
    if (!usb_mounted) return;

    uint16_t code = 0;
    if (!queue_try_add(&fifo_consumer, &code)) {
        printf("Consumer report queue full!\r\n");
    }
}

void press_system(uint16_t code)
{
    if (!usb_mounted) return;

    // Convert HID usage to report value (0x81->1, 0x82->2, 0x83->3)
    uint8_t report_val = (uint8_t)(code - 0x80);
    if (!queue_try_add(&fifo_system, &report_val)) {
        printf("System report queue full!\r\n");
    }
}

void release_system(void)
{
    if (!usb_mounted) return;

    uint8_t report_val = 0;
    if (!queue_try_add(&fifo_system, &report_val)) {
        printf("System report queue full!\r\n");
    }
}

//
// private function for sending updated usb packet
//

static inline int8_t clamp8(int16_t v)
{
    return v > 127 ? 127 : (v < -127 ? -127 : (int8_t)v);
}

static void send_events(int report_id)
{
    uint8_t new_keycodes[6] = { 0, 0, 0, 0, 0, 0 };

    if (report_id == REPORT_ID_KEYBOARD && !queue_is_empty(&fifo_keyboard)) {
        if (queue_try_remove(&fifo_keyboard, &new_keycodes)) {
            tud_hid_keyboard_report(
                    REPORT_ID_KEYBOARD,
                    0,  // modifiers
                    new_keycodes);
        }
    } else if (report_id == REPORT_ID_MOUSE && mouse_has_pending()) {
        int8_t cx = clamp8(mouse_acc.dx);
        int8_t cy = clamp8(mouse_acc.dy);
        int8_t cv = clamp8(mouse_acc.vertical);
        int8_t ch = clamp8(mouse_acc.horizontal);
        // Pop queued button state if available, ensuring each button
        // transition gets its own USB report
        uint8_t buttons = mouse_acc.buttons;
        if (!queue_is_empty(&fifo_mouse_btn)) {
            queue_try_remove(&fifo_mouse_btn, &buttons);
        }
        tud_hid_mouse_report(
                REPORT_ID_MOUSE,
                buttons,
                cx, cy, cv, ch);
        mouse_acc.dx -= cx;
        mouse_acc.dy -= cy;
        mouse_acc.vertical -= cv;
        mouse_acc.horizontal -= ch;
        mouse_acc.last_sent_buttons = buttons;
    } else if (report_id == REPORT_ID_CONSUMER_CONTROL && !queue_is_empty(&fifo_consumer)) {
        uint16_t code;
        if (queue_try_remove(&fifo_consumer, &code)) {
            // printf("Sending consumer code: %04x\r\n", code);
            tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &code, sizeof(code));
        }
    } else if (report_id == REPORT_ID_SYSTEM_CONTROL && !queue_is_empty(&fifo_system)) {
        uint8_t report_val;
        if (queue_try_remove(&fifo_system, &report_val)) {
            // printf("Sending system code: %02x\r\n", report_val);
            tud_hid_report(REPORT_ID_SYSTEM_CONTROL, &report_val, sizeof(report_val));
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

    // Don't access queues if USB is not mounted
    if (!usb_mounted) return;

    // Check for remote wakeup BEFORE checking tud_hid_ready()
    // (tud_hid_ready() returns false when suspended, so we'd never reach wakeup logic)
    if (tud_suspended() &&
            (!queue_is_empty(&fifo_keyboard) ||
             mouse_has_pending() ||
             !queue_is_empty(&fifo_consumer) ||
             !queue_is_empty(&fifo_system))) {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        if (remote_wakeup_enabled) {
            printf("USB: Triggering remote wakeup (reason:");
            if (!queue_is_empty(&fifo_keyboard)) printf(" keyboard");
            if (mouse_has_pending()) printf(" mouse[dx=%d,dy=%d,v=%d,h=%d,btn=0x%02x]",
                (int)mouse_acc.dx, (int)mouse_acc.dy, (int)mouse_acc.vertical,
                (int)mouse_acc.horizontal, mouse_acc.buttons);
            if (!queue_is_empty(&fifo_consumer)) printf(" consumer");
            if (!queue_is_empty(&fifo_system)) printf(" system");
            printf(")\r\n");
            tud_remote_wakeup();
        } else {
            printf("USB: Remote wakeup not enabled by host\r\n");
        }
        return;
    }

    // skip if hid is not ready yet
    if (!tud_hid_ready()) {
        return;
    }

    // try sending whichever we have in queue
    if (!queue_is_empty(&fifo_keyboard)) {
        send_events(REPORT_ID_KEYBOARD);
    } else if (!queue_is_empty(&fifo_consumer)) {
        send_events(REPORT_ID_CONSUMER_CONTROL);
    } else if (!queue_is_empty(&fifo_system)) {
        send_events(REPORT_ID_SYSTEM_CONTROL);
    } else if (mouse_has_pending()) {
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