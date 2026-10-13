#include "breakwater/mission.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

using breakwater::ActionState;
using breakwater::Board;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::Event;
using breakwater::EventKind;
using breakwater::Intent;
using breakwater::kBoardSize;
using breakwater::Mission;
using breakwater::MoveCommand;
using breakwater::Rejection;
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
    return Mission::from_board(1, board, {.intents = std::move(intents), .spawns = {}});
}

void add_water(Board& board, Coord c) { board.set_tile(c, Tile{.elevation = 0}); }

void add_building(Board& board, Coord c, std::uint8_t hp) {
    board.set_tile(c, Tile{.elevation = 8, .building_hp = hp});
}

/// Applies a command that must be refused, and checks the refusal changed
/// nothing: not the state hash, not the event log.
void require_refused(Mission& mission, const breakwater::Command& command, Rejection expected) {
    const auto before = mission.state_hash();
    const auto result = mission.apply(command);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == expected);
    CHECK(mission.state_hash() == before);
    CHECK(mission.take_events().empty());
}

} // namespace

TEST_CASE("missions are deterministic per seed", "[mission][determinism]") {
    CHECK(Mission::generate(1234).state_hash() == Mission::generate(1234).state_hash());
    CHECK(Mission::generate(1234).state_hash() != Mission::generate(1235).state_hash());
    Mission mission = Mission::generate(1234);
    CHECK(mission.take_events().empty());
}

TEST_CASE("deployment puts three engineers on dry ground in row 4", "[mission][deploy]") {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const Mission mission = Mission::generate(seed);
        const Board& board = mission.board();
        REQUIRE(board.unit_count() == breakwater::kEngineerCount + breakwater::kSpawnTable.front());
        REQUIRE(board.tide() == breakwater::kStartTide);
        REQUIRE(mission.turn() == 1);

        std::set<int> columns;
        for (UnitId id = 0; id < breakwater::kEngineerCount; ++id) {
            const auto& u = board.unit(id);
            REQUIRE(u.kind == UnitKind::Engineer);
            REQUIRE(u.hp == breakwater::kUnitHp);
            REQUIRE(u.pos.y == breakwater::kDeployRow);
            REQUIRE_FALSE(board.flooded(u.pos));
            REQUIRE(board.tile(u.pos).building_hp == 0);
            REQUIRE(mission.action_state(id) == ActionState::Ready);
            columns.insert(u.pos.x);
        }
        REQUIRE(columns.size() == 3);
    }
}

TEST_CASE("engineers reach dry, empty tiles within two steps", "[mission][move]") {
    SECTION("open ground: every tile within two steps") {
        Board board = flat_board();
        const auto e = board.add_unit(UnitKind::Engineer, {3, 3}, 2);
        const Mission mission = scenario(board);
        CHECK(mission.reachable(e).size() == 12);
    }

    SECTION("a corner limits the reach to what is on the board") {
        Board board = flat_board();
        const auto e = board.add_unit(UnitKind::Engineer, {0, 0}, 2);
        const Mission mission = scenario(board);
        CHECK(mission.reachable(e) == std::vector<Coord>{{1, 0}, {2, 0}, {0, 1}, {1, 1}, {0, 2}});
    }

    SECTION("water, buildings and units block, and so do the tiles behind them") {
        Board board = flat_board();
        const auto e = board.add_unit(UnitKind::Engineer, {3, 3}, 2);
        add_water(board, {3, 2});
        add_building(board, {4, 3}, 2);
        (void)board.add_unit(UnitKind::Crawler, {2, 3}, 2);
        const Mission mission = scenario(board);
        CHECK(mission.reachable(e) == std::vector<Coord>{{2, 4}, {3, 4}, {4, 4}, {3, 5}});
    }

    SECTION("a crawler has no engineer moves") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        const Mission mission = scenario(board);
        CHECK(mission.reachable(c).empty());
        CHECK(mission.reachable(99).empty());
    }
}

TEST_CASE("an engineer moves, then may shove, then is done", "[mission][move][shove]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {3, 3}, 2);
    const auto c = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
    Mission mission = scenario(board);

    REQUIRE(mission.apply(MoveCommand{.engineer = e, .to = {3, 2}}));
    CHECK(mission.board().unit(e).pos == Coord{3, 2});
    CHECK(mission.action_state(e) == ActionState::Moved);
    CHECK(mission.take_events() ==
          std::vector<Event>{{.kind = EventKind::UnitMoved, .unit = e, .from = {3, 3}, .to = {3, 2}}});
    CHECK(mission.reachable(e).empty());
    require_refused(mission, MoveCommand{.engineer = e, .to = {3, 3}}, Rejection::AlreadyMoved);

    REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
    CHECK(mission.board().unit(c).pos == Coord{3, 0});
    CHECK(mission.action_state(e) == ActionState::Done);
    CHECK(mission.take_events() ==
          std::vector<Event>{{.kind = EventKind::UnitShoved, .unit = c, .from = {3, 1}, .to = {3, 0}}});

    require_refused(mission, ShoveCommand{.engineer = e, .dir = Direction::North}, Rejection::AlreadyActed);
    require_refused(mission, MoveCommand{.engineer = e, .to = {3, 3}}, Rejection::AlreadyActed);
}

TEST_CASE("a shove resolves through Board::push and reports each effect", "[mission][shove]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {3, 2}, 2);

    SECTION("into the board edge: damage, no movement") {
        board.place(e, {3, 1});
        const auto c = board.add_unit(UnitKind::Crawler, {3, 0}, 2);
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK(mission.board().unit(c).hp == 1);
        CHECK(mission.take_events() ==
              std::vector<Event>{
                  {.kind = EventKind::UnitShoved, .unit = c, .from = {3, 0}, .to = {3, 0}},
                  {.kind = EventKind::UnitDamaged, .unit = c, .from = {3, 0}, .to = {3, 0}, .value = 1},
              });
    }

    SECTION("a bump that kills reports the death") {
        board.place(e, {3, 1});
        const auto c = board.add_unit(UnitKind::Crawler, {3, 0}, 1);
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK_FALSE(mission.board().unit(c).alive());
        const auto events = mission.take_events();
        REQUIRE(events.size() == 3);
        CHECK(events.at(1) == Event{.kind = EventKind::UnitDamaged, .unit = c, .from = {3, 0}, .to = {3, 0}});
        CHECK(events.at(2) == Event{.kind = EventKind::UnitDied, .unit = c, .from = {3, 0}, .to = {3, 0}});
    }

    SECTION("into another unit: both take damage, victim first") {
        const auto victim = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
        const auto blocker = board.add_unit(UnitKind::Crawler, {3, 0}, 2);
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK(mission.take_events() ==
              std::vector<Event>{
                  {.kind = EventKind::UnitShoved, .unit = victim, .from = {3, 1}, .to = {3, 1}},
                  {.kind = EventKind::UnitDamaged, .unit = victim, .from = {3, 1}, .to = {3, 1}, .value = 1},
                  {.kind = EventKind::UnitDamaged, .unit = blocker, .from = {3, 0}, .to = {3, 0}, .value = 1},
              });
    }

    SECTION("into a building: the unit and the building take damage") {
        const auto c = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
        add_building(board, {3, 0}, 2);
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK(mission.board().tile({3, 0}).building_hp == 1);
        CHECK(mission.take_events() ==
              std::vector<Event>{
                  {.kind = EventKind::UnitShoved, .unit = c, .from = {3, 1}, .to = {3, 1}},
                  {.kind = EventKind::UnitDamaged, .unit = c, .from = {3, 1}, .to = {3, 1}, .value = 1},
                  {.kind = EventKind::BuildingDamaged, .unit = c, .from = {3, 1}, .to = {3, 0}, .value = 1},
              });
    }

    SECTION("an engineer shoved into water drowns, and can no longer act") {
        const auto other = board.add_unit(UnitKind::Engineer, {3, 1}, 2);
        add_water(board, {3, 0});
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK_FALSE(mission.board().unit(other).alive());
        CHECK(mission.take_events() ==
              std::vector<Event>{
                  {.kind = EventKind::UnitShoved, .unit = other, .from = {3, 1}, .to = {3, 0}},
                  {.kind = EventKind::UnitDrowned, .unit = other, .from = {3, 1}, .to = {3, 0}},
              });
        require_refused(mission, MoveCommand{.engineer = other, .to = {2, 0}}, Rejection::UnitDead);
        require_refused(mission, ShoveCommand{.engineer = other, .dir = Direction::South}, Rejection::UnitDead);
    }
}

TEST_CASE("refused commands change nothing and say why", "[mission][rejection]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {0, 3}, 2);
    const auto c = board.add_unit(UnitKind::Crawler, {5, 5}, 2);
    add_water(board, {1, 3});
    add_building(board, {0, 2}, 2);
    (void)board.add_unit(UnitKind::Engineer, {0, 4}, 2);
    Mission mission = scenario(board);

    SECTION("no such unit, or not an engineer") {
        require_refused(mission, MoveCommand{.engineer = 99, .to = {0, 5}}, Rejection::NotAnEngineer);
        require_refused(mission, ShoveCommand{.engineer = 99, .dir = Direction::East}, Rejection::NotAnEngineer);
        require_refused(mission, MoveCommand{.engineer = c, .to = {5, 4}}, Rejection::NotAnEngineer);
        require_refused(mission, ShoveCommand{.engineer = c, .dir = Direction::North}, Rejection::NotAnEngineer);
    }

    SECTION("destinations out of reach") {
        for (const Coord to : {Coord{0, 3}, Coord{1, 3}, Coord{0, 2}, Coord{0, 4}, Coord{3, 3}, Coord{-1, 3},
                               Coord{0, kBoardSize}, Coord{kBoardSize, kBoardSize}}) {
            CAPTURE(to.x, to.y);
            require_refused(mission, MoveCommand{.engineer = e, .to = to}, Rejection::Unreachable);
        }
    }

    SECTION("nothing to shove: empty tile, or off the board") {
        require_refused(mission, ShoveCommand{.engineer = e, .dir = Direction::East}, Rejection::NothingToShove);
        require_refused(mission, ShoveCommand{.engineer = e, .dir = Direction::West}, Rejection::NothingToShove);
        require_refused(mission, ShoveCommand{.engineer = e, .dir = Direction::North}, Rejection::NothingToShove);
    }

    SECTION("every rejection has a description") {
        for (const auto r : {Rejection::NotAnEngineer, Rejection::UnitDead, Rejection::AlreadyMoved,
                             Rejection::AlreadyActed, Rejection::Unreachable, Rejection::NothingToShove}) {
            CHECK(breakwater::describe(r) != "unknown");
        }
    }
}

TEST_CASE("ending the turn starts the next one with fresh actions", "[mission][turn]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {3, 3}, 2);
    (void)board.add_unit(UnitKind::Crawler, {3, 1}, 2);
    Mission mission = scenario(board);
    REQUIRE(mission.apply(MoveCommand{.engineer = e, .to = {3, 2}}));
    REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
    (void)mission.take_events();

    REQUIRE(mission.apply(EndTurnCommand{}));
    CHECK(mission.turn() == 2);
    CHECK(mission.action_state(e) == ActionState::Ready);
    CHECK_FALSE(mission.reachable(e).empty());
    const auto events = mission.take_events();
    REQUIRE_FALSE(events.empty());
    CHECK(events.back() == Event::turn_started(2));
}

TEST_CASE("mission hash covers seed, turn and action states, not events", "[mission][determinism]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {3, 3}, 2);

    SECTION("the seed decides later draws, so it is part of the position") {
        CHECK(Mission::from_board(1, board).state_hash() != Mission::from_board(2, board).state_hash());
    }

    SECTION("the same board with a spent move is a different position") {
        Mission moved = scenario(board);
        REQUIRE(moved.apply(MoveCommand{.engineer = e, .to = {3, 4}}));
        Board there = board;
        there.place(e, {3, 4});
        const Mission placed = scenario(there);
        REQUIRE(moved.board().state_hash() == placed.board().state_hash());
        CHECK(moved.state_hash() != placed.state_hash());
    }

    SECTION("the turn counter") {
        Mission mission = scenario(board);
        const auto before = mission.state_hash();
        REQUIRE(mission.apply(EndTurnCommand{}));
        CHECK(mission.state_hash() != before);
    }

    SECTION("draining events is observation, not a state change") {
        Mission mission = scenario(board);
        REQUIRE(mission.apply(MoveCommand{.engineer = e, .to = {3, 4}}));
        const auto before = mission.state_hash();
        CHECK_FALSE(mission.take_events().empty());
        CHECK(mission.state_hash() == before);
    }
}

TEST_CASE("a preview is the outcome, and leaves the mission untouched", "[mission][preview]") {
    Board board = flat_board();
    const auto e = board.add_unit(UnitKind::Engineer, {3, 2}, 2);
    const auto c = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
    (void)board.add_unit(UnitKind::Crawler, {3, 0}, 2);
    Mission mission = scenario(board);
    const auto before = mission.state_hash();

    const auto preview = mission.preview(ShoveCommand{.engineer = e, .dir = Direction::North});
    REQUIRE(preview);
    CHECK(mission.state_hash() == before);
    CHECK(mission.take_events().empty());
    CHECK(preview->board().unit(c).hp == 1);

    REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
    CHECK(mission.state_hash() == preview->state_hash());
    auto shown = *preview;
    CHECK(mission.take_events() == shown.take_events());

    const auto refused = mission.preview(ShoveCommand{.engineer = e, .dir = Direction::North});
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == Rejection::AlreadyActed);
}
