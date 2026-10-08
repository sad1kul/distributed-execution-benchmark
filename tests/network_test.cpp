#include "remote.hpp"

#include "protocol.hpp"
#include "resolver_gate.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace {
int failures = 0;

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition \
                      << '\n';                                                         \
            ++failures;                                                                \
        }                                                                              \
    } while (false)

template <typename Function>
void expect_failure(Function function, int line) {
    try {
        function();
        std::cerr << __FILE__ << ':' << line << ": expected failure\n";
        ++failures;
    } catch (const std::exception&) {
    }
}

#define CHECK_FAILURE(expression) expect_failure([&] { expression; }, __LINE__)

class RunningWorker {
public:
    explicit RunningWorker(bom::NetworkConfig config = {})
        : server_("127.0.0.1", 0, config), thread_([this] { run(); }) {}

    ~RunningWorker() {
        server_.request_stop();
        thread_.join();
    }

    std::uint16_t port() const { return server_.port(); }
    void check() {
        if (failed_.load()) {
            CHECK(false);
        }
    }

private:
    void run() {
        try {
            server_.run();
        } catch (const std::exception& error) {
            std::cerr << "worker thread failed: " << error.what() << '\n';
            failed_.store(true);
        }
    }

    bom::WorkerServer server_;
    std::atomic<bool> failed_{false};
    std::thread thread_;
};

void persistent_and_fresh_sessions() {
    RunningWorker worker;
    const auto tasks = bom::make_tasks(4, 3, 7);
    const auto expected = bom::compute_expected_checksums(tasks);
    {
        bom::RemoteSession session("127.0.0.1", worker.port());
        for (const auto& task : tasks) {
            CHECK(session.execute(task).checksum == expected.at(task.task_id));
        }
        session.shutdown();
    }
    for (const auto& task : tasks) {
        CHECK(bom::execute_remote_fresh("127.0.0.1", worker.port(), task).checksum ==
              expected.at(task.task_id));
    }
    worker.check();
}

void shutdown_and_disconnect_do_not_stop_listener() {
    RunningWorker worker;
    {
        bom::RemoteSession first("127.0.0.1", worker.port());
        first.shutdown();
    }
    {
        auto raw = bom::net::connect_tcp("127.0.0.1", worker.port(), std::chrono::seconds(1));
        raw.close();
    }
    bom::RemoteSession second("127.0.0.1", worker.port());
    CHECK(second.execute({1, 2, 3}).task_id == 1);
    second.shutdown();
    worker.check();
}

void connection_reset_does_not_stop_listener() {
    RunningWorker worker;
    {
        auto reset_client = bom::net::connect_tcp(
            "127.0.0.1", worker.port(), std::chrono::seconds(1));
        linger reset_linger{};
        reset_linger.l_onoff = 1;
        reset_linger.l_linger = 0;
#ifdef _WIN32
        const int result = setsockopt(
            static_cast<SOCKET>(reset_client.native_handle()), SOL_SOCKET, SO_LINGER,
            reinterpret_cast<const char*>(&reset_linger), sizeof(reset_linger));
#else
        const int result = setsockopt(
            static_cast<int>(reset_client.native_handle()), SOL_SOCKET, SO_LINGER,
            &reset_linger, sizeof(reset_linger));
#endif
        CHECK(result == 0);
        reset_client.close();
    }

    bom::RemoteSession recovered("127.0.0.1", worker.port());
    CHECK(recovered.execute({81, 2, 3}).task_id == 81);
    recovered.shutdown();
    worker.check();
}

void task_before_hello_is_rejected() {
    RunningWorker worker;
    auto socket = bom::net::connect_tcp("127.0.0.1", worker.port(), std::chrono::seconds(1));
    bom::net::send_message(socket, bom::protocol::make_task({4, 2, 1}),
                           bom::net::deadline_after(std::chrono::seconds(1)));
    const auto response = bom::net::receive_message(
        socket, bom::net::deadline_after(std::chrono::seconds(1)));
    const auto error = bom::protocol::parse_error(response);
    CHECK(error.task_id == bom::protocol::kUnknownTaskId);
    CHECK(error.code == bom::protocol::ErrorCode::handshake_required);
    worker.check();
}

void invalid_post_handshake_order_is_rejected() {
    RunningWorker worker;
    auto socket = bom::net::connect_tcp("127.0.0.1", worker.port(), std::chrono::seconds(1));
    bom::net::send_message(socket, bom::protocol::make_hello(),
                           bom::net::deadline_after(std::chrono::seconds(1)));
    CHECK(bom::net::receive_message(socket, bom::net::deadline_after(std::chrono::seconds(1))).type ==
          bom::protocol::MessageType::hello);
    bom::net::send_message(socket, bom::protocol::make_hello(),
                           bom::net::deadline_after(std::chrono::seconds(1)));
    const auto response = bom::net::receive_message(
        socket, bom::net::deadline_after(std::chrono::seconds(1)));
    CHECK(bom::protocol::parse_error(response).code ==
          bom::protocol::ErrorCode::unexpected_message);
    worker.check();
}

void invalid_task_preserves_decoded_id() {
    RunningWorker worker;
    auto socket = bom::net::connect_tcp("127.0.0.1", worker.port(), std::chrono::seconds(1));
    bom::net::send_message(socket, bom::protocol::make_hello(),
                           bom::net::deadline_after(std::chrono::seconds(1)));
    static_cast<void>(bom::net::receive_message(
        socket, bom::net::deadline_after(std::chrono::seconds(1))));
    const std::vector<std::uint8_t> invalid_task = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    };
    bom::net::send_message(
        socket,
        {bom::protocol::MessageType::task, invalid_task},
        bom::net::deadline_after(std::chrono::seconds(1)));
    const auto error = bom::protocol::parse_error(bom::net::receive_message(
        socket, bom::net::deadline_after(std::chrono::seconds(1))));
    CHECK(error.task_id == 0x0102030405060708ULL);
    CHECK(error.code == bom::protocol::ErrorCode::invalid_task);
    worker.check();
}

void bounded_timeout_and_refusal() {
    const bom::NetworkConfig short_timeout{
        std::chrono::milliseconds(200), std::chrono::milliseconds(100)};
    RunningWorker worker(short_timeout);
    auto socket = bom::net::connect_tcp("127.0.0.1", worker.port(), short_timeout.connect_timeout);
    CHECK_FAILURE(bom::net::receive_message(
        socket, bom::net::deadline_after(short_timeout.message_timeout)));
    socket.close();

    auto unused = bom::net::listen_tcp("127.0.0.1", 0);
    const auto unused_port = unused.port;
    // Closing the listener reserves an observed unused port without contacting an external host.
    unused.socket.close();
    CHECK_FAILURE(bom::net::connect_tcp(
        "127.0.0.1", unused_port, std::chrono::milliseconds(200)));
    worker.check();
}

void resolver_work_is_bounded() {
    bom::net::detail::ResolverGate gate;
    CHECK(gate.try_acquire());
    CHECK(gate.active());
    CHECK(!gate.try_acquire());
    gate.release();
    CHECK(!gate.active());
    CHECK(gate.try_acquire());
    gate.release();
}

void wrong_task_id_is_rejected() {
    auto listener = bom::net::listen_tcp("127.0.0.1", 0);
    std::thread fake_worker([&] {
        auto socket = bom::net::accept_tcp(
            listener.socket, bom::net::deadline_after(std::chrono::seconds(1)));
        const auto hello = bom::net::receive_message(
            socket, bom::net::deadline_after(std::chrono::seconds(1)));
        CHECK(hello.type == bom::protocol::MessageType::hello);
        bom::net::send_message(socket, bom::protocol::make_hello(),
                               bom::net::deadline_after(std::chrono::seconds(1)));
        static_cast<void>(bom::net::receive_message(
            socket, bom::net::deadline_after(std::chrono::seconds(1))));
        bom::net::send_message(socket, bom::protocol::make_result({999, 0, 1, 2, 3, 4}),
                               bom::net::deadline_after(std::chrono::seconds(1)));
    });
    {
        bom::RemoteSession session("127.0.0.1", listener.port);
        CHECK_FAILURE(session.execute({1, 2, 3}));
    }
    fake_worker.join();
}

void fragmented_transport_and_worker_error() {
    auto listener = bom::net::listen_tcp("127.0.0.1", 0);
    std::thread fake_worker([&] {
        auto socket = bom::net::accept_tcp(
            listener.socket, bom::net::deadline_after(std::chrono::seconds(1)));
        static_cast<void>(bom::net::receive_message(
            socket, bom::net::deadline_after(std::chrono::seconds(1))));
        for (const auto byte : bom::protocol::encode_message(bom::protocol::make_hello())) {
            const std::array<std::uint8_t, 1> fragment{byte};
            socket.send_all(fragment, bom::net::deadline_after(std::chrono::seconds(1)));
        }
        const auto task_message = bom::net::receive_message(
            socket, bom::net::deadline_after(std::chrono::seconds(1)));
        const auto task = bom::protocol::parse_task(task_message);
        const auto encoded = bom::protocol::encode_message(
            bom::protocol::make_error({task.task_id, bom::protocol::ErrorCode::task_failed}));
        for (const auto byte : encoded) {
            const std::array<std::uint8_t, 1> fragment{byte};
            socket.send_all(fragment, bom::net::deadline_after(std::chrono::seconds(1)));
        }
    });
    {
        bom::RemoteSession session("127.0.0.1", listener.port);
        CHECK_FAILURE(session.execute({12, 2, 3}));
    }
    fake_worker.join();
}

}  // namespace

int main() {
    persistent_and_fresh_sessions();
    shutdown_and_disconnect_do_not_stop_listener();
    connection_reset_does_not_stop_listener();
    task_before_hello_is_rejected();
    invalid_post_handshake_order_is_rejected();
    invalid_task_preserves_decoded_id();
    bounded_timeout_and_refusal();
    resolver_work_is_bounded();
    wrong_task_id_is_rejected();
    fragmented_transport_and_worker_error();
    if (failures == 0) {
        std::cout << "network tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
