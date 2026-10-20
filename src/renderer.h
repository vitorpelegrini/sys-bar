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
    // Draws into the internal bitmap; Present() hands it to the layered window. When only bar fills changed
    // (the animation frames), only those bars are redrawn, unless `full`.
    bool Paint(const std::vector<ItemView>& items, bool hover, bool full = false);
    bool Present(HWND hwnd) const {
        if (!dirtyAll_ && IsRectEmpty(&dirty_)) return true;  // nothing was redrawn
        return surface_.Present(hwnd, nullptr, 255, dirtyAll_ ? nullptr : &dirty_);
    }
    // True when the last Paint() redrew only some bars (used by the tests).
    bool LastPaintWasPartial() const { return !dirtyAll_; }

    // Index of the first value that does not fit its column, or -1 (used by the tests).
    int FirstOverflow(const std::vector<ItemView>& items, float* excessDip = nullptr);
    bool ValuesFit(const std::vector<ItemView>& items) { return FirstOverflow(items) < 0; }
    const Surface& surface() const { return surface_; }
    bool compact() const { return compact_; }

private:
    struct BarGeometry {
        float top = 0, height = 0, width = 0;  // block-local DIPs
    };

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
        BarGeometry bar;
        uint64_t barStamp = 0;                // fill and color of the bar as last drawn (0: not drawn)
        RECT barPixels{};                     // device pixels the bar may touch
        std::vector<uint32_t> barBackground;  // those pixels before the bar was drawn
    };

    bool EnsureTarget();
    void ResetTarget();
    ComPtr<IDWriteTextLayout> MakeLabel(const std::wstring& text, float& baseline, float& width);
    ComPtr<IDWriteTextLayout> MakeBadge(const std::wstring& text, float& baseline, float& width);
    ComPtr<IDWriteTextLayout> MakeValue(const std::wstring& strong, const std::wstring& weak, float& baseline,
                                        float& width);
    uint64_t Signature(const std::vector<ItemView>& items, bool hover) const;
    uint64_t TextSignature(const std::vector<ItemView>& items, bool hover) const;
    uint64_t BarStamp(const ItemView& item, size_t index) const;
    BarGeometry Bar(const Block& b, float& labelBase, float& valueBase, float& valueX) const;
    void DrawBackground(bool hover);
    void DrawBar(const ItemView& item, const Block& b);
    bool PaintBars(const std::vector<ItemView>& items, bool hover);
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
    uint64_t lastText_ = 0;  // everything but the bars, as last drawn
    RECT dirty_{};           // part of the bitmap changed by the last Paint()
    bool dirtyAll_ = true;
};
