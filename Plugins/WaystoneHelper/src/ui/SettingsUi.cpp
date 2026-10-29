// Waystone Helper settings tab, built on the plugin UI kit (PluginUi.h) like the
// Health Bars / Atlas tabs: a status card, sub-tabs of padded cards (one
// measured label column per card, controls capped at ~15 em, wrapped tooltips,
// dimmed dependents), and a collapsed Diagnostics card at the bottom.
// Every text is a key of lang/en.json, looked up here in the program language
// (PluginI18n) every frame; the affix labels are "affix_<id>" keys.

#include "../WaystonePlugin.h"
#include "TextUtil.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "../../../../POEFixer/plugin_sdk/PluginUi.h"

#include <imgui.h>
#include <imgui_internal.h>  // ImTextInitClassifiers

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace Ui = PluginUi;
namespace I18n = PluginSDK::I18n;

namespace {

void TruncateUtf8(std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return;
    std::size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
}

std::string Num(int v) { return std::to_string(v); }
std::string Num(std::size_t v) { return std::to_string(v); }

// A number with two decimals for a TrF argument (the format is code, never a text).
std::string Num2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// "<icon>  <text>###<id>": a label whose ImGui id is the same in every language.
std::string IconLabel(const char* icon, std::string_view text, const char* id) {
    return std::string(icon) + "  " + std::string(text) + "###" + id;
}

// Marks where a slider's own number goes in a translated text (a TrF argument); NumberFormat
// turns the first mark into the conversion (TextUtil.h).
constexpr const char* kValue = WaystoneHelper::kNumberMark;
using WaystoneHelper::NumberFormat;

// Lower case for searching translated texts: ASCII, Latin-1 letters and Cyrillic
// (the scripts of the program languages that have case), other bytes unchanged.
std::string FoldCase(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c + ('a' - 'A'));
        } else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d = static_cast<unsigned char>(d + 0x20);  // U+00C0..U+00DE
            out += static_cast<char>(c);
            out += static_cast<char>(d);
            ++i;
        } else if (c == 0xD0 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x90 && d <= 0x9F) {          // U+0410..U+041F -> U+0430..U+043F
                out += static_cast<char>(0xD0);
                out += static_cast<char>(d + 0x20);
            } else if (d >= 0xA0 && d <= 0xAF) {   // U+0420..U+042F -> U+0440..U+044F
                out += static_cast<char>(0xD1);
                out += static_cast<char>(d - 0x20);
            } else if (d >= 0x80 && d <= 0x8F) {   // U+0400..U+040F -> U+0450..U+045F
                out += static_cast<char>(0xD1);
                out += static_cast<char>(d + 0x10);
            } else {
                out += static_cast<char>(c);
                out += static_cast<char>(d);
            }
            ++i;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// The score's words in the program language, for the helpers shared with it.
std::string TranslateWords(const char* key, WaystoneHelper::Words::Args args) { return I18n::TrF(key, args); }
const WaystoneHelper::Words& UiWords() {
    static const WaystoneHelper::Words words{&TranslateWords};
    return words;
}

// An affix's label in the program language with the table's numbers; the table's
// English when no language file has it (Tr gives the key back).
std::string AffixLabel(const WaystoneHelper::Affix& a) {
    const char* text = I18n::Tr(a.LabelKey.c_str());
    return a.LabelKey == text ? a.Label : WaystoneHelper::FillNumbers(text, a.Numbers);
}

// The first line of an affix label (lines are joined by " / ").
std::string FirstLine(const std::string& label) {
    const auto cut = label.find(" / ");
    return cut == std::string::npos ? label : label.substr(0, cut);
}

// One-line summary of a border group's conditions.
std::string RuleSummary(const WaystoneHelperConfig::BorderRule& r, const WaystoneHelper::WaystoneData& data) {
    std::string s;
    auto add = [&](const std::string& part) {
        if (!s.empty()) s += "  \xC2\xB7  ";
        s += part;
    };
    for (const auto& g : data.GeneratedStats()) {
        const int min = r.StatMin(g.statId);
        if (min > 0)
            add(I18n::TrF("rule_summary_stat", { { "stat", WaystoneHelper::StatNameText(UiWords(), g) }, { "min", Num(min) } }));
    }
    if (r.minTier > 0) add(I18n::TrF("rule_summary_tier", { { "min", Num(r.minTier) } }));
    if (r.minAffixes > 0) add(I18n::TrF("rule_summary_affixes", { { "min", Num(r.minAffixes) } }));
    if (r.corrupted == WaystoneHelperConfig::kCorruptedOnly) add(I18n::Tr("breakdown_corrupted"));
    if (r.corrupted == WaystoneHelperConfig::kCorruptedNever) add(I18n::Tr("breakdown_not_corrupted"));
    if (r.rarity >= 0) add(WaystoneHelper::RarityText(UiWords(), r.rarity));
    return s;
}

// Width of the move / delete buttons of a group title row.
float GroupRowButtonsWidth() {
    return Ui::ButtonWidth(ICON_FA_CHEVRON_UP) + Ui::ButtonWidth(ICON_FA_CHEVRON_DOWN) +
           Ui::ButtonWidth(ICON_FA_TRASH_CAN) + ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

// Name field of a group title row: the room left before the buttons, at most
// ~20 em so the name does not stretch across a wide pane.
float GroupNameWidth() {
    const float room = ImGui::GetContentRegionAvail().x - GroupRowButtonsWidth() - ImGui::GetStyle().ItemSpacing.x;
    return std::clamp(room, ImGui::GetFontSize() * 6.0f, ImGui::GetFontSize() * 20.0f);
}

// A group's name field: it shows the name as the overlay does (the plugin's own
// default names in the program language); an edit stores what was typed.
bool GroupNameField(std::string& name) {
    std::string shown = WaystoneHelper::GroupNameText(UiWords(), name);
    ImGui::SetNextItemWidth(GroupNameWidth());
    if (!Ui::InputString("##name", I18n::Tr("group_name_hint"), shown)) return false;
    TruncateUtf8(shown, WaystoneHelperConfig::kMaxNameLen);
    name = std::move(shown);
    return true;
}

// Icon buttons of a group title row; returns which was pressed (0 none, 1 up, 2 down, 3 delete).
int GroupRowButtons(int index, int count) {
    ImGui::SameLine();
    Ui::AlignRight(GroupRowButtonsWidth());
    int action = 0;
    ImGui::BeginDisabled(index == 0);
    if (Ui::SubtleButton(ICON_FA_CHEVRON_UP "###up")) action = 1;
    Ui::Tip(I18n::Tr("group_move_up_tip"));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= count);
    if (Ui::SubtleButton(ICON_FA_CHEVRON_DOWN "###down")) action = 2;
    Ui::Tip(I18n::Tr("group_move_down_tip"));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (Ui::SubtleButton(ICON_FA_TRASH_CAN "###del")) ImGui::OpenPopup("##confirmDelete");
    Ui::Tip(I18n::Tr("group_delete_tip"));
    if (Ui::ConfirmPopup("##confirmDelete", I18n::Tr("group_delete_confirm"), I18n::Tr("group_delete_yes"),
                         I18n::Tr("group_delete_keep")))
        action = 3;
    return action;
}

// The text, or its longest prefix plus "..." that fits maxW (UTF-8 safe; a
// binary search, so a long list stays cheap).
std::string FitWithEllipsis(const std::string& text, float maxW) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxW) return text;
    const float dotsW = ImGui::CalcTextSize("...").x;
    std::vector<std::size_t> cuts;   // character starts after the first one
    for (std::size_t i = 1; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    // The longest prefix (ending at a cut) that fits with the dots.
    int lo = -1, hi = static_cast<int>(cuts.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        const std::size_t n = cuts[static_cast<std::size_t>(mid)];
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + n).x + dotsW <= maxW) lo = mid;
        else hi = mid - 1;
    }
    std::string out = lo >= 0 ? text.substr(0, cuts[static_cast<std::size_t>(lo)]) : std::string();
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "...";
}

// Text that wraps at the right edge of the current region.
void WrappedDisabled(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().TextDisabled);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}  // namespace

// ---------------------------------------------------------------------------
// Tab
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSettings() {
    if (!m_loaded || !ctx()->ImGuiContext) return;
    BindImGui();
    m_menuContext = ImGui::GetCurrentContext();   // see PollOverlayHotkey
    // ImGui 1.93 keeps its word-wrap character classes in a per-DLL static that
    // only the copy which builds the font atlas initialises (the host's). This
    // DLL's copy wraps our text, so set it up here (idempotent, one check).
    ImTextInitClassifiers();
    ImGui::PushID("waystoneHelper");

    ImGui::Spacing();
    DrawHeader();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(IconLabel(ICON_FA_EYE, I18n::Tr("tab_display"), "display").c_str())) {
            ImGui::Spacing();
            const bool table = Ui::WideEnoughForTwoColumns() &&
                               ImGui::BeginTable("##displayCols", 2, ImGuiTableFlags_SizingStretchSame);
            if (table) ImGui::TableNextColumn();
            DrawSectionOverlay();
            ImGui::Spacing();
            DrawSectionAffixCount();
            if (table) ImGui::TableNextColumn();
            else ImGui::Spacing();
            DrawSectionMapStats();
            if (table) ImGui::EndTable();
            ImGui::EndTabItem();
        }
        const std::string borders = IconLabel(
            ICON_FA_SQUARE, I18n::TrF("tab_borders", { { "count", Num(m_settings.borderRules.size()) } }), "borders");
        if (ImGui::BeginTabItem(borders.c_str())) {
            ImGui::Spacing();
            DrawSectionBorders();
            ImGui::EndTabItem();
        }
        const std::string groups = IconLabel(
            ICON_FA_LAYER_GROUP, I18n::TrF("tab_groups", { { "count", Num(m_settings.affixGroups.size()) } }), "groups");
        if (ImGui::BeginTabItem(groups.c_str())) {
            ImGui::Spacing();
            DrawSectionAffixGroups();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();
    DrawSectionDiagnostics();
    ImGui::Spacing();
    PluginUi::Hint("Originally created by Omer Faruk ARPA. Maintained by POEFixer. GPL-3.0.");
    ImGui::Spacing();
    ImGui::PopID();

    // Write once an edit is complete, not every frame of a drag.
    if (m_dirty && !ImGui::IsAnyItemActive()) Save();
}

// Status card: what the plugin does, whether the overlay is shown, Reset display.
void WaystonePlugin::DrawHeader() {
    Ui::BeginCard("##status");
    Ui::Heading(ICON_FA_MAP, I18n::Tr("header_title"), I18n::Tr("header_help"));
    if (m_overlayHidden)
        Ui::Hint(I18n::TrF("header_hidden_hint", { { "key", Ui::KeyName(m_settings.toggleOverlayVk) } }).c_str());
    else
        Ui::Hint(I18n::Tr("header_hint"));
    ImGui::Spacing();

    const Ui::Palette pal = Ui::Colors();
    if (m_overlayHidden)
        Ui::StatusPill(I18n::Tr("header_hidden"), ImGui::ColorConvertFloat4ToU32(pal.Warning));
    else
        Ui::StatusPill(I18n::Tr("header_active"), ImGui::ColorConvertFloat4ToU32(pal.Success));
    Ui::Tip(I18n::Tr("header_pill_tip"));

    const std::string resetLabel = IconLabel(ICON_FA_ROTATE_LEFT, I18n::Tr("header_reset"), "resetDisplay");
    const float w = Ui::ButtonWidth(resetLabel.c_str());
    ImGui::SameLine();
    // On a narrow pane the button takes its own line instead of running off the edge.
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
    Ui::AlignRight(w);
    if (Ui::SubtleButton(resetLabel.c_str())) ImGui::OpenPopup("##resetDisplayConfirm");
    Ui::Tip(I18n::Tr("header_reset_tip"));
    if (Ui::ConfirmPopup("##resetDisplayConfirm", I18n::Tr("header_reset_confirm"), I18n::Tr("header_reset_yes"),
                         I18n::Tr("header_reset_no"))) {
        m_settings.ResetDisplay();
        SettingsChanged(false);
    }
    Ui::EndCard();
}

// ---------------------------------------------------------------------------
// Display tab
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSectionOverlay() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##overlay");
    Ui::Heading(ICON_FA_EYE, I18n::Tr("overlay_title"));
    {
        const char* hotkeyLabel = I18n::Tr("overlay_hotkey");
        const char* sizeLabel = I18n::Tr("overlay_badge_size");
        const char* backgroundLabel = I18n::Tr("overlay_badge_background");
        const char* refreshLabel = I18n::Tr("overlay_refresh");
        const Ui::LabelColumn col({hotkeyLabel, sizeLabel, backgroundLabel, refreshLabel});
        col.RowNoWidth(hotkeyLabel);
        if (Ui::HotkeyCapture("##hotkey", &s.toggleOverlayVk, I18n::Tr("overlay_hotkey_tip")))
            changed = true;

        col.Row(sizeLabel);
        int pct = static_cast<int>(std::lround(s.badgeScale * 100.0f));
        const std::string pctFormat = NumberFormat(I18n::TrF("unit_percent", { { "value", kValue } }));
        if (ImGui::SliderInt("##badgeScale", &pct, static_cast<int>(WaystoneHelperConfig::kBadgeScaleMin * 100),
                             static_cast<int>(WaystoneHelperConfig::kBadgeScaleMax * 100), pctFormat.c_str(),
                             ImGuiSliderFlags_AlwaysClamp)) {
            s.badgeScale = static_cast<float>(pct) / 100.0f;
            changed = true;
        }
        Ui::Tip(I18n::Tr("overlay_badge_size_tip"));

        col.RowNoWidth(backgroundLabel);
        changed |= Ui::Swatch("##badgeBg", s.badgeBackgroundColor);
        Ui::Tip(I18n::Tr("overlay_badge_background_tip"));

        col.Row(refreshLabel);
        const std::string msFormat = NumberFormat(I18n::TrF("unit_ms", { { "value", kValue } }));
        changed |= ImGui::SliderInt("##scanInterval", &s.scanIntervalMs, WaystoneHelperConfig::kScanIntervalMinMs,
                                    WaystoneHelperConfig::kScanIntervalMaxMs, msFormat.c_str(), ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip(I18n::Tr("overlay_refresh_tip"));
    }
    changed |= Ui::Check(I18n::Tr("overlay_hide_hovered"), "hideHovered", &s.hideWhenHovered,
                         I18n::Tr("overlay_hide_hovered_tip"));
    changed |= Ui::Check(I18n::Tr("overlay_breakdown"), "breakdown", &s.showHoverBreakdown,
                         I18n::Tr("overlay_breakdown_tip"));
    Ui::EndCard();
    if (changed) SettingsChanged(false);
}

void WaystonePlugin::DrawSectionAffixCount() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##affixCount");
    Ui::Heading(ICON_FA_HASHTAG, I18n::Tr("count_title"));
    changed |= Ui::Check(I18n::Tr("count_show_badge"), "affixBadge", &s.showAffixCountBadge,
                         I18n::Tr("count_show_badge_tip"));
    ImGui::BeginDisabled(!s.showAffixCountBadge);
    {
        const char* fromLabel = I18n::Tr("count_show_from");
        const char* colourLabel = I18n::Tr("count_badge_colour");
        const Ui::LabelColumn col({fromLabel, colourLabel});
        col.Row(fromLabel);
        const std::string fmt = NumberFormat(I18n::TrF("count_format", { { "value", kValue } }));
        changed |= ImGui::SliderInt("##target", &s.targetAffixCount, WaystoneHelperConfig::kTargetAffixMin,
                                    WaystoneHelperConfig::kTargetAffixMax, fmt.c_str(), ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip(I18n::Tr("count_show_from_tip"));
        col.RowNoWidth(colourLabel);
        changed |= Ui::Swatch("##affixColor", s.affixCountBadgeColor);
        Ui::Tip(I18n::Tr("count_badge_colour_tip"));
    }
    ImGui::EndDisabled();
    Ui::EndCard();
    if (changed) SettingsChanged(false);
}

void WaystonePlugin::DrawSectionMapStats() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##mapStats");
    Ui::Heading(ICON_FA_LIST_CHECK, I18n::Tr("stats_title"), I18n::Tr("stats_help"));
    changed |= Ui::Check(I18n::Tr("stats_show_badges"), "statBadges", &s.highlightImportantAffixes,
                         I18n::Tr("stats_show_badges_tip"));
    ImGui::BeginDisabled(!s.highlightImportantAffixes);
    // One column for the swatches: the widest "Name (code)" checkbox.
    float widest = 0.0f;
    std::vector<std::string> labels;
    for (const auto& g : m_data.GeneratedStats()) {
        labels.push_back(I18n::TrF("stats_name_code", { { "name", WaystoneHelper::StatNameText(UiWords(), g) },
                                                        { "code", g.badgeLabel } }));
        widest = std::max(widest, ImGui::CalcTextSize(labels.back().c_str()).x);
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x0 = ImGui::GetCursorPosX();
    const float swatchX = x0 + ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + widest + style.ItemSpacing.x * 2.0f;
    const auto& stats = m_data.GeneratedStats();
    for (std::size_t i = 0; i < stats.size(); ++i) {
        bool* show = s.StatDisplay(stats[i].statId);
        float* color = s.StatColor(stats[i].statId);
        if (!show || !color) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetCursorPosX(x0);
        changed |= ImGui::Checkbox((labels[i] + "###show").c_str(), show);
        Ui::Tip(I18n::Tr("stats_show_tip"));
        ImGui::SameLine();
        ImGui::SetCursorPosX(swatchX);
        changed |= Ui::Swatch("##color", color);
        Ui::Tip(I18n::Tr("stats_colour_tip"));
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    Ui::EndCard();
    if (changed) SettingsChanged(false);
}

// ---------------------------------------------------------------------------
// Border groups
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSectionBorders() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##bordersCard");
    Ui::Heading(ICON_FA_SQUARE, I18n::Tr("borders_title"), I18n::Tr("borders_help"));
    changed |= Ui::Check(I18n::Tr("borders_draw"), "drawBorders", &s.borderEnabled, I18n::Tr("borders_draw_tip"));
    ImGui::BeginDisabled(!s.borderEnabled);
    {
        const char* thicknessLabel = I18n::Tr("borders_thickness");
        const Ui::LabelColumn col({thicknessLabel});
        const std::string pxFormat = NumberFormat(I18n::TrF("unit_px", { { "value", kValue } }));
        changed |= Ui::SliderIntRow(col, thicknessLabel, "##thickness", &s.borderThickness,
                                    WaystoneHelperConfig::kBorderThicknessMin, WaystoneHelperConfig::kBorderThicknessMax,
                                    pxFormat.c_str(), I18n::Tr("borders_thickness_tip"));
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(s.borderRules.size() >= WaystoneHelperConfig::kMaxGroups);
    if (ImGui::Button(IconLabel(ICON_FA_PLUS, I18n::Tr("borders_add"), "addRule").c_str())) {
        WaystoneHelperConfig::BorderRule r;
        r.id = s.MakeId("b");
        r.name = I18n::TrF("group_name_numbered", { { "number", Num(s.borderRules.size() + 1) } });
        s.borderRules.push_back(std::move(r));
        changed = true;
    }
    Ui::Tip(I18n::Tr("group_add_tip"));
    ImGui::EndDisabled();
    if (s.borderRules.empty()) Ui::Hint(I18n::Tr("borders_empty_hint"));
    ImGui::EndDisabled();   // a disabled scope never crosses a card (child window) edge
    Ui::EndCard();

    ImGui::BeginDisabled(!s.borderEnabled);
    for (int i = 0; i < static_cast<int>(s.borderRules.size()); ++i) {
        ImGui::Spacing();
        if (DrawBorderRule(i, static_cast<int>(s.borderRules.size()))) break;   // list changed: redraw next frame
    }
    ImGui::EndDisabled();
    if (changed) SettingsChanged(false);
}

bool WaystonePlugin::DrawBorderRule(int index, int count) {
    auto& r = m_settings.borderRules[static_cast<size_t>(index)];
    bool changed = false;
    bool structural = false;
    ImGui::PushID(r.id.c_str());
    Ui::BeginCard("##rule");

    // Title row: on/off, colour, name, move and delete.
    changed |= ImGui::Checkbox("##enabled", &r.enabled);
    Ui::Tip(I18n::Tr("group_enabled_tip"));
    ImGui::SameLine();
    changed |= Ui::Swatch("##color", r.color);
    Ui::Tip(I18n::Tr("rule_colour_tip"));
    ImGui::SameLine();
    changed |= GroupNameField(r.name);
    const int action = GroupRowButtons(index, count);

    const std::string summary = RuleSummary(r, m_data);
    if (summary.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Warning);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(I18n::Tr("rule_no_conditions"));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        Ui::Hint(summary.c_str());
    }

    ImGui::BeginDisabled(!r.enabled);
    if (ImGui::TreeNodeEx(I18n::TrId("rule_conditions"), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<std::string> statLabels;
        std::vector<const char*> keys;
        for (const auto& g : m_data.GeneratedStats())
            statLabels.push_back(I18n::TrF("rule_stat_at_least", { { "stat", WaystoneHelper::StatNameText(UiWords(), g) } }));
        for (const auto& l : statLabels) keys.push_back(l.c_str());
        const char* tierLabel = I18n::Tr("rule_tier_at_least");
        const char* affixesLabel = I18n::Tr("rule_affixes_at_least");
        const char* corruptionLabel = I18n::Tr("rule_corruption");
        const char* rarityLabel = I18n::Tr("rule_rarity");
        keys.push_back(tierLabel);
        keys.push_back(affixesLabel);
        keys.push_back(corruptionLabel);
        keys.push_back(rarityLabel);
        const Ui::LabelColumn col(keys);
        const std::string anyFormat = NumberFormat(I18n::Tr("rule_any"));
        const auto& stats = m_data.GeneratedStats();
        for (std::size_t i = 0; i < stats.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            col.Row(statLabels[i].c_str());
            int v = r.StatMin(stats[i].statId);
            if (DrawMinInput("##stat", &v, WaystoneHelperConfig::kStatThresholdMax, anyFormat.c_str())) {
                if (v > 0) r.statMins[stats[i].statId] = v;
                else r.statMins.erase(stats[i].statId);
                changed = true;
            }
            Ui::Tip(I18n::Tr("rule_stat_tip"));
            ImGui::PopID();
        }
        col.Row(tierLabel);
        const std::string tierFormat = NumberFormat(I18n::TrF("rule_tier_format", { { "value", kValue } }));
        changed |= ImGui::SliderInt("##tier", &r.minTier, 0, WaystoneHelperConfig::kMinTierMax,
                                    r.minTier == 0 ? anyFormat.c_str() : tierFormat.c_str(), ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip(I18n::Tr("rule_tier_tip"));
        col.Row(affixesLabel);
        changed |= ImGui::SliderInt("##affixes", &r.minAffixes, 0, WaystoneHelperConfig::kTargetAffixMax,
                                    r.minAffixes == 0 ? anyFormat.c_str() : "%d", ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip(I18n::Tr("rule_affixes_tip"));
        col.Row(corruptionLabel);
        const char* corruptionItems[] = {I18n::Tr("rule_any"), I18n::Tr("corruption_only"), I18n::Tr("corruption_never")};
        changed |= Ui::Combo("##corruption", r.corrupted, corruptionItems, 3);
        Ui::Tip(I18n::Tr("rule_corruption_tip"));
        col.Row(rarityLabel);
        const char* rarityItems[] = {I18n::Tr("rule_any"), I18n::Tr("rarity_normal"), I18n::Tr("rarity_magic"),
                                     I18n::Tr("rarity_rare"), I18n::Tr("rarity_unique")};
        int rarityIndex = std::clamp(r.rarity + 1, 0, 4);
        if (Ui::Combo("##rarity", rarityIndex, rarityItems, 5)) {
            r.rarity = rarityIndex - 1;
            changed = true;
        }
        Ui::Tip(I18n::Tr("rule_rarity_tip"));
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    Ui::EndCard();
    ImGui::PopID();

    auto& rules = m_settings.borderRules;
    if (action == 1 && index > 0) { std::swap(rules[static_cast<size_t>(index)], rules[static_cast<size_t>(index) - 1]); structural = true; }
    if (action == 2 && index + 1 < count) { std::swap(rules[static_cast<size_t>(index)], rules[static_cast<size_t>(index) + 1]); structural = true; }
    if (action == 3) { rules.erase(rules.begin() + index); structural = true; }
    if (changed || structural) SettingsChanged(false);
    return structural;
}

// ---------------------------------------------------------------------------
// Affix groups
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSectionAffixGroups() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##groupsCard");
    Ui::Heading(ICON_FA_LAYER_GROUP, I18n::Tr("groups_title"), I18n::Tr("groups_help"));
    changed |= Ui::Check(I18n::Tr("groups_match"), "matchGroups", &s.enableAffixGroups, I18n::Tr("groups_match_tip"));
    ImGui::BeginDisabled(!s.enableAffixGroups);
    changed |= Ui::Check(I18n::Tr("groups_badges"), "groupBadges", &s.showAffixGroupBadges,
                         I18n::Tr("groups_badges_tip"));
    ImGui::Spacing();
    ImGui::BeginDisabled(s.affixGroups.size() >= WaystoneHelperConfig::kMaxGroups);
    if (ImGui::Button(IconLabel(ICON_FA_PLUS, I18n::Tr("groups_add"), "addGroup").c_str())) {
        WaystoneHelperConfig::AffixGroup g;
        g.id = s.MakeId("g");
        g.name = I18n::TrF("group_name_numbered", { { "number", Num(s.affixGroups.size() + 1) } });
        s.affixGroups.push_back(std::move(g));
        changed = true;
    }
    Ui::Tip(I18n::Tr("group_add_tip"));
    ImGui::EndDisabled();
    if (s.affixGroups.empty()) Ui::Hint(I18n::Tr("groups_empty_hint"));
    if (!m_data.Loaded()) Ui::Hint(I18n::Tr("groups_no_table"));
    ImGui::EndDisabled();   // a disabled scope never crosses a card (child window) edge
    Ui::EndCard();

    ImGui::BeginDisabled(!s.enableAffixGroups);
    for (int i = 0; i < static_cast<int>(s.affixGroups.size()); ++i) {
        ImGui::Spacing();
        if (DrawAffixGroup(i, static_cast<int>(s.affixGroups.size()))) break;
    }
    ImGui::EndDisabled();
    if (changed) SettingsChanged(false);
}

bool WaystonePlugin::DrawAffixGroup(int index, int count) {
    auto& g = m_settings.affixGroups[static_cast<size_t>(index)];
    bool changed = false;
    bool structural = false;
    ImGui::PushID(g.id.c_str());
    Ui::BeginCard("##group");

    changed |= ImGui::Checkbox("##enabled", &g.enabled);
    Ui::Tip(I18n::Tr("group_enabled_tip"));
    ImGui::SameLine();
    changed |= Ui::Swatch("##color", g.color);
    Ui::Tip(I18n::Tr("group_colour_tip"));
    ImGui::SameLine();
    changed |= GroupNameField(g.name);
    const int action = GroupRowButtons(index, count);

    ImGui::BeginDisabled(!g.enabled);
    {
        const char* matchLabel = I18n::Tr("group_match_when");
        const Ui::LabelColumn col({matchLabel});
        col.Row(matchLabel);
        const int selected = static_cast<int>(g.selectedAffixIds.size());
        const std::string fmt =
            NumberFormat(I18n::TrF("group_match_format", { { "value", kValue }, { "total", Num(selected) } }));
        changed |= ImGui::SliderInt("##minMatched", &g.minMatched, 1, WaystoneHelperConfig::kMinMatchedAffixMax,
                                    fmt.c_str(), ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip(I18n::Tr("group_match_tip"));
    }
    if (ImGui::TreeNodeEx(I18n::TrId("group_affixes"), ImGuiTreeNodeFlags_SpanAvailWidth |
                                                          (g.selectedAffixIds.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0))) {
        const size_t before = g.selectedAffixIds.size();
        DrawAffixPicker(g);
        changed |= before != g.selectedAffixIds.size();
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    Ui::EndCard();
    ImGui::PopID();

    auto& groups = m_settings.affixGroups;
    if (action == 1 && index > 0) { std::swap(groups[static_cast<size_t>(index)], groups[static_cast<size_t>(index) - 1]); structural = true; }
    if (action == 2 && index + 1 < count) { std::swap(groups[static_cast<size_t>(index)], groups[static_cast<size_t>(index) + 1]); structural = true; }
    if (action == 3) {
        m_affixSearch.erase(groups[static_cast<size_t>(index)].id);
        groups.erase(groups.begin() + index);
        structural = true;
    }
    if (changed || structural) SettingsChanged(false);
    return structural;
}

void WaystonePlugin::DrawAffixPicker(WaystoneHelperConfig::AffixGroup& g) {
    std::string& search = m_affixSearch[g.id];
    const char* clearLabel = I18n::TrId("picker_clear");
    const float clearW = Ui::ButtonWidth(clearLabel);
    const float fieldW = ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x;
    // The field gets its own line when the row is too narrow for a useful one.
    const bool stacked = fieldW < ImGui::GetFontSize() * 10.0f;
    ImGui::SetNextItemWidth(stacked ? ImGui::GetContentRegionAvail().x : fieldW);
    const std::string filterHint = std::string(ICON_FA_MAGNIFYING_GLASS "  ") + I18n::Tr("picker_filter_hint");
    Ui::InputString("##filter", filterHint.c_str(), search);
    Ui::Tip(I18n::Tr("picker_filter_tip"));
    if (!stacked) ImGui::SameLine();
    ImGui::BeginDisabled(g.selectedAffixIds.empty());
    if (Ui::SubtleButton(clearLabel)) g.selectedAffixIds.clear();
    Ui::Tip(I18n::Tr("picker_clear_tip"));
    ImGui::EndDisabled();
    const std::string filter = FoldCase(search);

    const float listH = std::floor(ImGui::GetTextLineHeightWithSpacing() * 12.0f);
    ImGui::BeginChild("##affixList", ImVec2(0.0f, listH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    const int columns = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 40.0f ? 2 : 1;
    std::string category = "\x01";
    bool open = false;
    int shown = 0;
    for (const auto& a : m_data.Affixes()) {
        const std::string label = AffixLabel(a);
        const char* categoryText = I18n::Tr(WaystoneHelper::CategoryKey(a.Category));
        if (!filter.empty() && FoldCase(label).find(filter) == std::string::npos &&
            FoldCase(a.Label).find(filter) == std::string::npos && FoldCase(categoryText).find(filter) == std::string::npos)
            continue;
        if (a.Category != category) {
            if (open) ImGui::EndTable();
            category = a.Category;
            ImGui::SeparatorText(categoryText);
            open = ImGui::BeginTable(("##cat" + category).c_str(), columns, ImGuiTableFlags_SizingStretchSame);
        }
        if (!open) continue;
        ImGui::TableNextColumn();
        const bool was = std::find(g.selectedAffixIds.begin(), g.selectedAffixIds.end(), a.Id) != g.selectedAffixIds.end();
        bool on = was;
        ImGui::PushID(a.Id.c_str());
        // A name too long for its column ends in "..." (only rows on screen are
        // measured); the tooltip has the whole affix.
        const std::string shortText = FirstLine(label);
        const float rowW = ImGui::GetContentRegionAvail().x;
        const bool visible = ImGui::IsRectVisible(ImVec2(rowW, ImGui::GetFrameHeight()));
        const std::string fitted =
            visible ? FitWithEllipsis(shortText, rowW - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x)
                    : shortText;
        if (ImGui::Checkbox((fitted + "###on").c_str(), &on) && on != was) {
            if (on) g.selectedAffixIds.push_back(a.Id);
            else g.selectedAffixIds.erase(std::remove(g.selectedAffixIds.begin(), g.selectedAffixIds.end(), a.Id),
                                          g.selectedAffixIds.end());
        }
        if (label != shortText || fitted != shortText) {
            std::string full = label;
            for (std::size_t p = full.find(" / "); p != std::string::npos; p = full.find(" / ", p + 1))
                full.replace(p, 3, "\n");
            Ui::Tip(full.c_str());
        }
        ImGui::PopID();
        ++shown;
    }
    if (open) ImGui::EndTable();
    if (shown == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(I18n::Tr("picker_no_match"));
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Diagnostics (collapsed)
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSectionDiagnostics() {
    if (!Ui::BeginDiagnostics(I18n::Tr("diag_tip"))) return;
    const Ui::Palette pal = Ui::Colors();
    WrappedDisabled(m_data.Loaded() ? I18n::TrF("diag_table", { { "affixes", Num(m_data.Affixes().size()) },
                                                                { "mods", Num(m_data.ModCount()) } })
                                    : std::string(I18n::Tr("diag_table_failed")));
    const std::string refresh =
        I18n::TrF("diag_last_refresh", { { "visible", Num(m_lastStats.visible) },
                                         { "read", Num(m_lastStats.parsed) },
                                         { "waiting", Num(m_lastStats.deferred) } }) +
        "  \xC2\xB7  " + I18n::TrF("diag_refresh_time", { { "ms", Num2(m_lastStats.ms) }, { "avg", Num2(m_scanMsAvg) } });
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(refresh.c_str());
    ImGui::PopTextWrapPos();
    switch (m_settingsNoteKind) {
    case WaystoneHelperConfig::LoadNote::UnreadableKept: Ui::Hint(I18n::Tr("diag_note_unreadable_kept")); break;
    case WaystoneHelperConfig::LoadNote::Unreadable:     Ui::Hint(I18n::Tr("diag_note_unreadable")); break;
    case WaystoneHelperConfig::LoadNote::Damaged:        Ui::Hint(I18n::Tr("diag_note_damaged")); break;
    case WaystoneHelperConfig::LoadNote::LegacyBorder:
        Ui::Hint(I18n::TrF("diag_note_legacy_border", { { "name", I18n::Tr("group_name_border") } }).c_str());
        break;
    case WaystoneHelperConfig::LoadNote::None: break;
    }

    ImGui::Spacing();
    bool readMods = m_settings.readMods;
    if (Ui::Check(I18n::Tr("diag_read_mods"), "readMods", &readMods, I18n::Tr("diag_read_mods_tip"))) {
        m_settings.readMods = readMods;
        SettingsChanged(true);
    }

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(I18n::Tr("diag_keys_title"));
    Ui::HelpMarker(I18n::Tr("diag_keys_help"));
    const auto& stats = m_data.GeneratedStats();
    if (!m_calibrationChanges.empty()) {
        std::string changes;
        for (const auto& c : m_calibrationChanges) {
            if (c.stat >= stats.size()) continue;
            if (!changes.empty()) changes += ", ";
            changes += I18n::TrF("diag_key_change", { { "stat", WaystoneHelper::StatNameText(UiWords(), stats[c.stat]) },
                                                      { "from", Num(c.from) },
                                                      { "to", Num(c.to) } });
        }
        Ui::Hint(I18n::TrF("diag_keys_updated", { { "changes", changes } }).c_str());
    }
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn(I18n::TrId("diag_col_stat"));
        ImGui::TableSetupColumn(I18n::TrId("diag_col_key"));
        ImGui::TableSetupColumn(I18n::TrId("diag_col_status"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < stats.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(WaystoneHelper::StatNameText(UiWords(), stats[i]).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", m_settings.statAggregateKeys.For(stats[i].statId));
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            switch (Calibration().StatusOf(i)) {
                case WaystoneHelper::StatKeyCalibrator::Status::Confirmed:
                    ImGui::PushStyleColor(ImGuiCol_Text, pal.Success);
                    ImGui::TextUnformatted(I18n::Tr("diag_key_confirmed"));
                    ImGui::PopStyleColor();
                    break;
                case WaystoneHelper::StatKeyCalibrator::Status::Stale: {
                    const int found = Calibration().Detected(i);
                    ImGui::PushStyleColor(ImGuiCol_Text, pal.Warning);
                    if (found) ImGui::TextUnformatted(I18n::TrF("diag_key_detected", { { "key", Num(found) } }).c_str());
                    else ImGui::TextUnformatted(I18n::Tr("diag_key_looking"));
                    ImGui::PopStyleColor();
                    break;
                }
                default: {
                    const bool unseen = Calibration().Observations(i) == 0;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextUnformatted(unseen ? I18n::Tr("diag_key_not_seen") : I18n::Tr("diag_key_checking"));
                    ImGui::PopStyleColor();
                    Ui::Tip(unseen ? I18n::Tr("diag_key_not_seen_tip") : I18n::Tr("diag_key_checking_tip"));
                    break;
                }
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    {
        WaystoneHelperConfig::StatAggregateKeys defaults;
        ImGui::BeginDisabled(defaults == m_settings.statAggregateKeys);
        if (Ui::SubtleButton(IconLabel(ICON_FA_ROTATE_LEFT, I18n::Tr("diag_default_keys"), "defaultKeys").c_str())) {
            m_settings.statAggregateKeys = defaults;
            m_calibrationChanges.clear();
            SettingsChanged(true);
        }
        Ui::Tip(I18n::Tr("diag_default_keys_tip"));
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (Ui::SubtleButton(IconLabel(ICON_FA_FILE_LINES, I18n::Tr("diag_dump"), "dump").c_str())) WriteDump();
    Ui::Tip(I18n::Tr("diag_dump_tip"));
    if (m_dumpStatus == DumpStatus::Written)
        WrappedDisabled(I18n::TrF("diag_dump_written", { { "count", Num(m_dumpCount) } }));
    else if (m_dumpStatus == DumpStatus::Failed)
        WrappedDisabled(I18n::Tr("diag_dump_failed"));
    if (m_lastHoveredAddress) WrappedDisabled(I18n::TrF("diag_last_hovered", { { "name", m_lastHoveredName } }));
    Ui::EndDiagnostics();
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

bool WaystonePlugin::DrawMinInput(const char* id, int* v, int maxV, const char* zeroText) {
    const bool changed = ImGui::DragInt(id, v, 0.5f, 0, maxV, *v == 0 ? zeroText : "%d", ImGuiSliderFlags_AlwaysClamp);
    if (changed) *v = std::clamp(*v, 0, maxV);
    return changed;
}
