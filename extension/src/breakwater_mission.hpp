#pragma once

#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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
    /// Campaigns: how many there are, and the seeds of campaign `k` (empty
    /// when out of range).
    [[nodiscard]] static int64_t get_campaign_count();
    [[nodiscard]] static godot::PackedInt64Array campaign_seeds(int64_t k);
    /// The campaign whose block of seeds holds `seed` (0 for one out of range).
    [[nodiscard]] static int64_t campaign_of(int64_t seed);
    /// The campaign log for campaign `k` from its missions' logs, in order:
    /// {error, text}. The core checks the seeds and replays it.
    [[nodiscard]] static godot::Dictionary campaign_log(int64_t k, const godot::PackedStringArray& mission_logs);
    /// Parses a campaign log without replaying it: {error, campaign,
    /// missions: [{seed, commands}]}, commands shaped as parse_log() returns.
    [[nodiscard]] static godot::Dictionary parse_campaign_log(const godot::String& text);
    /// What this mission adds to a campaign's score (core/campaign.hpp).
    [[nodiscard]] int64_t get_score() const;
    /// Replays a mission log from its text. Returns "" or the error: a parse
    /// error with its line, or the first refused command. Events from the
    /// replay are dropped: the scene builds the position from queries.
    godot::String load_log(const godot::String& text);

    /// Parses a log's text without applying it, so a scene can step through
    /// it: {error: String, seed: int, commands: [{verb, engineer, x, y, dir}]}.
    /// `error` is "" on success; unused fields are 0.
    [[nodiscard]] static godot::Dictionary parse_log(const godot::String& text);

    /// What a shove would do, without doing it: {error, events, units}, with
    /// events and units shaped as take_events() and get_units() return them.
    /// The core's own preview, so it is the outcome, not an estimate.
    [[nodiscard]] godot::Dictionary preview_shove(int64_t engineer, int64_t dir) const;
    /// The same for a harpoon pull.
    [[nodiscard]] godot::Dictionary preview_pull(int64_t engineer, int64_t dir) const;

    godot::String move(int64_t engineer, int64_t x, int64_t y);
    godot::String shove(int64_t engineer, int64_t dir);
    godot::String pull(int64_t engineer, int64_t dir);
    godot::String end_turn();
    /// Takes back the most recent move, restoring the exact prior state and
    /// dropping it from the log. Moves stack until a shove, a pull or the end
    /// of the turn, which are final. Returns "" or why nothing was undone.
    godot::String undo();
    [[nodiscard]] bool can_undo() const;

    [[nodiscard]] int64_t get_size() const;
    [[nodiscard]] int64_t get_seed() const;
    [[nodiscard]] int64_t get_turn() const;
    [[nodiscard]] int64_t get_turn_count() const;
    /// Full HP of a unit and of a building, for health displays.
    [[nodiscard]] int64_t get_unit_max_hp() const;
    [[nodiscard]] int64_t get_building_max_hp() const;
    [[nodiscard]] int64_t get_tide() const;
    [[nodiscard]] int64_t get_town_size() const;
    [[nodiscard]] int64_t get_razed() const;
    [[nodiscard]] int64_t get_town_falls_after() const;
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
    /// preview_shove() and preview_pull(): `command` already built from
    /// checked arguments.
    [[nodiscard]] godot::Dictionary preview_command(const Command& command) const;

    godot::String apply(const Command& command);
    [[nodiscard]] static godot::Array units_of(const Mission& mission);
    [[nodiscard]] static godot::Array events_of(const std::vector<Event>& events);

    Mission mission_ = Mission::generate(0);
    MissionLog log_;
    /// The mission before each move since the last final command, with the
    /// log length then. Restoring is exact: no rule runs backwards.
    std::vector<std::pair<Mission, std::size_t>> undo_;
    /// Why undo_ is empty, once a shove or an end turn has emptied it.
    godot::String undo_blocked_ = "nothing to undo";
};

} // namespace breakwater::gd
