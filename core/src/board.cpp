#include "breakwater/board.hpp"

#include "breakwater/rng.hpp"
#include "fnv.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace breakwater {
namespace {

constexpr int kBuildingCount = 3;
constexpr std::uint8_t kBuildingHp = 2;
constexpr int kTownFirstRow = 5;

[[nodiscard]] std::size_t index_of(Coord c) {
    assert(in_bounds(c));
    return tile_index(c);
}

} // namespace

Board Board::generate(std::uint64_t seed) {
    Board board;

    // The beach rises one level per row, with jitter, so a tide rising one
    // level per turn eats about a row per turn, unevenly (docs/RULES.md).
    Rng terrain = make_stream(seed, Stream::Terrain);
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const int base = y;
            const int jitter = static_cast<int>(terrain.below(2));
            board.tile_mut({x, y}).elevation = static_cast<std::uint8_t>(base + jitter);
        }
    }

    Rng buildings = make_stream(seed, Stream::Buildings);
    int placed = 0;
    while (placed < kBuildingCount) {
        const Coord c{
            static_cast<int>(buildings.below(kBoardSize)),
            kTownFirstRow + static_cast<int>(buildings.below(kBoardSize - kTownFirstRow)),
        };
        Tile& t = board.tile_mut(c);
        if (t.building_hp == 0) {
            t.building_hp = kBuildingHp;
            ++placed;
        }
    }
    return board;
}

const Tile& Board::tile(Coord c) const { return tiles_.at(index_of(c)); }

Tile& Board::tile_mut(Coord c) { return tiles_.at(index_of(c)); }

void Board::set_tile(Coord c, Tile t) { tile_mut(c) = t; }

bool Board::flooded(Coord c) const { return std::cmp_less(tile(c).elevation, tide_); }

UnitId Board::add_unit(UnitKind kind, Coord c, int hp) {
    assert(in_bounds(c));
    assert(tile(c).building_hp == 0);
    assert(!unit_at(c).has_value());
    assert(hp > 0);
    units_.push_back(Unit{.kind = kind, .pos = c, .hp = hp});
    return units_.size() - 1;
}

const Unit& Board::unit(UnitId id) const { return units_.at(id); }

void Board::place(UnitId id, Coord to) {
    assert(units_.at(id).alive());
    assert(in_bounds(to));
    assert(tile(to).building_hp == 0);
    assert(!unit_at(to).has_value());
    units_.at(id).pos = to;
}

std::optional<UnitId> Board::unit_at(Coord c) const {
    const auto it = std::ranges::find_if(units_, [c](const Unit& u) { return u.alive() && u.pos == c; });
    if (it == units_.end()) {
        return std::nullopt;
    }
    return static_cast<UnitId>(it - units_.begin());
}

PushOutcome Board::push(UnitId id, Direction dir) {
    Unit& pushed = units_.at(id);
    if (!pushed.alive()) {
        return PushOutcome::NoEffect;
    }

    const Coord target = step(pushed.pos, dir);
    if (!in_bounds(target)) {
        pushed.hp -= 1;
        return PushOutcome::BlockedByEdge;
    }

    if (const auto blocker = unit_at(target)) {
        pushed.hp -= 1;
        units_.at(*blocker).hp -= 1;
        return PushOutcome::BlockedByUnit;
    }

    Tile& target_tile = tile_mut(target);
    if (target_tile.building_hp > 0) {
        pushed.hp -= 1;
        target_tile.building_hp = static_cast<std::uint8_t>(target_tile.building_hp - 1);
        return PushOutcome::BlockedByBuilding;
    }

    pushed.pos = target;
    if (pushed.kind == UnitKind::Engineer && flooded(target)) {
        pushed.hp = 0;
        return PushOutcome::Drowned;
    }
    return PushOutcome::Moved;
}

void Board::damage_unit(UnitId id, int amount) {
    assert(units_.at(id).alive());
    assert(amount > 0);
    units_.at(id).hp -= amount;
}

void Board::damage_building(Coord c, int amount) {
    Tile& t = tile_mut(c);
    assert(t.building_hp > 0);
    assert(amount > 0);
    t.building_hp = static_cast<std::uint8_t>(std::max(0, t.building_hp - amount));
}

std::uint64_t Board::state_hash() const noexcept {
    std::uint64_t hash = detail::kFnvOffset;
    for (const Tile& t : tiles_) {
        detail::fnv_bytes(hash, t.elevation, 1);
        detail::fnv_bytes(hash, t.building_hp, 1);
    }
    detail::fnv_int(hash, tide_);
    for (const Unit& u : units_) {
        detail::fnv_bytes(hash, static_cast<std::uint64_t>(u.kind), 1);
        detail::fnv_int(hash, u.pos.x);
        detail::fnv_int(hash, u.pos.y);
        detail::fnv_int(hash, u.hp);
    }
    return hash;
}

} // namespace breakwater
