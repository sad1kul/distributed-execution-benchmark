#pragma once

#include "execution.hpp"
#include "socket.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace bom {

struct NetworkConfig {
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds message_timeout{300000};
};

class RemoteSession {
public:
    RemoteSession(
        const std::string& host,
        std::uint16_t port,
        const NetworkConfig& config = {});
    ~RemoteSession();
    RemoteSession(const RemoteSession&) = delete;
    RemoteSession& operator=(const RemoteSession&) = delete;

    TaskResult execute(const TaskSpec& task);
    void shutdown();

private:
    net::Socket socket_;
    NetworkConfig config_;
    bool open_{true};
};

TaskResult execute_remote_fresh(
    const std::string& host,
    std::uint16_t port,
    const TaskSpec& task,
    const NetworkConfig& config = {});

class WorkerServer {
public:
    WorkerServer(
        std::string bind_address = "127.0.0.1",
        std::uint16_t port = 0,
        NetworkConfig config = {});
    ~WorkerServer();
    WorkerServer(const WorkerServer&) = delete;
    WorkerServer& operator=(const WorkerServer&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;
    void run();
    void request_stop() noexcept;

private:
    void serve_session(net::Socket socket);
    void serve_session_messages(net::Socket& socket);

    net::Listener listener_;
    NetworkConfig config_;
    std::atomic<bool> stopping_{false};
    std::mutex active_socket_mutex_;
    net::Socket* active_socket_{nullptr};
};

}  // namespace bom
