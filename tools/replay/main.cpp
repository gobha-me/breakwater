// bw_replay: replays a mission log and prints the state hash after setup and
// after every turn, then the outcome. A campaign log (breakwater-campaign)
// replays each mission the same way, with its score, then the campaign's.
// Identical logs must print identical output on every compiler and build type;
// tools/replay_check.sh holds us to it.
//
//   bw_replay <log>
//
// Exit status: 0 replayed, 1 a command was refused, 2 bad usage or bad log.

#include "breakwater/campaign.hpp"
#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

namespace {

[[nodiscard]] std::string_view outcome_name(breakwater::Outcome outcome) {
    switch (outcome) {
    case breakwater::Outcome::InProgress:
        return "in-progress";
    case breakwater::Outcome::Won:
        return "won";
    case breakwater::Outcome::Lost:
        return "lost";
    }
    return "unknown";
}

/// Replays one mission's commands, printing its hashes and outcome. Returns
/// the mission as it ended, or empty after reporting a refused command.
std::optional<breakwater::Mission> replay_mission(std::string_view path, const breakwater::MissionLog& log) {
    breakwater::Mission mission = breakwater::Mission::generate(log.seed);
    std::cout << std::format("start {:016x}\n", mission.state_hash());
    int turns_ended = 0;
    for (std::size_t i = 0; i < log.commands.size(); ++i) {
        const auto& command = log.commands[i];
        if (const auto result = mission.apply(command); !result) {
            std::cerr << std::format("{}: seed {}, command {} refused: {}\n", path, log.seed, i + 1,
                                     breakwater::describe(result.error()));
            return std::nullopt;
        }
        if (std::holds_alternative<breakwater::EndTurnCommand>(command)) {
            ++turns_ended;
            std::cout << std::format("end {} {:016x}\n", turns_ended, mission.state_hash());
        }
    }
    std::cout << std::format("outcome {}\n", outcome_name(mission.outcome()));
    return mission;
}

int replay_campaign(std::string_view path, const std::string& text) {
    const auto log = breakwater::parse_campaign_log(text);
    if (!log) {
        std::cerr << std::format("{}:{}: {}\n", path, log.error().line, log.error().message);
        return 2;
    }
    // Print as we go, so a refused command shows where it happened; the
    // core's replay_campaign() then decides the score and the run's rules.
    std::cout << std::format("campaign {}\n", log->campaign);
    for (const auto& mission : log->missions) {
        std::cout << std::format("mission {}\n", mission.seed);
        const auto ended = replay_mission(path, mission);
        if (!ended) {
            return 1;
        }
        std::cout << std::format("score {}\n", breakwater::mission_score(*ended));
    }
    const auto result = breakwater::replay_campaign(*log);
    if (!result) {
        std::cerr << std::format("{}: {}\n", path, result.error());
        return 1;
    }
    std::cout << std::format("campaign score {}\ncampaign {}\n", result->score,
                             result->finished ? "finished" : "unfinished");
    return 0;
}

/// Whether the first line that is not blank or a comment is a campaign
/// header.
[[nodiscard]] bool is_campaign(std::string_view text) {
    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        line.remove_prefix(std::min(line.find_first_not_of(" \t\r"), line.size()));
        if (!line.empty() && line.front() != '#') {
            return line.starts_with("breakwater-campaign");
        }
    }
    return false;
}

int replay(std::span<char*> args) {
    if (args.size() != 2) {
        std::cerr << "usage: bw_replay <log>\n";
        return 2;
    }
    const std::string_view path = args[1];
    const std::ifstream in{std::string{path}, std::ios::binary};
    if (!in) {
        std::cerr << std::format("bw_replay: cannot read {}\n", path);
        return 2;
    }
    std::stringstream text;
    text << in.rdbuf();
    if (is_campaign(text.str())) {
        return replay_campaign(path, text.str());
    }

    const auto log = breakwater::parse_log(text.str());
    if (!log) {
        std::cerr << std::format("{}:{}: {}\n", path, log.error().line, log.error().message);
        return 2;
    }
    return replay_mission(path, *log) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return replay(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        std::cerr << "bw_replay: " << e.what() << '\n';
        return 2;
    }
}
