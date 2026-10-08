#include "protocol.hpp"

#include "workload.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace bom::protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'B', 'O', 'M', 'X'};

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

MessageType checked_type(std::uint16_t raw) {
    switch (raw) {
        case 1:
            return MessageType::hello;
        case 2:
            return MessageType::task;
        case 3:
            return MessageType::result;
        case 4:
            return MessageType::error;
        case 5:
            return MessageType::shutdown;
        default:
            throw ProtocolError("unknown message type");
    }
}

void require_type_and_size(const Message& message, MessageType type) {
    if (message.type != type) {
        throw ProtocolError("unexpected message type");
    }
    if (message.payload.size() != expected_payload_length(type)) {
        throw ProtocolError("incorrect payload length");
    }
}

}  // namespace

std::size_t expected_payload_length(MessageType type) {
    switch (type) {
        case MessageType::hello:
        case MessageType::shutdown:
            return 0;
        case MessageType::task:
            return 20;
        case MessageType::result:
            return 42;
        case MessageType::error:
            return 10;
    }
    throw ProtocolError("unknown message type");
}

std::vector<std::uint8_t> encode_header(const Header& header) {
    if (header.version != kVersion) {
        throw ProtocolError("unsupported protocol version");
    }
    if (header.payload_length > kMaxPayloadSize) {
        throw ProtocolError("payload exceeds protocol limit");
    }
    if (header.payload_length != expected_payload_length(header.type)) {
        throw ProtocolError("incorrect payload length");
    }

    std::vector<std::uint8_t> output(kMagic.begin(), kMagic.end());
    output.reserve(kHeaderSize);
    append_u16(output, header.version);
    append_u16(output, static_cast<std::uint16_t>(header.type));
    append_u32(output, header.payload_length);
    return output;
}

Header decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw ProtocolError("truncated message header");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        throw ProtocolError("invalid protocol magic");
    }
    const std::uint16_t version = read_u16(bytes, 4);
    if (version != kVersion) {
        throw ProtocolError("unsupported protocol version");
    }
    const MessageType type = checked_type(read_u16(bytes, 6));
    const std::uint32_t payload_length = read_u32(bytes, 8);
    if (payload_length > kMaxPayloadSize) {
        throw ProtocolError("payload exceeds protocol limit");
    }
    if (payload_length != expected_payload_length(type)) {
        throw ProtocolError("incorrect payload length");
    }
    return Header{version, type, payload_length};
}

Message make_hello() {
    return Message{MessageType::hello, {}};
}

Message make_task(const Task& task) {
    if (task.dimension == 0 || task.dimension > kDefaultMaxDimension) {
        throw ProtocolError("invalid task dimension");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(20);
    append_u64(payload, task.task_id);
    append_u32(payload, task.dimension);
    append_u64(payload, task.seed);
    return Message{MessageType::task, std::move(payload)};
}

Message make_result(const Result& result) {
    if (result.status != 0) {
        throw ProtocolError("successful result status must be zero");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(42);
    append_u64(payload, result.task_id);
    append_u16(payload, result.status);
    append_u64(payload, result.checksum);
    append_u64(payload, result.prepare_ns);
    append_u64(payload, result.compute_ns);
    append_u64(payload, result.checksum_ns);
    return Message{MessageType::result, std::move(payload)};
}

Message make_error(const Error& error) {
    const auto code = static_cast<std::uint16_t>(error.code);
    if (code < 1 || code > 5) {
        throw ProtocolError("unknown error code");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(10);
    append_u64(payload, error.task_id);
    append_u16(payload, code);
    return Message{MessageType::error, std::move(payload)};
}

Message make_shutdown() {
    return Message{MessageType::shutdown, {}};
}

Task parse_task(const Message& message) {
    require_type_and_size(message, MessageType::task);
    const Task task{
        read_u64(message.payload, 0),
        read_u32(message.payload, 8),
        read_u64(message.payload, 12),
    };
    if (task.dimension == 0 || task.dimension > kDefaultMaxDimension) {
        throw ProtocolError("invalid task dimension");
    }
    return task;
}

Result parse_result(const Message& message) {
    require_type_and_size(message, MessageType::result);
    const Result result{
        read_u64(message.payload, 0),
        read_u16(message.payload, 8),
        read_u64(message.payload, 10),
        read_u64(message.payload, 18),
        read_u64(message.payload, 26),
        read_u64(message.payload, 34),
    };
    if (result.status != 0) {
        throw ProtocolError("successful result status must be zero");
    }
    return result;
}

Error parse_error(const Message& message) {
    require_type_and_size(message, MessageType::error);
    const std::uint16_t raw_code = read_u16(message.payload, 8);
    if (raw_code < 1 || raw_code > 5) {
        throw ProtocolError("unknown error code");
    }
    return Error{read_u64(message.payload, 0), static_cast<ErrorCode>(raw_code)};
}

std::vector<std::uint8_t> encode_message(const Message& message) {
    if (message.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ProtocolError("payload length cannot be represented");
    }
    const Header header{
        kVersion,
        message.type,
        static_cast<std::uint32_t>(message.payload.size()),
    };
    auto output = encode_header(header);
    output.insert(output.end(), message.payload.begin(), message.payload.end());
    return output;
}

Message decode_message(std::span<const std::uint8_t> bytes) {
    const Header header = decode_header(bytes);
    const std::size_t total = kHeaderSize + header.payload_length;
    if (bytes.size() < total) {
        throw ProtocolError("truncated message payload");
    }
    if (bytes.size() != total) {
        throw ProtocolError("trailing bytes after message");
    }
    return Message{
        header.type,
        std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                  bytes.end()),
    };
}

void FrameDecoder::append(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaximumBuffered = kHeaderSize + kMaxPayloadSize;
    if (bytes.size() > kMaximumBuffered - std::min(buffer_.size(), kMaximumBuffered)) {
        throw ProtocolError("frame buffer exceeds protocol limit");
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

std::optional<Message> FrameDecoder::next() {
    if (buffer_.size() < kHeaderSize) {
        return std::nullopt;
    }
    const Header header = decode_header(buffer_);
    const std::size_t total = kHeaderSize + header.payload_length;
    if (buffer_.size() < total) {
        return std::nullopt;
    }
    Message result{
        header.type,
        std::vector<std::uint8_t>(buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                  buffer_.begin() + static_cast<std::ptrdiff_t>(total)),
    };
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
    return result;
}

std::size_t FrameDecoder::buffered_size() const noexcept {
    return buffer_.size();
}

}  // namespace bom::protocol
