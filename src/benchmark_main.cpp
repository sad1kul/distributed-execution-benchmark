#include "benchmark.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

void usage(std::ostream& output) {
    output << "Usage: bom_benchmark --mode MODE --dimension N --task-count COUNT "
              "--base-seed SEED --repetitions COUNT --output-dir PATH [options]\n"
              "Modes: local-1, remote-1, local-2, local-remote\n"
              "Options:\n"
              "  --worker-host HOST             default: 127.0.0.1\n"
              "  --worker-port PORT             required for remote modes\n"
              "  --connection-mode MODE         persistent or fresh-per-task\n"
              "  --warmup-count COUNT           default: 1; zero is allowed\n"
              "  --connect-timeout-ms MS        default: 5000\n"
              "  --message-timeout-ms MS        default: 300000\n"
              "  --experiment-id ID             optional stable label\n"
              "  --help\n";
}

template <typename Integer>
Integer parse_integer(std::string_view text, const std::string& name) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " + name + ": " + std::string(text));
    }
    return value;
}

std::string require_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    return argv[++index];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        bom::BenchmarkConfig config;
        bool mode_set = false;
        bool dimension_set = false;
        bool task_count_set = false;
        bool seed_set = false;
        bool repetitions_set = false;
        bool output_set = false;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--help") {
                usage(std::cout);
                return 0;
            }
            const std::string value = require_value(index, argc, argv);
            if (option == "--mode") {
                config.execution.mode = bom::parse_execution_mode(value);
                mode_set = true;
            } else if (option == "--dimension") {
                config.dimension = parse_integer<std::uint32_t>(value, option);
                dimension_set = true;
            } else if (option == "--task-count") {
                config.task_count = parse_integer<std::size_t>(value, option);
                task_count_set = true;
            } else if (option == "--base-seed") {
                config.base_seed = parse_integer<std::uint64_t>(value, option);
                seed_set = true;
            } else if (option == "--worker-host") {
                config.execution.worker_host = value;
            } else if (option == "--worker-port") {
                config.execution.worker_port = parse_integer<std::uint16_t>(value, option);
            } else if (option == "--connection-mode") {
                config.execution.connection_mode = bom::parse_connection_mode(value);
            } else if (option == "--warmup-count") {
                config.warmup_count = parse_integer<std::size_t>(value, option);
            } else if (option == "--repetitions") {
                config.measured_repetitions = parse_integer<std::size_t>(value, option);
                repetitions_set = true;
            } else if (option == "--connect-timeout-ms") {
                config.execution.network.connect_timeout = std::chrono::milliseconds(
                    parse_integer<std::int64_t>(value, option));
            } else if (option == "--message-timeout-ms") {
                config.execution.network.message_timeout = std::chrono::milliseconds(
                    parse_integer<std::int64_t>(value, option));
            } else if (option == "--output-dir") {
                config.output_directory = value;
                output_set = true;
            } else if (option == "--experiment-id") {
                config.experiment_id = value;
            } else {
                throw std::invalid_argument("unknown option: " + option);
            }
        }
        if (!(mode_set && dimension_set && task_count_set && seed_set && repetitions_set && output_set)) {
            usage(std::cerr);
            throw std::invalid_argument("required arguments are missing");
        }
        if (config.execution.network.connect_timeout.count() <= 0 ||
            config.execution.network.message_timeout.count() <= 0) {
            throw std::invalid_argument("network timeouts must be positive");
        }
        const auto result = bom::run_benchmark(config);
        std::cout << "task_csv=" << result.task_csv.string() << '\n'
                  << "batch_csv=" << result.batch_csv.string() << '\n'
                  << "environment=" << result.environment_json.string() << '\n'
                  << "valid_batches=" << result.valid_batches << '\n'
                  << "invalid_batches=" << result.invalid_batches << '\n';
        return result.invalid_batches == 0 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
