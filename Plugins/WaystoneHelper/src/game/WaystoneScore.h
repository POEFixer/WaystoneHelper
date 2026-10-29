#pragma once
// What one waystone shows under the current settings: badges, border, and the
// lines of the hover breakdown. Computed once per scan (or when the settings or
// the program language change), never per frame, on the render thread; the
// overlay only draws the strings built here. The breakdown is worded through
// Words: the plugin passes its translations, the tests pass nothing and get keys.

#include "WaystoneData.h"
#include "WaystoneScanner.h"
#include "../config/Settings.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WaystoneHelper {

// The words a score is built with: lang/en.json keys looked up by `translate` (the
// plugin passes PluginI18n's TrF, the program language), or the keys themselves
// when it is null (the tests). Tr / TrF carry those names so
// tools/plugin_i18n/gen.py --gate checks the keys and the argument names written here.
struct Words {
    using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;
    std::string (*translate)(const char* key, Args args) = nullptr;

    std::string Tr(const char* key) const { return translate ? translate(key, {}) : std::string(key); }
    std::string TrF(const char* key, Args args) const { return translate ? translate(key, args) : std::string(key); }
};

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

// The lang/en.json key of a rarity's name (0 normal .. 3 unique); nullptr for none.
inline const char* RarityKey(int rarity) {
    switch (rarity) {
        case 0: return Key("rarity_normal");
        case 1: return Key("rarity_magic");
        case 2: return Key("rarity_rare");
        case 3: return Key("rarity_unique");
        default: return nullptr;
    }
}

inline std::string RarityText(const Words& w, int rarity) {
    const char* key = RarityKey(rarity);
    return key ? w.Tr(key) : std::string("?");
}

// The lang/en.json key of a group name the plugin itself stored ("New group" for a
// group saved without a name, "Border" for the border a 1.x file had); nullptr for
// a name the player gave, shown as it is.
inline const char* DefaultGroupNameKey(const std::string& name) {
    if (name == "New group") return Key("group_name_new");
    if (name == "Border") return Key("group_name_border");
    return nullptr;
}

inline std::string GroupNameText(const Words& w, const std::string& name) {
    const char* key = DefaultGroupNameKey(name);
    return key ? w.Tr(key) : name;
}

// A tracked stat's name: its lang/en.json text, else the table's English name.
inline std::string StatNameText(const Words& w, const GeneratedStat& g) {
    const char* key = StatNameKey(g.statId);
    return key ? w.Tr(key) : g.displayName;
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
                                              const ParsedWaystone& t, const Words& w = Words{}) {
    std::vector<GroupMatch> out;
    for (const auto& g : s.affixGroups) {
        if (!g.enabled || g.selectedAffixIds.empty()) continue;
        GroupMatch gm;
        gm.name = GroupNameText(w, g.name);
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
                        const ParsedWaystone& t, std::vector<std::string>& met, const Words& w = Words{}) {
    met.clear();
    for (const auto& gs : data.GeneratedStats()) {
        const int min = r.StatMin(gs.statId);
        if (min <= 0) continue;
        if (!t.modsRead) return false;
        auto it = t.statValues.find(gs.statId);
        const int val = it == t.statValues.end() ? 0 : it->second;
        if (val < min) return false;
        met.push_back(w.TrF("breakdown_met_stat", { { "stat", StatNameText(w, gs) },
                                                     { "value", std::to_string(val) },
                                                     { "min", std::to_string(min) } }));
    }
    if (r.minTier > 0) {
        if (t.tier < r.minTier) return false;
        met.push_back(w.TrF("breakdown_met_tier", { { "tier", std::to_string(t.tier) },
                                                     { "min", std::to_string(r.minTier) } }));
    }
    if (r.minAffixes > 0) {
        if (!t.modsRead || t.affixCount < r.minAffixes) return false;
        met.push_back(w.TrF("breakdown_met_affixes", { { "count", std::to_string(t.affixCount) },
                                                        { "min", std::to_string(r.minAffixes) } }));
    }
    if (r.corrupted == WaystoneHelperConfig::kCorruptedOnly) {
        if (!t.corrupted) return false;
        met.push_back(w.Tr("breakdown_corrupted"));
    } else if (r.corrupted == WaystoneHelperConfig::kCorruptedNever) {
        if (t.corrupted) return false;
        met.push_back(w.Tr("breakdown_not_corrupted"));
    }
    if (r.rarity >= 0) {
        if (t.rarity != r.rarity) return false;
        met.push_back(RarityText(w, t.rarity));
    }
    return true;
}

inline Score Evaluate(const WaystoneHelperConfig::Settings& s, const WaystoneData& data,
                      const ParsedWaystone& t, const std::string& title, const Words& w = Words{}) {
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
        std::string info = t.tier > 0 ? w.TrF("breakdown_tier", { { "tier", std::to_string(t.tier) } }) + "  \xC2\xB7  "
                                      : std::string();
        info += RarityText(w, t.rarity);
        if (t.corrupted) info += "  \xC2\xB7  " + w.Tr("breakdown_corrupted");
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
        line(w.TrF("breakdown_affixes", { { "count", std::to_string(t.affixCount) },
                                           { "target", std::to_string(s.targetAffixCount) } }),
             reached ? s.affixCountBadgeColor : kMuted);
    } else {
        line(s.readMods ? w.Tr("breakdown_reading_mods") : w.Tr("breakdown_reading_off"), kMuted);
    }

    // Map-stat badges (all present stats are listed in the breakdown).
    bool statHeader = false;
    for (const auto& g : data.GeneratedStats()) {
        auto it = t.statValues.find(g.statId);
        if (it == t.statValues.end() || it->second <= 0) continue;
        const float* color = s.StatColor(g.statId);
        if (!color) color = kWhite;
        if (!statHeader) { line(w.Tr("breakdown_map_stats"), kMuted, true); statHeader = true; }
        line(StatNameText(w, g) + "  " + w.TrF("unit_percent", { { "value", std::to_string(it->second) } }), color,
             false, true);
        if (s.highlightImportantAffixes && s.StatDisplayEnabled(g.statId)) {
            Badge b;
            b.text = g.badgeLabel + std::to_string(it->second);
            CopyColor(b.color, color);
            e.rightBadges.push_back(std::move(b));
        }
    }

    // Affix groups.
    if (s.enableAffixGroups && t.modsRead) {
        const auto groups = EvaluateGroups(s, data, t, w);
        e.groupMatched = !groups.empty();
        if (e.groupMatched) line(w.Tr("breakdown_affix_groups"), kMuted, true);
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
            if (!RuleMatches(r, data, t, met, w)) continue;
            e.border = true;
            CopyColor(e.borderColor, r.color);
            const std::string name = r.name.empty() ? w.Tr("breakdown_unnamed_group") : GroupNameText(w, r.name);
            line(w.TrF("breakdown_border", { { "name", name } }), r.color);
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
