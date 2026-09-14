const connectionState = document.getElementById("connectionState");
const alertBanner = document.getElementById("alertBanner");
const sequenceValue = document.getElementById("sequenceValue");
const timestampValue = document.getElementById("timestampValue");
const gapWarningsValue = document.getElementById("gapWarningsValue");
const cpuCountValue = document.getElementById("cpuCountValue");
const cpuValue = document.getElementById("cpuValue");
const memoryValue = document.getElementById("memoryValue");
const temperatureValue = document.getElementById("temperatureValue");
const loadValue = document.getElementById("loadValue");
const swapValue = document.getElementById("swapValue");
const rxValue = document.getElementById("rxValue");
const txValue = document.getElementById("txValue");
const processCountValue = document.getElementById("processCountValue");
const loadCpuHint = document.getElementById("loadCpuHint");
const cpuFill = document.getElementById("cpuFill");
const memoryFill = document.getElementById("memoryFill");
const temperatureFill = document.getElementById("temperatureFill");
const swapFill = document.getElementById("swapFill");
const cpuCard = document.getElementById("cpuCard");
const memoryCard = document.getElementById("memoryCard");
const temperatureCard = document.getElementById("temperatureCard");
const loadCard = document.getElementById("loadCard");
const swapCard = document.getElementById("swapCard");
const cpuState = document.getElementById("cpuState");
const memoryState = document.getElementById("memoryState");
const temperatureState = document.getElementById("temperatureState");
const loadState = document.getElementById("loadState");
const swapState = document.getElementById("swapState");
const temperatureNote = document.getElementById("temperatureNote");
const eventList = document.getElementById("eventList");
const intervalInput = document.getElementById("intervalInput");

const THRESHOLDS = {
  cpu: 85,
  memory: 90,
  temperature: 80,
  swap: 50,
};

const sendCommand = (payload) => {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(payload));
  }
};

const setConnection = (state, label) => {
  connectionState.dataset.state = state;
  connectionState.textContent = label;
};

const showAlert = (message) => {
  if (!message) {
    alertBanner.hidden = true;
    alertBanner.textContent = "";
    return;
  }
  alertBanner.hidden = false;
  alertBanner.textContent = message;
};

const alertCopy = (code) => {
  switch (code) {
    case "cpu_high":
      return "CPU above safety threshold (85%)";
    case "memory_high":
      return "Memory above safety threshold (90%)";
    case "temperature_high":
      return "Temperature above safety threshold (80°C)";
    case "load_high":
      return "Load average near logical CPU count";
    case "sequence_gap":
      return "Packet delivery gap detected";
    case "stream_timeout":
      return "Telemetry stream lost — no frames for >2s";
    default:
      return code || "";
  }
};

const pushEvent = (message, tone = "") => {
  const item = document.createElement("li");
  const time = document.createElement("time");
  time.textContent = new Date().toLocaleTimeString("en-GB");
  const msg = document.createElement("span");
  msg.className = `msg${tone ? ` ${tone}` : ""}`;
  msg.textContent = message;
  item.append(time, msg);
  eventList.prepend(item);
  while (eventList.children.length > 40) {
    eventList.lastElementChild?.remove();
  }
};

const setMetricLevel = (card, pill, value, threshold) => {
  const danger = value >= threshold;
  const warn = !danger && value >= threshold * 0.85;
  card.classList.toggle("is-alert", danger);
  pill.dataset.level = danger ? "danger" : warn ? "warn" : "ok";
  pill.textContent = danger ? "alert" : warn ? "elevated" : "nominal";
};

const drawSpark = (svg, values, maxHint = 100) => {
  if (!svg || !values?.length) {
    return;
  }
  const width = 280;
  const height = 64;
  const max = Math.max(maxHint, ...values, 1);
  const step = values.length > 1 ? width / (values.length - 1) : width;
  const points = values
    .map((value, index) => {
      const x = index * step;
      const y = height - (value / max) * (height - 6) - 3;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");
  svg.innerHTML = `
    <polyline fill="none" stroke="rgba(62,207,154,0.85)" stroke-width="2"
      stroke-linecap="round" stroke-linejoin="round" points="${points}" />
  `;
};

const formatNumber = (value, digits = 1) =>
  Number.isFinite(value) ? value.toFixed(digits) : "—";

const formatRate = (bytesPerSec) => {
  if (!Number.isFinite(bytesPerSec)) {
    return "—";
  }
  if (bytesPerSec >= 1_000_000) {
    return `${(bytesPerSec / 1_000_000).toFixed(2)} MB/s`;
  }
  if (bytesPerSec >= 1_000) {
    return `${(bytesPerSec / 1_000).toFixed(1)} KB/s`;
  }
  return `${Math.round(bytesPerSec)} B/s`;
};

const handleTelemetry = (data) => {
  const cpu = Number(data.cpuPercent);
  const memory = Number(data.memoryPercent);
  const temperature = Number(data.temperatureC);
  const load1 = Number(data.loadAverage1);
  const swap = Number(data.swapPercent);
  const cpuCount = Number(data.logicalCpuCount);
  const hasTemperature = Boolean(data.hasTemperature);

  sequenceValue.textContent = String(data.sequence ?? "—");
  timestampValue.textContent = String(data.timestampMs ?? "—");
  gapWarningsValue.textContent = String(data.gapWarnings ?? 0);
  cpuCountValue.textContent = String(data.logicalCpuCount ?? "—");

  cpuValue.textContent = formatNumber(cpu);
  memoryValue.textContent = formatNumber(memory);
  loadValue.textContent = formatNumber(load1, 2);
  swapValue.textContent = formatNumber(swap);
  rxValue.textContent = formatRate(Number(data.networkRxBps));
  txValue.textContent = formatRate(Number(data.networkTxBps));
  processCountValue.textContent = String(data.processCount ?? "—");
  loadCpuHint.textContent = String(data.logicalCpuCount ?? "—");

  cpuFill.style.width = `${Math.min(100, Math.max(0, cpu))}%`;
  memoryFill.style.width = `${Math.min(100, Math.max(0, memory))}%`;
  swapFill.style.width = `${Math.min(100, Math.max(0, swap))}%`;

  setMetricLevel(cpuCard, cpuState, cpu, THRESHOLDS.cpu);
  setMetricLevel(memoryCard, memoryState, memory, THRESHOLDS.memory);
  setMetricLevel(swapCard, swapState, swap, THRESHOLDS.swap);

  const loadThreshold = Math.max(1, cpuCount) * 0.9;
  setMetricLevel(loadCard, loadState, load1, loadThreshold);

  drawSpark(document.getElementById("cpuSpark"), data.cpuHistory ?? [], 100);
  drawSpark(document.getElementById("memorySpark"), data.memoryHistory ?? [], 100);

  if (hasTemperature) {
    temperatureValue.textContent = formatNumber(temperature);
    temperatureFill.style.width = `${Math.min(100, Math.max(0, temperature))}%`;
    temperatureNote.textContent = "Thermal / hwmon reading from the vECU kernel view";
    setMetricLevel(temperatureCard, temperatureState, temperature, THRESHOLDS.temperature);
  } else {
    temperatureValue.textContent = "n/a";
    temperatureFill.style.width = "0%";
    temperatureNote.textContent =
      "No thermal_zone/hwmon in this container (common on Docker Desktop)";
    temperatureCard.classList.remove("is-alert");
    temperatureState.dataset.level = "warn";
    temperatureState.textContent = "unavailable";
  }

  const alert = data.alert || "";
  showAlert(alertCopy(alert));
  if (alert === "sequence_gap") {
    pushEvent("Sequence gap — possible packet loss", "warn");
  } else if (alert) {
    pushEvent(alertCopy(alert), "danger");
  } else {
    pushEvent(
      `seq ${data.sequence} · CPU ${formatNumber(cpu)}% · MEM ${formatNumber(memory)}% · load ${formatNumber(load1, 2)}` +
        (hasTemperature ? ` · ${formatNumber(temperature)}°C` : ""),
    );
  }
};

const handleMessage = (raw) => {
  let data;
  try {
    data = JSON.parse(raw);
  } catch {
    return;
  }

  if (data.type === "telemetry") {
    setConnection("live", "Live");
    handleTelemetry(data);
    return;
  }

  if (data.type === "alert") {
    showAlert(alertCopy(data.alert) || data.message || "Alert");
    pushEvent(data.message || alertCopy(data.alert), "danger");
    if (data.alert === "stream_timeout") {
      setConnection("down", "Stream lost");
    }
    return;
  }

  if (data.type === "ack") {
    pushEvent(`ACK ${data.action}${data.ms ? ` → ${data.ms} ms` : ""}`);
    return;
  }

  if (data.type === "error") {
    pushEvent(`Command error: ${data.message}`, "danger");
  }
};

let socket;
const connect = () => {
  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${window.location.host}/ws`);
  setConnection("connecting", "Connecting");

  socket.addEventListener("open", () => {
    setConnection("live", "Connected");
    pushEvent("WebSocket connected");
  });

  socket.addEventListener("message", (event) => {
    handleMessage(event.data);
  });

  socket.addEventListener("close", () => {
    setConnection("down", "Disconnected");
    showAlert("Dashboard socket closed — retrying");
    pushEvent("WebSocket disconnected", "warn");
    window.setTimeout(connect, 1500);
  });
};

document.querySelectorAll(".nav-item").forEach((button) => {
  button.addEventListener("click", () => {
    const panel = button.dataset.panel;
    document.querySelectorAll(".nav-item").forEach((item) => {
      item.classList.toggle("is-active", item === button);
    });
    document.querySelectorAll("[data-panel-section]").forEach((section) => {
      section.hidden = section.dataset.panelSection !== panel;
    });
  });
});

document.getElementById("applyIntervalBtn")?.addEventListener("click", () => {
  const ms = Number(intervalInput.value);
  if (!Number.isFinite(ms) || ms < 50 || ms > 10000) {
    pushEvent("Interval must be between 50 and 10000 ms", "warn");
    return;
  }
  sendCommand({ action: "setInterval", ms: Math.round(ms) });
});

document.getElementById("resetSequenceBtn")?.addEventListener("click", () => {
  sendCommand({ action: "resetSequence" });
});

document.getElementById("pauseStreamBtn")?.addEventListener("click", () => {
  sendCommand({ action: "pauseStream" });
  pushEvent("Fault simulation requested — pausing publisher stream", "warn");
});

document.getElementById("resumeStreamBtn")?.addEventListener("click", () => {
  sendCommand({ action: "resumeStream" });
  pushEvent("Resume stream requested");
});

connect();
