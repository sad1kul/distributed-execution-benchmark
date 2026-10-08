#include "remote.hpp"

#include "protocol.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace bom {
namespace {

protocol::Result to_protocol(const TaskResult& result) {
    return protocol::Result{
        result.task_id,
        0,
        result.checksum,
        result.prepare_ns,
        result.compute_ns,
        result.checksum_ns,
    };
}

TaskResult from_protocol(const protocol::Result& result) {
    return TaskResult{
        result.task_id,
        result.checksum,
        result.prepare_ns,
        result.compute_ns,
        result.checksum_ns,
    };
}

void send_error_best_effort(
    net::Socket& socket,
    std::uint64_t task_id,
    protocol::ErrorCode code,
    std::chrono::milliseconds timeout) noexcept {
    try {
        net::send_message(socket, protocol::make_error({task_id, code}), net::deadline_after(timeout));
    } catch (...) {
    }
}

}  // namespace

RemoteSession::RemoteSession(
    const std::string& host,
    std::uint16_t port,
    const NetworkConfig& config)
    : socket_(net::connect_tcp(host, port, config.connect_timeout)), config_(config) {
    const auto deadline = net::deadline_after(config_.message_timeout);
    net::send_message(socket_, protocol::make_hello(), deadline);
    const auto response = net::receive_message(socket_, deadline);
    if (response.type != protocol::MessageType::hello || !response.payload.empty()) {
        throw protocol::ProtocolError("worker rejected HELLO handshake");
    }
}

RemoteSession::~RemoteSession() {
    if (open_) {
        try {
            shutdown();
        } catch (...) {
        }
    }
}

TaskResult RemoteSession::execute(const TaskSpec& task) {
    if (!open_) {
        throw std::logic_error("remote session is closed");
    }
    const auto deadline = net::deadline_after(config_.message_timeout);
    net::send_message(
        socket_, protocol::make_task({task.task_id, task.dimension, task.seed}), deadline);
    const auto response = net::receive_message(socket_, deadline);
    if (response.type == protocol::MessageType::error) {
        const auto error = protocol::parse_error(response);
        if (error.task_id != task.task_id && error.task_id != protocol::kUnknownTaskId) {
            throw protocol::ProtocolError("worker ERROR task ID does not match request");
        }
        throw std::runtime_error("worker reported error code " +
                                 std::to_string(static_cast<std::uint16_t>(error.code)));
    }
    const auto result = protocol::parse_result(response);
    if (result.task_id != task.task_id) {
        throw protocol::ProtocolError("worker RESULT task ID does not match request");
    }
    return from_protocol(result);
}

void RemoteSession::shutdown() {
    if (!open_) {
        return;
    }
    open_ = false;
    net::send_message(
        socket_, protocol::make_shutdown(), net::deadline_after(config_.message_timeout));
    socket_.close();
}

TaskResult execute_remote_fresh(
    const std::string& host,
    std::uint16_t port,
    const TaskSpec& task,
    const NetworkConfig& config) {
    RemoteSession session(host, port, config);
    const auto result = session.execute(task);
    session.shutdown();
    return result;
}

WorkerServer::WorkerServer(
    std::string bind_address,
    std::uint16_t port,
    NetworkConfig config)
    : listener_(net::listen_tcp(bind_address, port)), config_(config) {}

WorkerServer::~WorkerServer() {
    request_stop();
}

std::uint16_t WorkerServer::port() const noexcept {
    return listener_.port;
}

void WorkerServer::run() {
    while (!stopping_.load()) {
        try {
            auto socket = net::accept_tcp(
                listener_.socket, net::deadline_after(std::chrono::milliseconds(100)));
            serve_session(std::move(socket));
        } catch (const net::TimeoutError&) {
        } catch (const net::NetworkError&) {
            if (!stopping_.load()) {
                throw;
            }
        }
    }
}

void WorkerServer::request_stop() noexcept {
    stopping_.store(true);
    listener_.socket.close();
}

void WorkerServer::serve_session(net::Socket socket) {
    bool handshake_complete = false;
    for (;;) {
        protocol::Message message;
        try {
            message = net::receive_message(
                socket, net::deadline_after(config_.message_timeout));
        } catch (const net::PeerDisconnected&) {
            return;
        } catch (const net::TimeoutError&) {
            return;
        } catch (const protocol::ProtocolError&) {
            send_error_best_effort(socket, protocol::kUnknownTaskId,
                                   protocol::ErrorCode::malformed_message,
                                   config_.message_timeout);
            return;
        }

        if (!handshake_complete) {
            if (message.type != protocol::MessageType::hello || !message.payload.empty()) {
                send_error_best_effort(socket, protocol::kUnknownTaskId,
                                       protocol::ErrorCode::handshake_required,
                                       config_.message_timeout);
                return;
            }
            net::send_message(
                socket, protocol::make_hello(), net::deadline_after(config_.message_timeout));
            handshake_complete = true;
            continue;
        }

        if (message.type == protocol::MessageType::shutdown) {
            if (!message.payload.empty()) {
                send_error_best_effort(socket, protocol::kUnknownTaskId,
                                       protocol::ErrorCode::malformed_message,
                                       config_.message_timeout);
            }
            return;
        }
        if (message.type != protocol::MessageType::task) {
            send_error_best_effort(socket, protocol::kUnknownTaskId,
                                   protocol::ErrorCode::unexpected_message,
                                   config_.message_timeout);
            return;
        }

        std::uint64_t task_id = protocol::kUnknownTaskId;
        try {
            const auto task = protocol::parse_task(message);
            task_id = task.task_id;
            const auto result = execute_task({task.task_id, task.dimension, task.seed});
            net::send_message(socket, protocol::make_result(to_protocol(result)),
                              net::deadline_after(config_.message_timeout));
        } catch (const protocol::ProtocolError&) {
            send_error_best_effort(socket, task_id, protocol::ErrorCode::invalid_task,
                                   config_.message_timeout);
            return;
        } catch (const std::exception&) {
            send_error_best_effort(socket, task_id, protocol::ErrorCode::task_failed,
                                   config_.message_timeout);
            return;
        }
    }
}

}  // namespace bom
