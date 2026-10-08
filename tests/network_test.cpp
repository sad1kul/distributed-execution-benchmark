#include "remote.hpp"

#include "protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

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

}  // namespace

int main() {
    persistent_and_fresh_sessions();
    shutdown_and_disconnect_do_not_stop_listener();
    task_before_hello_is_rejected();
    bounded_timeout_and_refusal();
    wrong_task_id_is_rejected();
    if (failures == 0) {
        std::cout << "network tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
