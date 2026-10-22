#include "WaystoneScanner.h"

#include <cmath>
#include <cstdio>

namespace WaystoneHelper {

namespace {

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

std::vector<VisibleWaystone> WaystoneScanner::Scan(const PluginSDK::Context* ctx, const WaystoneData& data,
                                                   const ScanOptions& options,
                                                   float displayW, float displayH,
                                                   std::uintptr_t hoveredAddress,
                                                   StatKeyCalibrator& calibrator, ScanStats& stats) {
    std::vector<VisibleWaystone> out;
    stats = ScanStats{};
    if (!ctx) return out;
    const auto t0 = Clock::now();
    const std::uint64_t scan = ++m_scanCounter;

    // Read what the host published, then ask for a fresh scan for next time
    // (the host reads inventories on its own worker thread).
    const auto all = ctx->Inventory.GetAll();
    ctx->Inventory.Scan(-1);

    std::unordered_set<std::uintptr_t> seenThisScan;
    int reads = 0;
    for (const auto& inv : all) {
        const bool trustedGrid = IsMainInventoryName(ctx->Inventory.GetName(inv.InventoryId));
        for (const auto& item : inv.Items) {
            if (item.Address == 0) continue;
            if (!LooksLikeWaystone(item.Path, item.BaseTypeName)) continue;
            auto rect = ResolveItemRect(inv, item, displayW, displayH, trustedGrid);
            if (!rect) continue;
            if (!seenThisScan.insert(item.Address).second) continue;

            Entry& e = m_cache[item.Address];
            const bool fresh = !e.parsed || e.parsed->sourcePath != item.Path;
            if (fresh) e.attempted = false;
            const bool hovered = item.Address == hoveredAddress;
            const bool withinBudget = reads < kMaxReadsPerScan && MsSince(t0) < kReadBudgetMs;
            const auto sinceRead = std::chrono::duration_cast<std::chrono::milliseconds>(t0 - e.readAt).count();
            const auto sinceAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(t0 - e.attemptAt).count();
            // Unread: retry, but not every scan when a read keeps failing. Read: refresh after a while.
            const bool stale = e.parsed && options.readMods &&
                (e.parsed->modsRead ? sinceRead >= kRefreshAfterMs
                                    : (!e.attempted || sinceAttempt >= kRetryFailedReadMs));

            if (fresh || (options.readMods && hovered) || (stale && withinBudget)) {
                // A new waystone always gets its cheap fields; its mods wait for budget.
                const bool readMods = options.readMods && (hovered || withinBudget);
                // The calibrator sees each waystone once, at its first mod read.
                const bool firstRead = fresh || !e.parsed->modsRead;
                auto p = Parse(ctx, data, options, item, readMods, firstRead ? &calibrator : nullptr);
                if (readMods) {
                    ++reads;
                    e.attempted = true;
                    e.attemptAt = t0;
                }
                if (fresh || p->modsRead || !e.parsed->modsRead) {
                    e.parsed = std::move(p);
                    e.readAt = t0;
                }
            }
            e.lastSeenScan = scan;
            if (options.readMods && !e.parsed->modsRead) ++stats.deferred;

            VisibleWaystone vw;
            vw.address = item.Address;
            vw.rect = *rect;
            vw.name = item.UniqueName.empty() ? item.BaseTypeName : item.UniqueName;
            vw.parsed = e.parsed;
            out.push_back(std::move(vw));
        }
    }

    // Forget waystones that have not been on screen for a while.
    if (scan % 32 == 0 || m_cache.size() > 4000) {
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (scan - it->second.lastSeenScan > static_cast<std::uint64_t>(kForgetAfterScans)) it = m_cache.erase(it);
            else ++it;
        }
    }

    stats.visible = static_cast<int>(out.size());
    stats.parsed = reads;
    stats.ms = MsSince(t0);
    return out;
}

std::shared_ptr<ParsedWaystone> WaystoneScanner::Parse(const PluginSDK::Context* ctx, const WaystoneData& data,
                                                       const ScanOptions& options,
                                                       const PluginSDK::InventoryItem& item, bool readMods,
                                                       StatKeyCalibrator* calibrator) {
    auto p = std::make_shared<ParsedWaystone>();
    p->sourcePath = item.Path;
    p->tier = ParseWaystoneTier(item.Path);
    p->rarity = item.Rarity;
    p->corrupted = item.IsCorrupted;
    p->generation = ++m_generation;
    if (!readMods || item.Address == 0) return p;

    // An item that left the inventory since the publication reads as empty:
    // probe it first so a stale address is never treated as "no mods".
    if (ctx->Inventory.ReadItemBaseTypeName(item.Address).empty()) return p;
    const auto mods = ctx->Inventory.ReadItemMods(item.Address);
    if (!mods.Valid) return p;
    p->modsRead = true;
    p->affixCount = static_cast<int>(mods.ExplicitMods.size());
    p->rarity = mods.Rarity;
    p->corrupted = mods.IsCorrupted;

    const auto& tracked = data.GeneratedStats();
    std::vector<bool> carried(tracked.size(), false);   // tracked stats the mods carry (any position)
    for (const auto& m : mods.ExplicitMods) {
        const std::string normId = NormalizeIdentifier(m.Id);
        const std::string normName = NormalizeIdentifier(m.Name);
        if (!normId.empty()) p->matchKeys.insert(normId);
        if (!normName.empty()) p->matchKeys.insert(normName);

        // A mod whose first stat is itself a tracked stat.
        if (!m.StatKey.empty()) {
            const int idx = data.TrackedIndex(m.StatKey);
            if (idx >= 0) {
                carried[static_cast<size_t>(idx)] = true;
                const int v = static_cast<int>(std::lround(m.Value0));
                if (v > 0) p->statValues[tracked[static_cast<size_t>(idx)].statId] += v;
            }
        }
        // Map mods add their tracked stats as 2nd..5th stat; the host passes the
        // first two values, the aggregated stats below give the real totals.
        if (const auto* statIds = data.StatIdsForMod(normId)) {
            for (size_t i = 0; i < statIds->size(); ++i) {
                const int idx = data.TrackedIndex((*statIds)[i]);
                if (idx < 0) continue;
                carried[static_cast<size_t>(idx)] = true;
                int v = 0;
                if (i == 0) v = static_cast<int>(std::lround(m.Value0));
                else if (i == 1) v = static_cast<int>(std::lround(m.Value1));
                if (v > 0) p->statValues[(*statIds)[i]] += v;
            }
        }
    }

    const auto agg = ctx->Inventory.ReadItemAggregatedStats(item.Address);
    std::vector<int> configured(tracked.size(), 0);
    for (size_t i = 0; i < tracked.size(); ++i) {
        const int key = options.statAggregateKeys.For(tracked[i].statId);
        configured[i] = key;
        if (key == 0) continue;
        for (const auto& pr : agg) {
            if (pr.first != key) continue;
            if (pr.second > 0) p->statValues[tracked[i].statId] = pr.second;
            break;
        }
    }
    if (calibrator) calibrator->Observe(carried, agg, configured);
    return p;
}

std::string WaystoneScanner::BuildDebugDump(const PluginSDK::Context* ctx, std::uintptr_t address,
                                            const std::string& name, const std::string& path) {
    std::string s = "name='" + name + "' path='" + path + "'\n";
    if (!ctx || address == 0) return s + "no context / address\n";
    if (ctx->Inventory.ReadItemBaseTypeName(address).empty()) return s + "item is no longer there - skipped\n";

    const auto mods = ctx->Inventory.ReadItemMods(address);
    char line[640];
    std::snprintf(line, sizeof(line), "explicit mods: %zu (valid=%d, rarity=%d, corrupted=%d)\n",
                  mods.ExplicitMods.size(), mods.Valid ? 1 : 0, mods.Rarity, mods.IsCorrupted ? 1 : 0);
    s += line;
    for (const auto& m : mods.ExplicitMods) {
        const std::string text = ctx->Inventory.FormatStat(m.StatKey, m.Value0, m.Value1);
        std::snprintf(line, sizeof(line), "  id='%s' stat='%s' v0=%.1f v1=%.1f :: %s\n",
                      m.Id.c_str(), m.StatKey.c_str(), m.Value0, m.Value1, text.c_str());
        s += line;
    }
    const auto agg = ctx->Inventory.ReadItemAggregatedStats(address);
    std::snprintf(line, sizeof(line), "aggregated stats: %zu pairs {key,value}\n", agg.size());
    s += line;
    for (const auto& pr : agg) {
        std::snprintf(line, sizeof(line), "  key=%d value=%d\n", pr.first, pr.second);
        s += line;
    }
    return s;
}

}  // namespace WaystoneHelper
