#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace breakwater {

inline constexpr int kBoardSize = 8;
inline constexpr std::size_t kTileCount = static_cast<std::size_t>(kBoardSize) * kBoardSize;

/// Row 0 is the sea edge; y grows inland.
struct Coord {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(Coord, Coord) = default;
};

enum class Direction : std::uint8_t { North, East, South, West };

[[nodiscard]] constexpr Coord step(Coord from, Direction dir) noexcept {
    switch (dir) {
    case Direction::North:
        return {from.x, from.y - 1};
    case Direction::East:
        return {from.x + 1, from.y};
    case Direction::South:
        return {from.x, from.y + 1};
    case Direction::West:
        return {from.x - 1, from.y};
    }
    return from;
}

[[nodiscard]] constexpr bool in_bounds(Coord c) noexcept {
    return c.x >= 0 && c.x < kBoardSize && c.y >= 0 && c.y < kBoardSize;
}

struct Tile {
    std::uint8_t elevation = 1;
    std::uint8_t building_hp = 0; ///< 0 means no building.
};

enum class UnitKind : std::uint8_t {
    Engineer, ///< Player unit. Cannot swim: drowns on a flooded tile.
    Crawler,  ///< Sea creature. Amphibious.
};

using UnitId = std::size_t;

struct Unit {
    UnitKind kind = UnitKind::Engineer;
    Coord pos;
    int hp = 0;

    [[nodiscard]] constexpr bool alive() const noexcept { return hp > 0; }
};

enum class PushOutcome : std::uint8_t {
    Moved,
    BlockedByEdge,     ///< Pushed unit takes 1 bump damage.
    BlockedByUnit,     ///< Both units take 1 bump damage.
    BlockedByBuilding, ///< Pushed unit and building each take 1 damage.
    Drowned,           ///< Moved onto a flooded tile it cannot survive.
    NoEffect,          ///< Unit was already dead.
};

class Board {
public:
    /// Deterministic: the same seed always produces the same board.
    [[nodiscard]] static Board generate(std::uint64_t seed);

    [[nodiscard]] const Tile& tile(Coord c) const;
    [[nodiscard]] int tide() const noexcept { return tide_; }
    [[nodiscard]] bool flooded(Coord c) const;
    void advance_tide() noexcept { ++tide_; }

    /// Precondition: c is in bounds, has no building and no living unit.
    UnitId add_unit(UnitKind kind, Coord c, int hp);
    [[nodiscard]] const Unit& unit(UnitId id) const;
    [[nodiscard]] std::size_t unit_count() const noexcept { return units_.size(); }
    [[nodiscard]] std::optional<UnitId> unit_at(Coord c) const;

    /// Pushes a unit one tile. Resolves bump damage and drowning.
    PushOutcome push(UnitId id, Direction dir);

    /// FNV-1a over the full game state. Equal states hash equal; used by
    /// determinism and replay tests.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    [[nodiscard]] Tile& tile_mut(Coord c);

    std::array<Tile, kTileCount> tiles_{};
    std::vector<Unit> units_;
    int tide_ = 0;
};

} // namespace breakwater
