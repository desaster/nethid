import "./style.css";
import { HIDClient, InputCapture, TouchTrackpad, getKeyDisplayName, isTouchDevice } from "./hid";
import type { ConnectionState, USBStatus } from "./hid";
import { KeyboardManager } from "./keyboard";
import { getDesktopLayouts, MOBILE_LAYOUTS, type LayoutPreset } from "./keyboard-layouts";

// Hash-based routing
function navigateTo(hash: string): void {
    cleanupControl();
    location.hash = hash;
}

window.addEventListener("hashchange", () => {
    cleanupControl();
    routeByHash();
});

function routeByHash(): void {
    const hash = location.hash.replace('#', '');
    if (!cachedStatus || cachedStatus.mode === "ap") {
        init();
    } else if (hash === "control") {
        renderControlPage();
    } else if (hash === "settings") {
        renderSettingsPage();
    } else {
        renderStatusPage(cachedStatus);
    }
}

// Layout preset storage
const LAYOUT_PRESET_KEY = 'nethid-layout-preset';

function getLayoutPreset(): LayoutPreset {
    const stored = localStorage.getItem(LAYOUT_PRESET_KEY);
    if (stored === 'ansi' || stored === 'iso') return stored;
    return 'ansi';  // default
}

function setLayoutPreset(preset: LayoutPreset): void {
    localStorage.setItem(LAYOUT_PRESET_KEY, preset);
}

// Mouse sensitivity storage
const MOUSE_SENSITIVITY_KEY = 'nethid-mouse-sensitivity';
const MOUSE_SENSITIVITY_PRESETS = [
    { label: 'Slow', value: 1 },
    { label: 'Normal', value: 2 },
    { label: 'Fast', value: 3 },
    { label: 'Very Fast', value: 5 },
];
const DEFAULT_MOUSE_SENSITIVITY = 2;

function getMouseSensitivity(): number {
    const stored = localStorage.getItem(MOUSE_SENSITIVITY_KEY);
    if (stored !== null) {
        const val = parseFloat(stored);
        if (!isNaN(val) && val >= 1 && val <= 10) return val;
    }
    return DEFAULT_MOUSE_SENSITIVITY;
}

function setMouseSensitivity(value: number): void {
    localStorage.setItem(MOUSE_SENSITIVITY_KEY, String(value));
}

interface DeviceStatus {
    hostname: string;
    mac: string;
    ip: string;
    uptime: number;
    mode: "sta" | "ap";
    version: string;
}

// HID client instance (shared)
let hidClient: HIDClient | null = null;
let inputCapture: InputCapture | null = null;
let touchTrackpad: TouchTrackpad | null = null;
let keyboardManager: KeyboardManager | null = null;

interface WifiNetwork {
    ssid: string;
    rssi: number;
    auth: string;
    ch: number;
}

interface NetworksResponse {
    scanning: boolean;
    networks: WifiNetwork[];
}

interface SettingsResponse {
    hostname: {
        value: string;
        default: boolean;
    };
    mqtt_enabled: boolean;
    mqtt_broker: string;
    mqtt_port: number;
    mqtt_topic: string;
    mqtt_username: string;
    mqtt_has_password: boolean;
    mqtt_client_id: string;
    // Syslog settings
    syslog_server: string;
    syslog_port: number;
}

interface MqttSettings {
    mqtt_enabled?: boolean;
    mqtt_broker?: string;
    mqtt_port?: number;
    mqtt_topic?: string;
    mqtt_username?: string;
    mqtt_password?: string;
    mqtt_client_id?: string;
}

interface SyslogSettings {
    syslog_server?: string;
    syslog_port?: number;
}

// Global state
let selectedNetwork: string | null = null;
let cachedNetworks: NetworksResponse | null = null;
let pollInterval: number | null = null;

async function fetchStatus(): Promise<DeviceStatus | null> {
    try {
        const response = await fetch("/api/status");
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error("Failed to fetch status:", error);
        return null;
    }
}

async function fetchNetworks(): Promise<NetworksResponse | null> {
    try {
        const response = await fetch("/api/networks");
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error("Failed to fetch networks:", error);
        return null;
    }
}

async function triggerScan(): Promise<boolean> {
    try {
        const response = await fetch("/api/scan", { method: "POST" });
        return response.ok;
    } catch (error) {
        console.error("Failed to trigger scan:", error);
        return false;
    }
}

async function saveCredentials(ssid: string, password: string): Promise<{ success: boolean; message?: string }> {
    try {
        const response = await fetch("/api/config", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ ssid, password })
        });
        const data = await response.json();
        return { success: data.status === "saved", message: data.message };
    } catch (error) {
        console.error("Failed to save credentials:", error);
        return { success: false, message: "Network error" };
    }
}

async function fetchSettings(): Promise<SettingsResponse | null> {
    try {
        const response = await fetch("/api/settings");
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error("Failed to fetch settings:", error);
        return null;
    }
}

async function saveSettings(settings: { hostname?: string } & MqttSettings & SyslogSettings): Promise<{ success: boolean; error?: string }> {
    try {
        const response = await fetch("/api/settings", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(settings)
        });
        return await response.json();
    } catch (error) {
        console.error("Failed to save settings:", error);
        return { success: false, error: "Network error" };
    }
}

function isValidHostname(hostname: string): boolean {
    // RFC 1123: alphanumeric + hyphen, 1-32 chars, no leading/trailing hyphen
    if (hostname.length === 0 || hostname.length > 32) return false;
    const regex = /^[a-zA-Z0-9]([a-zA-Z0-9-]{0,30}[a-zA-Z0-9])?$/;
    return hostname.length === 1 ? /^[a-zA-Z0-9]$/.test(hostname) : regex.test(hostname);
}

async function rebootDevice(): Promise<boolean> {
    try {
        const response = await fetch("/api/reboot", { method: "POST" });
        return response.ok;
    } catch (error) {
        console.error("Failed to reboot:", error);
        return false;
    }
}

function formatUptime(seconds: number): string {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${hours}h ${minutes}m ${secs}s`;
}

function escapeHtml(str: string): string {
    return str.replace(/[&<>"']/g, c => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        "\"": "&quot;",
        "'": "&#39;"
    }[c] || c));
}

function renderSignalBars(rssi: number): string {
    // Convert RSSI to 0-4 bars
    // Typical range: -90dBm (weak) to -30dBm (excellent)
    let bars = 0;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -60) bars = 3;
    else if (rssi >= -70) bars = 2;
    else if (rssi >= -80) bars = 1;

    return `<div class="signal-bars">
        ${[0, 1, 2, 3].map(i => `<div class="bar ${i < bars ? "active" : ""}"></div>`).join("")}
    </div>`;
}

function renderStatusPage(status: DeviceStatus): void {
    const app = document.querySelector<HTMLDivElement>("#app")!;

    app.innerHTML = `
        <div class="container">
            <h1>NetHID</h1>
            <p class="status-ok">Online</p>
            <table>
                <tr><th>Hostname</th><td>${escapeHtml(status.hostname)}</td></tr>
                <tr><th>MAC</th><td>${status.mac}</td></tr>
                <tr><th>IP</th><td>${status.ip}</td></tr>
                <tr><th>Mode</th><td>Station</td></tr>
                <tr><th>Uptime</th><td>${formatUptime(status.uptime)}</td></tr>
                <tr><th>Version</th><td>${escapeHtml(status.version)}</td></tr>
            </table>
            <button id="control-btn" class="btn-primary" style="margin-top: 1.5rem; width: 100%;">
                Open Remote Control
            </button>
            <button id="settings-btn" class="btn-secondary" style="margin-top: 0.5rem; width: 100%;">
                Settings
            </button>
        </div>
    `;

    document.getElementById("control-btn")?.addEventListener("click", () => {
        navigateTo("control");
    });

    document.getElementById("settings-btn")?.addEventListener("click", () => {
        navigateTo("settings");
    });
}

function renderControlPage(): void {
    const app = document.querySelector<HTMLDivElement>("#app")!;
    const isTouch = isTouchDevice();

    app.innerHTML = `
        <div class="control-page">
            <div class="control-header">
                <button id="back-btn" class="btn-small">Back</button>
                <span id="connection-status" class="connection-status">Disconnected</span>
                <div class="header-controls">
                    <label for="sensitivity-select" class="sensitivity-label">Mouse speed</label>
                    <select id="sensitivity-select" class="sensitivity-select" title="Mouse speed">
                        ${MOUSE_SENSITIVITY_PRESETS.map(p =>
                            `<option value="${p.value}" ${p.value === getMouseSensitivity() ? 'selected' : ''}>${p.label}</option>`
                        ).join('')}
                    </select>
                    <button id="fullscreen-btn" class="btn-small">Fullscreen</button>
                </div>
            </div>
            <div id="capture-zone" class="capture-zone ${isTouch ? 'touch-mode' : ''}">
                <div class="capture-content">
                    <div class="capture-prompt" id="capture-prompt">
                        ${isTouch ? 'Touch trackpad ready' : 'Click to capture keyboard & mouse'}
                    </div>
                    <div class="keys-display" id="keys-display"></div>
                </div>
            </div>
            <div id="keyboard-section" class="keyboard-section"></div>
            <div class="control-footer">
                ${isTouch ? `
                    <div class="touch-hints">
                        <span>Drag: move</span>
                        <span>Tap: click</span>
                        <span>2-finger tap: right-click</span>
                    </div>
                ` : `
                    <div class="modifiers-display" id="modifiers-display"></div>
                    <div class="control-hint">Press Escape to release</div>
                `}
            </div>
        </div>
    `;

    // Setup back button
    document.getElementById("back-btn")?.addEventListener("click", () => {
        navigateTo("");
    });

    // Setup fullscreen button
    document.getElementById("fullscreen-btn")?.addEventListener("click", toggleFullscreen);

    // Setup HID client
    setupHIDControl(isTouch);
}

function toggleFullscreen(): void {
    if (document.fullscreenElement) {
        document.exitFullscreen();
    } else {
        document.documentElement.requestFullscreen();
    }
}

function setupHIDControl(isTouch: boolean): void {
    const statusEl = document.getElementById("connection-status")!;
    const promptEl = document.getElementById("capture-prompt")!;
    const zoneEl = document.getElementById("capture-zone")!;
    const keysEl = document.getElementById("keys-display")!;
    const modsEl = document.getElementById("modifiers-display");

    // Track current states for combined status display
    let currentConnState: ConnectionState = 'disconnected';
    let currentUSBStatus: USBStatus = { mounted: false, suspended: false };

    // Update status display based on connection and USB state
    function updateStatusDisplay(): void {
        let text: string;
        let cssClass: string;

        if (currentConnState === 'connected') {
            if (currentUSBStatus.suspended) {
                text = 'Connected (Host Sleeping)';
                cssClass = 'connection-usb-suspended';
            } else if (!currentUSBStatus.mounted) {
                text = 'Connected (USB Disconnected)';
                cssClass = 'connection-usb-disconnected';
            } else {
                text = 'Connected';
                cssClass = 'connection-connected';
            }
        } else if (currentConnState === 'displaced') {
            text = 'Disconnected (Another Session)';
            cssClass = 'connection-displaced';
        } else {
            text = currentConnState.charAt(0).toUpperCase() + currentConnState.slice(1);
            cssClass = `connection-${currentConnState}`;
        }

        statusEl.textContent = text;
        statusEl.className = `connection-status ${cssClass}`;

        // Update prompt for touch mode
        if (isTouch && currentConnState === 'connected') {
            promptEl.textContent = 'Touch trackpad ready';
        }
    }

    // Create HID client
    hidClient = new HIDClient({
        onStateChange: (state: ConnectionState) => {
            currentConnState = state;
            updateStatusDisplay();
        },
        onUSBStatusChange: (status: USBStatus) => {
            currentUSBStatus = status;
            updateStatusDisplay();
        },
        onError: (error: string) => {
            console.error("HID error:", error);
        }
    });

    if (isTouch) {
        // Touch mode: use TouchTrackpad
        touchTrackpad = new TouchTrackpad(zoneEl, hidClient);
        zoneEl.classList.add("touch-active");
    } else {
        // Desktop mode: use InputCapture with pointer lock
        inputCapture = new InputCapture(zoneEl, hidClient, {
            onCaptureChange: (captured: boolean) => {
                zoneEl.classList.toggle("captured", captured);
                promptEl.textContent = captured
                    ? "Captured"
                    : "Click to capture keyboard & mouse";
            },
            onKeysChange: (keys: Set<string>, modifiers: Set<string>) => {
                // Update keys display
                keysEl.innerHTML = Array.from(keys)
                    .map(k => `<span class="key-badge">${escapeHtml(getKeyDisplayName(k))}</span>`)
                    .join("");

                // Update modifiers display
                if (modsEl) {
                    const modNames = ['Ctrl', 'Shift', 'Alt', 'Meta'];
                    const activeModNames: string[] = [];
                    for (const mod of modifiers) {
                        if (mod.startsWith('Control')) activeModNames.push('Ctrl');
                        else if (mod.startsWith('Shift')) activeModNames.push('Shift');
                        else if (mod.startsWith('Alt')) activeModNames.push('Alt');
                        else if (mod.startsWith('Meta')) activeModNames.push('Meta');
                    }
                    modsEl.innerHTML = modNames
                        .map(m => `<span class="mod-badge ${activeModNames.includes(m) ? 'active' : ''}">${m}</span>`)
                        .join("");
                }
            }
        });

        // Initialize modifiers display
        if (modsEl) {
            modsEl.innerHTML = ['Ctrl', 'Shift', 'Alt', 'Meta']
                .map(m => `<span class="mod-badge">${m}</span>`)
                .join("");
        }
    }

    // Setup mouse sensitivity control
    const sensitivitySelect = document.getElementById('sensitivity-select') as HTMLSelectElement;
    const currentSensitivity = getMouseSensitivity();

    if (inputCapture) {
        inputCapture.setSensitivity(currentSensitivity);
    }
    if (touchTrackpad) {
        touchTrackpad.setSensitivity(1.5 * currentSensitivity);
    }

    sensitivitySelect?.addEventListener('change', () => {
        const newValue = parseFloat(sensitivitySelect.value);
        setMouseSensitivity(newValue);
        if (inputCapture) {
            inputCapture.setSensitivity(newValue);
        }
        if (touchTrackpad) {
            touchTrackpad.setSensitivity(1.5 * newValue);
        }
    });

    // Setup virtual keyboard
    const keyboardSection = document.getElementById("keyboard-section")!;
    const currentPreset = getLayoutPreset();
    const layouts = isTouch ? MOBILE_LAYOUTS : getDesktopLayouts(currentPreset);

    keyboardManager = new KeyboardManager({
        container: keyboardSection,
        client: hidClient,
        layouts,
        unitSize: isTouch ? 36 : 48,
        gap: isTouch ? 3 : 4,
        isTouch,
    });

    // Add preset selector to tabs row (desktop only)
    if (!isTouch) {
        const tabsEl = keyboardSection.querySelector('.km-tabs');
        if (tabsEl) {
            const select = document.createElement('select');
            select.className = 'layout-select';
            select.innerHTML = `
                <option value="ansi">ANSI (US)</option>
                <option value="iso">ISO (UK)</option>
            `;
            select.value = currentPreset;
            select.addEventListener('change', () => {
                const newPreset = select.value as LayoutPreset;
                setLayoutPreset(newPreset);
                // Recreate keyboard with new layout
                keyboardManager?.destroy();
                keyboardManager = new KeyboardManager({
                    container: keyboardSection,
                    client: hidClient!,
                    layouts: getDesktopLayouts(newPreset),
                    unitSize: 48,
                    gap: 4,
                    isTouch: false,
                });
                // Re-add selector to new tabs
                const newTabsEl = keyboardSection.querySelector('.km-tabs');
                if (newTabsEl && !newTabsEl.contains(select)) {
                    newTabsEl.appendChild(select);
                }
            });
            tabsEl.appendChild(select);
        }
    }

    // Connect
    hidClient.connect();
}

function cleanupControl(): void {
    keyboardManager?.destroy();
    keyboardManager = null;
    inputCapture?.destroy();
    inputCapture = null;
    touchTrackpad?.destroy();
    touchTrackpad = null;
    hidClient?.disconnect();
    hidClient = null;
}

async function renderSettingsPage(): Promise<void> {
    const app = document.querySelector<HTMLDivElement>("#app")!;

    app.innerHTML = `
        <div class="container">
            <h1>NetHID</h1>
            <p class="status-ok">Settings</p>
            <p>Loading settings...</p>
        </div>
    `;

    const settings = await fetchSettings();
    if (!settings) {
        app.innerHTML = `
            <div class="container">
                <h1>NetHID</h1>
                <p class="error">Failed to load settings</p>
                <button id="back-btn" class="btn-secondary">Back</button>
            </div>
        `;
        document.getElementById("back-btn")?.addEventListener("click", () => navigateTo(""));
        return;
    }

    app.innerHTML = `
        <div class="container">
            <h1>NetHID</h1>
            <p class="status-ok">Settings</p>

            <form id="settings-form" class="settings-form">
                <div class="form-group">
                    <label for="hostname-input">Hostname</label>
                    <input type="text" id="hostname-input"
                           value="${escapeHtml(settings.hostname.value)}"
                           maxlength="32"
                           placeholder="Enter hostname">
                    ${settings.hostname.default ? `<p class="form-hint">Using auto-generated default</p>` : ""}
                    <p class="form-hint hostname-hint">Letters, numbers, and hyphens only (1-32 chars)</p>
                </div>

                <div class="settings-section">
                    <h2>MQTT</h2>

                    <div class="form-group form-group-checkbox">
                        <label class="checkbox-label">
                            <input type="checkbox" id="mqtt-enabled" ${settings.mqtt_enabled ? "checked" : ""}>
                            <span>Enable MQTT</span>
                        </label>
                    </div>

                    <div class="mqtt-fields" id="mqtt-fields">
                        <div class="form-group">
                            <label for="mqtt-broker">Broker</label>
                            <input type="text" id="mqtt-broker"
                                   value="${escapeHtml(settings.mqtt_broker)}"
                                   maxlength="63"
                                   placeholder="mqtt.example.com">
                        </div>

                        <div class="form-row">
                            <div class="form-group">
                                <label for="mqtt-port">Port</label>
                                <input type="number" id="mqtt-port"
                                       value="${settings.mqtt_port}"
                                       min="1" max="65535"
                                       placeholder="1883">
                            </div>
                        </div>

                        <div class="form-group">
                            <label for="mqtt-topic">Topic</label>
                            <input type="text" id="mqtt-topic"
                                   value="${escapeHtml(settings.mqtt_topic)}"
                                   maxlength="63"
                                   placeholder="nethid/device1">
                            <p class="form-hint">Device subscribes to {topic}/#</p>
                        </div>

                        <div class="form-group">
                            <label for="mqtt-username">Username <span class="optional">(optional)</span></label>
                            <input type="text" id="mqtt-username"
                                   value="${escapeHtml(settings.mqtt_username)}"
                                   maxlength="31"
                                   placeholder="Leave empty for no auth">
                        </div>

                        <div class="form-group">
                            <label for="mqtt-password">Password <span class="optional">(optional)</span></label>
                            <input type="password" id="mqtt-password"
                                   maxlength="63"
                                   placeholder="${settings.mqtt_has_password ? '(unchanged)' : 'Leave empty for no auth'}">
                            ${settings.mqtt_has_password ? '<p class="form-hint">Leave empty to keep existing password</p>' : ''}
                        </div>

                        <div class="form-group">
                            <label for="mqtt-client-id">Client ID <span class="optional">(optional)</span></label>
                            <input type="text" id="mqtt-client-id"
                                   value="${escapeHtml(settings.mqtt_client_id)}"
                                   maxlength="31"
                                   placeholder="Auto-generated from hostname">
                        </div>
                    </div>
                </div>

                <div class="settings-section">
                    <h2>Syslog</h2>
                    <p class="form-hint">Remote logging via UDP syslog. Requires reboot to apply.</p>

                    <div class="form-group">
                        <label for="syslog-server">Server IP</label>
                        <input type="text" id="syslog-server"
                               value="${escapeHtml(settings.syslog_server)}"
                               maxlength="15"
                               placeholder="Leave empty to disable"
                               pattern="^$|^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$">
                        <p class="form-hint">IPv4 address only (e.g., 192.168.1.100)</p>
                    </div>

                    <div class="form-group">
                        <label for="syslog-port">Port</label>
                        <input type="number" id="syslog-port"
                               value="${settings.syslog_port}"
                               min="1" max="65535"
                               placeholder="514">
                    </div>
                </div>

                <div id="settings-message" class="status-message"></div>

                <button type="submit" id="save-btn" class="btn-primary">Save Settings</button>
            </form>

            <div class="settings-actions">
                <button id="back-btn" class="btn-secondary">Back to Status</button>
                <button id="reboot-btn" class="btn-secondary btn-danger" style="margin-top: 0.5rem;">Reboot Device</button>
            </div>
        </div>
    `;

    // Back button
    document.getElementById("back-btn")?.addEventListener("click", () => navigateTo(""));

    // Reboot button
    document.getElementById("reboot-btn")?.addEventListener("click", async () => {
        if (!confirm("Reboot the device?")) return;
        const messageEl = document.getElementById("settings-message")!;
        messageEl.textContent = "Rebooting...";
        messageEl.className = "status-message status-info";
        await rebootDevice();
        messageEl.textContent = "Device is rebooting. Please wait...";
    });

    // MQTT enable/disable toggle
    const mqttEnabledCheckbox = document.getElementById("mqtt-enabled") as HTMLInputElement;
    const mqttFieldsDiv = document.getElementById("mqtt-fields") as HTMLDivElement;

    function updateMqttFieldsVisibility(): void {
        mqttFieldsDiv.style.display = mqttEnabledCheckbox.checked ? "block" : "none";
    }

    mqttEnabledCheckbox?.addEventListener("change", updateMqttFieldsVisibility);
    updateMqttFieldsVisibility();

    // Form submission
    document.getElementById("settings-form")?.addEventListener("submit", async (e) => {
        e.preventDefault();
        const hostnameInput = document.getElementById("hostname-input") as HTMLInputElement;
        const messageEl = document.getElementById("settings-message")!;
        const hostname = hostnameInput.value.trim();

        // Validate hostname
        if (!isValidHostname(hostname)) {
            messageEl.textContent = "Invalid hostname format";
            messageEl.className = "status-message status-error";
            return;
        }

        // Gather MQTT settings
        const mqttEnabled = (document.getElementById("mqtt-enabled") as HTMLInputElement).checked;
        const mqttBroker = (document.getElementById("mqtt-broker") as HTMLInputElement).value.trim();
        const mqttPort = parseInt((document.getElementById("mqtt-port") as HTMLInputElement).value) || 1883;
        const mqttTopic = (document.getElementById("mqtt-topic") as HTMLInputElement).value.trim();
        const mqttUsername = (document.getElementById("mqtt-username") as HTMLInputElement).value.trim();
        const mqttPassword = (document.getElementById("mqtt-password") as HTMLInputElement).value;
        const mqttClientId = (document.getElementById("mqtt-client-id") as HTMLInputElement).value.trim();

        // Validate MQTT settings if enabled
        if (mqttEnabled) {
            if (!mqttBroker) {
                messageEl.textContent = "MQTT broker is required when MQTT is enabled";
                messageEl.className = "status-message status-error";
                return;
            }
            if (!mqttTopic) {
                messageEl.textContent = "MQTT topic is required when MQTT is enabled";
                messageEl.className = "status-message status-error";
                return;
            }
            if (mqttPort < 1 || mqttPort > 65535) {
                messageEl.textContent = "MQTT port must be between 1 and 65535";
                messageEl.className = "status-message status-error";
                return;
            }
        }

        // Show saving status
        messageEl.textContent = "Saving...";
        messageEl.className = "status-message status-info";

        // Gather syslog settings
        const syslogServer = (document.getElementById("syslog-server") as HTMLInputElement).value.trim();
        const syslogPort = parseInt((document.getElementById("syslog-port") as HTMLInputElement).value) || 514;

        // Build settings object
        const settingsToSave: { hostname: string } & MqttSettings & SyslogSettings = {
            hostname,
            mqtt_enabled: mqttEnabled,
            mqtt_broker: mqttBroker,
            mqtt_port: mqttPort,
            mqtt_topic: mqttTopic,
            mqtt_username: mqttUsername,
            mqtt_client_id: mqttClientId,
            syslog_server: syslogServer,
            syslog_port: syslogPort,
        };

        // Only include password if it was changed (not empty)
        if (mqttPassword) {
            settingsToSave.mqtt_password = mqttPassword;
        }

        const result = await saveSettings(settingsToSave);

        if (result.success) {
            messageEl.textContent = "Settings saved! Changes take effect after reboot.";
            messageEl.className = "status-message status-success";
        } else {
            messageEl.textContent = "Error: " + (result.error || "Unknown error");
            messageEl.className = "status-message status-error";
        }
    });

    // Real-time hostname validation
    document.getElementById("hostname-input")?.addEventListener("input", (e) => {
        const input = e.target as HTMLInputElement;
        const hintEl = document.querySelector(".hostname-hint") as HTMLElement;
        if (input.value.length > 0 && !isValidHostname(input.value)) {
            hintEl.classList.add("hint-error");
        } else {
            hintEl.classList.remove("hint-error");
        }
    });
}

function renderWifiConfigPage(status: DeviceStatus, networks: NetworksResponse | null): void {
    const app = document.querySelector<HTMLDivElement>("#app")!;

    const networkList = networks?.networks || [];
    const scanning = networks?.scanning || false;

    app.innerHTML = `
        <div class="container">
            <h1>NetHID Setup</h1>
            <p class="status-ap">WiFi Configuration</p>

            <div class="device-info">
                <span class="info-item"><strong>IP:</strong> ${status.ip}</span>
                <span class="info-item"><strong>MAC:</strong> ${status.mac}</span>
            </div>

            <div class="wifi-section">
                <div class="section-header">
                    <h2>Available Networks</h2>
                    <button id="refresh-btn" class="btn-small" ${scanning ? "disabled" : ""}>
                        ${scanning ? "Scanning..." : "Refresh"}
                    </button>
                </div>

                ${scanning && networkList.length === 0 ? '<p class="scanning-text">Scanning for networks...</p>' : ""}

                <div class="network-list">
                    ${networkList.length === 0 && !scanning
                        ? '<p class="no-networks">No networks found. Tap Refresh to scan.</p>'
                        : networkList.map(net => `
                            <div class="network-item ${selectedNetwork === net.ssid ? "selected" : ""}"
                                 data-ssid="${escapeHtml(net.ssid)}">
                                <div class="network-info">
                                    <span class="network-ssid">${escapeHtml(net.ssid)}</span>
                                    <span class="network-security">${net.auth}</span>
                                </div>
                                <div class="network-signal">
                                    ${renderSignalBars(net.rssi)}
                                </div>
                            </div>
                        `).join("")
                    }
                </div>
            </div>

            <div class="password-section" style="${selectedNetwork ? "" : "display:none"}">
                <h2>Connect to "${selectedNetwork ? escapeHtml(selectedNetwork) : ""}"</h2>
                <form id="wifi-form">
                    <input type="password" id="password-input"
                           placeholder="WiFi Password"
                           autocomplete="off">
                    <button type="submit" id="connect-btn" class="btn-primary">
                        Connect
                    </button>
                </form>
            </div>

            <div id="status-message" class="status-message"></div>
        </div>
    `;

    attachWifiEventListeners();
}

function attachWifiEventListeners(): void {
    // Network selection
    document.querySelectorAll(".network-item").forEach(item => {
        item.addEventListener("click", () => {
            selectedNetwork = (item as HTMLElement).dataset.ssid || null;
            renderWifiConfigPageWithCachedData();
        });
    });

    // Refresh button
    document.getElementById("refresh-btn")?.addEventListener("click", async () => {
        await triggerScan();
        startPolling();
    });

    // Form submission
    document.getElementById("wifi-form")?.addEventListener("submit", async (e) => {
        e.preventDefault();
        const passwordInput = document.getElementById("password-input") as HTMLInputElement;
        const password = passwordInput.value;

        if (!selectedNetwork) return;

        showStatus("Saving credentials...", "info");
        const result = await saveCredentials(selectedNetwork, password);

        if (result.success) {
            showStatus("Saved! Device will reboot and connect to " + selectedNetwork + ". You will lose this connection.", "success");
            stopPolling();
        } else {
            showStatus("Error: " + (result.message || "Unknown error"), "error");
        }
    });
}

function showStatus(message: string, type: "info" | "success" | "error"): void {
    const el = document.getElementById("status-message");
    if (el) {
        el.textContent = message;
        el.className = `status-message status-${type}`;
    }
}

function startPolling(): void {
    if (pollInterval !== null) return;

    pollInterval = window.setInterval(async () => {
        const networks = await fetchNetworks();
        if (networks) {
            cachedNetworks = networks;
            if (!networks.scanning) {
                stopPolling();
            }
            renderWifiConfigPageWithCachedData();
        }
    }, 1500);
}

function stopPolling(): void {
    if (pollInterval !== null) {
        clearInterval(pollInterval);
        pollInterval = null;
    }
}

let cachedStatus: DeviceStatus | null = null;

async function renderWifiConfigPageWithCachedData(): Promise<void> {
    if (cachedStatus) {
        renderWifiConfigPage(cachedStatus, cachedNetworks);
    }
}

function render(status: DeviceStatus | null): void {
    const app = document.querySelector<HTMLDivElement>("#app")!;

    if (status === null) {
        app.innerHTML = `
            <div class="container">
                <h1>NetHID</h1>
                <p class="error">Unable to connect to device</p>
                <button id="retry">Retry</button>
            </div>
        `;
        document.querySelector<HTMLButtonElement>("#retry")?.addEventListener("click", init);
        return;
    }

    cachedStatus = status;

    if (status.mode === "ap") {
        renderWifiConfigPage(status, cachedNetworks);
    } else {
        routeByHash();
    }
}

async function init(): Promise<void> {
    const app = document.querySelector<HTMLDivElement>("#app")!;
    app.innerHTML = `
        <div class="container">
            <h1>NetHID</h1>
            <p>Loading...</p>
        </div>
    `;

    const status = await fetchStatus();

    // If in AP mode, also fetch networks
    if (status?.mode === "ap") {
        cachedNetworks = await fetchNetworks();
        // Start polling if scan is in progress
        if (cachedNetworks?.scanning) {
            startPolling();
        }
    }

    render(status);
}

init();
