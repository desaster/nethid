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
#include <hardware/flash.h>
#include <hardware/sync.h>

#include "settings.h"

// Flash storage for configuration
// Use the last sector of flash (4KB) for our config
// Pico W has 2MB flash, so last sector starts at 2MB - 4KB
#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_CONFIG_ADDR (XIP_BASE + FLASH_CONFIG_OFFSET)

// Magic values for config versioning
#define CONFIG_MAGIC 0x4E455436  // "NET6"

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
    // MQTT settings
    uint8_t mqtt_enabled;
    uint16_t mqtt_port;
    char mqtt_broker[MQTT_BROKER_MAX_LEN + 1];      // null-terminated
    char mqtt_topic[MQTT_TOPIC_MAX_LEN + 1];        // null-terminated
    char mqtt_username[MQTT_USERNAME_MAX_LEN + 1];  // null-terminated
    char mqtt_password[MQTT_PASSWORD_MAX_LEN + 1];  // null-terminated
    char mqtt_client_id[MQTT_CLIENT_ID_MAX_LEN + 1]; // null-terminated
    // Syslog settings
    char syslog_server[SYSLOG_SERVER_MAX_LEN + 1];  // null-terminated hostname or IPv4
    uint16_t syslog_port;
    uint8_t reserved_settings[16];              // Future settings space
    uint32_t checksum;
} flash_config_t;

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

// Initialize a fresh config structure
static void init_fresh_config(flash_config_t *cfg)
{
    memset(cfg, 0, sizeof(flash_config_t));
    cfg->magic = CONFIG_MAGIC;
    cfg->force_ap_mode = 0;
    cfg->has_credentials = 0;
    cfg->mqtt_enabled = 0;
    cfg->mqtt_port = MQTT_DEFAULT_PORT;
    cfg->syslog_server[0] = '\0';
    cfg->syslog_port = SYSLOG_DEFAULT_PORT;
}

//--------------------------------------------------------------------+
// Force AP Mode Flag
//--------------------------------------------------------------------+

bool settings_get_force_ap(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    return cfg.force_ap_mode != 0;
}

void settings_clear_force_ap(void)
{
    flash_config_t cfg;

    if (read_config(&cfg)) {
        if (cfg.force_ap_mode == 0) {
            return;  // Already cleared
        }
        // Preserve existing settings, just clear the flag
        cfg.force_ap_mode = 0;
    } else {
        // No valid config, create a fresh one
        init_fresh_config(&cfg);
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);
    printf("Force AP flag cleared\r\n");
}

void settings_set_force_ap(void)
{
    flash_config_t cfg;

    // Try to read existing config to preserve other settings
    if (!read_config(&cfg)) {
        // No valid config, create fresh
        init_fresh_config(&cfg);
    }

    cfg.force_ap_mode = 1;
    cfg.checksum = calc_checksum(&cfg);

    write_config(&cfg);
    printf("Force AP flag set\r\n");
}

//--------------------------------------------------------------------+
// WiFi Credentials
//--------------------------------------------------------------------+

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

    // Try to read existing config to preserve other settings
    if (!read_config(&cfg)) {
        // No valid config, create fresh
        init_fresh_config(&cfg);
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

//--------------------------------------------------------------------+
// Hostname Settings
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
        init_fresh_config(&cfg);
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

//--------------------------------------------------------------------+
// MQTT Settings
//--------------------------------------------------------------------+

bool settings_get_mqtt_enabled(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    return (cfg.settings_flags & SETTINGS_FLAG_MQTT_ENABLED) && cfg.mqtt_enabled;
}

bool settings_set_mqtt_enabled(bool enabled)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    cfg.mqtt_enabled = enabled ? 1 : 0;
    cfg.settings_flags |= SETTINGS_FLAG_MQTT_ENABLED;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT %s\r\n", enabled ? "enabled" : "disabled");
    return true;
}

bool settings_get_mqtt_broker(char *broker)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        broker[0] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_BROKER) || cfg.mqtt_broker[0] == '\0') {
        broker[0] = '\0';
        return false;
    }

    strncpy(broker, cfg.mqtt_broker, MQTT_BROKER_MAX_LEN);
    broker[MQTT_BROKER_MAX_LEN] = '\0';
    return true;
}

bool settings_set_mqtt_broker(const char *broker)
{
    if (broker == NULL) {
        return false;
    }

    size_t len = strlen(broker);
    if (len > MQTT_BROKER_MAX_LEN) {
        printf("MQTT broker too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.mqtt_broker, 0, sizeof(cfg.mqtt_broker));
    strncpy(cfg.mqtt_broker, broker, MQTT_BROKER_MAX_LEN);
    cfg.settings_flags |= SETTINGS_FLAG_MQTT_BROKER;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT broker saved: %s\r\n", broker);
    return true;
}

uint16_t settings_get_mqtt_port(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return MQTT_DEFAULT_PORT;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_PORT) || cfg.mqtt_port == 0) {
        return MQTT_DEFAULT_PORT;
    }

    return cfg.mqtt_port;
}

bool settings_set_mqtt_port(uint16_t port)
{
    if (port == 0) {
        printf("Invalid MQTT port: 0\r\n");
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    cfg.mqtt_port = port;
    cfg.settings_flags |= SETTINGS_FLAG_MQTT_PORT;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT port saved: %u\r\n", port);
    return true;
}

bool settings_get_mqtt_topic(char *topic)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        topic[0] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_TOPIC) || cfg.mqtt_topic[0] == '\0') {
        topic[0] = '\0';
        return false;
    }

    strncpy(topic, cfg.mqtt_topic, MQTT_TOPIC_MAX_LEN);
    topic[MQTT_TOPIC_MAX_LEN] = '\0';
    return true;
}

bool settings_set_mqtt_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    size_t len = strlen(topic);
    if (len > MQTT_TOPIC_MAX_LEN) {
        printf("MQTT topic too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.mqtt_topic, 0, sizeof(cfg.mqtt_topic));
    strncpy(cfg.mqtt_topic, topic, MQTT_TOPIC_MAX_LEN);
    cfg.settings_flags |= SETTINGS_FLAG_MQTT_TOPIC;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT topic saved: %s\r\n", topic);
    return true;
}

bool settings_get_mqtt_username(char *username)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        username[0] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_USER) || cfg.mqtt_username[0] == '\0') {
        username[0] = '\0';
        return false;
    }

    strncpy(username, cfg.mqtt_username, MQTT_USERNAME_MAX_LEN);
    username[MQTT_USERNAME_MAX_LEN] = '\0';
    return true;
}

bool settings_set_mqtt_username(const char *username)
{
    if (username == NULL) {
        return false;
    }

    size_t len = strlen(username);
    if (len > MQTT_USERNAME_MAX_LEN) {
        printf("MQTT username too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.mqtt_username, 0, sizeof(cfg.mqtt_username));
    if (len > 0) {
        strncpy(cfg.mqtt_username, username, MQTT_USERNAME_MAX_LEN);
        cfg.settings_flags |= SETTINGS_FLAG_MQTT_USER;
    } else {
        cfg.settings_flags &= ~SETTINGS_FLAG_MQTT_USER;
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT username %s\r\n", len > 0 ? "saved" : "cleared");
    return true;
}

bool settings_get_mqtt_password(char *password)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        password[0] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_PASS) || cfg.mqtt_password[0] == '\0') {
        password[0] = '\0';
        return false;
    }

    strncpy(password, cfg.mqtt_password, MQTT_PASSWORD_MAX_LEN);
    password[MQTT_PASSWORD_MAX_LEN] = '\0';
    return true;
}

bool settings_set_mqtt_password(const char *password)
{
    if (password == NULL) {
        return false;
    }

    size_t len = strlen(password);
    if (len > MQTT_PASSWORD_MAX_LEN) {
        printf("MQTT password too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.mqtt_password, 0, sizeof(cfg.mqtt_password));
    if (len > 0) {
        strncpy(cfg.mqtt_password, password, MQTT_PASSWORD_MAX_LEN);
        cfg.settings_flags |= SETTINGS_FLAG_MQTT_PASS;
    } else {
        cfg.settings_flags &= ~SETTINGS_FLAG_MQTT_PASS;
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT password %s\r\n", len > 0 ? "saved" : "cleared");
    return true;
}

bool settings_mqtt_has_password(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return false;
    }

    return (cfg.settings_flags & SETTINGS_FLAG_MQTT_PASS) && cfg.mqtt_password[0] != '\0';
}

bool settings_get_mqtt_client_id(char *client_id)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        // Fall back to hostname (use internal buffer to avoid overflow since
        // HOSTNAME_MAX_LEN > MQTT_CLIENT_ID_MAX_LEN)
        char hostname[HOSTNAME_MAX_LEN + 1];
        settings_get_hostname(hostname);
        strncpy(client_id, hostname, MQTT_CLIENT_ID_MAX_LEN);
        client_id[MQTT_CLIENT_ID_MAX_LEN] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_MQTT_CLIENT_ID) || cfg.mqtt_client_id[0] == '\0') {
        // Fall back to hostname (use internal buffer to avoid overflow)
        char hostname[HOSTNAME_MAX_LEN + 1];
        settings_get_hostname(hostname);
        strncpy(client_id, hostname, MQTT_CLIENT_ID_MAX_LEN);
        client_id[MQTT_CLIENT_ID_MAX_LEN] = '\0';
        return false;
    }

    strncpy(client_id, cfg.mqtt_client_id, MQTT_CLIENT_ID_MAX_LEN);
    client_id[MQTT_CLIENT_ID_MAX_LEN] = '\0';
    return true;
}

bool settings_set_mqtt_client_id(const char *client_id)
{
    if (client_id == NULL) {
        return false;
    }

    size_t len = strlen(client_id);
    if (len > MQTT_CLIENT_ID_MAX_LEN) {
        printf("MQTT client ID too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.mqtt_client_id, 0, sizeof(cfg.mqtt_client_id));
    if (len > 0) {
        strncpy(cfg.mqtt_client_id, client_id, MQTT_CLIENT_ID_MAX_LEN);
        cfg.settings_flags |= SETTINGS_FLAG_MQTT_CLIENT_ID;
    } else {
        cfg.settings_flags &= ~SETTINGS_FLAG_MQTT_CLIENT_ID;
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("MQTT client ID %s\r\n", len > 0 ? "saved" : "cleared (using hostname)");
    return true;
}

//--------------------------------------------------------------------+
// Syslog Settings
//--------------------------------------------------------------------+

bool settings_get_syslog_server(char *server)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        server[0] = '\0';
        return false;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_SYSLOG_SERVER) || cfg.syslog_server[0] == '\0') {
        server[0] = '\0';
        return false;
    }

    strncpy(server, cfg.syslog_server, SYSLOG_SERVER_MAX_LEN);
    server[SYSLOG_SERVER_MAX_LEN] = '\0';
    return true;
}

bool settings_set_syslog_server(const char *server)
{
    if (server == NULL) {
        return false;
    }

    size_t len = strlen(server);
    if (len > SYSLOG_SERVER_MAX_LEN) {
        printf("Syslog server too long: %zu\r\n", len);
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    memset(cfg.syslog_server, 0, sizeof(cfg.syslog_server));
    if (len > 0) {
        strncpy(cfg.syslog_server, server, SYSLOG_SERVER_MAX_LEN);
        cfg.settings_flags |= SETTINGS_FLAG_SYSLOG_SERVER;
    } else {
        cfg.settings_flags &= ~SETTINGS_FLAG_SYSLOG_SERVER;
    }

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("Syslog server %s\r\n", len > 0 ? "saved" : "cleared");
    return true;
}

uint16_t settings_get_syslog_port(void)
{
    flash_config_t cfg;

    if (!read_config(&cfg)) {
        return SYSLOG_DEFAULT_PORT;
    }

    if (!(cfg.settings_flags & SETTINGS_FLAG_SYSLOG_PORT) || cfg.syslog_port == 0) {
        return SYSLOG_DEFAULT_PORT;
    }

    return cfg.syslog_port;
}

bool settings_set_syslog_port(uint16_t port)
{
    if (port == 0) {
        printf("Invalid syslog port: 0\r\n");
        return false;
    }

    flash_config_t cfg;

    if (!read_config(&cfg)) {
        init_fresh_config(&cfg);
    }

    cfg.syslog_port = port;
    cfg.settings_flags |= SETTINGS_FLAG_SYSLOG_PORT;

    cfg.checksum = calc_checksum(&cfg);
    write_config(&cfg);

    printf("Syslog port saved: %u\r\n", port);
    return true;
}
