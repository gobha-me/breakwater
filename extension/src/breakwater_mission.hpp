#pragma once

#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstdint>

namespace breakwater::gd {

/// Godot-facing driver for one core Mission. Converts types and validates
/// arguments from scripts; every rule stays in core. Commands return "" when
/// accepted, or the core's reason when refused. Accepted commands are recorded
/// so the game can save a replayable log.
// GDCLASS declares a copy assignment operator; the rule-of-five members belong to godot-cpp.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class BreakwaterMission : public godot::RefCounted {
    // Findings inside godot-cpp's GDCLASS expansion, not in this code.
    // NOLINTBEGIN(misc-const-correctness,modernize-use-auto)
    GDCLASS(BreakwaterMission, godot::RefCounted)
    // NOLINTEND(misc-const-correctness,modernize-use-auto)

public:
    // Mirrors of core enums, bound as class constants so scripts never use
    // bare numbers. Kept equal to the core values by static_assert in the .cpp.
    static constexpr int64_t DIR_NORTH = 0;
    static constexpr int64_t DIR_EAST = 1;
    static constexpr int64_t DIR_SOUTH = 2;
    static constexpr int64_t DIR_WEST = 3;

    static constexpr int64_t OUTCOME_IN_PROGRESS = 0;
    static constexpr int64_t OUTCOME_WON = 1;
    static constexpr int64_t OUTCOME_LOST = 2;

    static constexpr int64_t ACTION_READY = 0;
    static constexpr int64_t ACTION_MOVED = 1;
    static constexpr int64_t ACTION_DONE = 2;

    static constexpr int64_t EVENT_UNIT_MOVED = 0;
    static constexpr int64_t EVENT_UNIT_SHOVED = 1;
    static constexpr int64_t EVENT_UNIT_DAMAGED = 2;
    static constexpr int64_t EVENT_UNIT_DIED = 3;
    static constexpr int64_t EVENT_UNIT_DROWNED = 4;
    static constexpr int64_t EVENT_BUILDING_DAMAGED = 5;
    static constexpr int64_t EVENT_TURN_STARTED = 6;
    static constexpr int64_t EVENT_INTENT_DECLARED = 7;
    static constexpr int64_t EVENT_ATTACK_RESOLVED = 8;
    static constexpr int64_t EVENT_TIDE_ROSE = 9;
    static constexpr int64_t EVENT_UNIT_SPAWNED = 10;
    static constexpr int64_t EVENT_MISSION_ENDED = 11;

    void generate(int64_t seed);
    /// Whether the game offers `seed`: in range and proven winnable.
    [[nodiscard]] static bool is_offered(int64_t seed);
    /// The next offered seed after `seed`, wrapping.
    [[nodiscard]] static int64_t next_offered(int64_t seed);
    /// Replays a mission log from its text. Returns "" or the error: a parse
    /// error with its line, or the first refused command. Events from the
    /// replay are dropped: the scene builds the position from queries.
    godot::String load_log(const godot::String& text);

    /// Parses a log's text without applying it, so a scene can step through
    /// it: {error: String, seed: int, commands: [{verb, engineer, x, y, dir}]}.
    /// `error` is "" on success; unused fields are 0.
    [[nodiscard]] static godot::Dictionary parse_log(const godot::String& text);

    godot::String move(int64_t engineer, int64_t x, int64_t y);
    godot::String shove(int64_t engineer, int64_t dir);
    godot::String end_turn();

    [[nodiscard]] int64_t get_size() const;
    [[nodiscard]] int64_t get_seed() const;
    [[nodiscard]] int64_t get_turn() const;
    [[nodiscard]] int64_t get_turn_count() const;
    [[nodiscard]] int64_t get_tide() const;
    [[nodiscard]] int64_t get_outcome() const;
    [[nodiscard]] int64_t get_elevation(int64_t x, int64_t y) const;
    [[nodiscard]] int64_t get_building_hp(int64_t x, int64_t y) const;
    [[nodiscard]] bool is_flooded(int64_t x, int64_t y) const;
    /// Whether the tile floods when the tide next rises.
    [[nodiscard]] bool floods_next(int64_t x, int64_t y) const;

    /// One Dictionary per unit, dead ones included so ids stay indices:
    /// {id, engineer: bool, x, y, hp, alive: bool, action, intent}, where
    /// intent is a DIR_ constant or -1 for none.
    [[nodiscard]] godot::Array get_units() const;
    [[nodiscard]] godot::TypedArray<godot::Vector2i> reachable(int64_t engineer) const;
    /// Events since the last call, oldest first: {kind, unit, from, to, value}.
    godot::Array take_events();

    /// Bit-for-bit copy of the core's uint64 hash into Godot's int64.
    [[nodiscard]] int64_t get_state_hash() const;
    /// The hash as 16 lowercase hex digits, as bw_replay prints it.
    [[nodiscard]] godot::String get_state_hash_hex() const;
    /// The seed and every accepted command, in the replay format.
    [[nodiscard]] godot::String get_log() const;

protected:
    static void _bind_methods();

private:
    godot::String apply(const Command& command);

    Mission mission_ = Mission::generate(0);
    MissionLog log_;
};

} // namespace breakwater::gd
