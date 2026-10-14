#include "breakwater/log.hpp"

#include "breakwater/campaign.hpp"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace breakwater {
namespace {

/// Whitespace-separated tokens of one line, comments stripped.
[[nodiscard]] std::vector<std::string_view> tokens_of(std::string_view line) {
    if (const auto hash = line.find('#'); hash != std::string_view::npos) {
        line = line.substr(0, hash);
    }
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            ++i;
        }
        if (i > start) {
            tokens.push_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

/// The whole token as a number of type T, or empty: no sign tricks, no
/// trailing junk, no overflow.
template <typename T> [[nodiscard]] std::optional<T> number(std::string_view token) {
    T value{};
    // from_chars takes an explicit end, so the token needs no terminator.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<Direction> direction(std::string_view token) {
    if (token == "N") {
        return Direction::North;
    }
    if (token == "E") {
        return Direction::East;
    }
    if (token == "S") {
        return Direction::South;
    }
    if (token == "W") {
        return Direction::West;
    }
    return std::nullopt;
}

[[nodiscard]] char direction_letter(Direction dir) {
    switch (dir) {
    case Direction::North:
        return 'N';
    case Direction::East:
        return 'E';
    case Direction::South:
        return 'S';
    case Direction::West:
        return 'W';
    }
    return '?';
}

[[nodiscard]] std::expected<Command, std::string> parse_command(const std::vector<std::string_view>& t) {
    const std::string_view verb = t.front();
    if (verb == "end") {
        if (t.size() != 1) {
            return std::unexpected{"'end' takes no arguments"};
        }
        return EndTurnCommand{};
    }
    if (verb == "move") {
        if (t.size() != 4) {
            return std::unexpected{"'move' takes an engineer, x and y"};
        }
        const auto engineer = number<UnitId>(t[1]);
        const auto x = number<int>(t[2]);
        const auto y = number<int>(t[3]);
        if (!engineer || !x || !y) {
            return std::unexpected{"'move' arguments must be integers in range"};
        }
        return MoveCommand{.engineer = *engineer, .to = {*x, *y}};
    }
    if (verb == "shove") {
        if (t.size() != 3) {
            return std::unexpected{"'shove' takes an engineer and a direction"};
        }
        const auto engineer = number<UnitId>(t[1]);
        if (!engineer) {
            return std::unexpected{"'shove' engineer must be an integer in range"};
        }
        const auto dir = direction(t[2]);
        if (!dir) {
            return std::unexpected{"'shove' direction must be N, E, S or W"};
        }
        return ShoveCommand{.engineer = *engineer, .dir = *dir};
    }
    if (verb == "pull") {
        if (t.size() != 3) {
            return std::unexpected{"'pull' takes an engineer and a direction"};
        }
        const auto engineer = number<UnitId>(t[1]);
        if (!engineer) {
            return std::unexpected{"'pull' engineer must be an integer in range"};
        }
        const auto dir = direction(t[2]);
        if (!dir) {
            return std::unexpected{"'pull' direction must be N, E, S or W"};
        }
        return PullCommand{.engineer = *engineer, .dir = *dir};
    }
    return std::unexpected{"unknown command '" + std::string{verb} + "'"};
}

[[nodiscard]] bool is_header(const std::vector<std::string_view>& t) {
    return t.size() == 2 && t[0] == "breakwater-log";
}

[[nodiscard]] std::optional<std::uint64_t> parse_seed(const std::vector<std::string_view>& t) {
    return t.size() == 2 && t[0] == "seed" ? number<std::uint64_t>(t[1]) : std::nullopt;
}

} // namespace

std::expected<MissionLog, LogError> parse_log(std::string_view text) {
    enum class Expect : std::uint8_t { Header, Seed, Commands };
    Expect expect = Expect::Header;
    MissionLog log;
    int line_number = 0;

    while (!text.empty()) {
        const auto newline = text.find('\n');
        const std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++line_number;

        const auto t = tokens_of(line);
        if (t.empty()) {
            continue;
        }
        const auto fail = [line_number](std::string message) {
            return std::unexpected{LogError{.line = line_number, .message = std::move(message)}};
        };
        switch (expect) {
        case Expect::Header:
            if (!is_header(t)) {
                return fail("expected 'breakwater-log <version>'");
            }
            if (number<int>(t[1]) != kLogVersion) {
                return fail("unsupported log version '" + std::string{t[1]} + "'");
            }
            expect = Expect::Seed;
            break;
        case Expect::Seed: {
            const auto seed = parse_seed(t);
            if (!seed) {
                return fail("expected 'seed <unsigned 64-bit integer>'");
            }
            log.seed = *seed;
            expect = Expect::Commands;
            break;
        }
        case Expect::Commands: {
            auto command = parse_command(t);
            if (!command) {
                return fail(std::move(command.error()));
            }
            log.commands.push_back(*command);
            break;
        }
        }
    }
    if (expect != Expect::Commands) {
        return std::unexpected{
            LogError{.line = line_number,
                     .message = expect == Expect::Header ? "missing 'breakwater-log' header" : "missing 'seed' line"}};
    }
    return log;
}

namespace {

void write_commands(std::string& out, const std::vector<Command>& commands) {
    for (const Command& command : commands) {
        if (const auto* move = std::get_if<MoveCommand>(&command)) {
            out += "move " + std::to_string(move->engineer) + ' ' + std::to_string(move->to.x) + ' ' +
                   std::to_string(move->to.y) + '\n';
        } else if (const auto* shove = std::get_if<ShoveCommand>(&command)) {
            out += "shove " + std::to_string(shove->engineer) + ' ' + direction_letter(shove->dir) + '\n';
        } else if (const auto* pull = std::get_if<PullCommand>(&command)) {
            out += "pull " + std::to_string(pull->engineer) + ' ' + direction_letter(pull->dir) + '\n';
        } else {
            out += "end\n";
        }
    }
}

} // namespace

std::string write_log(const MissionLog& log) {
    std::string out = "breakwater-log " + std::to_string(kLogVersion) + "\nseed " + std::to_string(log.seed) + "\n";
    write_commands(out, log.commands);
    return out;
}

namespace {

/// Splits off the first line of `text`.
[[nodiscard]] std::string_view next_line(std::string_view& text) {
    const auto newline = text.find('\n');
    const std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    return line;
}

/// `campaign <k>` with k in range, or the error.
[[nodiscard]] std::expected<std::uint64_t, std::string> campaign_number(const std::vector<std::string_view>& t) {
    const auto k = t.size() == 2 && t[0] == "campaign" ? number<std::uint64_t>(t[1]) : std::nullopt;
    if (!k || *k >= kCampaignCount) {
        return std::unexpected{"expected 'campaign <0 to " + std::to_string(kCampaignCount - 1) + ">'"};
    }
    return *k;
}

/// A line after the campaign line: a `mission <seed>` that opens the next
/// mission, or a command for the open one. Returns the error, if any.
[[nodiscard]] std::optional<std::string> campaign_body_line(const std::vector<std::string_view>& t, CampaignLog& log,
                                                            const CampaignSeeds& seeds) {
    if (t[0] != "mission") {
        if (log.missions.empty()) {
            return "expected 'mission <seed>' before any command";
        }
        auto command = parse_command(t);
        if (!command) {
            return std::move(command.error());
        }
        log.missions.back().commands.push_back(*command);
        return std::nullopt;
    }
    const auto seed = t.size() == 2 ? number<std::uint64_t>(t[1]) : std::nullopt;
    if (!seed) {
        return "expected 'mission <seed>'";
    }
    if (log.missions.size() == seeds.size()) {
        return "a campaign has " + std::to_string(seeds.size()) + " missions";
    }
    if (const auto expected = seeds.at(log.missions.size()); *seed != expected) {
        return "mission " + std::to_string(log.missions.size() + 1) + " of campaign " + std::to_string(log.campaign) +
               " is seed " + std::to_string(expected);
    }
    log.missions.push_back(MissionLog{.seed = *seed, .commands = {}});
    return std::nullopt;
}

} // namespace

std::expected<CampaignLog, LogError> parse_campaign_log(std::string_view text) {
    enum class Expect : std::uint8_t { Header, Campaign, Missions };
    Expect expect = Expect::Header;
    CampaignLog log;
    CampaignSeeds seeds{};
    int line_number = 0;

    while (!text.empty()) {
        const auto t = tokens_of(next_line(text));
        ++line_number;
        if (t.empty()) {
            continue;
        }
        std::optional<std::string> error;
        switch (expect) {
        case Expect::Header:
            if (t.size() != 2 || t[0] != "breakwater-campaign") {
                error = "expected 'breakwater-campaign <version>'";
            } else if (number<int>(t[1]) != kCampaignLogVersion) {
                error = "unsupported campaign log version '" + std::string{t[1]} + "'";
            }
            expect = Expect::Campaign;
            break;
        case Expect::Campaign:
            if (const auto k = campaign_number(t); k) {
                log.campaign = *k;
                seeds = campaign_seeds(*k);
            } else {
                error = k.error();
            }
            expect = Expect::Missions;
            break;
        case Expect::Missions:
            error = campaign_body_line(t, log, seeds);
            break;
        }
        if (error) {
            return std::unexpected{LogError{.line = line_number, .message = std::move(*error)}};
        }
    }
    if (expect != Expect::Missions) {
        return std::unexpected{LogError{.line = line_number,
                                        .message = expect == Expect::Header ? "missing 'breakwater-campaign' header"
                                                                            : "missing 'campaign' line"}};
    }
    return log;
}

std::string write_campaign_log(const CampaignLog& log) {
    std::string out = "breakwater-campaign " + std::to_string(kCampaignLogVersion) + "\ncampaign " +
                      std::to_string(log.campaign) + "\n";
    for (const MissionLog& mission : log.missions) {
        out += "mission " + std::to_string(mission.seed) + "\n";
        write_commands(out, mission.commands);
    }
    return out;
}

} // namespace breakwater
