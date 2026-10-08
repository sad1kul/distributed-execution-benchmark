#include "protocol.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
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
void expect_protocol_error(Function function, int line) {
    try {
        function();
        std::cerr << __FILE__ << ':' << line << ": expected ProtocolError\n";
        ++failures;
    } catch (const bom::protocol::ProtocolError&) {
    } catch (...) {
        std::cerr << __FILE__ << ':' << line << ": unexpected exception type\n";
        ++failures;
    }
}

#define CHECK_PROTOCOL_ERROR(expression) expect_protocol_error([&] { expression; }, __LINE__)

void header_and_byte_order() {
    using namespace bom::protocol;
    const auto bytes = encode_header({kVersion, MessageType::task, 20});
    const std::vector<std::uint8_t> expected = {
        'B', 'O', 'M', 'X', 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x14};
    CHECK(bytes == expected);
    const auto header = decode_header(bytes);
    CHECK(header.version == 1);
    CHECK(header.type == MessageType::task);
    CHECK(header.payload_length == 20);
}

void round_trips() {
    using namespace bom::protocol;
    CHECK(decode_message(encode_message(make_hello())).type == MessageType::hello);
    CHECK(decode_message(encode_message(make_shutdown())).type == MessageType::shutdown);

    const Task task{0x0102030405060708ULL, 0x00000102U, 0xf1f2f3f4f5f6f7f8ULL};
    const auto task_bytes = encode_message(make_task(task));
    CHECK(task_bytes[12] == 0x01 && task_bytes[19] == 0x08);
    CHECK(task_bytes[20] == 0x00 && task_bytes[23] == 0x02);
    const auto task_copy = parse_task(decode_message(task_bytes));
    CHECK(task_copy.task_id == task.task_id);
    CHECK(task_copy.dimension == task.dimension);
    CHECK(task_copy.seed == task.seed);

    const Result result{7, 0, 0x8877665544332211ULL, 10, 20, 30};
    const auto result_copy = parse_result(decode_message(encode_message(make_result(result))));
    CHECK(result_copy.task_id == 7);
    CHECK(result_copy.checksum == result.checksum);
    CHECK(result_copy.prepare_ns == 10);
    CHECK(result_copy.compute_ns == 20);
    CHECK(result_copy.checksum_ns == 30);

    const Error error{9, ErrorCode::task_failed};
    const auto error_copy = parse_error(decode_message(encode_message(make_error(error))));
    CHECK(error_copy.task_id == 9);
    CHECK(error_copy.code == ErrorCode::task_failed);
}

void malformed_frames() {
    using namespace bom::protocol;
    auto hello = encode_message(make_hello());
    auto invalid_magic = hello;
    invalid_magic[0] = 'X';
    CHECK_PROTOCOL_ERROR(decode_message(invalid_magic));

    auto invalid_version = hello;
    invalid_version[5] = 2;
    CHECK_PROTOCOL_ERROR(decode_message(invalid_version));

    auto unknown_type = hello;
    unknown_type[7] = 99;
    CHECK_PROTOCOL_ERROR(decode_message(unknown_type));

    CHECK_PROTOCOL_ERROR(decode_message(std::span<const std::uint8_t>(hello).first(11)));

    auto task = encode_message(make_task({1, 4, 2}));
    CHECK_PROTOCOL_ERROR(decode_message(std::span<const std::uint8_t>(task).first(task.size() - 1)));

    auto oversized = hello;
    oversized[8] = 0x00;
    oversized[9] = 0x00;
    oversized[10] = 0x04;
    oversized[11] = 0x01;
    CHECK_PROTOCOL_ERROR(decode_message(oversized));

    auto wrong_length = hello;
    wrong_length[11] = 1;
    CHECK_PROTOCOL_ERROR(decode_message(wrong_length));

    auto trailing = hello;
    trailing.push_back(0);
    CHECK_PROTOCOL_ERROR(decode_message(trailing));

    CHECK_PROTOCOL_ERROR(make_task({1, 0, 2}));
    CHECK_PROTOCOL_ERROR(make_task({1, 2049, 2}));
    CHECK_PROTOCOL_ERROR(parse_result(Message{MessageType::result, std::vector<std::uint8_t>(41)}));
}

void deterministic_fragmentation() {
    using namespace bom::protocol;
    const auto first = encode_message(make_task({5, 3, 99}));
    const auto second = encode_message(make_shutdown());
    std::vector<std::uint8_t> stream = first;
    stream.insert(stream.end(), second.begin(), second.end());

    FrameDecoder decoder;
    for (const auto byte : stream) {
        const std::array<std::uint8_t, 1> fragment{byte};
        decoder.append(fragment);
    }
    const auto decoded_first = decoder.next();
    CHECK(decoded_first.has_value());
    CHECK(parse_task(*decoded_first).task_id == 5);
    const auto decoded_second = decoder.next();
    CHECK(decoded_second.has_value());
    CHECK(decoded_second->type == MessageType::shutdown);
    CHECK(!decoder.next().has_value());
    CHECK(decoder.buffered_size() == 0);

    FrameDecoder partial;
    partial.append(std::span<const std::uint8_t>(first).first(7));
    CHECK(!partial.next().has_value());
    partial.append(std::span<const std::uint8_t>(first).subspan(7));
    CHECK(partial.next().has_value());
}

}  // namespace

int main() {
    header_and_byte_order();
    round_trips();
    malformed_frames();
    deterministic_fragmentation();
    if (failures == 0) {
        std::cout << "protocol tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
