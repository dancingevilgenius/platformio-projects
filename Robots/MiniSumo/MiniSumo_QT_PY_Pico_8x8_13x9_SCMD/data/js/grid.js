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
                updateCellColor(i, on ? 0x00FF00 : 0);
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
    if (rgb === 0) {
        cells[index].style.background = "#444"; // lighter inactive color
        return;
    }

    let r = (rgb >> 16) & 0xFF;
    let g = (rgb >> 8) & 0xFF;
    let b = rgb & 0xFF;

    cells[index].style.background = `rgb(${r},${g},${b})`;
}

// --- Send WS command ---
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

function runCalibrate() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send("CALIBRATE");
    } else {
        showSnackbar("WebSocket not connected", "warning");
    }
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
        sendCommand("MODE:" + modeSelect.value);

        const pkt = (modeSelect.value === "0") ? 256 : 8;
        document.getElementById("pkt").textContent = pad3(pkt);
    };
}

// --- Mode dropdown (Grid <-> DPad) ---
document.addEventListener("DOMContentLoaded", () => {
    cells = Array.from(document.querySelectorAll(".grid-cell"));

    connectWebSocket();
    setupControls();

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
    document.getElementById("calibrateBtn").onclick = runCalibrate;
    document.getElementById("btnStart").onclick = beginCountdown;
    document.getElementById("btnStop").onclick = stopMatch;

    const sideToggle = document.getElementById("sideToggle");
    const sideLeftLabel = document.getElementById("sideLeftLabel");
    const sideRightLabel = document.getElementById("sideRightLabel");
    const updateSideLabels = () => {
        const right = sideToggle.checked;
        sideLeftLabel.classList.toggle("is-selected", !right);
        sideRightLabel.classList.toggle("is-selected", right);
    };
    sideToggle.onchange = updateSideLabels;
    updateSideLabels();

    // Initialize PKT display on load
    const modeSelect = document.getElementById("modeSelect");
    const pkt = (modeSelect.value === "0") ? 256 : 8;
    document.getElementById("pkt").textContent = pad3(pkt);
});
