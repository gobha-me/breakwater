#pragma once

#include "breakwater/mission.hpp"

#include <cstdint>
#include <vector>

namespace breakwater {

enum class Verdict : std::uint8_t {
    Winnable,   ///< A winning line was found; it is the witness.
    Unwinnable, ///< The search was complete and every line loses.
    Unproven,   ///< The node budget ran out first. Says nothing either way.
};

struct SolveOptions {
    /// Positions the search may visit before giving up with Unproven.
    std::uint64_t node_budget = 1'000'000;
    /// False restricts the player to moving and ending turns, to measure
    /// whether a mission needs a shove at all (#19).
    bool allow_shove = true;
};

struct SolveResult {
    Verdict verdict = Verdict::Unproven;
    /// When Winnable: commands from the given position that win, replayable
    /// with bw_replay. A certificate, not a claim. Empty otherwise.
    std::vector<Command> witness;
    std::uint64_t nodes = 0;
};

/// Decides whether `mission` can be won from its current position.
///
/// Existential depth-first search over single commands, most promising
/// first, stopping at the first win. Positions shown to lose are remembered
/// by state_hash(), so permutations of the same actions are searched once.
/// Deterministic: the same mission and options give the same result.
///
/// A 64-bit hash collision could only prune a branch that wins, turning a
/// Winnable mission into Unwinnable or Unproven. It can never fake a win:
/// every witness is a real line, checkable by replay.
[[nodiscard]] SolveResult solve(const Mission& mission, SolveOptions options = {});

} // namespace breakwater
