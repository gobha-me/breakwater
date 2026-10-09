#pragma once

#include <cstdint>

namespace breakwater::detail {

inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;

/// Folds the low `byte_count` bytes of `value` into an FNV-1a hash, least
/// significant first, so the result does not depend on host byte order.
inline void fnv_bytes(std::uint64_t& hash, std::uint64_t value, int byte_count) noexcept {
    constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;
    for (int i = 0; i < byte_count; ++i) {
        hash ^= (value >> (8 * i)) & 0xFFU;
        hash *= kFnvPrime;
    }
}

inline void fnv_int(std::uint64_t& hash, int value) noexcept {
    fnv_bytes(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)), 4);
}

} // namespace breakwater::detail
