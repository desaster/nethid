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
#include <stddef.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <pico/bootrom.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/sio.h>

#include "bsp/board.h"  // for board_millis()
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "dhcpserver.h"

#include "ap_mode.h"
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

// Flash storage for configuration
// Use the last sector of flash (4KB) for our config
// Pico W has 2MB flash, so last sector starts at 2MB - 4KB
#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_CONFIG_ADDR (XIP_BASE + FLASH_CONFIG_OFFSET)

// Magic value to identify valid config (version 3 with settings)
#define CONFIG_MAGIC 0x4E455433  // "NET3" (version 3)

// Settings flags bitfield
#define SETTINGS_FLAG_HOSTNAME (1 << 0)

// Config structure stored in flash
typedef struct {
    uint32_t magic;
    uint32_t settings_flags;     // Bitfield: which settings are configured
    uint8_t force_ap_mode;
    uint8_t has_credentials;     // 1 if SSID/password are valid
    uint8_t reserved_flags[2];
    char wifi_ssid[WIFI_SSID_MAX_LEN + 1];      // null-terminated
    char wifi_password[WIFI_PASSWORD_MAX_LEN + 1];  // null-terminated
    char hostname[HOSTNAME_MAX_LEN + 1];        // null-terminated
    uint8_t reserved_settings[128];             // Future settings space
    uint32_t checksum;
} flash_config_t;

// Static state
static char ap_ssid[32];
static dhcp_server_t dhcp_server;

// Forward declarations
static void write_config(const flash_config_t *cfg);

// Calculate simple checksum over config data (excluding checksum field itself)
static uint32_t calc_checksum(const flash_config_t *cfg)
{
    const uint8_t *data = (const uint8_t *)cfg;
    // Sum all bytes except the checksum field at the end
    size_t len = offsetof(flash_config_t, checksum);
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum ^ 0xDEADBEEF;
}

// Read config from flash
static bool read_config(flash_config_t *cfg)
{
    const flash_config_t *flash_cfg = (const flash_config_t *)FLASH_CONFIG_ADDR;

    if (flash_cfg->magic != CONFIG_MAGIC) {
        return false;
    }

    memcpy(cfg, flash_cfg, sizeof(flash_config_t));

    if (cfg->checksum != calc_checksum(cfg)) {
        return false;
    }

    return true;
}

// Write config to flash
static void write_config(const flash_config_t *cfg)
{
    // Calculate number of pages needed (round up to page size)
    const size_t pages_needed = (sizeof(flash_config_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    const size_t buffer_size = pages_needed * FLASH_PAGE_SIZE;

    // Prepare data with proper alignment (flash writes must be 256-byte aligned)
    uint8_t buffer[buffer_size] __attribute__((aligned(4)));
    memset(buffer, 0xFF, sizeof(buffer));
    memcpy(buffer, cfg, sizeof(flash_config_t));

    // Disable interrupts during flash operations
    uint32_t ints = save_and_disable_interrupts();

    // Erase the sector
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);

    // Write the pages
    flash_range_program(FLASH_CONFIG_OFFSET, buffer, buffer_size);

    restore_interrupts(ints);
}

bool ap_mode_check_force_flag(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    return cfg.force_ap_mode != 0;
}

void ap_mode_clear_force_flag(void)
{
    flash_config_t cfg;

    if (read_config(&cfg)) {
        if (cfg.force_ap_mode == 0) {
            return;  // Already cleared
        }
        // Preserve existing credentials, just clear the flag
        cfg.force_ap_mode = 0;
    } else {
        // No valid config, create a fresh one
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = CONFIG_MAGIC;
        cfg.force_ap_mode = 0;
        cfg.has_credentials = 0;
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);
    printf("Force AP flag cleared\r\n");
}

void ap_mode_set_force_flag(void)
{
    flash_config_t cfg;

    // Try to read existing config to preserve credentials
    if (!read_config(&cfg)) {
        // No valid config, create fresh
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = CONFIG_MAGIC;
        cfg.has_credentials = 0;
    }

    cfg.force_ap_mode = 1;
    cfg.checksum = calc_checksum(&cfg);

    write_config(&cfg);
    printf("Force AP flag set\r\n");
}

bool wifi_credentials_exist(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    return cfg.has_credentials != 0;
}

bool wifi_credentials_get(char *ssid, char *password)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    if (!cfg.has_credentials) {
        return false;
    }

    // Copy credentials (already null-terminated in flash)
    strncpy(ssid, cfg.wifi_ssid, WIFI_SSID_MAX_LEN);
    ssid[WIFI_SSID_MAX_LEN] = '\0';

    strncpy(password, cfg.wifi_password, WIFI_PASSWORD_MAX_LEN);
    password[WIFI_PASSWORD_MAX_LEN] = '\0';

    return true;
}

bool wifi_credentials_get_ssid(char *ssid)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    if (!cfg.has_credentials) {
        return false;
    }

    strncpy(ssid, cfg.wifi_ssid, WIFI_SSID_MAX_LEN);
    ssid[WIFI_SSID_MAX_LEN] = '\0';

    return true;
}

bool wifi_credentials_set(const char *ssid, const char *password)
{
    flash_config_t cfg;

    // Validate inputs
    if (ssid == NULL || password == NULL) {
        return false;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);

    if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN) {
        printf("Invalid SSID length: %zu\r\n", ssid_len);
        return false;
    }

    if (pass_len > WIFI_PASSWORD_MAX_LEN) {
        printf("Invalid password length: %zu\r\n", pass_len);
        return false;
    }

    // Try to read existing config to preserve force_ap flag
    if (!read_config(&cfg)) {
        // No valid config, create fresh
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = CONFIG_MAGIC;
        cfg.force_ap_mode = 0;
    }

    // Store credentials
    memset(cfg.wifi_ssid, 0, sizeof(cfg.wifi_ssid));
    memset(cfg.wifi_password, 0, sizeof(cfg.wifi_password));
    strncpy(cfg.wifi_ssid, ssid, WIFI_SSID_MAX_LEN);
    strncpy(cfg.wifi_password, password, WIFI_PASSWORD_MAX_LEN);
    cfg.has_credentials = 1;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("WiFi credentials saved (SSID: %s)\r\n", ssid);
    return true;
}

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
    BOOTSEL_TRIGGERED
} bootsel_state = BOOTSEL_IDLE;

static uint32_t bootsel_press_start = 0;
static uint16_t saved_blink_state = 0;

// Fast blink pattern for BOOTSEL feedback
#define BLINK_BOOTSEL_HELD 0b1010101010101010

void bootsel_task(void)
{
    bool pressed = get_bootsel_button();
    uint32_t now = board_millis();

    switch (bootsel_state) {
        case BOOTSEL_IDLE:
            if (pressed) {
                bootsel_state = BOOTSEL_PRESSED;
                bootsel_press_start = now;
                saved_blink_state = blink_state;
                printf("BOOTSEL pressed, hold for %d seconds to enter AP mode\r\n",
                       BOOTSEL_HOLD_TIME_MS / 1000);
            }
            break;

        case BOOTSEL_PRESSED:
            if (!pressed) {
                // Released before timeout
                bootsel_state = BOOTSEL_IDLE;
                blink_state = saved_blink_state;
                printf("BOOTSEL released\r\n");
            } else if (now - bootsel_press_start >= BOOTSEL_HOLD_TIME_MS) {
                // Held long enough
                bootsel_state = BOOTSEL_TRIGGERED;
                printf("BOOTSEL held for %d seconds, setting AP mode flag and rebooting...\r\n",
                       BOOTSEL_HOLD_TIME_MS / 1000);

                // Set the force AP flag
                ap_mode_set_force_flag();

                // Give visual feedback
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                sleep_ms(200);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                sleep_ms(200);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                sleep_ms(200);

                // Reboot using watchdog
                watchdog_reboot(0, 0, 0);
                while (1) {
                    tight_loop_contents();
                }
            } else {
                // Still holding - fast blink feedback
                blink_state = BLINK_BOOTSEL_HELD;
            }
            break;

        case BOOTSEL_TRIGGERED:
            // Should never get here, we reboot
            break;
    }
}

//--------------------------------------------------------------------+
// Device Settings API
//--------------------------------------------------------------------+

// Generate default hostname from MAC address
static void generate_default_hostname(char *hostname, size_t len)
{
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    snprintf(hostname, len, "picow-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

// Validate hostname (RFC 1123)
static bool validate_hostname(const char *hostname)
{
    if (hostname == NULL) return false;

    size_t len = strlen(hostname);
    if (len == 0 || len > HOSTNAME_MAX_LEN) return false;

    // Cannot start or end with hyphen
    if (hostname[0] == '-' || hostname[len - 1] == '-') return false;

    // Must contain only alphanumeric and hyphen
    for (size_t i = 0; i < len; i++) {
        char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-')) {
            return false;
        }
    }

    return true;
}

bool settings_get_hostname(char *hostname)
{
    flash_config_t cfg;

    if (read_config(&cfg) && (cfg.settings_flags & SETTINGS_FLAG_HOSTNAME) && cfg.hostname[0] != '\0') {
        strncpy(hostname, cfg.hostname, HOSTNAME_MAX_LEN);
        hostname[HOSTNAME_MAX_LEN] = '\0';
        return true;
    }

    // Return default hostname
    generate_default_hostname(hostname, HOSTNAME_MAX_LEN + 1);
    return false;
}

bool settings_hostname_is_default(void)
{
    flash_config_t cfg;
    return !read_config(&cfg) || !(cfg.settings_flags & SETTINGS_FLAG_HOSTNAME) || cfg.hostname[0] == '\0';
}

bool settings_set_hostname(const char *hostname)
{
    if (!validate_hostname(hostname)) {
        printf("Invalid hostname: %s\r\n", hostname ? hostname : "(null)");
        return false;
    }

    flash_config_t cfg;

    // Try to read existing config to preserve other settings
    if (!read_config(&cfg)) {
        // No valid config, create fresh
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = CONFIG_MAGIC;
        cfg.force_ap_mode = 0;
        cfg.has_credentials = 0;
    }

    // Store hostname
    strncpy(cfg.hostname, hostname, HOSTNAME_MAX_LEN);
    cfg.hostname[HOSTNAME_MAX_LEN] = '\0';
    cfg.settings_flags |= SETTINGS_FLAG_HOSTNAME;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("Hostname saved: %s\r\n", hostname);
    return true;
}
