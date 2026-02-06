/*
 * HTTP server module for NetHID
 *
 * Uses lwIP's built-in httpd with custom file handling for API endpoints.
 */

#include "httpd.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "hardware/watchdog.h"

#include "board.h"
#include "settings.h"
#include "ap_mode.h"
#include "wifi_scan.h"
#include "usb.h"
#include "hid_keys.h"
#include "websocket/websocket.h"
#include "cjson/cJSON.h"

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

// Response buffer for dynamic API responses (larger for MQTT settings)
static char api_response[768];

// Uptime counter
static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

// POST request handling (larger buffer for MQTT settings)
static char post_uri[64];
static char post_body[512];
static size_t post_body_len = 0;

// Config API state
static bool post_config_success = false;
static bool scan_triggered = false;

// Settings API state
static bool settings_api_success = false;
static char settings_api_error[64] = "";

// HID API state
static uint8_t current_mouse_buttons = 0;
static bool hid_api_success = false;
static char hid_api_error[64] = "";
extern uint8_t keycodes[6];  // from usb.c

//--------------------------------------------------------------------+
// Utility Functions
//--------------------------------------------------------------------+

// Update uptime counter
static void update_uptime(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_uptime_update >= 1000) {
        uptime_seconds += (now - last_uptime_update) / 1000;
        last_uptime_update = now - (now - last_uptime_update) % 1000;
    }
}

// Map WiFi auth_mode to human-readable string
static const char *auth_mode_to_string(uint8_t auth_mode)
{
    if (auth_mode == 0) return "Open";
    if (auth_mode & 0x04) return "WPA2";
    if (auth_mode & 0x02) return "WPA";
    return "Secured";
}

// Build JSON response with HTTP headers and populate fs_file
static void api_json_response(struct fs_file *file, const char *body, int body_len)
{
    int len = snprintf(api_response, sizeof(api_response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        body_len, body);

    memset(file, 0, sizeof(struct fs_file));
    file->data = api_response;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
}

// Send cJSON object as response (frees the cJSON object)
static void api_cjson_response(struct fs_file *file, cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (body) {
        api_json_response(file, body, strlen(body));
        cJSON_free(body);
    } else {
        api_json_response(file, "{\"error\":\"json serialization failed\"}", 38);
    }
}

// Helper to get string from cJSON object (returns NULL if not found or not string)
static const char *cjson_get_string(cJSON *json, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

// Helper to get int from cJSON object (returns false if not found or not number)
static bool cjson_get_int(cJSON *json, const char *key, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsNumber(item)) {
        *value = item->valueint;
        return true;
    }
    return false;
}

// Helper to get bool from cJSON object (returns false if not found)
static bool cjson_get_bool(cJSON *json, const char *key, bool *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item);
        return true;
    }
    return false;
}

//--------------------------------------------------------------------+
// Device/Config API Handlers
//--------------------------------------------------------------------+

// GET /api/status - device info
static int handle_api_status(struct fs_file *file)
{
    update_uptime();

    uint8_t mac[6];
    int itf = in_ap_mode ? CYW43_ITF_AP : CYW43_ITF_STA;
    cyw43_wifi_get_mac(&cyw43_state, itf, mac);

    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(ip));

    char hostname[HOSTNAME_MAX_LEN + 1];
    settings_get_hostname(hostname);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "hostname", hostname);
    cJSON_AddStringToObject(json, "mac", mac_str);
    cJSON_AddStringToObject(json, "ip", ip_str);
    cJSON_AddNumberToObject(json, "uptime", uptime_seconds);
    cJSON_AddStringToObject(json, "mode", in_ap_mode ? "ap" : "sta");
    cJSON_AddStringToObject(json, "version", NETHID_VERSION);
    cJSON_AddBoolToObject(json, "usb_mounted", usb_mounted);
    cJSON_AddBoolToObject(json, "usb_suspended", usb_suspended);
    cJSON_AddBoolToObject(json, "websocket_connected", websocket_client_connected());

    api_cjson_response(file, json);
    return 1;
}

// POST /api/reboot-ap result
static int handle_api_reboot_ap_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "rebooting to AP mode");
    api_cjson_response(file, json);
    settings_set_force_ap();
    watchdog_reboot(0, 0, 0);
    return 1;
}

// POST /api/reboot result
static int handle_api_reboot_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "rebooting");
    api_cjson_response(file, json);
    watchdog_reboot(0, 0, 0);
    return 1;
}

// GET /api/config - stored WiFi config (SSID only)
static int handle_api_config_get(struct fs_file *file)
{
    char ssid[WIFI_SSID_MAX_LEN + 1];
    bool configured = wifi_credentials_get_ssid(ssid);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "configured", configured);
    cJSON_AddStringToObject(json, "ssid", configured ? ssid : "");

    api_cjson_response(file, json);
    return 1;
}

// POST /api/config result
static int handle_api_config_post_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();

    if (post_config_success) {
        cJSON_AddStringToObject(json, "status", "saved");
        cJSON_AddBoolToObject(json, "rebooting", true);
    } else {
        cJSON_AddStringToObject(json, "status", "error");
        cJSON_AddStringToObject(json, "message", "invalid request");
    }

    api_cjson_response(file, json);

    if (post_config_success) {
        watchdog_reboot(0, 0, 0);
    }
    return 1;
}

// GET /api/networks - scanned WiFi networks
static int handle_api_networks(struct fs_file *file)
{
    const wifi_scan_state_t *scan = wifi_scan_get_results();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "scanning", scan->scanning);

    cJSON *networks = cJSON_AddArrayToObject(json, "networks");
    for (int i = 0; i < scan->count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", scan->networks[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", scan->networks[i].rssi);
        cJSON_AddStringToObject(network, "auth", auth_mode_to_string(scan->networks[i].auth_mode));
        cJSON_AddNumberToObject(network, "ch", scan->networks[i].channel);
        cJSON_AddItemToArray(networks, network);
    }

    api_cjson_response(file, json);
    return 1;
}

// POST /api/scan result
static int handle_api_scan_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();

    if (scan_triggered) {
        cJSON_AddStringToObject(json, "status", "scanning");
    } else {
        cJSON_AddStringToObject(json, "status", "error");
        cJSON_AddStringToObject(json, "message", "scan failed");
    }

    api_cjson_response(file, json);
    return 1;
}

//--------------------------------------------------------------------+
// Settings API Handlers
//--------------------------------------------------------------------+

// GET /api/settings - all device settings
static int handle_api_settings_get(struct fs_file *file)
{
    // Hostname
    char hostname_buf[HOSTNAME_MAX_LEN + 1];
    bool hostname_is_default = !settings_get_hostname(hostname_buf);

    // MQTT settings
    char mqtt_broker[MQTT_BROKER_MAX_LEN + 1];
    char mqtt_topic[MQTT_TOPIC_MAX_LEN + 1];
    char mqtt_username[MQTT_USERNAME_MAX_LEN + 1];
    char mqtt_client_id[MQTT_CLIENT_ID_MAX_LEN + 1];

    bool has_broker = settings_get_mqtt_broker(mqtt_broker);
    bool has_topic = settings_get_mqtt_topic(mqtt_topic);
    bool has_username = settings_get_mqtt_username(mqtt_username);
    bool mqtt_has_pass = settings_mqtt_has_password();
    settings_get_mqtt_client_id(mqtt_client_id);

    bool mqtt_enabled = settings_get_mqtt_enabled();
    uint16_t mqtt_port = settings_get_mqtt_port();

    // Syslog settings
    char syslog_server[SYSLOG_SERVER_MAX_LEN + 1];
    bool has_syslog = settings_get_syslog_server(syslog_server);
    uint16_t syslog_port_val = settings_get_syslog_port();

    cJSON *json = cJSON_CreateObject();

    // Hostname as nested object
    cJSON *hostname_obj = cJSON_AddObjectToObject(json, "hostname");
    cJSON_AddStringToObject(hostname_obj, "value", hostname_buf);
    cJSON_AddBoolToObject(hostname_obj, "default", hostname_is_default);

    // MQTT settings
    cJSON_AddBoolToObject(json, "mqtt_enabled", mqtt_enabled);
    cJSON_AddStringToObject(json, "mqtt_broker", has_broker ? mqtt_broker : "");
    cJSON_AddNumberToObject(json, "mqtt_port", mqtt_port);
    cJSON_AddStringToObject(json, "mqtt_topic", has_topic ? mqtt_topic : "");
    cJSON_AddStringToObject(json, "mqtt_username", has_username ? mqtt_username : "");
    cJSON_AddBoolToObject(json, "mqtt_has_password", mqtt_has_pass);
    cJSON_AddStringToObject(json, "mqtt_client_id", mqtt_client_id);

    // Syslog settings
    cJSON_AddStringToObject(json, "syslog_server", has_syslog ? syslog_server : "");
    cJSON_AddNumberToObject(json, "syslog_port", syslog_port_val);

    api_cjson_response(file, json);
    return 1;
}

// POST /api/settings result
static int handle_api_settings_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();

    if (settings_api_success) {
        cJSON_AddBoolToObject(json, "success", true);
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", settings_api_error);
    }

    api_cjson_response(file, json);
    return 1;
}

// Process POST /api/settings
static void process_settings_post(void)
{
    cJSON *json = cJSON_Parse(post_body);
    if (json == NULL) {
        snprintf(settings_api_error, sizeof(settings_api_error), "Invalid JSON");
        return;
    }

    // Hostname
    const char *hostname = cjson_get_string(json, "hostname");
    if (hostname && strlen(hostname) > 0) {
        if (strlen(hostname) > HOSTNAME_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "Hostname too long");
            cJSON_Delete(json);
            return;
        }
        if (!settings_set_hostname(hostname)) {
            snprintf(settings_api_error, sizeof(settings_api_error), "Invalid hostname format");
            cJSON_Delete(json);
            return;
        }
    }

    // MQTT enabled
    bool mqtt_enabled;
    if (cjson_get_bool(json, "mqtt_enabled", &mqtt_enabled)) {
        settings_set_mqtt_enabled(mqtt_enabled);
    }

    // MQTT port
    int mqtt_port;
    if (cjson_get_int(json, "mqtt_port", &mqtt_port)) {
        if (mqtt_port > 0 && mqtt_port <= 65535) {
            settings_set_mqtt_port((uint16_t)mqtt_port);
        } else {
            snprintf(settings_api_error, sizeof(settings_api_error), "Invalid MQTT port");
            cJSON_Delete(json);
            return;
        }
    }

    // MQTT broker
    const char *broker = cjson_get_string(json, "mqtt_broker");
    if (broker) {
        if (strlen(broker) > MQTT_BROKER_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "MQTT broker too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_mqtt_broker(broker);
    }

    // MQTT topic
    const char *topic = cjson_get_string(json, "mqtt_topic");
    if (topic) {
        if (strlen(topic) > MQTT_TOPIC_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "MQTT topic too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_mqtt_topic(topic);
    }

    // MQTT username
    const char *username = cjson_get_string(json, "mqtt_username");
    if (username) {
        if (strlen(username) > MQTT_USERNAME_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "MQTT username too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_mqtt_username(username);
    }

    // MQTT password
    const char *password = cjson_get_string(json, "mqtt_password");
    if (password) {
        if (strlen(password) > MQTT_PASSWORD_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "MQTT password too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_mqtt_password(password);
    }

    // MQTT client ID
    const char *client_id = cjson_get_string(json, "mqtt_client_id");
    if (client_id) {
        if (strlen(client_id) > MQTT_CLIENT_ID_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "MQTT client ID too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_mqtt_client_id(client_id);
    }

    // Syslog server
    const char *syslog_server = cjson_get_string(json, "syslog_server");
    if (syslog_server) {
        if (strlen(syslog_server) > SYSLOG_SERVER_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "Syslog server too long");
            cJSON_Delete(json);
            return;
        }
        settings_set_syslog_server(syslog_server);
    }

    // Syslog port
    int syslog_port_val;
    if (cjson_get_int(json, "syslog_port", &syslog_port_val)) {
        if (syslog_port_val > 0 && syslog_port_val <= 65535) {
            settings_set_syslog_port((uint16_t)syslog_port_val);
        } else {
            snprintf(settings_api_error, sizeof(settings_api_error), "Invalid syslog port");
            cJSON_Delete(json);
            return;
        }
    }

    cJSON_Delete(json);
    settings_api_success = true;
}

//--------------------------------------------------------------------+
// HID API Handlers
//--------------------------------------------------------------------+

// POST /api/hid/* result
static int handle_api_hid_result(struct fs_file *file)
{
    cJSON *json = cJSON_CreateObject();

    if (hid_api_success) {
        cJSON_AddBoolToObject(json, "success", true);
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", hid_api_error);
    }

    api_cjson_response(file, json);
    return 1;
}

// Process POST /api/hid/key
// Accepts: {"key": "A", "action": "tap"}
// Key can be: name ("A", "ENTER"), consumer ("VOLUME_UP"), or hex ("0x04")
static void process_hid_key(void)
{
    cJSON *json = cJSON_Parse(post_body);
    if (json == NULL) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid JSON");
        return;
    }

    const char *key_name = cjson_get_string(json, "key");
    if (key_name == NULL) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Missing key field");
        cJSON_Delete(json);
        return;
    }

    hid_key_info_t key_info;
    if (!hid_lookup_key(key_name, &key_info)) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Unknown key: %s", key_name);
        cJSON_Delete(json);
        return;
    }

    // Optional type override (for raw hex codes)
    const char *type_str = cjson_get_string(json, "type");
    if (type_str != NULL) {
        if (strcmp(type_str, "consumer") == 0) {
            key_info.type = HID_KEY_TYPE_CONSUMER;
        } else if (strcmp(type_str, "system") == 0) {
            key_info.type = HID_KEY_TYPE_SYSTEM;
        } else if (strcmp(type_str, "keyboard") != 0) {
            snprintf(hid_api_error, sizeof(hid_api_error), "Invalid type: %s", type_str);
            cJSON_Delete(json);
            return;
        }
    }

    hid_action_t action;
    if (!hid_parse_action(cjson_get_string(json, "action"), &action)) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid action");
        cJSON_Delete(json);
        return;
    }

    if (!hid_execute_key(&key_info, action)) {
        snprintf(hid_api_error, sizeof(hid_api_error), "System keys not yet implemented");
        cJSON_Delete(json);
        return;
    }

    cJSON_Delete(json);
    hid_api_success = true;
}

// Process POST /api/hid/mouse/move
static void process_hid_mouse_move(void)
{
    cJSON *json = cJSON_Parse(post_body);
    if (json == NULL) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid JSON");
        return;
    }

    int dx = 0, dy = 0;
    cjson_get_int(json, "dx", &dx);
    cjson_get_int(json, "dy", &dy);

    if (dx < -127) dx = -127;
    if (dx > 127) dx = 127;
    if (dy < -127) dy = -127;
    if (dy > 127) dy = 127;

    move_mouse(current_mouse_buttons, (int8_t)dx, (int8_t)dy, 0, 0);

    cJSON_Delete(json);
    hid_api_success = true;
}

// Process POST /api/hid/mouse/button
static void process_hid_mouse_button(void)
{
    cJSON *json = cJSON_Parse(post_body);
    if (json == NULL) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid JSON");
        return;
    }

    int button_bit;
    if (!cjson_get_int(json, "button", &button_bit) || button_bit < 1 || button_bit > 31) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid or missing button");
        cJSON_Delete(json);
        return;
    }

    const char *action = cjson_get_string(json, "action");

    bool do_press = true;
    bool do_release = true;

    if (action != NULL) {
        if (strcmp(action, "press") == 0) {
            do_release = false;
        } else if (strcmp(action, "release") == 0) {
            do_press = false;
        } else if (strcmp(action, "click") != 0) {
            snprintf(hid_api_error, sizeof(hid_api_error), "Invalid action");
            cJSON_Delete(json);
            return;
        }
    }

    if (do_press) {
        current_mouse_buttons |= button_bit;
        move_mouse(current_mouse_buttons, 0, 0, 0, 0);
    }
    if (do_release) {
        current_mouse_buttons &= ~button_bit;
        move_mouse(current_mouse_buttons, 0, 0, 0, 0);
    }

    cJSON_Delete(json);
    hid_api_success = true;
}

// Process POST /api/hid/mouse/scroll
static void process_hid_mouse_scroll(void)
{
    cJSON *json = cJSON_Parse(post_body);
    if (json == NULL) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid JSON");
        return;
    }

    int x = 0, y = 0;
    cjson_get_int(json, "x", &x);
    cjson_get_int(json, "y", &y);

    if (x < -127) x = -127;
    if (x > 127) x = 127;
    if (y < -127) y = -127;
    if (y > 127) y = 127;

    move_mouse(current_mouse_buttons, 0, 0, (int8_t)y, (int8_t)x);

    cJSON_Delete(json);
    hid_api_success = true;
}

// Process POST /api/hid/release
static void process_hid_release(void)
{
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] != 0) {
            depress_key(keycodes[i]);
        }
    }

    current_mouse_buttons = 0;
    move_mouse(0, 0, 0, 0, 0);

    hid_api_success = true;
}

//--------------------------------------------------------------------+
// lwIP httpd Integration
//--------------------------------------------------------------------+

// Custom file open handler - routes API requests
int fs_open_custom(struct fs_file *file, const char *name)
{
    // Device/Config APIs
    if (strcmp(name, "/api/status") == 0) return handle_api_status(file);
    if (strcmp(name, "/api/reboot/result") == 0) return handle_api_reboot_result(file);
    if (strcmp(name, "/api/reboot-ap/result") == 0) return handle_api_reboot_ap_result(file);
    if (strcmp(name, "/api/config") == 0) return handle_api_config_get(file);
    if (strcmp(name, "/api/config/result") == 0) return handle_api_config_post_result(file);
    if (strcmp(name, "/api/networks") == 0) return handle_api_networks(file);
    if (strcmp(name, "/api/scan/result") == 0) return handle_api_scan_result(file);
    if (strcmp(name, "/api/settings") == 0) return handle_api_settings_get(file);
    if (strcmp(name, "/api/settings/result") == 0) return handle_api_settings_result(file);

    // HID API
    if (strcmp(name, "/api/hid/result") == 0) return handle_api_hid_result(file);

    // Not a custom file, let httpd try the embedded filesystem
    return 0;
}

// Custom file close handler
void fs_close_custom(struct fs_file *file)
{
    (void)file;
}

// Called when POST request starts
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    (void)connection;
    (void)http_request;
    (void)http_request_len;
    (void)post_auto_wnd;

    printf("POST begin: %s (content_len=%d)\r\n", uri, content_len);

    // Check if this is an accepted POST endpoint
    bool is_hid_endpoint = (
        strcmp(uri, "/api/hid/key") == 0 ||
        strcmp(uri, "/api/hid/mouse/move") == 0 ||
        strcmp(uri, "/api/hid/mouse/button") == 0 ||
        strcmp(uri, "/api/hid/mouse/scroll") == 0 ||
        strcmp(uri, "/api/hid/release") == 0
    );

    bool is_config_endpoint = (
        strcmp(uri, "/api/config") == 0 ||
        strcmp(uri, "/api/scan") == 0 ||
        strcmp(uri, "/api/settings") == 0 ||
        strcmp(uri, "/api/reboot") == 0 ||
        strcmp(uri, "/api/reboot-ap") == 0
    );

    if (!is_hid_endpoint && !is_config_endpoint) {
        return ERR_VAL;
    }

    // For /api/scan, /api/reboot, /api/reboot-ap we don't need a body
    if (strcmp(uri, "/api/scan") == 0 ||
        strcmp(uri, "/api/reboot") == 0 ||
        strcmp(uri, "/api/reboot-ap") == 0) {
        strncpy(post_uri, uri, sizeof(post_uri) - 1);
        post_uri[sizeof(post_uri) - 1] = '\0';
        return ERR_OK;
    }

    // For /api/hid/release, body is optional
    if (strcmp(uri, "/api/hid/release") == 0) {
        strncpy(post_uri, uri, sizeof(post_uri) - 1);
        post_uri[sizeof(post_uri) - 1] = '\0';
        hid_api_success = false;
        hid_api_error[0] = '\0';
        post_body_len = 0;
        return ERR_OK;
    }

    // Validate content length
    if (content_len <= 0 || content_len >= (int)sizeof(post_body)) {
        printf("POST content length invalid: %d\r\n", content_len);
        return ERR_VAL;
    }

    // Store URI and reset state
    strncpy(post_uri, uri, sizeof(post_uri) - 1);
    post_uri[sizeof(post_uri) - 1] = '\0';
    post_body_len = 0;
    post_config_success = false;
    hid_api_success = false;
    hid_api_error[0] = '\0';
    settings_api_success = false;
    settings_api_error[0] = '\0';

    return ERR_OK;
}

// Called with POST body data (may be called multiple times)
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    (void)connection;

    if (p == NULL) {
        return ERR_OK;
    }

    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        size_t copy_len = q->len;
        if (post_body_len + copy_len >= sizeof(post_body)) {
            copy_len = sizeof(post_body) - post_body_len - 1;
        }
        if (copy_len > 0) {
            memcpy(post_body + post_body_len, q->payload, copy_len);
            post_body_len += copy_len;
        }
    }

    post_body[post_body_len] = '\0';
    pbuf_free(p);

    return ERR_OK;
}

// Called when POST is complete - process the data and return response URI
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    (void)connection;

    printf("POST finished: uri=%s body=%s\r\n", post_uri, post_body);

    // Handle /api/scan
    if (strcmp(post_uri, "/api/scan") == 0) {
        scan_triggered = (wifi_scan_start() == 0);
        snprintf(response_uri, response_uri_len, "/api/scan/result");
        return;
    }

    // Handle /api/reboot
    if (strcmp(post_uri, "/api/reboot") == 0) {
        printf("API: reboot requested\r\n");
        snprintf(response_uri, response_uri_len, "/api/reboot/result");
        return;
    }

    // Handle /api/reboot-ap
    if (strcmp(post_uri, "/api/reboot-ap") == 0) {
        printf("API: reboot-ap requested\r\n");
        snprintf(response_uri, response_uri_len, "/api/reboot-ap/result");
        return;
    }

    // Handle /api/settings
    if (strcmp(post_uri, "/api/settings") == 0) {
        process_settings_post();
        snprintf(response_uri, response_uri_len, "/api/settings/result");
        return;
    }

    // Handle /api/config
    if (strcmp(post_uri, "/api/config") == 0) {
        cJSON *json = cJSON_Parse(post_body);
        if (json == NULL) {
            printf("POST /api/config: failed to parse JSON\r\n");
            snprintf(response_uri, response_uri_len, "/api/config/result");
            return;
        }

        const char *ssid = cjson_get_string(json, "ssid");
        const char *pass = cjson_get_string(json, "password");

        if (ssid && pass && strlen(ssid) > 0 && strlen(ssid) <= WIFI_SSID_MAX_LEN &&
            strlen(pass) <= WIFI_PASSWORD_MAX_LEN) {

            if (wifi_credentials_set(ssid, pass)) {
                post_config_success = true;
                printf("POST /api/config: credentials saved\r\n");
            } else {
                printf("POST /api/config: failed to save credentials\r\n");
            }
        } else {
            printf("POST /api/config: invalid JSON fields\r\n");
        }

        cJSON_Delete(json);
        snprintf(response_uri, response_uri_len, "/api/config/result");
        return;
    }

    // Handle HID API endpoints
    if (strncmp(post_uri, "/api/hid/", 9) == 0) {
        if (strcmp(post_uri, "/api/hid/key") == 0) {
            process_hid_key();
        } else if (strcmp(post_uri, "/api/hid/mouse/move") == 0) {
            process_hid_mouse_move();
        } else if (strcmp(post_uri, "/api/hid/mouse/button") == 0) {
            process_hid_mouse_button();
        } else if (strcmp(post_uri, "/api/hid/mouse/scroll") == 0) {
            process_hid_mouse_scroll();
        } else if (strcmp(post_uri, "/api/hid/release") == 0) {
            process_hid_release();
        }
        snprintf(response_uri, response_uri_len, "/api/hid/result");
        return;
    }

    // Default fallback
    snprintf(response_uri, response_uri_len, "/api/config/result");
}

//--------------------------------------------------------------------+
// Init
//--------------------------------------------------------------------+

void nethid_httpd_init(void)
{
    printf("Starting HTTP server\r\n");
    last_uptime_update = to_ms_since_boot(get_absolute_time());
    httpd_init();
    printf("HTTP server started on port 80\r\n");
}
