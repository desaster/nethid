#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <SDL.h>

#define DEFAULT_TARGET_HOST "192.168.1.10"
#define TARGET_PORT 4444

// Resolved target address (set once at startup)
static struct sockaddr_in target_addr;
static const char *target_host = NULL;

#define INHIBIT_SHORTCUTS 1

typedef struct {
    uint8_t type;
    uint8_t version;
    uint8_t pressed;
    uint8_t modifiers;
    uint8_t scancode;
} keypress_packet;

typedef struct {
    uint8_t type;
    uint8_t version;
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t vertical;
    int8_t horizontal;
} mouse_packet;

int resolve_target(void)
{
    target_host = getenv("NETHID_IP");
    if (target_host == NULL) {
        target_host = DEFAULT_TARGET_HOST;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", TARGET_PORT);

    int err = getaddrinfo(target_host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "Failed to resolve %s: %s\n", target_host, gai_strerror(err));
        return -1;
    }

    memcpy(&target_addr, res->ai_addr, sizeof(target_addr));
    freeaddrinfo(res);

    printf("Resolved %s to %s\n", target_host, inet_ntoa(target_addr.sin_addr));
    return 0;
}

void send_keyboard(uint8_t pressed, uint8_t scancode)
{
    int sockfd;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    keypress_packet packet;
    packet.type = 1; // 1 == keyboard
    packet.version = 1;
    packet.pressed = pressed;
    packet.modifiers = 0;
    packet.scancode = scancode;

    if (sendto(
            sockfd,
            &packet,
            sizeof(packet),
            0,
            (struct sockaddr *) &target_addr,
            sizeof(target_addr)) < 0) {
        printf("Error sending packet\n");
    }

    close(sockfd);
}

static int8_t clamp8(int v)
{
    return v > 127 ? 127 : (v < -127 ? -127 : (int8_t)v);
}

void send_mouse(
    uint8_t buttons,
    int x,
    int y,
    int vertical,
    int horizontal)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // Split large movements into multiple packets to avoid int8 overflow
    do {
        mouse_packet packet;
        packet.type = 2; // 2 == mouse
        packet.version = 1;
        packet.buttons = buttons;
        packet.x = clamp8(x);
        packet.y = clamp8(y);
        packet.vertical = clamp8(vertical);
        packet.horizontal = clamp8(horizontal);

        if (sendto(sockfd, &packet, sizeof(packet), 0,
                (struct sockaddr *) &target_addr, sizeof(target_addr)) < 0) {
            printf("Error sending packet\n");
            break;
        }

        x -= packet.x;
        y -= packet.y;
        vertical -= packet.vertical;
        horizontal -= packet.horizontal;
    } while (x != 0 || y != 0 || vertical != 0 || horizontal != 0);

    close(sockfd);
}

int main()
{
    SDL_Window *window;
    SDL_Surface *surface;

    if (resolve_target() < 0) {
        return 1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "Keyboard/Mouse event sender",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640,
        480,
        SDL_WINDOW_SHOWN);

#if INHIBIT_SHORTCUTS
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
#endif

    SDL_SetWindowGrab(window, SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WarpMouseInWindow(window, 320, 240);

    if (window == NULL) {
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Event event;
    int quit = 0;

    int rctrl_held = 0;
    int q_held = 0;

    uint8_t mouse_buttons = 0x00;

    printf("RCTRL-q to quit!\r\n");

    while (!quit) {
        surface = SDL_GetWindowSurface(window);
        SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0x80, 0x80, 0x80));
        SDL_UpdateWindowSurface(window);

        SDL_WaitEvent(&event);

        switch (event.type) {
            case SDL_QUIT:
                quit = 1;
                break;

            case SDL_KEYDOWN:
                if (event.key.repeat) {
                    break;
                }
                if (event.key.keysym.scancode == 228) {
                    rctrl_held = 1;
                }
                if (event.key.keysym.scancode == 20) {
                    q_held = 1;
                }
                if (rctrl_held && q_held) {
                    quit = 1;
                }

                // printf("Key press detected: %d\n", event.key.keysym.scancode);
                send_keyboard(1, event.key.keysym.scancode);
                break;

            case SDL_KEYUP:
                if (event.key.repeat) {
                    break;
                }
                if (event.key.keysym.scancode == 228) {
                    rctrl_held = 0;
                }
                if (event.key.keysym.scancode == 20) {
                    q_held = 0;
                }
                // printf("Key release detected: %d\n", event.key.keysym.scancode);
                send_keyboard(0, event.key.keysym.scancode);
                break;

            case SDL_MOUSEBUTTONDOWN:
                // printf("Mouse button pressed: %d\n", event.button.button);
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        mouse_buttons |= 0x01;
                        break;
                    case SDL_BUTTON_RIGHT:
                        mouse_buttons |= 0x02;
                        break;
                    case SDL_BUTTON_MIDDLE:
                        mouse_buttons |= 0x04;
                        break;
                }

                send_mouse(mouse_buttons, 0, 0, 0, 0);

                break;

            case SDL_MOUSEBUTTONUP:
                // printf("Mouse button released: %d\n", event.button.button);
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        mouse_buttons &= ~0x01;
                        break;
                    case SDL_BUTTON_RIGHT:
                        mouse_buttons &= ~0x02;
                        break;
                    case SDL_BUTTON_MIDDLE:
                        mouse_buttons &= ~0x04;
                        break;
                }

                send_mouse(mouse_buttons, 0, 0, 0, 0);

                break;

            case SDL_MOUSEMOTION: {
                int dx = event.motion.x - 320;
                int dy = event.motion.y - 240;
                if (dx != 0 || dy != 0) {
                    send_mouse(mouse_buttons, dx, dy, 0, 0);
                    SDL_WarpMouseInWindow(window, 320, 240);
                }
                break;
            }
            default:
                break;
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}