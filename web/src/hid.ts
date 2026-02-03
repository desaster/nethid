/**
 * HID Control Module
 *
 * Handles WebSocket connection and HID command transmission.
 */

// Browser keycode to USB HID scancode mapping
const KEY_MAP: Record<string, number> = {
    'KeyA': 0x04, 'KeyB': 0x05, 'KeyC': 0x06, 'KeyD': 0x07,
    'KeyE': 0x08, 'KeyF': 0x09, 'KeyG': 0x0A, 'KeyH': 0x0B,
    'KeyI': 0x0C, 'KeyJ': 0x0D, 'KeyK': 0x0E, 'KeyL': 0x0F,
    'KeyM': 0x10, 'KeyN': 0x11, 'KeyO': 0x12, 'KeyP': 0x13,
    'KeyQ': 0x14, 'KeyR': 0x15, 'KeyS': 0x16, 'KeyT': 0x17,
    'KeyU': 0x18, 'KeyV': 0x19, 'KeyW': 0x1A, 'KeyX': 0x1B,
    'KeyY': 0x1C, 'KeyZ': 0x1D,
    'Digit1': 0x1E, 'Digit2': 0x1F, 'Digit3': 0x20, 'Digit4': 0x21,
    'Digit5': 0x22, 'Digit6': 0x23, 'Digit7': 0x24, 'Digit8': 0x25,
    'Digit9': 0x26, 'Digit0': 0x27,
    'Enter': 0x28, 'Escape': 0x29, 'Backspace': 0x2A, 'Tab': 0x2B,
    'Space': 0x2C, 'Minus': 0x2D, 'Equal': 0x2E, 'BracketLeft': 0x2F,
    'BracketRight': 0x30, 'Backslash': 0x31, 'Semicolon': 0x33,
    'Quote': 0x34, 'Backquote': 0x35, 'Comma': 0x36, 'Period': 0x37,
    'Slash': 0x38, 'CapsLock': 0x39,
    'F1': 0x3A, 'F2': 0x3B, 'F3': 0x3C, 'F4': 0x3D,
    'F5': 0x3E, 'F6': 0x3F, 'F7': 0x40, 'F8': 0x41,
    'F9': 0x42, 'F10': 0x43, 'F11': 0x44, 'F12': 0x45,
    'PrintScreen': 0x46, 'ScrollLock': 0x47, 'Pause': 0x48,
    'Insert': 0x49, 'Home': 0x4A, 'PageUp': 0x4B,
    'Delete': 0x4C, 'End': 0x4D, 'PageDown': 0x4E,
    'ArrowRight': 0x4F, 'ArrowLeft': 0x50, 'ArrowDown': 0x51, 'ArrowUp': 0x52,
    'NumLock': 0x53,
    'NumpadDivide': 0x54, 'NumpadMultiply': 0x55, 'NumpadSubtract': 0x56,
    'NumpadAdd': 0x57, 'NumpadEnter': 0x58,
    'Numpad1': 0x59, 'Numpad2': 0x5A, 'Numpad3': 0x5B, 'Numpad4': 0x5C,
    'Numpad5': 0x5D, 'Numpad6': 0x5E, 'Numpad7': 0x5F, 'Numpad8': 0x60,
    'Numpad9': 0x61, 'Numpad0': 0x62, 'NumpadDecimal': 0x63,
    // Modifiers - handled specially but included for reference
    'ControlLeft': 0xE0, 'ShiftLeft': 0xE1, 'AltLeft': 0xE2, 'MetaLeft': 0xE3,
    'ControlRight': 0xE4, 'ShiftRight': 0xE5, 'AltRight': 0xE6, 'MetaRight': 0xE7,
};

// HID command types
const CMD_KEY = 0x01;
const CMD_MOUSE_MOVE = 0x02;
const CMD_MOUSE_BUTTON = 0x03;
const CMD_SCROLL = 0x04;
const CMD_CONSUMER = 0x06;
const CMD_SYSTEM = 0x07;
const CMD_RELEASE_ALL = 0x0F;
const CMD_STATUS = 0x10;  // Server -> Client: USB status

// Mouse button bits
const BTN_LEFT = 0x01;
const BTN_RIGHT = 0x02;
const BTN_MIDDLE = 0x04;

export type ConnectionState = 'disconnected' | 'connecting' | 'connected' | 'displaced';

export interface USBStatus {
    mounted: boolean;
    suspended: boolean;
}

export interface HIDClientCallbacks {
    onStateChange?: (state: ConnectionState) => void;
    onUSBStatusChange?: (status: USBStatus) => void;
    onError?: (error: string) => void;
}

// Modifier key codes for display
const MODIFIER_CODES = new Set([
    'ControlLeft', 'ControlRight',
    'ShiftLeft', 'ShiftRight',
    'AltLeft', 'AltRight',
    'MetaLeft', 'MetaRight',
]);

// Get display name for a key code
export function getKeyDisplayName(code: string): string {
    // Modifiers
    if (code.startsWith('Control')) return 'Ctrl';
    if (code.startsWith('Shift')) return 'Shift';
    if (code.startsWith('Alt')) return 'Alt';
    if (code.startsWith('Meta')) return 'Meta';
    // Letters
    if (code.startsWith('Key')) return code.slice(3);
    // Digits
    if (code.startsWith('Digit')) return code.slice(5);
    // Numpad
    if (code.startsWith('Numpad')) return 'Num' + code.slice(6);
    // Arrows
    if (code.startsWith('Arrow')) return code.slice(5);
    // Common names
    const names: Record<string, string> = {
        'Space': 'Space', 'Enter': 'Enter', 'Backspace': 'Bksp',
        'Tab': 'Tab', 'Escape': 'Esc', 'Delete': 'Del',
        'Insert': 'Ins', 'Home': 'Home', 'End': 'End',
        'PageUp': 'PgUp', 'PageDown': 'PgDn',
        'CapsLock': 'Caps', 'NumLock': 'NumLk', 'ScrollLock': 'ScrLk',
        'Minus': '-', 'Equal': '=', 'BracketLeft': '[', 'BracketRight': ']',
        'Backslash': '\\', 'Semicolon': ';', 'Quote': "'", 'Backquote': '`',
        'Comma': ',', 'Period': '.', 'Slash': '/',
    };
    return names[code] || code;
}

export function isModifierKey(code: string): boolean {
    return MODIFIER_CODES.has(code);
}

export class HIDClient {
    private ws: WebSocket | null = null;
    private state: ConnectionState = 'disconnected';
    private usbStatus: USBStatus = { mounted: false, suspended: false };
    private callbacks: HIDClientCallbacks;
    private reconnectTimer: number | null = null;
    private reconnectDelay = 1000;
    private intentionalDisconnect = false;

    constructor(callbacks: HIDClientCallbacks = {}) {
        this.callbacks = callbacks;
    }

    getUSBStatus(): USBStatus {
        return { ...this.usbStatus };
    }

    connect(): void {
        if (this.ws) {
            this.ws.close();
        }

        this.intentionalDisconnect = false;
        this.setState('connecting');

        // WebSocket on port 8081
        const wsUrl = `ws://${location.hostname}:8081/`;
        this.ws = new WebSocket(wsUrl);
        this.ws.binaryType = 'arraybuffer';

        this.ws.onopen = () => {
            this.setState('connected');
            this.reconnectDelay = 1000;
        };

        this.ws.onclose = (event) => {
            // Close code 4001 = session taken over by another client
            if (event.code === 4001) {
                this.setState('displaced');
                // Don't auto-reconnect when displaced to avoid reconnect loops
            } else {
                this.setState('disconnected');
                if (!this.intentionalDisconnect) {
                    this.scheduleReconnect();
                }
            }
        };

        this.ws.onerror = () => {
            this.callbacks.onError?.('WebSocket error');
        };

        this.ws.onmessage = (event) => {
            this.handleMessage(event.data);
        };
    }

    private handleMessage(data: ArrayBuffer): void {
        const bytes = new Uint8Array(data);
        if (bytes.length < 2) return;

        const cmd = bytes[0];
        if (cmd === CMD_STATUS) {
            const flags = bytes[1];
            this.usbStatus = {
                mounted: (flags & 0x01) !== 0,
                suspended: (flags & 0x02) !== 0,
            };
            this.callbacks.onUSBStatusChange?.(this.usbStatus);
        }
    }

    disconnect(): void {
        this.intentionalDisconnect = true;
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.ws) {
            this.sendReleaseAll();
            this.ws.close();
            this.ws = null;
        }
        this.setState('disconnected');
    }

    getState(): ConnectionState {
        return this.state;
    }

    private setState(state: ConnectionState): void {
        this.state = state;
        this.callbacks.onStateChange?.(state);
    }

    private scheduleReconnect(): void {
        if (this.reconnectTimer) return;
        this.reconnectTimer = window.setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, this.reconnectDelay);
        // Exponential backoff, max 10s
        this.reconnectDelay = Math.min(this.reconnectDelay * 1.5, 10000);
    }

    private send(data: Uint8Array): void {
        if (this.ws?.readyState === WebSocket.OPEN) {
            this.ws.send(data);
        }
    }

    // Key events
    sendKeyDown(code: string): void {
        const hid = KEY_MAP[code];
        if (hid !== undefined) {
            this.send(new Uint8Array([CMD_KEY, hid, 1]));
        }
    }

    sendKeyUp(code: string): void {
        const hid = KEY_MAP[code];
        if (hid !== undefined) {
            this.send(new Uint8Array([CMD_KEY, hid, 0]));
        }
    }

    // Mouse movement (dx, dy as signed 16-bit)
    sendMouseMove(dx: number, dy: number): void {
        const buf = new ArrayBuffer(5);
        const view = new DataView(buf);
        view.setUint8(0, CMD_MOUSE_MOVE);
        view.setInt16(1, dx, true); // little-endian
        view.setInt16(3, dy, true);
        this.send(new Uint8Array(buf));
    }

    // Mouse button (button: 0=left, 1=right, 2=middle)
    sendMouseButton(button: number, down: boolean): void {
        let bit = BTN_LEFT;
        if (button === 1) bit = BTN_MIDDLE;
        if (button === 2) bit = BTN_RIGHT;
        this.send(new Uint8Array([CMD_MOUSE_BUTTON, bit, down ? 1 : 0]));
    }

    // Scroll (dx, dy as signed 8-bit)
    sendScroll(dx: number, dy: number): void {
        dx = Math.max(-127, Math.min(127, Math.round(dx)));
        dy = Math.max(-127, Math.min(127, Math.round(dy)));
        const buf = new Uint8Array(3);
        buf[0] = CMD_SCROLL;
        buf[1] = dx & 0xFF;
        buf[2] = dy & 0xFF;
        this.send(buf);
    }

    sendReleaseAll(): void {
        this.send(new Uint8Array([CMD_RELEASE_ALL]));
    }

    // Consumer key (media controls)
    sendConsumerKey(code: number, down: boolean): void {
        const buf = new Uint8Array(4);
        buf[0] = CMD_CONSUMER;
        buf[1] = code & 0xFF;
        buf[2] = (code >> 8) & 0xFF;
        buf[3] = down ? 1 : 0;
        this.send(buf);
    }

    // System key (power, sleep, wake)
    sendSystemKey(code: number, down: boolean): void {
        const buf = new Uint8Array(4);
        buf[0] = CMD_SYSTEM;
        buf[1] = code & 0xFF;
        buf[2] = (code >> 8) & 0xFF;
        buf[3] = down ? 1 : 0;
        this.send(buf);
    }

    // Direct HID code sending (for virtual keyboard)
    sendKeyDownByCode(hidCode: number): void {
        this.send(new Uint8Array([CMD_KEY, hidCode, 1]));
    }

    sendKeyUpByCode(hidCode: number): void {
        this.send(new Uint8Array([CMD_KEY, hidCode, 0]));
    }
}

export interface InputCaptureCallbacks {
    onCaptureChange?: (captured: boolean) => void;
    onKeysChange?: (keys: Set<string>, modifiers: Set<string>) => void;
}

/**
 * Input capture controller
 * Manages pointer lock and keyboard capture on a target element
 */
export class InputCapture {
    private element: HTMLElement;
    private client: HIDClient;
    private captured = false;
    private callbacks: InputCaptureCallbacks;

    // Track held keys for display
    private heldKeys = new Set<string>();
    private heldModifiers = new Set<string>();

    // Mouse movement batching
    private pendingDx = 0;
    private pendingDy = 0;
    private moveTimer: number | null = null;

    constructor(element: HTMLElement, client: HIDClient, callbacks: InputCaptureCallbacks = {}) {
        this.element = element;
        this.client = client;
        this.callbacks = callbacks;

        this.setupEventListeners();
    }

    private setupEventListeners(): void {
        // Click to capture
        this.element.addEventListener('click', () => {
            if (!this.captured) {
                this.element.requestPointerLock();
            }
        });

        // Pointer lock change
        document.addEventListener('pointerlockchange', () => {
            this.captured = document.pointerLockElement === this.element;
            this.callbacks.onCaptureChange?.(this.captured);
            if (!this.captured) {
                this.client.sendReleaseAll();
                this.clearHeldKeys();
            }
        });

        // Keyboard events
        document.addEventListener('keydown', (e) => this.handleKeyDown(e));
        document.addEventListener('keyup', (e) => this.handleKeyUp(e));

        // Mouse events
        this.element.addEventListener('mousemove', (e) => this.handleMouseMove(e));
        this.element.addEventListener('mousedown', (e) => this.handleMouseButton(e, true));
        this.element.addEventListener('mouseup', (e) => this.handleMouseButton(e, false));
        this.element.addEventListener('wheel', (e) => this.handleWheel(e), { passive: false });

        // Prevent context menu on right-click
        this.element.addEventListener('contextmenu', (e) => {
            if (this.captured) e.preventDefault();
        });

        // Release all on page unload
        window.addEventListener('beforeunload', () => {
            this.client.sendReleaseAll();
        });

        // Release all on visibility change (tab switch)
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                this.client.sendReleaseAll();
            }
        });
    }

    private handleKeyDown(e: KeyboardEvent): void {
        if (!this.captured) return;

        // Let Escape through to exit pointer lock
        if (e.code === 'Escape') return;

        e.preventDefault();
        this.client.sendKeyDown(e.code);
        this.addHeldKey(e.code);
    }

    private handleKeyUp(e: KeyboardEvent): void {
        if (!this.captured) return;
        if (e.code === 'Escape') return;

        e.preventDefault();
        this.client.sendKeyUp(e.code);
        this.removeHeldKey(e.code);
    }

    private addHeldKey(code: string): void {
        if (isModifierKey(code)) {
            this.heldModifiers.add(code);
        } else {
            this.heldKeys.add(code);
        }
        this.callbacks.onKeysChange?.(this.heldKeys, this.heldModifiers);
    }

    private removeHeldKey(code: string): void {
        if (isModifierKey(code)) {
            this.heldModifiers.delete(code);
        } else {
            this.heldKeys.delete(code);
        }
        this.callbacks.onKeysChange?.(this.heldKeys, this.heldModifiers);
    }

    private clearHeldKeys(): void {
        this.heldKeys.clear();
        this.heldModifiers.clear();
        this.callbacks.onKeysChange?.(this.heldKeys, this.heldModifiers);
    }

    private handleMouseMove(e: MouseEvent): void {
        if (!this.captured) return;

        // Batch mouse movements
        this.pendingDx += e.movementX;
        this.pendingDy += e.movementY;

        if (!this.moveTimer) {
            this.moveTimer = window.setTimeout(() => {
                this.flushMouseMove();
            }, 16); // ~60fps
        }
    }

    private flushMouseMove(): void {
        this.moveTimer = null;
        if (this.pendingDx !== 0 || this.pendingDy !== 0) {
            // Clamp to int16 range
            const dx = Math.max(-32768, Math.min(32767, this.pendingDx));
            const dy = Math.max(-32768, Math.min(32767, this.pendingDy));
            this.client.sendMouseMove(dx, dy);
            this.pendingDx = 0;
            this.pendingDy = 0;
        }
    }

    private handleMouseButton(e: MouseEvent, down: boolean): void {
        if (!this.captured) return;
        e.preventDefault();
        this.client.sendMouseButton(e.button, down);
    }

    private handleWheel(e: WheelEvent): void {
        if (!this.captured) return;
        e.preventDefault();

        // Normalize wheel delta (different browsers report different values)
        let dy = e.deltaY;
        if (e.deltaMode === 1) dy *= 16; // lines to pixels
        if (e.deltaMode === 2) dy *= 100; // pages to pixels

        // Convert to scroll units (negative = scroll up)
        const scrollY = Math.round(-dy / 30);
        if (scrollY !== 0) {
            this.client.sendScroll(0, scrollY);
        }
    }

    isCaptured(): boolean {
        return this.captured;
    }

    release(): void {
        if (this.captured) {
            document.exitPointerLock();
        }
    }

    destroy(): void {
        this.release();
        if (this.moveTimer) {
            clearTimeout(this.moveTimer);
        }
    }
}

/**
 * Touch trackpad for mobile devices
 * Gestures:
 * - Single finger drag: mouse move
 * - Single tap: left click
 * - Double tap: double click
 * - Two finger tap: right click
 * - Two finger drag: scroll
 */
export class TouchTrackpad {
    private element: HTMLElement;
    private client: HIDClient;

    // Touch state
    private touches: Map<number, { x: number; y: number; startX: number; startY: number }> = new Map();
    private lastTapTime = 0;
    private tapTimeout: number | null = null;
    private moved = false;

    // Sensitivity multiplier
    private sensitivity = 1.5;

    constructor(element: HTMLElement, client: HIDClient) {
        this.element = element;
        this.client = client;
        this.setupEventListeners();
    }

    private setupEventListeners(): void {
        this.element.addEventListener('touchstart', (e) => this.handleTouchStart(e), { passive: false });
        this.element.addEventListener('touchmove', (e) => this.handleTouchMove(e), { passive: false });
        this.element.addEventListener('touchend', (e) => this.handleTouchEnd(e), { passive: false });
        this.element.addEventListener('touchcancel', (e) => this.handleTouchEnd(e), { passive: false });
    }

    private handleTouchStart(e: TouchEvent): void {
        e.preventDefault();

        for (const touch of Array.from(e.changedTouches)) {
            this.touches.set(touch.identifier, {
                x: touch.clientX,
                y: touch.clientY,
                startX: touch.clientX,
                startY: touch.clientY,
            });
        }
        this.moved = false;
    }

    private handleTouchMove(e: TouchEvent): void {
        e.preventDefault();

        const touchCount = this.touches.size;

        if (touchCount === 1) {
            // Single finger: mouse move
            const touch = e.changedTouches[0];
            const state = this.touches.get(touch.identifier);
            if (state) {
                const dx = (touch.clientX - state.x) * this.sensitivity;
                const dy = (touch.clientY - state.y) * this.sensitivity;

                if (Math.abs(dx) > 0.5 || Math.abs(dy) > 0.5) {
                    this.client.sendMouseMove(Math.round(dx), Math.round(dy));
                    this.moved = true;
                }

                state.x = touch.clientX;
                state.y = touch.clientY;
            }
        } else if (touchCount === 2) {
            // Two fingers: scroll
            let totalDy = 0;
            for (const touch of Array.from(e.changedTouches)) {
                const state = this.touches.get(touch.identifier);
                if (state) {
                    totalDy += touch.clientY - state.y;
                    state.x = touch.clientX;
                    state.y = touch.clientY;
                }
            }

            const scrollY = Math.round(-totalDy / 20);
            if (scrollY !== 0) {
                this.client.sendScroll(0, scrollY);
                this.moved = true;
            }
        }
    }

    private handleTouchEnd(e: TouchEvent): void {
        e.preventDefault();

        const touchCount = this.touches.size;
        const now = Date.now();

        // Check for tap gestures (no significant movement)
        if (!this.moved) {
            if (touchCount === 1) {
                // Single finger tap
                const timeSinceLastTap = now - this.lastTapTime;

                if (timeSinceLastTap < 300) {
                    // Double tap - cancel pending single tap and do double click
                    if (this.tapTimeout) {
                        clearTimeout(this.tapTimeout);
                        this.tapTimeout = null;
                    }
                    this.client.sendMouseButton(0, true);
                    this.client.sendMouseButton(0, false);
                    this.client.sendMouseButton(0, true);
                    this.client.sendMouseButton(0, false);
                    this.lastTapTime = 0;
                } else {
                    // Potential single tap - delay to check for double tap
                    this.lastTapTime = now;
                    this.tapTimeout = window.setTimeout(() => {
                        this.tapTimeout = null;
                        this.client.sendMouseButton(0, true);
                        this.client.sendMouseButton(0, false);
                    }, 200);
                }
            } else if (touchCount === 2) {
                // Two finger tap: right click
                if (this.tapTimeout) {
                    clearTimeout(this.tapTimeout);
                    this.tapTimeout = null;
                }
                this.client.sendMouseButton(2, true);
                this.client.sendMouseButton(2, false);
                this.lastTapTime = 0;
            }
        }

        // Remove ended touches
        for (const touch of Array.from(e.changedTouches)) {
            this.touches.delete(touch.identifier);
        }
    }

    destroy(): void {
        if (this.tapTimeout) {
            clearTimeout(this.tapTimeout);
        }
    }
}

// Check if device supports touch
export function isTouchDevice(): boolean {
    return 'ontouchstart' in window || navigator.maxTouchPoints > 0;
}
