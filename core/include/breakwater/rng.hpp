#pragma once

#include <cassert>
#include <cstdint>

namespace breakwater {

/// SplitMix64. Tiny and fully specified, so a seed yields the same sequence on
/// every compiler and platform; standard-library distributions do not.
class Rng {
public:
    constexpr explicit Rng(std::uint64_t seed) noexcept : state_{seed} {}

    /// An independent stream for one subsystem. Draws in one stream never shift
    /// another's sequence, so adding randomness to one system keeps old seeds'
    /// output stable everywhere else.
    [[nodiscard]] static constexpr Rng stream(std::uint64_t seed, std::uint64_t stream_id) noexcept {
        Rng mixer{seed ^ (stream_id * 0xD1B54A32D192ED03ULL)};
        return Rng{mixer.next()};
    }

    constexpr std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    /// Uniform integer in [0, bound), without modulo bias.
    constexpr std::uint32_t below(std::uint32_t bound) noexcept {
        assert(bound > 0);
        const auto wide_bound = static_cast<std::uint64_t>(bound);
        // 2^64 mod bound: rejecting draws below it leaves a range that is an
        // exact multiple of bound.
        const std::uint64_t threshold = (0 - wide_bound) % wide_bound;
        for (;;) {
            const std::uint64_t draw = next();
            if (draw >= threshold) {
                return static_cast<std::uint32_t>(draw % wide_bound);
            }
        }
    }

private:
    std::uint64_t state_;
};

/// Stream ids are part of the save/replay format: append, never renumber.
enum class Stream : std::uint8_t {
    Terrain = 1,
    Buildings = 2,
    Deploy = 3,
    Spawns = 4,
};

[[nodiscard]] constexpr Rng make_stream(std::uint64_t seed, Stream stream) noexcept {
    return Rng::stream(seed, static_cast<std::uint64_t>(stream));
}

/// One subsystem's stream for one turn (turn 0 is mission setup). Derived, not
/// stored, so a position needs no generator state and the solver can hash it
/// cheaply (docs/RULES.md, "RNG streams").
[[nodiscard]] constexpr Rng make_turn_stream(std::uint64_t seed, Stream stream, int turn) noexcept {
    Rng base = make_stream(seed, stream);
    return Rng::stream(base.next(), static_cast<std::uint64_t>(turn));
}

} // namespace breakwater
