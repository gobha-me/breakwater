#include "breakwater/campaign.hpp"
#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"
#include "breakwater/seeds.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using breakwater::Board;
using breakwater::CampaignLog;
using breakwater::EndTurnCommand;
using breakwater::Intent;
using breakwater::kCampaignCount;
using breakwater::kCampaignLength;
using breakwater::Mission;
using breakwater::MissionLog;
using breakwater::MoveCommand;
using breakwater::Outcome;
using breakwater::Tile;
using breakwater::UnitKind;

namespace {

std::string read_log(const std::string& name) {
    std::ifstream in{std::string{BREAKWATER_LOG_DIR} + "/" + name};
    REQUIRE(in);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Idles every turn of `seed` to its ending.
MissionLog idle(std::uint64_t seed) {
    MissionLog log{.seed = seed, .commands = {}};
    Mission mission = Mission::generate(seed);
    while (mission.outcome() == Outcome::InProgress) {
        REQUIRE(mission.apply(EndTurnCommand{}));
        log.commands.emplace_back(EndTurnCommand{});
    }
    return log;
}

} // namespace

TEST_CASE("a campaign is the next five offered seeds", "[campaign][seeds]") {
    CHECK(breakwater::campaign_seeds(0) == breakwater::CampaignSeeds{0, 1, 2, 3, 4});
    // Seed 35 is rejected (seeds.hpp), so campaign 7 reaches into the next five.
    REQUIRE_FALSE(breakwater::is_offered(35));
    CHECK(breakwater::campaign_seeds(7) == breakwater::CampaignSeeds{36, 37, 38, 39, 40});
    for (std::uint64_t k = 0; k < kCampaignCount; ++k) {
        CAPTURE(k);
        const auto seeds = breakwater::campaign_seeds(k);
        CHECK(std::ranges::all_of(seeds, [](std::uint64_t s) { return breakwater::is_offered(s); }));
        CHECK(seeds.front() >= k * kCampaignLength);
        CHECK(std::ranges::adjacent_find(seeds, std::ranges::greater_equal{}) == seeds.end());
    }
}

TEST_CASE("a mission's score follows docs/RULES.md", "[campaign][score]") {
    SECTION("in progress scores nothing") { CHECK(breakwater::mission_score(Mission::generate(20)) == 0); }

    SECTION("a loss scores 10 per turn held") {
        Mission mission = Mission::generate(39);
        for (const auto& c : idle(39).commands) {
            REQUIRE(mission.apply(c));
        }
        REQUIRE(mission.outcome() == Outcome::Lost);
        CHECK(breakwater::mission_score(mission) == 10 * (mission.turn() - 1));
    }

    SECTION("a win: 100, 50 a cottage standing, 25 an engineer alive, 5 an HP") {
        Board board;
        for (std::size_t i = 0; i < breakwater::kTileCount; ++i) {
            board.set_tile(breakwater::tile_coord(i), Tile{.elevation = 8});
        }
        board.advance_tide();
        (void)board.add_unit(UnitKind::Engineer, {0, 0}, 2);
        (void)board.add_unit(UnitKind::Engineer, {2, 0}, 1);
        board.set_tile({7, 7}, Tile{.elevation = 8, .building_hp = 2});
        board.set_tile({5, 7}, Tile{.elevation = 8, .building_hp = 1});
        Mission mission = Mission::from_board(1, board, {.intents = {}, .spawns = {}, .turn = breakwater::kTurnCount});
        REQUIRE(mission.apply(EndTurnCommand{}));
        REQUIRE(mission.outcome() == Outcome::Won);
        CHECK(breakwater::mission_score(mission) == 100 + (50 * 2) + (25 * 2) + (5 * 3));
    }
}

TEST_CASE("campaign logs round-trip and are strict", "[campaign][log]") {
    const CampaignLog log{
        .campaign = 7,
        .missions = {{.seed = 36, .commands = {MoveCommand{.engineer = 0, .to = {1, 2}}, EndTurnCommand{}}},
                     {.seed = 37, .commands = {}}},
    };
    const auto parsed = breakwater::parse_campaign_log(breakwater::write_campaign_log(log));
    REQUIRE(parsed);
    CHECK(*parsed == log);

    struct Case {
        std::string text;
        int line;
        std::string message;
    };
    const std::vector<Case> cases{
        {.text = "breakwater-log 1\ncampaign 0\n", .line = 1, .message = "expected 'breakwater-campaign <version>'"},
        {.text = "breakwater-campaign 2\n", .line = 1, .message = "unsupported campaign log version '2'"},
        {.text = "breakwater-campaign 1\ncampaign 200\n", .line = 2, .message = "expected 'campaign <0 to 199>'"},
        {.text = "breakwater-campaign 1\ncampaign 0\nend\n",
         .line = 3,
         .message = "expected 'mission <seed>' before any command"},
        {.text = "breakwater-campaign 1\ncampaign 0\nmission 1\n",
         .line = 3,
         .message = "mission 1 of campaign 0 is seed 0"},
        {.text =
             "breakwater-campaign 1\ncampaign 0\nmission 0\nmission 1\nmission 2\nmission 3\nmission 4\nmission 5\n",
         .line = 8,
         .message = "a campaign has 5 missions"},
        {.text = "breakwater-campaign 1\ncampaign 0\nmission 0\nshove 0 Q\n",
         .line = 4,
         .message = "'shove' direction must be N, E, S or W"},
        {.text = "breakwater-campaign 1\n", .line = 1, .message = "missing 'campaign' line"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto bad = breakwater::parse_campaign_log(c.text);
        REQUIRE_FALSE(bad);
        CHECK(bad.error().line == c.line);
        CHECK(bad.error().message == c.message);
    }
}

TEST_CASE("replaying a campaign enforces the run", "[campaign][replay]") {
    SECTION("the committed campaign wins all five, with the score its hashes record") {
        const auto log = breakwater::parse_campaign_log(read_log("campaigns/campaign-0.log"));
        REQUIRE(log);
        const auto result = breakwater::replay_campaign(*log);
        REQUIRE(result);
        CHECK(result->finished);
        CHECK(result->missions.size() == kCampaignLength);
        CHECK(std::ranges::all_of(result->missions, [](const Mission& m) { return m.outcome() == Outcome::Won; }));
        CHECK(read_log("campaigns/campaign-0.hashes").contains("campaign score " + std::to_string(result->score)));
    }

    SECTION("a loss ends the campaign: nothing may follow it") {
        const auto lost = idle(0);
        const CampaignLog log{.campaign = 0, .missions = {lost, {.seed = 1, .commands = {}}}};
        const auto result = breakwater::replay_campaign(log);
        REQUIRE_FALSE(result);
        CHECK(result.error() == "mission 2 follows one that was not won");

        const auto alone = breakwater::replay_campaign({.campaign = 0, .missions = {lost}});
        REQUIRE(alone);
        CHECK(alone->finished);
        CHECK(alone->score == breakwater::mission_score(alone->missions.front()));
    }

    SECTION("an unfinished mission is not over, and nothing may follow it") {
        const CampaignLog log{.campaign = 0, .missions = {{.seed = 0, .commands = {EndTurnCommand{}}}}};
        const auto result = breakwater::replay_campaign(log);
        REQUIRE(result);
        CHECK_FALSE(result->finished);
        CHECK(result->score == 0);
        const auto more = breakwater::replay_campaign(
            {.campaign = 0, .missions = {log.missions.front(), {.seed = 1, .commands = {}}}});
        CHECK_FALSE(more);
    }

    SECTION("a refused command names its mission and command") {
        const CampaignLog log{.campaign = 0,
                              .missions = {{.seed = 0, .commands = {MoveCommand{.engineer = 9, .to = {0, 0}}}}}};
        const auto result = breakwater::replay_campaign(log);
        REQUIRE_FALSE(result);
        CHECK(result.error() == "mission 1, command 1: not an engineer");
    }
}
