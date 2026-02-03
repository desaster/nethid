/*
 * Syslog remote logging for NetHID
 *
 * Sends log messages to a remote syslog server via UDP.
 * Also registers as a pico-sdk stdio driver to mirror all printf output.
 */

#ifndef __SYSLOG_H
#define __SYSLOG_H

#include <stdbool.h>

// Syslog severity levels (RFC 5424)
#define LOG_EMERG   0  // System is unusable
#define LOG_ALERT   1  // Action must be taken immediately
#define LOG_CRIT    2  // Critical conditions
#define LOG_ERR     3  // Error conditions
#define LOG_WARNING 4  // Warning conditions
#define LOG_NOTICE  5  // Normal but significant
#define LOG_INFO    6  // Informational
#define LOG_DEBUG   7  // Debug-level messages

// Syslog facility (using LOCAL0 = 16)
#define LOG_LOCAL0  (16 << 3)

// Initialize syslog UDP transport and stdio driver
// Call after network is up. Does nothing if syslog_server is not configured.
void syslog_init(void);

// Send a syslog message with explicit priority
// Use this for important messages where severity matters
void syslog_send(int priority, const char *fmt, ...);

// Convenience macros for explicit severity
#define SYSLOG_INFO(fmt, ...)    syslog_send(LOG_LOCAL0 | LOG_INFO, fmt, ##__VA_ARGS__)
#define SYSLOG_WARNING(fmt, ...) syslog_send(LOG_LOCAL0 | LOG_WARNING, fmt, ##__VA_ARGS__)
#define SYSLOG_ERR(fmt, ...)     syslog_send(LOG_LOCAL0 | LOG_ERR, fmt, ##__VA_ARGS__)

#endif
