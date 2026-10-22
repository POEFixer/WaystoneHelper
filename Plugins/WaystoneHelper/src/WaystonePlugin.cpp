#include "WaystonePlugin.h"

#include "game/WaystoneDataEmbedded.h"
#include "ui/Overlay.h"

#include "../../../POEFixer/core/FileIO.h"
#include "../../../POEFixer/plugin_sdk/PluginUi.h"

#include <imgui.h>

// Plugin info: the host checks it before CreatePlugin; the publish tool reads it.
POEFIXER_PLUGIN_INFO(
    "WaystoneHelper",
    "Waystone Helper",
    "1.4.3",
    "Omer Faruk ARPA, POEFixer",
    "Marks waystones in your inventory and stash with affix-count, map-stat and affix-group badges, coloured borders by your own rules, and a hover breakdown.")

#include <algorithm>
#include <cstdio>
#include <string>

using WaystoneHelper::Score;

namespace {
constexpr int kRescoreIntervalMs = 50;   // settings edits redraw at this pace at most
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WaystonePlugin::BindImGui() const {
    if (ctx()->ImGuiContext) ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
}

void WaystonePlugin::OnEnable(bool) {
    if (!HostCompatible()) {
        ctx()->Log.Error("Waystone Helper: this Fixer version is too old for the plugin; update Fixer.");
        return;
    }
    BindImGui();
    m_worker.Stop();   // it reads m_data, which is loaded again below

    m_settings = WaystoneHelperConfig::Settings{};
    WaystoneHelperConfig::LoadResult loadResult = WaystoneHelperConfig::LoadResult::Missing;
    m_settingsNote = m_settings.Load(DirectoryPath(), &loadResult);
    m_loaded = true;
    m_writeBlocked = loadResult == WaystoneHelperConfig::LoadResult::Unreadable ||
                     loadResult == WaystoneHelperConfig::LoadResult::Unparsable;
    // Write a migrated file back once; an unusable file stays as it is.
    m_dirty = !m_settingsNote.empty() && !m_writeBlocked;

    std::string text;
    text.reserve(WaystoneHelper::Embedded::kWaystoneDataSize);
    for (std::size_t i = 0; i < WaystoneHelper::Embedded::kWaystoneDataChunkCount; ++i)
        text += WaystoneHelper::Embedded::kWaystoneDataChunks[i];
    m_dataStatus = m_data.LoadFromText(text);

    PushOptions();
    ResetScanState(true);
    m_overlayHidden = false;
    m_toggleWasDown = false;
    m_areaCounter = ctx()->Game.GetAreaChangeCounter();
    m_worker.Start(ctx());
    if (m_writeBlocked) ctx()->Log.Warn(("Waystone Helper: " + m_settingsNote).c_str());
    else if (!m_settingsNote.empty()) ctx()->Log.Info(("Waystone Helper: " + m_settingsNote).c_str());
}

void WaystonePlugin::OnDisable() {
    // The host calls SaveSettings() right before this.
    m_worker.Stop();
    ResetScanState(true);
}

void WaystonePlugin::SaveSettings() {
    if (m_loaded) Save();
}

void WaystonePlugin::Save() {
    if (m_writeBlocked) {
        m_dirty = false;   // nothing the player chose yet: leave the unreadable file alone
        return;
    }
    if (m_settings.Save(DirectoryPath())) {
        m_dirty = false;
    } else {
        m_dirty = false;   // do not retry every frame; the next edit tries again
        ctx()->Log.Warn("Waystone Helper: could not write config/settings.json");
    }
}

// What the worker needs from the settings; cheap, sent on every change.
void WaystonePlugin::PushOptions() {
    WaystoneHelper::WaystoneWorker::Options o;
    o.scan.readMods = m_settings.readMods;
    o.scan.statAggregateKeys = m_settings.statAggregateKeys;
    o.scanIntervalMs = m_settings.scanIntervalMs;
    m_worker.SetOptions(o);
}

// Drops what is shown and tells the worker to drop its cached reads; its next
// scan runs at once in the new epoch, and older frames are ignored.
void WaystonePlugin::ResetScanState(bool resetCalibration) {
    m_shown.clear();
    m_scores.clear();
    m_lastStats = {};
    if (resetCalibration) m_calibration.reset();
    m_worker.Reset(++m_scanEpoch, resetCalibration);
}

// A display edit only re-scores the waystones already found (no new scan, so a
// slider drag does not scan every frame); a detection change re-reads them. Only an
// edit by the player lifts the write block of an unreadable settings file.
void WaystonePlugin::SettingsChanged(bool rereadItems, bool byPlayer) {
    ++m_settingsGeneration;
    m_dirty = true;
    if (byPlayer) m_writeBlocked = false;
    PushOptions();
    if (rereadItems) ResetScanState(true);
}

void WaystonePlugin::Rescore() {
    for (auto& s : m_shown) {
        ScoreMemo& memo = m_scores[s.address];
        if (memo.score && memo.settingsGeneration == m_settingsGeneration &&
            memo.parsedGeneration == s.parsed->generation)
            continue;
        memo.parsedGeneration = s.parsed->generation;
        memo.settingsGeneration = m_settingsGeneration;
        memo.score = std::make_shared<const Score>(WaystoneHelper::Evaluate(m_settings, m_data, *s.parsed, s.name));
        s.score = memo.score;
    }
    m_scoredGeneration = m_settingsGeneration;
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawUI() {
    if (!m_loaded || !ctx()->ImGuiContext) return;
    BindImGui();

    const ImGuiIO& io = ImGui::GetIO();
    // The worker scans only while this heartbeat says so; every early return idles it.
    auto idle = [&] { m_worker.Heartbeat(false, io.DisplaySize.x, io.DisplaySize.y, 0); };

    if (!ctx()->Game.IsInGame()) {
        if (!m_shown.empty()) m_shown.clear();
        idle();
        return;
    }
    // Item addresses do not survive an area change.
    const std::uint64_t area = ctx()->Game.GetAreaChangeCounter();
    if (area != m_areaCounter) {
        m_areaCounter = area;
        ResetScanState(false);
    }
    if (!ctx()->Game.IsForeground()) {
        idle();
        return;
    }

    PollOverlayHotkey();
    if (m_overlayHidden) {
        idle();
        return;
    }

    // Hover only counts over the game, not over a Fixer window.
    const bool mouseOverGame = !io.WantCaptureMouse;
    const ImVec2 mouse = io.MousePos;
    const Shown* hovered = mouseOverGame ? FindHovered(mouse.x, mouse.y) : nullptr;
    // The hovered waystone is re-read every scan, so currency used on it shows at once.
    m_worker.Heartbeat(true, io.DisplaySize.x, io.DisplaySize.y, hovered ? hovered->address : 0);

    // Take the worker's newest scan (it reads the game; this thread only scores and draws).
    const auto now = Clock::now();
    const auto frame = m_worker.Latest();
    if (frame && frame->seq != m_frameSeq && frame->epoch == m_scanEpoch) {
        AdoptFrame(*frame);
        hovered = mouseOverGame ? FindHovered(mouse.x, mouse.y) : nullptr;
    } else if (m_scoredGeneration != m_settingsGeneration &&
               std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRescore).count() >= kRescoreIntervalMs) {
        m_lastRescore = now;
        Rescore();
    }
    if (hovered) {
        m_lastHoveredAddress = hovered->address;
        m_lastHoveredName = hovered->name;
    }
    if (m_shown.empty()) return;

    // Under the Fixer menu while it is open, over the game otherwise.
    ImDrawList* dl = ctx()->Game.IsMenuVisible() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
    for (const auto& s : m_shown) {
        if (!s.score->matched) continue;
        if (m_settings.hideWhenHovered && hovered && s.address == hovered->address) continue;
        WaystoneHelper::Overlay::DrawWaystone(dl, s.rect, *s.score, m_settings);
    }
    if (m_settings.showHoverBreakdown && hovered && WaystoneHelper::WorthBreakdown(*hovered->score))
        WaystoneHelper::Overlay::DrawBreakdown(dl, hovered->rect, *hovered->score);
}

void WaystonePlugin::AdoptFrame(const WaystoneHelper::ScanFrame& frame) {
    m_frameSeq = frame.seq;
    m_lastStats = frame.stats;
    m_scanMsAvg = frame.avgMs;
    m_calibration = frame.calibration;
    const auto& visible = frame.visible;

    // Scores are kept per item while neither its mods nor the settings changed.
    std::unordered_map<std::uintptr_t, ScoreMemo> scores;
    scores.reserve(visible.size());
    std::vector<Shown> shown;
    shown.reserve(visible.size());
    for (const auto& v : visible) {
        ScoreMemo memo;
        auto it = m_scores.find(v.address);
        if (it != m_scores.end() && it->second.parsedGeneration == v.parsed->generation &&
            it->second.settingsGeneration == m_settingsGeneration) {
            memo = it->second;
        } else {
            memo.parsedGeneration = v.parsed->generation;
            memo.settingsGeneration = m_settingsGeneration;
            memo.score = std::make_shared<const Score>(WaystoneHelper::Evaluate(m_settings, m_data, *v.parsed, v.name));
        }
        Shown s;
        s.address = v.address;
        s.rect = v.rect;
        s.name = v.name;
        s.parsed = v.parsed;
        s.score = memo.score;
        scores.emplace(s.address, std::move(memo));
        shown.push_back(std::move(s));
    }
    m_scores = std::move(scores);
    m_shown = std::move(shown);
    m_scoredGeneration = m_settingsGeneration;

    ApplyCalibration();
}

// After a game patch the stat keys move: switch to the keys the waystones prove.
void WaystonePlugin::ApplyCalibration() {
    if (!m_calibration) return;
    const auto& tracked = m_data.GeneratedStats();
    std::string changed;
    for (std::size_t i = 0; i < tracked.size(); ++i) {
        int* slot = m_settings.statAggregateKeys.Slot(tracked[i].statId);
        if (!slot) continue;
        const int replacement = m_calibration->Replacement(i, *slot);
        if (replacement == 0) continue;
        if (!changed.empty()) changed += ", ";
        changed += tracked[i].displayName + " " + std::to_string(*slot) + " -> " + std::to_string(replacement);
        *slot = replacement;
    }
    if (changed.empty()) return;
    m_calibrationNote = "Stat keys updated after a game change: " + changed + ".";
    ctx()->Log.Info(("Waystone Helper: " + m_calibrationNote).c_str());
    SettingsChanged(true, /*byPlayer=*/false);
    Save();
}

const WaystonePlugin::Shown* WaystonePlugin::FindHovered(float mx, float my) const {
    for (const auto& s : m_shown)
        if (s.rect.Contains(mx, my)) return &s;
    return nullptr;
}

void WaystonePlugin::PollOverlayHotkey() {
    const int vk = m_settings.toggleOverlayVk;
    if (vk <= 0) {
        m_overlayHidden = false;
        m_toggleWasDown = false;
        return;
    }
    // Not while a key is being bound in the settings tab (the kit's capture
    // state) or while the player types into a Fixer text field. Marking the key
    // as held also makes a freshly bound key wait for its release before it
    // toggles anything.
    if (PluginUi::detail::CapturingId() != 0 || ImGui::GetIO().WantTextInput) {
        m_toggleWasDown = true;
        return;
    }
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (down && !m_toggleWasDown) m_overlayHidden = !m_overlayHidden;
    m_toggleWasDown = down;
}

void WaystonePlugin::WriteDump() {
    std::string all = "[Waystone Helper " + std::string(PluginSDK_GetInfo()->version) + "] waystone dump\n";
    all += "visible waystones: " + std::to_string(m_shown.size()) + "\n";
    const auto& tracked = m_data.GeneratedStats();
    for (const auto& t : tracked)
        all += "stat key " + t.displayName + " = " + std::to_string(m_settings.statAggregateKeys.For(t.statId)) + "\n";
    all += "\n";
    int n = 0;
    for (const auto& s : m_shown) {
        if (n++ >= 12) break;
        all += "==================================================\n";
        all += WaystoneHelper::WaystoneScanner::BuildDebugDump(ctx(), s.address, s.name,
                                                               s.parsed ? s.parsed->sourcePath : std::string());
        all += "\n";
    }
    try {
        const auto dir = DirectoryPath() / "debug";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (FileIO::AtomicWrite(dir / "waystone-dump.txt", all))
            m_dumpStatus = "Written to debug\\waystone-dump.txt (" + std::to_string(std::min(n, 12)) + " waystones).";
        else
            m_dumpStatus = "Could not write debug\\waystone-dump.txt.";
    } catch (...) {
        m_dumpStatus = "Could not write debug\\waystone-dump.txt.";
    }
}

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() { return new WaystonePlugin(); }
extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* p) { delete p; }
