#include "breakwater/mission.hpp"

#include "breakwater/reach.hpp"
#include "breakwater/rng.hpp"
#include "fnv.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <utility>

namespace breakwater {
namespace {

/// Engineers walk over dry, empty ground only (docs/RULES.md, Player phase).
[[nodiscard]] bool engineer_can_enter(const Board& board, Coord c) {
    return !board.flooded(c) && board.tile(c).building_hp == 0 && !board.unit_at(c).has_value();
}

} // namespace

Mission::Mission(std::uint64_t seed, Board board)
    : board_{std::move(board)}, seed_{seed}, actions_(board_.unit_count(), ActionState::Ready) {}

Mission Mission::generate(std::uint64_t seed) {
    Board board = Board::generate(seed);
    for (int i = 0; i < kStartTide; ++i) {
        board.advance_tide();
    }

    // Three distinct columns by partial Fisher-Yates over 0..7.
    Rng deploy = make_stream(seed, Stream::Deploy);
    std::array<int, kBoardSize> columns{};
    std::ranges::iota(columns, 0);
    for (std::size_t i = 0; i < kEngineerCount; ++i) {
        const auto remaining = static_cast<std::uint32_t>(columns.size() - i);
        const std::size_t pick = i + deploy.below(remaining);
        std::swap(columns.at(i), columns.at(pick));
        (void)board.add_unit(UnitKind::Engineer, {columns.at(i), kDeployRow}, kUnitHp);
    }
    return Mission{seed, std::move(board)};
}

Mission Mission::from_board(std::uint64_t seed, Board board) { return Mission{seed, std::move(board)}; }

ActionState Mission::action_state(UnitId id) const { return actions_.at(id); }

std::vector<Coord> Mission::reachable(UnitId engineer) const {
    if (!check_engineer(engineer) || actions_.at(engineer) != ActionState::Ready) {
        return {};
    }
    const auto distance = step_distances(board_.unit(engineer).pos, kEngineerMoveRange,
                                         [this](Coord c) { return engineer_can_enter(board_, c); });
    std::vector<Coord> tiles;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            if (distance.at(tile_index({x, y})) > 0) {
                tiles.push_back({x, y});
            }
        }
    }
    return tiles;
}

std::expected<void, Rejection> Mission::apply(const Command& command) {
    if (const auto* move = std::get_if<MoveCommand>(&command)) {
        return apply_move(*move);
    }
    if (const auto* shove = std::get_if<ShoveCommand>(&command)) {
        return apply_shove(*shove);
    }
    apply_end_turn();
    return {};
}

std::vector<Event> Mission::take_events() { return std::exchange(events_, {}); }

std::expected<void, Rejection> Mission::check_engineer(UnitId id) const {
    if (id >= board_.unit_count() || board_.unit(id).kind != UnitKind::Engineer) {
        return std::unexpected{Rejection::NotAnEngineer};
    }
    if (!board_.unit(id).alive()) {
        return std::unexpected{Rejection::UnitDead};
    }
    return {};
}

std::expected<void, Rejection> Mission::apply_move(const MoveCommand& move) {
    if (const auto ok = check_engineer(move.engineer); !ok) {
        return ok;
    }
    switch (actions_.at(move.engineer)) {
    case ActionState::Ready:
        break;
    case ActionState::Moved:
        return std::unexpected{Rejection::AlreadyMoved};
    case ActionState::Done:
        return std::unexpected{Rejection::AlreadyActed};
    }
    if (const auto tiles = reachable(move.engineer); std::ranges::find(tiles, move.to) == tiles.end()) {
        return std::unexpected{Rejection::Unreachable};
    }

    const Coord from = board_.unit(move.engineer).pos;
    board_.place(move.engineer, move.to);
    actions_.at(move.engineer) = ActionState::Moved;
    events_.push_back({.kind = EventKind::UnitMoved, .unit = move.engineer, .from = from, .to = move.to});
    return {};
}

std::expected<void, Rejection> Mission::apply_shove(const ShoveCommand& shove) {
    if (const auto ok = check_engineer(shove.engineer); !ok) {
        return ok;
    }
    if (actions_.at(shove.engineer) == ActionState::Done) {
        return std::unexpected{Rejection::AlreadyActed};
    }
    const Coord target = step(board_.unit(shove.engineer).pos, shove.dir);
    const auto victim = in_bounds(target) ? board_.unit_at(target) : std::nullopt;
    if (!victim) {
        return std::unexpected{Rejection::NothingToShove};
    }

    const Coord landing = step(target, shove.dir);
    const auto blocker = in_bounds(landing) ? board_.unit_at(landing) : std::nullopt;
    actions_.at(shove.engineer) = ActionState::Done;

    const PushOutcome outcome = board_.push(*victim, shove.dir);
    const Coord after = board_.unit(*victim).pos;
    events_.push_back({.kind = EventKind::UnitShoved, .unit = *victim, .from = target, .to = after});
    switch (outcome) {
    case PushOutcome::Moved:
    case PushOutcome::NoEffect:
        break;
    case PushOutcome::BlockedByEdge:
        emit_damage(*victim);
        break;
    case PushOutcome::BlockedByUnit:
        emit_damage(*victim);
        if (blocker) { // Always set: it is what blocked the push.
            emit_damage(*blocker);
        }
        break;
    case PushOutcome::BlockedByBuilding:
        emit_damage(*victim);
        events_.push_back({.kind = EventKind::BuildingDamaged,
                           .unit = *victim,
                           .from = target,
                           .to = landing,
                           .value = board_.tile(landing).building_hp});
        break;
    case PushOutcome::Drowned:
        events_.push_back({.kind = EventKind::UnitDrowned, .unit = *victim, .from = target, .to = after});
        break;
    }
    return {};
}

void Mission::apply_end_turn() {
    // The enemy phase (docs/RULES.md) lands here: attacks, tide, spawns, and
    // crawler plans, in that order.
    ++turn_;
    std::ranges::fill(actions_, ActionState::Ready);
    events_.push_back(Event::turn_started(turn_));
}

void Mission::emit_damage(UnitId id) {
    const Unit& u = board_.unit(id);
    events_.push_back(
        {.kind = EventKind::UnitDamaged, .unit = id, .from = u.pos, .to = u.pos, .value = std::max(u.hp, 0)});
    if (!u.alive()) {
        events_.push_back({.kind = EventKind::UnitDied, .unit = id, .from = u.pos, .to = u.pos});
    }
}

std::uint64_t Mission::state_hash() const noexcept {
    std::uint64_t hash = board_.state_hash();
    detail::fnv_bytes(hash, seed_, 8);
    detail::fnv_int(hash, turn_);
    for (const ActionState a : actions_) {
        detail::fnv_bytes(hash, static_cast<std::uint64_t>(a), 1);
    }
    return hash;
}

} // namespace breakwater
