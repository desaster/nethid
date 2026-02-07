/*
 * Custom HTTP server for NetHID
 *
 * Replaces lwIP's built-in httpd with a purpose-built server using raw TCP.
 * Handles API endpoints, static file serving, and WebSocket upgrade on a
 * single port.
 */

#ifndef NETHID_HTTPD_SERVER_H
#define NETHID_HTTPD_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lwip/tcp.h"

// Forward declaration
typedef struct cJSON cJSON;

//
// Configuration
//

#define HTTP_MAX_CONNECTIONS  6
#define HTTP_RECV_BUF_SIZE   2048
#define HTTP_SEND_BUF_SIZE   1024
#define HTTP_POLL_INTERVAL   5      // ~2.5s per tick
#define HTTP_TIMEOUT_TICKS   4      // ~10s idle timeout

#define WS_RECV_BUF_SIZE     512
#define WS_FRAME_BUF_SIZE    256

//
// HTTP methods
//

#define HTTP_GET   0
#define HTTP_POST  1

//
// Connection
//

typedef enum {
    CONN_RECV_HEADERS,
    CONN_RECV_BODY,
    CONN_SENDING_RESPONSE,
    CONN_SENDING_FILE,
    CONN_WEBSOCKET,
} conn_state_t;

typedef struct connection connection_t;

struct connection {
    bool in_use;
    struct tcp_pcb *pcb;
    conn_state_t state;
    uint8_t poll_ticks;   // idle timeout counter
    uint16_t headers_len;

    // Receive buffer - holds HTTP headers + body
    uint8_t recv_buf[HTTP_RECV_BUF_SIZE];
    size_t recv_len;

    // Parsed request
    struct {
        uint8_t method;
        const char *uri;
        const char *query;
        const char *body;
        uint16_t body_len;
        uint16_t content_length;
        const char *auth_header;
        bool websocket_upgrade;
        const char *ws_key;       // Sec-WebSocket-Key value
    } req;

    // Send buffer for dynamic responses
    char send_buf[HTTP_SEND_BUF_SIZE];
    uint16_t send_len;

    // Static file streaming state
    const uint8_t *file_data;
    uint32_t file_remaining;

    // WebSocket state
    // TODO: move to a separate static buffer, only one connection uses this
    uint8_t ws_recv_buf[WS_RECV_BUF_SIZE];
    size_t ws_recv_len;
    uint8_t ws_mouse_buttons;
};

//
// Route table
//

typedef void (*http_handler_fn)(connection_t *conn);

typedef struct {
    uint8_t method;
    const char *uri;
    bool prefix_match;
    http_handler_fn handler;
} http_route_t;

//
// Response helpers
//

void http_send_json(connection_t *conn, int status, const char *json_body);
void http_send_cjson(connection_t *conn, int status, cJSON *json);
void http_send_error(connection_t *conn, int status, const char *message);

//
// Connection management
//

void conn_close(connection_t *conn);

//
// Public API
//

void httpd_server_init(uint16_t port);

//
// WebSocket API
//

bool httpd_websocket_upgrade(connection_t *conn);
void httpd_websocket_recv(connection_t *conn, struct pbuf *p);
void httpd_websocket_closed(connection_t *conn);
bool websocket_client_connected(void);
void websocket_release_all(void);
void websocket_send_status(void);

#endif // NETHID_HTTPD_SERVER_H
