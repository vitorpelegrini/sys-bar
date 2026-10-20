#pragma once

#include "gfx.h"

#include <cstdint>
#include <string>
#include <vector>

struct ItemView {
    MetricId id = MetricId::Cpu;
    std::wstring label;
    std::wstring strong;       // main part of the value ("23", "12,4")
    std::wstring weak;         // dimmed suffix ("%", " / 31,9 GB")
    std::wstring worstStrong;  // widest possible text -> fixed column widths, nothing ever shifts
    std::wstring worstWeak;
    std::wstring badge;       // small text on the label line, e.g. a temperature ("54°")
    std::wstring worstBadge;  // widest possible badge; empty = the column never shows one
    int badgeLevel = 0;       // 0 normal, 1 warm (amber), 2 hot (red)
    float fraction = 0.0f;    // bar fill (animated), 0..1
    float load = 0.0f;        // actual load, 0..1 (drives the color)
};

struct LayoutInput {
    UINT dpi = 96;
    bool vertical = false;
    int thicknessPx = 40;  // taskbar height (or width, when vertical) available to us
};

// Draws the strip of metrics that lives inside the taskbar.
class Renderer {
public:
    explicit Renderer(const Gfx& gfx) : gfx_(gfx) {}

    void SetTheme(const Theme& theme);

    // Computes the window size in pixels. Work is only redone when something relevant changed.
    SIZE Layout(const LayoutInput& input, const std::vector<ItemView>& items);

    // True when drawing `items` would produce different pixels than the last Paint().
    bool NeedsPaint(const std::vector<ItemView>& items, bool hover) const;
    // Draws into the internal bitmap; Present() hands it to the layered window.
    bool Paint(const std::vector<ItemView>& items, bool hover);
    bool Present(HWND hwnd) const { return surface_.Present(hwnd, nullptr, 255); }

    // Index of the first value that does not fit its column, or -1 (used by the tests).
    int FirstOverflow(const std::vector<ItemView>& items, float* excessDip = nullptr);
    bool ValuesFit(const std::vector<ItemView>& items) { return FirstOverflow(items) < 0; }
    const Surface& surface() const { return surface_; }
    bool compact() const { return compact_; }

private:
    struct Block {
        float x = 0, y = 0, w = 0, h = 0;  // DIPs
        float contentScale = 1.0f;         // < 1 when the value does not fit (narrow vertical taskbar)
        ComPtr<IDWriteTextLayout> label;
        float labelBaseline = 0, labelWidth = 0;
        ComPtr<IDWriteTextLayout> value;
        float valueBaseline = 0, valueWidth = 0;
        std::wstring strong, weak;  // text currently held by `value`
        ComPtr<IDWriteTextLayout> badge;
        float badgeBaseline = 0, badgeWidth = 0;
        std::wstring badgeText;  // text currently held by `badge`
    };

    bool EnsureTarget();
    void ResetTarget();
    ComPtr<IDWriteTextLayout> MakeLabel(const std::wstring& text, float& baseline, float& width);
    ComPtr<IDWriteTextLayout> MakeBadge(const std::wstring& text, float& baseline, float& width);
    ComPtr<IDWriteTextLayout> MakeValue(const std::wstring& strong, const std::wstring& weak, float& baseline,
                                        float& width);
    uint64_t Signature(const std::vector<ItemView>& items, bool hover) const;
    float Snap(float dip) const;

    const Gfx& gfx_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1SolidColorBrush> weakBrush_;  // drawing effect of the dimmed text
    ComPtr<IDWriteTextFormat> labelFormat_;
    ComPtr<IDWriteTextFormat> valueFormat_;
    Surface surface_;

    Theme theme_{};
    uint64_t themeStamp_ = 0;
    std::wstring layoutKey_;
    bool compact_ = false;
    float scale_ = 1.0f;
    SIZE size_{0, 0};
    std::vector<Block> blocks_;
    uint64_t lastSignature_ = 0;
};
