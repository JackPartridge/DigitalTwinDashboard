# SDV Digital Twin Dashboard

Live browser view of SOME/IP telemetry from the local vECU sandbox — with bi-directional controls and threshold alerts.

**Repo:** https://github.com/JackPartridge/DigitalTwinDashboard (private)

**Depends on:** [SDV_Sandbox](https://github.com/JackPartridge/SDV_Sandbox) (`sdv-sandbox:latest` image, `vecu_sdk`)

## What it is

A separate project (sibling to `SDV_Sandbox`) that:

1. Subscribes to hardware telemetry through **`vecu_sdk`** (`VecuSubscriber`)
2. Bridges each event to a local **WebSocket**
3. Sends control commands back as SOME/IP **method requests** (interval, sequence reset, stream pause/resume)
4. Serves a modern header/sidebar dashboard your Windows browser can open

## What you see

- CPU %, memory %, load average, swap %, network I/O, process count
- Core temperature when the container exposes thermal/hwmon sensors (often unavailable on Docker Desktop)
- Sequence / gap warnings and stream-timeout alerts
- Controls to change publish rate, reset sequence, or simulate a lost stream

## Prerequisites

1. Build the sandbox image first:

```bash
cd ../SDV_Sandbox
docker compose build sandbox
```

2. Docker Desktop with WSL2

## Quick start

From this directory:

```bash
docker compose up --build
```

Then open [http://localhost:8080](http://localhost:8080) on the Windows host.

Verbose middleware logs:

```bash
docker compose run --rm digital-twin --verbose
```

## How the pieces fit

```text
Windows browser  --HTTP/WS-->  Digital Twin container
                                   │
                                   ├─ digitalTwinDashboard (VecuSubscriber + WebSocket)
                                   └─ sensorPublisher (from SDV_Sandbox image)
                                          SOME/IP on localhost inside the container
```

Both publisher and dashboard run **in the same Linux container** so SOME/IP on `127.0.0.1` works reliably under Docker Desktop.

## Project layout

```text
src/                 C++ HTTP/WebSocket bridge + main
web/                 Modern dashboard (header, sidebar, gauges, feed)
scripts/             container launch script (publisher + twin)
Dockerfile           builds on top of sdv-sandbox:latest
docker-compose.yml
```
