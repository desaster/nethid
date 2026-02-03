/*
 * WebSocket server for HID control
 *
 * Implements RFC 6455 WebSocket protocol (minimal subset) for low-latency
 * binary HID command transmission.
 */

#include "websocket.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include "usb.h"
#include "board.h"

//--------------------------------------------------------------------+
// Minimal SHA-1 Implementation (for WebSocket handshake only)
//--------------------------------------------------------------------+

static void sha1(const uint8_t *data, size_t len, uint8_t hash[20])
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Pre-processing: add padding
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = calloc(new_len, 1);
    if (!msg) return;

    memcpy(msg, data, len);
    msg[len] = 0x80;

    // Append length in bits as 64-bit big-endian
    uint64_t bits_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        msg[new_len - 1 - i] = (bits_len >> (i * 8)) & 0xFF;
    }

    // Process each 64-byte chunk
    for (size_t chunk = 0; chunk < new_len; chunk += 64) {
        uint32_t w[80];

        // Break chunk into 16 32-bit big-endian words
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[chunk + i*4] << 24) |
                   ((uint32_t)msg[chunk + i*4 + 1] << 16) |
                   ((uint32_t)msg[chunk + i*4 + 2] << 8) |
                   ((uint32_t)msg[chunk + i*4 + 3]);
        }

        // Extend to 80 words
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

    // Output hash as big-endian
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

//--------------------------------------------------------------------+
// Minimal Base64 Encode (for WebSocket handshake only)
//--------------------------------------------------------------------+

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

//--------------------------------------------------------------------+
// Constants
//--------------------------------------------------------------------+

// WebSocket magic GUID for handshake (RFC 6455)
#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// WebSocket opcodes
#define WS_OPCODE_CONTINUATION 0x00
#define WS_OPCODE_TEXT         0x01
#define WS_OPCODE_BINARY       0x02
#define WS_OPCODE_CLOSE        0x08
#define WS_OPCODE_PING         0x09
#define WS_OPCODE_PONG         0x0A

// HID command types (from protocol spec)
#define HID_CMD_KEY           0x01
#define HID_CMD_MOUSE_MOVE    0x02
#define HID_CMD_MOUSE_BUTTON  0x03
#define HID_CMD_SCROLL        0x04
#define HID_CMD_CONSUMER      0x06
#define HID_CMD_SYSTEM        0x07
#define HID_CMD_RELEASE_ALL   0x0F
#define HID_CMD_STATUS        0x10  // Server -> Client: USB status

// Buffer sizes
#define HTTP_BUFFER_SIZE 1024
#define WS_FRAME_BUFFER_SIZE 256

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

typedef enum {
    WS_STATE_IDLE,
    WS_STATE_HTTP_HANDSHAKE,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING,
} ws_state_t;

// Single client connection state
static struct tcp_pcb *ws_listen_pcb = NULL;
static struct tcp_pcb *ws_client_pcb = NULL;
static ws_state_t ws_state = WS_STATE_IDLE;

// Receive buffer for HTTP handshake
static char http_buffer[HTTP_BUFFER_SIZE];
static size_t http_buffer_len = 0;

// Current mouse button state (track for release-all)
static uint8_t current_buttons = 0;

// Reference to keycodes from usb.c
extern uint8_t keycodes[6];

//--------------------------------------------------------------------+
// Forward declarations
//--------------------------------------------------------------------+

static err_t ws_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t ws_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void ws_err(void *arg, err_t err);
static err_t ws_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

static bool process_http_handshake(struct tcp_pcb *pcb);
static bool compute_accept_key(const char *client_key, char *accept_key, size_t accept_key_size);
static void process_websocket_frame(struct tcp_pcb *pcb, const uint8_t *data, size_t len);
static void process_hid_command(const uint8_t *payload, size_t len);
static void send_close_frame(struct tcp_pcb *pcb);
static void send_close_frame_with_code(struct tcp_pcb *pcb, uint16_t code, const char *reason);
static void close_connection(void);

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void websocket_init(void)
{
    printf("WebSocket: Starting server on port %d\r\n", WEBSOCKET_PORT);

    ws_listen_pcb = tcp_new();
    if (ws_listen_pcb == NULL) {
        printf("WebSocket: Failed to create PCB\r\n");
        return;
    }

    err_t err = tcp_bind(ws_listen_pcb, IP_ADDR_ANY, WEBSOCKET_PORT);
    if (err != ERR_OK) {
        printf("WebSocket: Failed to bind to port %d (err=%d)\r\n", WEBSOCKET_PORT, err);
        tcp_close(ws_listen_pcb);
        ws_listen_pcb = NULL;
        return;
    }

    ws_listen_pcb = tcp_listen(ws_listen_pcb);
    if (ws_listen_pcb == NULL) {
        printf("WebSocket: Failed to listen\r\n");
        return;
    }

    tcp_accept(ws_listen_pcb, ws_accept);
    printf("WebSocket: Server listening on port %d\r\n", WEBSOCKET_PORT);
}

bool websocket_client_connected(void)
{
    return ws_state == WS_STATE_CONNECTED && ws_client_pcb != NULL;
}

void websocket_release_all(void)
{
    printf("WebSocket: Releasing all keys and buttons\r\n");

    // Release all keys
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] != 0) {
            depress_key(keycodes[i]);
        }
    }

    // Release all mouse buttons
    current_buttons = 0;
    move_mouse(0, 0, 0, 0, 0);
}

void websocket_send_status(void)
{
    if (ws_state != WS_STATE_CONNECTED || ws_client_pcb == NULL) {
        return;
    }

    // Build status flags
    uint8_t flags = 0;
    if (usb_mounted)   flags |= 0x01;  // Bit 0: usb_mounted
    if (usb_suspended) flags |= 0x02;  // Bit 1: usb_suspended

    // Build WebSocket binary frame
    // [0x82] = FIN + binary opcode
    // [0x02] = payload length (2 bytes)
    // [HID_CMD_STATUS] [flags]
    uint8_t frame[4] = {
        0x82,           // FIN + binary opcode
        0x02,           // Payload length
        HID_CMD_STATUS, // Command type
        flags           // Status flags
    };

    err_t err = tcp_write(ws_client_pcb, frame, sizeof(frame), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(ws_client_pcb);
    }
}

//--------------------------------------------------------------------+
// TCP Callbacks
//--------------------------------------------------------------------+

static err_t ws_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    // Session takeover - if client already connected, disconnect it
    if (ws_client_pcb != NULL) {
        printf("WebSocket: Taking over session (disconnecting previous client)\r\n");

        // Send close frame to old client with code 4001
        if (ws_state == WS_STATE_CONNECTED) {
            send_close_frame_with_code(ws_client_pcb, 4001, "Session taken over");
        }

        // Clean up old client
        websocket_release_all();
        tcp_arg(ws_client_pcb, NULL);
        tcp_recv(ws_client_pcb, NULL);
        tcp_err(ws_client_pcb, NULL);
        tcp_sent(ws_client_pcb, NULL);
        tcp_close(ws_client_pcb);
        ws_client_pcb = NULL;
        ws_state = WS_STATE_IDLE;
    }

    printf("WebSocket: New connection\r\n");

    ws_client_pcb = newpcb;
    ws_state = WS_STATE_HTTP_HANDSHAKE;
    http_buffer_len = 0;

    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, ws_recv);
    tcp_err(newpcb, ws_err);
    tcp_sent(newpcb, ws_sent);

    // Set TCP_NODELAY for lower latency
    tcp_nagle_disable(newpcb);

    return ERR_OK;
}

static err_t ws_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;

    if (p == NULL) {
        // Connection closed by client
        printf("WebSocket: Client closed connection\r\n");
        close_connection();
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    // Process received data based on state
    if (ws_state == WS_STATE_HTTP_HANDSHAKE) {
        // Accumulate HTTP request
        struct pbuf *q;
        for (q = p; q != NULL; q = q->next) {
            size_t copy_len = q->len;
            if (http_buffer_len + copy_len >= HTTP_BUFFER_SIZE - 1) {
                copy_len = HTTP_BUFFER_SIZE - 1 - http_buffer_len;
            }
            if (copy_len > 0) {
                memcpy(http_buffer + http_buffer_len, q->payload, copy_len);
                http_buffer_len += copy_len;
            }
        }
        http_buffer[http_buffer_len] = '\0';

        // Check if we have complete HTTP request (ends with \r\n\r\n)
        if (strstr(http_buffer, "\r\n\r\n") != NULL) {
            if (process_http_handshake(tpcb)) {
                ws_state = WS_STATE_CONNECTED;
                printf("WebSocket: Handshake complete\r\n");
                // Send initial USB status to client
                websocket_send_status();
            } else {
                printf("WebSocket: Handshake failed\r\n");
                close_connection();
            }
        }
    } else if (ws_state == WS_STATE_CONNECTED) {
        // Process WebSocket frames
        struct pbuf *q;
        for (q = p; q != NULL; q = q->next) {
            process_websocket_frame(tpcb, (const uint8_t *)q->payload, q->len);
        }
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

static void ws_err(void *arg, err_t err)
{
    (void)arg;
    printf("WebSocket: Connection error (err=%d)\r\n", err);
    websocket_release_all();
    ws_client_pcb = NULL;
    ws_state = WS_STATE_IDLE;
}

static err_t ws_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg;
    (void)tpcb;
    (void)len;
    return ERR_OK;
}

//--------------------------------------------------------------------+
// HTTP Handshake
//--------------------------------------------------------------------+

static bool process_http_handshake(struct tcp_pcb *pcb)
{
    // Verify it's a WebSocket upgrade request
    if (strstr(http_buffer, "GET ") != http_buffer) {
        return false;
    }

    // Case-insensitive search for Upgrade header (Safari may use different case)
    const char *upgrade = strstr(http_buffer, "Upgrade:");
    if (upgrade == NULL) {
        upgrade = strstr(http_buffer, "upgrade:");
    }
    if (upgrade == NULL) {
        return false;
    }

    // Find Sec-WebSocket-Key header (case-insensitive)
    const char *key_header = strstr(http_buffer, "Sec-WebSocket-Key:");
    if (key_header == NULL) {
        key_header = strstr(http_buffer, "sec-websocket-key:");
    }
    if (key_header == NULL) {
        key_header = strstr(http_buffer, "Sec-Websocket-Key:");
    }
    if (key_header == NULL) {
        printf("WebSocket: Missing Sec-WebSocket-Key\r\n");
        return false;
    }

    // Extract key value
    key_header = strchr(key_header, ':') + 1;
    while (*key_header == ' ') key_header++;

    char client_key[64];
    int i = 0;
    while (key_header[i] != '\r' && key_header[i] != '\n' && key_header[i] != '\0' && i < 63) {
        client_key[i] = key_header[i];
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

    err_t err = tcp_write(pcb, response, response_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("WebSocket: Failed to send handshake response (err=%d)\r\n", err);
        return false;
    }
    tcp_output(pcb);

    return true;
}

static bool compute_accept_key(const char *client_key, char *accept_key, size_t accept_key_size)
{
    // Concatenate client key + magic GUID
    char concat[128];
    int concat_len = snprintf(concat, sizeof(concat), "%s%s", client_key, WS_MAGIC_GUID);
    if (concat_len < 0 || concat_len >= (int)sizeof(concat)) {
        return false;
    }

    // SHA-1 hash
    uint8_t sha1_hash[20];
    sha1((const uint8_t *)concat, concat_len, sha1_hash);

    // Base64 encode (20 bytes -> 28 chars + null)
    if (accept_key_size < 29) {
        return false;
    }
    base64_encode(sha1_hash, 20, accept_key);

    return true;
}

//--------------------------------------------------------------------+
// WebSocket Frame Processing
//--------------------------------------------------------------------+

static void process_websocket_frame(struct tcp_pcb *pcb, const uint8_t *data, size_t len)
{
    if (len < 2) {
        return;
    }

    // Parse frame header
    // bool fin = (data[0] & 0x80) != 0;
    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    size_t payload_len = data[1] & 0x7F;

    size_t header_len = 2;

    // Extended payload length
    if (payload_len == 126) {
        if (len < 4) return;
        payload_len = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len == 127) {
        // 64-bit length - not supported for our use case
        printf("WebSocket: 64-bit payload length not supported\r\n");
        return;
    }

    // Masking key (client frames must be masked)
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < header_len + 4) return;
        memcpy(mask, data + header_len, 4);
        header_len += 4;
    }

    // Check we have complete payload
    if (len < header_len + payload_len) {
        printf("WebSocket: Incomplete frame (have %zu, need %zu)\r\n",
               len, header_len + payload_len);
        return;
    }

    // Unmask payload
    uint8_t payload[WS_FRAME_BUFFER_SIZE];
    if (payload_len > WS_FRAME_BUFFER_SIZE) {
        printf("WebSocket: Payload too large (%zu)\r\n", payload_len);
        return;
    }

    for (size_t i = 0; i < payload_len; i++) {
        payload[i] = data[header_len + i] ^ mask[i % 4];
    }

    // Handle frame based on opcode
    switch (opcode) {
        case WS_OPCODE_BINARY:
            process_hid_command(payload, payload_len);
            break;

        case WS_OPCODE_TEXT:
            // Text frames not supported
            printf("WebSocket: Text frame ignored\r\n");
            break;

        case WS_OPCODE_CLOSE:
            printf("WebSocket: Close frame received\r\n");
            send_close_frame(pcb);
            close_connection();
            break;

        case WS_OPCODE_PING:
            // Respond with pong (echo payload)
            {
                uint8_t pong_frame[2 + WS_FRAME_BUFFER_SIZE];
                pong_frame[0] = 0x80 | WS_OPCODE_PONG;  // FIN + PONG
                if (payload_len < 126) {
                    pong_frame[1] = payload_len;
                    memcpy(pong_frame + 2, payload, payload_len);
                    tcp_write(pcb, pong_frame, 2 + payload_len, TCP_WRITE_FLAG_COPY);
                    tcp_output(pcb);
                }
            }
            break;

        case WS_OPCODE_PONG:
            // Pong received - ignore for now (no keepalive implemented yet)
            break;

        default:
            printf("WebSocket: Unknown opcode 0x%02x\r\n", opcode);
            break;
    }
}

//--------------------------------------------------------------------+
// HID Command Processing
//--------------------------------------------------------------------+

static void process_hid_command(const uint8_t *payload, size_t len)
{
    if (len < 1) {
        return;
    }

    uint8_t cmd_type = payload[0];

    switch (cmd_type) {
        case HID_CMD_KEY:
            // [0x01][keycode:u8][down:u8]
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
            // [0x02][dx:i16 LE][dy:i16 LE]
            if (len >= 5) {
                int16_t dx = (int16_t)(payload[1] | (payload[2] << 8));
                int16_t dy = (int16_t)(payload[3] | (payload[4] << 8));
                // Clamp to int8_t range for move_mouse
                if (dx < -127) dx = -127;
                if (dx > 127) dx = 127;
                if (dy < -127) dy = -127;
                if (dy > 127) dy = 127;
                move_mouse(current_buttons, (int8_t)dx, (int8_t)dy, 0, 0);
            }
            break;

        case HID_CMD_MOUSE_BUTTON:
            // [0x03][button:u8][down:u8]
            if (len >= 3) {
                uint8_t button = payload[1];
                bool down = payload[2] != 0;
                if (down) {
                    current_buttons |= button;
                } else {
                    current_buttons &= ~button;
                }
                move_mouse(current_buttons, 0, 0, 0, 0);
            }
            break;

        case HID_CMD_SCROLL:
            // [0x04][dx:i8][dy:i8]
            if (len >= 3) {
                int8_t scroll_x = (int8_t)payload[1];
                int8_t scroll_y = (int8_t)payload[2];
                move_mouse(current_buttons, 0, 0, scroll_y, scroll_x);
            }
            break;

        case HID_CMD_CONSUMER:
            // [0x06][code_lo:u8][code_hi:u8][down:u8]
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
            // [0x07][code_lo:u8][code_hi:u8][down:u8]
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
            // [0x0F]
            websocket_release_all();
            break;

        default:
            printf("WebSocket: Unknown HID command 0x%02x\r\n", cmd_type);
            break;
    }
}

//--------------------------------------------------------------------+
// Connection Management
//--------------------------------------------------------------------+

static void send_close_frame(struct tcp_pcb *pcb)
{
    uint8_t close_frame[2] = {0x88, 0x00};  // FIN + CLOSE, no payload
    tcp_write(pcb, close_frame, 2, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void send_close_frame_with_code(struct tcp_pcb *pcb, uint16_t code, const char *reason)
{
    size_t reason_len = reason ? strlen(reason) : 0;
    size_t payload_len = 2 + reason_len;  // 2 bytes for code + reason string

    if (payload_len > 125) {
        // Truncate reason if too long
        reason_len = 123;
        payload_len = 125;
    }

    uint8_t frame[2 + 125];  // Max: 2-byte header + 125-byte payload
    frame[0] = 0x88;  // FIN + CLOSE opcode
    frame[1] = (uint8_t)payload_len;
    frame[2] = (code >> 8) & 0xFF;  // Close code high byte (big-endian)
    frame[3] = code & 0xFF;         // Close code low byte

    if (reason && reason_len > 0) {
        memcpy(frame + 4, reason, reason_len);
    }

    tcp_write(pcb, frame, 2 + payload_len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void close_connection(void)
{
    websocket_release_all();

    if (ws_client_pcb != NULL) {
        tcp_arg(ws_client_pcb, NULL);
        tcp_recv(ws_client_pcb, NULL);
        tcp_err(ws_client_pcb, NULL);
        tcp_sent(ws_client_pcb, NULL);
        tcp_close(ws_client_pcb);
        ws_client_pcb = NULL;
    }

    ws_state = WS_STATE_IDLE;
    http_buffer_len = 0;
}
