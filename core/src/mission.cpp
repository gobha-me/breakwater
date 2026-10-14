#include "breakwater/mission.hpp"

#include "breakwater/reach.hpp"
#include "breakwater/rng.hpp"
#include "fnv.hpp"

#include <algorithm>
#include <array>
#include <cassert>
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
    int target_class = 0; ///< 0 building, 1 engineer.
    int steps = 0;
    Coord dest;
    Direction dir = Direction::North;

    /// The ranking key from docs/RULES.md, "Crawler intent": lowest wins.
    [[nodiscard]] auto key() const { return std::tuple{target_class, steps, dest.y, dest.x, static_cast<int>(dir)}; }
};

/// 0 for a standing building, 1 for a living engineer, empty for anything a
/// crawler would not aim at. The town is what crawlers came for.
[[nodiscard]] std::optional<int> target_class(const Board& board, Coord c) {
    if (board.tile(c).building_hp > 0) {
        return 0;
    }
    if (const auto u = board.unit_at(c); u && board.unit(*u).kind == UnitKind::Engineer) {
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
            if (cls != 0) {
                continue; // Only buildings are worth a detour.
            }
            const Plan plan{.target_class = *cls, .steps = distance.at(i), .dest = dest, .dir = dir};
            if (!best || plan.key() < best->key()) {
                best = plan;
            }
        }
    }
    return best;
}

/// No target in reach: close on the quarry. Ranked by `nearest` (distance to
/// the nearest building or engineer), then steps, then y, then x. With no
/// quarry left, hold position.
template <typename Nearest>
[[nodiscard]] Coord approach(const Board& board, const Distances& distance, Coord from, Nearest nearest) {
    Coord dest = from;
    auto best_key = std::tuple{std::numeric_limits<int>::max(), 0, 0, 0};
    for (std::size_t i = 0; i < kTileCount; ++i) {
        const Coord c = tile_coord(i);
        const auto quarry = nearest(board, c);
        if (distance.at(i) == kUnreachable || !quarry) {
            continue;
        }
        if (const auto key = std::tuple{*quarry, distance.at(i), c.y, c.x}; key < best_key) {
            best_key = key;
            dest = c;
        }
    }
    return dest;
}

/// What a ram from `dest` in `dir` would cost the player, in HP of engineers
/// and buildings, judged on the board as it stands with the Rammer itself
/// (`self`) moved off its tile (docs/RULES.md, "Rammer intent").
[[nodiscard]] int ram_harm(const Board& board, UnitId self, Coord dest, Direction dir) {
    const Coord target = step(dest, dir);
    if (board.tile(target).building_hp > 0) {
        return 1; // Rammed directly: a bump.
    }
    const auto victim = board.unit_at(target);
    if (!victim || *victim == self) {
        return 0;
    }
    const bool engineer = board.unit(*victim).kind == UnitKind::Engineer;
    const Coord landing = step(target, dir);
    if (!in_bounds(landing)) {
        return engineer ? 1 : 0;
    }
    if (board.tile(landing).building_hp > 0) {
        return engineer ? 2 : 1;
    }
    if (const auto blocker = board.unit_at(landing); blocker && *blocker != self) {
        const bool blocker_engineer = board.unit(*blocker).kind == UnitKind::Engineer;
        return (engineer ? 1 : 0) + (blocker_engineer ? 1 : 0);
    }
    if (!engineer) {
        return 0;
    }
    if (board.flooded(landing)) {
        return board.unit(*victim).hp;
    }
    // Shifting an engineer off a guard post next to the town opens the way.
    for (const Direction d : kDirections) {
        const Coord next = step(target, d);
        if (in_bounds(next) && board.tile(next).building_hp > 0) {
            return 1;
        }
    }
    return 0;
}

/// The Rammer's most harmful ram over every reachable destination and
/// direction, or empty if no ram would harm anything.
[[nodiscard]] std::optional<Plan> best_ram(const Board& board, UnitId self, const Distances& distance) {
    std::optional<Plan> best;
    for (std::size_t i = 0; i < kTileCount; ++i) {
        if (distance.at(i) == kUnreachable) {
            continue;
        }
        const Coord dest = tile_coord(i);
        for (const Direction dir : kDirections) {
            if (!in_bounds(step(dest, dir))) {
                continue;
            }
            const int harm = ram_harm(board, self, dest, dir);
            if (harm == 0) {
                continue;
            }
            // Most harm first: the key's first field is lowest-wins.
            const Plan plan{.target_class = -harm, .steps = distance.at(i), .dest = dest, .dir = dir};
            if (!best || plan.key() < best->key()) {
                best = plan;
            }
        }
    }
    return best;
}

} // namespace

Mission::Mission(std::uint64_t seed, Board board, Setup setup)
    : board_{std::move(board)}, seed_{seed}, actions_(board_.unit_count(), ActionState::Ready),
      intents_{std::move(setup.intents)}, spawns_{setup.spawns}, rammers_{setup.rammers} {
    assert(setup.turn >= 1 && setup.turn <= kTurnCount);
    turn_ = setup.turn;
    intents_.resize(board_.unit_count());
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        has_engineers_ = has_engineers_ || board_.unit(id).kind == UnitKind::Engineer;
    }
    for (std::size_t i = 0; i < kTileCount; ++i) {
        town_size_ += board_.tile(tile_coord(i)).building_hp > 0 ? 1 : 0;
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

int Mission::razed() const noexcept {
    int standing = 0;
    for (std::size_t i = 0; i < kTileCount; ++i) {
        standing += board_.tile(tile_coord(i)).building_hp > 0 ? 1 : 0;
    }
    return town_size_ - standing;
}

int Mission::town_falls_after() const noexcept { return std::min(town_size_, kTownFallsAfter); }

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
            (void)check_loss(); // A shove can drown the last engineer or bring the town down.
        }
        return result;
    }
    apply_end_turn();
    return {};
}

std::vector<Event> Mission::take_events() { return std::exchange(events_, {}); }

std::expected<Mission, Rejection> Mission::preview(const Command& command) const {
    Mission next = *this;
    next.events_.clear();
    if (auto result = next.apply(command); !result) {
        return std::unexpected{result.error()};
    }
    return next;
}

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

    actions_.at(shove.engineer) = ActionState::Done;
    push_and_report(*victim, target, shove.dir);
    return {};
}

void Mission::push_and_report(UnitId victim, Coord at, Direction dir) {
    const Coord landing = step(at, dir);
    const auto blocker = in_bounds(landing) ? board_.unit_at(landing) : std::nullopt;
    const PushOutcome outcome = board_.push(victim, dir);
    const Coord after = board_.unit(victim).pos;
    events_.push_back({.kind = EventKind::UnitShoved, .unit = victim, .from = at, .to = after});
    switch (outcome) {
    case PushOutcome::Moved:
    case PushOutcome::NoEffect:
        break;
    case PushOutcome::BlockedByEdge:
        emit_damage(victim);
        break;
    case PushOutcome::BlockedByUnit:
        emit_damage(victim);
        if (blocker) { // Always set: it is what blocked the push.
            emit_damage(*blocker);
        }
        break;
    case PushOutcome::BlockedByBuilding:
        emit_damage(victim);
        events_.push_back({.kind = EventKind::BuildingDamaged,
                           .unit = victim,
                           .from = at,
                           .to = landing,
                           .value = board_.tile(landing).building_hp});
        break;
    case PushOutcome::Drowned:
        events_.push_back({.kind = EventKind::UnitDrowned, .unit = victim, .from = at, .to = after});
        break;
    }
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
    const bool town_fallen = town_size_ > 0 && razed() >= town_falls_after();
    if ((has_engineers_ && !engineer_alive) || town_fallen) {
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
        if (board_.unit(id).kind == UnitKind::Rammer) {
            ram(id, at, *dir);
            continue;
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

void Mission::ram(UnitId id, Coord at, Direction dir) {
    // A ram is a push with the Rammer's weight behind it: a unit there is
    // pushed by Board::push's rules; a building there takes a bump.
    if (const auto victim = board_.unit_at(at)) {
        push_and_report(*victim, at, dir);
    } else if (board_.tile(at).building_hp > 0) {
        board_.damage_building(at, kBumpDamage);
        events_.push_back({.kind = EventKind::BuildingDamaged,
                           .unit = id,
                           .from = board_.unit(id).pos,
                           .to = at,
                           .value = board_.tile(at).building_hp});
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
    const int rammers = rammers_.at(static_cast<std::size_t>(turn_));
    for (int i = 0; i < count; ++i) {
        const auto tiles = spawn_tiles(board_);
        if (tiles.empty()) {
            return;
        }
        const Coord at = tiles.at(rng.below(static_cast<std::uint32_t>(tiles.size())));
        const UnitId id = board_.add_unit(i < rammers ? UnitKind::Rammer : UnitKind::Crawler, at, kUnitHp);
        actions_.push_back(ActionState::Ready);
        intents_.emplace_back(std::nullopt);
        events_.push_back({.kind = EventKind::UnitSpawned, .unit = id, .from = at, .to = at, .value = 0});
    }
}

void Mission::plan_crawlers() {
    for (UnitId id = 0; id < board_.unit_count(); ++id) {
        if (!board_.unit(id).alive()) {
            continue;
        }
        if (board_.unit(id).kind == UnitKind::Crawler) {
            plan_crawler(id);
        } else if (board_.unit(id).kind == UnitKind::Rammer) {
            plan_rammer(id);
        }
    }
}

void Mission::plan_crawler(UnitId id) {
    const Coord from = board_.unit(id).pos;
    const auto distance =
        step_distances(from, kCrawlerMoveRange, [this](Coord c) { return crawler_can_enter(board_, c); });
    // A building in reach is the target. Otherwise the crawler marches on the
    // town, and strikes an engineer only if one is next to where it stops:
    // engineers cannot lure crawlers away from the town.
    const auto plan = best_attack(board_, distance);
    const Coord dest = plan ? plan->dest : approach(board_, distance, from, nearest_building);
    if (dest != from) {
        board_.place(id, dest);
        events_.push_back({.kind = EventKind::UnitMoved, .unit = id, .from = from, .to = dest, .value = 0});
    }
    Intent intent = plan ? Intent{plan->dir} : std::nullopt;
    if (!plan) {
        for (const Direction dir : kDirections) {
            const Coord target = step(dest, dir);
            if (in_bounds(target) && target_class(board_, target) == 1) {
                intent = dir;
                break;
            }
        }
    }
    declare(id, dest, intent);
}

void Mission::plan_rammer(UnitId id) {
    const Coord from = board_.unit(id).pos;
    const auto distance =
        step_distances(from, kCrawlerMoveRange, [this](Coord c) { return crawler_can_enter(board_, c); });
    // The ram that harms most in reach; otherwise the Rammer marches on the
    // town like a crawler and declares nothing.
    const auto plan = best_ram(board_, id, distance);
    const Coord dest = plan ? plan->dest : approach(board_, distance, from, nearest_building);
    if (dest != from) {
        board_.place(id, dest);
        events_.push_back({.kind = EventKind::UnitMoved, .unit = id, .from = from, .to = dest, .value = 0});
    }
    declare(id, dest, plan ? Intent{plan->dir} : std::nullopt);
}

void Mission::declare(UnitId id, Coord at, Intent intent) {
    intents_.at(id) = intent;
    if (intent) {
        events_.push_back({.kind = EventKind::IntentDeclared,
                           .unit = id,
                           .from = at,
                           .to = step(at, *intent),
                           .value = static_cast<int>(*intent)});
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
    for (const int count : rammers_) {
        detail::fnv_int(hash, count);
    }
    detail::fnv_int(hash, town_size_);
    detail::fnv_bytes(hash, static_cast<std::uint64_t>(outcome_), 1);
    return hash;
}

} // namespace breakwater
