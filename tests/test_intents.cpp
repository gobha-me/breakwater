#include "breakwater/mission.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
using breakwater::MoveCommand;
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

TEST_CASE("crawler intent follows the ranking key in docs/RULES.md", "[intent][plan]") {
    SECTION("an adjacent engineer: stay and strike it") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        (void)board.add_unit(UnitKind::Engineer, {3, 4}, 2);
        add_building(board, {6, 6});
        Mission mission = scenario(board);
        const auto events = end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{3, 3});
        CHECK(mission.intent(c) == Intent{Direction::South});
        CHECK(of_kind(events, EventKind::UnitMoved).empty());
        CHECK(of_kind(events, EventKind::IntentDeclared) ==
              std::vector<Event>{{.kind = EventKind::IntentDeclared,
                                  .unit = c,
                                  .from = {3, 3},
                                  .to = {3, 4},
                                  .value = static_cast<int>(Direction::South)}});
    }

    SECTION("a building outranks an engineer, even an adjacent one") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {0, 0}, 2);
        (void)board.add_unit(UnitKind::Engineer, {0, 1}, 2);
        add_building(board, {3, 0});
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{2, 0});
        CHECK(mission.intent(c) == Intent{Direction::East});
    }

    SECTION("a building in reach when no engineer is; ties go to the lower row") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {0, 0}, 2);
        add_building(board, {2, 1});
        add_building(board, {0, 6});
        (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        // (2,0) striking South and (1,1) striking East are both two steps; row 0 wins.
        CHECK(mission.board().unit(c).pos == Coord{2, 0});
        CHECK(mission.intent(c) == Intent{Direction::South});
    }

    SECTION("same row: the lower column wins") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
        (void)board.add_unit(UnitKind::Engineer, {1, 1}, 2);
        (void)board.add_unit(UnitKind::Engineer, {5, 1}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{2, 1});
        CHECK(mission.intent(c) == Intent{Direction::West});
    }

    SECTION("same tile: North, East, South, West") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        (void)board.add_unit(UnitKind::Engineer, {4, 3}, 2);
        (void)board.add_unit(UnitKind::Engineer, {3, 2}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{3, 3});
        CHECK(mission.intent(c) == Intent{Direction::North});
    }

    SECTION("nothing in reach: close on the town and declare no attack") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {0, 0}, 2);
        add_building(board, {0, 7});
        (void)board.add_unit(UnitKind::Engineer, {7, 0}, 2);
        Mission mission = scenario(board);
        const auto events = end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{0, 3}); // Three steps: the crawler's full reach.
        CHECK_FALSE(mission.intent(c).has_value());
        CHECK(of_kind(events, EventKind::IntentDeclared).empty());
        CHECK(of_kind(events, EventKind::UnitMoved) ==
              std::vector<Event>{{.kind = EventKind::UnitMoved, .unit = c, .from = {0, 0}, .to = {0, 3}, .value = 0}});
    }

    SECTION("no building standing and nothing in reach: hold position") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {0, 0}, 2);
        (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
        Mission mission = scenario(board);
        const auto events = end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{0, 0});
        CHECK_FALSE(mission.intent(c).has_value());
        CHECK(of_kind(events, EventKind::UnitMoved).empty());
    }

    SECTION("crawlers plan in id order, around the tiles earlier ones took") {
        Board board = flat_board();
        const auto first = board.add_unit(UnitKind::Crawler, {3, 1}, 2);
        const auto second = board.add_unit(UnitKind::Crawler, {2, 1}, 2);
        (void)board.add_unit(UnitKind::Engineer, {3, 3}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(first).pos == Coord{3, 2});
        CHECK(mission.intent(first) == Intent{Direction::South});
        // (3,2) would have ranked first for `second` too, but it is taken.
        CHECK(mission.board().unit(second).pos == Coord{2, 3});
        CHECK(mission.intent(second) == Intent{Direction::East});
    }

    SECTION("crawlers swim: water is the only path in reach to the target") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 0}, 2);
        add_water(board, {3, 1});
        // Every tile next to the engineer is four land steps away; (3,2) is
        // two steps through the water at (3,1).
        (void)board.add_unit(UnitKind::Engineer, {3, 3}, 2);
        Mission mission = scenario(board);
        (void)end_turn(mission);
        CHECK(mission.board().unit(c).pos == Coord{3, 2});
        CHECK(mission.intent(c) == Intent{Direction::South});
    }
}

TEST_CASE("intents are published before the player acts and hold through it", "[intent][telegraph]") {
    Board board = flat_board();
    const auto c = board.add_unit(UnitKind::Crawler, {3, 2}, 2);
    const auto e = board.add_unit(UnitKind::Engineer, {3, 4}, 2);
    add_building(board, {7, 7});
    Mission mission = scenario(board);
    (void)end_turn(mission);
    REQUIRE(mission.board().unit(c).pos == Coord{3, 3});
    REQUIRE(mission.intent(c) == Intent{Direction::South});

    // The player sidesteps; the intent does not follow.
    REQUIRE(mission.apply(MoveCommand{.engineer = e, .to = {5, 4}}));
    CHECK(mission.intent(c) == Intent{Direction::South});
    CHECK(mission.board().unit(c).pos == Coord{3, 3});

    const auto events = end_turn(mission);
    CHECK(of_kind(events, EventKind::AttackResolved) ==
          std::vector<Event>{{.kind = EventKind::AttackResolved, .unit = c, .from = {3, 3}, .to = {3, 4}, .value = 0}});
    CHECK(of_kind(events, EventKind::UnitDamaged).empty());
    CHECK(mission.board().unit(e).hp == 2);
}

TEST_CASE("attacks resolve against whatever is there now", "[intent][resolve]") {
    SECTION("a strike deals its damage to the engineer it was aimed at") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {3, 4}, 3); // Survives one strike of 2.
        Mission mission = scenario(board, {Intent{Direction::South}});
        const auto events = end_turn(mission);
        CHECK(mission.board().unit(e).hp == 1);
        CHECK(
            of_kind(events, EventKind::UnitDamaged) ==
            std::vector<Event>{{.kind = EventKind::UnitDamaged, .unit = e, .from = {3, 4}, .to = {3, 4}, .value = 1}});
        CHECK(mission.intent(c) == Intent{Direction::South}); // Replanned: still adjacent.
    }

    SECTION("a shove retargets the strike onto another crawler") {
        Board board = flat_board();
        const auto c1 = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        const auto c2 = board.add_unit(UnitKind::Crawler, {4, 4}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 3}, 2);
        Mission mission = scenario(board, {Intent{Direction::South}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::East}));
        REQUIRE(mission.board().unit(c1).pos == Coord{4, 3});
        (void)end_turn(mission);
        CHECK_FALSE(mission.board().unit(c2).alive()); // A strike of 2 kills a crawler outright.
    }

    SECTION("a building takes the hit, and a razed one absorbs no more") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        (void)board.add_unit(UnitKind::Crawler, {2, 4}, 2);
        add_building(board, {3, 4}, 1);
        Mission mission = scenario(board, {Intent{Direction::South}, Intent{Direction::East}});
        const auto events = end_turn(mission);
        CHECK(mission.board().tile({3, 4}).building_hp == 0);
        CHECK(of_kind(events, EventKind::BuildingDamaged).size() == 1);
        CHECK(of_kind(events, EventKind::AttackResolved).size() == 2);
    }

    SECTION("a crawler shoved to face off the board misses") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {1, 3}, 2);
        const auto e = board.add_unit(UnitKind::Engineer, {2, 3}, 2);
        Mission mission = scenario(board, {Intent{Direction::West}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::West}));
        const auto events = end_turn(mission);
        CHECK(of_kind(events, EventKind::AttackResolved) ==
              std::vector<Event>{
                  {.kind = EventKind::AttackResolved, .unit = c, .from = {0, 3}, .to = {-1, 3}, .value = 0}});
        CHECK(of_kind(events, EventKind::UnitDamaged).empty());
    }
}

TEST_CASE("stale intents degrade to stated fallbacks", "[intent][stale]") {
    SECTION("a crawler killed during the player phase does not strike") {
        Board board = flat_board();
        const auto c = board.add_unit(UnitKind::Crawler, {3, 0}, 1);
        const auto e = board.add_unit(UnitKind::Engineer, {3, 1}, 2);
        Mission mission = scenario(board, {Intent{Direction::South}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        REQUIRE_FALSE(mission.board().unit(c).alive());
        CHECK_FALSE(mission.intent(c).has_value());
        const auto events = end_turn(mission);
        CHECK(of_kind(events, EventKind::AttackResolved).empty());
        CHECK(mission.board().unit(e).hp == 2);
    }

    SECTION("a crawler killed earlier in the same step does not strike") {
        Board board = flat_board();
        const auto c0 = board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        const auto c1 = board.add_unit(UnitKind::Crawler, {4, 3}, 1);
        const auto e = board.add_unit(UnitKind::Engineer, {4, 4}, 2);
        Mission mission = scenario(board, {Intent{Direction::East}, Intent{Direction::South}});
        const auto events = end_turn(mission);
        CHECK_FALSE(mission.board().unit(c1).alive());
        CHECK(mission.board().unit(e).hp == 2);
        const auto attacks = of_kind(events, EventKind::AttackResolved);
        REQUIRE(attacks.size() == 1);
        CHECK(attacks.front().unit == c0);
        CHECK(of_kind(events, EventKind::UnitDied) ==
              std::vector<Event>{{.kind = EventKind::UnitDied, .unit = c1, .from = {4, 3}, .to = {4, 3}, .value = 0}});
    }

    SECTION("the target drowned: the strike lands on an empty tile") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        const auto target = board.add_unit(UnitKind::Engineer, {3, 4}, 2);
        const auto pusher = board.add_unit(UnitKind::Engineer, {2, 4}, 2);
        add_water(board, {4, 4});
        Mission mission = scenario(board, {Intent{Direction::South}});
        REQUIRE(mission.apply(ShoveCommand{.engineer = pusher, .dir = Direction::East}));
        REQUIRE_FALSE(mission.board().unit(target).alive());
        const auto events = end_turn(mission);
        CHECK(of_kind(events, EventKind::AttackResolved).size() == 1);
        CHECK(of_kind(events, EventKind::UnitDamaged).empty());
        CHECK(mission.board().unit(pusher).hp == 2);
    }
}

TEST_CASE("intent planning is deterministic", "[intent][determinism]") {
    auto run = [] {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Engineer, {2, 5}, 2);
        (void)board.add_unit(UnitKind::Engineer, {5, 5}, 2);
        (void)board.add_unit(UnitKind::Crawler, {0, 0}, 2);
        (void)board.add_unit(UnitKind::Crawler, {4, 0}, 2);
        (void)board.add_unit(UnitKind::Crawler, {7, 1}, 2);
        add_building(board, {1, 7});
        add_building(board, {6, 6});
        Mission mission = Mission::from_board(42, board);
        std::vector<Event> log;
        std::vector<std::uint64_t> hashes;
        for (int turn = 0; turn < 4 && mission.outcome() == breakwater::Outcome::InProgress; ++turn) {
            const auto events = end_turn(mission);
            log.insert(log.end(), events.begin(), events.end());
            hashes.push_back(mission.state_hash());
        }
        return std::pair{log, hashes};
    };
    const auto a = run();
    const auto b = run();
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
    CHECK_FALSE(of_kind(a.first, EventKind::IntentDeclared).empty());
    CHECK_FALSE(of_kind(a.first, EventKind::AttackResolved).empty());
}

TEST_CASE("intents are part of the state hash; a dead crawler's are not", "[intent][determinism]") {
    Board board = flat_board();
    (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
    const auto none = scenario(board).state_hash();
    const auto south = scenario(board, {Intent{Direction::South}}).state_hash();
    const auto north = scenario(board, {Intent{Direction::North}}).state_hash();
    CHECK(none != south);
    CHECK(south != north);
    CHECK(none != north);

    Board dead = flat_board();
    const auto c = dead.add_unit(UnitKind::Crawler, {3, 0}, 1);
    dead.damage_unit(c, 1);
    CHECK(scenario(dead).state_hash() == scenario(dead, {Intent{Direction::South}}).state_hash());
}
