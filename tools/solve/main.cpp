// bw_solve: decides whether missions can be won, and prints the proof.
//
//   bw_solve <seed> [--budget N] [--no-shove]
//       The verdict, the node count, and for a winnable mission its witness:
//       a log that bw_replay replays to a win.
//   bw_solve --range A-B [--budget N] [--no-shove]
//       One line per seed (seed, verdict, nodes, milliseconds), then a
//       summary. Witnesses are replayed in-process; a witness that does not
//       win is a solver bug and fails the run.
//
// Exit status: 0 done, 1 a witness failed to win, 2 bad usage.

#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"
#include "breakwater/solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

using breakwater::Mission;
using breakwater::SolveOptions;
using breakwater::Verdict;

[[nodiscard]] std::string_view verdict_name(Verdict v) {
    switch (v) {
    case Verdict::Winnable:
        return "winnable";
    case Verdict::Unwinnable:
        return "unwinnable";
    case Verdict::Unproven:
        return "unproven";
    }
    return "unknown";
}

[[nodiscard]] std::optional<std::uint64_t> number(std::string_view s) {
    try {
        std::size_t used = 0;
        const auto value = std::stoull(std::string{s}, &used);
        return used == s.size() ? std::optional{value} : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

/// Replays a witness from the mission's start; true if it is accepted
/// throughout and wins.
[[nodiscard]] bool witness_wins(std::uint64_t seed, const breakwater::SolveResult& result) {
    Mission m = Mission::generate(seed);
    for (const auto& command : result.witness) {
        if (!m.apply(command)) {
            return false;
        }
    }
    return m.outcome() == breakwater::Outcome::Won;
}

int solve_one(std::uint64_t seed, SolveOptions options) {
    const auto result = breakwater::solve(Mission::generate(seed), options);
    std::cout << std::format("# seed {}: {} after {} nodes\n", seed, verdict_name(result.verdict), result.nodes);
    if (result.verdict == Verdict::Winnable) {
        std::cout << breakwater::write_log({.seed = seed, .commands = result.witness});
        return witness_wins(seed, result) ? 0 : 1;
    }
    return 0;
}

int solve_range(std::uint64_t first, std::uint64_t last, SolveOptions options) {
    std::array<std::size_t, 3> counts{};
    std::uint64_t total_nodes = 0;
    std::uint64_t max_nodes = 0;
    int bad_witnesses = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t seed = first; seed <= last; ++seed) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto result = breakwater::solve(Mission::generate(seed), options);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = result.verdict != Verdict::Winnable || witness_wins(seed, result);
        bad_witnesses += ok ? 0 : 1;
        ++counts.at(static_cast<std::size_t>(result.verdict));
        total_nodes += result.nodes;
        max_nodes = std::max(max_nodes, result.nodes);
        std::cout << std::format("{} {} {} {:.1f}{}\n", seed, verdict_name(result.verdict), result.nodes, ms,
                                 ok ? "" : " WITNESS-FAILED");
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << std::format("# {} seeds: {} winnable, {} unwinnable, {} unproven; nodes total {}, max {}; {:.1f}s\n",
                             last - first + 1, counts.at(0), counts.at(1), counts.at(2), total_nodes, max_nodes,
                             seconds);
    return bad_witnesses == 0 ? 0 : 1;
}

int run(std::span<char*> args) {
    SolveOptions options;
    std::optional<std::uint64_t> seed;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> range;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--budget" && has_value) {
            const auto v = number(args[++i]);
            if (!v) {
                return 2;
            }
            options.node_budget = *v;
        } else if (arg == "--no-shove") {
            options.allow_shove = false;
        } else if (arg == "--range" && has_value) {
            const std::string_view r = args[++i];
            const auto dash = r.find('-');
            const auto a = number(r.substr(0, dash));
            const auto b = dash == std::string_view::npos ? std::nullopt : number(r.substr(dash + 1));
            if (!a || !b || *a > *b) {
                return 2;
            }
            range = {*a, *b};
        } else if (const auto v = number(arg); v && !seed) {
            seed = v;
        } else {
            return 2;
        }
    }
    if (range) {
        return solve_range(range->first, range->second, options);
    }
    if (seed) {
        return solve_one(*seed, options);
    }
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const int status = run(std::span(argv, static_cast<std::size_t>(argc)));
        if (status == 2) {
            std::cerr << "usage: bw_solve <seed> | --range A-B  [--budget N] [--no-shove]\n";
        }
        return status;
    } catch (const std::exception& e) {
        std::cerr << "bw_solve: " << e.what() << '\n';
        return 2;
    }
}
