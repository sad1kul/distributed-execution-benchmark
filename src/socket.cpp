#include "socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace bom::net {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

struct WinsockRuntime {
    WinsockRuntime() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw NetworkError("WSAStartup failed");
        }
    }
    ~WinsockRuntime() { WSACleanup(); }
};

void ensure_runtime() {
    static WinsockRuntime runtime;
    static_cast<void>(runtime);
}

int last_error() { return WSAGetLastError(); }
bool interrupted(int error) { return error == WSAEINTR; }
bool would_block(int error) { return error == WSAEWOULDBLOCK; }
bool connect_pending(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL;
}
void close_native(NativeSocket socket) { closesocket(socket); }
int socket_error(NativeSocket socket) {
    int value = 0;
    int size = sizeof(value);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &size) != 0) {
        return last_error();
    }
    return value;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
void ensure_runtime() {}
int last_error() { return errno; }
bool interrupted(int error) { return error == EINTR; }
bool would_block(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
bool connect_pending(int error) { return error == EINPROGRESS; }
void close_native(NativeSocket socket) { ::close(socket); }
int socket_error(NativeSocket socket) {
    int value = 0;
    socklen_t size = sizeof(value);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &value, &size) != 0) {
        return last_error();
    }
    return value;
}
#endif

NativeSocket native(std::intptr_t handle) {
    return static_cast<NativeSocket>(handle);
}

std::string error_message(const std::string& operation, int error) {
    return operation + " failed with socket error " + std::to_string(error);
}

void set_nonblocking(NativeSocket socket) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        throw NetworkError(error_message("ioctlsocket", last_error()));
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw NetworkError(error_message("fcntl", last_error()));
    }
#endif
}

void suppress_sigpipe(NativeSocket socket) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        throw NetworkError(error_message("setsockopt SO_NOSIGPIPE", last_error()));
    }
#else
    static_cast<void>(socket);
#endif
}

void wait_ready(NativeSocket socket, bool writing, Deadline deadline) {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw TimeoutError(writing ? "send/connect deadline exceeded" : "receive deadline exceeded");
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        timeval timeout{};
        timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(remaining.count() / 1000000);
        timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(remaining.count() % 1000000);
        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        if (writing) {
            FD_SET(socket, &write_set);
        } else {
            FD_SET(socket, &read_set);
        }
#ifdef _WIN32
        const int result = select(0, writing ? nullptr : &read_set,
                                  writing ? &write_set : nullptr, nullptr, &timeout);
#else
        const int result = select(socket + 1, writing ? nullptr : &read_set,
                                  writing ? &write_set : nullptr, nullptr, &timeout);
#endif
        if (result > 0) {
            return;
        }
        if (result == 0) {
            throw TimeoutError(writing ? "send/connect deadline exceeded" : "receive deadline exceeded");
        }
        const int error = last_error();
        if (!interrupted(error)) {
            throw NetworkError(error_message("select", error));
        }
    }
}

struct Address {
    sockaddr_storage storage{};
    socklen_t length{};
    int family{};
    int socket_type{};
    int protocol{};
};

std::vector<Address> resolve_now(
    const std::string& host,
    const std::string& service,
    bool passive) {
    ensure_runtime();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    addrinfo* raw = nullptr;
    const int result = getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &raw);
    if (result != 0) {
#ifdef _WIN32
        throw NetworkError("address resolution failed with code " + std::to_string(result));
#else
        throw NetworkError(std::string("address resolution failed: ") + gai_strerror(result));
#endif
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(raw, freeaddrinfo);
    std::vector<Address> addresses;
    for (auto* item = raw; item != nullptr; item = item->ai_next) {
        if (item->ai_addrlen > sizeof(sockaddr_storage)) {
            continue;
        }
        Address address;
        std::memcpy(&address.storage, item->ai_addr, item->ai_addrlen);
        address.length = static_cast<socklen_t>(item->ai_addrlen);
        address.family = item->ai_family;
        address.socket_type = item->ai_socktype;
        address.protocol = item->ai_protocol;
        addresses.push_back(address);
    }
    if (addresses.empty()) {
        throw NetworkError("address resolution returned no usable address");
    }
    return addresses;
}

std::vector<Address> resolve_with_deadline(
    const std::string& host,
    const std::string& service,
    Deadline deadline) {
    std::packaged_task<std::vector<Address>()> task(
        [host, service] { return resolve_now(host, service, false); });
    auto future = task.get_future();
    std::thread(std::move(task)).detach();
    if (future.wait_until(deadline) != std::future_status::ready) {
        throw TimeoutError("address resolution deadline exceeded");
    }
    return future.get();
}

Socket make_socket(const Address& address) {
    ensure_runtime();
    const NativeSocket handle = ::socket(address.family, address.socket_type, address.protocol);
    if (handle == kInvalidSocket) {
        throw NetworkError(error_message("socket", last_error()));
    }
    try {
        set_nonblocking(handle);
        suppress_sigpipe(handle);
    } catch (...) {
        close_native(handle);
        throw;
    }
    return Socket(static_cast<std::intptr_t>(handle));
}

}  // namespace

Socket::Socket(std::intptr_t handle) noexcept : handle_(handle) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : handle_(std::exchange(other.handle_, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

bool Socket::valid() const noexcept {
    return handle_ != -1;
}

void Socket::close() noexcept {
    if (valid()) {
        close_native(native(handle_));
        handle_ = -1;
    }
}

std::intptr_t Socket::native_handle() const noexcept {
    return handle_;
}

void Socket::send_all(std::span<const std::uint8_t> bytes, Deadline deadline) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        wait_ready(native(handle_), true, deadline);
        const std::size_t remaining = bytes.size() - sent;
        const int request = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
        const int result = ::send(native(handle_),
                                  reinterpret_cast<const char*>(bytes.data() + sent), request, 0);
#else
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const int result = static_cast<int>(
            ::send(native(handle_), bytes.data() + sent, static_cast<std::size_t>(request), flags));
#endif
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0) {
            throw PeerDisconnected("peer disconnected during send");
        }
        const int error = last_error();
        if (interrupted(error) || would_block(error)) {
            continue;
        }
        throw NetworkError(error_message("send", error));
    }
}

std::vector<std::uint8_t> Socket::receive_exact(std::size_t size, Deadline deadline) {
    std::vector<std::uint8_t> bytes(size);
    std::size_t received = 0;
    while (received < size) {
        wait_ready(native(handle_), false, deadline);
        const std::size_t remaining = size - received;
        const int request = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
        const int result = ::recv(native(handle_), reinterpret_cast<char*>(bytes.data() + received),
                                  request, 0);
#else
        const int result = static_cast<int>(
            ::recv(native(handle_), bytes.data() + received, static_cast<std::size_t>(request), 0));
#endif
        if (result > 0) {
            received += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0) {
            throw PeerDisconnected("peer disconnected during receive");
        }
        const int error = last_error();
        if (interrupted(error) || would_block(error)) {
            continue;
        }
        throw NetworkError(error_message("recv", error));
    }
    return bytes;
}

Deadline deadline_after(std::chrono::milliseconds duration) {
    if (duration.count() <= 0) {
        throw std::invalid_argument("network timeout must be positive");
    }
    return std::chrono::steady_clock::now() + duration;
}

Socket connect_tcp(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
    if (host.empty() || port == 0) {
        throw std::invalid_argument("remote host and port must be specified");
    }
    const Deadline deadline = deadline_after(timeout);
    const auto addresses = resolve_with_deadline(host, std::to_string(port), deadline);
    std::string last_failure = "connection failed";
    for (const auto& address : addresses) {
        Socket socket = make_socket(address);
        const int result = ::connect(native(socket.native_handle()),
                                     reinterpret_cast<const sockaddr*>(&address.storage), address.length);
        if (result == 0) {
            return socket;
        }
        const int error = last_error();
        if (!connect_pending(error)) {
            last_failure = error_message("connect", error);
            continue;
        }
        try {
            wait_ready(native(socket.native_handle()), true, deadline);
        } catch (const TimeoutError&) {
            throw;
        }
        const int completion_error = socket_error(native(socket.native_handle()));
        if (completion_error == 0) {
            return socket;
        }
        last_failure = error_message("connect", completion_error);
    }
    throw NetworkError(last_failure);
}

Listener listen_tcp(const std::string& bind_address, std::uint16_t port) {
    const auto addresses = resolve_now(bind_address, std::to_string(port), true);
    std::string last_failure = "listen failed";
    for (const auto& address : addresses) {
        Socket socket = make_socket(address);
        const int enabled = 1;
#ifdef _WIN32
        setsockopt(native(socket.native_handle()), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#else
        setsockopt(native(socket.native_handle()), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
        if (::bind(native(socket.native_handle()),
                   reinterpret_cast<const sockaddr*>(&address.storage), address.length) != 0) {
            last_failure = error_message("bind", last_error());
            continue;
        }
        if (::listen(native(socket.native_handle()), 16) != 0) {
            last_failure = error_message("listen", last_error());
            continue;
        }
        sockaddr_storage bound{};
        socklen_t length = sizeof(bound);
        if (getsockname(native(socket.native_handle()), reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
            throw NetworkError(error_message("getsockname", last_error()));
        }
        std::uint16_t bound_port = 0;
        if (bound.ss_family == AF_INET) {
            bound_port = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
        } else if (bound.ss_family == AF_INET6) {
            bound_port = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);
        }
        return Listener{std::move(socket), bound_port};
    }
    throw NetworkError(last_failure);
}

Socket accept_tcp(Socket& listener, Deadline deadline) {
    for (;;) {
        wait_ready(native(listener.native_handle()), false, deadline);
        const NativeSocket accepted = ::accept(native(listener.native_handle()), nullptr, nullptr);
        if (accepted != kInvalidSocket) {
            try {
                set_nonblocking(accepted);
                suppress_sigpipe(accepted);
            } catch (...) {
                close_native(accepted);
                throw;
            }
            return Socket(static_cast<std::intptr_t>(accepted));
        }
        const int error = last_error();
        if (interrupted(error) || would_block(error)) {
            continue;
        }
        throw NetworkError(error_message("accept", error));
    }
}

void send_message(Socket& socket, const protocol::Message& message, Deadline deadline) {
    const auto bytes = protocol::encode_message(message);
    socket.send_all(bytes, deadline);
}

protocol::Message receive_message(Socket& socket, Deadline deadline) {
    const auto header_bytes = socket.receive_exact(protocol::kHeaderSize, deadline);
    const protocol::Header header = protocol::decode_header(header_bytes);
    std::vector<std::uint8_t> payload;
    if (header.payload_length != 0) {
        payload = socket.receive_exact(header.payload_length, deadline);
    }
    return protocol::Message{header.type, std::move(payload)};
}

}  // namespace bom::net
