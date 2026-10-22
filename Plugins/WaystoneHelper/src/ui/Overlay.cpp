#include "Overlay.h"

#include "../../../../POEFixer/plugin_sdk/PluginUi.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace WaystoneHelper::Overlay {

namespace {

ImU32 ColF(const float c[4], float alphaMul = 1.0f) {
    const auto ch = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(ch(c[0]), ch(c[1]), ch(c[2]), ch(c[3] * alphaMul));
}

float Snap(float v) { return std::floor(v + 0.5f); }

struct BadgeMetrics {
    float fontSize;
    float padX;
    float height;
    float rounding;
    float gap;
};

BadgeMetrics Metrics(const WaystoneHelperConfig::Settings& s) {
    BadgeMetrics m{};
    m.fontSize = std::max(9.0f, Snap(ImGui::GetFontSize() * s.badgeScale));
    m.padX = std::max(2.0f, Snap(m.fontSize * 0.28f));
    m.height = Snap(m.fontSize + std::max(2.0f, Snap(m.fontSize * 0.12f)) * 2.0f);
    m.rounding = std::max(1.0f, Snap(m.fontSize * 0.2f));
    m.gap = std::max(1.0f, Snap(m.fontSize * 0.12f));
    return m;
}

float BadgeWidth(const BadgeMetrics& m, const char* text) {
    const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(m.fontSize, FLT_MAX, -1.0f, text);
    return std::max(m.height, Snap(t.x + m.padX * 2.0f));
}

void DrawBadge(ImDrawList* dl, const BadgeMetrics& m, ImVec2 pos, float width, const Badge& b,
               const float background[4]) {
    const ImVec2 br(pos.x + width, pos.y + m.height);
    dl->AddRectFilled(pos, br, ColF(background), m.rounding);
    // ImGui 1.93 strokes AddRect inside its rectangle: a 1 px frame on the badge's edge.
    dl->AddRect(pos, br, ColF(b.color, 0.9f), m.rounding, 1.0f);
    const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(m.fontSize, FLT_MAX, -1.0f, b.text.c_str());
    const ImVec2 tp(Snap(pos.x + (width - t.x) * 0.5f), Snap(pos.y + (m.height - t.y) * 0.5f));
    // A one-pixel shadow keeps light colours readable on a pale background.
    dl->AddText(ImGui::GetFont(), m.fontSize, ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 200), b.text.c_str());
    dl->AddText(ImGui::GetFont(), m.fontSize, tp, ColF(b.color), b.text.c_str());
}

struct Box {
    float x0, y0, x1, y1;
    bool Overlaps(const Box& o) const { return x0 < o.x1 && x1 > o.x0 && y0 < o.y1 && y1 > o.y0; }
};

}  // namespace

void DrawWaystone(ImDrawList* dl, const ScreenRect& item, const Score& e, const WaystoneHelperConfig::Settings& s) {
    if (!dl || !e.matched) return;
    const float x0 = Snap(item.x), y0 = Snap(item.y);
    const float x1 = Snap(item.x + item.w), y1 = Snap(item.y + item.h);
    if (x1 - x0 < 4.0f || y1 - y0 < 4.0f) return;

    dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);

    if (e.border) {
        const float t = static_cast<float>(std::clamp(s.borderThickness, WaystoneHelperConfig::kBorderThicknessMin,
                                                      WaystoneHelperConfig::kBorderThicknessMax));
        // Stroked inside the cell (ImGui 1.93 AddRect default): the cell's outer t pixels.
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), ColF(e.borderColor), 0.0f, t);
    }

    const BadgeMetrics m = Metrics(s);
    const float inset = (e.border ? static_cast<float>(s.borderThickness) : 0.0f) + m.gap;
    const float left = x0 + inset;
    const float right = x1 - inset;
    const float bottom = y1 - inset;

    Box placed[32];
    int placedCount = 0;
    auto fits = [&](const Box& b) {
        if (b.y1 > bottom + 0.5f || b.x0 < left - 0.5f) return false;
        for (int i = 0; i < placedCount; ++i)
            if (b.Overlaps(placed[i])) return false;
        return true;
    };
    // Places a badge in its column at the first free row; skipped when the item is full.
    auto place = [&](const Badge& b, bool rightSide, float& columnY) {
        if (placedCount >= 32) return;
        const float w = std::min(BadgeWidth(m, b.text.c_str()), right - left);
        for (float y = columnY; y + m.height <= bottom + 0.5f; y += m.height + m.gap) {
            const float x = rightSide ? right - w : left;
            const Box box{x, y, x + w, y + m.height};
            if (!fits(box)) continue;
            DrawBadge(dl, m, ImVec2(x, y), w, b, s.badgeBackgroundColor);
            placed[placedCount++] = box;
            columnY = y + m.height + m.gap;
            return;
        }
    };

    float leftY = y0 + inset;
    float rightY = y0 + inset;
    if (e.hasAffixCountBadge) place(e.affixCount, false, leftY);
    for (const auto& b : e.rightBadges) place(b, true, rightY);
    for (const auto& b : e.leftBadges) place(b, false, leftY);

    dl->PopClipRect();
}

void DrawBreakdown(ImDrawList* dl, const ScreenRect& item, const Score& e) {
    if (!dl || e.breakdown.empty()) return;
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float pad = Snap(fs * 0.6f);
    const float lineH = Snap(ImGui::GetTextLineHeight() + fs * 0.18f);
    const float indent = Snap(fs * 0.9f);
    const float maxW = Snap(fs * 34.0f);
    static constexpr float kAccent[4] = {0.98f, 0.80f, 0.32f, 1.0f};
    static constexpr float kHeader[4] = {0.62f, 0.66f, 0.71f, 1.0f};
    const char* title = "Waystone Helper";

    const size_t lines = std::min<size_t>(e.breakdown.size(), 24);
    float width = font->CalcTextSizeA(fs, FLT_MAX, -1.0f, title).x;
    for (size_t i = 0; i < lines; ++i) {
        const auto& l = e.breakdown[i];
        width = std::max(width, font->CalcTextSizeA(fs, FLT_MAX, -1.0f, l.text.c_str()).x + (l.indent ? indent : 0.0f));
    }
    width = Snap(std::min(width, maxW) + pad * 2.0f);
    const float height = Snap(lineH * static_cast<float>(lines + 1) + fs * 0.25f + pad * 2.0f);

    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float margin = Snap(fs * 0.4f);
    const float gap = Snap(fs * 0.6f);
    float x = item.x + item.w + gap;
    if (x + width > disp.x - margin) x = item.x - width - gap;
    x = Snap(std::clamp(x, margin, std::max(margin, disp.x - width - margin)));
    float y = Snap(std::clamp(item.y, margin, std::max(margin, disp.y - height - margin)));

    PluginUi::Panel(dl, ImVec2(x, y), ImVec2(x + width, y + height), 0.93f);

    const ImVec4 clip(x + pad, y, x + width - pad, y + height);
    float ty = y + pad;
    dl->AddText(font, fs, ImVec2(x + pad, ty), ColF(kAccent), title);
    ty += lineH;
    dl->AddLineH(x + pad, x + width - pad, Snap(ty) - 1.0f, IM_COL32(255, 255, 255, 30));
    ty += Snap(fs * 0.25f);
    for (size_t i = 0; i < lines; ++i) {
        const auto& l = e.breakdown[i];
        const float tx = x + pad + (l.indent ? indent : 0.0f);
        dl->AddText(font, fs, ImVec2(tx, ty), ColF(l.header ? kHeader : l.color), l.text.c_str(), nullptr, 0.0f, &clip);
        ty += lineH;
    }
}

}  // namespace WaystoneHelper::Overlay
