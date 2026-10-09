#pragma once

#include "breakwater/board.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

namespace breakwater::gd {

/// Thin Godot-facing view of the core Board. Rules live in core; this class
/// only converts types and validates coordinates coming from scripts.
// GDCLASS declares a copy assignment operator; the rule-of-five members belong to godot-cpp.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class BreakwaterBoard : public godot::RefCounted {
    // Findings inside godot-cpp's GDCLASS expansion, not in this code.
    // NOLINTBEGIN(misc-const-correctness,modernize-use-auto)
    GDCLASS(BreakwaterBoard, godot::RefCounted)
    // NOLINTEND(misc-const-correctness,modernize-use-auto)

public:
    void generate(int64_t seed);
    [[nodiscard]] int64_t get_size() const;
    [[nodiscard]] int64_t get_tide() const;
    void advance_tide();
    [[nodiscard]] int64_t get_elevation(int64_t x, int64_t y) const;
    [[nodiscard]] int64_t get_building_hp(int64_t x, int64_t y) const;
    [[nodiscard]] bool is_flooded(int64_t x, int64_t y) const;
    /// Bit-for-bit copy of the core's uint64 hash into Godot's int64.
    [[nodiscard]] int64_t get_state_hash() const;
    [[nodiscard]] static godot::String core_version();

protected:
    static void _bind_methods();

private:
    Board board_ = Board::generate(0);
};

} // namespace breakwater::gd
