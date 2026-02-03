/**
 * HID Keys Module
 *
 * Key lookup, parsing, and execution for USB HID keys.
 */

#include "hid_keys.h"
#include "usb.h"
#include <string.h>
#include <stdlib.h>

// Table entry
typedef struct {
    const char *name;
    uint16_t code;
    hid_key_type_t type;
} key_entry_t;

// Lookup table mapping key names to HID codes.
// Reference: TinyUSB hid.h ($PICO_SDK_PATH/lib/tinyusb/src/class/hid/hid.h)
// - Keyboard: HID_KEY_*
// - Consumer: HID_USAGE_CONSUMER_*
// - System: HID_USAGE_DESKTOP_SYSTEM_*
static const key_entry_t key_table[] = {
    //--------------------------------------------------------------------
    // Letters (short names)
    //--------------------------------------------------------------------
    {"A", HID_KEY_A, HID_KEY_TYPE_KEYBOARD},
    {"B", HID_KEY_B, HID_KEY_TYPE_KEYBOARD},
    {"C", HID_KEY_C, HID_KEY_TYPE_KEYBOARD},
    {"D", HID_KEY_D, HID_KEY_TYPE_KEYBOARD},
    {"E", HID_KEY_E, HID_KEY_TYPE_KEYBOARD},
    {"F", HID_KEY_F, HID_KEY_TYPE_KEYBOARD},
    {"G", HID_KEY_G, HID_KEY_TYPE_KEYBOARD},
    {"H", HID_KEY_H, HID_KEY_TYPE_KEYBOARD},
    {"I", HID_KEY_I, HID_KEY_TYPE_KEYBOARD},
    {"J", HID_KEY_J, HID_KEY_TYPE_KEYBOARD},
    {"K", HID_KEY_K, HID_KEY_TYPE_KEYBOARD},
    {"L", HID_KEY_L, HID_KEY_TYPE_KEYBOARD},
    {"M", HID_KEY_M, HID_KEY_TYPE_KEYBOARD},
    {"N", HID_KEY_N, HID_KEY_TYPE_KEYBOARD},
    {"O", HID_KEY_O, HID_KEY_TYPE_KEYBOARD},
    {"P", HID_KEY_P, HID_KEY_TYPE_KEYBOARD},
    {"Q", HID_KEY_Q, HID_KEY_TYPE_KEYBOARD},
    {"R", HID_KEY_R, HID_KEY_TYPE_KEYBOARD},
    {"S", HID_KEY_S, HID_KEY_TYPE_KEYBOARD},
    {"T", HID_KEY_T, HID_KEY_TYPE_KEYBOARD},
    {"U", HID_KEY_U, HID_KEY_TYPE_KEYBOARD},
    {"V", HID_KEY_V, HID_KEY_TYPE_KEYBOARD},
    {"W", HID_KEY_W, HID_KEY_TYPE_KEYBOARD},
    {"X", HID_KEY_X, HID_KEY_TYPE_KEYBOARD},
    {"Y", HID_KEY_Y, HID_KEY_TYPE_KEYBOARD},
    {"Z", HID_KEY_Z, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Numbers
    //--------------------------------------------------------------------
    {"1", HID_KEY_1, HID_KEY_TYPE_KEYBOARD},
    {"2", HID_KEY_2, HID_KEY_TYPE_KEYBOARD},
    {"3", HID_KEY_3, HID_KEY_TYPE_KEYBOARD},
    {"4", HID_KEY_4, HID_KEY_TYPE_KEYBOARD},
    {"5", HID_KEY_5, HID_KEY_TYPE_KEYBOARD},
    {"6", HID_KEY_6, HID_KEY_TYPE_KEYBOARD},
    {"7", HID_KEY_7, HID_KEY_TYPE_KEYBOARD},
    {"8", HID_KEY_8, HID_KEY_TYPE_KEYBOARD},
    {"9", HID_KEY_9, HID_KEY_TYPE_KEYBOARD},
    {"0", HID_KEY_0, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Special keys
    //--------------------------------------------------------------------
    {"ENTER", HID_KEY_ENTER, HID_KEY_TYPE_KEYBOARD},
    {"RETURN", HID_KEY_ENTER, HID_KEY_TYPE_KEYBOARD},
    {"ESCAPE", HID_KEY_ESCAPE, HID_KEY_TYPE_KEYBOARD},
    {"ESC", HID_KEY_ESCAPE, HID_KEY_TYPE_KEYBOARD},
    {"BACKSPACE", HID_KEY_BACKSPACE, HID_KEY_TYPE_KEYBOARD},
    {"TAB", HID_KEY_TAB, HID_KEY_TYPE_KEYBOARD},
    {"SPACE", HID_KEY_SPACE, HID_KEY_TYPE_KEYBOARD},
    {"MINUS", HID_KEY_MINUS, HID_KEY_TYPE_KEYBOARD},
    {"EQUAL", HID_KEY_EQUAL, HID_KEY_TYPE_KEYBOARD},
    {"BRACKET_LEFT", HID_KEY_BRACKET_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"BRACKET_RIGHT", HID_KEY_BRACKET_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"BACKSLASH", HID_KEY_BACKSLASH, HID_KEY_TYPE_KEYBOARD},
    {"SEMICOLON", HID_KEY_SEMICOLON, HID_KEY_TYPE_KEYBOARD},
    {"APOSTROPHE", HID_KEY_APOSTROPHE, HID_KEY_TYPE_KEYBOARD},
    {"QUOTE", HID_KEY_APOSTROPHE, HID_KEY_TYPE_KEYBOARD},
    {"GRAVE", HID_KEY_GRAVE, HID_KEY_TYPE_KEYBOARD},
    {"BACKTICK", HID_KEY_GRAVE, HID_KEY_TYPE_KEYBOARD},
    {"COMMA", HID_KEY_COMMA, HID_KEY_TYPE_KEYBOARD},
    {"PERIOD", HID_KEY_PERIOD, HID_KEY_TYPE_KEYBOARD},
    {"DOT", HID_KEY_PERIOD, HID_KEY_TYPE_KEYBOARD},
    {"SLASH", HID_KEY_SLASH, HID_KEY_TYPE_KEYBOARD},
    {"CAPS_LOCK", HID_KEY_CAPS_LOCK, HID_KEY_TYPE_KEYBOARD},
    {"CAPSLOCK", HID_KEY_CAPS_LOCK, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Function keys
    //--------------------------------------------------------------------
    {"F1", HID_KEY_F1, HID_KEY_TYPE_KEYBOARD},
    {"F2", HID_KEY_F2, HID_KEY_TYPE_KEYBOARD},
    {"F3", HID_KEY_F3, HID_KEY_TYPE_KEYBOARD},
    {"F4", HID_KEY_F4, HID_KEY_TYPE_KEYBOARD},
    {"F5", HID_KEY_F5, HID_KEY_TYPE_KEYBOARD},
    {"F6", HID_KEY_F6, HID_KEY_TYPE_KEYBOARD},
    {"F7", HID_KEY_F7, HID_KEY_TYPE_KEYBOARD},
    {"F8", HID_KEY_F8, HID_KEY_TYPE_KEYBOARD},
    {"F9", HID_KEY_F9, HID_KEY_TYPE_KEYBOARD},
    {"F10", HID_KEY_F10, HID_KEY_TYPE_KEYBOARD},
    {"F11", HID_KEY_F11, HID_KEY_TYPE_KEYBOARD},
    {"F12", HID_KEY_F12, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Navigation
    //--------------------------------------------------------------------
    {"PRINT_SCREEN", HID_KEY_PRINT_SCREEN, HID_KEY_TYPE_KEYBOARD},
    {"PRINTSCREEN", HID_KEY_PRINT_SCREEN, HID_KEY_TYPE_KEYBOARD},
    {"SCROLL_LOCK", HID_KEY_SCROLL_LOCK, HID_KEY_TYPE_KEYBOARD},
    {"SCROLLLOCK", HID_KEY_SCROLL_LOCK, HID_KEY_TYPE_KEYBOARD},
    {"PAUSE", HID_KEY_PAUSE, HID_KEY_TYPE_KEYBOARD},
    {"INSERT", HID_KEY_INSERT, HID_KEY_TYPE_KEYBOARD},
    {"HOME", HID_KEY_HOME, HID_KEY_TYPE_KEYBOARD},
    {"PAGE_UP", HID_KEY_PAGE_UP, HID_KEY_TYPE_KEYBOARD},
    {"PAGEUP", HID_KEY_PAGE_UP, HID_KEY_TYPE_KEYBOARD},
    {"DELETE", HID_KEY_DELETE, HID_KEY_TYPE_KEYBOARD},
    {"END", HID_KEY_END, HID_KEY_TYPE_KEYBOARD},
    {"PAGE_DOWN", HID_KEY_PAGE_DOWN, HID_KEY_TYPE_KEYBOARD},
    {"PAGEDOWN", HID_KEY_PAGE_DOWN, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Arrow keys
    //--------------------------------------------------------------------
    {"ARROW_RIGHT", HID_KEY_ARROW_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"ARROW_LEFT", HID_KEY_ARROW_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"ARROW_DOWN", HID_KEY_ARROW_DOWN, HID_KEY_TYPE_KEYBOARD},
    {"ARROW_UP", HID_KEY_ARROW_UP, HID_KEY_TYPE_KEYBOARD},
    {"RIGHT", HID_KEY_ARROW_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"LEFT", HID_KEY_ARROW_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"DOWN", HID_KEY_ARROW_DOWN, HID_KEY_TYPE_KEYBOARD},
    {"UP", HID_KEY_ARROW_UP, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Keypad
    //--------------------------------------------------------------------
    {"NUM_LOCK", HID_KEY_NUM_LOCK, HID_KEY_TYPE_KEYBOARD},
    {"NUMLOCK", HID_KEY_NUM_LOCK, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_DIVIDE", HID_KEY_KEYPAD_DIVIDE, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_MULTIPLY", HID_KEY_KEYPAD_MULTIPLY, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_SUBTRACT", HID_KEY_KEYPAD_SUBTRACT, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_ADD", HID_KEY_KEYPAD_ADD, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_ENTER", HID_KEY_KEYPAD_ENTER, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_1", HID_KEY_KEYPAD_1, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_2", HID_KEY_KEYPAD_2, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_3", HID_KEY_KEYPAD_3, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_4", HID_KEY_KEYPAD_4, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_5", HID_KEY_KEYPAD_5, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_6", HID_KEY_KEYPAD_6, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_7", HID_KEY_KEYPAD_7, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_8", HID_KEY_KEYPAD_8, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_9", HID_KEY_KEYPAD_9, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_0", HID_KEY_KEYPAD_0, HID_KEY_TYPE_KEYBOARD},
    {"KEYPAD_DECIMAL", HID_KEY_KEYPAD_DECIMAL, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Modifiers (left)
    //--------------------------------------------------------------------
    {"CONTROL_LEFT", HID_KEY_CONTROL_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"CTRL_LEFT", HID_KEY_CONTROL_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"CTRL", HID_KEY_CONTROL_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"SHIFT_LEFT", HID_KEY_SHIFT_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"SHIFT", HID_KEY_SHIFT_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"ALT_LEFT", HID_KEY_ALT_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"ALT", HID_KEY_ALT_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"GUI_LEFT", HID_KEY_GUI_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"GUI", HID_KEY_GUI_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"WIN", HID_KEY_GUI_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"SUPER", HID_KEY_GUI_LEFT, HID_KEY_TYPE_KEYBOARD},
    {"META", HID_KEY_GUI_LEFT, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Modifiers (right)
    //--------------------------------------------------------------------
    {"CONTROL_RIGHT", HID_KEY_CONTROL_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"CTRL_RIGHT", HID_KEY_CONTROL_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"SHIFT_RIGHT", HID_KEY_SHIFT_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"ALT_RIGHT", HID_KEY_ALT_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"ALTGR", HID_KEY_ALT_RIGHT, HID_KEY_TYPE_KEYBOARD},
    {"GUI_RIGHT", HID_KEY_GUI_RIGHT, HID_KEY_TYPE_KEYBOARD},

    //--------------------------------------------------------------------
    // Consumer control (media)
    //--------------------------------------------------------------------
    {"PLAY_PAUSE", HID_USAGE_CONSUMER_PLAY_PAUSE, HID_KEY_TYPE_CONSUMER},
    {"NEXT_TRACK", HID_USAGE_CONSUMER_SCAN_NEXT, HID_KEY_TYPE_CONSUMER},
    {"PREV_TRACK", HID_USAGE_CONSUMER_SCAN_PREVIOUS, HID_KEY_TYPE_CONSUMER},
    {"STOP", HID_USAGE_CONSUMER_STOP, HID_KEY_TYPE_CONSUMER},
    {"MUTE", HID_USAGE_CONSUMER_MUTE, HID_KEY_TYPE_CONSUMER},
    {"VOLUME_UP", HID_USAGE_CONSUMER_VOLUME_INCREMENT, HID_KEY_TYPE_CONSUMER},
    {"VOLUME_DOWN", HID_USAGE_CONSUMER_VOLUME_DECREMENT, HID_KEY_TYPE_CONSUMER},
    {"VOL_UP", HID_USAGE_CONSUMER_VOLUME_INCREMENT, HID_KEY_TYPE_CONSUMER},
    {"VOL_DOWN", HID_USAGE_CONSUMER_VOLUME_DECREMENT, HID_KEY_TYPE_CONSUMER},

    //--------------------------------------------------------------------
    // Consumer control (applications)
    //--------------------------------------------------------------------
    {"CALCULATOR", HID_USAGE_CONSUMER_AL_CALCULATOR, HID_KEY_TYPE_CONSUMER},
    {"CALC", HID_USAGE_CONSUMER_AL_CALCULATOR, HID_KEY_TYPE_CONSUMER},
    {"BROWSER", HID_USAGE_CONSUMER_AL_LOCAL_BROWSER, HID_KEY_TYPE_CONSUMER},
    {"MAIL", HID_USAGE_CONSUMER_AL_EMAIL_READER, HID_KEY_TYPE_CONSUMER},
    {"EMAIL", HID_USAGE_CONSUMER_AL_EMAIL_READER, HID_KEY_TYPE_CONSUMER},

    //--------------------------------------------------------------------
    // Consumer control (browser)
    //--------------------------------------------------------------------
    {"BROWSER_BACK", HID_USAGE_CONSUMER_AC_BACK, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_FORWARD", HID_USAGE_CONSUMER_AC_FORWARD, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_REFRESH", HID_USAGE_CONSUMER_AC_REFRESH, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_STOP", HID_USAGE_CONSUMER_AC_STOP, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_SEARCH", HID_USAGE_CONSUMER_AC_SEARCH, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_HOME", HID_USAGE_CONSUMER_AC_HOME, HID_KEY_TYPE_CONSUMER},
    {"BROWSER_BOOKMARKS", HID_USAGE_CONSUMER_AC_BOOKMARKS, HID_KEY_TYPE_CONSUMER},

    //--------------------------------------------------------------------
    // Consumer control (brightness)
    //--------------------------------------------------------------------
    {"BRIGHTNESS_UP", HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT, HID_KEY_TYPE_CONSUMER},
    {"BRIGHTNESS_DOWN", HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT, HID_KEY_TYPE_CONSUMER},

    //--------------------------------------------------------------------
    // System control
    //--------------------------------------------------------------------
    {"POWER", HID_USAGE_DESKTOP_SYSTEM_POWER_DOWN, HID_KEY_TYPE_SYSTEM},
    {"SLEEP", HID_USAGE_DESKTOP_SYSTEM_SLEEP, HID_KEY_TYPE_SYSTEM},
    {"WAKE", HID_USAGE_DESKTOP_SYSTEM_WAKE_UP, HID_KEY_TYPE_SYSTEM},

    // End marker
    {NULL, 0, HID_KEY_TYPE_KEYBOARD}
};

bool hid_lookup_key(const char *name, hid_key_info_t *out)
{
    if (name == NULL || name[0] == '\0' || out == NULL) {
        return false;
    }

    // Handle single character: a-z, A-Z, 0-9
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'a' && c <= 'z') {
            out->code = HID_KEY_A + (c - 'a');
            out->type = HID_KEY_TYPE_KEYBOARD;
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            out->code = HID_KEY_A + (c - 'A');
            out->type = HID_KEY_TYPE_KEYBOARD;
            return true;
        }
        if (c >= '1' && c <= '9') {
            out->code = HID_KEY_1 + (c - '1');
            out->type = HID_KEY_TYPE_KEYBOARD;
            return true;
        }
        if (c == '0') {
            out->code = HID_KEY_0;
            out->type = HID_KEY_TYPE_KEYBOARD;
            return true;
        }
    }

    // Handle hex codes: 0x04, 0xE0, etc.
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        char *endptr;
        long val = strtol(name, &endptr, 16);
        if (*endptr == '\0' && val >= 0 && val <= 0xFFFF) {
            out->code = (uint16_t)val;
            out->type = HID_KEY_TYPE_KEYBOARD;  // Default to keyboard for raw hex
            return true;
        }
    }

    // Table lookup (case-insensitive)
    for (const key_entry_t *entry = key_table; entry->name != NULL; entry++) {
        if (strcasecmp(name, entry->name) == 0) {
            out->code = entry->code;
            out->type = entry->type;
            return true;
        }
    }

    return false;
}

bool hid_parse_action(const char *action, hid_action_t *out)
{
    if (action == NULL || strcmp(action, "tap") == 0) {
        *out = HID_ACTION_TAP;
        return true;
    }
    if (strcmp(action, "press") == 0) {
        *out = HID_ACTION_PRESS;
        return true;
    }
    if (strcmp(action, "release") == 0) {
        *out = HID_ACTION_RELEASE;
        return true;
    }
    return false;
}

bool hid_execute_key(const hid_key_info_t *key_info, hid_action_t action)
{
    if (key_info->type == HID_KEY_TYPE_CONSUMER) {
        if (action == HID_ACTION_TAP || action == HID_ACTION_PRESS) {
            press_consumer(key_info->code);
        }
        if (action == HID_ACTION_TAP || action == HID_ACTION_RELEASE) {
            release_consumer();
        }
        return true;
    }

    if (key_info->type == HID_KEY_TYPE_SYSTEM) {
        // TODO: Implement system key support
        return false;
    }

    // Keyboard
    if (action == HID_ACTION_TAP || action == HID_ACTION_PRESS) {
        press_key(key_info->code);
    }
    if (action == HID_ACTION_TAP || action == HID_ACTION_RELEASE) {
        depress_key(key_info->code);
    }
    return true;
}
