#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using breakwater::Board;
using breakwater::Command;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::Event;
using breakwater::EventKind;
using breakwater::Intent;
using breakwater::kTurnCount;
using breakwater::Mission;
using breakwater::MoveCommand;
using breakwater::Outcome;
using breakwater::Rejection;
using breakwater::ShoveCommand;
using breakwater::Tile;
using breakwater::UnitId;
using breakwater::UnitKind;

namespace {

// Three committed missions in tests/logs/, one per ending, each recorded from
// a fixed policy (retreat inland, or stand idle). The golden hashes live in the
// .hashes file beside each log, in bw_replay's output format, so this test and
// tools/replay_check.sh share one source of truth. A rule change that touches
// these missions shows up in both; if it is intended, re-record them with
// bw_replay and say so in the PR.

std::string read_file(const std::string& name) {
    std::ifstream in{std::string{BREAKWATER_LOG_DIR} + "/" + name, std::ios::binary};
    REQUIRE(in);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// The hashes in a .hashes file: after setup, then after each EndTurn.
std::vector<std::uint64_t> golden_hashes(const std::string& name) {
    std::vector<std::uint64_t> hashes;
    std::istringstream lines{read_file(name)};
    std::string word;
    while (lines >> word) {
        if (word == "start" || word == "end") {
            if (word == "end") {
                lines >> word; // The turn number.
            }
            std::string hex;
            lines >> hex;
            hashes.push_back(std::stoull(hex, nullptr, 16));
        }
    }
    REQUIRE_FALSE(hashes.empty());
    return hashes;
}

/// Replays a committed log, requiring every command to be accepted, and
/// returns the mission and the hash after setup and after each EndTurn.
std::pair<Mission, std::vector<std::uint64_t>> replay(const std::string& name) {
    const auto log = breakwater::parse_log(read_file(name));
    REQUIRE(log);
    Mission mission = Mission::generate(log->seed);
    std::vector<std::uint64_t> hashes{mission.state_hash()};
    for (const Command& command : log->commands) {
        REQUIRE(mission.apply(command));
        if (std::holds_alternative<EndTurnCommand>(command)) {
            hashes.push_back(mission.state_hash());
        }
    }
    return {std::move(mission), std::move(hashes)};
}

bool any_engineer_alive(const Board& board) {
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        if (board.unit(id).kind == UnitKind::Engineer && board.unit(id).alive()) {
            return true;
        }
    }
    return false;
}

int standing_buildings(const Board& board) {
    int count = 0;
    for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
        count += board.tile(breakwater::tile_coord(i)).building_hp > 0 ? 1 : 0;
    }
    return count;
}

/// Every kind of command is refused once the mission is over, and the refusal
/// changes nothing.
void require_over(Mission& mission) {
    (void)mission.take_events();
    const auto before = mission.state_hash();
    for (const Command& command :
         std::vector<Command>{MoveCommand{.engineer = 0, .to = {0, 7}},
                              ShoveCommand{.engineer = 0, .dir = Direction::North}, EndTurnCommand{}}) {
        const auto result = mission.apply(command);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == Rejection::MissionOver);
    }
    CHECK(mission.state_hash() == before);
    CHECK(mission.take_events().empty());
}

Board flat_board() {
    Board board;
    for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
        board.set_tile(breakwater::tile_coord(i), Tile{.elevation = 8});
    }
    board.advance_tide();
    return board;
}

void add_building(Board& board, Coord c, std::uint8_t hp) {
    board.set_tile(c, Tile{.elevation = 8, .building_hp = hp});
}

Mission scenario(const Board& board, std::vector<Intent> intents = {}) {
    return Mission::from_board(1, board, {.intents = std::move(intents), .spawns = {}});
}

} // namespace

TEST_CASE("scripted mission: holding the high ground wins", "[outcome][script]") {
    auto [mission, hashes] = replay("win-seed20.log");
    CHECK(hashes == golden_hashes("win-seed20.hashes"));
    CHECK(mission.outcome() == Outcome::Won);
    CHECK(mission.turn() == kTurnCount);
    CHECK(any_engineer_alive(mission.board()));
    CHECK(standing_buildings(mission.board()) > 0);
    require_over(mission);
}

TEST_CASE("scripted mission: idling on the beach loses every engineer", "[outcome][script]") {
    auto [mission, hashes] = replay("loss-engineers-seed20.log");
    CHECK(hashes == golden_hashes("loss-engineers-seed20.hashes"));
    CHECK(mission.outcome() == Outcome::Lost);
    CHECK_FALSE(any_engineer_alive(mission.board()));
    CHECK(standing_buildings(mission.board()) > 0);
    require_over(mission);
}

TEST_CASE("scripted mission: never shoving loses the town", "[outcome][script]") {
    auto [mission, hashes] = replay("loss-town-seed19.log");
    CHECK(hashes == golden_hashes("loss-town-seed19.hashes"));
    CHECK(mission.outcome() == Outcome::Lost);
    CHECK(any_engineer_alive(mission.board()));
    CHECK(standing_buildings(mission.board()) == 0);
    require_over(mission);
}

TEST_CASE("a loss ends the enemy phase on the spot", "[outcome][loss]") {
    SECTION("an attack kills the last engineer: no tide, no spawn, no new turn") {
        Board board = flat_board();
        (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        (void)board.add_unit(UnitKind::Engineer, {3, 4}, 1);
        add_building(board, {7, 7}, 2);
        Mission mission =
            Mission::from_board(1, board, {.intents = {Intent{Direction::South}}, .spawns = {0, 1, 1, 1, 1}});
        REQUIRE(mission.apply(EndTurnCommand{}));
        const auto events = mission.take_events();
        CHECK(mission.outcome() == Outcome::Lost);
        CHECK(mission.turn() == 1);
        CHECK(mission.board().tide() == 1);
        REQUIRE_FALSE(events.empty());
        CHECK(events.back() == Event{.kind = EventKind::MissionEnded,
                                     .unit = 0,
                                     .from = {},
                                     .to = {},
                                     .value = static_cast<int>(Outcome::Lost)});
        for (const Event& e : events) {
            CHECK(e.kind != EventKind::TideRose);
            CHECK(e.kind != EventKind::UnitSpawned);
            CHECK(e.kind != EventKind::TurnStarted);
        }
    }

    SECTION("the tide drowns the last engineer") {
        Board board = flat_board();
        board.set_tile({2, 2}, Tile{.elevation = 1});
        (void)board.add_unit(UnitKind::Engineer, {2, 2}, 2);
        add_building(board, {7, 7}, 2);
        Mission mission = Mission::from_board(1, board, {.intents = {}, .spawns = {0, 1, 1, 1, 1}});
        REQUIRE(mission.apply(EndTurnCommand{}));
        CHECK(mission.outcome() == Outcome::Lost);
        CHECK(mission.board().unit_count() == 1); // No spawn after the loss.
    }

    SECTION("a shove razes the last building: lost before the turn ends") {
        Board board = flat_board();
        const auto e = board.add_unit(UnitKind::Engineer, {3, 4}, 2);
        (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
        add_building(board, {3, 2}, 1);
        Mission mission = scenario(board);
        REQUIRE(mission.apply(ShoveCommand{.engineer = e, .dir = Direction::North}));
        CHECK(mission.outcome() == Outcome::Lost);
        require_over(mission);
    }
}

TEST_CASE("surviving the last enemy phase wins, and nothing follows it", "[outcome][win]") {
    Board board = flat_board();
    // The crawler marches on the building but is still four tiles short of
    // it when the last turn ends; the engineer is never in its reach.
    const auto c = board.add_unit(UnitKind::Crawler, {0, 0}, 2);
    (void)board.add_unit(UnitKind::Engineer, {0, 7}, 2);
    add_building(board, {7, 7}, 2);
    Mission mission = scenario(board);
    for (int turn = 1; turn < kTurnCount; ++turn) {
        REQUIRE(mission.apply(EndTurnCommand{}));
        REQUIRE(mission.outcome() == Outcome::InProgress);
    }
    (void)mission.take_events();
    REQUIRE(mission.apply(EndTurnCommand{}));
    const auto events = mission.take_events();
    CHECK(mission.outcome() == Outcome::Won);
    CHECK(mission.turn() == kTurnCount);
    CHECK_FALSE(mission.intent(c).has_value());
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().kind == EventKind::MissionEnded);
    CHECK(events.back().value == static_cast<int>(Outcome::Won));
    for (const Event& e : events) {
        CHECK(e.kind != EventKind::TurnStarted);
    }
    require_over(mission);
}

TEST_CASE("a board with nothing to defend cannot be lost by razing it", "[outcome][scenario]") {
    Board board = flat_board();
    (void)board.add_unit(UnitKind::Engineer, {7, 7}, 2);
    Mission mission = scenario(board);
    REQUIRE(mission.apply(EndTurnCommand{}));
    CHECK(mission.outcome() == Outcome::InProgress);
}

TEST_CASE("the outcome is part of the state hash", "[outcome][determinism]") {
    Board board = flat_board();
    (void)board.add_unit(UnitKind::Crawler, {3, 3}, 2);
    (void)board.add_unit(UnitKind::Engineer, {3, 4}, 1);
    add_building(board, {7, 7}, 2);
    Mission lost = scenario(board, {Intent{Direction::South}});
    REQUIRE(lost.apply(EndTurnCommand{}));
    REQUIRE(lost.outcome() == Outcome::Lost);

    // The same board, turn, actions and (cleared) intents, still in progress.
    const Mission same_board = scenario(lost.board());
    REQUIRE(same_board.board().state_hash() == lost.board().state_hash());
    CHECK(same_board.state_hash() != lost.state_hash());
}
