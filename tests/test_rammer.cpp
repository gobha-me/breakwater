#include "breakwater/mission.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

using breakwater::Board;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::Event;
using breakwater::EventKind;
using breakwater::Intent;
using breakwater::Mission;
using breakwater::Outcome;
using breakwater::ShoveCommand;
using breakwater::Tile;
using breakwater::UnitId;
using breakwater::UnitKind;

namespace {

/// High, dry ground everywhere: elevation 8 at tide 1, so no tide within a
/// test reaches it. Scenarios add water and buildings.
Board flat_board() {
    Board board;
    for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
        board.set_tile(breakwater::tile_coord(i), Tile{.elevation = 8});
    }
    board.advance_tide();
    return board;
}

/// A hand-built mission with no spawns, so nothing arrives uninvited.
Mission scenario(const Board& board, std::vector<Intent> intents = {}) {
    return Mission::from_board(1, board, {.intents = std::move(intents), .spawns = {}, .rammers = {}});
}

void add_water(Board& board, Coord c) { board.set_tile(c, Tile{.elevation = 0}); }

void add_building(Board& board, Coord c, std::uint8_t hp = 2) {
    board.set_tile(c, Tile{.elevation = 8, .building_hp = hp});
}

std::vector<Event> end_turn(Mission& mission) {
    (void)mission.take_events();
    REQUIRE(mission.apply(EndTurnCommand{}));
    return mission.take_events();
}

std::vector<Event> of_kind(const std::vector<Event>& events, EventKind kind) {
    std::vector<Event> out;
    std::ranges::copy_if(events, std::back_inserter(out), [kind](const Event& e) { return e.kind == kind; });
    return out;
}

} // namespace

TEST_CASE("rammer intent follows the ranking key in docs/RULES.md", "[rammer][plan]") {
    SECTION("a ram that drowns an engineer beats a nearer bump") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {1, 2}, 2);
        (void)board.add_unit(UnitKind::Engineer, {0, 2}, 2); // West, against the edge: 1.
        (void)board.add_unit(UnitKind::Engineer, {3, 2}, 2); // North from (3, 3), into water: 2.
        add_water(board, {3, 1});
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(r).pos == Coord{3, 3});
        CHECK(mission.intent(r) == Intent{Direction::North});
    }

    SECTION("an engineer rammed into a cottage beats a bump against the edge") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {1, 2}, 2);
        (void)board.add_unit(UnitKind::Engineer, {0, 2}, 2); // West, against the edge: 1.
        (void)board.add_unit(UnitKind::Engineer, {2, 3}, 2); // South from (2, 2), into the cottage: 2.
        add_building(board, {2, 4});
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(r).pos == Coord{2, 2});
        CHECK(mission.intent(r) == Intent{Direction::South});
    }

    SECTION("shifting an engineer off a guard post next to a cottage counts") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {0, 4}, 2);
        (void)board.add_unit(UnitKind::Engineer, {3, 4}, 2);
        add_building(board, {3, 5});
        Mission mission = scenario(board);
        (void)end_turn(mission);
        // From (2, 4), East onto open ground: 1, in 2 steps. Ramming the
        // cottage itself from (2, 5) is also 1, but takes 3.
        CHECK(mission.board().unit(r).pos == Coord{2, 4});
        CHECK(mission.intent(r) == Intent{Direction::East});
    }

    SECTION("a crawler rammed into a cottage counts; one on open ground is never rammed") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {3, 3}, 2);
        (void)board.add_unit(UnitKind::Crawler, {4, 3}, 2);
        (void)board.add_unit(UnitKind::Engineer, {0, 7}, 2);
        add_building(board, {5, 3});
        Mission into = scenario(board);
        (void)end_turn(into);
        CHECK(into.intent(r) == Intent{Direction::East});

        Board open = board;
        open.set_tile({5, 3}, Tile{.elevation = 8});
        Mission nothing = scenario(open);
        (void)end_turn(nothing);
        CHECK_FALSE(nothing.intent(r).has_value());
    }

    SECTION("equal harm: fewest steps, then row, column and direction") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {1, 1}, 2);
        (void)board.add_unit(UnitKind::Engineer, {0, 1}, 2); // West against the edge, 0 steps.
        (void)board.add_unit(UnitKind::Engineer, {1, 0}, 2); // North against the edge, 0 steps.
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(r).pos == Coord{1, 1});
        CHECK(mission.intent(r) == Intent{Direction::North});
    }

    SECTION("its own start tile does not block the ram") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        (void)board.add_unit(UnitKind::Engineer, {2, 3}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        // From (2, 4), North would land on (2, 2), which the Rammer leaves:
        // open ground, no harm. With nothing to harm and no town, it holds.
        CHECK(mission.board().unit(r).pos == Coord{2, 2});
        CHECK_FALSE(mission.intent(r).has_value());
    }

    SECTION("nothing to harm in reach: march on the town and declare nothing") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {0, 0}, 2);
        (void)board.add_unit(UnitKind::Engineer, {7, 0}, 2);
        add_building(board, {0, 7});
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(r).pos == Coord{0, 3});
        CHECK_FALSE(mission.intent(r).has_value());
    }
}

TEST_CASE("a ram pushes whatever is there by the shove rules", "[rammer][resolve]") {
    SECTION("an engineer rammed into water drowns") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 1}, 2);
        (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
        add_water(board, {2, 0});
        Mission mission = scenario(board, {Intent{Direction::North}});
        const auto events = end_turn(mission);
        CHECK_FALSE(mission.board().unit(e).alive());
        CHECK(of_kind(events, EventKind::AttackResolved) ==
              std::vector<Event>{
                  {.kind = EventKind::AttackResolved, .unit = r, .from = {2, 2}, .to = {2, 1}, .value = 0}});
        CHECK(of_kind(events, EventKind::UnitShoved) ==
              std::vector<Event>{{.kind = EventKind::UnitShoved, .unit = e, .from = {2, 1}, .to = {2, 0}}});
        CHECK(of_kind(events, EventKind::UnitDrowned).size() == 1);
        CHECK(of_kind(events, EventKind::UnitDamaged).empty()); // A push, not a strike.
    }

    SECTION("an engineer rammed onto open ground moves, unhurt") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 3}, 2);
        Mission mission = scenario(board, {Intent{Direction::South}});
        (void)end_turn(mission);
        CHECK(mission.board().unit(e).pos == Coord{2, 4});
        CHECK(mission.board().unit(e).hp == 2);
    }

    SECTION("an engineer rammed into a building: each takes a bump") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {3, 2}, 2);
        add_building(board, {4, 2});
        Mission mission = scenario(board, {Intent{Direction::East}});
        (void)end_turn(mission);
        CHECK(mission.board().unit(e).pos == Coord{3, 2});
        CHECK(mission.board().unit(e).hp == 1);
        CHECK(mission.board().tile({4, 2}).building_hp == 1);
    }

    SECTION("an engineer rammed against the edge takes a bump") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {1, 5}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {0, 5}, 2);
        Mission mission = scenario(board, {Intent{Direction::West}});
        (void)end_turn(mission);
        CHECK(mission.board().unit(e).hp == 1);
    }

    SECTION("a building rammed directly takes a bump") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
        add_building(board, {2, 3});
        Mission mission = scenario(board, {Intent{Direction::South}});
        const auto events = end_turn(mission);
        CHECK(mission.board().tile({2, 3}).building_hp == 1);
        CHECK(of_kind(events, EventKind::BuildingDamaged) ==
              std::vector<Event>{
                  {.kind = EventKind::BuildingDamaged, .unit = r, .from = {2, 2}, .to = {2, 3}, .value = 1}});
    }

    SECTION("a rammed crawler strikes from where it lands, if it acts later") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto c = board.add_unit(UnitKind::Crawler, {2, 1}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {3, 0}, 2);
        (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
        add_building(board, {3, 1});
        Mission mission = scenario(board, {Intent{Direction::North}, Intent{Direction::East}});
        const auto events = end_turn(mission);
        CHECK(of_kind(events, EventKind::UnitShoved) ==
              std::vector<Event>{{.kind = EventKind::UnitShoved, .unit = c, .from = {2, 1}, .to = {2, 0}}});
        CHECK_FALSE(mission.board().unit(e).alive());          // Struck at (3, 0) instead.
        CHECK(mission.board().tile({3, 1}).building_hp == 2); // The building it aimed at is spared.
    }

    SECTION("a dodged ram hits an empty tile and does nothing") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 1}, 2);
        add_water(board, {2, 0});
        Mission mission = scenario(board, {Intent{Direction::North}});
        REQUIRE(mission.apply(breakwater::MoveCommand{.engineer = e, .to = {4, 1}}));
        const auto events = end_turn(mission);
        CHECK(mission.board().unit(e).hp == 2);
        CHECK(of_kind(events, EventKind::AttackResolved).size() == 1);
        CHECK(of_kind(events, EventKind::UnitShoved).empty());
    }

    SECTION("a Rammer shoved during the player phase rams from where it stands") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {1, 2}, 2);
        const auto other = board.add_unit(UnitKind::Engineer, {3, 1}, 2);
        add_water(board, {3, 0});
        Mission mission = scenario(board, {Intent{Direction::North}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::East}));
        REQUIRE(mission.board().unit(r).pos == Coord{3, 2});
        (void)end_turn(mission);
        CHECK_FALSE(mission.board().unit(other).alive()); // Rammed from (3, 2) into the water.
    }

    SECTION("a dead Rammer does not ram") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 0}, 1);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 1}, 2);
        Mission mission = scenario(board, {Intent{Direction::South}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        REQUIRE_FALSE(mission.board().unit(r).alive());
        const auto events = end_turn(mission);
        CHECK(of_kind(events, EventKind::AttackResolved).empty());
        CHECK(mission.board().unit(e).pos == Coord{2, 1});
    }

    SECTION("a ram that drowns the last engineer ends the mission at once") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Rammer, {2, 2}, 2);
        (void)board.add_unit(UnitKind::Engineer, {2, 1}, 2);
        add_water(board, {2, 0});
        Mission mission = scenario(board, {Intent{Direction::North}});
        const auto events = end_turn(mission);
        CHECK(mission.outcome() == Outcome::Lost);
        CHECK(of_kind(events, EventKind::TideRose).empty());
    }
}

TEST_CASE("Rammers come ashore by the rammer table", "[rammer][spawn]") {
    SECTION("generated missions: the turn-1 spawn is a Rammer, every other spawn a Crawler") {
        for (std::uint64_t seed = 0; seed < 20; ++seed) {
            Mission mission = Mission::generate(seed);
            while (mission.outcome() == Outcome::InProgress) {
                REQUIRE(mission.apply(EndTurnCommand{}));
            }
            const auto& b = mission.board();
            for (UnitId id = breakwater::kEngineerCount; id < b.unit_count(); ++id) {
                const auto expected = id == breakwater::kEngineerCount + 2 ? UnitKind::Rammer : UnitKind::Crawler;
                CHECK(b.unit(id).kind == expected);
            }
        }
    }

    SECTION("the table is part of the state hash") {
        const Board board = flat_board();
        const Mission with = Mission::from_board(1, board, {.intents = {}, .spawns = {}, .rammers = {0, 1, 0, 0, 0}});
        const Mission without = Mission::from_board(1, board, {.intents = {}, .spawns = {}, .rammers = {}});
        CHECK(with.state_hash() != without.state_hash());
    }

    SECTION("a Rammer never drowns: it swims like a crawler") {
        Board board = flat_board();
        const auto r = board.add_unit(UnitKind::Rammer, {2, 1}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 2}, 2);
        add_water(board, {2, 0});
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK(mission.board().unit(r).pos == Coord{2, 0});
        CHECK(mission.board().unit(r).hp == 2);
    }
}
