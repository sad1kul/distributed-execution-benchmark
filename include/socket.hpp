#pragma once

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bom::net {

using Deadline = std::chrono::steady_clock::time_point;

class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TimeoutError : public NetworkError {
public:
    using NetworkError::NetworkError;
};

class PeerDisconnected : public NetworkError {
public:
    using NetworkError::NetworkError;
};

class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(std::intptr_t handle) noexcept;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    void shutdown_both() noexcept;
    void close() noexcept;
    [[nodiscard]] std::intptr_t native_handle() const noexcept;

    void send_all(std::span<const std::uint8_t> bytes, Deadline deadline);
    std::vector<std::uint8_t> receive_exact(std::size_t size, Deadline deadline);

private:
    std::intptr_t handle_{-1};
};

struct Listener {
    Socket socket;
    std::uint16_t port;
};

Deadline deadline_after(std::chrono::milliseconds duration);
Socket connect_tcp(
    const std::string& host,
    std::uint16_t port,
    std::chrono::milliseconds timeout);
Listener listen_tcp(const std::string& bind_address, std::uint16_t port);
Socket accept_tcp(Socket& listener, Deadline deadline);

void send_message(Socket& socket, const protocol::Message& message, Deadline deadline);
protocol::Message receive_message(Socket& socket, Deadline deadline);

}  // namespace bom::net
