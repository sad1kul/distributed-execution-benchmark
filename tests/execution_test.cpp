#include "execution.hpp"

#include "workload.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
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

template <typename Function>
void expect_invalid(Function function, int line) {
    try {
        function();
        std::cerr << __FILE__ << ':' << line << ": expected invalid_argument\n";
        ++failures;
    } catch (const std::invalid_argument&) {
    } catch (...) {
        std::cerr << __FILE__ << ':' << line << ": unexpected exception\n";
        ++failures;
    }
}

#define CHECK_INVALID(expression) expect_invalid([&] { expression; }, __LINE__)

void deterministic_task_set() {
    const auto tasks = bom::make_tasks(3, 4, 0xffffffffffffffffULL);
    CHECK(tasks[0].task_id == 0 && tasks[0].seed == 0xffffffffffffffffULL);
    CHECK(tasks[1].task_id == 1 && tasks[1].seed == 0);
    CHECK(tasks[2].task_id == 2 && tasks[2].seed == 1);
    CHECK(tasks[3].task_id == 3 && tasks[3].seed == 2);
    CHECK_INVALID(bom::make_tasks(3, 0, 0));
    CHECK_INVALID(bom::make_tasks(0, 1, 0));
    CHECK_INVALID(bom::make_tasks(2049, 1, 0));
}

void local_modes() {
    for (const std::size_t count : {1U, 4U, 5U}) {
        const auto tasks = bom::make_tasks(4, count, 12345);
        const auto expected = bom::compute_expected_checksums(tasks);
        const auto local_one = bom::run_local_batch(tasks, 1);
        CHECK(bom::verify_batch(tasks, expected, local_one.observations).valid);
        for (const auto& item : local_one.observations) {
            CHECK(item.worker_index == 0);
            CHECK(item.success);
        }

        const auto local_two = bom::run_local_batch(tasks, 2);
        CHECK(bom::verify_batch(tasks, expected, local_two.observations).valid);
        for (std::size_t index = 0; index < local_two.observations.size(); ++index) {
            CHECK(local_two.observations[index].worker_index == index % 2);
            CHECK(local_two.observations[index].result.checksum ==
                  local_one.observations[index].result.checksum);
        }
        if (count >= 2) {
            CHECK(local_two.observations[0].assigned_worker !=
                  local_two.observations[1].assigned_worker);
        }
    }
}

void repeated_batches() {
    const auto tasks = bom::make_tasks(3, 3, 7);
    const auto expected = bom::compute_expected_checksums(tasks);
    const auto first = bom::run_local_batch(tasks, 2);
    const auto second = bom::run_local_batch(tasks, 2);
    CHECK(bom::verify_batch(tasks, expected, first.observations).valid);
    CHECK(bom::verify_batch(tasks, expected, second.observations).valid);
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        CHECK(first.observations[index].result.checksum == second.observations[index].result.checksum);
    }
}

void verification_failures() {
    const auto tasks = bom::make_tasks(2, 2, 0);
    const auto expected = bom::compute_expected_checksums(tasks);
    auto batch = bom::run_local_batch(tasks, 1);

    auto duplicate = batch.observations;
    duplicate[1].result.task_id = duplicate[0].result.task_id;
    CHECK(!bom::verify_batch(tasks, expected, duplicate).valid);

    auto mismatch = batch.observations;
    ++mismatch[0].result.checksum;
    const auto mismatch_verification = bom::verify_batch(tasks, expected, mismatch);
    CHECK(!mismatch_verification.valid);
    CHECK(mismatch_verification.completed_tasks == 1);
    CHECK(mismatch_verification.failed_tasks == 1);

    auto failed = batch.observations;
    failed[0].success = false;
    failed[0].failure_reason = "injected";
    CHECK(!bom::verify_batch(tasks, expected, failed).valid);

    batch.observations.pop_back();
    CHECK(!bom::verify_batch(tasks, expected, batch.observations).valid);
}

void zero_dimension_regression() {
    CHECK_INVALID(bom::multiply({0, {}}, {0, {}}));
    CHECK_INVALID(bom::checksum({0, {}}));
}

}  // namespace

int main() {
    deterministic_task_set();
    local_modes();
    repeated_batches();
    verification_failures();
    zero_dimension_regression();
    if (failures == 0) {
        std::cout << "execution tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
