/**
 * KLE (Keyboard Layout Editor) JSON Parser
 *
 * Parses KLE JSON format into positioned, sized key objects for rendering.
 * KLE format: https://github.com/ijprest/keyboard-layout-editor/wiki/Serialized-Data-Format
 */

export interface ParsedKey {
    index: number;       // For mapping lookup
    label: string;       // Primary label (first line of KLE label)
    legends: string[];   // All legend positions (KLE 12-position grid)
    x: number;           // X position in units
    y: number;           // Y position in units
    w: number;           // Width in units
    h: number;           // Height in units
    // Colors from KLE (persist until changed)
    color?: string;      // Keycap background color (KLE 'c' property)
    textColor?: string;  // Legend/text color (KLE 't' property)
    // ISO Enter support
    isIsoEnter: boolean;
    w2?: number;
    h2?: number;
    x2?: number;
    y2?: number;
}

export interface ParsedLayout {
    keys: ParsedKey[];
    width: number;       // Total width in units
    height: number;      // Total height in units
}

// KLE property object shape
interface KLEProperties {
    x?: number;   // X offset for next key
    y?: number;   // Y offset for next key
    w?: number;   // Width
    h?: number;   // Height
    x2?: number;  // Secondary block X offset (ISO Enter)
    y2?: number;  // Secondary block Y offset (ISO Enter)
    w2?: number;  // Secondary block width (ISO Enter)
    h2?: number;  // Secondary block height (ISO Enter)
    c?: string;   // Keycap color (persists)
    t?: string;   // Text color (persists)
}

/**
 * Parse KLE JSON into a structured layout
 *
 * KLE format:
 * - First element (optional): metadata object with name, author, etc.
 * - Subsequent elements: row arrays containing strings (labels) and objects (properties)
 * - Properties persist until overridden: w, h for size; c, t for colors
 * - Position offsets (x, y) apply only to the next key
 */
export function parseKLE(kle: unknown[]): ParsedLayout {
    const keys: ParsedKey[] = [];
    let keyIndex = 0;

    // Track current position
    let currentY = 0;

    // Track persistent properties (colors persist, size resets per key)
    let persistentColor: string | undefined;
    let persistentTextColor: string | undefined;

    // Track max dimensions
    let maxX = 0;
    let maxY = 0;

    // Start index - skip metadata object if present
    let startIndex = 0;
    if (kle.length > 0 && !Array.isArray(kle[0]) && typeof kle[0] === 'object') {
        startIndex = 1;
    }

    // Process each row
    for (let i = startIndex; i < kle.length; i++) {
        const row = kle[i];
        if (!Array.isArray(row)) continue;

        let currentX = 0;

        // Current key properties (reset per key, except colors)
        let props: KLEProperties = {
            w: 1,
            h: 1,
            x: 0,
            y: 0,
            x2: 0,
            y2: 0,
            w2: 0,
            h2: 0,
        };

        for (const item of row) {
            if (typeof item === 'object' && item !== null && !Array.isArray(item)) {
                // Property object - merge into current properties
                const p = item as KLEProperties;

                if (p.x !== undefined) props.x = p.x;
                if (p.y !== undefined) props.y = p.y;
                if (p.w !== undefined) props.w = p.w;
                if (p.h !== undefined) props.h = p.h;
                if (p.x2 !== undefined) props.x2 = p.x2;
                if (p.y2 !== undefined) props.y2 = p.y2;
                if (p.w2 !== undefined) props.w2 = p.w2;
                if (p.h2 !== undefined) props.h2 = p.h2;

                // Colors persist until changed
                if (p.c !== undefined) persistentColor = p.c;
                if (p.t !== undefined) persistentTextColor = p.t;

            } else if (typeof item === 'string') {
                // Key label - create key at current position

                // Apply position offsets
                currentX += props.x || 0;
                currentY += props.y || 0;

                // Parse legends - split by newline for multi-legend support
                const legends = item.split('\n');
                const primaryLabel = legends[0] || '';

                // Check for ISO Enter (has secondary block dimensions)
                const isIsoEnter = (props.w2 !== undefined && props.w2 !== 0) ||
                                   (props.h2 !== undefined && props.h2 !== 0);

                const key: ParsedKey = {
                    index: keyIndex++,
                    label: primaryLabel,
                    legends,
                    x: currentX,
                    y: currentY,
                    w: props.w || 1,
                    h: props.h || 1,
                    color: persistentColor,
                    textColor: persistentTextColor,
                    isIsoEnter,
                };

                // Add ISO Enter properties if present
                if (isIsoEnter) {
                    key.x2 = props.x2;
                    key.y2 = props.y2;
                    key.w2 = props.w2;
                    key.h2 = props.h2;
                }

                keys.push(key);

                // Update max dimensions
                const keyRight = currentX + (props.w || 1);
                const keyBottom = currentY + (props.h || 1);
                if (keyRight > maxX) maxX = keyRight;
                if (keyBottom > maxY) maxY = keyBottom;

                // Move X position for next key
                currentX += props.w || 1;

                // Reset per-key properties (but keep colors)
                props = {
                    w: 1,
                    h: 1,
                    x: 0,
                    y: 0,
                    x2: 0,
                    y2: 0,
                    w2: 0,
                    h2: 0,
                };
            }
        }

        // Move to next row
        currentY += 1;
    }

    return {
        keys,
        width: maxX,
        height: maxY,
    };
}
