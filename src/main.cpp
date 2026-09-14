#include "dashboardServer.hpp"

#include "telemetryFrame.hpp"
#include "vecu_sdk.hpp"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TwinState {
  std::mutex mutex;
  std::deque<double> cpuHistory;
  std::deque<double> memoryHistory;
  std::uint32_t lastSequence{0};
  bool hasSequence{false};
  std::chrono::steady_clock::time_point lastFrameAt{std::chrono::steady_clock::now()};
  std::size_t gapWarnings{0};
  bool streamingExpected{true};
  std::chrono::steady_clock::time_point lastTimeoutAlertAt{};
};

auto milliPercentToPercent(std::int32_t value) -> double {
  return static_cast<double>(value) / 1000.0;
}

auto historyJson(const std::deque<double>& values) -> std::string {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(2);
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << values[index];
  }
  out << ']';
  return out.str();
}

auto frameToJson(const sdv::TelemetryFrame& frame, TwinState& state) -> std::string {
  const auto cpu = milliPercentToPercent(frame.cpuUtilisationMilliPercent);
  const auto memory = milliPercentToPercent(frame.memoryUsedMilliPercent);
  const auto swap = milliPercentToPercent(frame.swapUsedMilliPercent);
  const auto load1 = static_cast<double>(frame.loadAverage1Milli) / 1000.0;
  const bool hasTemperature = frame.temperatureMilliCelsius != sdv::temperatureUnavailable;
  const auto temperatureC = hasTemperature
      ? static_cast<double>(frame.temperatureMilliCelsius) / 1000.0
      : 0.0;

  bool sequenceGap = false;
  std::size_t gapWarnings = 0;
  std::string cpuHistory;
  std::string memoryHistory;
  {
    std::scoped_lock lock{state.mutex};
    if (state.hasSequence && frame.sequenceNumber > state.lastSequence + 1) {
      sequenceGap = true;
      ++state.gapWarnings;
    }
    state.lastSequence = frame.sequenceNumber;
    state.hasSequence = true;
    state.lastFrameAt = std::chrono::steady_clock::now();
    state.cpuHistory.push_back(cpu);
    state.memoryHistory.push_back(memory);
    while (state.cpuHistory.size() > 60) {
      state.cpuHistory.pop_front();
      state.memoryHistory.pop_front();
    }
    gapWarnings = state.gapWarnings;
    cpuHistory = historyJson(state.cpuHistory);
    memoryHistory = historyJson(state.memoryHistory);
  }

  std::string alert;
  if (sequenceGap) {
    alert = "sequence_gap";
  } else if (cpu >= 85.0) {
    alert = "cpu_high";
  } else if (memory >= 90.0) {
    alert = "memory_high";
  } else if (hasTemperature && temperatureC >= 80.0) {
    alert = "temperature_high";
  } else if (load1 >= static_cast<double>(frame.logicalCpuCount) * 0.9
             && frame.logicalCpuCount > 0) {
    alert = "load_high";
  }

  std::ostringstream json;
  json.setf(std::ios::fixed);
  json.precision(3);
  json << '{'
       << "\"type\":\"telemetry\","
       << "\"sequence\":" << frame.sequenceNumber << ','
       << "\"cpuPercent\":" << cpu << ','
       << "\"memoryPercent\":" << memory << ','
       << "\"hasTemperature\":" << (hasTemperature ? "true" : "false") << ','
       << "\"temperatureC\":" << temperatureC << ','
       << "\"timestampMs\":" << frame.timestampMs << ','
       << "\"loadAverage1\":" << load1 << ','
       << "\"swapPercent\":" << swap << ','
       << "\"processCount\":" << frame.processCount << ','
       << "\"networkRxBps\":" << frame.networkRxBytesPerSec << ','
       << "\"networkTxBps\":" << frame.networkTxBytesPerSec << ','
       << "\"logicalCpuCount\":" << frame.logicalCpuCount << ','
       << "\"alert\":\"" << alert << "\","
       << "\"gapWarnings\":" << gapWarnings << ','
       << "\"cpuHistory\":" << cpuHistory << ','
       << "\"memoryHistory\":" << memoryHistory
       << '}';
  return json.str();
}

auto resolveWebRoot() -> std::string {
  if (const char* fromEnv = std::getenv("DIGITAL_TWIN_WEB_ROOT"); fromEnv != nullptr) {
    return fromEnv;
  }
  return "/twin/web";
}

auto extractIntervalMs(const std::string& raw) -> std::uint32_t {
  const auto key = raw.find("\"ms\"");
  if (key == std::string::npos) {
    return 500;
  }
  const auto colon = raw.find(':', key);
  if (colon == std::string::npos) {
    return 500;
  }
  try {
    return static_cast<std::uint32_t>(std::stoul(raw.substr(colon + 1)));
  } catch (const std::exception&) {
    return 500;
  }
}

auto handleCommand(vecu::VecuSubscriber& subscriber, const std::string& raw, TwinState& state)
    -> std::string {
  if (raw.find("setInterval") != std::string::npos) {
    const auto intervalMs = extractIntervalMs(raw);
    subscriber.requestSetIntervalMs(intervalMs);
    return "{\"type\":\"ack\",\"action\":\"setInterval\",\"ms\":" + std::to_string(intervalMs) + '}';
  }
  if (raw.find("resetSequence") != std::string::npos) {
    subscriber.requestResetSequence();
    {
      std::scoped_lock lock{state.mutex};
      state.hasSequence = false;
    }
    return "{\"type\":\"ack\",\"action\":\"resetSequence\"}";
  }
  if (raw.find("pauseStream") != std::string::npos) {
    subscriber.requestSetStreaming(false);
    {
      std::scoped_lock lock{state.mutex};
      state.streamingExpected = false;
    }
    return "{\"type\":\"ack\",\"action\":\"pauseStream\"}";
  }
  if (raw.find("resumeStream") != std::string::npos) {
    subscriber.requestSetStreaming(true);
    {
      std::scoped_lock lock{state.mutex};
      state.streamingExpected = true;
      state.lastFrameAt = std::chrono::steady_clock::now();
    }
    return "{\"type\":\"ack\",\"action\":\"resumeStream\"}";
  }
  return "{\"type\":\"error\",\"message\":\"unknown_command\"}";
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const bool verbose = vecu::wantsVerboseLoggingFromArgs(argc, argv);
  TwinState twinState;
  digitalTwin::DashboardServer dashboard{resolveWebRoot(), 8080};

  vecu::VecuSubscriber subscriber;
  dashboard.setClientMessageHandler(
      [&subscriber, &dashboard, &twinState](const std::string& message) {
        const auto ack = handleCommand(subscriber, message, twinState);
        dashboard.broadcast(ack);
      });
  dashboard.start();

  const auto started = subscriber.start(
      [&dashboard, &twinState](const std::vector<std::uint8_t>& payload) {
        const auto frame = sdv::processTelemetryPayload(payload);
        if (!frame.has_value()) {
          return;
        }
        dashboard.broadcast(frameToJson(*frame, twinState));
      },
      verbose);

  if (!started) {
    std::cerr << "Failed to start VecuSubscriber SDK\n";
    dashboard.stop();
    return EXIT_FAILURE;
  }

  std::cout << "Digital twin ready at http://localhost:8080\n";

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    bool shouldAlert = false;
    {
      std::scoped_lock lock{twinState.mutex};
      const auto elapsed = std::chrono::steady_clock::now() - twinState.lastFrameAt;
      if (twinState.streamingExpected && elapsed > std::chrono::seconds{2}) {
        const auto sinceLastAlert =
            std::chrono::steady_clock::now() - twinState.lastTimeoutAlertAt;
        if (sinceLastAlert > std::chrono::seconds{2}) {
          twinState.lastTimeoutAlertAt = std::chrono::steady_clock::now();
          shouldAlert = true;
        }
      }
    }
    if (shouldAlert) {
      dashboard.broadcast(
          "{\"type\":\"alert\",\"alert\":\"stream_timeout\",\"message\":\"No telemetry for >2s\"}");
    }
  }
}
