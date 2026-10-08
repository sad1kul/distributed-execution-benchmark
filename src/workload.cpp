#include "workload.hpp"

#include <limits>
#include <stdexcept>

namespace bom {

namespace {

template <typename Element>
void check_byte_size(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Element)) {
        throw std::overflow_error("matrix byte size overflows size_t");
    }
}

void validate_dimension(std::uint32_t n, std::uint32_t max_dimension) {
    if (n == 0 || n > max_dimension) {
        throw std::invalid_argument("matrix dimension is outside the permitted range");
    }
}

}  // namespace

std::uint64_t splitmix64_next(std::uint64_t& state) {
    // Unsigned overflow supplies the specified modulo-2^64 arithmetic.
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

std::size_t checked_square(std::size_t n) {
    if (n != 0 && n > std::numeric_limits<std::size_t>::max() / n) {
        throw std::overflow_error("matrix element count overflows size_t");
    }
    return n * n;
}

Matrix generate_matrix(std::uint32_t n, std::uint64_t seed, std::uint32_t max_dimension) {
    validate_dimension(n, max_dimension);
    const std::size_t count = checked_square(static_cast<std::size_t>(n));
    check_byte_size<std::uint32_t>(count);

    Matrix result{n, std::vector<std::uint32_t>(count)};
    std::uint64_t state = seed;
    for (std::size_t index = 0; index < count; ++index) {
        result.data[index] = static_cast<std::uint32_t>(splitmix64_next(state) % 16U);
    }
    return result;
}

Inputs generate_inputs(std::uint32_t n, std::uint64_t seed, std::uint32_t max_dimension) {
    // Keeping the XOR here makes the two independent streams visible at the API boundary.
    return Inputs{
        generate_matrix(n, seed, max_dimension),
        generate_matrix(n, seed ^ kSeedBXor, max_dimension),
    };
}

MatrixC multiply(const Matrix& a, const Matrix& b) {
    if (a.n != b.n) {
        throw std::invalid_argument("matrix dimensions do not match");
    }

    const std::size_t n = a.n;
    const std::size_t count = checked_square(n);
    if (a.data.size() != count || b.data.size() != count) {
        throw std::invalid_argument("matrix storage does not match its dimension");
    }
    check_byte_size<std::uint64_t>(count);

    MatrixC result{a.n, std::vector<std::uint64_t>(count, 0)};
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            std::uint64_t total = 0;
            for (std::size_t k = 0; k < n; ++k) {
                const std::uint64_t left = static_cast<std::uint64_t>(a.data[i * n + k]);
                const std::uint64_t right = static_cast<std::uint64_t>(b.data[k * n + j]);
                total += left * right;
            }
            result.data[i * n + j] = total;
        }
    }
    return result;
}

std::uint64_t fnv1a64_byte(std::uint64_t hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
    return hash;
}

std::uint64_t fnv1a64_le64(std::uint64_t hash, std::uint64_t value) {
    // Explicit extraction keeps the serialized form independent of native endianness.
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        const auto byte = static_cast<std::uint8_t>((value >> shift) & 0xffU);
        hash = fnv1a64_byte(hash, byte);
    }
    return hash;
}

std::uint64_t checksum(const MatrixC& c) {
    const std::size_t count = checked_square(static_cast<std::size_t>(c.n));
    if (c.data.size() != count) {
        throw std::invalid_argument("matrix storage does not match its dimension");
    }
    check_byte_size<std::uint64_t>(count);

    std::uint64_t hash = fnv1a64_le64(kFnvOffsetBasis, c.n);
    for (const std::uint64_t value : c.data) {
        hash = fnv1a64_le64(hash, value);
    }
    return hash;
}

}
