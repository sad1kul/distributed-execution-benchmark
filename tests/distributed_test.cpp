#include "distributed.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

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

class RunningWorker {
public:
    RunningWorker() : server_("127.0.0.1", 0), thread_([this] { run(); }) {}
    ~RunningWorker() {
        server_.request_stop();
        thread_.join();
    }
    std::uint16_t port() const { return server_.port(); }
    void check() const { CHECK(!failed_.load()); }

private:
    void run() {
        try {
            server_.run();
        } catch (...) {
            failed_.store(true);
        }
    }
    bom::WorkerServer server_;
    std::atomic<bool> failed_{false};
    std::thread thread_;
};

void all_modes_and_connections() {
    RunningWorker worker;
    const auto tasks = bom::make_tasks(5, 5, 100);
    const auto expected = bom::compute_expected_checksums(tasks);
    const std::vector<bom::ExecutionMode> modes = {
        bom::ExecutionMode::local_1,
        bom::ExecutionMode::remote_1,
        bom::ExecutionMode::local_2,
        bom::ExecutionMode::local_remote,
    };
    std::vector<std::uint64_t> baseline;
    for (const auto mode : modes) {
        for (const auto connection :
             {bom::ConnectionMode::persistent, bom::ConnectionMode::fresh_per_task}) {
            const bom::DistributedConfig config{mode, connection, "127.0.0.1", worker.port()};
            const auto batch = bom::run_distributed_batch(tasks, config);
            CHECK(bom::verify_batch(tasks, expected, batch.observations).valid);
            CHECK(batch.observations.size() == tasks.size());
            if (baseline.empty()) {
                for (const auto& item : batch.observations) {
                    baseline.push_back(item.result.checksum);
                }
            } else {
                for (std::size_t index = 0; index < tasks.size(); ++index) {
                    CHECK(batch.observations[index].result.checksum == baseline[index]);
                }
            }
        }
    }
    worker.check();
}

void static_assignment_and_single_task() {
    RunningWorker worker;
    for (const std::size_t count : {1U, 4U, 5U}) {
        const auto tasks = bom::make_tasks(3, count, 1);
        const auto expected = bom::compute_expected_checksums(tasks);
        for (const auto mode : {bom::ExecutionMode::local_2, bom::ExecutionMode::local_remote}) {
            const bom::DistributedConfig config{
                mode, bom::ConnectionMode::persistent, "127.0.0.1", worker.port()};
            const auto batch = bom::run_distributed_batch(tasks, config);
            CHECK(bom::verify_batch(tasks, expected, batch.observations).valid);
            for (std::size_t index = 0; index < count; ++index) {
                CHECK(batch.observations[index].worker_index == index % 2);
            }
            if (count >= 2) {
                CHECK(batch.observations[0].assigned_worker !=
                      batch.observations[1].assigned_worker);
            }
        }
    }
    worker.check();
}

void completion_order_does_not_affect_verification() {
    const auto tasks = bom::make_tasks(3, 4, 9);
    const auto expected = bom::compute_expected_checksums(tasks);
    auto batch = bom::run_distributed_batch(tasks, {});
    std::reverse(batch.observations.begin(), batch.observations.end());
    CHECK(bom::verify_batch(tasks, expected, batch.observations).valid);
}

}  // namespace

int main() {
    all_modes_and_connections();
    static_assignment_and_single_task();
    completion_order_does_not_affect_verification();
    if (failures == 0) {
        std::cout << "distributed execution tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
