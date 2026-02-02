/*
 * MQTT Client for NetHID
 *
 * Wraps lwIP MQTT client with settings integration, automatic reconnection,
 * and HID command processing.
 */

#ifndef __MQTT_H
#define __MQTT_H

#include <stdbool.h>
#include <stdint.h>

//--------------------------------------------------------------------+
// Connection States
//--------------------------------------------------------------------+

typedef enum {
    MQTT_STATE_DISABLED,       // MQTT disabled in settings
    MQTT_STATE_IDLE,           // Waiting for WiFi
    MQTT_STATE_DNS_RESOLVING,  // Resolving broker hostname
    MQTT_STATE_CONNECTING,     // TCP/MQTT connection in progress
    MQTT_STATE_SUBSCRIBING,    // Subscribing to topic
    MQTT_STATE_READY,          // Connected and subscribed
    MQTT_STATE_ERROR,          // Error, will retry after backoff
    MQTT_STATE_BACKOFF         // Waiting before reconnection attempt
} mqtt_state_t;

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

// Initialize MQTT client (call once after WiFi is up)
void mqtt_init(void);

// Poll function - call from main loop
// Handles connection management, reconnection, and keep-alive
void mqtt_task(void);

// Get current connection state
mqtt_state_t mqtt_get_state(void);

// Get human-readable state name
const char* mqtt_state_name(mqtt_state_t state);

// Check if MQTT is connected and ready
bool mqtt_is_ready(void);

// Force disconnect and stop (e.g., when entering AP mode)
void mqtt_stop(void);

// Trigger reconnection (e.g., after settings change)
void mqtt_reconnect(void);

#endif
