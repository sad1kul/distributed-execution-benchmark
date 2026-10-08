#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bom {

struct TaskSpec {
    std::uint64_t task_id;
    std::uint32_t dimension;
    std::uint64_t seed;
};

struct TaskResult {
    std::uint64_t task_id;
    std::uint64_t checksum;
    std::uint64_t prepare_ns;
    std::uint64_t compute_ns;
    std::uint64_t checksum_ns;
};

struct TaskObservation {
    TaskResult result{};
    std::size_t worker_index{};
    std::string assigned_worker;
    std::uint64_t dispatch_ns{};
    std::uint64_t completion_ns{};
    bool success{false};
    std::string failure_reason;
};

struct BatchExecution {
    std::vector<TaskObservation> observations;
    std::uint64_t total_batch_ns{};
};

struct Verification {
    bool valid;
    std::size_t completed_tasks;
    std::size_t failed_tasks;
    std::string failure_reason;
};

std::vector<TaskSpec> make_tasks(
    std::uint32_t dimension,
    std::size_t task_count,
    std::uint64_t base_seed);
TaskResult execute_task(const TaskSpec& task);
std::unordered_map<std::uint64_t, std::uint64_t> compute_expected_checksums(
    const std::vector<TaskSpec>& tasks);
BatchExecution run_local_batch(const std::vector<TaskSpec>& tasks, std::size_t worker_count);
Verification verify_batch(
    const std::vector<TaskSpec>& tasks,
    const std::unordered_map<std::uint64_t, std::uint64_t>& expected,
    const std::vector<TaskObservation>& observations);

}  // namespace bom
