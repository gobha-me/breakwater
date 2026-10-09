#include "breakwater/mission.hpp"
#include "breakwater/rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

using breakwater::Board;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::Event;
using breakwater::EventKind;
using breakwater::Intent;
using breakwater::kEngineerCount;
using breakwater::Mission;
using breakwater::MoveCommand;
using breakwater::SpawnTable;
using breakwater::Tile;
using breakwater::UnitId;
using breakwater::UnitKind;

namespace {

/// High, dry ground everywhere: elevation 8 at tide 1. Scenarios lower tiles.
Board flat_board() {
    Board board;
    for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
        board.set_tile(breakwater::tile_coord(i), Tile{.elevation = 8});
    }
    board.advance_tide();
    return board;
}

void set_elevation(Board& board, Coord c, std::uint8_t elevation) { board.set_tile(c, Tile{.elevation = elevation}); }

void add_building(Board& board, Coord c) { board.set_tile(c, Tile{.elevation = 8, .building_hp = 2}); }

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

/// Index of the first event of a kind; the size of the log if there is none.
std::size_t first(const std::vector<Event>& events, EventKind kind) {
    return static_cast<std::size_t>(std::ranges::find_if(events, [kind](const Event& e) { return e.kind == kind; }) -
                                    events.begin());
}

std::size_t last(const std::vector<Event>& events, EventKind kind) {
    const auto it =
        std::ranges::find_if(events.rbegin(), events.rend(), [kind](const Event& e) { return e.kind == kind; });
    return static_cast<std::size_t>(events.rend() - it) - 1;
}

/// The spawn tile the rules pick from a candidate list on a given turn.
Coord expected_spawn(std::uint64_t seed, int turn, const std::vector<Coord>& candidates) {
    auto rng = breakwater::make_turn_stream(seed, breakwater::Stream::Spawns, turn);
    return candidates.at(rng.below(static_cast<std::uint32_t>(candidates.size())));
}

/// A fixed, legal policy for replay tests: each living engineer retreats to its
/// most inland reachable tile (lowest x on ties), then the turn ends.
void retreat_and_end_turn(Mission& mission) {
    for (UnitId id = 0; id < kEngineerCount; ++id) {
        const auto tiles = mission.reachable(id);
        if (tiles.empty()) {
            continue;
        }
        const auto best =
            std::ranges::max_element(tiles, [](Coord a, Coord b) { return a.y != b.y ? a.y < b.y : a.x > b.x; });
        REQUIRE(mission.apply(MoveCommand{.engineer = id, .to = *best}));
    }
    REQUIRE(mission.apply(EndTurnCommand{}));
}

} // namespace

TEST_CASE("a turn runs its phases in the order the rules give", "[turn][pipeline]") {
    Board board = flat_board();
    const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
    const auto target = board.add_unit(UnitKind::Engineer, {3, 4}, 2);
    set_elevation(board, {1, 6}, 1); // Dry at tide 1, sea at tide 2.
    const auto doomed = board.add_unit(UnitKind::Engineer, {1, 6}, 2);
    set_elevation(board, {5, 2}, 0);
    add_building(board, {7, 7});
    Mission mission = Mission::from_board(7, board, {.intents = {Intent{Direction::South}}, .spawns = {0, 1, 0, 0, 0}});

    const auto events = end_turn(mission);
    CHECK(mission.board().unit(target).hp == 1);
    CHECK_FALSE(mission.board().unit(doomed).alive());
    CHECK(mission.board().tide() == 2);

    // Attacks, then the tide and its drownings, then spawns, then plans, then the turn.
    CHECK(last(events, EventKind::AttackResolved) < first(events, EventKind::TideRose));
    CHECK(first(events, EventKind::TideRose) < first(events, EventKind::UnitDrowned));
    CHECK(last(events, EventKind::UnitDrowned) < first(events, EventKind::UnitSpawned));
    CHECK(last(events, EventKind::UnitSpawned) < first(events, EventKind::IntentDeclared));
    CHECK(events.back() == Event::turn_started(2));

    // The drowned engineer's tile is now the most inland water, so the crawler
    // comes ashore there rather than at (5,2).
    const auto spawned = of_kind(events, EventKind::UnitSpawned);
    REQUIRE(spawned.size() == 1);
    CHECK(spawned.front().to == Coord{1, 6});
    CHECK(mission.intent(c) == Intent{Direction::South});
}

TEST_CASE("the rising tide drowns engineers and spares crawlers", "[turn][tide]") {
    Board board = flat_board();
    set_elevation(board, {2, 2}, 1);
    set_elevation(board, {5, 5}, 1);
    const auto low = board.add_unit(UnitKind::Engineer, {2, 2}, 2);
    const auto high = board.add_unit(UnitKind::Engineer, {6, 6}, 2);
    const auto swimmer = board.add_unit(UnitKind::Crawler, {5, 5}, 2);
    Mission mission = Mission::from_board(1, board, {.intents = {}, .spawns = {}});

    const auto events = end_turn(mission);
    CHECK_FALSE(mission.board().unit(low).alive());
    CHECK(mission.board().unit(high).alive());
    CHECK(mission.board().unit(swimmer).alive());
    CHECK(of_kind(events, EventKind::TideRose) ==
          std::vector<Event>{{.kind = EventKind::TideRose, .unit = 0, .from = {}, .to = {}, .value = 2}});
    CHECK(of_kind(events, EventKind::UnitDrowned) ==
          std::vector<Event>{{.kind = EventKind::UnitDrowned, .unit = low, .from = {2, 2}, .to = {2, 2}, .value = 0}});
}

TEST_CASE("spawns come ashore at the waterline", "[turn][spawn]") {
    constexpr std::uint64_t kSeed = 99;
    Board board = flat_board();
    add_building(board, {4, 7});
    (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);

    SECTION("the most inland water row, sorted by x, one draw from the turn's stream") {
        set_elevation(board, {0, 1}, 0);
        set_elevation(board, {5, 2}, 0);
        set_elevation(board, {1, 2}, 0);
        Mission mission = Mission::from_board(kSeed, board, {.intents = {}, .spawns = {0, 1, 0, 0, 0}});
        const auto spawned = of_kind(end_turn(mission), EventKind::UnitSpawned);
        REQUIRE(spawned.size() == 1);
        CHECK(spawned.front().to == expected_spawn(kSeed, 1, {{1, 2}, {5, 2}}));
        CHECK(mission.board().unit(spawned.front().unit).kind == UnitKind::Crawler);
        CHECK(mission.board().unit(spawned.front().unit).hp == breakwater::kUnitHp);
    }

    SECTION("no water: the empty tiles of row 0") {
        Mission mission = Mission::from_board(kSeed, board, {.intents = {}, .spawns = {0, 1, 0, 0, 0}});
        const auto spawned = of_kind(end_turn(mission), EventKind::UnitSpawned);
        REQUIRE(spawned.size() == 1);
        std::vector<Coord> row0;
        row0.reserve(breakwater::kBoardSize);
        for (int x = 0; x < breakwater::kBoardSize; ++x) {
            row0.push_back({x, 0});
        }
        CHECK(spawned.front().to == expected_spawn(kSeed, 1, row0));
    }

    SECTION("several spawns in one turn rebuild the list after each") {
        set_elevation(board, {3, 2}, 0);
        Mission mission = Mission::from_board(kSeed, board, {.intents = {}, .spawns = {0, 2, 0, 0, 0}});
        const auto spawned = of_kind(end_turn(mission), EventKind::UnitSpawned);
        REQUIRE(spawned.size() == 2);
        CHECK(spawned.at(0).to == Coord{3, 2}); // The only water tile.
        CHECK(spawned.at(1).to.y == 0);         // Then the shore of last resort.
    }

    SECTION("a full shore and no water: the spawn is skipped") {
        for (int x = 0; x < breakwater::kBoardSize; ++x) {
            (void)board.add_unit(UnitKind::Crawler, {x, 0}, 2);
        }
        Mission mission = Mission::from_board(kSeed, board, {.intents = {}, .spawns = {0, 1, 0, 0, 0}});
        const auto before = mission.board().unit_count();
        CHECK(of_kind(end_turn(mission), EventKind::UnitSpawned).empty());
        CHECK(mission.board().unit_count() == before);
    }

    SECTION("a spawned crawler plans in the same enemy phase") {
        set_elevation(board, {7, 5}, 0);
        Mission mission = Mission::from_board(kSeed, board, {.intents = {}, .spawns = {0, 1, 0, 0, 0}});
        const auto events = end_turn(mission);
        const auto spawned = of_kind(events, EventKind::UnitSpawned);
        REQUIRE(spawned.size() == 1);
        CHECK(mission.intent(spawned.front().unit) == Intent{Direction::South}); // The engineer at (7,7).
        CHECK(mission.board().unit(spawned.front().unit).pos == Coord{7, 6});
    }
}

TEST_CASE("setup lands two crawlers that telegraph before turn 1", "[turn][spawn][setup]") {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        Mission mission = Mission::generate(seed);
        const Board& board = mission.board();
        REQUIRE(board.unit_count() == kEngineerCount + 2);
        std::set<std::pair<int, int>> tiles;
        for (UnitId id = 0; id < board.unit_count(); ++id) {
            const auto& u = board.unit(id);
            REQUIRE(u.alive());
            REQUIRE(board.tile(u.pos).building_hp == 0);
            tiles.insert({u.pos.x, u.pos.y});
        }
        REQUIRE(tiles.size() == board.unit_count());
        REQUIRE(board.unit(kEngineerCount).kind == UnitKind::Crawler);
        REQUIRE(board.unit(kEngineerCount + 1).kind == UnitKind::Crawler);
        REQUIRE(mission.take_events().empty());
    }
}

TEST_CASE("the last turn runs attacks and the tide, then nothing plans", "[turn][final]") {
    Board board = flat_board();
    const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
    (void)board.add_unit(UnitKind::Engineer, {3, 4}, 20); // Outlasts every strike, so c always has a target.
    set_elevation(board, {0, 0}, 0);
    Mission mission = Mission::from_board(1, board, {.intents = {}, .spawns = {0, 1, 1, 1, 1}});
    for (int turn = 1; turn < breakwater::kTurnCount; ++turn) {
        (void)end_turn(mission);
    }
    REQUIRE(mission.turn() == breakwater::kTurnCount);
    REQUIRE(mission.intent(c).has_value());
    const auto before = mission.board().unit_count();

    const auto events = end_turn(mission);
    CHECK_FALSE(of_kind(events, EventKind::AttackResolved).empty());
    CHECK_FALSE(of_kind(events, EventKind::TideRose).empty());
    CHECK(of_kind(events, EventKind::UnitSpawned).empty());
    CHECK(of_kind(events, EventKind::IntentDeclared).empty());
    CHECK(of_kind(events, EventKind::UnitMoved).empty());
    CHECK(mission.board().unit_count() == before);
    CHECK_FALSE(mission.intent(c).has_value());
}

TEST_CASE("generated missions survive five idle turns without breaking a rule", "[turn][soak]") {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        Mission mission = Mission::generate(seed);
        for (int turn = 0; turn < breakwater::kTurnCount; ++turn) {
            REQUIRE(mission.apply(EndTurnCommand{}));
        }
        const Board& board = mission.board();
        REQUIRE(board.tide() == breakwater::kStartTide + breakwater::kTurnCount);
        REQUIRE(board.unit_count() <= kEngineerCount + 5);
        for (UnitId id = 0; id < board.unit_count(); ++id) {
            const auto& u = board.unit(id);
            if (u.alive() && u.kind == UnitKind::Engineer) {
                REQUIRE_FALSE(board.flooded(u.pos));
            }
        }
    }
}

TEST_CASE("the same commands from the same seed give the same hash every turn", "[turn][determinism]") {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        auto play = [seed] {
            Mission mission = Mission::generate(seed);
            std::vector<std::uint64_t> hashes{mission.state_hash()};
            for (int turn = 0; turn < breakwater::kTurnCount; ++turn) {
                retreat_and_end_turn(mission);
                hashes.push_back(mission.state_hash());
            }
            return hashes;
        };
        REQUIRE(play() == play());
    }
}

// Hidden: run with `breakwater_tests "[evidence]"` to print hashes for a PR.
TEST_CASE("per-turn hashes for seeds 0-2 under the retreat policy", "[.][evidence]") {
    for (std::uint64_t seed = 0; seed < 3; ++seed) {
        Mission mission = Mission::generate(seed);
        std::cout << "seed " << seed << " turn 1 " << std::hex << mission.state_hash() << std::dec << '\n';
        for (int turn = 0; turn < breakwater::kTurnCount; ++turn) {
            retreat_and_end_turn(mission);
            std::cout << "seed " << seed << " turn " << mission.turn() << ' ' << std::hex << mission.state_hash()
                      << std::dec << '\n';
        }
    }
    SUCCEED();
}
