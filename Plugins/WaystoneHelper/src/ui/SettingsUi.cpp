// Waystone Helper settings tab, built on the plugin UI kit (PluginUi.h) like the
// Health Bars / Atlas tabs: a status card, sub-tabs of padded cards (one
// measured label column per card, controls capped at ~15 em, wrapped tooltips,
// dimmed dependents), and a collapsed Diagnostics card at the bottom.

#include "../WaystonePlugin.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "../../../../POEFixer/plugin_sdk/PluginUi.h"

#include <imgui.h>
#include <imgui_internal.h>  // ImTextInitClassifiers

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Ui = PluginUi;

namespace {

void TruncateUtf8(std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return;
    std::size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
}

const char* const kCorruptionItems[] = {"Any", "Corrupted only", "Not corrupted"};
const char* const kRarityItems[] = {"Any", "Normal", "Magic", "Rare", "Unique"};

// One-line summary of a border group's conditions.
std::string RuleSummary(const WaystoneHelperConfig::BorderRule& r, const WaystoneHelper::WaystoneData& data) {
    std::string s;
    auto add = [&](const std::string& part) {
        if (!s.empty()) s += "  \xC2\xB7  ";
        s += part;
    };
    for (const auto& g : data.GeneratedStats()) {
        const int min = r.StatMin(g.statId);
        if (min > 0) add(g.displayName + " \xE2\x89\xA5 " + std::to_string(min));
    }
    if (r.minTier > 0) add("Tier \xE2\x89\xA5 " + std::to_string(r.minTier));
    if (r.minAffixes > 0) add(std::to_string(r.minAffixes) + "+ affixes");
    if (r.corrupted == WaystoneHelperConfig::kCorruptedOnly) add("Corrupted");
    if (r.corrupted == WaystoneHelperConfig::kCorruptedNever) add("Not corrupted");
    if (r.rarity >= 0) add(WaystoneHelper::RarityName(r.rarity));
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

// Icon buttons of a group title row; returns which was pressed (0 none, 1 up, 2 down, 3 delete).
int GroupRowButtons(int index, int count) {
    ImGui::SameLine();
    Ui::AlignRight(GroupRowButtonsWidth());
    int action = 0;
    ImGui::BeginDisabled(index == 0);
    if (Ui::SubtleButton(ICON_FA_CHEVRON_UP "###up")) action = 1;
    Ui::Tip("Move up. The first matching group in the list wins.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= count);
    if (Ui::SubtleButton(ICON_FA_CHEVRON_DOWN "###down")) action = 2;
    Ui::Tip("Move down.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (Ui::SubtleButton(ICON_FA_TRASH_CAN "###del")) ImGui::OpenPopup("##confirmDelete");
    Ui::Tip("Delete this group (asks first).");
    if (Ui::ConfirmPopup("##confirmDelete", "Delete this group?", "Delete", "Keep")) action = 3;
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
    // ImGui 1.93 keeps its word-wrap character classes in a per-DLL static that
    // only the copy which builds the font atlas initialises (the host's). This
    // DLL's copy wraps our text, so set it up here (idempotent, one check).
    ImTextInitClassifiers();
    ImGui::PushID("waystoneHelper");

    ImGui::Spacing();
    DrawHeader();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(ICON_FA_EYE "  Display###display")) {
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
        const std::string borders = std::string(ICON_FA_SQUARE "  Border groups (") +
                                    std::to_string(m_settings.borderRules.size()) + ")###borders";
        if (ImGui::BeginTabItem(borders.c_str())) {
            ImGui::Spacing();
            DrawSectionBorders();
            ImGui::EndTabItem();
        }
        const std::string groups = std::string(ICON_FA_LAYER_GROUP "  Affix groups (") +
                                   std::to_string(m_settings.affixGroups.size()) + ")###groups";
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
    Ui::Heading(ICON_FA_MAP, "Waystones",
                "Waystones in your inventory, stash and guild stash get badges: the affix count in the "
                "top-left corner, map-stat totals in the top-right corner and matching affix groups on "
                "the left. Border groups colour the frame of waystones that meet your conditions.\n\n"
                "Hover a marked waystone for a breakdown of what it matched.");
    if (m_overlayHidden) {
        const std::string hint = std::string("Press ") + Ui::KeyName(m_settings.toggleOverlayVk) +
                                 " in game to show the badges again.";
        Ui::Hint(hint.c_str());
    } else {
        Ui::Hint("Waystones are marked in your inventory, stash and guild stash.");
    }
    ImGui::Spacing();

    const Ui::Palette pal = Ui::Colors();
    if (m_overlayHidden)
        Ui::StatusPill("Hidden by the hotkey", ImGui::ColorConvertFloat4ToU32(pal.Warning));
    else
        Ui::StatusPill("Overlay active", ImGui::ColorConvertFloat4ToU32(pal.Success));
    Ui::Tip("The toggle hotkey (Display tab) hides or shows every badge at once.");

    const char* resetLabel = ICON_FA_ROTATE_LEFT "  Reset display###resetDisplay";
    const float w = Ui::ButtonWidth(resetLabel);
    ImGui::SameLine();
    // On a narrow pane the button takes its own line instead of running off the edge.
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
    Ui::AlignRight(w);
    if (Ui::SubtleButton(resetLabel)) ImGui::OpenPopup("##resetDisplayConfirm");
    Ui::Tip("Restores badge, colour and overlay defaults.\nBorder groups, affix groups and the hotkey are kept.");
    if (Ui::ConfirmPopup("##resetDisplayConfirm",
                         "Reset the display settings? Your border groups, affix groups and hotkey stay.", "Reset",
                         "Cancel")) {
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
    Ui::Heading(ICON_FA_EYE, "Overlay");
    {
        const Ui::LabelColumn col({"Toggle hotkey", "Badge size", "Badge background", "Refresh every"});
        col.RowNoWidth("Toggle hotkey");
        if (Ui::HotkeyCapture("##hotkey", &s.toggleOverlayVk,
                              "Press this key in game to hide or show all badges at once,\n"
                              "e.g. to see the game's own highlight while using currency on maps.\n"
                              "Click, then press the key you want (Esc cancels)."))
            changed = true;

        col.Row("Badge size");
        int pct = static_cast<int>(std::lround(s.badgeScale * 100.0f));
        if (ImGui::SliderInt("##badgeScale", &pct, static_cast<int>(WaystoneHelperConfig::kBadgeScaleMin * 100),
                             static_cast<int>(WaystoneHelperConfig::kBadgeScaleMax * 100), "%d%%",
                             ImGuiSliderFlags_AlwaysClamp)) {
            s.badgeScale = static_cast<float>(pct) / 100.0f;
            changed = true;
        }
        Ui::Tip("Size of the badge text, relative to the overlay font.\nThe badges always stay inside their waystone.");

        col.RowNoWidth("Badge background");
        changed |= Ui::Swatch("##badgeBg", s.badgeBackgroundColor);
        Ui::Tip("Fill behind every badge.\nLower the alpha to see more of the item art.");

        col.Row("Refresh every");
        changed |= ImGui::SliderInt("##scanInterval", &s.scanIntervalMs, WaystoneHelperConfig::kScanIntervalMinMs,
                                    WaystoneHelperConfig::kScanIntervalMaxMs, "%d ms", ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip("How often the open windows are checked for waystones.\nLower = badges follow tab switches faster;\n"
                "higher = less work for the overlay.");
    }
    changed |= Ui::Check("Hide badges on the hovered waystone", "hideHovered", &s.hideWhenHovered,
                         "Keeps the waystone under the cursor clean so the game's own\ntooltip and highlight stay readable.");
    changed |= Ui::Check("Show the breakdown panel on hover", "breakdown", &s.showHoverBreakdown,
                         "A panel beside a marked waystone lists its affix count, map stats,\n"
                         "matching affix groups and the border group that coloured it.");
    Ui::EndCard();
    if (changed) SettingsChanged(false);
}

void WaystonePlugin::DrawSectionAffixCount() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##affixCount");
    Ui::Heading(ICON_FA_HASHTAG, "Affix count");
    changed |= Ui::Check("Show the affix-count badge", "affixBadge", &s.showAffixCountBadge,
                         "A badge in the top-left corner with the number of explicit affixes,\n"
                         "shown once the waystone has at least the count below.");
    ImGui::BeginDisabled(!s.showAffixCountBadge);
    {
        const Ui::LabelColumn col({"Show from", "Badge colour"});
        col.Row("Show from");
        changed |= ImGui::SliderInt("##target", &s.targetAffixCount, WaystoneHelperConfig::kTargetAffixMin,
                                    WaystoneHelperConfig::kTargetAffixMax, s.targetAffixCount == 1 ? "%d affix" : "%d affixes",
                                    ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip("The smallest affix count that gets a badge.\n0-2 also marks normal and magic waystones.");
        col.RowNoWidth("Badge colour");
        changed |= Ui::Swatch("##affixColor", s.affixCountBadgeColor);
        Ui::Tip("Text and frame colour of the affix-count badge.");
    }
    ImGui::EndDisabled();
    Ui::EndCard();
    if (changed) SettingsChanged(false);
}

void WaystonePlugin::DrawSectionMapStats() {
    auto& s = m_settings;
    bool changed = false;
    Ui::BeginCard("##mapStats");
    Ui::Heading(ICON_FA_LIST_CHECK, "Map stats",
                "Badges in the top-right corner with the waystone's total of a map stat,\n"
                "e.g. R65 = 65% more Rarity of Items. Borders based on these totals\n"
                "are set up under Border groups.");
    changed |= Ui::Check("Show map-stat badges", "statBadges", &s.highlightImportantAffixes,
                         "Draws a badge for each stat ticked below.");
    ImGui::BeginDisabled(!s.highlightImportantAffixes);
    // One column for the swatches: the widest "Name (code)" checkbox.
    float widest = 0.0f;
    std::vector<std::string> labels;
    for (const auto& g : m_data.GeneratedStats()) {
        labels.push_back(g.displayName + " (" + g.badgeLabel + ")");
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
        Ui::Tip("Show a badge with this stat's total.");
        ImGui::SameLine();
        ImGui::SetCursorPosX(swatchX);
        changed |= Ui::Swatch("##color", color);
        Ui::Tip("Badge colour of this stat.");
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
    Ui::Heading(ICON_FA_SQUARE, "Border groups",
                "Each group colours the border of waystones that meet ALL of its conditions:\n"
                "map-stat totals, tier, affix count, corruption and rarity.\n"
                "Conditions left at Any are ignored. The first matching group in the\n"
                "list wins, and a group without conditions never matches.");
    changed |= Ui::Check("Draw borders", "drawBorders", &s.borderEnabled, "Turns every border group on or off.");
    ImGui::BeginDisabled(!s.borderEnabled);
    {
        const Ui::LabelColumn col({"Border thickness"});
        changed |= Ui::SliderIntRow(col, "Border thickness", "##thickness", &s.borderThickness,
                                    WaystoneHelperConfig::kBorderThicknessMin, WaystoneHelperConfig::kBorderThicknessMax,
                                    "%d px", "Width of the border, drawn inside the item's cell.");
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(s.borderRules.size() >= WaystoneHelperConfig::kMaxGroups);
    if (ImGui::Button(ICON_FA_PLUS "  Add border group###addRule")) {
        WaystoneHelperConfig::BorderRule r;
        r.id = s.MakeId("b");
        r.name = "Group " + std::to_string(s.borderRules.size() + 1);
        s.borderRules.push_back(std::move(r));
        changed = true;
    }
    Ui::Tip("Adds a group at the end of the list.");
    ImGui::EndDisabled();
    if (s.borderRules.empty())
        Ui::Hint("No groups yet. Example: Item Rarity at least 60 with a red border, and "
                 "Waystone Drop Chance at least 120 with a green one.");
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
    Ui::Tip("Turns this group on or off.");
    ImGui::SameLine();
    changed |= Ui::Swatch("##color", r.color);
    Ui::Tip("Border colour of this group.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(GroupNameWidth());
    if (Ui::InputString("##name", "Group name", r.name)) {
        TruncateUtf8(r.name, WaystoneHelperConfig::kMaxNameLen);
        changed = true;
    }
    const int action = GroupRowButtons(index, count);

    const std::string summary = RuleSummary(r, m_data);
    if (summary.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Warning);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("No conditions yet: this group never matches.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        Ui::Hint(summary.c_str());
    }

    ImGui::BeginDisabled(!r.enabled);
    if (ImGui::TreeNodeEx("Conditions###cond", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<std::string> statLabels;
        std::vector<const char*> keys;
        for (const auto& g : m_data.GeneratedStats()) statLabels.push_back(g.displayName + " at least");
        for (const auto& l : statLabels) keys.push_back(l.c_str());
        keys.push_back("Tier at least");
        keys.push_back("Affixes at least");
        keys.push_back("Corruption");
        keys.push_back("Rarity");
        const Ui::LabelColumn col(keys);
        const auto& stats = m_data.GeneratedStats();
        for (std::size_t i = 0; i < stats.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            col.Row(statLabels[i].c_str());
            int v = r.StatMin(stats[i].statId);
            if (DrawMinInput("##stat", &v, WaystoneHelperConfig::kStatThresholdMax, "Any")) {
                if (v > 0) r.statMins[stats[i].statId] = v;
                else r.statMins.erase(stats[i].statId);
                changed = true;
            }
            Ui::Tip("The waystone's total of this map stat must be at least this much.\n"
                    "Drag, or double-click to type (Enter confirms). 0 = any.");
            ImGui::PopID();
        }
        col.Row("Tier at least");
        changed |= ImGui::SliderInt("##tier", &r.minTier, 0, WaystoneHelperConfig::kMinTierMax,
                                    r.minTier == 0 ? "Any" : "Tier %d", ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip("Lowest waystone tier that matches. Any = every tier.");
        col.Row("Affixes at least");
        changed |= ImGui::SliderInt("##affixes", &r.minAffixes, 0, WaystoneHelperConfig::kTargetAffixMax,
                                    r.minAffixes == 0 ? "Any" : "%d", ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip("Lowest number of explicit affixes that matches. Any = no limit.");
        col.Row("Corruption");
        changed |= Ui::Combo("##corruption", r.corrupted, kCorruptionItems, 3);
        Ui::Tip("Corrupted only, not corrupted only, or either.");
        col.Row("Rarity");
        int rarityIndex = std::clamp(r.rarity + 1, 0, 4);
        if (Ui::Combo("##rarity", rarityIndex, kRarityItems, 5)) {
            r.rarity = rarityIndex - 1;
            changed = true;
        }
        Ui::Tip("Only waystones of this rarity match. Any = every rarity.");
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
    Ui::Heading(ICON_FA_LAYER_GROUP, "Affix groups",
                "A group is a set of waystone affixes you care about. A waystone matches\n"
                "when it has at least the chosen number of them, and gets a badge on\n"
                "the left in the group's colour: matched/selected, e.g. 2/3.");
    changed |= Ui::Check("Match affix groups", "matchGroups", &s.enableAffixGroups,
                         "Checks every waystone against the groups below.");
    ImGui::BeginDisabled(!s.enableAffixGroups);
    changed |= Ui::Check("Show group badges", "groupBadges", &s.showAffixGroupBadges,
                         "Off: matches only appear in the hover breakdown.");
    ImGui::Spacing();
    ImGui::BeginDisabled(s.affixGroups.size() >= WaystoneHelperConfig::kMaxGroups);
    if (ImGui::Button(ICON_FA_PLUS "  Add affix group###addGroup")) {
        WaystoneHelperConfig::AffixGroup g;
        g.id = s.MakeId("g");
        g.name = "Group " + std::to_string(s.affixGroups.size() + 1);
        s.affixGroups.push_back(std::move(g));
        changed = true;
    }
    Ui::Tip("Adds a group at the end of the list.");
    ImGui::EndDisabled();
    if (s.affixGroups.empty()) Ui::Hint("No groups yet. Example: a group of the curse suffixes to spot maps you skip.");
    if (!m_data.Loaded()) Ui::Hint("The built-in affix table is not available, so groups cannot match.");
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
    Ui::Tip("Turns this group on or off.");
    ImGui::SameLine();
    changed |= Ui::Swatch("##color", g.color);
    Ui::Tip("Badge colour of this group.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(GroupNameWidth());
    if (Ui::InputString("##name", "Group name", g.name)) {
        TruncateUtf8(g.name, WaystoneHelperConfig::kMaxNameLen);
        changed = true;
    }
    const int action = GroupRowButtons(index, count);

    ImGui::BeginDisabled(!g.enabled);
    {
        const Ui::LabelColumn col({"Match when"});
        col.Row("Match when");
        const int selected = static_cast<int>(g.selectedAffixIds.size());
        char fmt[64];
        std::snprintf(fmt, sizeof(fmt), "at least %%d of %d selected", selected);
        changed |= ImGui::SliderInt("##minMatched", &g.minMatched, 1, WaystoneHelperConfig::kMinMatchedAffixMax, fmt,
                                    ImGuiSliderFlags_AlwaysClamp);
        Ui::Tip("How many of the selected affixes a waystone needs to match this group.");
    }
    if (ImGui::TreeNodeEx("Affixes###affixes", ImGuiTreeNodeFlags_SpanAvailWidth |
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
    const char* clearLabel = "Clear selection###clearSel";
    const float clearW = Ui::ButtonWidth(clearLabel);
    const float fieldW = ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x;
    // The field gets its own line when the row is too narrow for a useful one.
    const bool stacked = fieldW < ImGui::GetFontSize() * 10.0f;
    ImGui::SetNextItemWidth(stacked ? ImGui::GetContentRegionAvail().x : fieldW);
    Ui::InputString("##filter", ICON_FA_MAGNIFYING_GLASS "  Filter affixes", search);
    Ui::Tip("Type part of an affix or category name.");
    if (!stacked) ImGui::SameLine();
    ImGui::BeginDisabled(g.selectedAffixIds.empty());
    if (Ui::SubtleButton(clearLabel)) g.selectedAffixIds.clear();
    Ui::Tip("Unticks every affix of this group.");
    ImGui::EndDisabled();
    const std::string filter = WaystoneHelper::ToLowerCopy(search);

    const float listH = std::floor(ImGui::GetTextLineHeightWithSpacing() * 12.0f);
    ImGui::BeginChild("##affixList", ImVec2(0.0f, listH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    const int columns = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 40.0f ? 2 : 1;
    std::string category = "\x01";
    bool open = false;
    int shown = 0;
    for (const auto& a : m_data.Affixes()) {
        if (!filter.empty() && !WaystoneHelper::ContainsCI(a.Label, filter) && !WaystoneHelper::ContainsCI(a.Category, filter))
            continue;
        if (a.Category != category) {
            if (open) ImGui::EndTable();
            category = a.Category;
            ImGui::SeparatorText(category.empty() ? "Other" : category.c_str());
            open = ImGui::BeginTable(("##cat" + category).c_str(), columns, ImGuiTableFlags_SizingStretchSame);
        }
        if (!open) continue;
        ImGui::TableNextColumn();
        const bool was = std::find(g.selectedAffixIds.begin(), g.selectedAffixIds.end(), a.Id) != g.selectedAffixIds.end();
        bool on = was;
        ImGui::PushID(a.Id.c_str());
        // A name too long for its column ends in "..." (only rows on screen are
        // measured); the tooltip has the whole affix.
        const float rowW = ImGui::GetContentRegionAvail().x;
        const bool visible = ImGui::IsRectVisible(ImVec2(rowW, ImGui::GetFrameHeight()));
        const std::string fitted =
            visible ? FitWithEllipsis(a.Short, rowW - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x)
                    : a.Short;
        if (ImGui::Checkbox((fitted + "###on").c_str(), &on) && on != was) {
            if (on) g.selectedAffixIds.push_back(a.Id);
            else g.selectedAffixIds.erase(std::remove(g.selectedAffixIds.begin(), g.selectedAffixIds.end(), a.Id),
                                          g.selectedAffixIds.end());
        }
        if (a.Label != a.Short || fitted != a.Short) {
            std::string full = a.Label;
            for (std::size_t p = full.find(" / "); p != std::string::npos; p = full.find(" / ", p + 1))
                full.replace(p, 3, "\n");
            Ui::Tip(full.c_str());
        }
        ImGui::PopID();
        ++shown;
    }
    if (open) ImGui::EndTable();
    if (shown == 0) ImGui::TextDisabled("No affixes match the filter.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Diagnostics (collapsed)
// ---------------------------------------------------------------------------

void WaystonePlugin::DrawSectionDiagnostics() {
    if (!Ui::BeginDiagnostics("Refresh timings, reading item mods, the game's map-stat keys and a dump of the "
                              "waystones on screen."))
        return;
    const Ui::Palette pal = Ui::Colors();
    WrappedDisabled(m_dataStatus);
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Last refresh: %d waystones on screen, %d read, %d waiting  \xC2\xB7  %.2f ms (avg %.2f ms)",
                  m_lastStats.visible, m_lastStats.parsed, m_lastStats.deferred, m_lastStats.ms, m_scanMsAvg);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(buf);
    ImGui::PopTextWrapPos();
    if (!m_settingsNote.empty()) Ui::Hint(m_settingsNote.c_str());

    ImGui::Spacing();
    bool readMods = m_settings.readMods;
    if (Ui::Check("Read item mods", "readMods", &readMods,
                  "Reads each waystone's mods for the affix count, map stats and affix groups.\n"
                  "Off: only tier, rarity and corruption conditions work.")) {
        m_settings.readMods = readMods;
        SettingsChanged(true);
    }

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Map-stat keys");
    Ui::HelpMarker("The game's numbers for the map stats inside an item. A game patch can move\n"
                   "them; the plugin then finds the new numbers from the waystones you look at\n"
                   "and switches to them by itself (it needs a few waystones carrying each stat).");
    if (!m_calibrationNote.empty()) Ui::Hint(m_calibrationNote.c_str());
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Stat");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        const auto& stats = m_data.GeneratedStats();
        for (std::size_t i = 0; i < stats.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stats[i].displayName.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", m_settings.statAggregateKeys.For(stats[i].statId));
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            switch (Calibration().StatusOf(i)) {
                case WaystoneHelper::StatKeyCalibrator::Status::Confirmed:
                    ImGui::TextColored(pal.Success, "Confirmed");
                    break;
                case WaystoneHelper::StatKeyCalibrator::Status::Stale: {
                    const int found = Calibration().Detected(i);
                    if (found) ImGui::TextColored(pal.Warning, "Not seen; detected %d", found);
                    else ImGui::TextColored(pal.Warning, "Not seen; looking at more waystones");
                    break;
                }
                default:
                    ImGui::TextDisabled(Calibration().Observations(i) == 0 ? "Not seen yet" : "Checking");
                    Ui::Tip(Calibration().Observations(i) == 0
                                ? "No waystone carrying this stat has been looked at yet."
                                : "Waystones with this stat were seen; the key is being checked.");
                    break;
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    {
        WaystoneHelperConfig::StatAggregateKeys defaults;
        ImGui::BeginDisabled(defaults == m_settings.statAggregateKeys);
        if (Ui::SubtleButton(ICON_FA_ROTATE_LEFT "  Default keys###defaultKeys")) {
            m_settings.statAggregateKeys = defaults;
            m_calibrationNote.clear();
            SettingsChanged(true);
        }
        Ui::Tip("Restores the keys of the game version this plugin release was made for.");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (Ui::SubtleButton(ICON_FA_FILE_LINES "  Write dump of visible waystones###dump")) WriteDump();
    Ui::Tip("Writes every mod and stat of the waystones on screen (up to 12) to\n"
            "debug\\waystone-dump.txt in the plugin folder, for bug reports.\nOpen a stash with waystones first.");
    if (!m_dumpStatus.empty()) WrappedDisabled(m_dumpStatus);
    if (m_lastHoveredAddress) WrappedDisabled("Last hovered waystone: " + m_lastHoveredName);
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
