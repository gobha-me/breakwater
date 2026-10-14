#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"
#include "breakwater/rng.hpp"
#include "breakwater/seeds.hpp"
#include "breakwater/solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

using breakwater::Board;
using breakwater::Command;
using breakwater::Coord;
using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::kBoardSize;
using breakwater::kEngineerCount;
using breakwater::Mission;
using breakwater::MissionLog;
using breakwater::MoveCommand;
using breakwater::Outcome;
using breakwater::Rng;
using breakwater::ShoveCommand;
using breakwater::UnitId;
using breakwater::UnitKind;

namespace {

/// Every rule-level invariant that must hold between any two commands. Returns
/// a description of the first violation, or an empty string.
std::string violated_invariant(const Mission& mission) {
    const Board& board = mission.board();
    const int turn = mission.turn();
    if (turn < 1 || turn > breakwater::kTurnCount) {
        return "turn out of range";
    }
    // The tide is `turn` during a turn; an ending may leave it one higher.
    const int tide = board.tide();
    if (mission.outcome() == Outcome::InProgress ? tide != turn : (tide != turn && tide != turn + 1)) {
        return "tide off schedule";
    }
    constexpr std::size_t kMaxUnits = kEngineerCount + 5; // Every spawn in the table.
    if (board.unit_count() > kMaxUnits) {
        return "more crawlers than the spawn table allows";
    }
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const auto& t = board.tile({x, y});
            const int elevation = t.elevation;
            if (t.building_hp > 2) {
                return "building HP above 2";
            }
            if (elevation != y && elevation != y + 1) {
                return "elevation changed";
            }
        }
    }
    if (mission.town_size() != 3 || mission.razed() < 0 || mission.razed() > mission.town_size()) {
        return "the town's size or razed count is off";
    }
    if (mission.outcome() != Outcome::Lost && mission.razed() >= mission.town_falls_after()) {
        return "the town fell but the mission was not lost";
    }
    std::set<std::pair<int, int>> occupied;
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        const auto& u = board.unit(id);
        if (u.hp > breakwater::kUnitHp) {
            return "unit HP above the maximum";
        }
        if (mission.intent(id) && (u.kind != UnitKind::Crawler || !u.alive())) {
            return "intent on something other than a living crawler";
        }
        if (mission.outcome() != Outcome::InProgress && mission.intent(id)) {
            return "intent survives the ending";
        }
        if (!u.alive()) {
            continue;
        }
        if (!breakwater::in_bounds(u.pos)) {
            return "living unit off the board";
        }
        if (!occupied.insert({u.pos.x, u.pos.y}).second) {
            return "two living units share a tile";
        }
        if (board.tile(u.pos).building_hp > 0) {
            return "living unit on a building";
        }
        if (u.kind == UnitKind::Engineer && board.flooded(u.pos)) {
            return "living engineer in the water";
        }
    }
    return {};
}

/// Every command the mission would accept right now.
std::vector<Command> legal_commands(const Mission& mission) {
    std::vector<Command> commands{EndTurnCommand{}};
    for (UnitId id = 0; id < kEngineerCount; ++id) {
        for (const Coord to : mission.reachable(id)) {
            commands.emplace_back(MoveCommand{.engineer = id, .to = to});
        }
        const auto& u = mission.board().unit(id);
        if (!u.alive() || mission.action_state(id) == breakwater::ActionState::Done) {
            continue;
        }
        for (const Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const Coord target = breakwater::step(u.pos, dir);
            if (breakwater::in_bounds(target) && mission.board().unit_at(target)) {
                commands.emplace_back(ShoveCommand{.engineer = id, .dir = dir});
            }
        }
    }
    return commands;
}

/// Anything at all, legal or not: an id that may not exist, a tile that may be
/// off the board.
Command random_command(Rng& rng) {
    const auto id = static_cast<UnitId>(rng.below(10));
    switch (rng.below(3)) {
    case 0:
        return MoveCommand{.engineer = id,
                           .to = {static_cast<int>(rng.below(12)) - 2, static_cast<int>(rng.below(12)) - 2}};
    case 1:
        return ShoveCommand{.engineer = id, .dir = static_cast<Direction>(rng.below(4))};
    default:
        return EndTurnCommand{};
    }
}

struct SoakStats {
    int missions = 0;
    int commands = 0;
    int refusals = 0;
    int won = 0;
};

/// Plays one mission to its end with random legal commands, probing random
/// illegal ones along the way, and checks every invariant after every command.
/// Then writes the mission as a log, parses it back, and replays it to the same
/// hash.
void soak_one(std::uint64_t seed, Rng& driver, SoakStats& stats) {
    Mission mission = Mission::generate(seed);
    MissionLog log{.seed = seed, .commands = {}};
    {
        const auto broken = violated_invariant(mission);
        INFO("seed " << seed << ", command 0: " << broken);
        REQUIRE(broken.empty());
    }

    while (mission.outcome() == Outcome::InProgress) {
        // One probe in four: anything at all. If refused, nothing may change.
        if (driver.below(4) == 0) {
            const Command probe = random_command(driver);
            const auto before = mission.state_hash();
            if (mission.apply(probe)) {
                log.commands.push_back(probe);
                ++stats.commands;
            } else {
                REQUIRE(mission.state_hash() == before);
                ++stats.refusals;
            }
        } else {
            // Ending the turn is one choice among many, so turns run long
            // enough to use every engineer's move and shove.
            const auto commands = legal_commands(mission);
            const Command chosen = commands.at(driver.below(static_cast<std::uint32_t>(commands.size())));
            // Every preview must be the outcome: same hash, same events.
            auto preview = mission.preview(chosen);
            REQUIRE(preview);
            (void)mission.take_events();
            REQUIRE(mission.apply(chosen));
            REQUIRE(preview->state_hash() == mission.state_hash());
            const auto shown = preview->take_events();
            const auto happened = mission.take_events();
            REQUIRE(shown == happened);
            log.commands.push_back(chosen);
            ++stats.commands;
        }
        (void)mission.take_events();
        const auto broken = violated_invariant(mission);
        INFO("seed " << seed << ", command " << log.commands.size() << ": " << broken);
        REQUIRE(broken.empty());
    }
    REQUIRE(mission.apply(EndTurnCommand{}).error() == breakwater::Rejection::MissionOver);

    const auto parsed = breakwater::parse_log(breakwater::write_log(log));
    REQUIRE(parsed);
    REQUIRE(*parsed == log);
    Mission replayed = Mission::generate(parsed->seed);
    for (const Command& command : parsed->commands) {
        REQUIRE(replayed.apply(command));
    }
    REQUIRE(replayed.state_hash() == mission.state_hash());

    ++stats.missions;
    stats.won += mission.outcome() == Outcome::Won ? 1 : 0;
}

SoakStats soak(std::uint64_t first_seed, int count) {
    Rng driver{0x50A4ULL ^ first_seed}; // The driver's own generator, not a game stream.
    SoakStats stats;
    for (int i = 0; i < count; ++i) {
        soak_one(first_seed + static_cast<std::uint64_t>(i), driver, stats);
    }
    return stats;
}

} // namespace

TEST_CASE("a thousand missions of random commands break no invariant", "[soak][random]") {
    const auto stats = soak(0, 1'000);
    CHECK(stats.missions == 1'000);
    CHECK(stats.refusals > 0);
    WARN("soak: " << stats.missions << " missions, " << stats.commands << " commands, " << stats.refusals
                  << " refusals, " << stats.won << " won");
}

// Random play almost never wins once the town falls after two buildings, so
// the winning ending is covered here: solver witnesses, played through the
// same preview and invariant checks as the soak, all the way to a win.
TEST_CASE("solver witnesses keep every invariant through to a win", "[soak][win]") {
    // Seeds 0-9 are test_solver.cpp's; three more are enough to see the ending.
    int won = 0;
    for (std::uint64_t seed = 10; won < 3; ++seed) {
        if (!breakwater::is_offered(seed)) {
            continue;
        }
        Mission mission = Mission::generate(seed);
        const auto result = breakwater::solve(mission, {.node_budget = breakwater::kProofBudget});
        REQUIRE(result.verdict == breakwater::Verdict::Winnable);
        for (const Command& command : result.witness) {
            auto preview = mission.preview(command);
            REQUIRE(preview);
            (void)mission.take_events();
            REQUIRE(mission.apply(command));
            REQUIRE(preview->state_hash() == mission.state_hash());
            REQUIRE(preview->take_events() == mission.take_events());
            const auto broken = violated_invariant(mission);
            INFO("seed " << seed << ": " << broken);
            REQUIRE(broken.empty());
        }
        REQUIRE(mission.outcome() == Outcome::Won);
        ++won;
    }
}

// Hidden: `breakwater_tests "[.long-soak]"`, run in the sanitizer build.
TEST_CASE("twenty thousand missions of random commands break no invariant", "[.long-soak]") {
    const auto stats = soak(1'000'000, 20'000);
    CHECK(stats.missions == 20'000);
    WARN("long soak: " << stats.missions << " missions, " << stats.commands << " commands, " << stats.refusals
                       << " refusals, " << stats.won << " won");
}
