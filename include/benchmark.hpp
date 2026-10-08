#pragma once

#include "distributed.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace bom {

struct BenchmarkConfig {
    DistributedConfig execution;
    std::uint32_t dimension{4};
    std::size_t task_count{4};
    std::uint64_t base_seed{0};
    std::size_t warmup_count{1};
    std::size_t measured_repetitions{5};
    std::filesystem::path output_directory;
    std::string experiment_id;
};

struct BenchmarkOutput {
    std::filesystem::path task_csv;
    std::filesystem::path batch_csv;
    std::filesystem::path environment_json;
    std::size_t valid_batches{};
    std::size_t invalid_batches{};
};

BenchmarkOutput run_benchmark(const BenchmarkConfig& config);

}  // namespace bom
