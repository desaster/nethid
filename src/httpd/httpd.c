/*
 * HTTP server module for NetHID
 *
 * Uses lwIP's built-in httpd with custom file handling for API endpoints.
 */

#include "httpd.h"

#include <string.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"

// Uptime counter (seconds since boot)
static uint32_t uptime_seconds = 0;
static uint32_t last_uptime_update = 0;

// Buffer for dynamic API responses (header + body)
static char api_response[512];

// Custom file handle for API responses
static struct fs_file api_file;

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
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);

    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(ip));

    char hostname[32];
    snprintf(hostname, sizeof(hostname), "picow-%02x%02x%02x", mac[3], mac[4], mac[5]);

    // Build JSON body first
    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"hostname\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"ip\":\"%s\",\"uptime\":%lu}",
        hostname,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        ip_str,
        (unsigned long)uptime_seconds);

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

// Custom file open handler - intercepts API requests
int fs_open_custom(struct fs_file *file, const char *name)
{
    // API endpoint: /api/status
    if (strcmp(name, "/api/status") == 0) {
        return handle_api_status(file);
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

void nethid_httpd_init(void)
{
    printf("Starting HTTP server\r\n");
    last_uptime_update = to_ms_since_boot(get_absolute_time());
    httpd_init();
    printf("HTTP server started on port 80\r\n");
}
