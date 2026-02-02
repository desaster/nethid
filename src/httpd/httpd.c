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
#include "ap_mode.h"

// Uptime counter (seconds since boot)
static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

// Buffer for dynamic API responses (header + body)
static char api_response[512];

// POST request handling state
static char post_uri[32];
static char post_body[256];
static size_t post_body_len = 0;
static bool post_config_success = false;

// Update uptime counter
static void update_uptime(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_uptime_update >= 1000) {
        uptime_seconds += (now - last_uptime_update) / 1000;
        last_uptime_update = now - (now - last_uptime_update) % 1000;
    }
}

// Handle /api/status - returns device info as JSON
static int handle_api_status(struct fs_file *file)
{
    update_uptime();

    uint8_t mac[6];
    int itf = in_ap_mode ? CYW43_ITF_AP : CYW43_ITF_STA;
    cyw43_wifi_get_mac(&cyw43_state, itf, mac);

    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(ip));

    char hostname[32];
    snprintf(hostname, sizeof(hostname), "picow-%02x%02x%02x", mac[3], mac[4], mac[5]);

    // Build JSON body first
    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"hostname\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"ip\":\"%s\",\"uptime\":%lu,\"mode\":\"%s\"}",
        hostname,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        ip_str,
        (unsigned long)uptime_seconds,
        in_ap_mode ? "ap" : "sta");

    // Build full response with HTTP headers
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

    return 1;
}

// Handle /api/reboot-ap - set force AP flag and reboot
static int handle_api_reboot_ap(struct fs_file *file)
{
    printf("API: reboot-ap requested\r\n");

    char body[] = "{\"status\":\"rebooting to AP mode\"}";
    int body_len = strlen(body);

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

    // Set flag and schedule reboot via watchdog (gives time for response)
    ap_mode_set_force_flag();
    watchdog_enable(100, false);

    return 1;
}

// Handle /api/reboot - simple reboot (back to normal mode)
static int handle_api_reboot(struct fs_file *file)
{
    printf("API: reboot requested\r\n");

    char body[] = "{\"status\":\"rebooting\"}";
    int body_len = strlen(body);

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

    // Schedule reboot via watchdog (gives time for response)
    watchdog_enable(100, false);

    return 1;
}

// Handle GET /api/config - returns stored WiFi config (SSID only, no password)
static int handle_api_config_get(struct fs_file *file)
{
    char body[128];
    int body_len;

    char ssid[WIFI_SSID_MAX_LEN + 1];
    if (wifi_credentials_get_ssid(ssid)) {
        body_len = snprintf(body, sizeof(body),
            "{\"configured\":true,\"ssid\":\"%s\"}",
            ssid);
    } else {
        body_len = snprintf(body, sizeof(body),
            "{\"configured\":false,\"ssid\":\"\"}");
    }

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

    return 1;
}

// Handle POST /api/config result - called via fs_open_custom after POST completes
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

    // If config was saved successfully, reboot to apply
    if (post_config_success) {
        watchdog_enable(100, false);
    }

    return 1;
}

// Custom file open handler - intercepts API requests
int fs_open_custom(struct fs_file *file, const char *name)
{
    // API endpoint: /api/status
    if (strcmp(name, "/api/status") == 0) {
        return handle_api_status(file);
    }

    // API endpoint: /api/reboot-ap - reboot into AP mode
    if (strcmp(name, "/api/reboot-ap") == 0) {
        return handle_api_reboot_ap(file);
    }

    // API endpoint: /api/reboot - simple reboot
    if (strcmp(name, "/api/reboot") == 0) {
        return handle_api_reboot(file);
    }

    // API endpoint: GET /api/config - get stored config
    if (strcmp(name, "/api/config") == 0) {
        return handle_api_config_get(file);
    }

    // POST result handler (called after httpd_post_finished returns this URI)
    if (strcmp(name, "/api/config/result") == 0) {
        return handle_api_config_post_result(file);
    }

    // Not a custom file, let httpd try the embedded filesystem
    return 0;
}

// Custom file close handler
void fs_close_custom(struct fs_file *file)
{
    // Nothing to free for our static buffer responses
    (void)file;
}

//--------------------------------------------------------------------+
// POST request handlers for lwIP httpd
//--------------------------------------------------------------------+

// Simple JSON parser - find value for a key in {"key":"value",...} format
// Returns pointer to start of value (after opening quote), or NULL if not found
// Sets value_len to length of value (excluding quotes)
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

    // Only accept POST to /api/config
    if (strcmp(uri, "/api/config") != 0) {
        return ERR_VAL;
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

    return ERR_OK;
}

// Called with POST body data (may be called multiple times)
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    (void)connection;

    if (p == NULL) {
        return ERR_OK;
    }

    // Copy data to buffer
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

    printf("POST finished: body=%s\r\n", post_body);

    // Parse JSON body: {"ssid":"...", "password":"..."}
    if (strcmp(post_uri, "/api/config") == 0) {
        size_t ssid_len = 0, pass_len = 0;
        const char *ssid = json_find_string(post_body, "ssid", &ssid_len);
        const char *pass = json_find_string(post_body, "password", &pass_len);

        if (ssid && pass && ssid_len > 0 && ssid_len <= WIFI_SSID_MAX_LEN &&
            pass_len <= WIFI_PASSWORD_MAX_LEN) {

            // Copy to null-terminated buffers
            char ssid_buf[WIFI_SSID_MAX_LEN + 1];
            char pass_buf[WIFI_PASSWORD_MAX_LEN + 1];

            memcpy(ssid_buf, ssid, ssid_len);
            ssid_buf[ssid_len] = '\0';

            memcpy(pass_buf, pass, pass_len);
            pass_buf[pass_len] = '\0';

            // Store credentials
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
    }

    // Return the URI that will serve the response
    snprintf(response_uri, response_uri_len, "/api/config/result");
}

void nethid_httpd_init(void)
{
    printf("Starting HTTP server\r\n");
    last_uptime_update = to_ms_since_boot(get_absolute_time());
    httpd_init();
    printf("HTTP server started on port 80\r\n");
}
