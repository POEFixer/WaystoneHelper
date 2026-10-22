#pragma once
// The scan worker: everything that reads the game runs here, off the render
// thread. It copies the host's inventory publication, finds the waystones drawn
// on screen, reads their mods within the scanner's budget, feeds the stat-key
// calibrator and publishes an immutable ScanFrame. DrawUI only scores (memoized
// per item) and draws the latest frame.
//
// Inputs from the render thread: a heartbeat every DrawUI (active, display size,
// the hovered waystone), the scan options on every settings change, and a reset
// (new epoch) whenever the cached items must be dropped (area change, detection
// settings, new stat keys). Without a recent heartbeat the worker idles.

#include "StatKeyCalibrator.h"
#include "WaystoneData.h"
#include "WaystoneScanner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace WaystoneHelper {

// One published scan. Never modified after publication.
struct ScanFrame {
    std::uint64_t seq = 0;
    std::uint64_t epoch = 0;          // the reset epoch the scan ran in
    std::vector<VisibleWaystone> visible;
    ScanStats stats;
    double avgMs = 0.0;
    std::shared_ptr<const StatKeyCalibrator> calibration;   // never null
};

class WaystoneWorker {
public:
    struct Options {
        ScanOptions scan;
        int scanIntervalMs = WaystoneHelperConfig::kScanIntervalDefault;
    };

    // `data` must outlive the worker and must not change while it runs.
    explicit WaystoneWorker(const WaystoneData& data) : m_data(data) {}
    ~WaystoneWorker() { Stop(); }
    WaystoneWorker(const WaystoneWorker&) = delete;
    WaystoneWorker& operator=(const WaystoneWorker&) = delete;

    void Start(const PluginSDK::Context* ctx);
    void Stop();   // joins the thread; safe to call twice

    // Render thread, on every settings change (and once before Start).
    void SetOptions(const Options& options);
    // Render thread: drop the cached items (and the calibration evidence when asked)
    // before the next scan, which then runs at once. Frames carry the epoch they
    // were scanned in, so the caller can ignore frames from before the reset.
    void Reset(std::uint64_t epoch, bool resetCalibration);
    // Render thread, every DrawUI.
    void Heartbeat(bool active, float displayW, float displayH, std::uintptr_t hoveredAddress);

    std::shared_ptr<const ScanFrame> Latest() const;
    std::uint64_t Faults() const { return m_faults.load(std::memory_order_relaxed); }

private:
    using Clock = std::chrono::steady_clock;

    void Loop();
    static int TickSEH(WaystoneWorker* self);
    int TickGuarded();
    void Tick();
    bool ActiveNow() const;

    const WaystoneData& m_data;
    const PluginSDK::Context* m_ctx = nullptr;
    std::thread m_thread;
    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCv;
    bool m_stop = false;

    // Inputs.
    mutable std::mutex m_inMutex;
    Options m_options;
    std::uint64_t m_epoch = 0;
    bool m_resetPending = true;
    bool m_resetCalibration = true;
    std::atomic<bool> m_active{false};
    std::atomic<std::int64_t> m_lastBeatMs{0};
    std::atomic<float> m_displayW{0.f};
    std::atomic<float> m_displayH{0.f};
    std::atomic<std::uintptr_t> m_hovered{0};

    // Output.
    mutable std::mutex m_outMutex;
    std::shared_ptr<const ScanFrame> m_frame;
    std::atomic<std::uint64_t> m_faults{0};

    // Worker-thread state.
    WaystoneScanner m_scanner;
    StatKeyCalibrator m_calibrator;
    std::shared_ptr<const StatKeyCalibrator> m_publishedCalibration;
    int m_publishedObserved = -1;
    std::uint64_t m_workerEpoch = 0;
    std::uint64_t m_seq = 0;
    bool m_forceScan = true;
    std::size_t m_lastVisible = 0;
    double m_avgMs = 0.0;
    Clock::time_point m_lastScan{};
    Clock::time_point m_faultBackoffUntil{};
};

}  // namespace WaystoneHelper
