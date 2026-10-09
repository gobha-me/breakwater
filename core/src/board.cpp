#include "breakwater/board.hpp"

#include "breakwater/rng.hpp"

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
    return (static_cast<std::size_t>(c.y) * static_cast<std::size_t>(kBoardSize)) + static_cast<std::size_t>(c.x);
}

void hash_bytes(std::uint64_t& hash, std::uint64_t value, int byte_count) noexcept {
    constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;
    for (int i = 0; i < byte_count; ++i) {
        hash ^= (value >> (8 * i)) & 0xFFU;
        hash *= kFnvPrime;
    }
}

void hash_int(std::uint64_t& hash, int value) noexcept {
    hash_bytes(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)), 4);
}

} // namespace

Board Board::generate(std::uint64_t seed) {
    Board board;

    // The beach rises inland: row 0 starts at sea level and each row climbs
    // with jitter, so the tide eats the shore unevenly.
    Rng terrain = make_stream(seed, Stream::Terrain);
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const int base = y / 2;
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

std::uint64_t Board::state_hash() const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const Tile& t : tiles_) {
        hash_bytes(hash, t.elevation, 1);
        hash_bytes(hash, t.building_hp, 1);
    }
    hash_int(hash, tide_);
    for (const Unit& u : units_) {
        hash_bytes(hash, static_cast<std::uint64_t>(u.kind), 1);
        hash_int(hash, u.pos.x);
        hash_int(hash, u.pos.y);
        hash_int(hash, u.hp);
    }
    return hash;
}

} // namespace breakwater
