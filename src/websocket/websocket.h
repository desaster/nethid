/*
 * WebSocket server for HID control
 *
 * Implements RFC 6455 WebSocket protocol (minimal subset) for low-latency
 * binary HID command transmission.
 */

#ifndef __WEBSOCKET_H
#define __WEBSOCKET_H

#include <stdint.h>
#include <stdbool.h>

// WebSocket server port (separate from HTTP on port 80)
#define WEBSOCKET_PORT 8081

// Initialize the WebSocket server
// Call after network is up
void websocket_init(void);

// Check if a WebSocket client is connected
bool websocket_client_connected(void);

// Release all HID keys/buttons (called on disconnect)
void websocket_release_all(void);

#endif /* __WEBSOCKET_H */
