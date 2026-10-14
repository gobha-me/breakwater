// bw_solve: decides whether missions can be won, and prints the proof.
//
//   bw_solve <seed> [--budget N] [--no-shove]
//       The verdict, the node count, and for a winnable mission its witness:
//       a log that bw_replay replays to a win.
//   bw_solve --campaign K
//       A winning campaign log for campaign K: every mission's witness at
//       kProofBudget, replayed as a campaign before it is printed.
//   bw_solve --check-offered
//       Re-runs the proof sweep over the offered seeds at kProofBudget and
//       fails unless the seeds it cannot prove are exactly kRejectedSeeds,
//       and every witness wins (concept criterion 4).
//   bw_solve --range A-B [--budget N] [--no-shove]
//       One line per seed (seed, verdict, nodes, milliseconds, and the
//       witness's shoves and pulls), then a
//       summary. Witnesses are replayed in-process; a witness that does not
//       win is a solver bug and fails the run.
//
// Exit status: 0 done, 1 a witness failed to win, 2 bad usage.

#include "breakwater/campaign.hpp"
#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"
#include "breakwater/seeds.hpp"
#include "breakwater/solver.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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
#include <thread>
#include <utility>
#include <vector>

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
        const auto count = [&result]<typename T>(T /*kind*/) {
            return std::ranges::count_if(result.witness,
                                         [](const breakwater::Command& c) { return std::holds_alternative<T>(c); });
        };
        std::cout << std::format("{} {} {} {:.1f} {} {}{}\n", seed, verdict_name(result.verdict), result.nodes, ms,
                                 count(breakwater::ShoveCommand{}), count(breakwater::PullCommand{}),
                                 ok ? "" : " WITNESS-FAILED");
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << std::format("# {} seeds: {} winnable, {} unwinnable, {} unproven; nodes total {}, max {}; {:.1f}s\n",
                             last - first + 1, counts.at(0), counts.at(1), counts.at(2), total_nodes, max_nodes,
                             seconds);
    return bad_witnesses == 0 ? 0 : 1;
}

int check_offered() {
    // Seeds are independent, so they are solved on every core; the report is
    // written afterwards in seed order, so the output never depends on timing.
    struct Proof {
        Verdict verdict = Verdict::Unproven;
        bool witness_ok = true;
    };
    std::vector<Proof> proofs(breakwater::kOfferedSeeds);
    std::atomic<std::uint64_t> next{0};
    {
        std::vector<std::jthread> workers;
        const unsigned count = std::max(1U, std::thread::hardware_concurrency());
        workers.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            workers.emplace_back([&proofs, &next] {
                for (std::uint64_t seed = next++; seed < breakwater::kOfferedSeeds; seed = next++) {
                    const auto result =
                        breakwater::solve(Mission::generate(seed), {.node_budget = breakwater::kProofBudget});
                    proofs.at(seed) = {.verdict = result.verdict,
                                       .witness_ok = result.verdict != Verdict::Winnable || witness_wins(seed, result)};
                }
            });
        }
    }
    int mismatches = 0;
    std::size_t proven = 0;
    for (std::uint64_t seed = 0; seed < breakwater::kOfferedSeeds; ++seed) {
        const Proof& proof = proofs.at(seed);
        const bool winnable = proof.verdict == Verdict::Winnable;
        if (!proof.witness_ok) {
            std::cout << std::format("seed {}: witness does not win\n", seed);
            ++mismatches;
        } else if (winnable != breakwater::is_offered(seed)) {
            std::cout << std::format("seed {}: solver says {}, kRejectedSeeds says {}\n", seed,
                                     verdict_name(proof.verdict),
                                     breakwater::is_offered(seed) ? "offered" : "rejected");
            ++mismatches;
        }
        proven += winnable ? 1 : 0;
    }
    std::cout << std::format("offered seeds: {} proven with replayed witnesses, {} rejected, {} mismatch(es)\n", proven,
                             breakwater::kRejectedSeeds.size(), mismatches);
    return mismatches == 0 ? 0 : 1;
}

int solve_campaign(std::uint64_t k) {
    if (k >= breakwater::kCampaignCount) {
        return 2;
    }
    breakwater::CampaignLog log{.campaign = k, .missions = {}};
    for (const std::uint64_t seed : breakwater::campaign_seeds(k)) {
        const auto result = breakwater::solve(Mission::generate(seed), {.node_budget = breakwater::kProofBudget});
        if (result.verdict != Verdict::Winnable) {
            std::cerr << std::format("seed {}: {}, though it is offered\n", seed, verdict_name(result.verdict));
            return 1;
        }
        log.missions.push_back({.seed = seed, .commands = result.witness});
    }
    const auto replayed = breakwater::replay_campaign(log);
    if (!replayed || !replayed->finished || replayed->missions.back().outcome() != breakwater::Outcome::Won) {
        std::cerr << "the campaign's witnesses do not win it\n";
        return 1;
    }
    std::cout << std::format("# campaign {}: won, score {}\n", k, replayed->score)
              << breakwater::write_campaign_log(log);
    return 0;
}

/// The modes that are the whole command line, taking no other options; empty
/// for anything else.
std::optional<int> whole_command(std::span<char*> args) {
    if (args.size() == 2 && std::string_view{args[1]} == "--check-offered") {
        return check_offered();
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--campaign") {
        const auto k = number(args[2]);
        return k ? solve_campaign(*k) : 2;
    }
    return std::nullopt;
}

int run(std::span<char*> args) {
    SolveOptions options;
    std::optional<std::uint64_t> seed;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> range;
    if (const auto status = whole_command(args)) {
        return *status;
    }
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
            std::cerr << "usage: bw_solve <seed> | --range A-B | --campaign K | --check-offered"
                         "  [--budget N] [--no-shove]\n";
        }
        return status;
    } catch (const std::exception& e) {
        std::cerr << "bw_solve: " << e.what() << '\n';
        return 2;
    }
}
