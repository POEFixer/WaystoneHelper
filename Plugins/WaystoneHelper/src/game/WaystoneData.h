#pragma once
// The waystone affix families and the "final from map" stats each map mod adds.
// The table ships inside the DLL (WaystoneDataEmbedded.h, generated from
// data/waystone_data.json), so the plugin needs no data file next to it.

#include "WaystoneTypes.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WaystoneHelper {

namespace StatIds {
inline constexpr const char* MonsterEffectiveness = "map_monster_potency_+%_final_from_map";
inline constexpr const char* ItemRarity           = "map_item_drop_rarity_+%_final_from_map";
inline constexpr const char* PackSize             = "map_pack_size_+%_final_from_map";
inline constexpr const char* MonsterRarity        =
    "map_number_of_magic_and_rare_packs_+%_final_and_rare_monster_modifiers_chance_+%_final_from_map";
inline constexpr const char* WaystoneDropChance   = "map_map_item_drop_chance_+%_final_from_map";
}  // namespace StatIds

struct GeneratedStat {
    std::string statId;
    std::string displayName;
    std::string badgeLabel;
};

struct Affix {
    std::string Id;
    std::string Label;
    std::string Short;       // the first line of Label ("(20-24)% increased Monster Damage")
    std::string Category;    // "Prefix" / "Suffix"
    std::vector<std::string> ModIds;
    std::unordered_set<std::string> NormModIds;
};

class WaystoneData {
public:
    // Parses the table; returns a one-line status for the Diagnostics card.
    std::string LoadFromText(std::string_view text) {
        m_affixes.clear();
        m_affixIndex.clear();
        m_modStatIds.clear();
        m_generatedStats = DefaultGeneratedStats();
        m_loaded = false;

        nlohmann::json j = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return "The built-in waystone table could not be read: only the affix count works.";

        LoadGeneratedStats(j);
        LoadAffixes(j);
        LoadModStatIds(j);
        m_loaded = true;
        return "Built-in table: " + std::to_string(m_affixes.size()) + " affix families, "
             + std::to_string(m_modStatIds.size()) + " map mods.";
    }

    bool Loaded() const { return m_loaded; }
    const std::vector<GeneratedStat>& GeneratedStats() const { return m_generatedStats; }
    const std::vector<Affix>& Affixes() const { return m_affixes; }

    const Affix* FindAffix(const std::string& id) const {
        auto it = m_affixIndex.find(NormalizeIdentifier(id));
        return it == m_affixIndex.end() ? nullptr : &m_affixes[it->second];
    }

    // The stat ids (in Mods.dat stat order) a mod adds; nullptr for an unknown mod.
    const std::vector<std::string>* StatIdsForMod(const std::string& normId) const {
        auto it = m_modStatIds.find(normId);
        return it == m_modStatIds.end() ? nullptr : &it->second;
    }

    // Index of statId in GeneratedStats(), or -1.
    int TrackedIndex(const std::string& statId) const {
        for (size_t i = 0; i < m_generatedStats.size(); ++i)
            if (m_generatedStats[i].statId == statId) return static_cast<int>(i);
        return -1;
    }

private:
    std::vector<GeneratedStat> m_generatedStats;
    std::vector<Affix> m_affixes;
    std::unordered_map<std::string, std::size_t> m_affixIndex;
    std::unordered_map<std::string, std::vector<std::string>> m_modStatIds;
    bool m_loaded = false;

    static std::string Str(const nlohmann::json& o, const char* key) {
        auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    static std::vector<GeneratedStat> DefaultGeneratedStats() {
        return {
            {StatIds::MonsterEffectiveness, "Monster Effectiveness", "E"},
            {StatIds::ItemRarity,           "Item Rarity",           "R"},
            {StatIds::PackSize,             "Monster Pack Size",     "P"},
            {StatIds::MonsterRarity,        "Monster Rarity",        "MR"},
            {StatIds::WaystoneDropChance,   "Waystone Drop Chance",  "W"},
        };
    }

    void LoadGeneratedStats(const nlohmann::json& j) {
        auto it = j.find("generatedStats");
        if (it == j.end() || !it->is_array()) return;
        std::vector<GeneratedStat> loaded;
        for (const auto& e : *it) {
            if (!e.is_object()) continue;
            GeneratedStat g{Str(e, "statId"), Str(e, "displayName"), Str(e, "badgeLabel")};
            if (g.statId.empty() || g.badgeLabel.empty()) continue;
            if (g.displayName.empty()) g.displayName = g.statId;
            loaded.push_back(std::move(g));
        }
        if (!loaded.empty()) m_generatedStats = std::move(loaded);
    }

    void LoadAffixes(const nlohmann::json& j) {
        auto it = j.find("affixes");
        if (it == j.end() || !it->is_array()) return;
        for (const auto& e : *it) {
            if (!e.is_object()) continue;
            Affix a;
            a.Id = Str(e, "id");
            if (a.Id.empty()) continue;
            a.Label = Str(e, "label");
            if (a.Label.empty()) a.Label = a.Id;
            const auto cut = a.Label.find(" / ");
            a.Short = cut == std::string::npos ? a.Label : a.Label.substr(0, cut);
            a.Category = Str(e, "category");
            auto mods = e.find("modIds");
            if (mods != e.end() && mods->is_array()) {
                for (const auto& m : *mods) {
                    if (!m.is_string()) continue;
                    std::string raw = m.get<std::string>();
                    if (raw.empty()) continue;
                    a.NormModIds.insert(NormalizeIdentifier(raw));
                    a.ModIds.push_back(std::move(raw));
                }
            }
            const std::string normId = NormalizeIdentifier(a.Id);
            if (m_affixIndex.count(normId)) continue;
            m_affixIndex.emplace(normId, m_affixes.size());
            m_affixes.push_back(std::move(a));
        }
    }

    void LoadModStatIds(const nlohmann::json& j) {
        auto it = j.find("modStatIdsById");
        if (it == j.end() || !it->is_object()) return;
        for (auto m = it->begin(); m != it->end(); ++m) {
            if (!m.value().is_array()) continue;
            std::vector<std::string> stats;
            for (const auto& s : m.value())
                stats.push_back(s.is_string() ? s.get<std::string>() : std::string());
            while (!stats.empty() && stats.back().empty()) stats.pop_back();
            if (stats.empty()) continue;
            m_modStatIds.emplace(NormalizeIdentifier(m.key()), std::move(stats));
        }
    }
};

}  // namespace WaystoneHelper
