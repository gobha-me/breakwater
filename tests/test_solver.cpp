#include "breakwater/mission.hpp"
#include "breakwater/seeds.hpp"
#include "breakwater/solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

using breakwater::Board;
using breakwater::Direction;
using breakwater::Intent;
using breakwater::Mission;
using breakwater::Outcome;
using breakwater::ShoveCommand;
using breakwater::Tile;
using breakwater::UnitKind;
using breakwater::Verdict;

namespace {

Board flat_board() {
    Board board;
    for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
        board.set_tile(breakwater::tile_coord(i), Tile{.elevation = 8});
    }
    board.advance_tide();
    return board;
}

void add_building(Board& board, breakwater::Coord c, std::uint8_t hp) {
    board.set_tile(c, Tile{.elevation = 8, .building_hp = hp});
}

/// Replays a witness from `start`; true if every command is accepted and the
/// mission is won at the end.
bool wins(Mission start, const std::vector<breakwater::Command>& witness) {
    for (const auto& command : witness) {
        if (!start.apply(command)) {
            return false;
        }
    }
    return start.outcome() == Outcome::Won;
}

/// The last turn: a crawler will raze the only building unless an engineer
/// shoves it aside. Moving cannot save the town; one shove can.
Mission last_stand() {
    Board board = flat_board();
    add_building(board, {3, 6}, 1);
    (void)board.add_unit(UnitKind::Crawler, {3, 5}, 2);
    (void)board.add_unit(UnitKind::Engineer, {2, 5}, 2);
    return Mission::from_board(1, board,
                               {.intents = {Intent{Direction::South}}, .spawns = {}, .turn = breakwater::kTurnCount});
}

} // namespace

TEST_CASE("every witness the solver returns wins on replay", "[solver]") {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        if (!breakwater::is_offered(seed)) {
            continue; // Rejected: the solver could not prove it (seeds.hpp).
        }
        CAPTURE(seed);
        const Mission mission = Mission::generate(seed);
        const auto result = breakwater::solve(mission, {.node_budget = breakwater::kProofBudget});
        REQUIRE(result.verdict == Verdict::Winnable);
        CHECK(wins(mission, result.witness));
    }
}

TEST_CASE("the solver finds a win that needs a shove, and refuses one without", "[solver]") {
    const Mission mission = last_stand();
    const auto result = breakwater::solve(mission);
    REQUIRE(result.verdict == Verdict::Winnable);
    CHECK(wins(mission, result.witness));
    CHECK(std::ranges::any_of(result.witness, [](const auto& c) { return std::holds_alternative<ShoveCommand>(c); }));

    // Without shoves every line loses, and the search says so only because it
    // was complete.
    const auto passive = breakwater::solve(mission, {.node_budget = 1'000'000, .allow_shove = false});
    CHECK(passive.verdict == Verdict::Unwinnable);
    CHECK(passive.witness.empty());
    CHECK(passive.nodes < 1'000'000);
}

TEST_CASE("a forced loss is unwinnable after a complete search", "[solver]") {
    Board board = flat_board();
    add_building(board, {3, 6}, 1);
    (void)board.add_unit(UnitKind::Crawler, {3, 5}, 2);
    (void)board.add_unit(UnitKind::Crawler, {4, 6}, 2);
    (void)board.add_unit(UnitKind::Engineer, {0, 0}, 2); // Too far away to matter.
    const Mission mission = Mission::from_board(
        1, board,
        {.intents = {Intent{Direction::South}, Intent{Direction::West}}, .spawns = {}, .turn = breakwater::kTurnCount});
    const auto result = breakwater::solve(mission);
    CHECK(result.verdict == Verdict::Unwinnable);
    CHECK(result.witness.empty());
}

TEST_CASE("running out of budget is unproven, never unwinnable", "[solver]") {
    const auto result = breakwater::solve(Mission::generate(20), {.node_budget = 1, .allow_shove = true});
    CHECK(result.verdict == Verdict::Unproven);
    CHECK(result.witness.empty());
    CHECK(result.nodes == 2);
}

TEST_CASE("the solver is deterministic", "[solver][determinism]") {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        const auto a = breakwater::solve(Mission::generate(seed));
        const auto b = breakwater::solve(Mission::generate(seed));
        CHECK(a.verdict == b.verdict);
        CHECK(a.witness == b.witness);
        CHECK(a.nodes == b.nodes);
    }
}

TEST_CASE("solving a finished mission answers immediately", "[solver]") {
    Mission won = last_stand();
    REQUIRE(won.apply(ShoveCommand{.engineer = 1, .dir = Direction::East}));
    REQUIRE(won.apply(breakwater::EndTurnCommand{}));
    REQUIRE(won.outcome() == Outcome::Won);
    const auto result = breakwater::solve(won);
    CHECK(result.verdict == Verdict::Winnable);
    CHECK(result.witness.empty());
}
