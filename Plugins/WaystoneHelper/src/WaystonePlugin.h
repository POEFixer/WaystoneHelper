#pragma once
// Waystone Helper: highlights waystones in every open item window (inventory,
// stash, guild stash) with an affix-count badge, map-stat badges, affix-group
// badges, a coloured border per border group and a hover breakdown.

#include "../../../POEFixer/plugin_sdk/PluginSDK.h"

#include "config/Settings.h"
#include "game/StatKeyCalibrator.h"
#include "game/WaystoneData.h"
#include "game/WaystoneScanner.h"
#include "game/WaystoneScore.h"
#include "game/WaystoneWorker.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGuiContext;

class WaystonePlugin : public PluginSDK::Plugin {
public:
    ~WaystonePlugin() override { m_worker.Stop(); }   // hot reload unloads the DLL: no thread may outlive it

    const char* GetName() const override { return "Waystone Helper"; }
    bool WantsOverlay() const override { return true; }

    void OnEnable(bool gameOpened) override;
    void OnDisable() override;
    void DrawUI() override;
    void DrawSettings() override;
    void SaveSettings() override;

private:
    using Clock = std::chrono::steady_clock;

    // A waystone on screen with what the settings make of it.
    struct Shown {
        std::uintptr_t address = 0;
        WaystoneHelper::ScreenRect rect;
        std::string name;
        std::shared_ptr<const WaystoneHelper::ParsedWaystone> parsed;
        std::shared_ptr<const WaystoneHelper::Score> score;
    };
    struct ScoreMemo {
        std::uint64_t parsedGeneration = 0;
        std::uint64_t settingsGeneration = 0;
        std::shared_ptr<const WaystoneHelper::Score> score;
    };

    // --- lifecycle / scanning (WaystonePlugin.cpp) ---
    void BindImGui() const;
    void AdoptFrame(const WaystoneHelper::ScanFrame& frame);
    void ApplyCalibration();
    const Shown* FindHovered(float mx, float my) const;
    void PollOverlayHotkey();
    bool MenuWantsTextInput() const;
    void PushOptions();
    void ResetScanState(bool resetCalibration);
    const WaystoneHelper::StatKeyCalibrator& Calibration() const {
        return m_calibration ? *m_calibration : m_noCalibration;
    }
    void SettingsChanged(bool rereadItems, bool byPlayer = true);
    void Rescore();
    void WriteDump();
    void Save();

    // --- settings tab (ui/SettingsUi.cpp) ---
    void DrawHeader();
    void DrawSectionOverlay();
    void DrawSectionMapStats();
    void DrawSectionAffixCount();
    void DrawSectionBorders();
    void DrawSectionAffixGroups();
    void DrawSectionDiagnostics();
    bool DrawBorderRule(int index, int count);
    bool DrawAffixGroup(int index, int count);
    void DrawAffixPicker(WaystoneHelperConfig::AffixGroup& g);
    bool DrawMinInput(const char* id, int* v, int maxV, const char* zeroText);

    WaystoneHelperConfig::Settings m_settings;
    bool m_loaded = false;
    bool m_dirty = false;
    // The file exists but could not be read or parsed: it is not written until the
    // player changes a setting (the host also saves every minute and on shutdown).
    bool m_writeBlocked = false;
    std::uint64_t m_settingsGeneration = 1;
    std::string m_settingsNote;

    WaystoneHelper::WaystoneData m_data;
    std::string m_dataStatus;
    // Declared after m_data, which it reads: destroyed (and joined) before it.
    WaystoneHelper::WaystoneWorker m_worker{m_data};
    std::uint64_t m_scanEpoch = 0;       // frames scanned before the last reset are ignored
    std::uint64_t m_frameSeq = 0;        // last frame adopted
    std::shared_ptr<const WaystoneHelper::StatKeyCalibrator> m_calibration;   // from the last frame
    WaystoneHelper::StatKeyCalibrator m_noCalibration;
    WaystoneHelper::ScanStats m_lastStats;
    std::string m_calibrationNote;

    std::vector<Shown> m_shown;
    std::unordered_map<std::uintptr_t, ScoreMemo> m_scores;
    std::uint64_t m_scoredGeneration = 0;
    Clock::time_point m_lastRescore{};
    std::uint64_t m_areaCounter = 0;
    double m_scanMsAvg = 0.0;

    // Overlay toggle hotkey (bound with PluginUi::HotkeyCapture).
    bool m_overlayHidden = false;
    bool m_toggleWasDown = false;
    // The ImGui context the settings tab was last drawn in: the host's menu, which
    // lives as long as the plugin. With a separate menu window it differs from the
    // overlay's context that DrawUI runs in.
    ImGuiContext* m_menuContext = nullptr;

    // Diagnostics.
    std::uintptr_t m_lastHoveredAddress = 0;
    std::string m_lastHoveredName;
    std::string m_dumpStatus;

    // Settings-tab state.
    std::unordered_map<std::string, std::string> m_affixSearch;
    int m_selectedTab = 0;
};
