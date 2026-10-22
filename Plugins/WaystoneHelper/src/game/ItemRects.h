#pragma once
// Where an inventory item sits on screen: the host's per-item rectangle when it
// has one, else the inventory grid's cell maths.

#include "../../../../POEFixer/plugin_sdk/PluginSDK.h"

#include <cstring>
#include <optional>

namespace WaystoneHelper {

struct ScreenRect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;

    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

inline bool GridOnScreen(const PluginSDK::Inventory& inv, float displayW, float displayH) {
    if (!inv.Grid.Valid || inv.Grid.CellSize <= 0.f) return false;
    if (displayW <= 0.f || displayH <= 0.f) return false;
    const float x = inv.Grid.GridScreenX;
    const float y = inv.Grid.GridScreenY;
    const float margin = 4.f;
    return x >= -margin && y >= -margin && x < displayW && y < displayH;
}

inline bool ItemOnScreen(const PluginSDK::InventoryItem& item, float displayW, float displayH) {
    if (!item.ScreenValid) return false;
    if (item.ScreenW <= 0.f || item.ScreenH <= 0.f) return false;
    if (displayW <= 0.f || displayH <= 0.f) return false;
    const float cx = item.ScreenX + item.ScreenW * 0.5f;
    const float cy = item.ScreenY + item.ScreenH * 0.5f;
    return cx >= 0.f && cy >= 0.f && cx < displayW && cy < displayH;
}

inline bool IsMainInventoryName(const char* name) {
    return name && std::strncmp(name, "MainInventory", 13) == 0;
}

// A stash-like grid (at least six rows, and not wider than the screen). The
// player's 12x5 inventory is trusted by name instead.
inline bool GridLayoutPlausible(const PluginSDK::Inventory& inv, float displayW) {
    if (inv.TotalBoxesY < 6) return false;
    if (inv.Grid.CellSize > 0.f && static_cast<float>(inv.TotalBoxesX) * inv.Grid.CellSize > displayW)
        return false;
    return true;
}

inline std::optional<ScreenRect> ResolveItemRect(const PluginSDK::Inventory& inv,
                                                 const PluginSDK::InventoryItem& item,
                                                 float displayW, float displayH,
                                                 bool trustedGrid) {
    if (ItemOnScreen(item, displayW, displayH))
        return ScreenRect{item.ScreenX, item.ScreenY, item.ScreenW, item.ScreenH};
    if (item.Width <= 0 || item.Height <= 0 || item.SlotX < 0 || item.SlotY < 0) return std::nullopt;
    if (GridOnScreen(inv, displayW, displayH) && (trustedGrid || GridLayoutPlausible(inv, displayW))) {
        const float cell = inv.Grid.CellSize;
        const ScreenRect r{inv.Grid.GridScreenX + static_cast<float>(item.SlotX) * cell,
                           inv.Grid.GridScreenY + static_cast<float>(item.SlotY) * cell,
                           static_cast<float>(item.Width) * cell,
                           static_cast<float>(item.Height) * cell};
        if (r.x + r.w * 0.5f < displayW && r.y + r.h * 0.5f < displayH) return r;
    }
    return std::nullopt;
}

}  // namespace WaystoneHelper
