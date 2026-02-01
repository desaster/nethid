/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Upi Tamminen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/cyw43_arch.h>

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "board.h"
#include "usb.h"
#include "config.h"

#define UART0_TX_PIN 16
#define UART0_RX_PIN 17

#define UART_DEBUG_INSTANCE uart0
#define UART_DEBUG_TX_PIN UART0_TX_PIN
#define UART_DEBUG_RX_PIN UART0_RX_PIN
#define UART_DEBUG_BAUD 115200

bool key_toggle = false;
struct udp_pcb *pcb;

#define PACKET_TYPE_KEYBOARD 1
#define PACKET_TYPE_MOUSE 2
#define PACKET_TYPE_CONSUMER 3

// header determines the second part of the packet
typedef struct {
    uint8_t type;
    uint8_t version; // 1
} packet_header;

// second part is either keyboard..
typedef struct {
    uint8_t pressed;
    uint8_t modifiers;
    uint8_t key;
} packet_keyboard;

// ..or mouse
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t vertical;
    int8_t horizontal;
} packet_mouse;

// ..or consumer control (media keys)
typedef struct {
    uint8_t pressed;
    uint16_t code;
} __attribute__((packed)) packet_consumer;

void led_blinking_task(void);
void hid_task(void);
void wifi_task(void);
int setup_wifi(uint32_t country, const char *ssid, const char *pass, uint32_t auth);

int main()
{
    board_init();

    stdio_uart_init_full(
        UART_DEBUG_INSTANCE,
        UART_DEBUG_BAUD,
        UART_DEBUG_TX_PIN,
        UART_DEBUG_RX_PIN);
    printf("\r\n------------------------------------------------------------------------------\r\nNetHID initializing\r\n------------------------------------------------------------------------------\r\n");

    printf("tusb_init()\r\n");
    tusb_init();

    printf("setup_wifi()\r\n");
    if (setup_wifi(
                CYW43_COUNTRY_FINLAND,
                WIFI_SSID,
                WIFI_PASSWORD,
                CYW43_AUTH_WPA2_MIXED_PSK) != 0) {
        printf("Failed to connect to WiFi\n");
        return 1;
    }

    printf("Entering main loop\r\n");

    while (true) {
        // usb device task
        tud_task();

        // need to call periodically when using pico_cyw43_arch_lwip_poll
        cyw43_arch_poll();

        // check wifi status
        wifi_task();

        // send usb hid report if needed, and stuff
        hid_task();

        // blink led or do other periodic status display
        led_blinking_task();

    }
}

//--------------------------------------------------------------------+
// WiFi stuff
//--------------------------------------------------------------------+

int setup_wifi(uint32_t country, const char *ssid, const char *pass, uint32_t auth)
{
    printf("cyw43_arch_init()\r\n");
    if (cyw43_arch_init()) {
        return 1;
    }
    printf("cyw43_arch_enable_sta_mode()\r\n");
    cyw43_arch_enable_sta_mode();

    // Disable power save mode to prevent AP disassociation due to inactivity
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    char hostname[16];
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    sprintf(hostname, "picow-%02x%02x%02x", mac[3], mac[4], mac[5]);
    netif_set_hostname(netif_default, hostname);

    printf("cyw43_arch_wifi_connect_async(%s, ..., ...)\r\n", ssid);
    wifi_up = false;
    update_blink_state();

    if (cyw43_arch_wifi_connect_async(ssid, pass, auth)) {
        return 2;
    }

    return 0;
}

static void udp_receive(
    void *arg,
    struct udp_pcb *pcb,
    struct pbuf *p,
    const struct ip4_addr *addr,
    unsigned short port)
{
    packet_header *hdr;
    packet_keyboard *kbd;
    packet_mouse *mou;
    packet_consumer *con;

    if (p == NULL) {
        return;
    }

    uint8_t *req_data = (uint8_t *) p->payload;

    if (p->len < sizeof(packet_header)) {
        printf("Packet too short\r\n");
        pbuf_free(p);
        return;
    }

    hdr = (packet_header *) p->payload;

    if (hdr->version != 1) {
        printf("Unknown packet version\r\n");
        pbuf_free(p);
        return;
    }

    if (hdr->type == PACKET_TYPE_KEYBOARD) {
        if (p->len != sizeof(packet_header) + sizeof(packet_keyboard)) {
            printf("Keyboard packet too short (%d)\r\n", p->len);
            pbuf_free(p);
            return;
        }
        kbd = (packet_keyboard *) (p->payload + sizeof(packet_header));
        printf("Received scancode: %02x %02x\r\n",
                kbd->pressed,
                kbd->key);
        if (kbd->pressed) {
            // printf("pressing key\r\n");
            press_key(kbd->key);
        } else {
            // printf("depressing key\r\n");
            depress_key(kbd->key);
        }
    } else if (hdr->type == PACKET_TYPE_MOUSE) {
        if (p->len != sizeof(packet_header) + sizeof(packet_mouse)) {
            printf("Mouse packet too short (%d)\r\n", p->len);
            pbuf_free(p);
            return;
        }
        mou = (packet_mouse *) (p->payload + sizeof(packet_header));
        // printf("Received mouse packet: %02x %02x %02x %02x %02x\r\n",
        //         mou->buttons,
        //         mou->x,
        //         mou->y,
        //         mou->vertical,
        //         mou->horizontal);
        move_mouse(mou->buttons, mou->x, mou->y, mou->vertical, mou->horizontal);
    } else if (hdr->type == PACKET_TYPE_CONSUMER) {
        if (p->len != sizeof(packet_header) + sizeof(packet_consumer)) {
            printf("Consumer packet too short (%d)\r\n", p->len);
            pbuf_free(p);
            return;
        }
        con = (packet_consumer *) (p->payload + sizeof(packet_header));
        printf("Received consumer code: %04x %s\r\n",
                con->code,
                con->pressed ? "down" : "up");
        if (con->pressed) {
            press_consumer(con->code);
        } else {
            release_consumer();
        }
    } else {
        printf("Unknown packet type: %d\r\n", hdr->type);
        pbuf_free(p);
        return;
    }

    /*
    printf("Received %d bytes from %s:%d\r\n", p->len, ip4addr_ntoa(addr), port);
    for (int i = 0; i < p->len; i++) {
        printf("%02x ", req_data[i]);
    }
    printf("\r\n");
    for (int i = 0; i < p->len; i++) {
        // check if charactes printable ascii
        if (req_data[i] >= 0x20 && req_data[i] <= 0x7e) {
            printf("%c", req_data[i]);
        } else {
            printf(".");
        }
    }
    printf("\r\n");
    */

    pbuf_free(p);
}

int setup_server()
{
    cyw43_arch_lwip_begin();

    printf("IP address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    pcb = udp_new();
    udp_bind(pcb, IP_ADDR_ANY, 4444);
    udp_recv(pcb, udp_receive, 0);

    cyw43_arch_lwip_end();
}

// poll for wifi status
void wifi_task(void)
{
    static int prev_result = -1;
    int result = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    wifi_up = false;
    switch (result) {
        case CYW43_LINK_DOWN:
            if (prev_result != result) {
                printf("CYW43_LINK_DOWN\r\n");
                // Only reconnect if we were previously connected (not on initial boot)
                if (prev_result == CYW43_LINK_UP) {
                    printf("Attempting to reconnect...\r\n");
                    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK);
                }
            }
            break;
        case CYW43_LINK_JOIN:
            if (prev_result != result) {
                printf("CYW43_LINK_JOIN\r\n");
            }
            break;
        case CYW43_LINK_NOIP:
            if (prev_result != result) {
                printf("CYW43_LINK_NOIP\r\n");
            }
            break;
        case CYW43_LINK_UP:
            wifi_up = true;
            if (prev_result != result) {
                printf("CYW43_LINK_UP\r\n");
                update_blink_state();
                setup_server();
            }
            // TODO: also should unsetup server
            break;
        case CYW43_LINK_FAIL:
            if (prev_result != result) {
                printf("CYW43_LINK_FAIL\r\n");
                // Attempt to reconnect on failure (but not if this is the initial attempt)
                if (prev_result >= 0) {
                    printf("Attempting to reconnect...\r\n");
                    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK);
                }
            }
            break;
        case CYW43_LINK_NONET:
            if (prev_result != result) {
                printf("CYW43_LINK_NONET\r\n");
            }
            break;
        case CYW43_LINK_BADAUTH:
            if (prev_result != result) {
                printf("CYW43_LINK_BADAUTH\r\n");
            }
            break;
    }
    prev_result = result;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
    static uint32_t start_ms = 0;
    static uint16_t prev_blink_state = -1;

    // blink is disabled
    if (!blink_state) {
        return;
    }

    // Blink every interval ms
    if (board_millis() - start_ms < BLINK_STATE_MS) return; // not enough time
    start_ms += BLINK_STATE_MS;

    // rotate 16bit blink_state to right
    blink_state = (blink_state >> 1) | (blink_state << 15);

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_state & 0x01);

    // virtual blinky for remote coding
#if VIRTUAL_BLINKY
    if ((prev_blink_state & 0x01) != (blink_state & 0x01)) {
        prev_blink_state = blink_state;
        printf("\rLoop: [%s] %s ",
                (blink_state & 0x01) ? "*" : "·",
                wifi_up ? ":)" : ":(");
    }
#endif
}