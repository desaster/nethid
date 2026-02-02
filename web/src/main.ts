import "./style.css";

interface DeviceStatus {
    hostname: string;
    mac: string;
    ip: string;
    uptime: number;
    mode: "sta" | "ap";
}

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
        </div>
    `;
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
