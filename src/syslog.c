/*
 * Syslog remote logging implementation
 */

#include "syslog.h"
#include "settings.h"
#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

//--------------------------------------------------------------------+
// UDP Transport
//--------------------------------------------------------------------+

static struct udp_pcb *syslog_pcb = NULL;
static ip_addr_t syslog_addr;
static uint16_t syslog_port = 514;
static bool syslog_ready = false;
static char syslog_hostname[HOSTNAME_MAX_LEN + 1];

// Internal send function (does not use printf to avoid recursion)
static void syslog_send_raw(int priority, const char *message)
{
    if (!syslog_ready || syslog_pcb == NULL) {
        return;
    }

    // Format: <priority>hostname: message (RFC 3164 simplified)
    char packet[512];
    int len = snprintf(packet, sizeof(packet), "<%d>%s: %s",
                       priority, syslog_hostname, message);

    if (len <= 0 || len >= (int)sizeof(packet)) {
        return;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p != NULL) {
        memcpy(p->payload, packet, len);
        udp_sendto(syslog_pcb, p, &syslog_addr, syslog_port);
        pbuf_free(p);
    }
}

//--------------------------------------------------------------------+
// Stdio Driver (mirrors printf to syslog)
//--------------------------------------------------------------------+

#if SYSLOG_MIRROR_PRINTF

#define SYSLOG_LINE_BUFFER_SIZE 256

static char line_buffer[SYSLOG_LINE_BUFFER_SIZE];
static size_t line_pos = 0;

static void syslog_stdio_out_chars(const char *buf, int len)
{
    if (!syslog_ready) {
        return;  // Not yet initialized, skip silently
    }

    for (int i = 0; i < len; i++) {
        char c = buf[i];

        // On newline or buffer full, send the line
        if (c == '\n' || line_pos >= SYSLOG_LINE_BUFFER_SIZE - 1) {
            if (line_pos > 0) {
                line_buffer[line_pos] = '\0';
                syslog_send_raw(LOG_LOCAL0 | LOG_DEBUG, line_buffer);
                line_pos = 0;
            }
            // Skip the newline character itself
        } else if (c != '\r') {
            // Accumulate non-newline, non-CR characters
            line_buffer[line_pos++] = c;
        }
    }
}

static stdio_driver_t syslog_stdio_driver = {
    .out_chars = syslog_stdio_out_chars,
    .in_chars = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = false,
#endif
};

#endif // SYSLOG_MIRROR_PRINTF

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void syslog_init(void)
{
    char server[SYSLOG_SERVER_MAX_LEN + 1];

    // Check if syslog is configured
    if (!settings_get_syslog_server(server) || server[0] == '\0') {
        printf("Syslog: Disabled (no server configured)\r\n");
        return;
    }

    // Get port
    syslog_port = settings_get_syslog_port();

    // Get hostname for syslog messages
    settings_get_hostname(syslog_hostname);

    // Parse IP address (IP only, no DNS)
    if (!ip4addr_aton(server, ip_2_ip4(&syslog_addr))) {
        printf("Syslog: Invalid IP address: %s\r\n", server);
        return;
    }

    // Create UDP PCB
    syslog_pcb = udp_new();
    if (syslog_pcb == NULL) {
        printf("Syslog: Failed to create UDP PCB\r\n");
        return;
    }

    // Mark as ready BEFORE registering stdio driver
    syslog_ready = true;

#if SYSLOG_MIRROR_PRINTF
    // Register stdio driver to mirror printf output
    stdio_set_driver_enabled(&syslog_stdio_driver, true);
    printf("Syslog: Enabled (mirroring printf), sending to %s:%d\r\n", server, syslog_port);
#else
    printf("Syslog: Enabled (explicit only), sending to %s:%d\r\n", server, syslog_port);
#endif
}

void syslog_send(int priority, const char *fmt, ...)
{
    if (!syslog_ready) {
        return;
    }

    char message[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    syslog_send_raw(priority, message);
}
