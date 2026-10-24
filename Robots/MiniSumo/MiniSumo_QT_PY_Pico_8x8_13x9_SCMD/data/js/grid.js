// --- Formatting helpers ---
function pad2(n) { return n.toString().padStart(2, ' '); }
function pad3(n) { return n.toString().padStart(3, ' '); }
function padLatency(ms) { return ms.toFixed(1).padStart(5, ' '); }

// --- Snackbar ---
function showSnackbar(message, type = "success") {
    const container = document.getElementById("snackbar-container");
    const bar = document.createElement("div");
    bar.className = "snackbar";

    if (type === "success") bar.classList.add("snackbar-success");
    if (type === "warning") bar.classList.add("snackbar-warning");
    if (type === "error")   bar.classList.add("snackbar-error");

    bar.textContent = message;

    bar.addEventListener("click", () => {
        bar.classList.remove("show");
        setTimeout(() => bar.remove(), 300);
    });

    container.appendChild(bar);
    void bar.offsetWidth;
    bar.classList.add("show");

    setTimeout(() => {
        bar.classList.remove("show");
        setTimeout(() => bar.remove(), 300);
    }, 6000);
}

// --- WebSocket globals ---
let socket = null;
let reconnectTimer = null;
let lastPingTime = 0;

let frameCount = 0;
let lastFPSUpdate = performance.now();

let cells = [];
let sensorView = "both";

function rowOfCell(index) {
    return Math.floor(index / 8);
}

function isRowEnabled(row) {
    if (sensorView === "edge") return row === 6 || row === 7;
    if (sensorView === "opponent") return row <= 4;
    return true;
}

function triggeredColor(index) {
    const row = rowOfCell(index);
    if (row === 7) return 0xFF0000;
    if (row === 6) return 0xFFFF00;
    return 0x00FF00;
}

function applySensorView(view, notifyServer) {
    sensorView = view;
    for (let i = 0; i < cells.length; i++) {
        if (!isRowEnabled(rowOfCell(i))) updateCellColor(i, 0);
    }
    if (notifyServer) sendCommand("SENSORS:" + view);
}

// --- WebSocket connection ---
function connectWebSocket() {
    const host = window.location.hostname;
    socket = new WebSocket(`ws://${host}/ws`);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
        socket.send("UA:" + navigator.userAgent);
        lastPingTime = performance.now();
        socket.send("PING");
        if (reconnectTimer) clearTimeout(reconnectTimer);
    };

    socket.onclose = () => {
        reconnectTimer = setTimeout(connectWebSocket, 500);
    };

    socket.onerror = () => socket.close();

    socket.onmessage = (event) => {

        // --- TEXT MESSAGES ---
        if (typeof event.data === "string") {

            // FIXED: Accept "PONG" or "PONG:<id>"
            if (event.data.startsWith("PONG")) {
                const latency = performance.now() - lastPingTime;
                document.getElementById("lat").textContent = padLatency(latency);
                return;
            }

            // SNACK messages
            if (event.data === "CALIBRATE_SENSOR_DONE" || event.data === "CALIBRATE_DRIVE_DONE") {
                resetCalibrateSelect();
                showSnackbar(
                    event.data === "CALIBRATE_SENSOR_DONE"
                        ? "Sensor calibration complete"
                        : "Drive calibration complete",
                    "success"
                );
                return;
            }

            if (event.data.startsWith("MODE:")) {
                const mode = event.data.substring(5);
                const modeSelect = document.getElementById("modeSelect");
                if (modeSelect) {
                    modeSelect.value = (mode === "1" || mode === "bitmask" || mode === "bit") ? "1" : "0";
                    const pkt = (modeSelect.value === "0") ? 256 : 8;
                    document.getElementById("pkt").textContent = pad3(pkt);
                }
                return;
            }

            if (event.data.startsWith("SENSORS:")) {
                const view = event.data.substring(8);
                const sensorSelect = document.getElementById("sensorSelect");
                if (sensorSelect && (view === "both" || view === "edge" || view === "opponent")) {
                    sensorSelect.value = view;
                    applySensorView(view, false);
                }
                return;
            }

            if (event.data.startsWith("EDGE_MIN:")) {
                const parts = event.data.substring(9).split(",");
                setEdgeMinSelect("redMinSelect", parts[0]);
                setEdgeMinSelect("yellowMinSelect", parts[1]);
                if (parts.length > 2) setEdgeMinSelect("opponentMaxSelect", parts[2]);
                return;
            }

            if (event.data.startsWith("SNACK:")) {
                const parts = event.data.split(":");
                const type = parts[1];
                const msg  = parts.slice(2).join(":");
                showSnackbar(msg, type);
                return;
            }

            return;
        }

        // --- BINARY FRAMES ---
        const buf = event.data;

        // DO NOT update PKT here — PKT is UI-driven
        // document.getElementById("pkt").textContent = pad3(buf.byteLength);

        frameCount++;
        updateFPS();   // backend FPS measurement

        // 256-byte RGB frame
        if (buf.byteLength === 256) {
            const view = new DataView(buf);
            for (let i = 0; i < 64; i++) {
                const rgb = view.getUint32(i * 4, true);
                updateCellColor(i, rgb);
            }
        }

        // 8-byte bitmask frame
        else if (buf.byteLength === 8) {
            const view = new DataView(buf);
            const bits = view.getBigUint64(0, true);

            for (let i = 0; i < 64; i++) {
                const on = ((bits >> BigInt(i)) & 1n) === 1n;
                updateCellColor(i, on ? triggeredColor(i) : 0);
            }
        }
    };
}

// --- Periodic PING ---
setInterval(() => {
    if (socket && socket.readyState === WebSocket.OPEN) {
        lastPingTime = performance.now();
        socket.send("PING");
    }
}, 2000);

// --- Backend FPS measurement ---
function updateFPS() {
    const now = performance.now();
    const elapsed = now - lastFPSUpdate;

    if (elapsed >= 1000) {
        const fps = Math.round((frameCount / elapsed) * 1000);
        document.getElementById("fps").textContent = pad2(fps);
        frameCount = 0;
        lastFPSUpdate = now;
    }
}

// --- Update grid cell color ---
function updateCellColor(index, rgb) {
    const cell = cells[index];
    if (!cell) return;

    if (!isRowEnabled(rowOfCell(index))) {
        cell.classList.add("is-filtered");
        cell.style.background = "#1a1a1a";
        return;
    }

    cell.classList.remove("is-filtered");

    if (rgb === 0) {
        cell.style.background = "#444"; // lighter inactive color
        return;
    }

    let r = (rgb >> 16) & 0xFF;
    let g = (rgb >> 8) & 0xFF;
    let b = rgb & 0xFF;

    cell.style.background = `rgb(${r},${g},${b})`;
}

// --- Send WS command ---
function fillCmSelect(select, minCm, maxCm, selectedCm) {
    select.innerHTML = "";
    for (let cm = minCm; cm <= maxCm; cm++) {
        const opt = document.createElement("option");
        opt.value = cm + " cm";
        opt.textContent = cm + " cm";
        if (cm === selectedCm) opt.selected = true;
        select.appendChild(opt);
    }
}

function setEdgeMinSelect(id, cm) {
    const select = document.getElementById(id);
    if (!select) return;
    const value = String(cm).replace(" cm", "") + " cm";
    if ([...select.options].some((opt) => opt.value === value)) {
        select.value = value;
    }
}

let applyingSettings = false;

function postSetting(fields) {
    fetch("/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(fields)
    }).catch(() => showSnackbar("Could not save settings", "error"));
}

function applyLoadedSettings(data) {
    applyingSettings = true;
    const modeSelect = document.getElementById("modeSelect");
    const sensorSelect = document.getElementById("sensorSelect");
    if (modeSelect && data.mode) {
        modeSelect.value = (data.mode === "bitmask" || data.mode === "1" || data.mode === "bit") ? "1" : "0";
        const pkt = (modeSelect.value === "0") ? 256 : 8;
        document.getElementById("pkt").textContent = pad3(pkt);
    }
    if (sensorSelect && (data.sensors === "both" || data.sensors === "edge" || data.sensors === "opponent")) {
        sensorSelect.value = data.sensors;
        applySensorView(data.sensors, false);
    }
    if (data.edgeRedMinCm != null) setEdgeMinSelect("redMinSelect", data.edgeRedMinCm);
    if (data.edgeYellowMinCm != null) setEdgeMinSelect("yellowMinSelect", data.edgeYellowMinCm);
    if (data.opponentMaxCm != null) setEdgeMinSelect("opponentMaxSelect", data.opponentMaxCm);
    const powerSelect = document.getElementById("powerSelect");
    if (powerSelect && data.powerPercent != null) {
        powerSelect.value = String(data.powerPercent);
    }
    applyingSettings = false;
}

function loadSettings() {
    fetch("/settings")
        .then((res) => res.json())
        .then(applyLoadedSettings)
        .catch(() => {});
}

function sendCommand(cmd) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(cmd);
    }
}

function runHealthChecks() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send("HEALTHCHECK");
    } else {
        showSnackbar("WebSocket not connected", "warning");
    }
}

function resetCalibrateSelect() {
    const sel = document.getElementById("calibrateSelect");
    if (!sel) return;
    sel.disabled = false;
    sel.value = "";
}

function runCalibrateChoice() {
    const sel = document.getElementById("calibrateSelect");
    if (!sel) return;
    const choice = sel.value;
    if (!choice) return;

    const actions = {
        driveLeft: "calibrateDriveLeft",
        driveRight: "calibrateDriveRight",
        driveBoth: "calibrateDriveBoth",
        sensor: "calibrateSensor"
    };
    const action = actions[choice];
    if (!action) return;

    sel.disabled = true;
    sendAction(action);
    setTimeout(() => {
        if (sel.disabled) resetCalibrateSelect();
    }, 7000);
}

function sendAction(action) {
    fetch("/controller", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: action })
    }).catch(() => showSnackbar("Could not reach the robot", "error"));
}

let countdownTimer = null;

function resetMatchButtons() {
    const countdown = document.getElementById("countdown");
    document.getElementById("btnStart").disabled = false;
    document.getElementById("btnStop").disabled = true;
    countdown.hidden = true;
    countdown.textContent = "";
}

function beginCountdown() {
    const countdown = document.getElementById("countdown");
    const stopBtn = document.getElementById("btnStop");
    if (countdownTimer) clearInterval(countdownTimer);

    document.getElementById("btnStart").disabled = true;
    stopBtn.disabled = true;

    let remaining = 5;
    countdown.hidden = false;
    countdown.textContent = String(remaining);

    countdownTimer = setInterval(() => {
        remaining -= 1;
        countdown.textContent = String(remaining);
        if (remaining <= 0) {
            clearInterval(countdownTimer);
            countdownTimer = null;
            countdown.hidden = true;
            stopBtn.disabled = false;
            sendAction("start");
        }
    }, 1000);
}

function stopMatch() {
    if (countdownTimer) {
        clearInterval(countdownTimer);
        countdownTimer = null;
    }
    sendAction("stop");
    resetMatchButtons();
}

// --- UI Controls (RUN / FPS / MODE) ---
function setupControls() {
    const btnRun = document.getElementById("btnRun");
    const fpsSlider = document.getElementById("fpsSlider");
    const modeSelect = document.getElementById("modeSelect");
    const sensorSelect = document.getElementById("sensorSelect");
    const redMinSelect = document.getElementById("redMinSelect");
    const yellowMinSelect = document.getElementById("yellowMinSelect");
    const opponentMaxSelect = document.getElementById("opponentMaxSelect");

    fillCmSelect(redMinSelect, 10, 20, 10);
    fillCmSelect(yellowMinSelect, 15, 30, 15);
    fillCmSelect(opponentMaxSelect, 15, 75, 57);

    let running = true;

    btnRun.onclick = () => {
        running = !running;
        btnRun.textContent = running ? "Pause" : "Resume";
        sendCommand("RUN:" + (running ? "1" : "0"));
    };

    // FPS slider sends backend request ONLY
    fpsSlider.oninput = () => {
        sendCommand("FPS:" + fpsSlider.value);
    };

    // Mode dropdown updates PKT + backend
    modeSelect.onchange = () => {
        if (applyingSettings) return;
        const pkt = (modeSelect.value === "0") ? 256 : 8;
        document.getElementById("pkt").textContent = pad3(pkt);
        postSetting({ mode: modeSelect.value === "1" ? "bitmask" : "full" });
    };

    sensorSelect.onchange = () => {
        if (applyingSettings) return;
        applySensorView(sensorSelect.value, false);
        postSetting({ sensors: sensorSelect.value });
    };

    redMinSelect.onchange = () => {
        if (applyingSettings) return;
        postSetting({ edgeRedMinCm: parseInt(redMinSelect.value, 10) });
    };
    yellowMinSelect.onchange = () => {
        if (applyingSettings) return;
        postSetting({ edgeYellowMinCm: parseInt(yellowMinSelect.value, 10) });
    };
    opponentMaxSelect.onchange = () => {
        if (applyingSettings) return;
        postSetting({ opponentMaxCm: parseInt(opponentMaxSelect.value, 10) });
    };
    const powerSelect = document.getElementById("powerSelect");
    powerSelect.onchange = () => {
        if (applyingSettings) return;
        postSetting({ powerPercent: parseInt(powerSelect.value, 10) });
    };

    loadSettings();
}

// --- Mode dropdown (Grid <-> DPad) ---
document.addEventListener("DOMContentLoaded", () => {
    cells = Array.from(document.querySelectorAll(".grid-cell"));

    setupControls();
    connectWebSocket();

    const dropdown = document.getElementById("modeDropdown");
    if (dropdown) {
        dropdown.onchange = () => {
            if (dropdown.value === "dpad") {
                window.location = "/index_dpad.html";
            }
        };
    }

    const settingsPopup = document.getElementById("settingsPopup");
    const openSettings = () => { settingsPopup.hidden = false; };
    const closeSettings = () => { settingsPopup.hidden = true; };
    document.getElementById("settingsBtn").onclick = openSettings;
    document.getElementById("settingsArrow").onclick = closeSettings;
    document.getElementById("settingsClose").onclick = closeSettings;
    document.getElementById("healthCheckBtn").onclick = runHealthChecks;
    document.getElementById("calibrateSelect").onchange = runCalibrateChoice;
    document.getElementById("btnStart").onclick = beginCountdown;
    document.getElementById("btnStop").onclick = stopMatch;

    // Initialize PKT display on load
    const modeSelect = document.getElementById("modeSelect");
    const pkt = (modeSelect.value === "0") ? 256 : 8;
    document.getElementById("pkt").textContent = pad3(pkt);
});
