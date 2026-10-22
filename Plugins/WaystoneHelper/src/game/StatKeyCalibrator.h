#pragma once
// Finds the game's numeric key of each tracked map stat in an item's aggregated
// stats. Those keys are Stats.dat row numbers and move when a patch inserts rows,
// so the configured keys can go stale; the item's own mods tell which tracked
// stats a waystone carries, which narrows each stat down to one key.
//
// Per stat: keys seen with a value on every waystone that carries the stat,
// minus keys seen on any waystone that does not carry it. One key left = found.
// Pure logic (no SDK, no ImGui) so a standalone test can drive it.

#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace WaystoneHelper {

class StatKeyCalibrator {
public:
    enum class Status { Unknown, Confirmed, Stale };

    // Observations of a stat before a stale configured key may be replaced.
    static constexpr int kMinEvidence = 3;

    void Reset(std::size_t statCount) {
        m_stats.assign(statCount, State{});
        m_observedItems = 0;
    }

    std::size_t StatCount() const { return m_stats.size(); }
    int ObservedItems() const { return m_observedItems; }

    // One waystone, once: present[i] = its mods carry tracked stat i;
    // aggregated = its {key, value} pairs; configured[i] = the key in use (0 = none).
    void Observe(const std::vector<bool>& present,
                 const std::vector<std::pair<int, int>>& aggregated,
                 const std::vector<int>& configured) {
        if (aggregated.empty() || present.size() != m_stats.size()) return;
        ++m_observedItems;
        std::set<int> keys;
        for (const auto& kv : aggregated)
            if (kv.second != 0) keys.insert(kv.first);
        for (std::size_t i = 0; i < m_stats.size(); ++i) {
            State& s = m_stats[i];
            const int key = i < configured.size() ? configured[i] : 0;
            if (present[i]) {
                ++s.observations;
                if (key != 0 && keys.count(key)) ++s.configuredHits;
                else ++s.configuredMisses;
                if (!s.seeded) {
                    s.candidates = keys;
                    s.seeded = true;
                } else {
                    for (auto it = s.candidates.begin(); it != s.candidates.end();)
                        it = keys.count(*it) ? std::next(it) : s.candidates.erase(it);
                }
            } else {
                s.excluded.insert(keys.begin(), keys.end());
            }
        }
    }

    // The one key left for stat i, or 0 while the evidence is not conclusive.
    int Detected(std::size_t i) const {
        if (i >= m_stats.size() || !m_stats[i].seeded) return 0;
        int found = 0;
        for (int k : m_stats[i].candidates) {
            if (m_stats[i].excluded.count(k)) continue;
            if (found != 0) return 0;
            found = k;
        }
        return found;
    }

    Status StatusOf(std::size_t i) const {
        if (i >= m_stats.size()) return Status::Unknown;
        const State& s = m_stats[i];
        if (s.configuredHits > 0) return Status::Confirmed;
        if (s.configuredMisses >= kMinEvidence) return Status::Stale;
        return Status::Unknown;
    }

    int Observations(std::size_t i) const { return i < m_stats.size() ? m_stats[i].observations : 0; }

    // The key to switch stat i to: the configured key never matched in enough
    // waystones and the evidence names exactly one other key. 0 = keep it.
    int Replacement(std::size_t i, int configured) const {
        if (StatusOf(i) != Status::Stale) return 0;
        const int k = Detected(i);
        return (k != 0 && k != configured) ? k : 0;
    }

private:
    struct State {
        bool seeded = false;
        int observations = 0;
        int configuredHits = 0;
        int configuredMisses = 0;
        std::set<int> candidates;
        std::set<int> excluded;
    };
    std::vector<State> m_stats;
    int m_observedItems = 0;
};

}  // namespace WaystoneHelper
