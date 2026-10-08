#include "workload.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition    \
                      << '\n';                                                            \
            ++failures;                                                                   \
        }                                                                                 \
    } while (false)

template <typename Exception, typename Function>
void check_throws(Function function, const char* expression, const char* file, int line) {
    try {
        function();
        std::cerr << file << ':' << line << ": expected exception from " << expression << '\n';
        ++failures;
    } catch (const Exception&) {
    } catch (...) {
        std::cerr << file << ':' << line << ": wrong exception from " << expression << '\n';
        ++failures;
    }
}

#define CHECK_THROWS_AS(expression, exception_type) \
    check_throws<exception_type>([&] { static_cast<void>(expression); }, #expression, __FILE__, __LINE__)

struct GoldenCase {
    std::uint32_t n;
    std::uint64_t seed;
    std::vector<std::uint32_t> a;
    std::vector<std::uint32_t> b;
    std::vector<std::uint64_t> c;
    std::uint64_t checksum;
};

#include "golden_vectors.inc"

const GoldenCase& golden(std::uint32_t n, std::uint64_t seed) {
    for (const auto& item : kGoldenCases) {
        if (item.n == n && item.seed == seed) {
            return item;
        }
    }
    throw std::logic_error("test requested a missing golden case");
}

void t01_golden_sequences() {
    const std::vector<std::pair<std::uint64_t, std::vector<std::uint64_t>>> anchors = {
        {0ULL, {0xe220a8397b1dcdafULL, 0x6e789e6aa1b965f4ULL, 0x06c45d188009454fULL}},
        {1ULL, {0x910a2dec89025cc1ULL, 0xbeeb8da1658eec67ULL, 0xf893a2eefb32555eULL}},
        {42ULL, {0xbdd732262feb6e95ULL, 0x28efe333b266f103ULL, 0x47526757130f9f52ULL}},
        {0xdeadbeefcafef00dULL,
         {0x901d4f652fb472cbULL, 0xa7ce246440f74527ULL, 0x19b40bbbb9380d34ULL}},
    };
    for (const auto& [seed, expected] : anchors) {
        std::uint64_t state = seed;
        for (const auto value : expected) {
            CHECK(bom::splitmix64_next(state) == value);
        }
    }
}

void t02_repeatable() {
    std::uint64_t first = 42;
    std::uint64_t second = 42;
    for (int index = 0; index < 100; ++index) {
        CHECK(bom::splitmix64_next(first) == bom::splitmix64_next(second));
    }
}

void t03_different_seeds() {
    std::uint64_t zero = 0;
    std::uint64_t one = 1;
    CHECK(bom::splitmix64_next(zero) == 0xe220a8397b1dcdafULL);
    CHECK(bom::splitmix64_next(one) == 0x910a2dec89025cc1ULL);
    CHECK(zero != one);
}

void check_generation_case(std::uint32_t n) {
    const auto& expected = golden(n, 0);
    const auto inputs = bom::generate_inputs(n, 0);
    CHECK(inputs.a.data == expected.a);
    CHECK(inputs.b.data == expected.b);
}

void t04_to_t06_golden_generation() {
    check_generation_case(1);
    check_generation_case(2);
    check_generation_case(3);
}

void t07_same_input_identical() {
    const auto first = bom::generate_inputs(4, 12345);
    const auto second = bom::generate_inputs(4, 12345);
    CHECK(first.a.data == second.a.data);
    CHECK(first.b.data == second.b.data);
}

void t08_independent_b_stream() {
    const std::uint32_t n = 3;
    const std::uint64_t seed = 17;
    const auto inputs = bom::generate_inputs(n, seed);
    const auto expected_b = bom::generate_matrix(n, seed ^ bom::kSeedBXor);
    CHECK(inputs.b.data == expected_b.data);

    std::uint64_t continued_state = seed;
    for (std::uint32_t index = 0; index < n * n; ++index) {
        static_cast<void>(bom::splitmix64_next(continued_state));
    }
    std::vector<std::uint32_t> continued;
    for (std::uint32_t index = 0; index < n * n; ++index) {
        continued.push_back(static_cast<std::uint32_t>(bom::splitmix64_next(continued_state) % 16U));
    }
    CHECK(inputs.b.data != continued);
}

void t09_element_range() {
    const auto inputs = bom::generate_inputs(16, 7);
    for (const auto value : inputs.a.data) {
        CHECK(value < 16U);
    }
    for (const auto value : inputs.b.data) {
        CHECK(value < 16U);
    }
}

void t10_to_t12_hand_products() {
    const auto one = bom::multiply({1, {7}}, {1, {6}});
    CHECK(one.data == std::vector<std::uint64_t>{42});

    const auto two = bom::multiply({2, {1, 2, 3, 4}}, {2, {5, 6, 7, 8}});
    CHECK(two.data == (std::vector<std::uint64_t>{19, 22, 43, 50}));

    const auto three = bom::multiply(
        {3, {1, 2, 3, 4, 5, 6, 7, 8, 9}}, {3, {9, 8, 7, 6, 5, 4, 3, 2, 1}});
    CHECK(three.data == (std::vector<std::uint64_t>{30, 24, 18, 84, 69, 54, 138, 114, 90}));
}

void t13_identity() {
    const bom::Matrix value{3, {2, 4, 6, 8, 10, 12, 14, 1, 3}};
    const bom::Matrix identity{3, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    CHECK(bom::multiply(value, identity).data ==
          (std::vector<std::uint64_t>{2, 4, 6, 8, 10, 12, 14, 1, 3}));
}

void t14_zero() {
    const bom::Matrix value{2, {1, 2, 3, 4}};
    const bom::Matrix zero{2, {0, 0, 0, 0}};
    CHECK(bom::multiply(value, zero).data == (std::vector<std::uint64_t>{0, 0, 0, 0}));
}

void t15_golden_workloads() {
    for (const auto& expected : kGoldenCases) {
        const auto inputs = bom::generate_inputs(expected.n, expected.seed);
        CHECK(bom::multiply(inputs.a, inputs.b).data == expected.c);
    }
}

void t16_input_immutability() {
    auto inputs = bom::generate_inputs(3, 1);
    const auto a_before = inputs.a.data;
    const auto b_before = inputs.b.data;
    static_cast<void>(bom::multiply(inputs.a, inputs.b));
    CHECK(inputs.a.data == a_before);
    CHECK(inputs.b.data == b_before);
}

void t17_wraparound_product() {
    constexpr std::uint32_t f = 0xffffffffU;
    const auto result = bom::multiply({2, {f, f, 0, 0}}, {2, {f, 0, f, 0}});
    CHECK(result.data == (std::vector<std::uint64_t>{0xfffffffc00000002ULL, 0, 0, 0}));
}

void t18_golden_checksums() {
    for (const auto& expected : kGoldenCases) {
        CHECK(bom::checksum({expected.n, expected.c}) == expected.checksum);
    }
}

void t19_fnv_bytes() {
    CHECK(bom::kFnvOffsetBasis == 0xcbf29ce484222325ULL);
    CHECK(bom::fnv1a64_byte(bom::kFnvOffsetBasis, static_cast<std::uint8_t>('a')) ==
          0xaf63dc4c8601ec8cULL);
    std::uint64_t hash = bom::kFnvOffsetBasis;
    for (const unsigned char byte : std::string("foobar")) {
        hash = bom::fnv1a64_byte(hash, byte);
    }
    CHECK(hash == 0x85944171f73967e8ULL);
}

void t20_dimension_little_endian() {
    CHECK(bom::fnv1a64_le64(bom::kFnvOffsetBasis, 3) == 0xc7c2bf3b330983e6ULL);
    CHECK(bom::fnv1a64_le64(bom::kFnvOffsetBasis, 0x0102030405060708ULL) ==
          0x0c6d4496e17859d5ULL);
}

void t21_element_byte_order() {
    std::uint64_t expected = bom::fnv1a64_le64(bom::kFnvOffsetBasis, 1);
    const std::uint64_t value = 0x0102030405060708ULL;
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        expected = bom::fnv1a64_byte(expected, static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
    CHECK(bom::checksum({1, {value}}) == expected);
}

void t22_wrap_checksum() {
    CHECK(bom::checksum({2, {0xfffffffc00000002ULL, 0, 0, 0}}) == 0x5c82b77744bd5f52ULL);
}

void t23_checksum_repeatable() {
    const bom::MatrixC value{2, {19, 22, 43, 50}};
    CHECK(bom::checksum(value) == bom::checksum(value));
}

void t24_to_t27_generation_validation() {
    CHECK_THROWS_AS(bom::generate_matrix(0, 0), std::invalid_argument);
    CHECK_THROWS_AS(bom::generate_matrix(2049, 0), std::invalid_argument);
    CHECK_THROWS_AS(bom::generate_matrix(33, 0, 32), std::invalid_argument);
    CHECK_THROWS_AS(bom::checked_square(std::numeric_limits<std::size_t>::max()),
                    std::overflow_error);
    CHECK(bom::checked_square(1000) == 1000000);
}

void t28_dimension_mismatch() {
    CHECK_THROWS_AS(bom::multiply({1, {1}}, {2, {1, 0, 0, 1}}), std::invalid_argument);
}

void t29_storage_mismatch() {
    CHECK_THROWS_AS(bom::multiply({2, {1}}, {2, {1, 0, 0, 1}}), std::invalid_argument);
    CHECK_THROWS_AS(bom::checksum({2, {1}}), std::invalid_argument);
    CHECK_THROWS_AS(bom::multiply({0, {}}, {0, {}}), std::invalid_argument);
    CHECK_THROWS_AS(bom::checksum({0, {}}), std::invalid_argument);
}

void t30_boundaries() {
    CHECK(bom::generate_matrix(1, 0).data.size() == 1);
    CHECK(bom::generate_matrix(4, 0, 4).data.size() == 16);
}

void t31_stability() {
    const auto& expected = golden(4, 12345);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto inputs = bom::generate_inputs(4, 12345);
        CHECK(bom::checksum(bom::multiply(inputs.a, inputs.b)) == expected.checksum);
    }
}

}  // namespace

int main() {
    t01_golden_sequences();
    t02_repeatable();
    t03_different_seeds();
    t04_to_t06_golden_generation();
    t07_same_input_identical();
    t08_independent_b_stream();
    t09_element_range();
    t10_to_t12_hand_products();
    t13_identity();
    t14_zero();
    t15_golden_workloads();
    t16_input_immutability();
    t17_wraparound_product();
    t18_golden_checksums();
    t19_fnv_bytes();
    t20_dimension_little_endian();
    t21_element_byte_order();
    t22_wrap_checksum();
    t23_checksum_repeatable();
    t24_to_t27_generation_validation();
    t28_dimension_mismatch();
    t29_storage_mismatch();
    t30_boundaries();
    t31_stability();

    if (failures == 0) {
        std::cout << "T01-T31 passed\n";
    }
    return failures == 0 ? 0 : 1;
}
