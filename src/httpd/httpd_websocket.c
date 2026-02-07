/*
 * WebSocket protocol handling for NetHID HTTP server
 *
 * Handles WebSocket upgrade handshake, frame parsing, and HID command
 * processing. Integrated into the HTTP server on the same port.
 */

#include "httpd_server.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include "usb.h"
#include "board.h"

//
// Minimal SHA-1 implementation
//

static void sha1(const uint8_t *data, size_t len, uint8_t hash[20])
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = calloc(new_len, 1);
    if (!msg) return;

    memcpy(msg, data, len);
    msg[len] = 0x80;

    uint64_t bits_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        msg[new_len - 1 - i] = (bits_len >> (i * 8)) & 0xFF;
    }

    for (size_t chunk = 0; chunk < new_len; chunk += 64) {
        uint32_t w[80];

        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[chunk + i*4] << 24) |
                   ((uint32_t)msg[chunk + i*4 + 1] << 16) |
                   ((uint32_t)msg[chunk + i*4 + 2] << 8) |
                   ((uint32_t)msg[chunk + i*4 + 3]);
        }

        for (int i = 16; i < 80; i++) {
            uint32_t temp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (temp << 1) | (temp >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    free(msg);

    hash[0] = (h0 >> 24) & 0xFF; hash[1] = (h0 >> 16) & 0xFF;
    hash[2] = (h0 >> 8) & 0xFF;  hash[3] = h0 & 0xFF;
    hash[4] = (h1 >> 24) & 0xFF; hash[5] = (h1 >> 16) & 0xFF;
    hash[6] = (h1 >> 8) & 0xFF;  hash[7] = h1 & 0xFF;
    hash[8] = (h2 >> 24) & 0xFF; hash[9] = (h2 >> 16) & 0xFF;
    hash[10] = (h2 >> 8) & 0xFF; hash[11] = h2 & 0xFF;
    hash[12] = (h3 >> 24) & 0xFF; hash[13] = (h3 >> 16) & 0xFF;
    hash[14] = (h3 >> 8) & 0xFF; hash[15] = h3 & 0xFF;
    hash[16] = (h4 >> 24) & 0xFF; hash[17] = (h4 >> 16) & 0xFF;
    hash[18] = (h4 >> 8) & 0xFF; hash[19] = h4 & 0xFF;
}

//
// Minimal Base64 encode
//

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *data, size_t len, char *out)
{
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        uint32_t octet_a = i < len ? data[i] : 0;
        uint32_t octet_b = i + 1 < len ? data[i + 1] : 0;
        uint32_t octet_c = i + 2 < len ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = base64_chars[(triple >> 18) & 0x3F];
        out[j++] = base64_chars[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? base64_chars[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? base64_chars[triple & 0x3F] : '=';
    }
    out[j] = '\0';
}

//
// Constants
//

#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_OPCODE_TEXT         0x01
#define WS_OPCODE_BINARY       0x02
#define WS_OPCODE_CLOSE        0x08
#define WS_OPCODE_PING         0x09
#define WS_OPCODE_PONG         0x0A

#define HID_CMD_KEY           0x01
#define HID_CMD_MOUSE_MOVE    0x02
#define HID_CMD_MOUSE_BUTTON  0x03
#define HID_CMD_SCROLL        0x04
#define HID_CMD_CONSUMER      0x06
#define HID_CMD_SYSTEM        0x07
#define HID_CMD_RELEASE_ALL   0x0F
#define HID_CMD_STATUS        0x10

//
// State
//

// Active WebSocket connection
static connection_t *ws_active_conn = NULL;

extern uint8_t keycodes[6];  // from usb.c

//
// Forward declarations
//

static bool compute_accept_key(const char *client_key, char *accept_key, size_t accept_key_size);
static size_t process_websocket_frame(connection_t *conn, const uint8_t *data, size_t len);
static void process_ws_recv_buffer(connection_t *conn);
static void process_hid_command(connection_t *conn, const uint8_t *payload, size_t len);
static void send_close_frame(struct tcp_pcb *pcb);
static void send_close_frame_with_code(struct tcp_pcb *pcb, uint16_t code, const char *reason);
static void ws_close_connection(connection_t *conn);

//
// Public API
//

bool websocket_client_connected(void)
{
    return ws_active_conn != NULL && ws_active_conn->state == CONN_WEBSOCKET;
}

void websocket_release_all(void)
{
    if (ws_active_conn == NULL) return;

    printf("WebSocket: Releasing all keys and buttons\r\n");

    for (int i = 0; i < 6; i++) {
        if (keycodes[i] != 0) {
            depress_key(keycodes[i]);
        }
    }

    ws_active_conn->ws_mouse_buttons = 0;
    move_mouse(0, 0, 0, 0, 0);
}

void websocket_send_status(void)
{
    if (ws_active_conn == NULL || ws_active_conn->state != CONN_WEBSOCKET) {
        return;
    }

    uint8_t flags = 0;
    if (usb_mounted)   flags |= 0x01;
    if (usb_suspended) flags |= 0x02;

    uint8_t frame[4] = {
        0x82,           // FIN + binary opcode
        0x02,           // Payload length
        HID_CMD_STATUS,
        flags
    };

    err_t err = tcp_write(ws_active_conn->pcb, frame, sizeof(frame), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(ws_active_conn->pcb);
    }
}

//
// Handshake
//

bool httpd_websocket_upgrade(connection_t *conn)
{
    // Session takeover - disconnect previous WebSocket client
    if (ws_active_conn != NULL && ws_active_conn != conn) {
        printf("WebSocket: Taking over session (disconnecting previous client)\r\n");

        if (ws_active_conn->state == CONN_WEBSOCKET) {
            send_close_frame_with_code(ws_active_conn->pcb, 4001, "Session taken over");
        }

        websocket_release_all();
        conn_close(ws_active_conn);
        ws_active_conn = NULL;
    }

    // Extract the Sec-WebSocket-Key value
    char client_key[64];
    int i = 0;
    const char *key = conn->req.ws_key;
    while (key[i] != '\r' && key[i] != '\n' && key[i] != '\0' && i < 63) {
        client_key[i] = key[i];
        i++;
    }
    client_key[i] = '\0';

    // Compute accept key
    char accept_key[64];
    if (!compute_accept_key(client_key, accept_key, sizeof(accept_key))) {
        printf("WebSocket: Failed to compute accept key\r\n");
        return false;
    }

    // Send HTTP 101 response
    char response[256];
    int response_len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);

    err_t err = tcp_write(conn->pcb, response, response_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("WebSocket: Failed to send handshake response (err=%d)\r\n", err);
        return false;
    }
    tcp_output(conn->pcb);

    // Transition to WebSocket state
    conn->state = CONN_WEBSOCKET;
    conn->ws_recv_len = 0;
    conn->ws_mouse_buttons = 0;
    ws_active_conn = conn;

    printf("WebSocket: Handshake complete\r\n");

    // Send initial USB status
    websocket_send_status();

    return true;
}

//
// Receive handler
//

void httpd_websocket_recv(connection_t *conn, struct pbuf *p)
{
    if (p == NULL) {
        // Client closed connection
        printf("WebSocket: Client closed connection\r\n");
        ws_close_connection(conn);
        return;
    }

    // Append pbuf data to WebSocket reassembly buffer
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        size_t copy_len = q->len;
        if (conn->ws_recv_len + copy_len > WS_RECV_BUF_SIZE) {
            copy_len = WS_RECV_BUF_SIZE - conn->ws_recv_len;
        }
        if (copy_len > 0) {
            memcpy(conn->ws_recv_buf + conn->ws_recv_len, q->payload, copy_len);
            conn->ws_recv_len += copy_len;
        }
    }

    process_ws_recv_buffer(conn);

    // Only acknowledge if connection is still open
    if (conn->in_use && conn->pcb != NULL) {
        tcp_recved(conn->pcb, p->tot_len);
    }
    pbuf_free(p);
}

//
// Handshake crypto
//

static bool compute_accept_key(const char *client_key, char *accept_key, size_t accept_key_size)
{
    char concat[128];
    int concat_len = snprintf(concat, sizeof(concat), "%s%s", client_key, WS_MAGIC_GUID);
    if (concat_len < 0 || concat_len >= (int)sizeof(concat)) {
        return false;
    }

    uint8_t sha1_hash[20];
    sha1((const uint8_t *)concat, concat_len, sha1_hash);

    if (accept_key_size < 29) {
        return false;
    }
    base64_encode(sha1_hash, 20, accept_key);

    return true;
}

//
// WebSocket Frame Processing
//

static size_t process_websocket_frame(connection_t *conn, const uint8_t *data, size_t len)
{
    if (len < 2) {
        return 0;
    }

    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    size_t payload_len = data[1] & 0x7F;

    size_t header_len = 2;

    if (payload_len == 126) {
        if (len < 4) return 0;
        payload_len = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len == 127) {
        printf("WebSocket: 64-bit payload length not supported\r\n");
        return len;
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < header_len + 4) return 0;
        memcpy(mask, data + header_len, 4);
        header_len += 4;
    }

    size_t frame_len = header_len + payload_len;

    if (len < frame_len) {
        return 0;
    }

    // Unmask payload
    uint8_t payload[WS_FRAME_BUF_SIZE];
    if (payload_len > WS_FRAME_BUF_SIZE) {
        printf("WebSocket: Payload too large (%zu)\r\n", payload_len);
        return frame_len;
    }

    for (size_t i = 0; i < payload_len; i++) {
        payload[i] = data[header_len + i] ^ mask[i % 4];
    }

    switch (opcode) {
        case WS_OPCODE_BINARY:
            process_hid_command(conn, payload, payload_len);
            break;

        case WS_OPCODE_TEXT:
            printf("WebSocket: Text frame ignored\r\n");
            break;

        case WS_OPCODE_CLOSE:
            printf("WebSocket: Close frame received\r\n");
            send_close_frame(conn->pcb);
            ws_close_connection(conn);
            break;

        case WS_OPCODE_PING:
            {
                uint8_t pong_frame[2 + WS_FRAME_BUF_SIZE];
                pong_frame[0] = 0x80 | WS_OPCODE_PONG;
                if (payload_len < 126) {
                    pong_frame[1] = payload_len;
                    memcpy(pong_frame + 2, payload, payload_len);
                    tcp_write(conn->pcb, pong_frame, 2 + payload_len, TCP_WRITE_FLAG_COPY);
                    tcp_output(conn->pcb);
                }
            }
            break;

        case WS_OPCODE_PONG:
            break;

        default:
            printf("WebSocket: Unknown opcode 0x%02x\r\n", opcode);
            break;
    }

    return frame_len;
}

static void process_ws_recv_buffer(connection_t *conn)
{
    while (conn->ws_recv_len > 0 && conn->state == CONN_WEBSOCKET) {
        size_t consumed = process_websocket_frame(conn, conn->ws_recv_buf, conn->ws_recv_len);
        if (consumed == 0) {
            break;
        }
        if (consumed >= conn->ws_recv_len) {
            conn->ws_recv_len = 0;
        } else {
            conn->ws_recv_len -= consumed;
            memmove(conn->ws_recv_buf, conn->ws_recv_buf + consumed, conn->ws_recv_len);
        }
    }
}

//
// HID Command Processing
//

static void process_hid_command(connection_t *conn, const uint8_t *payload, size_t len)
{
    if (len < 1) return;

    uint8_t cmd_type = payload[0];

    switch (cmd_type) {
        case HID_CMD_KEY:
            if (len >= 3) {
                uint8_t keycode = payload[1];
                bool down = payload[2] != 0;
                if (down) {
                    press_key(keycode);
                } else {
                    depress_key(keycode);
                }
            }
            break;

        case HID_CMD_MOUSE_MOVE:
            if (len >= 5) {
                int16_t dx = (int16_t)(payload[1] | (payload[2] << 8));
                int16_t dy = (int16_t)(payload[3] | (payload[4] << 8));
                move_mouse(conn->ws_mouse_buttons, dx, dy, 0, 0);
            }
            break;

        case HID_CMD_MOUSE_BUTTON:
            if (len >= 3) {
                uint8_t button = payload[1];
                bool down = payload[2] != 0;
                if (down) {
                    conn->ws_mouse_buttons |= button;
                } else {
                    conn->ws_mouse_buttons &= ~button;
                }
                move_mouse(conn->ws_mouse_buttons, 0, 0, 0, 0);
            }
            break;

        case HID_CMD_SCROLL:
            if (len >= 3) {
                int8_t scroll_x = (int8_t)payload[1];
                int8_t scroll_y = (int8_t)payload[2];
                move_mouse(conn->ws_mouse_buttons, 0, 0, scroll_y, scroll_x);
            }
            break;

        case HID_CMD_CONSUMER:
            if (len >= 4) {
                uint16_t code = payload[1] | (payload[2] << 8);
                bool down = payload[3] != 0;
                if (down) {
                    press_consumer(code);
                } else {
                    release_consumer();
                }
            }
            break;

        case HID_CMD_SYSTEM:
            if (len >= 4) {
                uint16_t code = payload[1] | (payload[2] << 8);
                bool down = payload[3] != 0;
                if (down) {
                    press_system(code);
                } else {
                    release_system();
                }
            }
            break;

        case HID_CMD_RELEASE_ALL:
            websocket_release_all();
            break;

        default:
            printf("WebSocket: Unknown HID command 0x%02x\r\n", cmd_type);
            break;
    }
}

//
// Connection Management
//

static void send_close_frame(struct tcp_pcb *pcb)
{
    uint8_t close_frame[2] = {0x88, 0x00};
    tcp_write(pcb, close_frame, 2, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void send_close_frame_with_code(struct tcp_pcb *pcb, uint16_t code, const char *reason)
{
    size_t reason_len = reason ? strlen(reason) : 0;
    size_t payload_len = 2 + reason_len;

    if (payload_len > 125) {
        reason_len = 123;
        payload_len = 125;
    }

    uint8_t frame[2 + 125];
    frame[0] = 0x88;
    frame[1] = (uint8_t)payload_len;
    frame[2] = (code >> 8) & 0xFF;
    frame[3] = code & 0xFF;

    if (reason && reason_len > 0) {
        memcpy(frame + 4, reason, reason_len);
    }

    tcp_write(pcb, frame, 2 + payload_len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

void httpd_websocket_closed(connection_t *conn)
{
    if (ws_active_conn == conn) {
        websocket_release_all();
        ws_active_conn = NULL;
    }
}

static void ws_close_connection(connection_t *conn)
{
    httpd_websocket_closed(conn);
    conn_close(conn);
}
