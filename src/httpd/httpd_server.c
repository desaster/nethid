/*
 * Custom HTTP server for NetHID
 *
 * TCP server using raw lwIP API. Handles connection pooling, HTTP request
 * parsing, route dispatch, response helpers, and static file serving.
 */

#include "httpd_server.h"
#include "httpd_handlers.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/apps/fs.h"

#include "auth.h"
#include "board.h"
#include "cjson/cJSON.h"

//
// Embedded filesystem
//

#include "fsdata.c"

//
// Connection pool
//

static connection_t connections[HTTP_MAX_CONNECTIONS];
static struct tcp_pcb *listen_pcb = NULL;

static connection_t *conn_alloc(void)
{
    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
        if (!connections[i].in_use) {
            memset(&connections[i], 0, sizeof(connection_t));
            connections[i].in_use = true;
            return &connections[i];
        }
    }
    return NULL;
}

void conn_close(connection_t *conn)
{
    if (conn == NULL) return;

    if (conn->pcb != NULL) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_poll(conn->pcb, NULL, 0);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }

    conn->in_use = false;
}

//
// Forward declarations
//

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void http_err(void *arg, err_t err);
static err_t http_poll(void *arg, struct tcp_pcb *tpcb);

static void parse_request(connection_t *conn);
static void dispatch_request(connection_t *conn);
static void serve_static_file(connection_t *conn, const char *uri);
static void send_file_chunk(connection_t *conn);

//
// HTTP status text
//

static const char *status_text(int status)
{
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

//
// Response helpers
//

void http_send_json(connection_t *conn, int status, const char *json_body)
{
    int body_len = strlen(json_body);
    int len = snprintf(conn->send_buf, HTTP_SEND_BUF_SIZE,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, status_text(status), body_len, json_body);

    if (len > 0 && len < HTTP_SEND_BUF_SIZE) {
        conn->send_len = len;
        conn->state = CONN_SENDING_RESPONSE;
        tcp_write(conn->pcb, conn->send_buf, len, TCP_WRITE_FLAG_COPY);
        tcp_output(conn->pcb);
    } else {
        conn_close(conn);
    }
}

void http_send_cjson(connection_t *conn, int status, cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (body) {
        http_send_json(conn, status, body);
        cJSON_free(body);
    } else {
        http_send_json(conn, 500, "{\"error\":\"json serialization failed\"}");
    }
}

void http_send_error(connection_t *conn, int status, const char *message)
{
    // Build a small JSON error in send_buf
    char body[256];
    // TODO: escape message for JSON safety
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
    http_send_json(conn, status, body);
}

//
// Static file serving
//

static const struct fsdata_file *find_file(const char *uri)
{
    const struct fsdata_file *f = FS_ROOT;
    while (f != NULL) {
        if (strcmp(uri, (const char *)f->name) == 0) {
            return f;
        }
        f = f->next;
    }
    return NULL;
}

static void serve_static_file(connection_t *conn, const char *uri)
{
    const struct fsdata_file *f = find_file(uri);

    // SPA fallback: non-API paths that don't match a file get /index.html
    if (f == NULL && strncmp(uri, "/api/", 5) != 0) {
        f = find_file("/index.html");
    }

    if (f == NULL) {
        http_send_error(conn, 404, "not found");
        return;
    }

    // f->data includes HTTP headers, f->len is total size
    conn->state = CONN_SENDING_FILE;
    conn->file_data = f->data;
    conn->file_remaining = f->len;

    send_file_chunk(conn);
}

static void send_file_chunk(connection_t *conn)
{
    if (conn->file_remaining == 0) {
        conn_close(conn);
        return;
    }

    // Write as much as the TCP send buffer can accept
    uint16_t send_len = tcp_sndbuf(conn->pcb);
    if (send_len == 0) {
        return;  // wait for sent callback
    }
    if (send_len > conn->file_remaining) {
        send_len = conn->file_remaining;
    }

    // No copy needed, fsdata is in flash
    err_t err = tcp_write(conn->pcb, conn->file_data, send_len, 0);
    if (err == ERR_OK) {
        conn->file_data += send_len;
        conn->file_remaining -= send_len;
        tcp_output(conn->pcb);
    } else if (err == ERR_MEM) {
        // TCP buffer full, retry on next sent() callback
    } else {
        conn_close(conn);
    }
}

//
// Auth
//

// Extract "token" value from query string like "token=abc123&foo=bar"
static const char *query_get_token(const char *query)
{
    if (query == NULL) return NULL;

    const char *p = query;
    while (*p) {
        if (strncmp(p, "token=", 6) == 0) {
            return p + 6;
        }
        // Skip to next parameter
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return NULL;
}

// Check if the request carries a valid auth token
static bool request_is_authenticated(connection_t *conn)
{
    // Auth disabled or AP mode — allow everything
    if (!auth_is_enabled() || in_ap_mode) return true;

    // Check Authorization: Bearer <token> header
    const char *auth = conn->req.auth_header;
    if (auth && strncmp(auth, "Bearer ", 7) == 0) {
        if (auth_validate_token(auth + 7)) return true;
    }

    // Check ?token=<token> query parameter
    const char *token = query_get_token(conn->req.query);
    if (token && auth_validate_token(token)) return true;

    return false;
}

//
// Route dispatch
//

static void dispatch_request(connection_t *conn)
{
    // Search route table
    const http_route_t *routes = httpd_get_routes();
    int route_count = httpd_get_route_count();

    for (int i = 0; i < route_count; i++) {
        const http_route_t *r = &routes[i];
        if (r->method != conn->req.method) continue;

        bool match;
        if (r->prefix_match) {
            match = (strncmp(conn->req.uri, r->uri, strlen(r->uri)) == 0);
        } else {
            match = (strcmp(conn->req.uri, r->uri) == 0);
        }

        if (match) {
            if (!r->no_auth && !request_is_authenticated(conn)) {
                http_send_error(conn, 401, "unauthorized");
                return;
            }
            r->handler(conn);
            return;
        }
    }

    // No API route matched — static files don't require auth
    if (conn->req.method == HTTP_GET) {
        serve_static_file(conn, conn->req.uri);
    } else {
        http_send_error(conn, 404, "not found");
    }
}

//
// HTTP request parser
//

// Find \r\n within a bounded region
static const char *find_crlf(const char *start, const char *end)
{
    for (const char *p = start; p < end - 1; p++) {
        if (p[0] == '\r' && p[1] == '\n') return p;
    }
    return NULL;
}

// Case-insensitive header search, returns pointer to value or NULL
static const char *find_header(connection_t *conn, const char *name)
{
    size_t name_len = strlen(name);
    const char *buf = (const char *)conn->recv_buf;
    const char *end = buf + conn->recv_len;

    // Start after request line
    const char *crlf = find_crlf(buf, end);
    if (crlf == NULL) return NULL;
    const char *line = crlf + 2;

    while (line < end - 1) {
        // Check for end of headers
        if (line[0] == '\r' && line[1] == '\n') break;

        // Find end of this header line
        const char *line_end = find_crlf(line, end);
        if (line_end == NULL) break;

        // Case-insensitive compare of header name
        if ((size_t)(line_end - line) > name_len && line[name_len] == ':') {
            bool match = true;
            for (size_t i = 0; i < name_len; i++) {
                char a = line[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                const char *val = line + name_len + 1;
                while (val < line_end && *val == ' ') val++;
                return val;
            }
        }

        line = line_end + 2;
    }

    return NULL;
}

// Extract header value into a buffer
static void extract_header_value(const char *val, char *out, size_t out_size)
{
    size_t i = 0;
    while (val[i] != '\r' && val[i] != '\n' && val[i] != '\0' && i < out_size - 1) {
        out[i] = val[i];
        i++;
    }
    out[i] = '\0';
}

static void parse_request(connection_t *conn)
{
    char *buf = (char *)conn->recv_buf;

    // Find header/body boundary BEFORE any null-termination modifies the buffer
    char *header_end = strstr(buf, "\r\n\r\n");
    if (header_end == NULL) {
        http_send_error(conn, 400, "malformed request");
        return;
    }
    char *body_start = header_end + 4;
    size_t headers_len = body_start - buf;
    conn->headers_len = headers_len;

    // Parse method
    if (strncmp(buf, "GET ", 4) == 0) {
        conn->req.method = HTTP_GET;
        buf += 4;
    } else if (strncmp(buf, "POST ", 5) == 0) {
        conn->req.method = HTTP_POST;
        buf += 5;
    } else {
        http_send_error(conn, 405, "method not allowed");
        return;
    }

    // Extract URI
    char *uri_start = buf;
    char *uri_end = uri_start;
    while (*uri_end != ' ' && *uri_end != '?' && *uri_end != '\r' && *uri_end != '\0') {
        uri_end++;
    }

    // Check for query string
    conn->req.query = NULL;
    if (*uri_end == '?') {
        *uri_end = '\0';
        char *query_start = uri_end + 1;
        char *query_end = query_start;
        while (*query_end != ' ' && *query_end != '\r' && *query_end != '\0') {
            query_end++;
        }
        *query_end = '\0';
        conn->req.query = query_start;
    } else {
        *uri_end = '\0';
    }

    conn->req.uri = uri_start;

    // Parse key headers
    conn->req.content_length = 0;
    conn->req.auth_header = NULL;
    conn->req.websocket_upgrade = false;
    conn->req.ws_key = NULL;

    const char *cl = find_header(conn, "Content-Length");
    if (cl) {
        conn->req.content_length = (uint16_t)atoi(cl);
    }

    const char *auth = find_header(conn, "Authorization");
    if (auth) {
        conn->req.auth_header = auth;
    }

    const char *upgrade = find_header(conn, "Upgrade");
    if (upgrade) {
        // Check if value starts with "websocket"
        if ((upgrade[0] == 'w' || upgrade[0] == 'W') &&
            (upgrade[1] == 'e' || upgrade[1] == 'E') &&
            (upgrade[2] == 'b' || upgrade[2] == 'B')) {
            conn->req.websocket_upgrade = true;
        }
    }

    const char *ws_key = find_header(conn, "Sec-WebSocket-Key");
    if (ws_key) {
        conn->req.ws_key = ws_key;
    }

    // WebSocket upgrade check
    if (conn->req.websocket_upgrade && conn->req.ws_key) {
        if (!request_is_authenticated(conn)) {
            http_send_error(conn, 401, "unauthorized");
            return;
        }
        if (httpd_websocket_upgrade(conn)) {
            tcp_nagle_disable(conn->pcb);
        } else {
            http_send_error(conn, 400, "WebSocket handshake failed");
        }
        return;
    }

    // Body handling for POST
    conn->req.body = NULL;
    conn->req.body_len = 0;

    if (conn->req.method == HTTP_POST && conn->req.content_length > 0) {
        size_t body_received = conn->recv_len - headers_len;

        if (body_received >= conn->req.content_length) {
            // Full body received
            conn->req.body = body_start;
            conn->req.body_len = conn->req.content_length;
            body_start[conn->req.body_len] = '\0';
            dispatch_request(conn);
        } else {
            // Need more body data
            conn->state = CONN_RECV_BODY;
        }
        return;
    }

    // GET or POST with no body - dispatch immediately
    dispatch_request(conn);
}

//
// TCP callbacks
//

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    connection_t *conn = conn_alloc();
    if (conn == NULL) {
        // Pool exhausted
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    conn->pcb = newpcb;
    conn->state = CONN_RECV_HEADERS;

    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, http_recv);
    tcp_sent(newpcb, http_sent);
    tcp_err(newpcb, http_err);
    tcp_poll(newpcb, http_poll, HTTP_POLL_INTERVAL);

    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    connection_t *conn = (connection_t *)arg;

    if (conn == NULL) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }

    if (p == NULL) {
        // Client closed connection
        conn_close(conn);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    // Reset idle timeout on data received
    conn->poll_ticks = 0;

    // WebSocket connections use their own recv buffer and handler
    if (conn->state == CONN_WEBSOCKET) {
        httpd_websocket_recv(conn, p);
        return ERR_OK;
    }

    // Accumulate data into recv_buf
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        size_t copy_len = q->len;
        if (conn->recv_len + copy_len > HTTP_RECV_BUF_SIZE - 1) {
            copy_len = HTTP_RECV_BUF_SIZE - 1 - conn->recv_len;
        }
        if (copy_len > 0) {
            memcpy(conn->recv_buf + conn->recv_len, q->payload, copy_len);
            conn->recv_len += copy_len;
        }
    }
    conn->recv_buf[conn->recv_len] = '\0';

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    if (conn->state == CONN_RECV_HEADERS) {
        // Check for end of headers
        if (strstr((char *)conn->recv_buf, "\r\n\r\n") != NULL) {
            parse_request(conn);
        }
    } else if (conn->state == CONN_RECV_BODY) {
        // Check if full body has been received
        size_t body_received = conn->recv_len - conn->headers_len;
        if (body_received >= conn->req.content_length) {
            char *bp = (char *)conn->recv_buf + conn->headers_len;
            conn->req.body = bp;
            conn->req.body_len = conn->req.content_length;
            bp[conn->req.body_len] = '\0';
            dispatch_request(conn);
        }
    }

    return ERR_OK;
}

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)tpcb;
    (void)len;

    connection_t *conn = (connection_t *)arg;
    if (conn == NULL) return ERR_OK;

    if (conn->state == CONN_SENDING_FILE) {
        if (conn->file_remaining > 0) {
            send_file_chunk(conn);
        } else {
            conn_close(conn);
        }
    } else if (conn->state == CONN_SENDING_RESPONSE) {
        // Dynamic response fully sent
        conn_close(conn);
    }

    return ERR_OK;
}

static void http_err(void *arg, err_t err)
{
    (void)err;

    connection_t *conn = (connection_t *)arg;
    if (conn == NULL) return;

    // Clean up WebSocket state if this was a WebSocket connection
    if (conn->state == CONN_WEBSOCKET) {
        httpd_websocket_closed(conn);
    }

    // PCB is already freed by lwIP when err callback fires
    conn->pcb = NULL;
    conn->in_use = false;
}

static err_t http_poll(void *arg, struct tcp_pcb *tpcb)
{
    (void)tpcb;

    connection_t *conn = (connection_t *)arg;
    if (conn == NULL) return ERR_OK;

    // WebSocket connections are long-lived, don't timeout
    if (conn->state == CONN_WEBSOCKET) return ERR_OK;

    conn->poll_ticks++;
    if (conn->poll_ticks >= HTTP_TIMEOUT_TICKS) {
        printf("HTTP: Closing idle connection\r\n");
        conn_close(conn);
    }

    return ERR_OK;
}

//
// Init
//

void httpd_server_init(uint16_t port)
{
    printf("HTTP server: Starting on port %d\r\n", port);

    listen_pcb = tcp_new();
    if (listen_pcb == NULL) {
        printf("HTTP server: Failed to create PCB\r\n");
        return;
    }

    err_t err = tcp_bind(listen_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        printf("HTTP server: Failed to bind to port %d (err=%d)\r\n", port, err);
        tcp_close(listen_pcb);
        listen_pcb = NULL;
        return;
    }

    listen_pcb = tcp_listen(listen_pcb);
    if (listen_pcb == NULL) {
        printf("HTTP server: Failed to listen\r\n");
        return;
    }

    tcp_accept(listen_pcb, http_accept);
    printf("HTTP server: Listening on port %d\r\n", port);
}
