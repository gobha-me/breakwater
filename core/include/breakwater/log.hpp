#pragma once

#include "breakwater/mission.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace breakwater {

/// A mission, fully defined: the seed plus every command in order. The text
/// form is the replay and save format:
///
///     breakwater-log 1
///     seed 20
///     move 0 2 6
///     shove 1 N
///     end
///
/// `#` starts a comment; blank lines are ignored. Parsing is strict: anything
/// malformed is an error with its line number, never a guess.
struct MissionLog {
    std::uint64_t seed = 0;
    std::vector<Command> commands;

    friend bool operator==(const MissionLog&, const MissionLog&) = default;
};

inline constexpr int kLogVersion = 1;

struct LogError {
    int line = 0; ///< 1-based.
    std::string message;
};

[[nodiscard]] std::expected<MissionLog, LogError> parse_log(std::string_view text);

/// Canonical text form; parse_log(write_log(log)) == log.
[[nodiscard]] std::string write_log(const MissionLog& log);

} // namespace breakwater
