#pragma once

#include "breakwater/board.hpp"

#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>
#include <vector>

namespace breakwater {

// Numbers from docs/RULES.md.
inline constexpr int kEngineerCount = 3;
inline constexpr int kUnitHp = 2;
inline constexpr int kEngineerMoveRange = 2;
inline constexpr int kDeployRow = 4;
inline constexpr int kStartTide = 1;

/// Where an engineer is in its turn: it may move and then shove, and a shove
/// ends its turn.
enum class ActionState : std::uint8_t {
    Ready, ///< Has done nothing this turn.
    Moved, ///< May still shove.
    Done,  ///< Shoved; nothing left this turn.
};

struct MoveCommand {
    UnitId engineer = 0;
    Coord to;
};

struct ShoveCommand {
    UnitId engineer = 0;
    Direction dir = Direction::North;
};

struct EndTurnCommand {};

using Command = std::variant<MoveCommand, ShoveCommand, EndTurnCommand>;

/// Why a command was refused. A refused command changes nothing.
enum class Rejection : std::uint8_t {
    NotAnEngineer,  ///< No such unit, or it is a crawler.
    UnitDead,       ///< The engineer is dead.
    AlreadyMoved,   ///< The engineer moved this turn.
    AlreadyActed,   ///< The engineer shoved this turn.
    Unreachable,    ///< The destination is not in reachable().
    NothingToShove, ///< No living unit on the adjacent tile.
};

[[nodiscard]] constexpr std::string_view describe(Rejection r) noexcept {
    switch (r) {
    case Rejection::NotAnEngineer:
        return "not an engineer";
    case Rejection::UnitDead:
        return "engineer is dead";
    case Rejection::AlreadyMoved:
        return "engineer already moved this turn";
    case Rejection::AlreadyActed:
        return "engineer already shoved this turn";
    case Rejection::Unreachable:
        return "destination is out of reach";
    case Rejection::NothingToShove:
        return "nothing to shove there";
    }
    return "unknown";
}

/// What happened, in order, for the presentation layer to animate. Events are
/// an observation log, not state: they are not part of state_hash().
enum class EventKind : std::uint8_t {
    UnitMoved,       ///< `unit` walked from `from` to `to`.
    UnitShoved,      ///< `unit` was pushed from `from`; `to == from` when blocked.
    UnitDamaged,     ///< `unit` has `value` HP left.
    UnitDied,        ///< `unit` died of damage at `from`.
    UnitDrowned,     ///< `unit` drowned at `to`.
    BuildingDamaged, ///< The building at `to` has `value` HP left.
    TurnStarted,     ///< Turn `value` begins.
};

struct Event {
    EventKind kind = EventKind::TurnStarted;
    UnitId unit = 0;
    Coord from;
    Coord to;
    int value = 0;

    [[nodiscard]] static constexpr Event turn_started(int turn) noexcept {
        return {.kind = EventKind::TurnStarted, .unit = 0, .from = {}, .to = {}, .value = turn};
    }

    friend constexpr bool operator==(const Event&, const Event&) = default;
};

/// One mission: a board, a turn counter, and each engineer's action state,
/// driven only through apply() and observed through queries and events.
class Mission {
public:
    /// Deterministic: the same seed always produces the same mission.
    [[nodiscard]] static Mission generate(std::uint64_t seed);

    /// A mission over a hand-built board, for scenarios and tests. Every unit
    /// starts Ready on turn 1. `seed` drives the mission's later draws.
    [[nodiscard]] static Mission from_board(std::uint64_t seed, Board board);

    [[nodiscard]] const Board& board() const noexcept { return board_; }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    /// 1-based.
    [[nodiscard]] int turn() const noexcept { return turn_; }
    /// Precondition: `id` names a unit.
    [[nodiscard]] ActionState action_state(UnitId id) const;

    /// Tiles the engineer may move to this turn, row-major. Empty when the
    /// engineer may not move: not an engineer, dead, or already acted.
    [[nodiscard]] std::vector<Coord> reachable(UnitId engineer) const;

    /// Applies a command whole, or refuses it and changes nothing.
    std::expected<void, Rejection> apply(const Command& command);

    /// Events since the last call, oldest first.
    [[nodiscard]] std::vector<Event> take_events();

    /// FNV-1a over everything that decides the future: the board, the seed
    /// (later draws derive from it), the turn and the action states.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    Mission(std::uint64_t seed, Board board);

    [[nodiscard]] std::expected<void, Rejection> check_engineer(UnitId id) const;
    std::expected<void, Rejection> apply_move(const MoveCommand& move);
    std::expected<void, Rejection> apply_shove(const ShoveCommand& shove);
    void apply_end_turn();
    void emit_damage(UnitId id);

    Board board_;
    std::uint64_t seed_ = 0;
    int turn_ = 1;
    std::vector<ActionState> actions_; ///< Indexed by UnitId.
    std::vector<Event> events_;
};

} // namespace breakwater
