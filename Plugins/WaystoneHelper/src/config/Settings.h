#pragma once
// Waystone Helper settings, stored as config/settings.json in the plugin folder.
// The file format is the one Waystone Helper 1.x wrote, so existing settings
// carry over; it is written atomically and a damaged file falls back to defaults.

#include "../game/WaystoneData.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace WaystoneHelperConfig {

inline constexpr int   kScanIntervalMinMs  = 150;
inline constexpr int   kScanIntervalMaxMs  = 2000;
inline constexpr int   kScanIntervalDefault = 300;
inline constexpr int   kTargetAffixMin     = 0;
inline constexpr int   kTargetAffixMax     = 8;
inline constexpr int   kBorderThicknessMin = 1;
inline constexpr int   kBorderThicknessMax = 12;
inline constexpr float kBadgeScaleMin      = 0.5f;
inline constexpr float kBadgeScaleMax      = 1.8f;
inline constexpr int   kMinMatchedAffixMax = 8;
inline constexpr int   kMinTierMax         = 16;
inline constexpr int   kStatThresholdMax   = 1000;
inline constexpr std::size_t kMaxNameLen   = 95;
inline constexpr std::size_t kMaxGroups    = 64;
inline constexpr int   kConfigVersion      = 2;

inline constexpr int kCorruptedAny   = 0;
inline constexpr int kCorruptedOnly  = 1;
inline constexpr int kCorruptedNever = 2;

struct AffixGroup {
    std::string id;
    std::string name = "New group";
    bool  enabled = true;
    int   minMatched = 1;
    float color[4] = {0.0f, 0.749f, 1.0f, 1.0f};
    std::vector<std::string> selectedAffixIds;
};

struct BorderRule {
    std::string id;
    std::string name = "New group";
    bool  enabled = true;
    float color[4] = {0.20f, 0.90f, 0.35f, 1.0f};
    std::unordered_map<std::string, int> statMins;   // stat id -> minimum total (0 = ignore)
    int   minTier = 0;
    int   minAffixes = 0;
    int   corrupted = kCorruptedAny;
    int   rarity = -1;                               // -1 any, 0 normal, 1 magic, 2 rare, 3 unique

    int StatMin(const std::string& statId) const {
        auto it = statMins.find(statId);
        return it == statMins.end() ? 0 : it->second;
    }
};

// The game's numeric keys of the tracked stats in an item's aggregated stats
// (Stats.dat row + 1). They move when a patch inserts Stats.dat rows; the plugin
// re-detects them from the waystones it sees (StatKeyCalibrator).
struct StatAggregateKeys {
    static constexpr int kDefaultMonsterEffectiveness = 8209;
    static constexpr int kDefaultItemRarity           = 8206;
    static constexpr int kDefaultPackSize             = 8207;
    static constexpr int kDefaultMonsterRarity        = 8208;
    static constexpr int kDefaultWaystoneDropChance   = 8210;

    int monsterEffectiveness = kDefaultMonsterEffectiveness;
    int itemRarity           = kDefaultItemRarity;
    int packSize             = kDefaultPackSize;
    int monsterRarity        = kDefaultMonsterRarity;
    int waystoneDropChance   = kDefaultWaystoneDropChance;

    int* Slot(const std::string& statId);
    int For(const std::string& statId) const;
    bool operator==(const StatAggregateKeys&) const = default;
};

// What Settings::Load found.
enum class LoadResult {
    Loaded,      // the file was read and applied
    Missing,     // no file yet (first start): defaults
    Unreadable,  // the file exists but could not be opened or read (locked, no access): defaults
    Unparsable,  // the file is not a JSON object (hand edit, truncated): defaults
};

// Which note Settings::Load returned, so the settings tab can word it in the
// program language (the returned text is English, for the log).
enum class LoadNote {
    None,
    UnreadableKept,   // could not be read, a copy was kept as settings.json.bad
    Unreadable,       // could not be read, no copy
    Damaged,          // not JSON, kept as settings.json.bad
    LegacyBorder,     // the single border of a 1.x file became the border group "Border"
};

struct Settings {
    // Map-stat badges (top right of a waystone).
    bool highlightImportantAffixes     = true;
    bool highlightMonsterEffectiveness = true;
    bool highlightItemRarity           = true;
    bool highlightMonsterPackSize      = false;
    bool highlightMonsterRarity        = false;
    bool highlightWaystoneDropChance   = false;

    // Affix-count badge (top left).
    bool showAffixCountBadge = true;
    int  targetAffixCount    = 8;

    // Affix groups (left side) and border groups.
    bool enableAffixGroups    = true;
    bool showAffixGroupBadges = true;
    bool borderEnabled        = true;
    int  borderThickness      = 3;

    // Overlay behaviour.
    bool  hideWhenHovered    = true;
    bool  showHoverBreakdown = true;
    bool  readMods           = true;
    int   toggleOverlayVk    = 0;
    int   scanIntervalMs     = kScanIntervalDefault;
    float badgeScale         = 0.9f;

    float affixCountBadgeColor[4]      = {0.0f, 0.749f, 1.0f, 1.0f};
    float monsterEffectivenessColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float itemRarityColor[4]           = {1.0f, 0.647f, 0.0f, 1.0f};
    float monsterPackSizeColor[4]      = {0.196f, 0.804f, 0.196f, 1.0f};
    float monsterRarityColor[4]        = {0.0f, 0.749f, 1.0f, 1.0f};
    float waystoneDropChanceColor[4]   = {1.0f, 0.843f, 0.0f, 1.0f};
    float badgeBackgroundColor[4]      = {0.0f, 0.0f, 0.0f, 0.863f};

    StatAggregateKeys statAggregateKeys;

    std::vector<AffixGroup> affixGroups;
    std::vector<BorderRule> borderRules;

    long long nextId = 1;

    std::string MakeId(const char* prefix) { return std::string(prefix) + std::to_string(nextId++); }

    float* StatColor(const std::string& statId);
    const float* StatColor(const std::string& statId) const;
    bool* StatDisplay(const std::string& statId);
    bool StatDisplayEnabled(const std::string& statId) const;

    // Display defaults only: groups, rules, the hotkey and the stat keys stay.
    void ResetDisplay();

    static std::filesystem::path SettingsPath(const std::filesystem::path& pluginDir) {
        return pluginDir / "config" / "settings.json";
    }

    // Missing, unreadable or damaged file: defaults (an unreadable or damaged file is
    // kept as settings.json.bad when it can be copied; the caller should not write the
    // file again until the player changes a setting). Returns a one-line note when
    // something was migrated or repaired, else ""; `result` says what was found and
    // `note` which note was returned.
    std::string Load(const std::filesystem::path& pluginDir, LoadResult* result = nullptr, LoadNote* note = nullptr);
    bool Save(const std::filesystem::path& pluginDir) const;
};

}  // namespace WaystoneHelperConfig
