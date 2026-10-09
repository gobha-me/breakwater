#include "breakwater_board.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <bit>
#include <cstdint>

namespace breakwater::gd {
namespace {

[[nodiscard]] bool valid(int64_t x, int64_t y) { return x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize; }

[[nodiscard]] Coord to_coord(int64_t x, int64_t y) { return {static_cast<int>(x), static_cast<int>(y)}; }

} // namespace

void BreakwaterBoard::generate(int64_t seed) { board_ = Board::generate(std::bit_cast<std::uint64_t>(seed)); }

// Bound as an instance method so scripts call board.get_size().
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int64_t BreakwaterBoard::get_size() const { return kBoardSize; }

int64_t BreakwaterBoard::get_tide() const { return board_.tide(); }

void BreakwaterBoard::advance_tide() { board_.advance_tide(); }

int64_t BreakwaterBoard::get_elevation(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!valid(x, y), -1, "Breakwater: coordinate out of bounds");
    return board_.tile(to_coord(x, y)).elevation;
}

int64_t BreakwaterBoard::get_building_hp(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!valid(x, y), -1, "Breakwater: coordinate out of bounds");
    return board_.tile(to_coord(x, y)).building_hp;
}

bool BreakwaterBoard::is_flooded(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!valid(x, y), false, "Breakwater: coordinate out of bounds");
    return board_.flooded(to_coord(x, y));
}

int64_t BreakwaterBoard::get_state_hash() const { return std::bit_cast<int64_t>(board_.state_hash()); }

godot::String BreakwaterBoard::core_version() { return "0.1.0"; }

void BreakwaterBoard::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("generate", "seed"), &BreakwaterBoard::generate);
    ClassDB::bind_method(D_METHOD("get_size"), &BreakwaterBoard::get_size);
    ClassDB::bind_method(D_METHOD("get_tide"), &BreakwaterBoard::get_tide);
    ClassDB::bind_method(D_METHOD("advance_tide"), &BreakwaterBoard::advance_tide);
    ClassDB::bind_method(D_METHOD("get_elevation", "x", "y"), &BreakwaterBoard::get_elevation);
    ClassDB::bind_method(D_METHOD("get_building_hp", "x", "y"), &BreakwaterBoard::get_building_hp);
    ClassDB::bind_method(D_METHOD("is_flooded", "x", "y"), &BreakwaterBoard::is_flooded);
    ClassDB::bind_method(D_METHOD("get_state_hash"), &BreakwaterBoard::get_state_hash);
    ClassDB::bind_static_method("BreakwaterBoard", D_METHOD("core_version"), &BreakwaterBoard::core_version);
}

} // namespace breakwater::gd
