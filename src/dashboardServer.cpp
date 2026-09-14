#include "dashboardServer.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace digitalTwin {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

auto mimeTypeFor(const std::filesystem::path& path) -> std::string {
  const auto extension = path.extension().string();
  if (extension == ".html") {
    return "text/html";
  }
  if (extension == ".js") {
    return "application/javascript";
  }
  if (extension == ".css") {
    return "text/css";
  }
  return "application/octet-stream";
}

auto readFileBytes(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path, std::ios::binary};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

class WebsocketSession : public std::enable_shared_from_this<WebsocketSession> {
public:
  WebsocketSession(tcp::socket socket, DashboardServer::ClientMessageHandler onClientMessage)
      : websocket_{std::move(socket)}, onClientMessage_{std::move(onClientMessage)} {}

  auto run(http::request<http::string_body> request) -> void {
    websocket_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    websocket_.set_option(websocket::stream_base::decorator([](websocket::response_type& response) {
      response.set(http::field::server, "digital-twin-dashboard");
    }));
    websocket_.async_accept(
        request,
        beast::bind_front_handler(&WebsocketSession::onAccept, shared_from_this()));
  }

  auto send(const std::shared_ptr<std::string const>& message) -> void {
    net::post(websocket_.get_executor(), [self = shared_from_this(), message] {
      self->outbound_.push_back(message);
      if (self->outbound_.size() > 1) {
        return;
      }
      self->writeNext();
    });
  }

  auto close() -> void {
    beast::error_code error;
    websocket_.close(websocket::close_code::normal, error);
  }

private:
  auto onAccept(beast::error_code error) -> void {
    if (error) {
      return;
    }
    doRead();
  }

  auto doRead() -> void {
    websocket_.async_read(
        buffer_,
        beast::bind_front_handler(&WebsocketSession::onRead, shared_from_this()));
  }

  auto onRead(beast::error_code error, std::size_t) -> void {
    if (error) {
      return;
    }
    if (onClientMessage_) {
      onClientMessage_(beast::buffers_to_string(buffer_.data()));
    }
    buffer_.consume(buffer_.size());
    doRead();
  }

  auto writeNext() -> void {
    websocket_.text(true);
    websocket_.async_write(
        net::buffer(*outbound_.front()),
        beast::bind_front_handler(&WebsocketSession::onWrite, shared_from_this()));
  }

  auto onWrite(beast::error_code error, std::size_t) -> void {
    if (error) {
      outbound_.clear();
      return;
    }
    outbound_.erase(outbound_.begin());
    if (!outbound_.empty()) {
      writeNext();
    }
  }

  websocket::stream<tcp::socket> websocket_;
  beast::flat_buffer buffer_;
  std::vector<std::shared_ptr<std::string const>> outbound_;
  DashboardServer::ClientMessageHandler onClientMessage_;
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
  HttpSession(
      tcp::socket socket,
      std::filesystem::path webRoot,
      std::function<void(std::shared_ptr<WebsocketSession>)> onWebsocket,
      DashboardServer::ClientMessageHandler onClientMessage)
      : stream_{std::move(socket)},
        webRoot_{std::move(webRoot)},
        onWebsocket_{std::move(onWebsocket)},
        onClientMessage_{std::move(onClientMessage)} {}

  auto run() -> void { doRead(); }

private:
  auto doRead() -> void {
    request_ = {};
    http::async_read(
        stream_,
        buffer_,
        request_,
        beast::bind_front_handler(&HttpSession::onRead, shared_from_this()));
  }

  auto onRead(beast::error_code error, std::size_t) -> void {
    if (error) {
      return;
    }
    if (websocket::is_upgrade(request_)) {
      auto session = std::make_shared<WebsocketSession>(stream_.release_socket(), onClientMessage_);
      onWebsocket_(session);
      session->run(std::move(request_));
      return;
    }
    handleRequest();
  }

  auto handleRequest() -> void {
    http::response<http::string_body> response{http::status::ok, request_.version()};
    response.set(http::field::server, "digital-twin-dashboard");
    response.keep_alive(false);

    std::string target{request_.target()};
    if (target == "/" || target.empty()) {
      target = "/index.html";
    }
    if (const auto query = target.find('?'); query != std::string::npos) {
      target = target.substr(0, query);
    }

    const auto filePath = webRoot_ / std::filesystem::path{target}.relative_path();
    if (!std::filesystem::exists(filePath) || !std::filesystem::is_regular_file(filePath)) {
      response.result(http::status::not_found);
      response.set(http::field::content_type, "text/plain");
      response.body() = "Not found";
    } else {
      response.set(http::field::content_type, mimeTypeFor(filePath));
      response.body() = readFileBytes(filePath);
    }
    response.prepare_payload();

    auto responsePtr = std::make_shared<http::response<http::string_body>>(std::move(response));
    http::async_write(
        stream_,
        *responsePtr,
        [self = shared_from_this(), responsePtr](beast::error_code, std::size_t) {
          beast::error_code ignored;
          self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
        });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> request_;
  std::filesystem::path webRoot_;
  std::function<void(std::shared_ptr<WebsocketSession>)> onWebsocket_;
  DashboardServer::ClientMessageHandler onClientMessage_;
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
  Listener(
      net::io_context& io,
      tcp::endpoint endpoint,
      std::filesystem::path webRoot,
      std::function<void(std::shared_ptr<WebsocketSession>)> onWebsocket,
      DashboardServer::ClientMessageHandler onClientMessage)
      : io_{io},
        acceptor_{net::make_strand(io)},
        webRoot_{std::move(webRoot)},
        onWebsocket_{std::move(onWebsocket)},
        onClientMessage_{std::move(onClientMessage)} {
    beast::error_code error;
    acceptor_.open(endpoint.protocol(), error);
    acceptor_.set_option(net::socket_base::reuse_address(true), error);
    acceptor_.bind(endpoint, error);
    acceptor_.listen(net::socket_base::max_listen_connections, error);
  }

  auto run() -> void { doAccept(); }

private:
  auto doAccept() -> void {
    acceptor_.async_accept(
        net::make_strand(io_),
        beast::bind_front_handler(&Listener::onAccept, shared_from_this()));
  }

  auto onAccept(beast::error_code error, tcp::socket socket) -> void {
    if (!error) {
      std::make_shared<HttpSession>(
          std::move(socket), webRoot_, onWebsocket_, onClientMessage_)
          ->run();
    }
    doAccept();
  }

  net::io_context& io_;
  tcp::acceptor acceptor_;
  std::filesystem::path webRoot_;
  std::function<void(std::shared_ptr<WebsocketSession>)> onWebsocket_;
  DashboardServer::ClientMessageHandler onClientMessage_;
};

}  // namespace

struct DashboardServer::Impl {
  std::string webRoot;
  unsigned short port;
  net::io_context io;
  std::jthread ioThread;
  std::mutex sessionsMutex;
  std::unordered_set<std::shared_ptr<WebsocketSession>> sessions;
  std::shared_ptr<Listener> listener;
  std::atomic<bool> running{false};
  ClientMessageHandler clientMessageHandler;

  auto addSession(const std::shared_ptr<WebsocketSession>& session) -> void {
    std::scoped_lock lock{sessionsMutex};
    sessions.insert(session);
  }
};

DashboardServer::DashboardServer(std::string webRoot, unsigned short port)
    : impl_{std::make_unique<Impl>()} {
  impl_->webRoot = std::move(webRoot);
  impl_->port = port;
}

DashboardServer::~DashboardServer() {
  stop();
}

auto DashboardServer::setClientMessageHandler(ClientMessageHandler handler) -> void {
  impl_->clientMessageHandler = std::move(handler);
}

auto DashboardServer::start() -> void {
  if (impl_->running.exchange(true)) {
    return;
  }

  const auto endpoint = tcp::endpoint{tcp::v4(), impl_->port};
  impl_->listener = std::make_shared<Listener>(
      impl_->io,
      endpoint,
      impl_->webRoot,
      [this](const std::shared_ptr<WebsocketSession>& session) { impl_->addSession(session); },
      [this](const std::string& message) {
        if (impl_->clientMessageHandler) {
          impl_->clientMessageHandler(message);
        }
      });
  impl_->listener->run();

  impl_->ioThread = std::jthread([this](std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
      impl_->io.run_for(std::chrono::milliseconds{200});
      if (impl_->io.stopped()) {
        impl_->io.restart();
      }
    }
    impl_->io.stop();
  });

  std::cout << "Digital twin dashboard listening on http://localhost:" << impl_->port << '\n';
}

auto DashboardServer::stop() -> void {
  if (!impl_ || !impl_->running.exchange(false)) {
    return;
  }

  {
    std::scoped_lock lock{impl_->sessionsMutex};
    for (const auto& session : impl_->sessions) {
      session->close();
    }
    impl_->sessions.clear();
  }

  impl_->io.stop();
  if (impl_->ioThread.joinable()) {
    impl_->ioThread.request_stop();
  }
}

auto DashboardServer::broadcast(const std::string& message) -> void {
  auto sharedMessage = std::make_shared<std::string const>(message);
  std::vector<std::shared_ptr<WebsocketSession>> snapshot;
  {
    std::scoped_lock lock{impl_->sessionsMutex};
    snapshot.assign(impl_->sessions.begin(), impl_->sessions.end());
  }
  for (const auto& session : snapshot) {
    session->send(sharedMessage);
  }
}

}  // namespace digitalTwin
