#include "breakwater/rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using breakwater::Rng;

TEST_CASE("SplitMix64 matches the reference sequence", "[rng]") {
    // Reference values from the published splitmix64.c with state 0.
    Rng rng{0};
    CHECK(rng.next() == 0xE220A8397B1DCDAFULL);
    CHECK(rng.next() == 0x6E789E6AA1B965F4ULL);
}

TEST_CASE("Rng is usable at compile time", "[rng]") {
    constexpr auto first = [] {
        Rng rng{0};
        return rng.next();
    }();
    STATIC_REQUIRE(first == 0xE220A8397B1DCDAFULL);
}

TEST_CASE("below stays in range and covers every value", "[rng]") {
    Rng rng{42};
    std::array<int, 7> seen{};
    for (int i = 0; i < 10'000; ++i) {
        const std::uint32_t v = rng.below(7);
        REQUIRE(v < 7U);
        ++seen.at(v);
    }
    for (const int count : seen) {
        CHECK(count > 0);
    }
}

TEST_CASE("below(1) is always zero", "[rng]") {
    Rng rng{7};
    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng.below(1) == 0U);
    }
}

TEST_CASE("streams are independent of each other's draws", "[rng]") {
    auto terrain = breakwater::make_stream(99, breakwater::Stream::Terrain);
    const auto buildings_before = breakwater::make_stream(99, breakwater::Stream::Buildings);

    for (int i = 0; i < 50; ++i) {
        (void)terrain.next();
    }
    auto buildings_after = breakwater::make_stream(99, breakwater::Stream::Buildings);
    auto copy = buildings_before;
    CHECK(copy.next() == buildings_after.next());
}

TEST_CASE("different streams of one seed differ", "[rng]") {
    auto a = breakwater::make_stream(99, breakwater::Stream::Terrain);
    auto b = breakwater::make_stream(99, breakwater::Stream::Buildings);
    CHECK(a.next() != b.next());
}
