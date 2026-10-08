#include "remote.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

template <typename Integer>
Integer parse_integer(const std::string& text, const std::string& name) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " + name);
    }
    return value;
}

std::string require_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    return argv[++index];
}

void usage() {
    std::cout << "Usage: bom_worker [--bind ADDRESS] [--port PORT] "
                 "[--message-timeout-ms MS]\n";
}

class ShutdownWatcher {
public:
    explicit ShutdownWatcher(bom::WorkerServer& server)
        : server_(server), thread_([this] { watch(); }) {}

    ~ShutdownWatcher() {
        finish_.store(true);
        thread_.join();
    }

    ShutdownWatcher(const ShutdownWatcher&) = delete;
    ShutdownWatcher& operator=(const ShutdownWatcher&) = delete;

private:
    void watch() noexcept {
        while (!finish_.load() && stop_requested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (stop_requested != 0) {
            server_.request_stop();
        }
    }

    bom::WorkerServer& server_;
    std::atomic<bool> finish_{false};
    std::thread thread_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string bind_address = "127.0.0.1";
        std::uint16_t port = 9000;
        bom::NetworkConfig config;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--help") {
                usage();
                return 0;
            }
            const std::string value = require_value(index, argc, argv);
            if (option == "--bind") {
                bind_address = value;
            } else if (option == "--port") {
                port = parse_integer<std::uint16_t>(value, option);
            } else if (option == "--message-timeout-ms") {
                config.message_timeout = std::chrono::milliseconds(
                    parse_integer<std::int64_t>(value, option));
            } else {
                throw std::invalid_argument("unknown option: " + option);
            }
        }
        if (config.message_timeout.count() <= 0) {
            throw std::invalid_argument("message timeout must be positive");
        }
        bom::WorkerServer server(bind_address, port, config);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        ShutdownWatcher watcher(server);
        std::cout << "READY " << bind_address << ':' << server.port() << '\n' << std::flush;
        server.run();
        stop_requested = 1;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
