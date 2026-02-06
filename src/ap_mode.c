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

#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <hardware/watchdog.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/sio.h>
#include <hardware/sync.h>

#include "bsp/board.h"  // for board_millis()
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "dhcpserver.h"

#include "ap_mode.h"
#include "settings.h"
#include "board.h"

// BOOTSEL button reading - based on pico-examples/picoboard/button/button.c
// The BOOTSEL button is directly connected to the QSPI CS pin, so we need to
// temporarily take over that pin to read the button state.
static bool __no_inline_not_in_flash_func(get_bootsel_button)(void)
{
    const uint CS_PIN_INDEX = 1;

    uint32_t flags = save_and_disable_interrupts();

    // Set CS pin to high-impedance (input)
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Delay to let pin settle
    for (volatile int i = 0; i < 1000; ++i);

    // Read the pin state (button pulls low when pressed)
#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Restore CS pin to normal operation
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

// Static state
static char ap_ssid[32];
static dhcp_server_t dhcp_server;

int ap_mode_start(void)
{
    printf("Starting AP mode\r\n");

    // Enable STA mode briefly to read MAC address (required before AP mode)
    cyw43_arch_enable_sta_mode();
    sleep_ms(100);

    // Get MAC address for SSID suffix
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);

    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X%02X",
             AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

    printf("AP SSID: %s, Password: %s\r\n", ap_ssid, AP_PASSWORD);

    // Switch to AP mode
    cyw43_arch_enable_ap_mode(ap_ssid, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    // Wait for AP mode to initialize
    sleep_ms(500);

    // Configure IP address
    ip4_addr_t ip, netmask;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    netif_set_addr(netif_default, &ip, &netmask, &ip);

    printf("AP IP: %s\r\n", ip4addr_ntoa(&ip));

    // Start DHCP server (clients get IPs starting at .16)
    dhcp_server_init(&dhcp_server, &ip, &netmask);

    return 0;
}

// BOOTSEL button state machine
static enum {
    BOOTSEL_IDLE,
    BOOTSEL_PRESSED,
    BOOTSEL_WAIT_RELEASE
} bootsel_state = BOOTSEL_IDLE;

static uint32_t bootsel_press_start = 0;
static uint32_t bootsel_release_start = 0;
static uint16_t saved_blink_state = 0;
static uint8_t bootsel_release_count = 0;

// Fast blink pattern for BOOTSEL feedback
#define BLINK_BOOTSEL_HELD 0b1010101010101010

// Debounce: require consecutive "released" reads before accepting release
#define BOOTSEL_DEBOUNCE_COUNT 50

// Max time to wait for button release before rebooting anyway
#define BOOTSEL_RELEASE_TIMEOUT_MS 10000

void bootsel_task(void)
{
    bool pressed = get_bootsel_button();
    uint32_t now = board_millis();

    switch (bootsel_state) {
        case BOOTSEL_IDLE:
            if (pressed) {
                bootsel_state = BOOTSEL_PRESSED;
                bootsel_press_start = now;
                bootsel_release_count = 0;
                saved_blink_state = blink_state;
                printf("BOOTSEL pressed, hold for %d seconds to enter AP mode\r\n",
                       BOOTSEL_HOLD_TIME_MS / 1000);
            }
            break;

        case BOOTSEL_PRESSED:
            if (!pressed) {
                bootsel_release_count++;
                if (bootsel_release_count >= BOOTSEL_DEBOUNCE_COUNT) {
                    // Genuinely released before hold time reached
                    bootsel_state = BOOTSEL_IDLE;
                    blink_state = saved_blink_state;
                    bootsel_release_count = 0;
                    printf("BOOTSEL released\r\n");
                }
            } else {
                // Still pressed - reset release debounce counter
                bootsel_release_count = 0;
                if (now - bootsel_press_start >= BOOTSEL_HOLD_TIME_MS) {
                    // Held long enough - set flag, wait for release before rebooting
                    printf("BOOTSEL held for %d seconds, setting AP mode flag...\r\n",
                           BOOTSEL_HOLD_TIME_MS / 1000);
                    settings_set_force_ap();

                    bootsel_state = BOOTSEL_WAIT_RELEASE;
                    bootsel_release_start = now;
                    blink_state = 0xFFFF;  // Solid LED = "release the button"
                    printf("Release BOOTSEL to reboot into AP mode\r\n");
                } else {
                    // Still holding - fast blink feedback
                    blink_state = BLINK_BOOTSEL_HELD;
                }
            }
            break;

        case BOOTSEL_WAIT_RELEASE:
            if (!pressed) {
                // Button released - safe to reboot (BOOTROM won't see BOOTSEL held)
                printf("BOOTSEL released, rebooting to AP mode...\r\n");
                watchdog_reboot(0, 0, 0);
                while (1) { tight_loop_contents(); }
            } else if (now - bootsel_release_start >= BOOTSEL_RELEASE_TIMEOUT_MS) {
                // Timeout - reboot anyway (flag is saved, user can re-flash to recover)
                printf("BOOTSEL release timeout, rebooting anyway\r\n");
                watchdog_reboot(0, 0, 0);
                while (1) { tight_loop_contents(); }
            }
            break;
    }
}
