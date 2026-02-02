/** Keyboard Layout Definitions - KLE JSON layouts with HID code mappings */

import type { KeyboardLayout, KeyAction } from './keyboard';

/* eslint object-property-newline: "error" */
// HID Scancodes
export const HID = {
    A: 0x04,
    B: 0x05,
    C: 0x06,
    D: 0x07,
    E: 0x08,
    F: 0x09,
    G: 0x0A,
    H: 0x0B,
    I: 0x0C,
    J: 0x0D,
    K: 0x0E,
    L: 0x0F,
    M: 0x10,
    N: 0x11,
    O: 0x12,
    P: 0x13,
    Q: 0x14,
    R: 0x15,
    S: 0x16,
    T: 0x17,
    U: 0x18,
    V: 0x19,
    W: 0x1A,
    X: 0x1B,
    Y: 0x1C,
    Z: 0x1D,
    N1: 0x1E,
    N2: 0x1F,
    N3: 0x20,
    N4: 0x21,
    N5: 0x22,
    N6: 0x23,
    N7: 0x24,
    N8: 0x25,
    N9: 0x26,
    N0: 0x27,
    ENTER: 0x28,
    ESCAPE: 0x29,
    BACKSPACE: 0x2A,
    TAB: 0x2B,
    SPACE: 0x2C,
    MINUS: 0x2D,
    EQUAL: 0x2E,
    LBRACKET: 0x2F,
    RBRACKET: 0x30,
    BACKSLASH: 0x31,
    NONUS_HASH: 0x32,
    SEMICOLON: 0x33,
    QUOTE: 0x34,
    GRAVE: 0x35,
    COMMA: 0x36,
    PERIOD: 0x37,
    SLASH: 0x38,
    CAPS_LOCK: 0x39,
    NUM_LOCK: 0x53,
    SCROLL_LOCK: 0x47,
    F1: 0x3A,
    F2: 0x3B,
    F3: 0x3C,
    F4: 0x3D,
    F5: 0x3E,
    F6: 0x3F,
    F7: 0x40,
    F8: 0x41,
    F9: 0x42,
    F10: 0x43,
    F11: 0x44,
    F12: 0x45,
    PRINT_SCREEN: 0x46,
    PAUSE: 0x48,
    INSERT: 0x49,
    HOME: 0x4A,
    PAGE_UP: 0x4B,
    DELETE: 0x4C,
    END: 0x4D,
    PAGE_DOWN: 0x4E,
    RIGHT: 0x4F,
    LEFT: 0x50,
    DOWN: 0x51,
    UP: 0x52,
    NONUS_BACKSLASH: 0x64,  // ISO key between left shift and Z
    LCTRL: 0xE0,
    LSHIFT: 0xE1,
    LALT: 0xE2,
    LGUI: 0xE3,
    RCTRL: 0xE4,
    RSHIFT: 0xE5,
    RALT: 0xE6,
    RGUI: 0xE7,
};
/* eslint-disable object-property-newline */

// Consumer Control Codes (Media Keys)
export const CONSUMER = {
    PLAY_PAUSE: 0x00CD, NEXT_TRACK: 0x00B5, PREV_TRACK: 0x00B6, STOP: 0x00B7,
    MUTE: 0x00E2, VOLUME_UP: 0x00E9, VOLUME_DOWN: 0x00EA,
    WWW_HOME: 0x0223, WWW_BACK: 0x0224, WWW_FORWARD: 0x0225, WWW_REFRESH: 0x0227,
};

const hid = (code: number): KeyAction => ({ type: 'hid', code });
const mod = (code: number): KeyAction => ({ type: 'modifier', code, toggle: true });
const consumer = (code: number): KeyAction => ({ type: 'consumer', code });
const noop = (): KeyAction => ({ type: 'noop' });

// TKL (Tenkeyless) Main Layout - 87 keys with shift legends
export const TKL_MAIN: KeyboardLayout = {
    id: 'tkl-main',
    name: 'Main',
    kle: [
        ["Esc", { x: 1 }, "F1", "F2", "F3", "F4", { x: 0.5 }, "F5", "F6", "F7", "F8", { x: 0.5 }, "F9", "F10", "F11", "F12", { x: 0.25 }, "PrtSc", "ScrLk", "Pause\nBreak"],
        [{ y: 0.5 }, "~\n`", "!\n1", "@\n2", "#\n3", "$\n4", "%\n5", "^\n6", "&\n7", "*\n8", "(\n9", ")\n0", "_\n-", "+\n=", { w: 2 }, "Backspace", { x: 0.25 }, "Ins", "Home", "PgUp"],
        [{ w: 1.5 }, "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{\n[", "}\n]", { w: 1.5 }, "|\n\\", { x: 0.25 }, "Del", "End", "PgDn"],
        [{ w: 1.75 }, "Caps", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":\n;", "\"\n'", { w: 2.25 }, "Enter"],
        [{ w: 2.25 }, "Shift", "Z", "X", "C", "V", "B", "N", "M", "<\n,", ">\n.", "?\n/", { w: 2.75 }, "Shift", { x: 1.25 }, "↑"],
        [{ w: 1.25 }, "Ctrl", { w: 1.25 }, "Win", { w: 1.25 }, "Alt", { w: 6.25 }, "", { w: 1.25 }, "Alt", { w: 1.25 }, "Win", { w: 1.25 }, "Menu", { w: 1.25 }, "Ctrl", { x: 0.25 }, "←", "↓", "→"],
    ],
    mapping: [
        hid(HID.ESCAPE), hid(HID.F1), hid(HID.F2), hid(HID.F3), hid(HID.F4),
        hid(HID.F5), hid(HID.F6), hid(HID.F7), hid(HID.F8),
        hid(HID.F9), hid(HID.F10), hid(HID.F11), hid(HID.F12),
        hid(HID.PRINT_SCREEN), hid(HID.SCROLL_LOCK), hid(HID.PAUSE),
        hid(HID.GRAVE), hid(HID.N1), hid(HID.N2), hid(HID.N3), hid(HID.N4), hid(HID.N5),
        hid(HID.N6), hid(HID.N7), hid(HID.N8), hid(HID.N9), hid(HID.N0),
        hid(HID.MINUS), hid(HID.EQUAL), hid(HID.BACKSPACE),
        hid(HID.INSERT), hid(HID.HOME), hid(HID.PAGE_UP),
        hid(HID.TAB), hid(HID.Q), hid(HID.W), hid(HID.E), hid(HID.R), hid(HID.T),
        hid(HID.Y), hid(HID.U), hid(HID.I), hid(HID.O), hid(HID.P),
        hid(HID.LBRACKET), hid(HID.RBRACKET), hid(HID.BACKSLASH),
        hid(HID.DELETE), hid(HID.END), hid(HID.PAGE_DOWN),
        hid(HID.CAPS_LOCK), hid(HID.A), hid(HID.S), hid(HID.D), hid(HID.F), hid(HID.G),
        hid(HID.H), hid(HID.J), hid(HID.K), hid(HID.L),
        hid(HID.SEMICOLON), hid(HID.QUOTE), hid(HID.ENTER),
        mod(HID.LSHIFT), hid(HID.Z), hid(HID.X), hid(HID.C), hid(HID.V), hid(HID.B),
        hid(HID.N), hid(HID.M), hid(HID.COMMA), hid(HID.PERIOD), hid(HID.SLASH),
        mod(HID.RSHIFT), hid(HID.UP),
        mod(HID.LCTRL), mod(HID.LGUI), mod(HID.LALT), hid(HID.SPACE),
        mod(HID.RALT), mod(HID.RGUI), noop(), mod(HID.RCTRL),
        hid(HID.LEFT), hid(HID.DOWN), hid(HID.RIGHT),
    ],
};

// TKL ISO Layout - 88 keys (UK English)
export const TKL_ISO: KeyboardLayout = {
    id: 'tkl-iso',
    name: 'Main',
    kle: [
        ["Esc", { x: 1 }, "F1", "F2", "F3", "F4", { x: 0.5 }, "F5", "F6", "F7", "F8", { x: 0.5 }, "F9", "F10", "F11", "F12", { x: 0.25 }, "PrtSc", "ScrLk", "Pause\nBreak"],
        [{ y: 0.5 }, "¬\n`", "!\n1", "\"\n2", "£\n3", "$\n4", "%\n5", "^\n6", "&\n7", "*\n8", "(\n9", ")\n0", "_\n-", "+\n=", { w: 2 }, "Backspace", { x: 0.25 }, "Ins", "Home", "PgUp"],
        [{ w: 1.5 }, "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{\n[", "}\n]", { x: 0.25, w: 1.25, h: 2, w2: 1.5, h2: 1, x2: -0.25 }, "Enter", { x: 0.25 }, "Del", "End", "PgDn"],
        [{ w: 1.75 }, "Caps", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":\n;", "@\n'", "~\n#"],
        [{ w: 1.25 }, "Shift", "|\n\\", "Z", "X", "C", "V", "B", "N", "M", "<\n,", ">\n.", "?\n/", { w: 2.75 }, "Shift", { x: 1.25 }, "↑"],
        [{ w: 1.25 }, "Ctrl", { w: 1.25 }, "Win", { w: 1.25 }, "Alt", { w: 6.25 }, "", { w: 1.25 }, "AltGr", { w: 1.25 }, "Win", { w: 1.25 }, "Menu", { w: 1.25 }, "Ctrl", { x: 0.25 }, "←", "↓", "→"],
    ],
    mapping: [
        // Row 0: F-row
        hid(HID.ESCAPE), hid(HID.F1), hid(HID.F2), hid(HID.F3), hid(HID.F4),
        hid(HID.F5), hid(HID.F6), hid(HID.F7), hid(HID.F8),
        hid(HID.F9), hid(HID.F10), hid(HID.F11), hid(HID.F12),
        hid(HID.PRINT_SCREEN), hid(HID.SCROLL_LOCK), hid(HID.PAUSE),
        // Row 1: Number row
        hid(HID.GRAVE), hid(HID.N1), hid(HID.N2), hid(HID.N3), hid(HID.N4), hid(HID.N5),
        hid(HID.N6), hid(HID.N7), hid(HID.N8), hid(HID.N9), hid(HID.N0),
        hid(HID.MINUS), hid(HID.EQUAL), hid(HID.BACKSPACE),
        hid(HID.INSERT), hid(HID.HOME), hid(HID.PAGE_UP),
        // Row 2: QWERTY row + ISO Enter
        hid(HID.TAB), hid(HID.Q), hid(HID.W), hid(HID.E), hid(HID.R), hid(HID.T),
        hid(HID.Y), hid(HID.U), hid(HID.I), hid(HID.O), hid(HID.P),
        hid(HID.LBRACKET), hid(HID.RBRACKET), hid(HID.ENTER),
        hid(HID.DELETE), hid(HID.END), hid(HID.PAGE_DOWN),
        // Row 3: Home row + hash key
        hid(HID.CAPS_LOCK), hid(HID.A), hid(HID.S), hid(HID.D), hid(HID.F), hid(HID.G),
        hid(HID.H), hid(HID.J), hid(HID.K), hid(HID.L),
        hid(HID.SEMICOLON), hid(HID.QUOTE), hid(HID.NONUS_HASH),
        // Row 4: Bottom row + extra backslash key
        mod(HID.LSHIFT), hid(HID.NONUS_BACKSLASH),
        hid(HID.Z), hid(HID.X), hid(HID.C), hid(HID.V), hid(HID.B),
        hid(HID.N), hid(HID.M), hid(HID.COMMA), hid(HID.PERIOD), hid(HID.SLASH),
        mod(HID.RSHIFT), hid(HID.UP),
        // Row 5: Space row
        mod(HID.LCTRL), mod(HID.LGUI), mod(HID.LALT), hid(HID.SPACE),
        mod(HID.RALT), mod(HID.RGUI), noop(), mod(HID.RCTRL),
        hid(HID.LEFT), hid(HID.DOWN), hid(HID.RIGHT),
    ],
};

// Media Layout - media controls and browser shortcuts
export const MEDIA_LAYOUT: KeyboardLayout = {
    id: 'media',
    name: 'Media',
    kle: [
        [{ w: 1.5 }, "Prev", { w: 2 }, "Play/Pause", { w: 1.5 }, "Next", { x: 0.5 }, { w: 1.5 }, "Stop"],
        [{ y: 0.25, w: 1.5 }, "Mute", { w: 2 }, "Vol -", { w: 1.5 }, "Vol +"],
        [{ y: 0.5 }, "Back", "Forward", "Refresh", "Home"],
    ],
    mapping: [
        consumer(CONSUMER.PREV_TRACK), consumer(CONSUMER.PLAY_PAUSE), consumer(CONSUMER.NEXT_TRACK), consumer(CONSUMER.STOP),
        consumer(CONSUMER.MUTE), consumer(CONSUMER.VOLUME_DOWN), consumer(CONSUMER.VOLUME_UP),
        consumer(CONSUMER.WWW_BACK), consumer(CONSUMER.WWW_FORWARD), consumer(CONSUMER.WWW_REFRESH), consumer(CONSUMER.WWW_HOME),
    ],
};

// Mobile ABC Layout - QWERTY alpha keys
export const MOBILE_ABC: KeyboardLayout = {
    id: 'mobile-abc',
    name: 'ABC',
    kle: [
        ["Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"],
        [{ x: 0.5 }, "A", "S", "D", "F", "G", "H", "J", "K", "L"],
        [{ w: 1.5 }, "Shift", "Z", "X", "C", "V", "B", "N", "M", { w: 1.5 }, "Bksp"],
        [{ w: 1.5 }, "Ctrl", { w: 5.5 }, "", { w: 1.5 }, "Alt", { w: 1.5 }, "Enter"],
    ],
    mapping: [
        hid(HID.Q), hid(HID.W), hid(HID.E), hid(HID.R), hid(HID.T),
        hid(HID.Y), hid(HID.U), hid(HID.I), hid(HID.O), hid(HID.P),
        hid(HID.A), hid(HID.S), hid(HID.D), hid(HID.F), hid(HID.G),
        hid(HID.H), hid(HID.J), hid(HID.K), hid(HID.L),
        mod(HID.LSHIFT), hid(HID.Z), hid(HID.X), hid(HID.C), hid(HID.V),
        hid(HID.B), hid(HID.N), hid(HID.M), hid(HID.BACKSPACE),
        mod(HID.LCTRL), hid(HID.SPACE), mod(HID.LALT), hid(HID.ENTER),
    ],
};

// Mobile 123 Layout - Numbers and symbols
export const MOBILE_123: KeyboardLayout = {
    id: 'mobile-123',
    name: '123',
    kle: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        ["-", "=", "[", "]", "\\", ";", "'", ",", ".", "/"],
        [{ w: 1.5 }, "Shift", "`", "Tab", "Esc", { x: 3 }, { w: 1.5 }, "Bksp"],
        [{ w: 1.5 }, "Ctrl", { w: 5.5 }, "", { w: 1.5 }, "Alt", { w: 1.5 }, "Enter"],
    ],
    mapping: [
        hid(HID.N1), hid(HID.N2), hid(HID.N3), hid(HID.N4), hid(HID.N5),
        hid(HID.N6), hid(HID.N7), hid(HID.N8), hid(HID.N9), hid(HID.N0),
        hid(HID.MINUS), hid(HID.EQUAL), hid(HID.LBRACKET), hid(HID.RBRACKET), hid(HID.BACKSLASH),
        hid(HID.SEMICOLON), hid(HID.QUOTE), hid(HID.COMMA), hid(HID.PERIOD), hid(HID.SLASH),
        mod(HID.LSHIFT), hid(HID.GRAVE), hid(HID.TAB), hid(HID.ESCAPE), hid(HID.BACKSPACE),
        mod(HID.LCTRL), hid(HID.SPACE), mod(HID.LALT), hid(HID.ENTER),
    ],
};

// Mobile Fn Layout - Function keys and navigation
export const MOBILE_FN: KeyboardLayout = {
    id: 'mobile-fn',
    name: 'Fn',
    kle: [
        ["F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10"],
        ["F11", "F12", "Ins", "Del", "Home", "End", "PgUp", "PgDn", { x: 2 }],
        [{ w: 1.5 }, "Ctrl", { w: 1.5 }, "Alt", { x: 1 }, "Up", { x: 1 }, { w: 1.5 }, "Bksp", { w: 1.5 }, "Enter"],
        [{ x: 3.5 }, "Left", "Down", "Right"],
    ],
    mapping: [
        hid(HID.F1), hid(HID.F2), hid(HID.F3), hid(HID.F4), hid(HID.F5),
        hid(HID.F6), hid(HID.F7), hid(HID.F8), hid(HID.F9), hid(HID.F10),
        hid(HID.F11), hid(HID.F12), hid(HID.INSERT), hid(HID.DELETE),
        hid(HID.HOME), hid(HID.END), hid(HID.PAGE_UP), hid(HID.PAGE_DOWN),
        mod(HID.LCTRL), mod(HID.LALT), hid(HID.UP), hid(HID.BACKSPACE), hid(HID.ENTER),
        hid(HID.LEFT), hid(HID.DOWN), hid(HID.RIGHT),
    ],
};

// Mobile Media Layout - Large media controls
export const MOBILE_MEDIA: KeyboardLayout = {
    id: 'mobile-media',
    name: 'Media',
    kle: [
        [{ w: 2, h: 2 }, "Prev", { w: 3, h: 2 }, "Play\nPause", { w: 2, h: 2 }, "Next"],
        [],
        [{ y: 0.25 }, "Mute", { w: 2.5 }, "Vol -", { w: 2.5 }, "Vol +"],
    ],
    mapping: [
        consumer(CONSUMER.PREV_TRACK), consumer(CONSUMER.PLAY_PAUSE), consumer(CONSUMER.NEXT_TRACK),
        consumer(CONSUMER.MUTE), consumer(CONSUMER.VOLUME_DOWN), consumer(CONSUMER.VOLUME_UP),
    ],
};

// Layout presets by region
export type LayoutPreset = 'ansi' | 'iso';

export const DESKTOP_PRESETS: Record<LayoutPreset, KeyboardLayout[]> = {
    ansi: [TKL_MAIN, MEDIA_LAYOUT],
    iso: [TKL_ISO, MEDIA_LAYOUT],
};

export const MOBILE_LAYOUTS: KeyboardLayout[] = [MOBILE_ABC, MOBILE_123, MOBILE_FN, MOBILE_MEDIA];

// Helper to get desktop layouts for a preset
export function getDesktopLayouts(preset: LayoutPreset): KeyboardLayout[] {
    return DESKTOP_PRESETS[preset] || DESKTOP_PRESETS.ansi;
}
