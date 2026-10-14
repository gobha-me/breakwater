#include "breakwater/campaign.hpp"

#include <cassert>
#include <cstddef>
#include <format>
#include <utility>

namespace breakwater {

CampaignSeeds campaign_seeds(std::uint64_t k) {
    assert(k < kCampaignCount);
    CampaignSeeds seeds{};
    std::size_t found = 0;
    for (std::uint64_t i = 0; found < seeds.size(); ++i) {
        const std::uint64_t seed = ((k * kCampaignLength) + i) % kOfferedSeeds;
        if (is_offered(seed)) {
            seeds.at(found++) = seed;
        }
    }
    return seeds;
}

int mission_score(const Mission& mission) {
    switch (mission.outcome()) {
    case Outcome::InProgress:
        return 0;
    case Outcome::Lost:
        // A loss on turn t ends it during turn t, so t - 1 turns were held.
        return 10 * (mission.turn() - 1);
    case Outcome::Won:
        break;
    }
    const Board& board = mission.board();
    int engineers = 0;
    int hp = 0;
    for (UnitId id = 0; id < board.unit_count(); ++id) {
        const Unit& u = board.unit(id);
        if (u.kind == UnitKind::Engineer && u.alive()) {
            ++engineers;
            hp += u.hp;
        }
    }
    const int standing = mission.town_size() - mission.razed();
    return 100 + (50 * standing) + (25 * engineers) + (5 * hp);
}

std::expected<CampaignResult, std::string> replay_campaign(const CampaignLog& log) {
    CampaignResult result;
    for (std::size_t i = 0; i < log.missions.size(); ++i) {
        if (result.finished || (i > 0 && result.missions.back().outcome() != Outcome::Won)) {
            return std::unexpected{std::format("mission {} follows one that was not won", i + 1)};
        }
        const MissionLog& m = log.missions.at(i);
        Mission mission = Mission::generate(m.seed);
        for (std::size_t c = 0; c < m.commands.size(); ++c) {
            if (const auto ok = mission.apply(m.commands.at(c)); !ok) {
                return std::unexpected{std::format("mission {}, command {}: {}", i + 1, c + 1, describe(ok.error()))};
            }
        }
        (void)mission.take_events();
        result.score += mission_score(mission);
        result.finished = mission.outcome() == Outcome::Lost ||
                          (mission.outcome() == Outcome::Won && i + 1 == static_cast<std::size_t>(kCampaignLength));
        result.missions.push_back(std::move(mission));
    }
    return result;
}

} // namespace breakwater
