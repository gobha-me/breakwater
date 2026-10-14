#pragma once

#include "breakwater/log.hpp"
#include "breakwater/mission.hpp"
#include "breakwater/seeds.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace breakwater {

/// A campaign is this many offered missions in a row (docs/RULES.md,
/// "Campaign").
inline constexpr int kCampaignLength = 5;
/// Campaigns 0 to kCampaignCount - 1 start inside the offered range.
inline constexpr std::uint64_t kCampaignCount = kOfferedSeeds / kCampaignLength;

using CampaignSeeds = std::array<std::uint64_t, kCampaignLength>;

/// The missions of campaign `k`: the first kCampaignLength offered seeds from
/// k * kCampaignLength, wrapping within the offered range. Precondition:
/// k < kCampaignCount.
[[nodiscard]] CampaignSeeds campaign_seeds(std::uint64_t k);

/// What a mission adds to the campaign score: 0 while it is in progress
/// (docs/RULES.md, "Campaign").
[[nodiscard]] int mission_score(const Mission& mission);

/// A campaign replayed from its log.
struct CampaignResult {
    std::vector<Mission> missions; ///< One per mission in the log, as it ended.
    int score = 0;
    /// True when the campaign is over: a mission was lost, or the last won.
    bool finished = false;
};

/// Replays a campaign log. Fails on a command a mission refuses, and on a
/// mission that follows one that was not won.
[[nodiscard]] std::expected<CampaignResult, std::string> replay_campaign(const CampaignLog& log);

} // namespace breakwater
