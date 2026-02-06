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

#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

// WiFi credential limits (matching IEEE 802.11 standards)
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

// Device settings limits
#define HOSTNAME_MAX_LEN 32

// MQTT settings limits
#define MQTT_BROKER_MAX_LEN 63
#define MQTT_TOPIC_MAX_LEN 63
#define MQTT_USERNAME_MAX_LEN 31
#define MQTT_PASSWORD_MAX_LEN 63
#define MQTT_CLIENT_ID_MAX_LEN 31
#define MQTT_DEFAULT_PORT 1883

// Syslog settings limits
#define SYSLOG_SERVER_MAX_LEN 63  // Hostname or IPv4 address
#define SYSLOG_DEFAULT_PORT 514

// Settings flags bitfield
#define SETTINGS_FLAG_HOSTNAME       (1 << 0)
#define SETTINGS_FLAG_MQTT_BROKER    (1 << 1)
#define SETTINGS_FLAG_MQTT_PORT      (1 << 2)
#define SETTINGS_FLAG_MQTT_TOPIC     (1 << 3)
#define SETTINGS_FLAG_MQTT_USER      (1 << 4)
#define SETTINGS_FLAG_MQTT_PASS      (1 << 5)
#define SETTINGS_FLAG_MQTT_ENABLED   (1 << 6)
#define SETTINGS_FLAG_MQTT_CLIENT_ID (1 << 7)
#define SETTINGS_FLAG_SYSLOG_SERVER  (1 << 8)
#define SETTINGS_FLAG_SYSLOG_PORT    (1 << 9)

//--------------------------------------------------------------------+
// Force AP Mode Flag
//--------------------------------------------------------------------+

// Check if "force AP" flag is set in flash
bool settings_get_force_ap(void);

// Clear the "force AP" flag in flash
void settings_clear_force_ap(void);

// Set the "force AP" flag in flash (called before reboot)
void settings_set_force_ap(void);

//--------------------------------------------------------------------+
// WiFi Credentials
//--------------------------------------------------------------------+

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

//--------------------------------------------------------------------+
// Hostname Settings
//--------------------------------------------------------------------+

// Get hostname - returns configured hostname or MAC-based default
// hostname buffer must be at least HOSTNAME_MAX_LEN+1 bytes
// Returns true if configured hostname was found, false if using default
bool settings_get_hostname(char *hostname);

// Set hostname - validates and stores to flash
// Returns true on success, false on invalid hostname
bool settings_set_hostname(const char *hostname);

// Check if hostname is the auto-generated default
bool settings_hostname_is_default(void);

//--------------------------------------------------------------------+
// MQTT Settings
//--------------------------------------------------------------------+

// Check if MQTT is enabled
bool settings_get_mqtt_enabled(void);

// Enable/disable MQTT
bool settings_set_mqtt_enabled(bool enabled);

// Get MQTT broker hostname/IP
// broker buffer must be at least MQTT_BROKER_MAX_LEN+1 bytes
// Returns true if configured, false if not set
bool settings_get_mqtt_broker(char *broker);

// Set MQTT broker hostname/IP
bool settings_set_mqtt_broker(const char *broker);

// Get MQTT port (returns default 1883 if not configured)
uint16_t settings_get_mqtt_port(void);

// Set MQTT port
bool settings_set_mqtt_port(uint16_t port);

// Get MQTT topic
// topic buffer must be at least MQTT_TOPIC_MAX_LEN+1 bytes
// Returns true if configured, false if not set
bool settings_get_mqtt_topic(char *topic);

// Set MQTT topic
bool settings_set_mqtt_topic(const char *topic);

// Get MQTT username (optional)
// username buffer must be at least MQTT_USERNAME_MAX_LEN+1 bytes
// Returns true if configured, false if not set
bool settings_get_mqtt_username(char *username);

// Set MQTT username (empty string to clear)
bool settings_set_mqtt_username(const char *username);

// Get MQTT password (optional)
// password buffer must be at least MQTT_PASSWORD_MAX_LEN+1 bytes
// Returns true if configured, false if not set
bool settings_get_mqtt_password(char *password);

// Set MQTT password (empty string to clear)
bool settings_set_mqtt_password(const char *password);

// Check if MQTT password is set (without exposing it)
bool settings_mqtt_has_password(void);

// Get MQTT client ID (optional, defaults to hostname if not set)
// client_id buffer must be at least MQTT_CLIENT_ID_MAX_LEN+1 bytes
// Returns true if configured, false if using default (hostname)
bool settings_get_mqtt_client_id(char *client_id);

// Set MQTT client ID (empty string to use hostname as default)
bool settings_set_mqtt_client_id(const char *client_id);

//--------------------------------------------------------------------+
// Syslog Settings
//--------------------------------------------------------------------+

// Get syslog server hostname/IP (empty string if not configured)
// server buffer must be at least SYSLOG_SERVER_MAX_LEN+1 bytes
// Returns true if configured, false if not set
bool settings_get_syslog_server(char *server);

// Set syslog server hostname/IP (empty string to disable)
bool settings_set_syslog_server(const char *server);

// Get syslog port (returns default 514 if not configured)
uint16_t settings_get_syslog_port(void);

// Set syslog port
bool settings_set_syslog_port(uint16_t port);

#endif
