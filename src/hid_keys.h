/**
 * HID Keys Module
 *
 * Provides key lookup, parsing, and execution for USB HID keys.
 * Uses TinyUSB's hid.h for the actual constant definitions.
 */

#ifndef HID_KEYS_H
#define HID_KEYS_H

#include <stdbool.h>
#include <stdint.h>
#include "class/hid/hid.h"  // TinyUSB HID definitions

// Key types - determines which USB HID report to use
typedef enum {
    HID_KEY_TYPE_KEYBOARD = 0,  // Standard keyboard report
    HID_KEY_TYPE_CONSUMER = 1,  // Consumer control report (media keys)
    HID_KEY_TYPE_SYSTEM = 2,    // System control report (power, sleep, wake)
} hid_key_type_t;

// Key actions
typedef enum {
    HID_ACTION_TAP = 0,     // Press and release
    HID_ACTION_PRESS = 1,   // Press only
    HID_ACTION_RELEASE = 2, // Release only
} hid_action_t;

// Lookup result
typedef struct {
    uint16_t code;
    hid_key_type_t type;
} hid_key_info_t;

/**
 * Look up a key by name.
 *
 * Supports:
 * - Key names: "A", "ENTER", "F1", "CTRL", "VOLUME_UP"
 * - Single characters: "a"-"z", "0"-"9"
 * - Raw hex codes: "0x04", "0xE0" (defaults to keyboard type)
 *
 * Lookup is case-insensitive.
 *
 * @param name  Key name or hex code string
 * @param out   Result struct (code and type)
 * @return      true if found, false otherwise
 */
bool hid_lookup_key(const char *name, hid_key_info_t *out);

/**
 * Parse action string to enum.
 *
 * @param action  "press", "release", "tap", or NULL (defaults to tap)
 * @param out     Result enum
 * @return        true if valid, false otherwise
 */
bool hid_parse_action(const char *action, hid_action_t *out);

/**
 * Execute a key action.
 *
 * @param key_info  Key code and type from hid_lookup_key
 * @param action    Action to perform
 * @return          true if executed, false if unsupported (e.g., system keys)
 */
bool hid_execute_key(const hid_key_info_t *key_info, hid_action_t action);

#endif // HID_KEYS_H
