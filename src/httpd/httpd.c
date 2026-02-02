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

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

// Response buffer for dynamic API responses
static char api_response[512];

// Uptime counter
static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

// POST request handling
static char post_uri[64];
static char post_body[256];
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

// Escape a string for JSON (handles quotes and backslashes)
static void escape_json_string(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

// Find string value in JSON: {"key":"value"} -> returns pointer to value, sets len
static const char *json_find_string(const char *json, const char *key, size_t *value_len)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);

    const char *start = strstr(json, search_key);
    if (!start) {
        return NULL;
    }

    start += strlen(search_key);
    const char *end = strchr(start, '"');
    if (!end) {
        return NULL;
    }

    *value_len = end - start;
    return start;
}

// Find integer value in JSON: {"key":123}
static bool json_find_int(const char *json, const char *key, int *value)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char *start = strstr(json, search_key);
    if (!start) {
        return false;
    }

    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;

    char *end;
    long val = strtol(start, &end, 10);
    if (end == start) {
        return false;
    }

    *value = (int)val;
    return true;
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

    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"hostname\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"ip\":\"%s\",\"uptime\":%lu,\"mode\":\"%s\"}",
        hostname,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        ip_str,
        (unsigned long)uptime_seconds,
        in_ap_mode ? "ap" : "sta");

    api_json_response(file, body, body_len);
    return 1;
}

// POST /api/reboot-ap result
static int handle_api_reboot_ap_result(struct fs_file *file)
{
    char body[] = "{\"status\":\"rebooting to AP mode\"}";
    api_json_response(file, body, strlen(body));
    settings_set_force_ap();
    watchdog_enable(100, false);
    return 1;
}

// POST /api/reboot result
static int handle_api_reboot_result(struct fs_file *file)
{
    char body[] = "{\"status\":\"rebooting\"}";
    api_json_response(file, body, strlen(body));
    watchdog_enable(100, false);
    return 1;
}

// GET /api/config - stored WiFi config (SSID only)
static int handle_api_config_get(struct fs_file *file)
{
    char body[128];
    int body_len;

    char ssid[WIFI_SSID_MAX_LEN + 1];
    if (wifi_credentials_get_ssid(ssid)) {
        body_len = snprintf(body, sizeof(body),
            "{\"configured\":true,\"ssid\":\"%s\"}", ssid);
    } else {
        body_len = snprintf(body, sizeof(body),
            "{\"configured\":false,\"ssid\":\"\"}");
    }

    api_json_response(file, body, body_len);
    return 1;
}

// POST /api/config result
static int handle_api_config_post_result(struct fs_file *file)
{
    char body[64];
    int body_len;

    if (post_config_success) {
        body_len = snprintf(body, sizeof(body),
            "{\"status\":\"saved\",\"rebooting\":true}");
    } else {
        body_len = snprintf(body, sizeof(body),
            "{\"status\":\"error\",\"message\":\"invalid request\"}");
    }

    api_json_response(file, body, body_len);

    if (post_config_success) {
        watchdog_enable(100, false);
    }
    return 1;
}

// GET /api/networks - scanned WiFi networks
static int handle_api_networks(struct fs_file *file)
{
    const wifi_scan_state_t *scan = wifi_scan_get_results();

    char body[400];
    int pos = 0;

    pos += snprintf(body + pos, sizeof(body) - pos,
        "{\"scanning\":%s,\"networks\":[",
        scan->scanning ? "true" : "false");

    for (int i = 0; i < scan->count && pos < (int)sizeof(body) - 80; i++) {
        if (i > 0) {
            body[pos++] = ',';
        }

        char escaped_ssid[65];
        escape_json_string(scan->networks[i].ssid, escaped_ssid, sizeof(escaped_ssid));

        pos += snprintf(body + pos, sizeof(body) - pos,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"ch\":%d}",
            escaped_ssid,
            scan->networks[i].rssi,
            auth_mode_to_string(scan->networks[i].auth_mode),
            scan->networks[i].channel);
    }

    pos += snprintf(body + pos, sizeof(body) - pos, "]}");

    api_json_response(file, body, pos);
    return 1;
}

// POST /api/scan result
static int handle_api_scan_result(struct fs_file *file)
{
    char body[64];
    int body_len;

    if (scan_triggered) {
        body_len = snprintf(body, sizeof(body), "{\"status\":\"scanning\"}");
    } else {
        body_len = snprintf(body, sizeof(body), "{\"status\":\"error\",\"message\":\"scan failed\"}");
    }

    api_json_response(file, body, body_len);
    return 1;
}

//--------------------------------------------------------------------+
// Settings API Handlers
//--------------------------------------------------------------------+

// GET /api/settings - device settings
static int handle_api_settings_get(struct fs_file *file)
{
    char hostname[HOSTNAME_MAX_LEN + 1];
    bool is_default = !settings_get_hostname(hostname);

    char escaped_hostname[HOSTNAME_MAX_LEN * 2 + 1];
    escape_json_string(hostname, escaped_hostname, sizeof(escaped_hostname));

    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"hostname\":{\"value\":\"%s\",\"default\":%s}}",
        escaped_hostname,
        is_default ? "true" : "false");

    api_json_response(file, body, body_len);
    return 1;
}

// POST /api/settings result
static int handle_api_settings_result(struct fs_file *file)
{
    char body[128];
    int body_len;

    if (settings_api_success) {
        body_len = snprintf(body, sizeof(body), "{\"success\":true}");
    } else {
        char escaped_error[64];
        escape_json_string(settings_api_error, escaped_error, sizeof(escaped_error));
        body_len = snprintf(body, sizeof(body),
            "{\"success\":false,\"error\":\"%s\"}", escaped_error);
    }

    api_json_response(file, body, body_len);
    return 1;
}

// Process POST /api/settings
static void process_settings_post(void)
{
    size_t hostname_len = 0;
    const char *hostname = json_find_string(post_body, "hostname", &hostname_len);

    if (hostname && hostname_len > 0) {
        if (hostname_len > HOSTNAME_MAX_LEN) {
            snprintf(settings_api_error, sizeof(settings_api_error), "Hostname too long");
            return;
        }

        char hostname_buf[HOSTNAME_MAX_LEN + 1];
        memcpy(hostname_buf, hostname, hostname_len);
        hostname_buf[hostname_len] = '\0';

        if (!settings_set_hostname(hostname_buf)) {
            snprintf(settings_api_error, sizeof(settings_api_error), "Invalid hostname format");
            return;
        }
    }

    settings_api_success = true;
}

//--------------------------------------------------------------------+
// HID API Handlers
//--------------------------------------------------------------------+

// POST /api/hid/* result
static int handle_api_hid_result(struct fs_file *file)
{
    char body[128];
    int body_len;

    if (hid_api_success) {
        body_len = snprintf(body, sizeof(body), "{\"success\":true}");
    } else {
        char escaped_error[64];
        escape_json_string(hid_api_error, escaped_error, sizeof(escaped_error));
        body_len = snprintf(body, sizeof(body),
            "{\"success\":false,\"error\":\"%s\"}", escaped_error);
    }

    api_json_response(file, body, body_len);
    return 1;
}

// Process POST /api/hid/key
static void process_hid_key(void)
{
    size_t action_len = 0;
    const char *action_str = json_find_string(post_body, "action", &action_len);

    int hid_code;
    if (!json_find_int(post_body, "code", &hid_code) || hid_code < 0 || hid_code > 255) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid or missing code");
        return;
    }

    bool do_press = true;
    bool do_release = true;

    if (action_str && action_len > 0) {
        if (strncmp(action_str, "press", action_len) == 0) {
            do_release = false;
        } else if (strncmp(action_str, "release", action_len) == 0) {
            do_press = false;
        } else if (strncmp(action_str, "tap", action_len) != 0) {
            snprintf(hid_api_error, sizeof(hid_api_error), "Invalid action");
            return;
        }
    }

    if (do_press) press_key(hid_code);
    if (do_release) depress_key(hid_code);

    hid_api_success = true;
}

// Process POST /api/hid/mouse/move
static void process_hid_mouse_move(void)
{
    int dx = 0, dy = 0;

    json_find_int(post_body, "dx", &dx);
    json_find_int(post_body, "dy", &dy);

    if (dx < -127) dx = -127;
    if (dx > 127) dx = 127;
    if (dy < -127) dy = -127;
    if (dy > 127) dy = 127;

    move_mouse(current_mouse_buttons, (int8_t)dx, (int8_t)dy, 0, 0);
    hid_api_success = true;
}

// Process POST /api/hid/mouse/button
static void process_hid_mouse_button(void)
{
    size_t action_len = 0;
    const char *action_str = json_find_string(post_body, "action", &action_len);

    int button_bit;
    if (!json_find_int(post_body, "button", &button_bit) || button_bit < 1 || button_bit > 31) {
        snprintf(hid_api_error, sizeof(hid_api_error), "Invalid or missing button");
        return;
    }

    bool do_press = true;
    bool do_release = true;

    if (action_str && action_len > 0) {
        if (strncmp(action_str, "press", action_len) == 0) {
            do_release = false;
        } else if (strncmp(action_str, "release", action_len) == 0) {
            do_press = false;
        } else if (strncmp(action_str, "click", action_len) != 0) {
            snprintf(hid_api_error, sizeof(hid_api_error), "Invalid action");
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

    hid_api_success = true;
}

// Process POST /api/hid/mouse/scroll
static void process_hid_mouse_scroll(void)
{
    int x = 0, y = 0;

    json_find_int(post_body, "x", &x);
    json_find_int(post_body, "y", &y);

    if (x < -127) x = -127;
    if (x > 127) x = 127;
    if (y < -127) y = -127;
    if (y > 127) y = 127;

    move_mouse(current_mouse_buttons, 0, 0, (int8_t)y, (int8_t)x);
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
        size_t ssid_len = 0, pass_len = 0;
        const char *ssid = json_find_string(post_body, "ssid", &ssid_len);
        const char *pass = json_find_string(post_body, "password", &pass_len);

        if (ssid && pass && ssid_len > 0 && ssid_len <= WIFI_SSID_MAX_LEN &&
            pass_len <= WIFI_PASSWORD_MAX_LEN) {

            char ssid_buf[WIFI_SSID_MAX_LEN + 1];
            char pass_buf[WIFI_PASSWORD_MAX_LEN + 1];

            memcpy(ssid_buf, ssid, ssid_len);
            ssid_buf[ssid_len] = '\0';

            memcpy(pass_buf, pass, pass_len);
            pass_buf[pass_len] = '\0';

            if (wifi_credentials_set(ssid_buf, pass_buf)) {
                post_config_success = true;
                printf("POST /api/config: credentials saved\r\n");
            } else {
                printf("POST /api/config: failed to save credentials\r\n");
            }
        } else {
            printf("POST /api/config: invalid JSON (ssid=%p/%zu, pass=%p/%zu)\r\n",
                   ssid, ssid_len, pass, pass_len);
        }
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
