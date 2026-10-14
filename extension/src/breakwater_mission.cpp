#include "breakwater_mission.hpp"

#include "breakwater/seeds.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <variant>

namespace breakwater::gd {
namespace {

static_assert(BreakwaterMission::DIR_NORTH == static_cast<int64_t>(Direction::North));
static_assert(BreakwaterMission::DIR_EAST == static_cast<int64_t>(Direction::East));
static_assert(BreakwaterMission::DIR_SOUTH == static_cast<int64_t>(Direction::South));
static_assert(BreakwaterMission::DIR_WEST == static_cast<int64_t>(Direction::West));
static_assert(BreakwaterMission::OUTCOME_IN_PROGRESS == static_cast<int64_t>(Outcome::InProgress));
static_assert(BreakwaterMission::OUTCOME_WON == static_cast<int64_t>(Outcome::Won));
static_assert(BreakwaterMission::OUTCOME_LOST == static_cast<int64_t>(Outcome::Lost));
static_assert(BreakwaterMission::ACTION_READY == static_cast<int64_t>(ActionState::Ready));
static_assert(BreakwaterMission::ACTION_MOVED == static_cast<int64_t>(ActionState::Moved));
static_assert(BreakwaterMission::ACTION_DONE == static_cast<int64_t>(ActionState::Done));
static_assert(BreakwaterMission::EVENT_UNIT_MOVED == static_cast<int64_t>(EventKind::UnitMoved));
static_assert(BreakwaterMission::EVENT_UNIT_SHOVED == static_cast<int64_t>(EventKind::UnitShoved));
static_assert(BreakwaterMission::EVENT_UNIT_DAMAGED == static_cast<int64_t>(EventKind::UnitDamaged));
static_assert(BreakwaterMission::EVENT_UNIT_DIED == static_cast<int64_t>(EventKind::UnitDied));
static_assert(BreakwaterMission::EVENT_UNIT_DROWNED == static_cast<int64_t>(EventKind::UnitDrowned));
static_assert(BreakwaterMission::EVENT_BUILDING_DAMAGED == static_cast<int64_t>(EventKind::BuildingDamaged));
static_assert(BreakwaterMission::EVENT_TURN_STARTED == static_cast<int64_t>(EventKind::TurnStarted));
static_assert(BreakwaterMission::EVENT_INTENT_DECLARED == static_cast<int64_t>(EventKind::IntentDeclared));
static_assert(BreakwaterMission::EVENT_ATTACK_RESOLVED == static_cast<int64_t>(EventKind::AttackResolved));
static_assert(BreakwaterMission::EVENT_TIDE_ROSE == static_cast<int64_t>(EventKind::TideRose));
static_assert(BreakwaterMission::EVENT_UNIT_SPAWNED == static_cast<int64_t>(EventKind::UnitSpawned));
static_assert(BreakwaterMission::EVENT_MISSION_ENDED == static_cast<int64_t>(EventKind::MissionEnded));

[[nodiscard]] godot::String to_godot(std::string_view s) {
    return godot::String::utf8(s.data(), static_cast<int64_t>(s.size()));
}

[[nodiscard]] std::string to_std(const godot::String& s) {
    const godot::CharString utf8 = s.utf8();
    return {utf8.get_data(), static_cast<std::size_t>(utf8.length())};
}

[[nodiscard]] bool on_board(int64_t x, int64_t y) { return x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize; }

[[nodiscard]] Coord to_coord(int64_t x, int64_t y) { return {static_cast<int>(x), static_cast<int>(y)}; }

/// Saturates a script integer into int range: anything that far off the board
/// is refused by the core as out of reach, so the clamp loses nothing.
[[nodiscard]] int saturate(int64_t v) {
    return static_cast<int>(std::clamp<int64_t>(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

/// Script ids are int64; anything negative maps to an id no unit has, which
/// the core refuses as "not an engineer".
[[nodiscard]] UnitId to_unit_id(int64_t id) {
    return id < 0 ? std::numeric_limits<UnitId>::max() : static_cast<UnitId>(id);
}

[[nodiscard]] godot::Vector2i to_vector(Coord c) { return {c.x, c.y}; }

} // namespace

void BreakwaterMission::generate(int64_t seed) {
    const auto core_seed = std::bit_cast<std::uint64_t>(seed);
    mission_ = Mission::generate(core_seed);
    log_ = MissionLog{.seed = core_seed, .commands = {}};
    undo_.clear();
    undo_blocked_ = "nothing to undo";
}

bool BreakwaterMission::is_offered(int64_t seed) {
    return seed >= 0 && breakwater::is_offered(static_cast<std::uint64_t>(seed));
}

int64_t BreakwaterMission::next_offered(int64_t seed) {
    return static_cast<int64_t>(breakwater::next_offered(seed < 0 ? 0 : static_cast<std::uint64_t>(seed)));
}

godot::String BreakwaterMission::load_log(const godot::String& text) {
    const auto parsed = breakwater::parse_log(to_std(text));
    if (!parsed) {
        return to_godot(std::format("line {}: {}", parsed.error().line, parsed.error().message));
    }
    mission_ = Mission::generate(parsed->seed);
    log_ = MissionLog{.seed = parsed->seed, .commands = {}};
    undo_.clear();
    undo_blocked_ = "nothing to undo";
    for (std::size_t i = 0; i < parsed->commands.size(); ++i) {
        if (const auto refused = apply(parsed->commands[i]); !refused.is_empty()) {
            (void)mission_.take_events();
            return to_godot(std::format("command {} refused: ", i + 1)) + refused;
        }
    }
    (void)mission_.take_events();
    return {};
}

godot::Dictionary BreakwaterMission::parse_log(const godot::String& text) {
    godot::Dictionary out;
    godot::Array commands;
    const auto parsed = breakwater::parse_log(to_std(text));
    out["error"] =
        parsed ? godot::String{} : to_godot(std::format("line {}: {}", parsed.error().line, parsed.error().message));
    out["seed"] = parsed ? std::bit_cast<int64_t>(parsed->seed) : int64_t{0};
    if (parsed) {
        for (const Command& command : parsed->commands) {
            godot::Dictionary d;
            d["engineer"] = int64_t{0};
            d["x"] = 0;
            d["y"] = 0;
            d["dir"] = int64_t{0};
            if (const auto* move = std::get_if<MoveCommand>(&command)) {
                d["verb"] = "move";
                d["engineer"] = static_cast<int64_t>(move->engineer);
                d["x"] = move->to.x;
                d["y"] = move->to.y;
            } else if (const auto* shove = std::get_if<ShoveCommand>(&command)) {
                d["verb"] = "shove";
                d["engineer"] = static_cast<int64_t>(shove->engineer);
                d["dir"] = static_cast<int64_t>(shove->dir);
            } else if (const auto* pull = std::get_if<PullCommand>(&command)) {
                d["verb"] = "pull";
                d["engineer"] = static_cast<int64_t>(pull->engineer);
                d["dir"] = static_cast<int64_t>(pull->dir);
            } else {
                d["verb"] = "end";
            }
            commands.push_back(d);
        }
    }
    out["commands"] = commands;
    return out;
}

godot::String BreakwaterMission::apply(const Command& command) {
    Mission before = mission_;
    if (const auto result = mission_.apply(command); !result) {
        return to_godot(describe(result.error()));
    }
    if (std::holds_alternative<MoveCommand>(command)) {
        undo_.emplace_back(std::move(before), log_.commands.size());
    } else {
        undo_.clear();
        undo_blocked_ =
            std::holds_alternative<EndTurnCommand>(command) ? "the turn has ended" : "a shove or pull cannot be undone";
    }
    log_.commands.push_back(command);
    return {};
}

godot::String BreakwaterMission::undo() {
    if (undo_.empty()) {
        return undo_blocked_;
    }
    auto [mission, log_size] = std::move(undo_.back());
    undo_.pop_back();
    mission_ = std::move(mission);
    log_.commands.resize(log_size);
    return {};
}

bool BreakwaterMission::can_undo() const { return !undo_.empty(); }

godot::String BreakwaterMission::move(int64_t engineer, int64_t x, int64_t y) {
    return apply(MoveCommand{.engineer = to_unit_id(engineer), .to = {saturate(x), saturate(y)}});
}

godot::String BreakwaterMission::shove(int64_t engineer, int64_t dir) {
    if (dir < DIR_NORTH || dir > DIR_WEST) {
        return "unknown direction";
    }
    return apply(ShoveCommand{.engineer = to_unit_id(engineer), .dir = static_cast<Direction>(dir)});
}

godot::String BreakwaterMission::pull(int64_t engineer, int64_t dir) {
    if (dir < DIR_NORTH || dir > DIR_WEST) {
        return "unknown direction";
    }
    return apply(PullCommand{.engineer = to_unit_id(engineer), .dir = static_cast<Direction>(dir)});
}

godot::String BreakwaterMission::end_turn() { return apply(EndTurnCommand{}); }

// Bound as instance methods so scripts call them on the mission they hold.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
int64_t BreakwaterMission::get_size() const { return kBoardSize; }

int64_t BreakwaterMission::get_turn_count() const { return kTurnCount; }

int64_t BreakwaterMission::get_unit_max_hp() const { return kUnitHp; }

int64_t BreakwaterMission::get_building_max_hp() const { return kBuildingHp; }
// NOLINTEND(readability-convert-member-functions-to-static)

int64_t BreakwaterMission::get_seed() const { return std::bit_cast<int64_t>(mission_.seed()); }

int64_t BreakwaterMission::get_turn() const { return mission_.turn(); }

int64_t BreakwaterMission::get_tide() const { return mission_.board().tide(); }

int64_t BreakwaterMission::get_town_size() const { return mission_.town_size(); }

int64_t BreakwaterMission::get_razed() const { return mission_.razed(); }

int64_t BreakwaterMission::get_town_falls_after() const { return mission_.town_falls_after(); }

int64_t BreakwaterMission::get_outcome() const { return static_cast<int64_t>(mission_.outcome()); }

int64_t BreakwaterMission::get_elevation(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!on_board(x, y), -1, "Breakwater: coordinate out of bounds");
    return mission_.board().tile(to_coord(x, y)).elevation;
}

int64_t BreakwaterMission::get_building_hp(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!on_board(x, y), -1, "Breakwater: coordinate out of bounds");
    return mission_.board().tile(to_coord(x, y)).building_hp;
}

bool BreakwaterMission::is_flooded(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!on_board(x, y), false, "Breakwater: coordinate out of bounds");
    return mission_.board().flooded(to_coord(x, y));
}

bool BreakwaterMission::floods_next(int64_t x, int64_t y) const {
    ERR_FAIL_COND_V_MSG(!on_board(x, y), false, "Breakwater: coordinate out of bounds");
    Board next = mission_.board();
    next.advance_tide();
    return next.flooded(to_coord(x, y));
}

godot::Array BreakwaterMission::units_of(const Mission& mission) {
    godot::Array units;
    const Board& board = mission.board();
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        const Unit& u = board.unit(id);
        const Intent intent = mission.intent(id);
        godot::Dictionary d;
        d["id"] = static_cast<int64_t>(id);
        d["engineer"] = u.kind == UnitKind::Engineer;
        d["x"] = u.pos.x;
        d["y"] = u.pos.y;
        d["hp"] = std::max(u.hp, 0);
        d["alive"] = u.alive();
        d["action"] = static_cast<int64_t>(mission.action_state(id));
        d["intent"] = intent ? static_cast<int64_t>(*intent) : int64_t{-1};
        units.push_back(d);
    }
    return units;
}

godot::Array BreakwaterMission::get_units() const { return units_of(mission_); }

godot::TypedArray<godot::Vector2i> BreakwaterMission::reachable(int64_t engineer) const {
    godot::TypedArray<godot::Vector2i> tiles;
    for (const Coord c : mission_.reachable(to_unit_id(engineer))) {
        tiles.push_back(to_vector(c));
    }
    return tiles;
}

godot::Array BreakwaterMission::events_of(const std::vector<Event>& events) {
    godot::Array out;
    for (const Event& e : events) {
        godot::Dictionary d;
        d["kind"] = static_cast<int64_t>(e.kind);
        d["unit"] = static_cast<int64_t>(e.unit);
        d["from"] = to_vector(e.from);
        d["to"] = to_vector(e.to);
        d["value"] = e.value;
        out.push_back(d);
    }
    return out;
}

godot::Array BreakwaterMission::take_events() { return events_of(mission_.take_events()); }

godot::Dictionary BreakwaterMission::preview_shove(int64_t engineer, int64_t dir) const {
    if (dir < DIR_NORTH || dir > DIR_WEST) {
        godot::Dictionary out;
        out["error"] = "unknown direction";
        out["events"] = godot::Array{};
        out["units"] = godot::Array{};
        return out;
    }
    return preview_command(ShoveCommand{.engineer = to_unit_id(engineer), .dir = static_cast<Direction>(dir)});
}

godot::Dictionary BreakwaterMission::preview_pull(int64_t engineer, int64_t dir) const {
    if (dir < DIR_NORTH || dir > DIR_WEST) {
        godot::Dictionary out;
        out["error"] = "unknown direction";
        out["events"] = godot::Array{};
        out["units"] = godot::Array{};
        return out;
    }
    return preview_command(PullCommand{.engineer = to_unit_id(engineer), .dir = static_cast<Direction>(dir)});
}

godot::Dictionary BreakwaterMission::preview_command(const Command& command) const {
    godot::Dictionary out;
    out["error"] = godot::String{};
    out["events"] = godot::Array{};
    out["units"] = godot::Array{};
    auto next = mission_.preview(command);
    if (!next) {
        out["error"] = to_godot(describe(next.error()));
        return out;
    }
    out["events"] = events_of(next->take_events());
    out["units"] = units_of(*next);
    return out;
}

int64_t BreakwaterMission::get_state_hash() const { return std::bit_cast<int64_t>(mission_.state_hash()); }

godot::String BreakwaterMission::get_state_hash_hex() const {
    return to_godot(std::format("{:016x}", mission_.state_hash()));
}

godot::String BreakwaterMission::get_log() const { return to_godot(write_log(log_)); }

void BreakwaterMission::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("generate", "seed"), &BreakwaterMission::generate);
    ClassDB::bind_method(D_METHOD("load_log", "text"), &BreakwaterMission::load_log);
    ClassDB::bind_static_method("BreakwaterMission", D_METHOD("is_offered", "seed"), &BreakwaterMission::is_offered);
    ClassDB::bind_static_method("BreakwaterMission", D_METHOD("next_offered", "seed"),
                                &BreakwaterMission::next_offered);
    ClassDB::bind_static_method("BreakwaterMission", D_METHOD("parse_log", "text"), &BreakwaterMission::parse_log);
    ClassDB::bind_method(D_METHOD("preview_shove", "engineer", "dir"), &BreakwaterMission::preview_shove);
    ClassDB::bind_method(D_METHOD("preview_pull", "engineer", "dir"), &BreakwaterMission::preview_pull);
    ClassDB::bind_method(D_METHOD("move", "engineer", "x", "y"), &BreakwaterMission::move);
    ClassDB::bind_method(D_METHOD("shove", "engineer", "dir"), &BreakwaterMission::shove);
    ClassDB::bind_method(D_METHOD("pull", "engineer", "dir"), &BreakwaterMission::pull);
    ClassDB::bind_method(D_METHOD("end_turn"), &BreakwaterMission::end_turn);
    ClassDB::bind_method(D_METHOD("undo"), &BreakwaterMission::undo);
    ClassDB::bind_method(D_METHOD("can_undo"), &BreakwaterMission::can_undo);
    ClassDB::bind_method(D_METHOD("get_size"), &BreakwaterMission::get_size);
    ClassDB::bind_method(D_METHOD("get_seed"), &BreakwaterMission::get_seed);
    ClassDB::bind_method(D_METHOD("get_turn"), &BreakwaterMission::get_turn);
    ClassDB::bind_method(D_METHOD("get_turn_count"), &BreakwaterMission::get_turn_count);
    ClassDB::bind_method(D_METHOD("get_unit_max_hp"), &BreakwaterMission::get_unit_max_hp);
    ClassDB::bind_method(D_METHOD("get_building_max_hp"), &BreakwaterMission::get_building_max_hp);
    ClassDB::bind_method(D_METHOD("get_tide"), &BreakwaterMission::get_tide);
    ClassDB::bind_method(D_METHOD("get_town_size"), &BreakwaterMission::get_town_size);
    ClassDB::bind_method(D_METHOD("get_razed"), &BreakwaterMission::get_razed);
    ClassDB::bind_method(D_METHOD("get_town_falls_after"), &BreakwaterMission::get_town_falls_after);
    ClassDB::bind_method(D_METHOD("get_outcome"), &BreakwaterMission::get_outcome);
    ClassDB::bind_method(D_METHOD("get_elevation", "x", "y"), &BreakwaterMission::get_elevation);
    ClassDB::bind_method(D_METHOD("get_building_hp", "x", "y"), &BreakwaterMission::get_building_hp);
    ClassDB::bind_method(D_METHOD("is_flooded", "x", "y"), &BreakwaterMission::is_flooded);
    ClassDB::bind_method(D_METHOD("floods_next", "x", "y"), &BreakwaterMission::floods_next);
    ClassDB::bind_method(D_METHOD("get_units"), &BreakwaterMission::get_units);
    ClassDB::bind_method(D_METHOD("reachable", "engineer"), &BreakwaterMission::reachable);
    ClassDB::bind_method(D_METHOD("take_events"), &BreakwaterMission::take_events);
    ClassDB::bind_method(D_METHOD("get_state_hash"), &BreakwaterMission::get_state_hash);
    ClassDB::bind_method(D_METHOD("get_state_hash_hex"), &BreakwaterMission::get_state_hash_hex);
    ClassDB::bind_method(D_METHOD("get_log"), &BreakwaterMission::get_log);

    BIND_CONSTANT(DIR_NORTH);
    BIND_CONSTANT(DIR_EAST);
    BIND_CONSTANT(DIR_SOUTH);
    BIND_CONSTANT(DIR_WEST);
    BIND_CONSTANT(OUTCOME_IN_PROGRESS);
    BIND_CONSTANT(OUTCOME_WON);
    BIND_CONSTANT(OUTCOME_LOST);
    BIND_CONSTANT(ACTION_READY);
    BIND_CONSTANT(ACTION_MOVED);
    BIND_CONSTANT(ACTION_DONE);
    BIND_CONSTANT(EVENT_UNIT_MOVED);
    BIND_CONSTANT(EVENT_UNIT_SHOVED);
    BIND_CONSTANT(EVENT_UNIT_DAMAGED);
    BIND_CONSTANT(EVENT_UNIT_DIED);
    BIND_CONSTANT(EVENT_UNIT_DROWNED);
    BIND_CONSTANT(EVENT_BUILDING_DAMAGED);
    BIND_CONSTANT(EVENT_TURN_STARTED);
    BIND_CONSTANT(EVENT_INTENT_DECLARED);
    BIND_CONSTANT(EVENT_ATTACK_RESOLVED);
    BIND_CONSTANT(EVENT_TIDE_ROSE);
    BIND_CONSTANT(EVENT_UNIT_SPAWNED);
    BIND_CONSTANT(EVENT_MISSION_ENDED);
}

} // namespace breakwater::gd
