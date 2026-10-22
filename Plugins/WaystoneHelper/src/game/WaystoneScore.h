#pragma once
// What one waystone shows under the current settings: badges, border, and the
// lines of the hover breakdown. Computed once per scan (or when the settings
// change), never per frame; the overlay only draws the strings built here.

#include "WaystoneData.h"
#include "WaystoneScanner.h"
#include "../config/Settings.h"

#include <string>
#include <vector>

namespace WaystoneHelper {

struct Badge {
    std::string text;
    float color[4] = {1.f, 1.f, 1.f, 1.f};
};

struct GroupMatch {
    std::string name;
    float color[4] = {1.f, 1.f, 1.f, 1.f};
    int matched = 0;
    int selected = 0;
    std::vector<std::string> matchedLabels;

    std::string BadgeLabel() const {
        if (selected <= 1) return std::to_string(matched);
        return std::to_string(matched) + "/" + std::to_string(selected);
    }
};

struct BreakdownLine {
    std::string text;
    float color[4] = {1.f, 1.f, 1.f, 1.f};
    bool header = false;   // section heading (drawn in the secondary colour)
    bool indent = false;
};

struct Score {
    bool matched = false;              // anything to draw on the item
    bool hasAffixCountBadge = false;
    bool border = false;
    bool groupMatched = false;         // an affix group matched (also when its badge is hidden)
    float borderColor[4] = {1.f, 1.f, 1.f, 1.f};
    Badge affixCount;                  // valid when hasAffixCountBadge
    std::vector<Badge> leftBadges;     // affix groups
    std::vector<Badge> rightBadges;    // map stats
    std::vector<BreakdownLine> breakdown;
};

inline const char* RarityName(int rarity) {
    switch (rarity) {
        case 0: return "Normal";
        case 1: return "Magic";
        case 2: return "Rare";
        case 3: return "Unique";
        default: return "?";
    }
}

inline void CopyColor(float dst[4], const float src[4]) {
    for (int i = 0; i < 4; ++i) dst[i] = src[i];
}

inline bool AffixPresent(const Affix& a, const std::unordered_set<std::string>& matchKeys) {
    for (const auto& id : a.NormModIds)
        if (matchKeys.count(id)) return true;
    return false;
}

inline std::vector<GroupMatch> EvaluateGroups(const WaystoneHelperConfig::Settings& s, const WaystoneData& data,
                                              const ParsedWaystone& t) {
    std::vector<GroupMatch> out;
    for (const auto& g : s.affixGroups) {
        if (!g.enabled || g.selectedAffixIds.empty()) continue;
        GroupMatch gm;
        gm.name = g.name;
        CopyColor(gm.color, g.color);
        for (const auto& affixId : g.selectedAffixIds) {
            const Affix* a = data.FindAffix(affixId);
            if (!a) continue;
            ++gm.selected;
            if (AffixPresent(*a, t.matchKeys)) {
                ++gm.matched;
                gm.matchedLabels.push_back(a->Short);
            }
        }
        if (gm.selected > 0 && gm.matched >= g.minMatched) out.push_back(std::move(gm));
    }
    return out;
}

inline bool RuleHasConditions(const WaystoneHelperConfig::BorderRule& r) {
    if (r.minTier > 0 || r.minAffixes > 0) return true;
    if (r.corrupted != WaystoneHelperConfig::kCorruptedAny || r.rarity >= 0) return true;
    for (const auto& kv : r.statMins)
        if (kv.second > 0) return true;
    return false;
}

// True when the waystone meets every condition of the rule; `met` lists them.
inline bool RuleMatches(const WaystoneHelperConfig::BorderRule& r, const WaystoneData& data,
                        const ParsedWaystone& t, std::vector<std::string>& met) {
    met.clear();
    for (const auto& gs : data.GeneratedStats()) {
        const int min = r.StatMin(gs.statId);
        if (min <= 0) continue;
        if (!t.modsRead) return false;
        auto it = t.statValues.find(gs.statId);
        const int val = it == t.statValues.end() ? 0 : it->second;
        if (val < min) return false;
        met.push_back(gs.displayName + " " + std::to_string(val) + " (at least " + std::to_string(min) + ")");
    }
    if (r.minTier > 0) {
        if (t.tier < r.minTier) return false;
        met.push_back("Tier " + std::to_string(t.tier) + " (at least " + std::to_string(r.minTier) + ")");
    }
    if (r.minAffixes > 0) {
        if (!t.modsRead || t.affixCount < r.minAffixes) return false;
        met.push_back(std::to_string(t.affixCount) + " affixes (at least " + std::to_string(r.minAffixes) + ")");
    }
    if (r.corrupted == WaystoneHelperConfig::kCorruptedOnly) {
        if (!t.corrupted) return false;
        met.push_back("Corrupted");
    } else if (r.corrupted == WaystoneHelperConfig::kCorruptedNever) {
        if (t.corrupted) return false;
        met.push_back("Not corrupted");
    }
    if (r.rarity >= 0) {
        if (t.rarity != r.rarity) return false;
        met.push_back(RarityName(t.rarity));
    }
    return true;
}

inline Score Evaluate(const WaystoneHelperConfig::Settings& s, const WaystoneData& data,
                      const ParsedWaystone& t, const std::string& title) {
    Score e;
    static constexpr float kWhite[4] = {0.94f, 0.95f, 0.96f, 1.f};
    static constexpr float kMuted[4] = {0.68f, 0.71f, 0.75f, 1.f};
    auto line = [&](std::string text, const float* color, bool header = false, bool indent = false) {
        BreakdownLine l;
        l.text = std::move(text);
        CopyColor(l.color, color ? color : kWhite);
        l.header = header;
        l.indent = indent;
        e.breakdown.push_back(std::move(l));
    };

    if (!title.empty()) line(title, kWhite);
    {
        std::string info = t.tier > 0 ? "Tier " + std::to_string(t.tier) + "  \xC2\xB7  " : std::string();
        info += RarityName(t.rarity);
        if (t.corrupted) info += "  \xC2\xB7  Corrupted";
        line(std::move(info), kMuted);
    }

    // Affix count.
    if (t.modsRead) {
        const bool reached = t.affixCount >= s.targetAffixCount;
        e.hasAffixCountBadge = s.showAffixCountBadge && reached;
        if (e.hasAffixCountBadge) {
            e.affixCount.text = std::to_string(t.affixCount);
            CopyColor(e.affixCount.color, s.affixCountBadgeColor);
        }
        line("Affixes: " + std::to_string(t.affixCount) + " (target " + std::to_string(s.targetAffixCount) + ")",
             reached ? s.affixCountBadgeColor : kMuted);
    } else {
        line(s.readMods ? "Reading mods..." : "Reading mods is off", kMuted);
    }

    // Map-stat badges (all present stats are listed in the breakdown).
    bool statHeader = false;
    for (const auto& g : data.GeneratedStats()) {
        auto it = t.statValues.find(g.statId);
        if (it == t.statValues.end() || it->second <= 0) continue;
        const float* color = s.StatColor(g.statId);
        if (!color) color = kWhite;
        if (!statHeader) { line("Map stats", kMuted, true); statHeader = true; }
        line(g.displayName + "  " + std::to_string(it->second) + "%", color, false, true);
        if (s.highlightImportantAffixes && s.StatDisplayEnabled(g.statId)) {
            Badge b;
            b.text = g.badgeLabel + std::to_string(it->second);
            CopyColor(b.color, color);
            e.rightBadges.push_back(std::move(b));
        }
    }

    // Affix groups.
    if (s.enableAffixGroups && t.modsRead) {
        const auto groups = EvaluateGroups(s, data, t);
        e.groupMatched = !groups.empty();
        if (e.groupMatched) line("Affix groups", kMuted, true);
        for (const auto& gm : groups) {
            line(gm.name + "  " + gm.BadgeLabel(), gm.color, false, true);
            if (s.showAffixGroupBadges) {
                Badge b;
                b.text = gm.BadgeLabel();
                CopyColor(b.color, gm.color);
                e.leftBadges.push_back(std::move(b));
            }
        }
    }

    // Border: the first enabled group whose conditions all hold.
    if (s.borderEnabled) {
        std::vector<std::string> met;
        for (const auto& r : s.borderRules) {
            if (!r.enabled || !RuleHasConditions(r)) continue;
            if (!RuleMatches(r, data, t, met)) continue;
            e.border = true;
            CopyColor(e.borderColor, r.color);
            line("Border: " + (r.name.empty() ? std::string("unnamed group") : r.name), r.color);
            for (auto& m : met) line(std::move(m), r.color, false, true);
            break;
        }
    }

    e.matched = e.hasAffixCountBadge || !e.rightBadges.empty() || !e.leftBadges.empty() || e.border;
    return e;
}

// The hover breakdown appears for waystones the settings single out.
inline bool WorthBreakdown(const Score& e) {
    return e.matched || e.groupMatched;
}

}  // namespace WaystoneHelper
