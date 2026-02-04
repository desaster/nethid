/*
 * MQTT Client for NetHID
 *
 * Wraps lwIP MQTT client with settings integration, automatic reconnection,
 * and HID command processing.
 */

#include "mqtt.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/dns.h"

#include "settings.h"
#include "board.h"
#include "usb.h"
#include "hid_keys.h"
#include "cjson/cJSON.h"

// Get milliseconds since boot (similar to millis() from TinyUSB)
static inline uint32_t millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

//--------------------------------------------------------------------+
// Configuration
//--------------------------------------------------------------------+

#define MQTT_KEEP_ALIVE_S       60
#define MQTT_QOS                1
#define MQTT_RECONNECT_MIN_MS   1000
#define MQTT_RECONNECT_MAX_MS   60000
#define MQTT_RECONNECT_MULT     2

// LWT settings
#define MQTT_WILL_QOS           1
#define MQTT_WILL_RETAIN        1
#define MQTT_STATUS_SUFFIX      "/status"
#define MQTT_WILL_MSG           "offline"
#define MQTT_ONLINE_MSG         "online"

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

static mqtt_state_t mqtt_state = MQTT_STATE_DISABLED;
static mqtt_client_t *mqtt_client = NULL;
static ip_addr_t mqtt_server_ip;

// Reconnection backoff
static uint32_t reconnect_delay_ms = MQTT_RECONNECT_MIN_MS;
static uint32_t backoff_start_ms = 0;

// Connection info (must persist during connection)
static struct mqtt_connect_client_info_t mqtt_client_info;
static char client_id_buf[MQTT_CLIENT_ID_MAX_LEN + 1];
static char username_buf[MQTT_USERNAME_MAX_LEN + 1];
static char password_buf[MQTT_PASSWORD_MAX_LEN + 1];
static char will_topic_buf[MQTT_TOPIC_MAX_LEN + 16];  // topic + "/status"
static char subscribe_topic_buf[MQTT_TOPIC_MAX_LEN + 4];  // topic + "/#"
static char status_topic_buf[MQTT_TOPIC_MAX_LEN + 16];  // topic + "/status"

// Incoming message state
static char incoming_topic[MQTT_TOPIC_MAX_LEN + 64];
static uint8_t incoming_data[256];
static size_t incoming_data_len = 0;

// Reference to keycodes from usb.c for release_all
extern uint8_t keycodes[6];

// Mouse button state for MQTT
static uint8_t mqtt_mouse_buttons = 0;


//--------------------------------------------------------------------+
// Forward Declarations
//--------------------------------------------------------------------+

static void mqtt_start_connection(void);
static void mqtt_dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg);
static void mqtt_connection_callback(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
static void mqtt_subscribe_callback(void *arg, err_t err);
static void mqtt_incoming_publish_callback(void *arg, const char *topic, u32_t tot_len);
static void mqtt_incoming_data_callback(void *arg, const u8_t *data, u16_t len, u8_t flags);
static void mqtt_publish_callback(void *arg, err_t err);
static void mqtt_process_message(const char *topic, const uint8_t *data, size_t len);
static void mqtt_release_all_keys(void);
static void mqtt_enter_backoff(void);
static void mqtt_set_state(mqtt_state_t new_state);

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void mqtt_init(void)
{
    printf("MQTT: Initializing\r\n");
    mqtt_state = MQTT_STATE_DISABLED;
    mqtt_client = NULL;
    reconnect_delay_ms = MQTT_RECONNECT_MIN_MS;
}

void mqtt_task(void)
{
    // Check if MQTT is enabled in settings
    if (!settings_get_mqtt_enabled()) {
        if (mqtt_state != MQTT_STATE_DISABLED) {
            printf("MQTT: Disabled in settings\r\n");
            mqtt_stop();
            mqtt_state = MQTT_STATE_DISABLED;
        }
        return;
    }

    // Check if WiFi is up
    if (!wifi_up) {
        if (mqtt_state != MQTT_STATE_IDLE && mqtt_state != MQTT_STATE_DISABLED) {
            printf("MQTT: WiFi down, disconnecting\r\n");
            mqtt_stop();
        }
        mqtt_state = MQTT_STATE_IDLE;
        return;
    }

    // State machine
    switch (mqtt_state) {
        case MQTT_STATE_DISABLED:
            // MQTT just enabled, start connecting
            printf("MQTT: Enabled, starting connection\r\n");
            mqtt_state = MQTT_STATE_IDLE;
            // Fall through to IDLE
            // fallthrough

        case MQTT_STATE_IDLE:
            // Start connection process
            mqtt_start_connection();
            break;

        case MQTT_STATE_DNS_RESOLVING:
        case MQTT_STATE_CONNECTING:
        case MQTT_STATE_SUBSCRIBING:
        case MQTT_STATE_READY:
            // These states are handled by callbacks
            break;

        case MQTT_STATE_ERROR:
            // Enter backoff state
            mqtt_enter_backoff();
            break;

        case MQTT_STATE_BACKOFF:
            // Check if backoff period has elapsed
            if (millis() - backoff_start_ms >= reconnect_delay_ms) {
                printf("MQTT: Backoff complete, retrying connection\r\n");
                mqtt_state = MQTT_STATE_IDLE;
            }
            break;
    }
}

mqtt_state_t mqtt_get_state(void)
{
    return mqtt_state;
}

const char* mqtt_state_name(mqtt_state_t state)
{
    switch (state) {
        case MQTT_STATE_DISABLED:      return "disabled";
        case MQTT_STATE_IDLE:          return "idle";
        case MQTT_STATE_DNS_RESOLVING: return "dns_resolving";
        case MQTT_STATE_CONNECTING:    return "connecting";
        case MQTT_STATE_SUBSCRIBING:   return "subscribing";
        case MQTT_STATE_READY:         return "ready";
        case MQTT_STATE_ERROR:         return "error";
        case MQTT_STATE_BACKOFF:       return "backoff";
        default:                       return "unknown";
    }
}

bool mqtt_is_ready(void)
{
    return mqtt_state == MQTT_STATE_READY &&
           mqtt_client != NULL &&
           mqtt_client_is_connected(mqtt_client);
}

void mqtt_stop(void)
{
    printf("MQTT: Stopping\r\n");

    // Release all keys on disconnect
    mqtt_release_all_keys();

    if (mqtt_client != NULL) {
        if (mqtt_client_is_connected(mqtt_client)) {
            mqtt_disconnect(mqtt_client);
        }
        mqtt_client_free(mqtt_client);
        mqtt_client = NULL;
    }

    mqtt_state = MQTT_STATE_DISABLED;
    reconnect_delay_ms = MQTT_RECONNECT_MIN_MS;
}

void mqtt_reconnect(void)
{
    printf("MQTT: Reconnect requested\r\n");

    if (mqtt_client != NULL) {
        if (mqtt_client_is_connected(mqtt_client)) {
            mqtt_disconnect(mqtt_client);
        }
        mqtt_client_free(mqtt_client);
        mqtt_client = NULL;
    }

    reconnect_delay_ms = MQTT_RECONNECT_MIN_MS;
    mqtt_state = MQTT_STATE_IDLE;
}

//--------------------------------------------------------------------+
// Connection Management
//--------------------------------------------------------------------+

static void mqtt_set_state(mqtt_state_t new_state)
{
    if (mqtt_state != new_state) {
        printf("MQTT: State %s -> %s\r\n", mqtt_state_name(mqtt_state), mqtt_state_name(new_state));
        mqtt_state = new_state;
    }
}

static void mqtt_start_connection(void)
{
    char broker[MQTT_BROKER_MAX_LEN + 1];
    char topic[MQTT_TOPIC_MAX_LEN + 1];

    // Get broker from settings
    if (!settings_get_mqtt_broker(broker) || broker[0] == '\0') {
        printf("MQTT: No broker configured\r\n");
        mqtt_set_state(MQTT_STATE_ERROR);
        return;
    }

    // Get topic from settings
    if (!settings_get_mqtt_topic(topic) || topic[0] == '\0') {
        printf("MQTT: No topic configured\r\n");
        mqtt_set_state(MQTT_STATE_ERROR);
        return;
    }

    printf("MQTT: Connecting to %s, topic %s\r\n", broker, topic);

    // Prepare subscribe topic (with /# wildcard)
    snprintf(subscribe_topic_buf, sizeof(subscribe_topic_buf), "%s/#", topic);

    // Prepare status topic for LWT and online message
    snprintf(status_topic_buf, sizeof(status_topic_buf), "%s%s", topic, MQTT_STATUS_SUFFIX);
    snprintf(will_topic_buf, sizeof(will_topic_buf), "%s%s", topic, MQTT_STATUS_SUFFIX);

    // Get client ID (falls back to hostname if not set)
    settings_get_mqtt_client_id(client_id_buf);

    // Get optional username/password
    bool has_username = settings_get_mqtt_username(username_buf);
    bool has_password = settings_get_mqtt_password(password_buf);

    // Setup client info
    memset(&mqtt_client_info, 0, sizeof(mqtt_client_info));
    mqtt_client_info.client_id = client_id_buf;
    mqtt_client_info.client_user = has_username ? username_buf : NULL;
    mqtt_client_info.client_pass = has_password ? password_buf : NULL;
    mqtt_client_info.keep_alive = MQTT_KEEP_ALIVE_S;
    mqtt_client_info.will_topic = will_topic_buf;
    mqtt_client_info.will_msg = MQTT_WILL_MSG;
    mqtt_client_info.will_qos = MQTT_WILL_QOS;
    mqtt_client_info.will_retain = MQTT_WILL_RETAIN;

    printf("MQTT: Client ID: %s\r\n", client_id_buf);

    // Start DNS resolution
    mqtt_set_state(MQTT_STATE_DNS_RESOLVING);

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(broker, &mqtt_server_ip, mqtt_dns_callback, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // DNS was cached, proceed directly
        printf("MQTT: DNS cached: %s\r\n", ipaddr_ntoa(&mqtt_server_ip));
        mqtt_dns_callback(broker, &mqtt_server_ip, NULL);
    } else if (err != ERR_INPROGRESS) {
        printf("MQTT: DNS lookup failed: %d\r\n", err);
        mqtt_set_state(MQTT_STATE_ERROR);
    }
    // ERR_INPROGRESS means callback will be called
}

static void mqtt_dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;

    if (ipaddr == NULL) {
        printf("MQTT: DNS resolution failed\r\n");
        mqtt_set_state(MQTT_STATE_ERROR);
        return;
    }

    mqtt_server_ip = *ipaddr;
    printf("MQTT: Resolved to %s\r\n", ipaddr_ntoa(&mqtt_server_ip));

    // Ensure any previous client is cleaned up
    if (mqtt_client != NULL) {
        printf("MQTT: Cleaning up previous client\r\n");
        mqtt_client_free(mqtt_client);
        mqtt_client = NULL;
    }

    // Create MQTT client
    mqtt_client = mqtt_client_new();
    if (mqtt_client == NULL) {
        printf("MQTT: Failed to create client\r\n");
        mqtt_set_state(MQTT_STATE_ERROR);
        return;
    }

    // Set incoming message callbacks
    mqtt_set_inpub_callback(mqtt_client, mqtt_incoming_publish_callback, mqtt_incoming_data_callback, NULL);

    // Connect to broker
    uint16_t port = settings_get_mqtt_port();
    printf("MQTT: Connecting to %s:%u\r\n", ipaddr_ntoa(&mqtt_server_ip), port);
    mqtt_set_state(MQTT_STATE_CONNECTING);

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(mqtt_client, &mqtt_server_ip, port,
                                     mqtt_connection_callback, NULL, &mqtt_client_info);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("MQTT: Connect call failed: %d\r\n", err);
        mqtt_client_free(mqtt_client);
        mqtt_client = NULL;
        mqtt_set_state(MQTT_STATE_ERROR);
    }
}

static void mqtt_connection_callback(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    (void)arg;

    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("MQTT: Connected!\r\n");

        // Reset reconnect delay on successful connection
        reconnect_delay_ms = MQTT_RECONNECT_MIN_MS;

        // Subscribe to topic
        printf("MQTT: Subscribing to %s\r\n", subscribe_topic_buf);
        mqtt_set_state(MQTT_STATE_SUBSCRIBING);

        cyw43_arch_lwip_begin();
        err_t err = mqtt_subscribe(client, subscribe_topic_buf, MQTT_QOS, mqtt_subscribe_callback, NULL);
        cyw43_arch_lwip_end();

        if (err != ERR_OK) {
            printf("MQTT: Subscribe call failed: %d\r\n", err);
            mqtt_set_state(MQTT_STATE_ERROR);
        }
    } else if (status == MQTT_CONNECT_DISCONNECTED) {
        printf("MQTT: Disconnected\r\n");
        mqtt_release_all_keys();

        if (mqtt_client != NULL) {
            mqtt_client_free(mqtt_client);
            mqtt_client = NULL;
        }

        mqtt_set_state(MQTT_STATE_ERROR);
    } else {
        printf("MQTT: Connection refused, status=%d\r\n", status);

        if (mqtt_client != NULL) {
            mqtt_client_free(mqtt_client);
            mqtt_client = NULL;
        }

        mqtt_set_state(MQTT_STATE_ERROR);
    }
}

static void mqtt_subscribe_callback(void *arg, err_t err)
{
    (void)arg;

    if (err != ERR_OK) {
        printf("MQTT: Subscribe failed: %d\r\n", err);
        mqtt_set_state(MQTT_STATE_ERROR);
        return;
    }

    printf("MQTT: Subscribed successfully\r\n");
    mqtt_set_state(MQTT_STATE_READY);

    // Publish online status
    cyw43_arch_lwip_begin();
    mqtt_publish(mqtt_client, status_topic_buf, MQTT_ONLINE_MSG, strlen(MQTT_ONLINE_MSG),
                 MQTT_QOS, 1, mqtt_publish_callback, NULL);
    cyw43_arch_lwip_end();
}

static void mqtt_publish_callback(void *arg, err_t err)
{
    (void)arg;

    if (err != ERR_OK) {
        printf("MQTT: Publish failed: %d\r\n", err);
    }
}

//--------------------------------------------------------------------+
// Incoming Message Handling
//--------------------------------------------------------------------+

static void mqtt_incoming_publish_callback(void *arg, const char *topic, u32_t tot_len)
{
    (void)arg;

    // Store topic for when data arrives
    strncpy(incoming_topic, topic, sizeof(incoming_topic) - 1);
    incoming_topic[sizeof(incoming_topic) - 1] = '\0';
    incoming_data_len = 0;

    // printf("MQTT: Incoming publish on %s (%lu bytes)\r\n", topic, (unsigned long)tot_len);
    (void)tot_len;
}

static void mqtt_incoming_data_callback(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    (void)arg;

    // Accumulate data
    size_t copy_len = len;
    if (incoming_data_len + copy_len > sizeof(incoming_data) - 1) {
        copy_len = sizeof(incoming_data) - 1 - incoming_data_len;
    }

    if (copy_len > 0) {
        memcpy(incoming_data + incoming_data_len, data, copy_len);
        incoming_data_len += copy_len;
    }

    // Process when complete
    if (flags & MQTT_DATA_FLAG_LAST) {
        incoming_data[incoming_data_len] = '\0';
        mqtt_process_message(incoming_topic, incoming_data, incoming_data_len);
        incoming_data_len = 0;
    }
}

// Process key message: {"key": "A", "action": "tap"} or {"key": "a"}
static void mqtt_handle_key(const uint8_t *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLength((const char *)data, len);
    if (json == NULL) {
        printf("MQTT: Invalid JSON in key message\r\n");
        return;
    }

    cJSON *key_item = cJSON_GetObjectItemCaseSensitive(json, "key");
    if (!cJSON_IsString(key_item) || key_item->valuestring == NULL) {
        printf("MQTT: Missing 'key' field\r\n");
        cJSON_Delete(json);
        return;
    }

    const char *key_name = key_item->valuestring;
    hid_key_info_t key_info;

    if (!hid_lookup_key(key_name, &key_info)) {
        printf("MQTT: Unknown key '%s'\r\n", key_name);
        cJSON_Delete(json);
        return;
    }

    // Optional type override (for raw hex codes)
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (cJSON_IsString(type_item) && type_item->valuestring != NULL) {
        const char *type_str = type_item->valuestring;
        if (strcmp(type_str, "consumer") == 0) {
            key_info.type = HID_KEY_TYPE_CONSUMER;
        } else if (strcmp(type_str, "system") == 0) {
            key_info.type = HID_KEY_TYPE_SYSTEM;
        } else if (strcmp(type_str, "keyboard") != 0) {
            printf("MQTT: Invalid type '%s'\r\n", type_str);
            cJSON_Delete(json);
            return;
        }
    }

    // Parse action (optional, defaults to tap)
    cJSON *action_item = cJSON_GetObjectItemCaseSensitive(json, "action");
    const char *action_str = cJSON_IsString(action_item) ? action_item->valuestring : NULL;
    hid_action_t action;
    if (!hid_parse_action(action_str, &action)) {
        printf("MQTT: Invalid action '%s'\r\n", action_str);
        cJSON_Delete(json);
        return;
    }

    const char *type_str = key_info.type == HID_KEY_TYPE_CONSUMER ? "consumer" :
                           key_info.type == HID_KEY_TYPE_SYSTEM ? "system" : "keyboard";
    const char *action_names[] = {"tap", "press", "release"};
    printf("MQTT: Key %s (0x%04X, %s) %s\r\n",
           key_name, key_info.code, type_str, action_names[action]);

    if (!hid_execute_key(&key_info, action)) {
        printf("MQTT: System keys not yet implemented\r\n");
    }

    cJSON_Delete(json);
}

// Process mouse move message: {"x": 10, "y": -5}
static void mqtt_handle_mouse_move(const uint8_t *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLength((const char *)data, len);
    if (json == NULL) {
        printf("MQTT: Invalid JSON in mouse/move message\r\n");
        return;
    }

    int x = 0, y = 0;
    cJSON *x_item = cJSON_GetObjectItemCaseSensitive(json, "x");
    cJSON *y_item = cJSON_GetObjectItemCaseSensitive(json, "y");

    if (cJSON_IsNumber(x_item)) x = x_item->valueint;
    if (cJSON_IsNumber(y_item)) y = y_item->valueint;

    // Clamp to int16 range, then split into int8-sized HID reports
    if (x < -32768) x = -32768;
    if (x > 32767) x = 32767;
    if (y < -32768) y = -32768;
    if (y > 32767) y = 32767;

    move_mouse(mqtt_mouse_buttons, (int16_t)x, (int16_t)y, 0, 0);

    cJSON_Delete(json);
}

// Process mouse button message: {"button": 1, "down": true} or {"button": "left", "down": false}
static void mqtt_handle_mouse_button(const uint8_t *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLength((const char *)data, len);
    if (json == NULL) {
        printf("MQTT: Invalid JSON in mouse/button message\r\n");
        return;
    }

    cJSON *button_item = cJSON_GetObjectItemCaseSensitive(json, "button");
    int button_bit = 0;

    if (cJSON_IsNumber(button_item)) {
        button_bit = button_item->valueint;
    } else if (cJSON_IsString(button_item)) {
        const char *btn_name = button_item->valuestring;
        if (strcasecmp(btn_name, "left") == 0 || strcmp(btn_name, "1") == 0) {
            button_bit = 1;
        } else if (strcasecmp(btn_name, "right") == 0 || strcmp(btn_name, "2") == 0) {
            button_bit = 2;
        } else if (strcasecmp(btn_name, "middle") == 0 || strcmp(btn_name, "3") == 0) {
            button_bit = 4;
        } else {
            printf("MQTT: Unknown button '%s'\r\n", btn_name);
            cJSON_Delete(json);
            return;
        }
    } else {
        printf("MQTT: Missing or invalid 'button' field\r\n");
        cJSON_Delete(json);
        return;
    }

    if (button_bit < 1 || button_bit > 31) {
        printf("MQTT: Invalid button value %d\r\n", button_bit);
        cJSON_Delete(json);
        return;
    }

    // Check for "down" field (optional, defaults to click if not present)
    cJSON *down_item = cJSON_GetObjectItemCaseSensitive(json, "down");
    bool has_down = cJSON_IsBool(down_item);
    bool is_down = has_down ? cJSON_IsTrue(down_item) : true;

    if (!has_down) {
        // Click: press and release
        mqtt_mouse_buttons |= button_bit;
        move_mouse(mqtt_mouse_buttons, 0, 0, 0, 0);
        mqtt_mouse_buttons &= ~button_bit;
        move_mouse(mqtt_mouse_buttons, 0, 0, 0, 0);
    } else if (is_down) {
        mqtt_mouse_buttons |= button_bit;
        move_mouse(mqtt_mouse_buttons, 0, 0, 0, 0);
    } else {
        mqtt_mouse_buttons &= ~button_bit;
        move_mouse(mqtt_mouse_buttons, 0, 0, 0, 0);
    }

    cJSON_Delete(json);
}

// Process scroll message: {"x": 0, "y": -3}
static void mqtt_handle_scroll(const uint8_t *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLength((const char *)data, len);
    if (json == NULL) {
        printf("MQTT: Invalid JSON in scroll message\r\n");
        return;
    }

    int x = 0, y = 0;
    cJSON *x_item = cJSON_GetObjectItemCaseSensitive(json, "x");
    cJSON *y_item = cJSON_GetObjectItemCaseSensitive(json, "y");

    if (cJSON_IsNumber(x_item)) x = x_item->valueint;
    if (cJSON_IsNumber(y_item)) y = y_item->valueint;

    // Clamp to int8 range
    if (x < -127) x = -127;
    if (x > 127) x = 127;
    if (y < -127) y = -127;
    if (y > 127) y = 127;

    // Note: scroll uses vertical (y) and horizontal (x) parameters
    move_mouse(mqtt_mouse_buttons, 0, 0, (int8_t)y, (int8_t)x);

    cJSON_Delete(json);
}

static void mqtt_process_message(const char *topic, const uint8_t *data, size_t len)
{
    // Extract subtopic (part after base topic)
    char base_topic[MQTT_TOPIC_MAX_LEN + 1];
    if (!settings_get_mqtt_topic(base_topic)) {
        return;
    }

    size_t base_len = strlen(base_topic);
    if (strncmp(topic, base_topic, base_len) != 0) {
        return;  // Topic doesn't match our base
    }

    const char *subtopic = topic + base_len;
    if (*subtopic == '/') {
        subtopic++;  // Skip leading slash
    }

    // Route to appropriate handler
    if (strcmp(subtopic, "key") == 0) {
        mqtt_handle_key(data, len);
    } else if (strcmp(subtopic, "mouse/move") == 0) {
        mqtt_handle_mouse_move(data, len);
    } else if (strcmp(subtopic, "mouse/button") == 0) {
        mqtt_handle_mouse_button(data, len);
    } else if (strcmp(subtopic, "scroll") == 0) {
        mqtt_handle_scroll(data, len);
    } else if (strcmp(subtopic, "release") == 0) {
        mqtt_release_all_keys();
    } else if (strcmp(subtopic, "status") == 0) {
        // Ignore our own status messages
    } else {
        printf("MQTT: Unknown subtopic '%s'\r\n", subtopic);
    }
}

//--------------------------------------------------------------------+
// HID Integration
//--------------------------------------------------------------------+

static void mqtt_release_all_keys(void)
{
    printf("MQTT: Releasing all keys and buttons\r\n");

    // Release all keyboard keys
    for (int i = 0; i < 6; i++) {
        if (keycodes[i] != 0) {
            depress_key(keycodes[i]);
        }
    }

    // Release all mouse buttons
    mqtt_mouse_buttons = 0;
    move_mouse(0, 0, 0, 0, 0);

    // Release consumer control
    release_consumer();
}

//--------------------------------------------------------------------+
// Reconnection Backoff
//--------------------------------------------------------------------+

static void mqtt_enter_backoff(void)
{
    printf("MQTT: Entering backoff, delay=%lu ms\r\n", (unsigned long)reconnect_delay_ms);

    backoff_start_ms = millis();
    mqtt_set_state(MQTT_STATE_BACKOFF);

    // Increase delay for next time (exponential backoff)
    reconnect_delay_ms *= MQTT_RECONNECT_MULT;
    if (reconnect_delay_ms > MQTT_RECONNECT_MAX_MS) {
        reconnect_delay_ms = MQTT_RECONNECT_MAX_MS;
    }
}
