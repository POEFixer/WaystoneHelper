#pragma once
// Finds the waystones on screen and reads their mods.
//
// One scan per refresh interval (never per frame), on the plugin's scan worker
// (WaystoneWorker), never on the render thread: the host's last inventory
// publication is copied once, every waystone is located, and its mods are read
// only when the item is new to the cache. Reading is budgeted per scan (count
// and time); the waystone under the cursor is re-read every scan, so currency
// used on it shows at once, and other cached waystones are re-read after a while.

#include "ItemRects.h"
#include "StatKeyCalibrator.h"
#include "WaystoneData.h"
#include "WaystoneTypes.h"
#include "../config/Settings.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WaystoneHelper {

struct ParsedWaystone {
    std::string sourcePath;
    int  affixCount = 0;
    int  tier = 0;
    int  rarity = 0;
    bool corrupted = false;
    bool modsRead = false;                               // false: only path/rarity/corruption are known
    std::unordered_set<std::string> matchKeys;           // normalized mod ids and names
    std::unordered_map<std::string, int> statValues;     // tracked stat id -> total (> 0 only)
    std::uint64_t generation = 0;                        // changes whenever this entry is re-read
};

struct VisibleWaystone {
    std::uintptr_t address = 0;
    ScreenRect rect;
    std::string name;
    std::shared_ptr<const ParsedWaystone> parsed;        // never null
};

// What a scan needs from the settings (a copy the worker owns).
struct ScanOptions {
    bool readMods = true;
    WaystoneHelperConfig::StatAggregateKeys statAggregateKeys;
};

struct ScanStats {
    int  visible = 0;
    int  parsed = 0;          // mod reads this scan
    int  deferred = 0;        // waystones still waiting for their first read
    double ms = 0.0;
};

class WaystoneScanner {
public:
    // Reads per scan beyond the hovered waystone, and the time budget for them.
    static constexpr int    kMaxReadsPerScan = 24;
    static constexpr double kReadBudgetMs = 3.0;
    static constexpr int    kRefreshAfterMs = 20000;     // re-read a cached waystone after this
    static constexpr int    kRetryFailedReadMs = 2000;   // wait between failed first reads
    static constexpr int    kForgetAfterScans = 200;     // drop entries not seen for this many scans

    std::vector<VisibleWaystone> Scan(const PluginSDK::Context* ctx, const WaystoneData& data,
                                      const ScanOptions& options,
                                      float displayW, float displayH,
                                      std::uintptr_t hoveredAddress,
                                      StatKeyCalibrator& calibrator, ScanStats& stats);

    void Reset() { m_cache.clear(); }
    std::size_t CacheSize() const { return m_cache.size(); }

    // Every mod and aggregated stat of one item, for the Diagnostics dump.
    static std::string BuildDebugDump(const PluginSDK::Context* ctx, std::uintptr_t address,
                                      const std::string& name, const std::string& path);

private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::shared_ptr<ParsedWaystone> parsed;
        Clock::time_point readAt{};      // when `parsed` was taken
        Clock::time_point attemptAt{};   // last mod read attempt
        bool attempted = false;
        std::uint64_t lastSeenScan = 0;
    };

    std::shared_ptr<ParsedWaystone> Parse(const PluginSDK::Context* ctx, const WaystoneData& data,
                                          const ScanOptions& options,
                                          const PluginSDK::InventoryItem& item, bool readMods,
                                          StatKeyCalibrator* calibrator);

    std::unordered_map<std::uintptr_t, Entry> m_cache;
    std::uint64_t m_scanCounter = 0;
    std::uint64_t m_generation = 0;
};

}  // namespace WaystoneHelper
