#pragma once
// In-game drawing: the badges and border on a waystone, and the hover breakdown
// panel. Sizes follow the overlay font size (times the badge scale), every
// rectangle is snapped to whole pixels, and nothing here allocates per badge
// beyond what ImGui's draw list does.

#include "../config/Settings.h"
#include "../game/ItemRects.h"
#include "../game/WaystoneScore.h"

#include <imgui.h>

namespace WaystoneHelper::Overlay {

// Badges and border, clipped to the item so nothing spills onto its neighbours.
void DrawWaystone(ImDrawList* dl, const ScreenRect& item, const Score& e,
                  const WaystoneHelperConfig::Settings& s);

// The breakdown panel beside the hovered item, kept on screen.
void DrawBreakdown(ImDrawList* dl, const ScreenRect& item, const Score& e);

}  // namespace WaystoneHelper::Overlay
