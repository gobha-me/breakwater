#pragma once

#include "breakwater/board.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace breakwater {

// Numbers from docs/RULES.md.
inline constexpr int kEngineerCount = 3;
inline constexpr int kUnitHp = 2;
inline constexpr int kEngineerMoveRange = 2;
inline constexpr int kCrawlerMoveRange = 3;
inline constexpr int kAttackDamage = 2;
inline constexpr int kDeployRow = 4;
inline constexpr int kStartTide = 1;
inline constexpr int kTurnCount = 5;

enum class Outcome : std::uint8_t {
    InProgress,
    Won,  ///< The enemy phase of the last turn completed without a loss.
    Lost, ///< Every engineer died, or every building was razed.
};

/// Where an engineer is in its turn: it may move and then shove, and a shove
/// ends its turn.
enum class ActionState : std::uint8_t {
    Ready, ///< Has done nothing this turn.
    Moved, ///< May still shove.
    Done,  ///< Shoved; nothing left this turn.
};

/// Crawlers spawned at setup (index 0) and in the enemy phase of turns 1-4.
/// The last turn has no entry: nothing spawns or plans after it.
using SpawnTable = std::array<int, kTurnCount>;

inline constexpr SpawnTable kSpawnTable{2, 1, 1, 1, 0};

/// A crawler's telegraphed attack: the direction it will strike next enemy
/// phase, from wherever it stands then. Empty means no attack. A direction, not
/// a target, so shoving a crawler retargets it (docs/RULES.md).
using Intent = std::optional<Direction>;

/// Everything a hand-built mission needs besides its board.
struct Setup {
    /// What crawlers have telegraphed, indexed by UnitId. Missing entries mean
    /// no attack.
    std::vector<Intent> intents;
    /// Spawns per turn. Scenarios that must not be disturbed pass zeros.
    SpawnTable spawns = kSpawnTable;
    /// The turn the scenario starts on, 1 to kTurnCount.
    int turn = 1;
};

struct MoveCommand {
    UnitId engineer = 0;
    Coord to;

    friend constexpr bool operator==(const MoveCommand&, const MoveCommand&) = default;
};

struct ShoveCommand {
    UnitId engineer = 0;
    Direction dir = Direction::North;

    friend constexpr bool operator==(const ShoveCommand&, const ShoveCommand&) = default;
};

struct EndTurnCommand {
    friend constexpr bool operator==(EndTurnCommand, EndTurnCommand) = default;
};

using Command = std::variant<MoveCommand, ShoveCommand, EndTurnCommand>;

/// Why a command was refused. A refused command changes nothing.
enum class Rejection : std::uint8_t {
    NotAnEngineer,  ///< No such unit, or it is a crawler.
    UnitDead,       ///< The engineer is dead.
    AlreadyMoved,   ///< The engineer moved this turn.
    AlreadyActed,   ///< The engineer shoved this turn.
    Unreachable,    ///< The destination is not in reachable().
    NothingToShove, ///< No living unit on the adjacent tile.
    MissionOver,    ///< The mission has been won or lost.
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
    case Rejection::MissionOver:
        return "the mission is over";
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
    IntentDeclared,  ///< Crawler `unit` at `from` will strike `to`; `value` is the Direction.
    AttackResolved,  ///< Crawler `unit` at `from` struck `to`, whatever was there.
    TideRose,        ///< The tide is now `value`.
    UnitSpawned,     ///< Crawler `unit` came ashore at `to`.
    MissionEnded,    ///< `value` is the Outcome. Always the last event.
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
    [[nodiscard]] static Mission from_board(std::uint64_t seed, Board board, Setup setup = {});

    [[nodiscard]] const Board& board() const noexcept { return board_; }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    /// 1-based. Stays at the last turn once the mission is won.
    [[nodiscard]] int turn() const noexcept { return turn_; }
    [[nodiscard]] Outcome outcome() const noexcept { return outcome_; }
    /// Precondition: `id` names a unit.
    [[nodiscard]] ActionState action_state(UnitId id) const;
    /// What crawler `id` will strike next enemy phase. Empty for engineers, the
    /// dead, and crawlers with no target. Precondition: `id` names a unit.
    [[nodiscard]] Intent intent(UnitId id) const;

    /// Tiles the engineer may move to this turn, row-major. Empty when the
    /// engineer may not move: not an engineer, dead, or already acted.
    [[nodiscard]] std::vector<Coord> reachable(UnitId engineer) const;

    /// Applies a command whole, or refuses it and changes nothing. Every
    /// command is refused once the mission is over.
    std::expected<void, Rejection> apply(const Command& command);

    /// Events since the last call, oldest first.
    [[nodiscard]] std::vector<Event> take_events();

    /// FNV-1a over everything that decides the future: the board, the seed
    /// (later draws derive from it), the turn, the action states, the intents,
    /// the spawn table and the outcome.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    Mission(std::uint64_t seed, Board board, Setup setup);

    [[nodiscard]] std::expected<void, Rejection> check_engineer(UnitId id) const;
    std::expected<void, Rejection> apply_move(const MoveCommand& move);
    std::expected<void, Rejection> apply_shove(const ShoveCommand& shove);
    void apply_end_turn();
    void resolve_attacks();
    void raise_tide();
    void spawn_crawlers(int count);
    void plan_crawlers();
    void plan_crawler(UnitId id);
    void emit_damage(UnitId id);
    /// Ends the mission as lost if the last engineer or building is gone, and
    /// says whether it did.
    bool check_loss();
    void end(Outcome outcome);

    Board board_;
    std::uint64_t seed_ = 0;
    int turn_ = 1;
    std::vector<ActionState> actions_; ///< Indexed by UnitId.
    std::vector<Intent> intents_;      ///< Indexed by UnitId.
    SpawnTable spawns_ = kSpawnTable;
    Outcome outcome_ = Outcome::InProgress;
    /// Whether the mission started with any engineer or building. Neither can
    /// be created later, so a hand-built board without buildings cannot be
    /// lost by razing them. Generated missions always have both.
    bool has_engineers_ = false;
    bool has_town_ = false;
    std::vector<Event> events_;
};

} // namespace breakwater
