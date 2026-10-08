#pragma once

#include "execution.hpp"
#include "remote.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bom {

enum class ExecutionMode {
    local_1,
    remote_1,
    local_2,
    local_remote,
};

enum class ConnectionMode {
    persistent,
    fresh_per_task,
};

struct DistributedConfig {
    ExecutionMode mode{ExecutionMode::local_1};
    ConnectionMode connection_mode{ConnectionMode::persistent};
    std::string worker_host{"127.0.0.1"};
    std::uint16_t worker_port{};
    NetworkConfig network{};
};

struct DistributedBatch {
    std::vector<TaskObservation> observations;
    std::uint64_t total_batch_ns{};
    std::uint64_t connection_setup_ns{};
};

std::string to_string(ExecutionMode mode);
std::string to_string(ConnectionMode mode);
ExecutionMode parse_execution_mode(const std::string& value);
ConnectionMode parse_connection_mode(const std::string& value);

DistributedBatch run_distributed_batch(
    const std::vector<TaskSpec>& tasks,
    const DistributedConfig& config);

}  // namespace bom
