#include "WaystoneWorker.h"

#include <algorithm>

namespace WaystoneHelper {

namespace {

// With no waystone on screen a slower pace is enough to notice a window opening.
constexpr int kIdleScanIntervalMs = 450;
// Without a DrawUI heartbeat for this long the worker stops scanning.
constexpr std::int64_t kHeartbeatTimeoutMs = 400;

std::int64_t SteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void WaystoneWorker::Start(const PluginSDK::Context* ctx) {
    if (m_thread.joinable()) return;
    m_ctx = ctx;
    {
        std::lock_guard<std::mutex> lk(m_wakeMutex);
        m_stop = false;
    }
    {
        std::lock_guard<std::mutex> lk(m_inMutex);
        m_resetPending = true;
        m_resetCalibration = true;
    }
    m_active.store(false, std::memory_order_relaxed);
    m_lastBeatMs.store(0, std::memory_order_relaxed);
    m_publishedCalibration.reset();
    m_publishedObserved = -1;
    m_forceScan = true;
    m_lastVisible = 0;
    m_avgMs = 0.0;
    m_faultBackoffUntil = {};
    m_thread = std::thread(&WaystoneWorker::Loop, this);
}

void WaystoneWorker::Stop() {
    {
        std::lock_guard<std::mutex> lk(m_wakeMutex);
        m_stop = true;
    }
    m_wakeCv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    // The thread is gone: its state can be dropped from here.
    m_scanner.Reset();
    m_publishedCalibration.reset();
    std::lock_guard<std::mutex> lk(m_outMutex);
    m_frame.reset();
}

void WaystoneWorker::SetOptions(const Options& options) {
    std::lock_guard<std::mutex> lk(m_inMutex);
    m_options = options;
}

void WaystoneWorker::Reset(std::uint64_t epoch, bool resetCalibration) {
    {
        std::lock_guard<std::mutex> lk(m_inMutex);
        m_epoch = epoch;
        m_resetPending = true;
        m_resetCalibration = m_resetCalibration || resetCalibration;
    }
    m_wakeCv.notify_all();
}

void WaystoneWorker::Heartbeat(bool active, float displayW, float displayH, std::uintptr_t hoveredAddress) {
    m_displayW.store(displayW, std::memory_order_relaxed);
    m_displayH.store(displayH, std::memory_order_relaxed);
    m_hovered.store(hoveredAddress, std::memory_order_relaxed);
    m_lastBeatMs.store(SteadyMs(), std::memory_order_relaxed);
    m_active.store(active, std::memory_order_release);
}

std::shared_ptr<const ScanFrame> WaystoneWorker::Latest() const {
    std::lock_guard<std::mutex> lk(m_outMutex);
    return m_frame;
}

bool WaystoneWorker::ActiveNow() const {
    return m_active.load(std::memory_order_acquire) &&
           SteadyMs() - m_lastBeatMs.load(std::memory_order_relaxed) < kHeartbeatTimeoutMs;
}

void WaystoneWorker::Loop() {
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(m_wakeMutex);
            if (m_stop) return;
        }
        // This is the plugin's own thread: the host's SEH bridge only guards
        // host -> plugin calls. A structured fault or a C++ exception here would
        // end the whole program, so every tick is guarded twice and a faulting
        // tick only costs this pass.
        if (TickSEH(this) == 0) {
            m_faults.fetch_add(1, std::memory_order_relaxed);
            m_scanner.Reset();
            m_forceScan = true;
            m_faultBackoffUntil = Clock::now() + std::chrono::seconds(1);
        }
        const bool active = ActiveNow();
        std::unique_lock<std::mutex> lk(m_wakeMutex);
        if (m_stop) return;
        m_wakeCv.wait_for(lk, std::chrono::milliseconds(active ? 10 : 50));
    }
}

// SEH layer: no locals that need unwinding (C2712), it only forwards.
int WaystoneWorker::TickSEH(WaystoneWorker* self) {
    __try {
        return self->TickGuarded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int WaystoneWorker::TickGuarded() {
    try {
        Tick();
        return 1;
    } catch (...) {
        return 0;
    }
}

void WaystoneWorker::Tick() {
    const auto now = Clock::now();
    if (now < m_faultBackoffUntil) return;

    Options options;
    std::uint64_t epoch = 0;
    bool reset = false;
    bool resetCalibration = false;
    {
        std::lock_guard<std::mutex> lk(m_inMutex);
        options = m_options;
        epoch = m_epoch;
        reset = m_resetPending;
        resetCalibration = m_resetCalibration;
        m_resetPending = false;
        m_resetCalibration = false;
    }
    if (reset) {
        // Cached addresses may name other items now (area change), or the cached
        // reads were taken with other options: start over.
        m_scanner.Reset();
        if (resetCalibration) {
            m_calibrator.Reset(m_data.GeneratedStats().size());
            m_publishedCalibration.reset();
        }
        m_workerEpoch = epoch;
        m_forceScan = true;
        m_lastVisible = 0;
    }
    if (!m_ctx || !ActiveNow()) return;

    int interval = std::clamp(options.scanIntervalMs, WaystoneHelperConfig::kScanIntervalMinMs,
                              WaystoneHelperConfig::kScanIntervalMaxMs);
    if (m_lastVisible == 0) interval = (std::max)(interval, kIdleScanIntervalMs);
    if (!m_forceScan &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastScan).count() < interval)
        return;
    m_lastScan = now;
    m_forceScan = false;

    ScanStats stats;
    std::vector<VisibleWaystone> visible =
        m_scanner.Scan(m_ctx, m_data, options.scan, m_displayW.load(std::memory_order_relaxed),
                       m_displayH.load(std::memory_order_relaxed), m_hovered.load(std::memory_order_relaxed),
                       m_calibrator, stats);
    m_lastVisible = visible.size();
    m_avgMs = m_avgMs <= 0.0 ? stats.ms : m_avgMs * 0.9 + stats.ms * 0.1;

    // The calibration evidence only changes when a waystone was observed: copy it then.
    if (!m_publishedCalibration || m_publishedObserved != m_calibrator.ObservedItems()) {
        m_publishedCalibration = std::make_shared<const StatKeyCalibrator>(m_calibrator);
        m_publishedObserved = m_calibrator.ObservedItems();
    }

    auto frame = std::make_shared<ScanFrame>();
    frame->seq = ++m_seq;
    frame->epoch = m_workerEpoch;
    frame->visible = std::move(visible);
    frame->stats = stats;
    frame->avgMs = m_avgMs;
    frame->calibration = m_publishedCalibration;
    std::lock_guard<std::mutex> lk(m_outMutex);
    m_frame = std::move(frame);
}

}  // namespace WaystoneHelper
