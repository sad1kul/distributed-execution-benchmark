#include "distributed.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bom {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t nanoseconds(Clock::duration duration) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

bool needs_remote(ExecutionMode mode) {
    return mode == ExecutionMode::remote_1 || mode == ExecutionMode::local_remote;
}

std::size_t lane_count(ExecutionMode mode) {
    return mode == ExecutionMode::local_1 || mode == ExecutionMode::remote_1 ? 1U : 2U;
}

bool lane_is_remote(ExecutionMode mode, std::size_t lane) {
    return mode == ExecutionMode::remote_1 || (mode == ExecutionMode::local_remote && lane == 1);
}

}  // namespace

std::string to_string(ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::local_1:
            return "local-1";
        case ExecutionMode::remote_1:
            return "remote-1";
        case ExecutionMode::local_2:
            return "local-2";
        case ExecutionMode::local_remote:
            return "local-remote";
    }
    throw std::invalid_argument("unknown execution mode");
}

std::string to_string(ConnectionMode mode) {
    switch (mode) {
        case ConnectionMode::persistent:
            return "persistent";
        case ConnectionMode::fresh_per_task:
            return "fresh-per-task";
    }
    throw std::invalid_argument("unknown connection mode");
}

ExecutionMode parse_execution_mode(const std::string& value) {
    if (value == "local-1") {
        return ExecutionMode::local_1;
    }
    if (value == "remote-1") {
        return ExecutionMode::remote_1;
    }
    if (value == "local-2") {
        return ExecutionMode::local_2;
    }
    if (value == "local-remote") {
        return ExecutionMode::local_remote;
    }
    throw std::invalid_argument("mode must be local-1, remote-1, local-2, or local-remote");
}

ConnectionMode parse_connection_mode(const std::string& value) {
    if (value == "persistent") {
        return ConnectionMode::persistent;
    }
    if (value == "fresh-per-task") {
        return ConnectionMode::fresh_per_task;
    }
    throw std::invalid_argument("connection mode must be persistent or fresh-per-task");
}

DistributedBatch run_distributed_batch(
    const std::vector<TaskSpec>& tasks,
    const DistributedConfig& config) {
    if (tasks.empty()) {
        throw std::invalid_argument("task batch must not be empty");
    }
    if (needs_remote(config.mode) && config.worker_port == 0) {
        throw std::invalid_argument("remote modes require a nonzero worker port");
    }

    DistributedBatch batch;
    batch.observations.resize(tasks.size());
    const std::size_t lanes = lane_count(config.mode);
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool start = false;
    Clock::time_point batch_start;
    std::vector<std::unique_ptr<RemoteSession>> persistent_sessions(lanes);
    std::vector<std::uint64_t> setup_durations(lanes, 0);
    std::vector<std::string> setup_errors(lanes);

    std::vector<std::thread> threads;
    threads.reserve(lanes);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        threads.emplace_back([&, lane] {
            {
                std::unique_lock lock(gate_mutex);
                gate.wait(lock, [&] { return start; });
            }
            const bool remote = lane_is_remote(config.mode, lane);
            if (remote && config.connection_mode == ConnectionMode::persistent && lane < tasks.size()) {
                const auto setup_start = Clock::now();
                try {
                    persistent_sessions[lane] = std::make_unique<RemoteSession>(
                        config.worker_host, config.worker_port, config.network);
                    setup_durations[lane] = nanoseconds(Clock::now() - setup_start);
                } catch (const std::exception& error) {
                    setup_durations[lane] = nanoseconds(Clock::now() - setup_start);
                    setup_errors[lane] = error.what();
                } catch (...) {
                    setup_durations[lane] = nanoseconds(Clock::now() - setup_start);
                    setup_errors[lane] = "unknown connection setup failure";
                }
            }

            for (std::size_t index = lane; index < tasks.size(); index += lanes) {
                TaskObservation observation;
                observation.worker_index = lane;
                observation.assigned_worker = remote ? "remote-0" : "local-" + std::to_string(lane);
                observation.dispatch_ns = nanoseconds(Clock::now() - batch_start);
                try {
                    if (!remote) {
                        observation.result = execute_task(tasks[index]);
                    } else if (config.connection_mode == ConnectionMode::persistent) {
                        if (!persistent_sessions[lane]) {
                            throw std::runtime_error("persistent connection setup failed: " +
                                                     setup_errors[lane]);
                        }
                        observation.result = persistent_sessions[lane]->execute(tasks[index]);
                    } else {
                        observation.result = execute_remote_fresh(
                            config.worker_host, config.worker_port, tasks[index], config.network);
                    }
                    observation.success = true;
                } catch (const std::exception& error) {
                    observation.result.task_id = tasks[index].task_id;
                    observation.failure_reason = error.what();
                } catch (...) {
                    observation.result.task_id = tasks[index].task_id;
                    observation.failure_reason = "unknown execution failure";
                }
                observation.completion_ns = nanoseconds(Clock::now() - batch_start);
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
    for (auto& thread : threads) {
        thread.join();
    }
    batch.total_batch_ns = nanoseconds(Clock::now() - batch_start);
    for (const auto duration : setup_durations) {
        batch.connection_setup_ns += duration;
    }

    // Persistent teardown is deliberately after the recorded batch boundary.
    for (auto& session : persistent_sessions) {
        if (session) {
            try {
                session->shutdown();
            } catch (...) {
            }
        }
    }
    return batch;
}

}  // namespace bom
