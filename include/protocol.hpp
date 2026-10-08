#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace bom::protocol {

constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr std::uint32_t kMaxPayloadSize = 1024;
constexpr std::uint64_t kUnknownTaskId = 0xffffffffffffffffULL;

enum class MessageType : std::uint16_t {
    hello = 1,
    task = 2,
    result = 3,
    error = 4,
    shutdown = 5,
};

enum class ErrorCode : std::uint16_t {
    malformed_message = 1,
    handshake_required = 2,
    invalid_task = 3,
    task_failed = 4,
    unexpected_message = 5,
};

struct Header {
    std::uint16_t version;
    MessageType type;
    std::uint32_t payload_length;
};

struct Task {
    std::uint64_t task_id;
    std::uint32_t dimension;
    std::uint64_t seed;
};

struct Result {
    std::uint64_t task_id;
    std::uint16_t status;
    std::uint64_t checksum;
    std::uint64_t prepare_ns;
    std::uint64_t compute_ns;
    std::uint64_t checksum_ns;
};

struct Error {
    std::uint64_t task_id;
    ErrorCode code;
};

struct Message {
    MessageType type;
    std::vector<std::uint8_t> payload;
};

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::size_t expected_payload_length(MessageType type);
std::vector<std::uint8_t> encode_header(const Header& header);
Header decode_header(std::span<const std::uint8_t> bytes);

Message make_hello();
Message make_task(const Task& task);
Message make_result(const Result& result);
Message make_error(const Error& error);
Message make_shutdown();

Task parse_task(const Message& message);
Result parse_result(const Message& message);
Error parse_error(const Message& message);

std::vector<std::uint8_t> encode_message(const Message& message);
Message decode_message(std::span<const std::uint8_t> bytes);

class FrameDecoder {
public:
    void append(std::span<const std::uint8_t> bytes);
    std::optional<Message> next();
    [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
    std::vector<std::uint8_t> buffer_;
};

}  // namespace bom::protocol
