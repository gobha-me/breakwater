#include "breakwater/solver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_set>
#include <utility>
#include <variant>

namespace breakwater {
namespace {

constexpr std::array kDirections{Direction::North, Direction::East, Direction::South, Direction::West};

/// How good a position looks for the player. Only orders the search; it
/// never decides a verdict.
[[nodiscard]] int score(const Mission& m) {
    if (m.outcome() == Outcome::Won) {
        return 1'000'000;
    }
    if (m.outcome() == Outcome::Lost) {
        return -1'000'000;
    }
    const Board& board = m.board();
    int value = 0;
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        const Unit& u = board.unit(id);
        if (!u.alive()) {
            continue;
        }
        if (u.kind == UnitKind::Engineer) {
            value += 400 + (100 * u.hp);
            // Standing where the next tide will reach is a slow death.
            Board next = board;
            next.advance_tide();
            value -= next.flooded(u.pos) ? 300 : 0;
        } else {
            value -= 60 + (20 * u.hp);
        }
    }
    for (std::size_t i = 0; i < kTileCount; ++i) {
        value += 150 * board.tile(tile_coord(i)).building_hp;
    }
    return value;
}

/// Every command the mission would accept now.
[[nodiscard]] std::vector<Command> legal_commands(const Mission& m, bool allow_shove) {
    std::vector<Command> commands;
    const Board& board = m.board();
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        const Unit& u = board.unit(id);
        if (u.kind != UnitKind::Engineer || !u.alive()) {
            continue;
        }
        for (const Coord to : m.reachable(id)) {
            commands.emplace_back(MoveCommand{.engineer = id, .to = to});
        }
        if (!allow_shove || m.action_state(id) == ActionState::Done) {
            continue;
        }
        for (const Direction dir : kDirections) {
            const Coord target = step(u.pos, dir);
            if (in_bounds(target) && board.unit_at(target)) {
                commands.emplace_back(ShoveCommand{.engineer = id, .dir = dir});
            }
            const Coord far = pull_target(u.pos, dir);
            if (in_bounds(far) && board.unit_at(far)) {
                commands.emplace_back(PullCommand{.engineer = id, .dir = dir});
            }
        }
    }
    commands.emplace_back(EndTurnCommand{});
    return commands;
}

struct Child {
    int priority = 0;
    std::size_t index = 0; ///< Tie-break: generation order, which is deterministic.
    Command command;
    Mission next;
};

class Search {
public:
    explicit Search(SolveOptions options) : options_{options} {}

    // Recursion depth is bounded by the mission: at most kTurnCount turns of
    // three engineers' moves and shoves plus an end turn, about 35 levels.
    // NOLINTNEXTLINE(misc-no-recursion)
    bool run(const Mission& m) {
        if (m.outcome() == Outcome::Won) {
            return true;
        }
        if (m.outcome() == Outcome::Lost) {
            return false;
        }
        if (++nodes_ > options_.node_budget) {
            exhausted_ = true;
            return false;
        }
        const std::uint64_t hash = m.state_hash();
        if (losing_.contains(hash)) {
            return false;
        }

        for (const Child& child : ordered_children(m)) {
            path_.push_back(child.command);
            if (run(child.next)) {
                return true;
            }
            path_.pop_back();
            if (exhausted_) {
                return false; // Incomplete: this position is not known to lose.
            }
        }
        losing_.insert(hash);
        return false;
    }

    [[nodiscard]] std::uint64_t nodes() const { return nodes_; }
    [[nodiscard]] bool exhausted() const { return exhausted_; }
    [[nodiscard]] std::vector<Command> take_path() { return std::exchange(path_, {}); }

private:
    /// Children, best first. Each command is judged by the position after it
    /// and, unless it ends the turn itself, after the enemy phase that would
    /// follow if the player stopped there.
    [[nodiscard]] std::vector<Child> ordered_children(const Mission& m) const {
        std::vector<Child> children;
        const auto commands = legal_commands(m, options_.allow_shove);
        children.reserve(commands.size());
        for (std::size_t i = 0; i < commands.size(); ++i) {
            Mission next = m;
            if (!next.apply(commands[i])) {
                continue; // Cannot happen for a legal command; skip rather than trust it.
            }
            (void)next.take_events();
            int priority = score(next);
            if (!std::holds_alternative<EndTurnCommand>(commands[i]) && next.outcome() == Outcome::InProgress) {
                Mission ahead = next;
                if (ahead.apply(EndTurnCommand{})) {
                    priority = score(ahead);
                }
            }
            children.push_back({.priority = priority, .index = i, .command = commands[i], .next = std::move(next)});
        }
        std::ranges::sort(children, [](const Child& a, const Child& b) {
            return a.priority != b.priority ? a.priority > b.priority : a.index < b.index;
        });
        return children;
    }

    SolveOptions options_;
    std::uint64_t nodes_ = 0;
    bool exhausted_ = false;
    std::vector<Command> path_;
    // Membership only: iteration order never reaches the result.
    std::unordered_set<std::uint64_t> losing_;
};

} // namespace

SolveResult solve(const Mission& mission, SolveOptions options) {
    Search search{options};
    const bool won = search.run(mission);
    SolveResult result;
    result.nodes = search.nodes();
    if (won) {
        result.verdict = Verdict::Winnable;
        result.witness = search.take_path();
    } else {
        result.verdict = search.exhausted() ? Verdict::Unproven : Verdict::Unwinnable;
    }
    return result;
}

} // namespace breakwater
