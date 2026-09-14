# About this project

Plain-English guide to the **Digital Twin Dashboard**.

For run commands, see [README.md](README.md).

---

## In one sentence

This project takes live hardware telemetry from the local vECU sandbox (CPU, memory, temperature) and shows it in a browser — with controls that send SOME/IP method requests back to the publisher.

---

## Why it exists

The sandbox proves publisher and subscriber talk correctly. That proof usually looks like terminal text.

This dashboard makes the same data **visible**:

- gauges move with real host/container strain
- sequence numbers advance
- buttons change publish rate, reset sequence, or pause the stream
- alerts flash when thresholds are breached or the stream dies

---

## What it does

1. Starts the sandbox hardware telemetry publisher inside the container.
2. Starts a C++ bridge via `vecu_sdk` (`VecuSubscriber`).
3. Converts each 24-byte telemetry frame to JSON and pushes it over WebSocket.
4. Accepts browser commands and turns them into SOME/IP method requests.
5. Serves a modern header/sidebar dashboard on port 8080.

---

## How it relates to `SDV_Sandbox`

| Project | Job |
|---------|-----|
| `SDV_Sandbox` | vECU environment, SOME/IP stack, `vecu_sdk`, hardware collector, publisher methods |
| `DigitalTwinDashboard` | Consumes the SDK, visualises metrics, and sends control commands |

---

## What we have today

- Real `/proc` / thermal-zone metrics (not a simulated ramp)
- Bi-directional control: interval, sequence reset, stream pause/resume
- Threshold alerting (CPU / memory / temperature / stream timeout / sequence gaps)
- Modern shell UI with header, sidebar, gauges, and event feed
