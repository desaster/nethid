import "./style.css";
import { HIDClient, InputCapture, TouchTrackpad, getKeyDisplayName, isTouchDevice } from "./hid";
import type { ConnectionState } from "./hid";
import { KeyboardManager } from "./keyboard";
import { getDesktopLayouts, MOBILE_LAYOUTS, type LayoutPreset } from "./keyboard-layouts";

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

interface DeviceStatus {
    hostname: string;
    mac: string;
    ip: string;
    uptime: number;
    mode: "sta" | "ap";
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
                <tr><th>Hostname</th><td>${status.hostname}</td></tr>
                <tr><th>MAC</th><td>${status.mac}</td></tr>
                <tr><th>IP</th><td>${status.ip}</td></tr>
                <tr><th>Mode</th><td>Station</td></tr>
                <tr><th>Uptime</th><td>${formatUptime(status.uptime)}</td></tr>
            </table>
            <button id="control-btn" class="btn-primary" style="margin-top: 1.5rem; width: 100%;">
                Open Remote Control
            </button>
        </div>
    `;

    document.getElementById("control-btn")?.addEventListener("click", () => {
        renderControlPage();
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
                <button id="fullscreen-btn" class="btn-small">Fullscreen</button>
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
        cleanupControl();
        init();
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

    // Create HID client
    hidClient = new HIDClient({
        onStateChange: (state: ConnectionState) => {
            statusEl.textContent = state.charAt(0).toUpperCase() + state.slice(1);
            statusEl.className = `connection-status connection-${state}`;

            // Update prompt for touch mode
            if (isTouch && state === 'connected') {
                promptEl.textContent = 'Touch trackpad ready';
            }
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
        renderStatusPage(status);
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
