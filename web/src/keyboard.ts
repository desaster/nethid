/**
 * Virtual Keyboard Renderer
 *
 * Renders KLE layouts as interactive virtual keyboards for HID control.
 */

import { parseKLE } from './kle-parser';
import type { ParsedKey, ParsedLayout } from './kle-parser';
import type { HIDClient } from './hid';

// Key action types
export interface KeyAction {
    type: 'hid' | 'consumer' | 'system' | 'modifier' | 'layer' | 'noop';
    code?: number;        // HID scancode, consumer code, or system code
    layer?: string;       // Target layer ID for layer switch keys
    toggle?: boolean;     // For modifiers: sticky toggle vs momentary
}

// Combined layout + mapping format
export interface KeyboardLayout {
    id: string;           // e.g., "tkl-main"
    name: string;         // e.g., "Main"
    kle: unknown[];       // KLE JSON array (rows of keys)
    mapping: KeyAction[]; // 1:1 by flattened key index
}

export interface VirtualKeyboardOptions {
    container: HTMLElement;
    layout: KeyboardLayout;
    client: HIDClient;
    unitSize?: number;    // Pixels per unit (default: 48)
    gap?: number;         // Gap between keys (default: 4)
    isTouch?: boolean;    // Touch mode: immediate press+release, no key hold
    onLayerChange?: (layerId: string) => void;
}

interface KeyState {
    element: HTMLElement;
    action: KeyAction;
    pressed: boolean;
    toggled: boolean;     // For modifier toggle state
}

export class VirtualKeyboard {
    private container: HTMLElement;
    private client: HIDClient;
    private unitSize: number;
    private gap: number;
    private isTouch: boolean;
    private onLayerChange?: (layerId: string) => void;

    private keyboardEl: HTMLElement | null = null;
    private keys: Map<number, KeyState> = new Map();
    private parsedLayout: ParsedLayout | null = null;

    constructor(options: VirtualKeyboardOptions) {
        this.container = options.container;
        this.client = options.client;
        this.unitSize = options.unitSize ?? 48;
        this.gap = options.gap ?? 4;
        this.isTouch = options.isTouch ?? false;
        this.onLayerChange = options.onLayerChange;

        this.setLayout(options.layout);
    }

    setLayout(layout: KeyboardLayout): void {
        // Clear existing keyboard
        this.destroy();

        // Parse KLE layout
        this.parsedLayout = parseKLE(layout.kle);

        // Create keyboard container
        this.keyboardEl = document.createElement('div');
        this.keyboardEl.className = 'virtual-keyboard';

        // Calculate total size
        const totalWidth = this.parsedLayout.width * this.unitSize +
                          (this.parsedLayout.width - 1) * this.gap;
        const totalHeight = this.parsedLayout.height * this.unitSize +
                           (this.parsedLayout.height - 1) * this.gap;

        this.keyboardEl.style.width = `${totalWidth}px`;
        this.keyboardEl.style.height = `${totalHeight}px`;

        // Render each key
        for (const key of this.parsedLayout.keys) {
            const action = layout.mapping[key.index] || { type: 'noop' };
            this.renderKey(key, action);
        }

        this.container.appendChild(this.keyboardEl);
    }

    private renderKey(key: ParsedKey, action: KeyAction): void {
        const el = document.createElement('div');
        el.className = 'vk-key';

        // For ISO Enter, calculate bounding box including secondary block
        let keyW = key.w;
        let keyH = key.h;
        let offsetX = 0;
        let offsetY = 0;

        if (key.isIsoEnter && key.w2 !== undefined && key.h2 !== undefined) {
            const x2 = key.x2 ?? 0;
            const y2 = key.y2 ?? 0;
            // Bounding box encompasses both primary and secondary rectangles
            const minX = Math.min(0, x2);
            const maxX = Math.max(key.w, x2 + key.w2);
            const minY = Math.min(0, y2);
            const maxY = Math.max(key.h, y2 + key.h2);
            keyW = maxX - minX;
            keyH = maxY - minY;
            offsetX = minX;  // Shift position if secondary extends left
            offsetY = minY;  // Shift position if secondary extends up
        }

        // Calculate position (with offset for ISO Enter)
        const left = (key.x + offsetX) * (this.unitSize + this.gap);
        const top = (key.y + offsetY) * (this.unitSize + this.gap);

        // Calculate size
        const width = keyW * this.unitSize + (keyW - 1) * this.gap;
        const height = keyH * this.unitSize + (keyH - 1) * this.gap;

        el.style.left = `${left}px`;
        el.style.top = `${top}px`;
        el.style.width = `${width}px`;
        el.style.height = `${height}px`;

        // Apply KLE colors as inline styles
        if (key.color) {
            el.style.backgroundColor = key.color;
        }
        if (key.textColor) {
            el.style.color = key.textColor;
        }

        // ISO Enter clip-path
        if (key.isIsoEnter && key.w2 !== undefined && key.h2 !== undefined) {
            const clipPath = this.computeIsoEnterClipPath(key, keyW, keyH, offsetX, offsetY);
            if (clipPath) {
                el.style.clipPath = clipPath;
            }
        }

        // Add legends container
        const legendsEl = document.createElement('div');
        legendsEl.className = 'vk-legends';

        // Find non-empty legends with their positions
        const nonEmpty: { text: string; pos: number }[] = [];
        for (let i = 0; i < key.legends.length; i++) {
            const legend = key.legends[i];
            if (legend && legend.trim()) {
                nonEmpty.push({ text: legend, pos: i });
            }
        }

        // Single legend at position 0: center it (common case)
        // Otherwise: render each at its KLE position
        if (nonEmpty.length === 1 && nonEmpty[0].pos === 0) {
            const span = document.createElement('span');
            span.className = 'vk-legend vk-legend-center';
            span.textContent = nonEmpty[0].text;
            legendsEl.appendChild(span);
        } else {
            for (const { text, pos } of nonEmpty) {
                const span = document.createElement('span');
                span.className = `vk-legend vk-legend-${pos}`;
                span.textContent = text;
                legendsEl.appendChild(span);
            }
        }

        el.appendChild(legendsEl);

        // Store key state
        const state: KeyState = {
            element: el,
            action,
            pressed: false,
            toggled: false,
        };
        this.keys.set(key.index, state);

        // Set up interaction handlers
        this.setupKeyInteraction(el, state);

        this.keyboardEl!.appendChild(el);
    }

    private computeIsoEnterClipPath(
        key: ParsedKey,
        boundingW: number,
        boundingH: number,
        offsetX: number,
        offsetY: number
    ): string | null {
        // ISO Enter is an L-shaped key
        // Primary block at (0,0) with size (w,h)
        // Secondary block at (x2,y2) with size (w2,h2)
        // Bounding box already calculated, we need clip-path in bounding box coords

        const { x2 = 0, y2 = 0, w2 = 0, h2 = 0 } = key;

        if (w2 === 0 || h2 === 0) return null;

        // Convert key coordinates to bounding box coordinates
        // Primary in bbox: left = -offsetX
        // Secondary in bbox: left = x2 - offsetX, right = x2 + w2 - offsetX
        const primaryLeft = -offsetX;
        const secondaryLeft = x2 - offsetX;
        const secondaryRight = x2 + w2 - offsetX;
        const secondaryBottom = (y2 - offsetY) + h2;

        const toPercent = (val: number, total: number) => `${(val / total) * 100}%`;

        // Standard ISO Enter: secondary wider at top, primary narrower extends down
        // Shape: wider top portion, step down to narrower bottom portion
        // Polygon clockwise from top-left:
        return `polygon(
            ${toPercent(secondaryLeft, boundingW)} 0%,
            ${toPercent(secondaryRight, boundingW)} 0%,
            ${toPercent(secondaryRight, boundingW)} 100%,
            ${toPercent(primaryLeft, boundingW)} 100%,
            ${toPercent(primaryLeft, boundingW)} ${toPercent(secondaryBottom, boundingH)},
            ${toPercent(secondaryLeft, boundingW)} ${toPercent(secondaryBottom, boundingH)}
        )`.replace(/\s+/g, ' ');
    }

    private setupKeyInteraction(el: HTMLElement, state: KeyState): void {
        const onPress = (e: PointerEvent) => {
            e.preventDefault();
            el.setPointerCapture(e.pointerId);

            if (state.action.type === 'modifier' && state.action.toggle !== false) {
                // Toggle modifier
                state.toggled = !state.toggled;
                el.classList.toggle('vk-key-toggled', state.toggled);

                if (state.toggled) {
                    this.sendKeyDown(state.action);
                } else {
                    this.sendKeyUp(state.action);
                }
            } else if (this.isTouch) {
                // Touch mode: immediate press+release (no key repeat)
                el.classList.add('vk-key-active');
                this.sendKeyDown(state.action);
                this.sendKeyUp(state.action);
                // Brief visual feedback
                setTimeout(() => {
                    el.classList.remove('vk-key-active');
                }, 100);
            } else {
                // Desktop: hold until release
                state.pressed = true;
                el.classList.add('vk-key-active');
                this.sendKeyDown(state.action);
            }
        };

        const onRelease = (e: PointerEvent) => {
            e.preventDefault();

            // Don't release toggleable modifiers on pointer up
            if (state.action.type === 'modifier' && state.action.toggle !== false) {
                return;
            }

            // Touch mode already released on press
            if (this.isTouch) {
                return;
            }

            if (state.pressed) {
                state.pressed = false;
                el.classList.remove('vk-key-active');
                this.sendKeyUp(state.action);
            }
        };

        el.addEventListener('pointerdown', onPress);
        el.addEventListener('pointerup', onRelease);
        el.addEventListener('pointercancel', onRelease);
        el.addEventListener('pointerleave', (e) => {
            // Only release if we don't have pointer capture
            if (!el.hasPointerCapture(e.pointerId)) {
                onRelease(e);
            }
        });

        // Prevent context menu
        el.addEventListener('contextmenu', (e) => e.preventDefault());

        // Prevent double-tap zoom on Safari
        el.addEventListener('touchend', (e) => {
            e.preventDefault();
        });
    }

    private sendKeyDown(action: KeyAction): void {
        switch (action.type) {
            case 'hid':
                if (action.code !== undefined) {
                    this.client.sendKeyDownByCode(action.code);
                }
                break;
            case 'consumer':
                if (action.code !== undefined) {
                    this.client.sendConsumerKey(action.code, true);
                }
                break;
            case 'system':
                if (action.code !== undefined) {
                    this.client.sendSystemKey(action.code, true);
                }
                break;
            case 'modifier':
                if (action.code !== undefined) {
                    this.client.sendKeyDownByCode(action.code);
                }
                break;
            case 'layer':
                if (action.layer) {
                    this.onLayerChange?.(action.layer);
                }
                break;
            case 'noop':
                // Do nothing
                break;
        }
    }

    private sendKeyUp(action: KeyAction): void {
        switch (action.type) {
            case 'hid':
                if (action.code !== undefined) {
                    this.client.sendKeyUpByCode(action.code);
                }
                break;
            case 'consumer':
                if (action.code !== undefined) {
                    this.client.sendConsumerKey(action.code, false);
                }
                break;
            case 'system':
                if (action.code !== undefined) {
                    this.client.sendSystemKey(action.code, false);
                }
                break;
            case 'modifier':
                if (action.code !== undefined) {
                    this.client.sendKeyUpByCode(action.code);
                }
                break;
            case 'layer':
                // Layer switches are instantaneous, no key-up
                break;
            case 'noop':
                // Do nothing
                break;
        }
    }

    releaseAllKeys(): void {
        for (const [, state] of this.keys) {
            if (state.pressed) {
                state.pressed = false;
                state.element.classList.remove('vk-key-active');
                this.sendKeyUp(state.action);
            }
            if (state.toggled) {
                state.toggled = false;
                state.element.classList.remove('vk-key-toggled');
                this.sendKeyUp(state.action);
            }
        }
    }

    destroy(): void {
        this.releaseAllKeys();
        this.keys.clear();
        if (this.keyboardEl) {
            this.keyboardEl.remove();
            this.keyboardEl = null;
        }
        this.parsedLayout = null;
    }
}

// ============================================================================
// Keyboard Manager - handles multiple layouts with tab switching
// ============================================================================

export interface KeyboardManagerOptions {
    container: HTMLElement;
    client: HIDClient;
    layouts: KeyboardLayout[];
    initialLayoutId?: string;
    unitSize?: number;
    gap?: number;
    isTouch?: boolean;
    onToggle?: () => void;
    presets?: { value: string; label: string }[];
    currentPreset?: string;
    onPresetChange?: (preset: string) => void;
}

export class KeyboardManager {
    private container: HTMLElement;
    private client: HIDClient;
    private layouts: KeyboardLayout[];
    private unitSize: number;
    private gap: number;
    private isTouch: boolean;
    private onToggle?: () => void;
    private presets?: { value: string; label: string }[];
    private currentPreset?: string;
    private onPresetChange?: (preset: string) => void;

    private managerEl: HTMLElement | null = null;
    private tabsEl: HTMLElement | null = null;
    private keyboardContainer: HTMLElement | null = null;
    private keyboard: VirtualKeyboard | null = null;
    private currentLayoutId: string;
    private maxWidthUnits: number = 0;
    private maxHeightUnits: number = 0;
    private effectiveUnitSize: number = 0;

    constructor(options: KeyboardManagerOptions) {
        this.container = options.container;
        this.client = options.client;
        this.layouts = options.layouts;
        this.unitSize = options.unitSize ?? 48;
        this.gap = options.gap ?? 4;
        this.isTouch = options.isTouch ?? false;
        this.onToggle = options.onToggle;
        this.presets = options.presets;
        this.currentPreset = options.currentPreset;
        this.onPresetChange = options.onPresetChange;

        // Default to first layout
        this.currentLayoutId = options.initialLayoutId || options.layouts[0]?.id || '';

        // Calculate max dimensions (in units) across all layouts
        this.calculateMaxDimensions();

        this.render();
    }

    private calculateMaxDimensions(): void {
        for (const layout of this.layouts) {
            const parsed = parseKLE(layout.kle);
            this.maxWidthUnits = Math.max(this.maxWidthUnits, parsed.width);
            this.maxHeightUnits = Math.max(this.maxHeightUnits, parsed.height);
        }
    }

    private render(): void {
        // Create manager container
        this.managerEl = document.createElement('div');
        this.managerEl.className = 'keyboard-manager';

        // Create tabs (if more than one layout, or if extras need a home)
        if (this.layouts.length > 1 || this.onToggle || this.presets) {
            this.tabsEl = document.createElement('div');
            this.tabsEl.className = 'km-tabs';

            if (this.layouts.length > 1) {
                for (const layout of this.layouts) {
                    const tab = document.createElement('button');
                    tab.className = 'km-tab';
                    tab.textContent = layout.name;
                    tab.dataset.layoutId = layout.id;

                    if (layout.id === this.currentLayoutId) {
                        tab.classList.add('km-tab-active');
                    }

                    tab.addEventListener('click', () => {
                        this.switchLayout(layout.id);
                    });

                    this.tabsEl.appendChild(tab);
                }
            }

            if (this.presets && this.onPresetChange) {
                const select = document.createElement('select');
                select.className = 'layout-select';
                for (const p of this.presets) {
                    const opt = document.createElement('option');
                    opt.value = p.value;
                    opt.textContent = p.label;
                    select.appendChild(opt);
                }
                if (this.currentPreset) {
                    select.value = this.currentPreset;
                }
                const onChange = this.onPresetChange;
                select.addEventListener('change', () => onChange(select.value));
                this.tabsEl.appendChild(select);
            }

            if (this.onToggle) {
                const toggleBtn = document.createElement('button');
                toggleBtn.className = 'km-tab keyboard-toggle';
                toggleBtn.textContent = '\u2328\uFE0E';
                toggleBtn.addEventListener('click', () => this.onToggle!());
                this.tabsEl.appendChild(toggleBtn);
            }

            this.managerEl.appendChild(this.tabsEl);
        }

        // Create keyboard container
        this.keyboardContainer = document.createElement('div');
        this.keyboardContainer.className = 'km-keyboard-container';
        this.managerEl.appendChild(this.keyboardContainer);

        // Append to DOM before measuring so clientWidth is available
        this.container.appendChild(this.managerEl);

        // Calculate effective unitSize: cap at configured value, shrink to fit container
        const availableWidth = this.keyboardContainer.clientWidth;
        if (availableWidth > 0 && this.maxWidthUnits > 0) {
            this.effectiveUnitSize = Math.min(
                this.unitSize,
                Math.floor((availableWidth - (this.maxWidthUnits - 1) * this.gap) / this.maxWidthUnits)
            );
        } else {
            this.effectiveUnitSize = this.unitSize;
        }

        // Set min-height based on effective unitSize
        const minHeight = this.maxHeightUnits * this.effectiveUnitSize +
                         (this.maxHeightUnits - 1) * this.gap;
        this.keyboardContainer.style.minHeight = `${minHeight}px`;

        // Render initial keyboard
        this.renderKeyboard();
    }

    private renderKeyboard(): void {
        const layout = this.layouts.find(l => l.id === this.currentLayoutId);
        if (!layout || !this.keyboardContainer) return;

        // Destroy existing keyboard
        if (this.keyboard) {
            this.keyboard.destroy();
        }

        // Create new keyboard
        this.keyboard = new VirtualKeyboard({
            container: this.keyboardContainer,
            layout,
            client: this.client,
            unitSize: this.effectiveUnitSize,
            gap: this.gap,
            isTouch: this.isTouch,
            onLayerChange: (layerId) => {
                this.switchLayout(layerId);
            },
        });
    }

    switchLayout(layoutId: string): void {
        if (layoutId === this.currentLayoutId) return;

        const layout = this.layouts.find(l => l.id === layoutId);
        if (!layout) return;

        // Release all keys before switching
        if (this.keyboard) {
            this.keyboard.releaseAllKeys();
        }

        // Update current layout
        this.currentLayoutId = layoutId;

        // Update tab active state
        if (this.tabsEl) {
            for (const tab of this.tabsEl.querySelectorAll('.km-tab')) {
                const tabEl = tab as HTMLElement;
                tabEl.classList.toggle('km-tab-active', tabEl.dataset.layoutId === layoutId);
            }
        }

        // Render new keyboard
        this.renderKeyboard();
    }

    replaceLayouts(layouts: KeyboardLayout[], currentPreset?: string): void {
        this.layouts = layouts;
        this.currentLayoutId = layouts[0]?.id || '';
        if (currentPreset !== undefined) {
            this.currentPreset = currentPreset;
        }
        this.maxWidthUnits = 0;
        this.maxHeightUnits = 0;
        this.calculateMaxDimensions();

        // Tear down current DOM
        if (this.keyboard) {
            this.keyboard.destroy();
            this.keyboard = null;
        }
        if (this.managerEl) {
            this.managerEl.remove();
            this.managerEl = null;
        }
        this.tabsEl = null;
        this.keyboardContainer = null;

        this.render();
    }

    destroy(): void {
        if (this.keyboard) {
            this.keyboard.destroy();
            this.keyboard = null;
        }
        if (this.managerEl) {
            this.managerEl.remove();
            this.managerEl = null;
        }
        this.tabsEl = null;
        this.keyboardContainer = null;
    }
}
