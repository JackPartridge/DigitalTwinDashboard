#!/usr/bin/env bash
set -euo pipefail

export VSOMEIP_CONFIGURATION="${VSOMEIP_CONFIGURATION:-/workspace/config/vsomeipLocal.json}"
export DIGITAL_TWIN_WEB_ROOT="${DIGITAL_TWIN_WEB_ROOT:-/twin/web}"

cleanup() {
  if [[ -n "${PUBLISHER_PID:-}" ]] && kill -0 "${PUBLISHER_PID}" 2>/dev/null; then
    kill "${PUBLISHER_PID}" 2>/dev/null || true
    wait "${PUBLISHER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "Starting vECU telemetry publisher..."
VSOMEIP_APPLICATION_NAME=sensorPublisher /workspace/build/sensorPublisher &
PUBLISHER_PID=$!

sleep 1

echo "Starting digital twin dashboard bridge..."
VSOMEIP_APPLICATION_NAME=sensorSubscriber /twin/build/digitalTwinDashboard "$@"
