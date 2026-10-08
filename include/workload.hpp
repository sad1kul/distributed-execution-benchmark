#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bom {

constexpr std::uint32_t kDefaultMaxDimension = 2048;
constexpr std::uint64_t kSeedBXor = 0xA5A5A5A5A5A5A5A5ULL;
constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;

struct Matrix {
    std::uint32_t n;
    std::vector<std::uint32_t> data;
};

struct MatrixC {
    std::uint32_t n;
    std::vector<std::uint64_t> data;
};

struct Inputs {
    Matrix a;
    Matrix b;
};

std::uint64_t splitmix64_next(std::uint64_t& state);
std::size_t checked_square(std::size_t n);
Matrix generate_matrix(
    std::uint32_t n,
    std::uint64_t seed,
    std::uint32_t max_dimension = kDefaultMaxDimension);
Inputs generate_inputs(
    std::uint32_t n,
    std::uint64_t seed,
    std::uint32_t max_dimension = kDefaultMaxDimension);
MatrixC multiply(const Matrix& a, const Matrix& b);
std::uint64_t fnv1a64_byte(std::uint64_t hash, std::uint8_t byte);
std::uint64_t fnv1a64_le64(std::uint64_t hash, std::uint64_t value);
std::uint64_t checksum(const MatrixC& c);

}
