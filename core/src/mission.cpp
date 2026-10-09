#include "breakwater/mission.hpp"

#include "breakwater/reach.hpp"
#include "breakwater/rng.hpp"
#include "fnv.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace breakwater {
namespace {

/// Engineers walk over dry, empty ground only (docs/RULES.md, Player phase).
[[nodiscard]] bool engineer_can_enter(const Board& board, Coord c) {
    return !board.flooded(c) && board.tile(c).building_hp == 0 && !board.unit_at(c).has_value();
}

/// Crawlers swim: anything without a building or a unit (docs/RULES.md, Units).
[[nodiscard]] bool crawler_can_enter(const Board& board, Coord c) {
    return board.tile(c).building_hp == 0 && !board.unit_at(c).has_value();
}

constexpr std::array kDirections{Direction::North, Direction::East, Direction::South, Direction::West};

/// One way a crawler could spend its turn: walk to `dest`, then strike `dir`.
struct Plan {
    int target_class = 0; ///< 0 engineer, 1 building.
    int steps = 0;
    Coord dest;
    Direction dir = Direction::North;

    /// The ranking key from docs/RULES.md, "Crawler intent": lowest wins.
    [[nodiscard]] auto key() const { return std::tuple{target_class, steps, dest.y, dest.x, static_cast<int>(dir)}; }
};

/// 0 for a living engineer, 1 for a standing building, empty for anything a
/// crawler would not aim at.
[[nodiscard]] std::optional<int> target_class(const Board& board, Coord c) {
    if (const auto u = board.unit_at(c); u && board.unit(*u).kind == UnitKind::Engineer) {
        return 0;
    }
    if (board.tile(c).building_hp > 0) {
        return 1;
    }
    return std::nullopt;
}

[[nodiscard]] int manhattan(Coord a, Coord b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

/// Distance to the nearest standing building, or empty if none stands.
[[nodiscard]] std::optional<int> nearest_building(const Board& board, Coord from) {
    std::optional<int> best;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            if (board.tile({x, y}).building_hp > 0) {
                const int d = manhattan(from, {x, y});
                best = best ? std::min(*best, d) : d;
            }
        }
    }
    return best;
}

using Distances = std::array<int, kTileCount>;

/// Where the next crawler may come ashore, sorted by x: the most inland row
/// of flooded, empty tiles, or failing that the empty tiles of row 0.
[[nodiscard]] std::vector<Coord> spawn_tiles(const Board& board) {
    const auto open = [&board](Coord c) { return board.tile(c).building_hp == 0 && !board.unit_at(c).has_value(); };
    for (int y = kBoardSize - 1; y >= 0; --y) {
        std::vector<Coord> row;
        for (int x = 0; x < kBoardSize; ++x) {
            if (board.flooded({x, y}) && open({x, y})) {
                row.push_back({x, y});
            }
        }
        if (!row.empty()) {
            return row;
        }
    }
    std::vector<Coord> edge;
    for (int x = 0; x < kBoardSize; ++x) {
        if (open({x, 0})) {
            edge.push_back({x, 0});
        }
    }
    return edge;
}

/// The best plan over every reachable destination and direction, or empty if
/// no target is in reach. Tiles are visited in key order, so strict-less keeps
/// the first of equals.
[[nodiscard]] std::optional<Plan> best_attack(const Board& board, const Distances& distance) {
    std::optional<Plan> best;
    for (std::size_t i = 0; i < kTileCount; ++i) {
        if (distance.at(i) == kUnreachable) {
            continue;
        }
        const Coord dest = tile_coord(i);
        for (const Direction dir : kDirections) {
            const Coord target = step(dest, dir);
            const auto cls = in_bounds(target) ? target_class(board, target) : std::nullopt;
            if (!cls) {
                continue;
            }
            const Plan plan{.target_class = *cls, .steps = distance.at(i), .dest = dest, .dir = dir};
            if (!best || plan.key() < best->key()) {
                best = plan;
            }
        }
    }
    return best;
}

/// No target in reach: close on the town. Ranked by distance to the nearest
/// standing building, then steps, then y, then x. With no building standing,
/// hold position.
[[nodiscard]] Coord approach_town(const Board& board, const Distances& distance, Coord from) {
    Coord dest = from;
    auto best_key = std::tuple{std::numeric_limits<int>::max(), 0, 0, 0};
    for (std::size_t i = 0; i < kTileCount; ++i) {
        const Coord c = tile_coord(i);
        const auto town = nearest_building(board, c);
        if (distance.at(i) == kUnreachable || !town) {
            continue;
        }
        if (const auto key = std::tuple{*town, distance.at(i), c.y, c.x}; key < best_key) {
            best_key = key;
            dest = c;
        }
    }
    return dest;
}

} // namespace

Mission::Mission(std::uint64_t seed, Board board, Setup setup)
    : board_{std::move(board)}, seed_{seed}, actions_(board_.unit_count(), ActionState::Ready),
      intents_{std::move(setup.intents)}, spawns_{setup.spawns} {
    intents_.resize(board_.unit_count());
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        has_engineers_ = has_engineers_ || board_.unit(id).kind == UnitKind::Engineer;
    }
    for (std::size_t i = 0; i < kTileCount; ++i) {
        has_town_ = has_town_ || board_.tile(tile_coord(i)).building_hp > 0;
    }
}

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

    // Setup is turn 0: the first crawlers come ashore and telegraph, so turn 1
    // opens with intents on the board. Setup is not something to animate, so
    // its events are dropped.
    Mission mission{seed, std::move(board), {}};
    mission.turn_ = 0;
    mission.spawn_crawlers(mission.spawns_.front());
    mission.plan_crawlers();
    mission.turn_ = 1;
    mission.events_.clear();
    return mission;
}

Mission Mission::from_board(std::uint64_t seed, Board board, Setup setup) {
    return Mission{seed, std::move(board), std::move(setup)};
}

ActionState Mission::action_state(UnitId id) const { return actions_.at(id); }

Intent Mission::intent(UnitId id) const { return board_.unit(id).alive() ? intents_.at(id) : std::nullopt; }

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
    if (outcome_ != Outcome::InProgress) {
        return std::unexpected{Rejection::MissionOver};
    }
    if (const auto* move = std::get_if<MoveCommand>(&command)) {
        return apply_move(*move);
    }
    if (const auto* shove = std::get_if<ShoveCommand>(&command)) {
        auto result = apply_shove(*shove);
        if (result) {
            (void)check_loss(); // A shove can drown the last engineer or raze the last building.
        }
        return result;
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
    // The enemy phase, in the order docs/RULES.md gives. A loss ends it on the
    // spot. After the last turn nobody plans for a turn that will never come.
    resolve_attacks();
    if (check_loss()) {
        return;
    }
    raise_tide();
    if (check_loss()) {
        return;
    }
    if (turn_ == kTurnCount) {
        end(Outcome::Won);
        return;
    }
    spawn_crawlers(spawns_.at(static_cast<std::size_t>(turn_)));
    plan_crawlers();
    ++turn_;
    std::ranges::fill(actions_, ActionState::Ready);
    events_.push_back(Event::turn_started(turn_));
}

bool Mission::check_loss() {
    bool engineer_alive = false;
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        const Unit& u = board_.unit(id);
        engineer_alive = engineer_alive || (u.kind == UnitKind::Engineer && u.alive());
    }
    bool building_standing = false;
    for (std::size_t i = 0; i < kTileCount; ++i) {
        building_standing = building_standing || board_.tile(tile_coord(i)).building_hp > 0;
    }
    if ((has_engineers_ && !engineer_alive) || (has_town_ && !building_standing)) {
        end(Outcome::Lost);
        return true;
    }
    return false;
}

void Mission::end(Outcome outcome) {
    outcome_ = outcome;
    std::ranges::fill(intents_, std::nullopt);
    events_.push_back(
        {.kind = EventKind::MissionEnded, .unit = 0, .from = {}, .to = {}, .value = static_cast<int>(outcome)});
}

void Mission::resolve_attacks() {
    // Ascending id is spawn order. Liveness is checked at each crawler's own
    // turn, so a crawler killed earlier in this step never strikes.
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        const Intent dir = std::exchange(intents_.at(id), std::nullopt);
        if (!dir || !board_.unit(id).alive()) {
            continue;
        }
        const Coord from = board_.unit(id).pos;
        const Coord at = step(from, *dir);
        events_.push_back({.kind = EventKind::AttackResolved, .unit = id, .from = from, .to = at, .value = 0});
        if (!in_bounds(at)) {
            continue; // Shoved to the edge and facing off the board: a miss.
        }
        if (const auto victim = board_.unit_at(at)) {
            board_.damage_unit(*victim, kAttackDamage);
            emit_damage(*victim);
        } else if (board_.tile(at).building_hp > 0) {
            board_.damage_building(at, kAttackDamage);
            events_.push_back({.kind = EventKind::BuildingDamaged,
                               .unit = id,
                               .from = from,
                               .to = at,
                               .value = board_.tile(at).building_hp});
        }
    }
}

void Mission::raise_tide() {
    board_.advance_tide();
    events_.push_back({.kind = EventKind::TideRose, .unit = 0, .from = {}, .to = {}, .value = board_.tide()});
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        const Unit& u = board_.unit(id);
        if (u.kind == UnitKind::Engineer && u.alive() && board_.flooded(u.pos)) {
            board_.damage_unit(id, u.hp);
            events_.push_back({.kind = EventKind::UnitDrowned, .unit = id, .from = u.pos, .to = u.pos, .value = 0});
        }
    }
}

void Mission::spawn_crawlers(int count) {
    // One draw per spawn from this turn's stream; candidates are recomputed
    // after each placement (docs/RULES.md, "Spawns").
    Rng rng = make_turn_stream(seed_, Stream::Spawns, turn_);
    for (int i = 0; i < count; ++i) {
        const auto tiles = spawn_tiles(board_);
        if (tiles.empty()) {
            return;
        }
        const Coord at = tiles.at(rng.below(static_cast<std::uint32_t>(tiles.size())));
        const UnitId id = board_.add_unit(UnitKind::Crawler, at, kUnitHp);
        actions_.push_back(ActionState::Ready);
        intents_.emplace_back(std::nullopt);
        events_.push_back({.kind = EventKind::UnitSpawned, .unit = id, .from = at, .to = at, .value = 0});
    }
}

void Mission::plan_crawlers() {
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        if (board_.unit(id).kind == UnitKind::Crawler && board_.unit(id).alive()) {
            plan_crawler(id);
        }
    }
}

void Mission::plan_crawler(UnitId id) {
    const Coord from = board_.unit(id).pos;
    const auto distance =
        step_distances(from, kCrawlerMoveRange, [this](Coord c) { return crawler_can_enter(board_, c); });
    const auto plan = best_attack(board_, distance);
    const Coord dest = plan ? plan->dest : approach_town(board_, distance, from);

    if (dest != from) {
        board_.place(id, dest);
        events_.push_back({.kind = EventKind::UnitMoved, .unit = id, .from = from, .to = dest, .value = 0});
    }
    intents_.at(id) = plan ? Intent{plan->dir} : std::nullopt;
    if (plan) {
        events_.push_back({.kind = EventKind::IntentDeclared,
                           .unit = id,
                           .from = dest,
                           .to = step(dest, plan->dir),
                           .value = static_cast<int>(plan->dir)});
    }
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
    for (UnitId id = 0; id < intents_.size(); ++id) {
        const Intent i = intent(id);
        detail::fnv_bytes(hash, i ? static_cast<std::uint64_t>(*i) : 0xFFU, 1);
    }
    for (const int count : spawns_) {
        detail::fnv_int(hash, count);
    }
    detail::fnv_bytes(hash, static_cast<std::uint64_t>(outcome_), 1);
    return hash;
}

} // namespace breakwater
