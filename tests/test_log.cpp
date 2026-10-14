#include "breakwater/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

using breakwater::Direction;
using breakwater::EndTurnCommand;
using breakwater::MissionLog;
using breakwater::MoveCommand;
using breakwater::parse_log;
using breakwater::PullCommand;
using breakwater::ShoveCommand;
using breakwater::write_log;

namespace {

std::string read_log(std::string_view name) {
    std::ifstream in{std::string{BREAKWATER_LOG_DIR} + "/" + std::string{name}, std::ios::binary};
    REQUIRE(in);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("write then parse round-trips every command", "[log]") {
    const MissionLog log{
        .seed = std::numeric_limits<std::uint64_t>::max(),
        .commands = {MoveCommand{.engineer = 0, .to = {2, 6}}, MoveCommand{.engineer = 2, .to = {-1, 8}},
                     ShoveCommand{.engineer = 1, .dir = Direction::North},
                     ShoveCommand{.engineer = 1, .dir = Direction::East},
                     ShoveCommand{.engineer = 1, .dir = Direction::South},
                     ShoveCommand{.engineer = 99, .dir = Direction::West},
                     PullCommand{.engineer = 2, .dir = Direction::North},
                     PullCommand{.engineer = 0, .dir = Direction::West}, EndTurnCommand{}},
    };
    const auto parsed = parse_log(write_log(log));
    REQUIRE(parsed);
    CHECK(*parsed == log);
}

TEST_CASE("the committed logs parse and write back canonically", "[log]") {
    for (const auto* name : {"win-seed20.log", "loss-engineers-seed39.log", "loss-town-seed19.log"}) {
        CAPTURE(name);
        const auto parsed = parse_log(read_log(name));
        REQUIRE(parsed);
        CHECK_FALSE(parsed->commands.empty());
        const auto again = parse_log(write_log(*parsed));
        REQUIRE(again);
        CHECK(*again == *parsed);
    }
}

TEST_CASE("comments, blank lines, tabs and CRLF are tolerated", "[log]") {
    const auto parsed =
        parse_log("# a mission\r\n\r\nbreakwater-log 1 # format\r\n\tseed\t7\r\n\nmove 0 1 2  # go\r\nend\r\n");
    REQUIRE(parsed);
    CHECK(parsed->seed == 7);
    CHECK(parsed->commands.size() == 2);
    CHECK(parsed->commands.front() == breakwater::Command{MoveCommand{.engineer = 0, .to = {1, 2}}});
}

TEST_CASE("malformed logs are refused with the line and the reason", "[log][malformed]") {
    struct Case {
        std::string_view text;
        int line;
        std::string_view message;
    };
    const auto cases = std::to_array<Case>({
        {.text = "", .line = 0, .message = "missing 'breakwater-log' header"},
        {.text = "seed 1\n", .line = 1, .message = "expected 'breakwater-log <version>'"},
        {.text = "breakwater-log 1 extra\n", .line = 1, .message = "expected 'breakwater-log <version>'"},
        {.text = "breakwater-log 2\nseed 1\n", .line = 1, .message = "unsupported log version '2'"},
        {.text = "breakwater-log x\n", .line = 1, .message = "unsupported log version 'x'"},
        {.text = "breakwater-log 1\n", .line = 1, .message = "missing 'seed' line"},
        {.text = "breakwater-log 1\nseed -1\n", .line = 2, .message = "expected 'seed <unsigned 64-bit integer>'"},
        {.text = "breakwater-log 1\nseed 18446744073709551616\n",
         .line = 2,
         .message = "expected 'seed <unsigned 64-bit integer>'"},
        {.text = "breakwater-log 1\nseed 1\njump 0\n", .line = 3, .message = "unknown command 'jump'"},
        {.text = "breakwater-log 1\nseed 1\nmove 0 1\n", .line = 3, .message = "'move' takes an engineer, x and y"},
        {.text = "breakwater-log 1\nseed 1\nmove 0 x 1\n",
         .line = 3,
         .message = "'move' arguments must be integers in range"},
        {.text = "breakwater-log 1\nseed 1\nmove 0 99999999999 1\n",
         .line = 3,
         .message = "'move' arguments must be integers in range"},
        {.text = "breakwater-log 1\nseed 1\nmove -1 2 3\n",
         .line = 3,
         .message = "'move' arguments must be integers in range"},
        {.text = "breakwater-log 1\nseed 1\nmove 0 1 2x\n",
         .line = 3,
         .message = "'move' arguments must be integers in range"},
        {.text = "breakwater-log 1\nseed 1\nshove 0 Q\n",
         .line = 3,
         .message = "'shove' direction must be N, E, S or W"},
        {.text = "breakwater-log 1\nseed 1\nshove 0\n",
         .line = 3,
         .message = "'shove' takes an engineer and a direction"},
        {.text = "breakwater-log 1\nseed 1\nshove z N\n",
         .line = 3,
         .message = "'shove' engineer must be an integer in range"},
        {.text = "breakwater-log 1\nseed 1\npull 0 Q\n", .line = 3, .message = "'pull' direction must be N, E, S or W"},
        {.text = "breakwater-log 1\nseed 1\npull 0 N W\n",
         .line = 3,
         .message = "'pull' takes an engineer and a direction"},
        {.text = "breakwater-log 1\nseed 1\npull -1 N\n",
         .line = 3,
         .message = "'pull' engineer must be an integer in range"},
        {.text = "breakwater-log 1\nseed 1\nend now\n", .line = 3, .message = "'end' takes no arguments"},
    });
    for (const Case& c : cases) {
        CAPTURE(c.text);
        const auto parsed = parse_log(c.text);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().line == c.line);
        CHECK(parsed.error().message == c.message);
    }
}
