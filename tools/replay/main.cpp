// bw_replay: replays a mission log and prints the state hash after setup and
// after every turn, then the outcome. Identical logs must print identical
// output on every compiler and build type; tools/replay_check.sh holds us to it.
//
//   bw_replay <log>
//
// Exit status: 0 replayed, 1 a command was refused, 2 bad usage or bad log.

#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"

#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
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

    const auto log = breakwater::parse_log(text.str());
    if (!log) {
        std::cerr << std::format("{}:{}: {}\n", path, log.error().line, log.error().message);
        return 2;
    }

    breakwater::Mission mission = breakwater::Mission::generate(log->seed);
    std::cout << std::format("start {:016x}\n", mission.state_hash());
    int turns_ended = 0;
    for (std::size_t i = 0; i < log->commands.size(); ++i) {
        const auto& command = log->commands[i];
        if (const auto result = mission.apply(command); !result) {
            std::cerr << std::format("{}: command {} refused: {}\n", path, i + 1, breakwater::describe(result.error()));
            return 1;
        }
        if (std::holds_alternative<breakwater::EndTurnCommand>(command)) {
            ++turns_ended;
            std::cout << std::format("end {} {:016x}\n", turns_ended, mission.state_hash());
        }
    }
    std::cout << std::format("outcome {}\n", outcome_name(mission.outcome()));
    return 0;
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
