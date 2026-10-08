#include "benchmark.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

namespace bom {
namespace {

struct MeasuredBatch {
    std::size_t repetition;
    DistributedBatch batch;
    Verification verification;
    std::string timestamp;
};

std::string wall_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()) %
                              1000;
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
           << std::setfill('0') << milliseconds.count() << 'Z';
    return output.str();
}

std::string default_experiment_id(const BenchmarkConfig& config) {
    std::string value = wall_timestamp();
    for (char& character : value) {
        if (character == ':' || character == '.') {
            character = '-';
        }
    }
    return value + "_" + to_string(config.execution.mode) + "_n" +
           std::to_string(config.dimension) + "_tasks" + std::to_string(config.task_count);
}

std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

std::string json_field(const std::string& value) {
    std::string escaped;
    for (const char character : value) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
        }
    }
    return escaped;
}

std::string architecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

std::string operating_system() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

std::string os_version() {
#if defined(__APPLE__) || defined(__linux__)
    utsname value{};
    if (uname(&value) == 0) {
        return std::string(value.release) + " " + value.version;
    }
#endif
    return "unknown";
}

std::string cpu_model() {
#if defined(__APPLE__)
    std::size_t size = 0;
    if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return "unknown";
    }
    std::string value(size, '\0');
    if (sysctlbyname("machdep.cpu.brand_string", value.data(), &size, nullptr, 0) != 0) {
        return "unknown";
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value.empty() ? "unknown" : value;
#elif defined(__linux__)
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        if (key.find("model name") != std::string::npos || key.find("Hardware") != std::string::npos) {
            const auto start = line.find_first_not_of(" \t", separator + 1);
            return start == std::string::npos ? "unknown" : line.substr(start);
        }
    }
    return "unknown";
#else
    return "unknown";
#endif
}

void require_new_file(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        throw std::invalid_argument("refusing to overwrite existing output file: " + path.string());
    }
}

void write_task_csv(
    const std::filesystem::path& path,
    const BenchmarkConfig& config,
    const std::string& experiment_id,
    const std::string& run_id,
    const std::vector<MeasuredBatch>& batches) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create task observation CSV");
    }
    output << "timestamp,experiment_id,run_id,repetition,mode,matrix_size,task_count,base_seed,"
              "workload_version,connection_mode,connect_timeout_ms,message_timeout_ms,task_id,"
              "assigned_worker,dispatch_ns,completion_ns,task_latency_ns,"
              "prepare_ns,compute_ns,checksum_ns,checksum,valid,failure_reason\n";
    for (const auto& measured : batches) {
        for (const auto& item : measured.batch.observations) {
            const bool valid = item.success &&
                               measured.verification.valid;
            const std::uint64_t latency = item.completion_ns >= item.dispatch_ns
                                              ? item.completion_ns - item.dispatch_ns
                                              : 0;
            output << measured.timestamp << ',' << csv_field(experiment_id) << ','
                   << csv_field(run_id) << ',' << measured.repetition << ','
                   << to_string(config.execution.mode) << ',' << config.dimension << ','
                   << config.task_count << ',' << config.base_seed << ",1,"
                   << to_string(config.execution.connection_mode) << ','
                   << config.execution.network.connect_timeout.count() << ','
                   << config.execution.network.message_timeout.count() << ','
                   << item.result.task_id << ','
                   << csv_field(item.assigned_worker) << ',' << item.dispatch_ns << ','
                   << item.completion_ns << ',' << latency << ',' << item.result.prepare_ns << ','
                   << item.result.compute_ns << ',' << item.result.checksum_ns << ",0x" << std::hex
                   << std::setw(16) << std::setfill('0') << item.result.checksum << std::dec
                   << std::setfill(' ') << ',' << (valid ? "true" : "false") << ','
                   << csv_field(item.failure_reason.empty() && !measured.verification.valid
                                    ? measured.verification.failure_reason
                                    : item.failure_reason)
                   << '\n';
        }
    }
}

void write_batch_csv(
    const std::filesystem::path& path,
    const BenchmarkConfig& config,
    const std::string& experiment_id,
    const std::string& run_id,
    const std::vector<MeasuredBatch>& batches) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create batch observation CSV");
    }
    output << "experiment_id,run_id,repetition,mode,matrix_size,task_count,base_seed,"
              "workload_version,connection_mode,connect_timeout_ms,message_timeout_ms,"
              "total_batch_ns,connection_setup_ns,batch_valid,completed_tasks,"
              "failed_tasks,failure_reason\n";
    for (const auto& measured : batches) {
        output << csv_field(experiment_id) << ',' << csv_field(run_id) << ','
               << measured.repetition << ',' << to_string(config.execution.mode) << ','
               << config.dimension << ',' << config.task_count << ',' << config.base_seed << ",1,"
               << to_string(config.execution.connection_mode) << ','
               << config.execution.network.connect_timeout.count() << ','
               << config.execution.network.message_timeout.count() << ','
               << measured.batch.total_batch_ns << ',' << measured.batch.connection_setup_ns << ','
               << (measured.verification.valid ? "true" : "false") << ','
               << measured.verification.completed_tasks << ','
               << measured.verification.failed_tasks << ','
               << csv_field(measured.verification.failure_reason) << '\n';
    }
}

void write_environment(
    const std::filesystem::path& path,
    const BenchmarkConfig& config,
    const std::string& experiment_id) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create environment record");
    }
#ifndef BOM_COMPILER_ID
#define BOM_COMPILER_ID "unknown"
#endif
#ifndef BOM_COMPILER_VERSION
#define BOM_COMPILER_VERSION "unknown"
#endif
#ifndef BOM_BUILD_TYPE
#define BOM_BUILD_TYPE "unknown"
#endif
#ifndef BOM_COMPILE_FLAGS
#define BOM_COMPILE_FLAGS "unknown"
#endif
    output << "{\n"
           << "  \"experiment_id\": \"" << json_field(experiment_id) << "\",\n"
           << "  \"timestamp\": \"" << wall_timestamp() << "\",\n"
           << "  \"coordinator_architecture\": \"" << architecture() << "\",\n"
           << "  \"worker_architecture\": \"unknown\",\n"
           << "  \"coordinator_cpu_model\": \"" << json_field(cpu_model()) << "\",\n"
           << "  \"worker_cpu_model\": \"unknown\",\n"
           << "  \"operating_system\": \"" << operating_system() << "\",\n"
           << "  \"kernel_or_os_version\": \"" << json_field(os_version()) << "\",\n"
           << "  \"compiler\": \"" << json_field(BOM_COMPILER_ID) << "\",\n"
           << "  \"compiler_version\": \"" << json_field(BOM_COMPILER_VERSION) << "\",\n"
           << "  \"build_configuration\": \"" << json_field(BOM_BUILD_TYPE) << "\",\n"
           << "  \"compiler_flags\": \"" << json_field(BOM_COMPILE_FLAGS) << "\",\n"
           << "  \"physical_or_virtual_machine\": \"unknown\",\n"
           << "  \"worker_cpu_allocation\": \"unknown\",\n"
           << "  \"network_rtt\": \"unknown\",\n"
           << "  \"network_environment\": \"unknown\",\n"
           << "  \"power_mode\": \"unknown\",\n"
           << "  \"worker_host\": \"" << json_field(config.execution.worker_host) << "\",\n"
           << "  \"worker_port\": " << config.execution.worker_port << ",\n"
           << "  \"scale_out_eligible\": "
           << ((config.task_count >= 2) ? "true" : "false") << ",\n"
           << "  \"manual_fields_required\": [\"worker architecture\", \"worker CPU model\", "
              "\"host configuration\", \"network environment\", \"power mode\"]\n"
           << "}\n";
}

Verification failed_verification(const std::string& reason) {
    return Verification{false, 0, 0, reason};
}

}  // namespace

BenchmarkOutput run_benchmark(const BenchmarkConfig& config) {
    if (config.measured_repetitions == 0) {
        throw std::invalid_argument("measured repetitions must be greater than zero");
    }
    if (config.output_directory.empty()) {
        throw std::invalid_argument("output directory must be specified");
    }
    if (config.measured_repetitions > std::vector<MeasuredBatch>().max_size()) {
        throw std::invalid_argument("measured repetition count is unsupported");
    }
    const auto tasks = make_tasks(config.dimension, config.task_count, config.base_seed);
    const auto expected = compute_expected_checksums(tasks);

    for (std::size_t warmup = 0; warmup < config.warmup_count; ++warmup) {
        const auto batch = run_distributed_batch(tasks, config.execution);
        const auto verification = verify_batch(tasks, expected, batch.observations);
        if (!verification.valid) {
            throw std::runtime_error("warm-up batch failed correctness verification: " +
                                     verification.failure_reason);
        }
    }

    std::vector<MeasuredBatch> batches;
    batches.reserve(config.measured_repetitions);
    for (std::size_t repetition = 0; repetition < config.measured_repetitions; ++repetition) {
        MeasuredBatch measured;
        measured.repetition = repetition;
        measured.timestamp = wall_timestamp();
        try {
            measured.batch = run_distributed_batch(tasks, config.execution);
            measured.verification = verify_batch(tasks, expected, measured.batch.observations);
        } catch (const std::exception& error) {
            measured.verification = failed_verification(error.what());
        } catch (...) {
            measured.verification = failed_verification("unknown batch failure");
        }
        batches.push_back(std::move(measured));
    }

    std::filesystem::create_directories(config.output_directory);
    BenchmarkOutput result{
        config.output_directory / "task_observations.csv",
        config.output_directory / "batch_observations.csv",
        config.output_directory / "environment.json",
        0,
        0,
    };
    require_new_file(result.task_csv);
    require_new_file(result.batch_csv);
    require_new_file(result.environment_json);

    const std::string experiment_id = config.experiment_id.empty()
                                          ? default_experiment_id(config)
                                          : config.experiment_id;
    const std::string run_id = experiment_id + "_run0";
    write_task_csv(result.task_csv, config, experiment_id, run_id, batches);
    write_batch_csv(result.batch_csv, config, experiment_id, run_id, batches);
    write_environment(result.environment_json, config, experiment_id);
    for (const auto& batch : batches) {
        if (batch.verification.valid) {
            ++result.valid_batches;
        } else {
            ++result.invalid_batches;
        }
    }
    return result;
}

}  // namespace bom
