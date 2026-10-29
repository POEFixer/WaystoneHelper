#pragma once
// The waystone affix families and the "final from map" stats each map mod adds.
// The table ships inside the DLL (WaystoneDataEmbedded.h, generated from
// data/waystone_data.json), so the plugin needs no data file next to it.
// Its English labels are also lang/en.json texts: an affix's label is the key
// "affix_<id>" (lang/dynamic_prefixes.txt lists "affix_") whose text holds {n1},
// {n2}, ... where the label's numbers go (FillNumbers puts the table's numbers
// back), and a tracked stat's name is StatNameKey(); tests/test_waystone.cpp
// checks both against lang/en.json.

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
    std::string LabelKey;    // lang/en.json key of the label ("affix_<Id>")
    std::string Label;       // the table's English label, lines joined by " / "
    std::string Short;       // the first line of Label ("(20-24)% increased Monster Damage")
    std::string Category;    // "Prefix" / "Suffix"
    std::vector<std::string> Numbers;   // LabelNumbers(Label): {n1}, {n2}, ... of its lang/en.json text
    std::vector<std::string> ModIds;
    std::unordered_set<std::string> NormModIds;
};

// The numbers of an affix label, in order, each with its sign, range and percent
// sign: "(20-24)% increased Monster Damage / 20% more Waystones found in Area" ->
// {"(20-24)%", "20%"}; "-(10-8)% maximum Player Resistances" -> {"-(10-8)%"}.
inline std::vector<std::string> LabelNumbers(std::string_view label) {
    std::vector<std::string> out;
    const auto digit = [&](std::size_t i) { return i < label.size() && label[i] >= '0' && label[i] <= '9'; };
    // Digits with an optional fraction from i; returns the index after them (i: none).
    const auto number = [&](std::size_t i) {
        std::size_t j = i;
        while (digit(j)) ++j;
        if (j > i && j + 1 < label.size() && label[j] == '.' && digit(j + 1)) {
            ++j;
            while (digit(j)) ++j;
        }
        return j;
    };
    std::size_t i = 0;
    while (i < label.size()) {
        const bool wordBefore = i > 0 && AsciiAlnum(label[i - 1]);
        std::size_t start = i;
        std::size_t j = i;
        if (!wordBefore && (label[j] == '+' || label[j] == '-') && (digit(j + 1) || (j + 1 < label.size() && label[j + 1] == '(')))
            ++j;
        std::size_t end = j;
        if (j < label.size() && label[j] == '(' && digit(j + 1)) {
            std::size_t k = number(j + 1);
            if (k < label.size() && label[k] == '-' && digit(k + 1)) {
                k = number(k + 1);
                if (k < label.size() && label[k] == ')') end = k + 1;
            }
        } else if (!wordBefore && digit(j)) {
            end = number(j);
        }
        if (end == j) {   // no number here
            ++i;
            continue;
        }
        if (end < label.size() && label[end] == '%') ++end;
        out.emplace_back(label.substr(start, end - start));
        i = end;
    }
    return out;
}

// `text` with each {n1}, {n2}, ... replaced by numbers[0], numbers[1], ...; a
// placeholder without a number stays as it is.
inline std::string FillNumbers(std::string_view text, const std::vector<std::string>& numbers) {
    std::string out;
    out.reserve(text.size() + 16);
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{' && i + 2 < text.size() && text[i + 1] == 'n') {
            std::size_t j = i + 2;
            std::size_t index = 0;
            while (j < text.size() && text[j] >= '0' && text[j] <= '9' && index < 1000)
                index = index * 10 + static_cast<std::size_t>(text[j++] - '0');
            if (j > i + 2 && j < text.size() && text[j] == '}' && index >= 1 && index <= numbers.size()) {
                out += numbers[index - 1];
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// The lang/en.json key of a tracked stat's name; nullptr for a stat the table adds
// later (its English name from the table is shown then).
inline const char* StatNameKey(std::string_view statId) {
    if (statId == StatIds::MonsterEffectiveness) return Key("stat_monster_effectiveness");
    if (statId == StatIds::ItemRarity)           return Key("stat_item_rarity");
    if (statId == StatIds::PackSize)             return Key("stat_pack_size");
    if (statId == StatIds::MonsterRarity)        return Key("stat_monster_rarity");
    if (statId == StatIds::WaystoneDropChance)   return Key("stat_waystone_drop_chance");
    return nullptr;
}

// The lang/en.json key of an affix category's heading ("Prefix" / "Suffix" / other).
inline const char* CategoryKey(std::string_view category) {
    if (category == "Prefix") return Key("category_prefix");
    if (category == "Suffix") return Key("category_suffix");
    return Key("category_other");
}

class WaystoneData {
public:
    // Parses the table; returns a one-line English status (the Diagnostics card words
    // Loaded(), Affixes().size() and ModCount() in the program language).
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
    std::size_t ModCount() const { return m_modStatIds.size(); }
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
            a.LabelKey = "affix_" + a.Id;
            a.Label = Str(e, "label");
            if (a.Label.empty()) a.Label = a.Id;
            a.Numbers = LabelNumbers(a.Label);
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
