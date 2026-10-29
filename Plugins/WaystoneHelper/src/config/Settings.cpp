#include "Settings.h"

#include "../../../../POEFixer/core/FileIO.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace WaystoneHelperConfig {

namespace {

using nlohmann::json;
namespace StatIds = WaystoneHelper::StatIds;

// Type-checked readers: a value of the wrong type keeps the default instead of
// throwing half-way through the file.
bool GetBool(const json& o, const char* key, bool def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
}

long long GetInt(const json& o, const char* key, long long def) {
    auto it = o.find(key);
    if (it == o.end()) return def;
    if (it->is_number_integer()) return it->get<long long>();
    if (it->is_number_float()) return static_cast<long long>(it->get<double>());
    return def;
}

int GetIntClamped(const json& o, const char* key, int def, int lo, int hi) {
    const long long v = GetInt(o, key, def);
    return static_cast<int>(std::clamp<long long>(v, lo, hi));
}

float GetFloat(const json& o, const char* key, float def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_number()) ? it->get<float>() : def;
}

std::string GetString(const json& o, const char* key, const std::string& def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : def;
}

// Cuts at a UTF-8 character boundary.
void TruncateUtf8(std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return;
    std::size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
}

void ColorFromJson(const json& o, const char* key, float dst[4]) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_array()) return;
    for (std::size_t i = 0; i < 4 && i < it->size(); ++i)
        if ((*it)[i].is_number()) dst[i] = std::clamp((*it)[i].get<float>(), 0.0f, 1.0f);
}

json ColorToJson(const float c[4]) { return json{c[0], c[1], c[2], c[3]}; }

std::vector<std::string> StringVecFromJson(const json& o, const char* key) {
    std::vector<std::string> out;
    auto it = o.find(key);
    if (it == o.end() || !it->is_array()) return out;
    for (const auto& e : *it) {
        if (!e.is_string()) continue;
        std::string s = e.get<std::string>();
        if (s.empty() || std::find(out.begin(), out.end(), s) != out.end()) continue;
        out.push_back(std::move(s));
    }
    return out;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

}  // namespace

int* StatAggregateKeys::Slot(const std::string& statId) {
    if (statId == StatIds::MonsterEffectiveness) return &monsterEffectiveness;
    if (statId == StatIds::ItemRarity)           return &itemRarity;
    if (statId == StatIds::PackSize)             return &packSize;
    if (statId == StatIds::MonsterRarity)        return &monsterRarity;
    if (statId == StatIds::WaystoneDropChance)   return &waystoneDropChance;
    return nullptr;
}

int StatAggregateKeys::For(const std::string& statId) const {
    int* p = const_cast<StatAggregateKeys*>(this)->Slot(statId);
    return p ? *p : 0;
}

float* Settings::StatColor(const std::string& statId) {
    if (statId == StatIds::MonsterEffectiveness) return monsterEffectivenessColor;
    if (statId == StatIds::ItemRarity)           return itemRarityColor;
    if (statId == StatIds::PackSize)             return monsterPackSizeColor;
    if (statId == StatIds::MonsterRarity)        return monsterRarityColor;
    if (statId == StatIds::WaystoneDropChance)   return waystoneDropChanceColor;
    return nullptr;
}

const float* Settings::StatColor(const std::string& statId) const {
    return const_cast<Settings*>(this)->StatColor(statId);
}

bool* Settings::StatDisplay(const std::string& statId) {
    if (statId == StatIds::MonsterEffectiveness) return &highlightMonsterEffectiveness;
    if (statId == StatIds::ItemRarity)           return &highlightItemRarity;
    if (statId == StatIds::PackSize)             return &highlightMonsterPackSize;
    if (statId == StatIds::MonsterRarity)        return &highlightMonsterRarity;
    if (statId == StatIds::WaystoneDropChance)   return &highlightWaystoneDropChance;
    return nullptr;
}

bool Settings::StatDisplayEnabled(const std::string& statId) const {
    const bool* p = const_cast<Settings*>(this)->StatDisplay(statId);
    return p && *p;
}

void Settings::ResetDisplay() {
    Settings fresh;
    fresh.toggleOverlayVk = toggleOverlayVk;
    fresh.statAggregateKeys = statAggregateKeys;
    fresh.affixGroups = std::move(affixGroups);
    fresh.borderRules = std::move(borderRules);
    fresh.nextId = nextId;
    fresh.readMods = readMods;
    *this = std::move(fresh);
}

std::string Settings::Load(const std::filesystem::path& pluginDir, LoadResult* result, LoadNote* noteKind) {
    LoadResult unused = LoadResult::Missing;
    LoadResult& res = result ? *result : unused;
    res = LoadResult::Missing;
    LoadNote unusedNote = LoadNote::None;
    LoadNote& kind = noteKind ? *noteKind : unusedNote;
    kind = LoadNote::None;
    const auto path = SettingsPath(pluginDir);
    std::error_code ec;
    // Keep an unusable file for the player instead of letting a later save replace it.
    auto keepCopy = [&]() {
        auto bad = path;
        bad += L".bad";
        std::error_code copyEc;
        return std::filesystem::copy_file(path, bad, std::filesystem::copy_options::overwrite_existing, copyEc) &&
               !copyEc;
    };
    const bool exists = std::filesystem::exists(path, ec);
    if (!exists && !ec) return {};   // first start

    std::string text;
    if (!exists || !ReadFile(path, text)) {
        // Locked by another program, no access to the folder: the file is probably fine.
        res = LoadResult::Unreadable;
        const bool kept = keepCopy();
        kind = kept ? LoadNote::UnreadableKept : LoadNote::Unreadable;
        return kept ? "settings.json could not be read (a copy was kept as settings.json.bad): defaults are "
                      "used, and the file is left alone until you change a setting."
                    : "settings.json could not be read: defaults are used, and the file is left alone until "
                      "you change a setting.";
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        res = LoadResult::Unparsable;
        keepCopy();
        kind = LoadNote::Damaged;
        return "settings.json was damaged (kept as settings.json.bad): defaults are used, and the file is left "
               "alone until you change a setting.";
    }
    res = LoadResult::Loaded;

    std::string note;
    const int version = static_cast<int>(GetInt(j, "config_version", 1));

    highlightImportantAffixes     = GetBool(j, "highlight_important_affixes", highlightImportantAffixes);
    highlightMonsterEffectiveness = GetBool(j, "highlight_monster_effectiveness", highlightMonsterEffectiveness);
    highlightItemRarity           = GetBool(j, "highlight_item_rarity", highlightItemRarity);
    highlightMonsterPackSize      = GetBool(j, "highlight_monster_pack_size", highlightMonsterPackSize);
    highlightMonsterRarity        = GetBool(j, "highlight_monster_rarity", highlightMonsterRarity);
    highlightWaystoneDropChance   = GetBool(j, "highlight_waystone_drop_chance", highlightWaystoneDropChance);
    showAffixCountBadge  = GetBool(j, "show_affix_count_badge", showAffixCountBadge);
    enableAffixGroups    = GetBool(j, "enable_affix_groups", enableAffixGroups);
    showAffixGroupBadges = GetBool(j, "show_affix_group_badges", showAffixGroupBadges);
    borderEnabled        = GetBool(j, "border_enabled", borderEnabled);
    hideWhenHovered      = GetBool(j, "hide_when_hovered", hideWhenHovered);
    showHoverBreakdown   = GetBool(j, "show_hover_breakdown", showHoverBreakdown);
    readMods             = GetBool(j, "read_mods", readMods);
    toggleOverlayVk      = GetIntClamped(j, "toggle_overlay_vk", toggleOverlayVk, 0, 254);

    scanIntervalMs   = GetIntClamped(j, "scan_interval_ms", scanIntervalMs, kScanIntervalMinMs, kScanIntervalMaxMs);
    // 1.x saved its 650 ms default into every file; the refresh is cheap now, so
    // an untouched old default moves to the new one.
    if (version < 2 && scanIntervalMs == 650) scanIntervalMs = kScanIntervalDefault;
    targetAffixCount = GetIntClamped(j, "target_affix_count", targetAffixCount, kTargetAffixMin, kTargetAffixMax);
    borderThickness  = GetIntClamped(j, "border_thickness", borderThickness, kBorderThicknessMin, kBorderThicknessMax);
    badgeScale       = std::clamp(GetFloat(j, "badge_scale", badgeScale), kBadgeScaleMin, kBadgeScaleMax);

    if (auto c = j.find("colors"); c != j.end() && c->is_object()) {
        ColorFromJson(*c, "affix_count", affixCountBadgeColor);
        ColorFromJson(*c, "monster_effectiveness", monsterEffectivenessColor);
        ColorFromJson(*c, "item_rarity", itemRarityColor);
        ColorFromJson(*c, "monster_pack_size", monsterPackSizeColor);
        ColorFromJson(*c, "monster_rarity", monsterRarityColor);
        ColorFromJson(*c, "waystone_drop_chance", waystoneDropChanceColor);
        ColorFromJson(*c, "badge_background", badgeBackgroundColor);
    }

    if (auto k = j.find("stat_aggregate_keys"); k != j.end() && k->is_object()) {
        auto key = [&](const char* name, int def) { return GetIntClamped(*k, name, def, 0, 1 << 24); };
        statAggregateKeys.monsterEffectiveness = key("monster_effectiveness", statAggregateKeys.monsterEffectiveness);
        statAggregateKeys.itemRarity           = key("item_rarity", statAggregateKeys.itemRarity);
        statAggregateKeys.packSize             = key("pack_size", statAggregateKeys.packSize);
        statAggregateKeys.monsterRarity        = key("monster_rarity", statAggregateKeys.monsterRarity);
        statAggregateKeys.waystoneDropChance   = key("waystone_drop_chance", statAggregateKeys.waystoneDropChance);
    }

    if (auto g = j.find("affix_groups"); g != j.end() && g->is_array()) {
        affixGroups.clear();
        for (const auto& e : *g) {
            if (!e.is_object() || affixGroups.size() >= kMaxGroups) continue;
            AffixGroup a;
            a.id = GetString(e, "id", "");
            a.name = GetString(e, "name", a.name);
            TruncateUtf8(a.name, kMaxNameLen);
            a.enabled = GetBool(e, "enabled", true);
            a.minMatched = GetIntClamped(e, "min_matched", 1, 1, kMinMatchedAffixMax);
            ColorFromJson(e, "color", a.color);
            a.selectedAffixIds = StringVecFromJson(e, "selected_affix_ids");
            affixGroups.push_back(std::move(a));
        }
    }

    if (auto b = j.find("border_rules"); b != j.end() && b->is_array()) {
        borderRules.clear();
        for (const auto& e : *b) {
            if (!e.is_object() || borderRules.size() >= kMaxGroups) continue;
            BorderRule r;
            r.id = GetString(e, "id", "");
            r.name = GetString(e, "name", r.name);
            TruncateUtf8(r.name, kMaxNameLen);
            r.enabled = GetBool(e, "enabled", true);
            ColorFromJson(e, "color", r.color);
            if (auto sm = e.find("stat_mins"); sm != e.end() && sm->is_object()) {
                for (auto it = sm->begin(); it != sm->end(); ++it) {
                    if (!it.value().is_number()) continue;
                    const int v = static_cast<int>(std::clamp<double>(it.value().get<double>(), 0.0, kStatThresholdMax));
                    if (v > 0) r.statMins[it.key()] = v;
                }
            }
            r.minTier    = GetIntClamped(e, "min_tier", 0, 0, kMinTierMax);
            r.minAffixes = GetIntClamped(e, "min_affixes", 0, 0, kTargetAffixMax);
            r.corrupted  = GetIntClamped(e, "corrupted", kCorruptedAny, kCorruptedAny, kCorruptedNever);
            r.rarity     = GetIntClamped(e, "rarity", -1, -1, 3);
            borderRules.push_back(std::move(r));
        }
    } else {
        // Waystone Helper before 1.3 had one border with flat min_* keys.
        struct LegacyMin { const char* key; const char* statId; };
        static const LegacyMin kLegacy[] = {
            {"min_monster_effectiveness", StatIds::MonsterEffectiveness},
            {"min_item_rarity",           StatIds::ItemRarity},
            {"min_monster_pack_size",     StatIds::PackSize},
            {"min_monster_rarity",        StatIds::MonsterRarity},
            {"min_waystone_drop_chance",  StatIds::WaystoneDropChance},
        };
        BorderRule r;
        bool any = false;
        for (const auto& l : kLegacy) {
            const int v = GetIntClamped(j, l.key, 0, 0, kStatThresholdMax);
            if (v > 0) { r.statMins[l.statId] = v; any = true; }
        }
        r.minTier = GetIntClamped(j, "border_min_tier", 0, 0, kMinTierMax);
        if (r.minTier > 0) any = true;
        if (GetBool(j, "border_require_affix_count", false)) {
            r.minAffixes = targetAffixCount;
            if (r.minAffixes > 0) any = true;
        }
        if (any) {
            if (auto c = j.find("colors"); c != j.end() && c->is_object()) ColorFromJson(*c, "border", r.color);
            r.name = "Border";
            borderRules.push_back(std::move(r));
            note = "The single border of an older version became the border group \"Border\".";
            kind = LoadNote::LegacyBorder;
        }
    }

    nextId = std::max<long long>(1, GetInt(j, "next_id", nextId));
    // Ids must be unique and present (the editors key their state on them).
    std::vector<std::string> seen;
    auto fixId = [&](std::string& id, const char* prefix) {
        if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end()) id = MakeId(prefix);
        seen.push_back(id);
    };
    for (auto& g : affixGroups) fixId(g.id, "g");
    for (auto& r : borderRules) fixId(r.id, "b");
    return note;
}

bool Settings::Save(const std::filesystem::path& pluginDir) const {
    if (pluginDir.empty()) return false;
    try {
        std::error_code ec;
        std::filesystem::create_directories(pluginDir / "config", ec);

        json j;
        j["config_version"] = kConfigVersion;
        j["highlight_important_affixes"] = highlightImportantAffixes;
        j["highlight_monster_effectiveness"] = highlightMonsterEffectiveness;
        j["highlight_item_rarity"] = highlightItemRarity;
        j["highlight_monster_pack_size"] = highlightMonsterPackSize;
        j["highlight_monster_rarity"] = highlightMonsterRarity;
        j["highlight_waystone_drop_chance"] = highlightWaystoneDropChance;
        j["show_affix_count_badge"] = showAffixCountBadge;
        j["enable_affix_groups"] = enableAffixGroups;
        j["show_affix_group_badges"] = showAffixGroupBadges;
        j["border_enabled"] = borderEnabled;
        j["hide_when_hovered"] = hideWhenHovered;
        j["show_hover_breakdown"] = showHoverBreakdown;
        j["read_mods"] = readMods;
        j["toggle_overlay_vk"] = toggleOverlayVk;
        j["scan_interval_ms"] = scanIntervalMs;
        j["target_affix_count"] = targetAffixCount;
        j["border_thickness"] = borderThickness;
        j["badge_scale"] = badgeScale;

        json colors;
        colors["affix_count"] = ColorToJson(affixCountBadgeColor);
        colors["monster_effectiveness"] = ColorToJson(monsterEffectivenessColor);
        colors["item_rarity"] = ColorToJson(itemRarityColor);
        colors["monster_pack_size"] = ColorToJson(monsterPackSizeColor);
        colors["monster_rarity"] = ColorToJson(monsterRarityColor);
        colors["waystone_drop_chance"] = ColorToJson(waystoneDropChanceColor);
        colors["badge_background"] = ColorToJson(badgeBackgroundColor);
        j["colors"] = std::move(colors);

        json keys;
        keys["monster_effectiveness"] = statAggregateKeys.monsterEffectiveness;
        keys["item_rarity"] = statAggregateKeys.itemRarity;
        keys["pack_size"] = statAggregateKeys.packSize;
        keys["monster_rarity"] = statAggregateKeys.monsterRarity;
        keys["waystone_drop_chance"] = statAggregateKeys.waystoneDropChance;
        j["stat_aggregate_keys"] = std::move(keys);

        json groups = json::array();
        for (const auto& g : affixGroups) {
            json e;
            e["id"] = g.id;
            e["name"] = g.name;
            e["enabled"] = g.enabled;
            e["min_matched"] = g.minMatched;
            e["color"] = ColorToJson(g.color);
            e["selected_affix_ids"] = g.selectedAffixIds;
            groups.push_back(std::move(e));
        }
        j["affix_groups"] = std::move(groups);

        json rules = json::array();
        for (const auto& r : borderRules) {
            json e;
            e["id"] = r.id;
            e["name"] = r.name;
            e["enabled"] = r.enabled;
            e["color"] = ColorToJson(r.color);
            json sm = json::object();
            for (const auto& kv : r.statMins)
                if (kv.second > 0) sm[kv.first] = kv.second;
            e["stat_mins"] = std::move(sm);
            e["min_tier"] = r.minTier;
            e["min_affixes"] = r.minAffixes;
            e["corrupted"] = r.corrupted;
            e["rarity"] = r.rarity;
            rules.push_back(std::move(e));
        }
        j["border_rules"] = std::move(rules);
        j["next_id"] = nextId;

        const std::string text = j.dump(2, ' ', false, json::error_handler_t::replace);
        return FileIO::AtomicWrite(SettingsPath(pluginDir), text);
    } catch (...) {
        return false;
    }
}

}  // namespace WaystoneHelperConfig
