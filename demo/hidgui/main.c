#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <SDL.h>

#define TARGET_IP "192.168.1.10"
#define TARGET_PORT 4444

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

void send_keyboard(uint8_t pressed, uint8_t scancode)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(TARGET_IP);
    servaddr.sin_port = htons(TARGET_PORT);

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
            (struct sockaddr *) &servaddr,
            sizeof(servaddr)) < 0) {
        printf("Error sending packet\n");
    }

    close(sockfd);
}

void send_mouse(
    uint8_t buttons,
    int8_t x,
    int8_t y,
    int8_t vertical,
    int8_t horizontal)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(TARGET_IP);
    servaddr.sin_port = htons(TARGET_PORT);

    mouse_packet packet;
    packet.type = 2; // 2 == mouse
    packet.version = 1;
    packet.buttons = buttons;
    packet.x = x;
    packet.y = y;
    packet.vertical = vertical;
    packet.horizontal = horizontal;

    if (sendto(
            sockfd,
            &packet,
            sizeof(packet),
            0,
            (struct sockaddr *) &servaddr,
            sizeof(servaddr)) < 0) {
        printf("Error sending packet\n");
    }

    close(sockfd);
}

int main()
{
    SDL_Window *window;
    SDL_Surface *surface;

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
    SDL_SetRelativeMouseMode(SDL_TRUE);

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

            case SDL_MOUSEMOTION:
                // printf("Mouse moved: %d, %d\n", event.motion.xrel, event.motion.yrel);
                send_mouse(mouse_buttons, event.motion.xrel, event.motion.yrel, 0, 0);
                break;
            default:
                break;
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}