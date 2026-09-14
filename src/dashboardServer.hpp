#pragma once

#include <functional>
#include <memory>
#include <string>

namespace digitalTwin {

class DashboardServer {
public:
  using ClientMessageHandler = std::function<void(const std::string& message)>;

  DashboardServer(std::string webRoot, unsigned short port = 8080);
  ~DashboardServer();

  DashboardServer(const DashboardServer&) = delete;
  auto operator=(const DashboardServer&) -> DashboardServer& = delete;

  auto setClientMessageHandler(ClientMessageHandler handler) -> void;
  auto start() -> void;
  auto stop() -> void;
  auto broadcast(const std::string& message) -> void;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitalTwin
