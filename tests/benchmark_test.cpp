#include "benchmark.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string first_run_id(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string header;
    std::string row;
    std::getline(input, header);
    std::getline(input, row);
    const auto first = row.find(',');
    const auto second = row.find(',', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return {};
    }
    return row.substr(first + 1, second - first - 1);
}

void local_output_pipeline() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("bom-benchmark-test-" + std::to_string(unique));
    const auto directory = root / "first";
    bom::BenchmarkConfig config;
    config.execution.mode = bom::ExecutionMode::local_2;
    config.dimension = 3;
    config.task_count = 3;
    config.base_seed = 42;
    config.warmup_count = 0;
    config.measured_repetitions = 2;
    config.output_directory = directory;
    config.experiment_id = "test-experiment";
    const auto output = bom::run_benchmark(config);
    CHECK(output.valid_batches == 2);
    CHECK(output.invalid_batches == 0);
    const auto tasks = read_file(output.task_csv);
    const auto batches = read_file(output.batch_csv);
    const auto environment = read_file(output.environment_json);
    CHECK(tasks.find("task_latency_ns") != std::string::npos);
    CHECK(tasks.find("test-experiment") != std::string::npos);
    CHECK(batches.find("total_batch_ns") != std::string::npos);
    CHECK(batches.find("connection_setup_ns") != std::string::npos);
    CHECK(environment.find("coordinator_architecture") != std::string::npos);

    auto second_config = config;
    second_config.output_directory = root / "second";
    const auto second_output = bom::run_benchmark(second_config);
    CHECK(first_run_id(output.batch_csv) != first_run_id(second_output.batch_csv));

    try {
        static_cast<void>(bom::run_benchmark(config));
        CHECK(false);
    } catch (const std::invalid_argument&) {
    }
    std::filesystem::remove_all(root);
}

void validation() {
    bom::BenchmarkConfig config;
    config.output_directory = std::filesystem::temp_directory_path() / "unused-bom-output";
    config.measured_repetitions = 0;
    try {
        static_cast<void>(bom::run_benchmark(config));
        CHECK(false);
    } catch (const std::invalid_argument&) {
    }
}

}  // namespace

int main() {
    local_output_pipeline();
    validation();
    if (failures == 0) {
        std::cout << "benchmark tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
