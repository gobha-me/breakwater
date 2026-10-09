#include "breakwater/board.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using breakwater::Board;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::kBoardSize;
using breakwater::PushOutcome;
using breakwater::UnitKind;

namespace {

/// Finds a building-free tile in the given row; generated boards keep the
/// shore rows (y < 5) free of buildings.
Coord open_tile(const Board& board, int y) {
    for (int x = 0; x < kBoardSize; ++x) {
        if (board.tile({x, y}).building_hp == 0) {
            return {x, y};
        }
    }
    FAIL("no open tile in row");
    return {};
}

int building_count(const Board& board) {
    int count = 0;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            count += board.tile({x, y}).building_hp > 0 ? 1 : 0;
        }
    }
    return count;
}

} // namespace

TEST_CASE("generation is deterministic per seed", "[board][determinism]") {
    CHECK(Board::generate(1234).state_hash() == Board::generate(1234).state_hash());
    CHECK(Board::generate(1234).state_hash() != Board::generate(1235).state_hash());
}

TEST_CASE("generated boards have a town and a rising shore", "[board]") {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const Board board = Board::generate(seed);
        REQUIRE(building_count(board) == 3);
        for (int x = 0; x < kBoardSize; ++x) {
            REQUIRE(board.tile({x, 0}).elevation <= 1);
            REQUIRE(board.tile({x, 0}).building_hp == 0);
        }
    }
}

TEST_CASE("the tide floods tiles below its level", "[board][tide]") {
    Board board = Board::generate(5);
    const Coord shore{0, 0};
    const int elevation = board.tile(shore).elevation;

    while (board.tide() <= elevation) {
        CHECK_FALSE(board.flooded(shore));
        board.advance_tide();
    }
    CHECK(board.flooded(shore));
}

TEST_CASE("push resolves movement and bump damage", "[board][push]") {
    Board board = Board::generate(7);

    SECTION("into open ground moves the unit") {
        const Coord from = open_tile(board, 2);
        const auto id = board.add_unit(UnitKind::Crawler, from, 3);
        const auto dir = from.x + 1 < kBoardSize ? Direction::East : Direction::West;
        CHECK(board.push(id, dir) == PushOutcome::Moved);
        CHECK(board.unit(id).pos == breakwater::step(from, dir));
        CHECK(board.unit(id).hp == 3);
    }

    SECTION("off the board edge bumps without moving") {
        const auto id = board.add_unit(UnitKind::Crawler, {3, 0}, 3);
        CHECK(board.push(id, Direction::North) == PushOutcome::BlockedByEdge);
        CHECK(board.unit(id).pos == Coord{3, 0});
        CHECK(board.unit(id).hp == 2);
    }

    SECTION("into another unit damages both") {
        const auto a = board.add_unit(UnitKind::Crawler, {3, 1}, 3);
        const auto b = board.add_unit(UnitKind::Engineer, {4, 1}, 2);
        CHECK(board.push(a, Direction::East) == PushOutcome::BlockedByUnit);
        CHECK(board.unit(a).pos == Coord{3, 1});
        CHECK(board.unit(a).hp == 2);
        CHECK(board.unit(b).hp == 1);
    }

    SECTION("a unit killed by a bump frees its tile") {
        const auto a = board.add_unit(UnitKind::Crawler, {3, 1}, 1);
        CHECK(board.push(a, Direction::North) == PushOutcome::Moved);
        CHECK(board.push(a, Direction::North) == PushOutcome::BlockedByEdge);
        CHECK_FALSE(board.unit(a).alive());
        CHECK_FALSE(board.unit_at({3, 0}).has_value());
        CHECK(board.push(a, Direction::South) == PushOutcome::NoEffect);
    }
}

TEST_CASE("push into a building damages unit and building", "[board][push]") {
    const Board fresh = Board::generate(11);
    for (int y = 1; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            if (fresh.tile({x, y}).building_hp == 0 || fresh.tile({x, y - 1}).building_hp != 0) {
                continue;
            }
            Board board = fresh;
            const auto id = board.add_unit(UnitKind::Crawler, {x, y - 1}, 3);
            const int building_hp = board.tile({x, y}).building_hp;
            CHECK(board.push(id, Direction::South) == PushOutcome::BlockedByBuilding);
            CHECK(board.unit(id).hp == 2);
            CHECK(board.tile({x, y}).building_hp == building_hp - 1);
            return;
        }
    }
    FAIL("seed 11 has no building with an open tile to its north");
}

TEST_CASE("engineers drown in flooded tiles; crawlers swim", "[board][push][tide]") {
    Board board = Board::generate(3);
    for (int i = 0; i < 10; ++i) {
        board.advance_tide(); // Floods the whole board: max elevation is 4.
    }
    REQUIRE(board.flooded({2, 2}));

    const auto engineer = board.add_unit(UnitKind::Engineer, {2, 1}, 3);
    const auto crawler = board.add_unit(UnitKind::Crawler, {5, 1}, 3);

    CHECK(board.push(engineer, Direction::South) == PushOutcome::Drowned);
    CHECK_FALSE(board.unit(engineer).alive());
    CHECK(board.push(crawler, Direction::South) == PushOutcome::Moved);
    CHECK(board.unit(crawler).alive());
}

TEST_CASE("state hash tracks every mutation", "[board][determinism]") {
    Board board = Board::generate(9);
    const auto initial = board.state_hash();

    board.advance_tide();
    const auto after_tide = board.state_hash();
    CHECK(after_tide != initial);

    const auto id = board.add_unit(UnitKind::Crawler, {0, 1}, 2);
    const auto after_add = board.state_hash();
    CHECK(after_add != after_tide);

    (void)board.push(id, Direction::North);
    CHECK(board.state_hash() != after_add);
}
