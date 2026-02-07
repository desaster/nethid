/*
 * HTTP API endpoint handlers for NetHID
 *
 * All handlers receive a fully parsed request via connection_t and
 * send their response directly using http_send_* helpers.
 */

#include "httpd_handlers.h"
#include "httpd_server.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include "board.h"
#include "settings.h"
#include "ap_mode.h"
#include "wifi_scan.h"
#include "usb.h"
#include "hid_keys.h"
#include "cjson/cJSON.h"

//
// Utility
//

static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

static void update_uptime(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_uptime_update >= 1000) {
        uptime_seconds += (now - last_uptime_update) / 1000;
        last_uptime_update = now - (now - last_uptime_update) % 1000;
    }
}

static const char *auth_mode_to_string(uint8_t auth_mode)
{
    if (auth_mode == 0) return "Open";
    if (auth_mode & 0x04) return "WPA2";
    if (auth_mode & 0x02) return "WPA";
    return "Secured";
}

// Helper to get string from cJSON object
static const char *cjson_get_string(cJSON *json, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

// Helper to get int from cJSON object
static bool cjson_get_int(cJSON *json, const char *key, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsNumber(item)) {
        *value = item->valueint;
        return true;
    }
    return false;
}

// Helper to get bool from cJSON object
static bool cjson_get_bool(cJSON *json, const char *key, bool *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item);
        return true;
    }
    return false;
}

// HID mouse button state for HTTP API
static uint8_t current_mouse_buttons = 0;
extern uint8_t keycodes[6];  // from usb.c

//
// Device/Config API Handlers
//

static void handle_api_status(connection_t *conn)
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

    http_send_cjson(conn, 200, json);
}

static void handle_api_config_get(connection_t *conn)
{
    char ssid[WIFI_SSID_MAX_LEN + 1];
    bool configured = wifi_credentials_get_ssid(ssid);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "configured", configured);
    cJSON_AddStringToObject(json, "ssid", configured ? ssid : "");

    http_send_cjson(conn, 200, json);
}

static void handle_api_config_post(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "invalid JSON");
        return;
    }

    const char *ssid = cjson_get_string(json, "ssid");
    const char *pass = cjson_get_string(json, "password");

    if (ssid && pass && strlen(ssid) > 0 && strlen(ssid) <= WIFI_SSID_MAX_LEN &&
        strlen(pass) <= WIFI_PASSWORD_MAX_LEN) {

        if (wifi_credentials_set(ssid, pass)) {
            cJSON_Delete(json);
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "status", "saved");
            cJSON_AddBoolToObject(resp, "rebooting", true);
            http_send_cjson(conn, 200, resp);
            request_reboot();
            return;
        }
    }

    cJSON_Delete(json);
    http_send_error(conn, 400, "invalid request");
}

static void handle_api_networks(connection_t *conn)
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

    http_send_cjson(conn, 200, json);
}

static void handle_api_scan(connection_t *conn)
{
    cJSON *json = cJSON_CreateObject();

    if (wifi_scan_start() == 0) {
        cJSON_AddStringToObject(json, "status", "scanning");
    } else {
        cJSON_AddStringToObject(json, "status", "error");
        cJSON_AddStringToObject(json, "message", "scan failed");
    }

    http_send_cjson(conn, 200, json);
}

static void handle_api_reboot(connection_t *conn)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "rebooting");
    http_send_cjson(conn, 200, json);
    request_reboot();
}

static void handle_api_reboot_ap(connection_t *conn)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "rebooting to AP mode");
    http_send_cjson(conn, 200, json);
    settings_set_force_ap();
    request_reboot();
}

//
// Settings API Handlers
//

static void handle_api_settings_get(connection_t *conn)
{
    char hostname_buf[HOSTNAME_MAX_LEN + 1];
    bool hostname_is_default = !settings_get_hostname(hostname_buf);

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

    char syslog_server[SYSLOG_SERVER_MAX_LEN + 1];
    bool has_syslog = settings_get_syslog_server(syslog_server);
    uint16_t syslog_port_val = settings_get_syslog_port();

    cJSON *json = cJSON_CreateObject();

    cJSON *hostname_obj = cJSON_AddObjectToObject(json, "hostname");
    cJSON_AddStringToObject(hostname_obj, "value", hostname_buf);
    cJSON_AddBoolToObject(hostname_obj, "default", hostname_is_default);

    cJSON_AddBoolToObject(json, "mqtt_enabled", mqtt_enabled);
    cJSON_AddStringToObject(json, "mqtt_broker", has_broker ? mqtt_broker : "");
    cJSON_AddNumberToObject(json, "mqtt_port", mqtt_port);
    cJSON_AddStringToObject(json, "mqtt_topic", has_topic ? mqtt_topic : "");
    cJSON_AddStringToObject(json, "mqtt_username", has_username ? mqtt_username : "");
    cJSON_AddBoolToObject(json, "mqtt_has_password", mqtt_has_pass);
    cJSON_AddStringToObject(json, "mqtt_client_id", mqtt_client_id);

    cJSON_AddStringToObject(json, "syslog_server", has_syslog ? syslog_server : "");
    cJSON_AddNumberToObject(json, "syslog_port", syslog_port_val);

    http_send_cjson(conn, 200, json);
}

static void handle_api_settings_post(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "Invalid JSON");
        return;
    }

    // Hostname
    const char *hostname = cjson_get_string(json, "hostname");
    if (hostname && strlen(hostname) > 0) {
        if (strlen(hostname) > HOSTNAME_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "Hostname too long");
            return;
        }
        if (!settings_set_hostname(hostname)) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "Invalid hostname format");
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
            cJSON_Delete(json);
            http_send_error(conn, 400, "Invalid MQTT port");
            return;
        }
    }

    // MQTT broker
    const char *broker = cjson_get_string(json, "mqtt_broker");
    if (broker) {
        if (strlen(broker) > MQTT_BROKER_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "MQTT broker too long");
            return;
        }
        settings_set_mqtt_broker(broker);
    }

    // MQTT topic
    const char *topic = cjson_get_string(json, "mqtt_topic");
    if (topic) {
        if (strlen(topic) > MQTT_TOPIC_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "MQTT topic too long");
            return;
        }
        settings_set_mqtt_topic(topic);
    }

    // MQTT username
    const char *username = cjson_get_string(json, "mqtt_username");
    if (username) {
        if (strlen(username) > MQTT_USERNAME_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "MQTT username too long");
            return;
        }
        settings_set_mqtt_username(username);
    }

    // MQTT password
    const char *password = cjson_get_string(json, "mqtt_password");
    if (password) {
        if (strlen(password) > MQTT_PASSWORD_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "MQTT password too long");
            return;
        }
        settings_set_mqtt_password(password);
    }

    // MQTT client ID
    const char *client_id = cjson_get_string(json, "mqtt_client_id");
    if (client_id) {
        if (strlen(client_id) > MQTT_CLIENT_ID_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "MQTT client ID too long");
            return;
        }
        settings_set_mqtt_client_id(client_id);
    }

    // Syslog server
    const char *syslog_server = cjson_get_string(json, "syslog_server");
    if (syslog_server) {
        if (strlen(syslog_server) > SYSLOG_SERVER_MAX_LEN) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "Syslog server too long");
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
            cJSON_Delete(json);
            http_send_error(conn, 400, "Invalid syslog port");
            return;
        }
    }

    cJSON_Delete(json);
    http_send_json(conn, 200, "{\"success\":true}");
}

//
// HID API Handlers
//

static void handle_api_hid_key(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "Invalid JSON");
        return;
    }

    const char *key_name = cjson_get_string(json, "key");
    if (key_name == NULL) {
        cJSON_Delete(json);
        http_send_error(conn, 400, "Missing key field");
        return;
    }

    hid_key_info_t key_info;
    if (!hid_lookup_key(key_name, &key_info)) {
        cJSON_Delete(json);
        char msg[64];
        snprintf(msg, sizeof(msg), "Unknown key: %.40s", key_name);
        http_send_error(conn, 400, msg);
        return;
    }

    // Optional type override
    const char *type_str = cjson_get_string(json, "type");
    if (type_str != NULL) {
        if (strcmp(type_str, "consumer") == 0) {
            key_info.type = HID_KEY_TYPE_CONSUMER;
        } else if (strcmp(type_str, "system") == 0) {
            key_info.type = HID_KEY_TYPE_SYSTEM;
        } else if (strcmp(type_str, "keyboard") != 0) {
            cJSON_Delete(json);
            http_send_error(conn, 400, "Invalid type");
            return;
        }
    }

    hid_action_t action;
    if (!hid_parse_action(cjson_get_string(json, "action"), &action)) {
        cJSON_Delete(json);
        http_send_error(conn, 400, "Invalid action");
        return;
    }

    if (!hid_execute_key(&key_info, action)) {
        cJSON_Delete(json);
        http_send_error(conn, 400, "System keys not yet implemented");
        return;
    }

    cJSON_Delete(json);
    http_send_json(conn, 200, "{\"success\":true}");
}

static void handle_api_hid_mouse_move(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "Invalid JSON");
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
    http_send_json(conn, 200, "{\"success\":true}");
}

static void handle_api_hid_mouse_button(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "Invalid JSON");
        return;
    }

    int button_bit;
    if (!cjson_get_int(json, "button", &button_bit) || button_bit < 1 || button_bit > 31) {
        cJSON_Delete(json);
        http_send_error(conn, 400, "Invalid or missing button");
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
            cJSON_Delete(json);
            http_send_error(conn, 400, "Invalid action");
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
    http_send_json(conn, 200, "{\"success\":true}");
}

static void handle_api_hid_mouse_scroll(connection_t *conn)
{
    cJSON *json = cJSON_Parse(conn->req.body);
    if (json == NULL) {
        http_send_error(conn, 400, "Invalid JSON");
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
    http_send_json(conn, 200, "{\"success\":true}");
}

static void handle_api_hid_release(connection_t *conn)
{
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] != 0) {
            depress_key(keycodes[i]);
        }
    }

    current_mouse_buttons = 0;
    move_mouse(0, 0, 0, 0, 0);

    http_send_json(conn, 200, "{\"success\":true}");
}

//
// Route table
//

static const http_route_t routes[] = {
    { HTTP_GET,  "/api/status",            false, handle_api_status },
    { HTTP_GET,  "/api/config",            false, handle_api_config_get },
    { HTTP_GET,  "/api/networks",          false, handle_api_networks },
    { HTTP_GET,  "/api/settings",          false, handle_api_settings_get },
    { HTTP_POST, "/api/config",            false, handle_api_config_post },
    { HTTP_POST, "/api/settings",          false, handle_api_settings_post },
    { HTTP_POST, "/api/scan",              false, handle_api_scan },
    { HTTP_POST, "/api/reboot",            false, handle_api_reboot },
    { HTTP_POST, "/api/reboot-ap",         false, handle_api_reboot_ap },
    { HTTP_POST, "/api/hid/key",           false, handle_api_hid_key },
    { HTTP_POST, "/api/hid/mouse/move",    false, handle_api_hid_mouse_move },
    { HTTP_POST, "/api/hid/mouse/button",  false, handle_api_hid_mouse_button },
    { HTTP_POST, "/api/hid/mouse/scroll",  false, handle_api_hid_mouse_scroll },
    { HTTP_POST, "/api/hid/release",       false, handle_api_hid_release },
};

const http_route_t *httpd_get_routes(void)
{
    return routes;
}

int httpd_get_route_count(void)
{
    return sizeof(routes) / sizeof(routes[0]);
}
