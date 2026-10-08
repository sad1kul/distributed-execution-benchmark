#include "execution.hpp"

#include "workload.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace bom {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t nanoseconds(Clock::duration duration) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

}  // namespace

std::vector<TaskSpec> make_tasks(
    std::uint32_t dimension,
    std::size_t task_count,
    std::uint64_t base_seed) {
    if (task_count == 0) {
        throw std::invalid_argument("task count must be greater than zero");
    }
    if (task_count > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        throw std::invalid_argument("task count cannot be represented by task IDs");
    }
    if (task_count > std::vector<TaskSpec>().max_size()) {
        throw std::invalid_argument("task count is unsupported by this implementation");
    }
    // Validation occurs before reserving task storage or allocating matrices.
    if (dimension == 0 || dimension > kDefaultMaxDimension) {
        throw std::invalid_argument("matrix dimension is outside the permitted range");
    }

    std::vector<TaskSpec> tasks;
    tasks.reserve(task_count);
    for (std::size_t index = 0; index < task_count; ++index) {
        const auto task_id = static_cast<std::uint64_t>(index);
        tasks.push_back(TaskSpec{task_id, dimension, base_seed + task_id});
    }
    return tasks;
}

TaskResult execute_task(const TaskSpec& task) {
    const auto prepare_start = Clock::now();
    const Inputs inputs = generate_inputs(task.dimension, task.seed);
    const auto prepare_end = Clock::now();
    const MatrixC product = multiply(inputs.a, inputs.b);
    const auto compute_end = Clock::now();
    const std::uint64_t result_checksum = checksum(product);
    const auto checksum_end = Clock::now();
    return TaskResult{
        task.task_id,
        result_checksum,
        nanoseconds(prepare_end - prepare_start),
        nanoseconds(compute_end - prepare_end),
        nanoseconds(checksum_end - compute_end),
    };
}

std::unordered_map<std::uint64_t, std::uint64_t> compute_expected_checksums(
    const std::vector<TaskSpec>& tasks) {
    if (tasks.empty()) {
        throw std::invalid_argument("expected-checksum task set must not be empty");
    }
    std::unordered_map<std::uint64_t, std::uint64_t> expected;
    expected.reserve(tasks.size());
    for (const auto& task : tasks) {
        const auto [iterator, inserted] = expected.emplace(task.task_id, execute_task(task).checksum);
        static_cast<void>(iterator);
        if (!inserted) {
            throw std::invalid_argument("task IDs must be unique");
        }
    }
    return expected;
}

BatchExecution run_local_batch(const std::vector<TaskSpec>& tasks, std::size_t worker_count) {
    if (tasks.empty()) {
        throw std::invalid_argument("task batch must not be empty");
    }
    if (worker_count == 0 || worker_count > 2) {
        throw std::invalid_argument("V1 local worker count must be one or two");
    }

    BatchExecution batch;
    batch.observations.resize(tasks.size());
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool start = false;
    Clock::time_point batch_start;

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&, worker] {
            {
                std::unique_lock lock(gate_mutex);
                gate.wait(lock, [&] { return start; });
            }
            for (std::size_t index = worker; index < tasks.size(); index += worker_count) {
                TaskObservation observation;
                observation.worker_index = worker;
                observation.assigned_worker = "local-" + std::to_string(worker);
                observation.dispatch_ns = nanoseconds(Clock::now() - batch_start);
                try {
                    observation.result = execute_task(tasks[index]);
                    observation.success = true;
                } catch (const std::exception& error) {
                    observation.result.task_id = tasks[index].task_id;
                    observation.failure_reason = error.what();
                } catch (...) {
                    observation.result.task_id = tasks[index].task_id;
                    observation.failure_reason = "unknown task failure";
                }
                observation.completion_ns = nanoseconds(Clock::now() - batch_start);
                // Static ownership gives each thread a disjoint slot, avoiding a timing mutex.
                batch.observations[index] = std::move(observation);
            }
        });
    }

    {
        std::lock_guard lock(gate_mutex);
        batch_start = Clock::now();
        start = true;
    }
    gate.notify_all();
    for (auto& worker : workers) {
        worker.join();
    }
    batch.total_batch_ns = nanoseconds(Clock::now() - batch_start);
    return batch;
}

Verification verify_batch(
    const std::vector<TaskSpec>& tasks,
    const std::unordered_map<std::uint64_t, std::uint64_t>& expected,
    const std::vector<TaskObservation>& observations) {
    std::vector<std::string> errors;
    std::unordered_set<std::uint64_t> task_ids;
    task_ids.reserve(tasks.size());
    for (const auto& task : tasks) {
        if (!task_ids.insert(task.task_id).second) {
            errors.push_back("duplicate expected task ID " + std::to_string(task.task_id));
        }
        if (!expected.contains(task.task_id)) {
            errors.push_back("missing expected checksum for task " + std::to_string(task.task_id));
        }
    }
    if (expected.size() != tasks.size()) {
        errors.push_back("expected checksum set does not match task set");
    }

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(observations.size());
    std::size_t completed = 0;
    std::size_t failed = 0;
    for (const auto& observation : observations) {
        const auto task_id = observation.result.task_id;
        if (!task_ids.contains(task_id)) {
            errors.push_back("unexpected task ID " + std::to_string(task_id));
        }
        if (!seen.insert(task_id).second) {
            errors.push_back("duplicate result for task " + std::to_string(task_id));
        }
        if (!observation.success) {
            ++failed;
            errors.push_back("task " + std::to_string(task_id) + " failed: " +
                             observation.failure_reason);
            continue;
        }
        ++completed;
        const auto iterator = expected.find(task_id);
        if (iterator != expected.end() && observation.result.checksum != iterator->second) {
            errors.push_back("checksum mismatch for task " + std::to_string(task_id));
        }
    }
    for (const auto task_id : task_ids) {
        if (!seen.contains(task_id)) {
            errors.push_back("missing result for task " + std::to_string(task_id));
        }
    }

    std::ostringstream reason;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            reason << "; ";
        }
        reason << errors[index];
    }
    return Verification{errors.empty(), completed, failed, reason.str()};
}

}  // namespace bom
