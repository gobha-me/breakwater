#pragma once

#include "breakwater/board.hpp"

#include <array>
#include <cstddef>

namespace breakwater {

inline constexpr int kUnreachable = -1;

/// Breadth-first step counts from `from` to every tile within `range`
/// orthogonal steps, entering only tiles `passable(Coord)` accepts. `from` is 0
/// whatever `passable` says of it; everything else out of reach is
/// kUnreachable. Neighbours expand North, East, South, West, so the result
/// never depends on anything but the board.
template <typename Passable>
[[nodiscard]] std::array<int, kTileCount> step_distances(Coord from, int range, const Passable& passable) {
    std::array<int, kTileCount> distance{};
    distance.fill(kUnreachable);
    std::array<Coord, kTileCount> queue{};
    std::size_t head = 0;
    std::size_t tail = 0;

    distance.at(tile_index(from)) = 0;
    queue.at(tail++) = from;
    while (head < tail) {
        const Coord at = queue.at(head++);
        const int next = distance.at(tile_index(at)) + 1;
        if (next > range) {
            continue;
        }
        for (const Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const Coord to = step(at, dir);
            if (!in_bounds(to) || distance.at(tile_index(to)) != kUnreachable || !passable(to)) {
                continue;
            }
            distance.at(tile_index(to)) = next;
            queue.at(tail++) = to;
        }
    }
    return distance;
}

} // namespace breakwater
