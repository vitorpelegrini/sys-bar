#pragma once

#include "gfx.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

struct CardRow {
    MetricId id = MetricId::Cpu;
    std::wstring title;     // "CPU"
    std::wstring value;     // "23%" / "18,2 GB"
    std::wstring subtitle;  // hardware model, "of 31,8 GB · 57% in use"
    std::wstring peak;      // "max. 87%"
    float load = 0.0f;
    std::vector<float> history;  // oldest to newest, 0..1
};

struct CardModel {
    std::wstring title;
    std::wstring window;  // "last 60 s"
    std::wstring hintLeft, hintRight;
    int capacity = 60;  // samples that fit in the graph
    std::vector<CardRow> rows;
};

// Details panel shown on hover: values, hardware model and a history graph per metric.
class Card {
public:
    explicit Card(const Gfx& gfx) : gfx_(gfx) {}

    void SetTheme(const Theme& theme);

    // Bitmap size in pixels, including the shadow margin. The width grows with long translations.
    SIZE Measure(UINT dpi, const CardModel& model);
    int ShadowMarginPx() const;
    float WidthDip() const { return width_; }

    bool Paint(const CardModel& model);
    bool Present(HWND hwnd, POINT screen, BYTE alpha) const { return surface_.Present(hwnd, &screen, alpha); }
    const Surface& surface() const { return surface_; }

private:
    bool EnsureTarget();
    ComPtr<IDWriteTextLayout> Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight);
    void DrawText(IDWriteTextLayout* layout, float x, float baseline, const D2D1_COLOR_F& color);
    void DrawTextRight(IDWriteTextLayout* layout, float right, float baseline, const D2D1_COLOR_F& color);
    void DrawSparkline(const CardRow& row, int capacity, const D2D1_RECT_F& area);
    float Snap(float dip) const;

    const Gfx& gfx_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> roundStroke_;
    std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> formats_;  // (size x 10, weight) -> format
    Surface surface_;
    Theme theme_{};
    float scale_ = 1.0f;
    float width_ = 0.0f;   // DIPs, without the margin
    float height_ = 0.0f;  // DIPs, without the margin
    SIZE size_{0, 0};
};
