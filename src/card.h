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
    bool half = false;           // shares its line with the next half row (e.g. one per disk)
};

// One tile of the "Sensors" grid: a device with its temperature and the fans that cool it.
//   CPU                59 °C
//   ━━━━━━━━━━━━━──────────      <- gauge (omitted for fan-only tiles)
//   2591 RPM                     <- detail: fans, or the device model
struct CardSensorLine {
    std::wstring title;           // "CPU", "GPU", "NVMe C:", "Case fan"
    std::wstring value;           // "59 °C", "1205 RPM"
    std::wstring detail;          // "2591 RPM", "fan stopped", "GVR1TB"
    float gauge = -1.0f;          // 0..1 temperature gauge; < 0 = none
    int level = 0;                // 0 normal, 1 warm, 2 hot
    MetricId id = MetricId::Cpu;  // palette of the marker and the gauge
    bool fan = false;             // fan-only tile: neutral marker
};

struct CardModel {
    std::wstring title;
    std::wstring window;  // "last 60 s"
    std::wstring hintLeft, hintRight;
    int capacity = 60;  // samples that fit in the graph
    std::vector<CardRow> rows;
    std::wstring sensorsTitle;  // empty = no sensors section
    std::vector<CardSensorLine> sensors;
    std::wstring sensorsHint;  // faint line under the sensors (e.g. how to get more of them)
    std::wstring notice;       // highlighted line above the footer (e.g. an available update)
};

// Details panel shown on hover: values, hardware model and a history graph per metric.
class Card {
public:
    explicit Card(const Gfx& gfx) : gfx_(gfx) {}

    void SetTheme(const Theme& theme);

    // Bitmap size in pixels, including the shadow margin. The width grows with long translations and
    // long texts wrap; when the visible card would be taller than `maxHeightPx` (> 0, shadow not included),
    // the graphs get shorter to fit.
    SIZE Measure(UINT dpi, const CardModel& model, int maxHeightPx = 0);
    float SparkHeightDip() const { return sparkHeight_; }
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
    void DrawSensorTile(const CardSensorLine& tile, const D2D1_RECT_F& area);
    // Text that never gets an ellipsis: it wraps within `maxWidth`; `extra` = height beyond one line.
    ComPtr<IDWriteTextLayout> Wrapped(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, float maxWidth,
                                      float* extra = nullptr);
    float TileWidth() const;
    float TileDetailWidth() const;
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
    // Graph rows laid out by Measure(): a line holds one full row or two half rows.
    struct RowBox {
        size_t row = 0;
        size_t line = 0;
        float x = 0, w = 0;     // relative to the content's left edge
        float subWidth = 0;     // wrap width of the subtitle
        std::wstring subtitle;  // half rows carry the peak in it ("... · max. 37%")
    };
    struct RowLine {
        float y = 0;      // relative to the first line
        float extra = 0;  // wrapped subtitle lines beyond the first
        bool half = false;
    };
    std::vector<RowBox> boxes_;
    std::vector<RowLine> lines_;
    float sparkHeight_ = 0.0f;
    int tileColumns_ = 2;
    std::vector<float> tileExtra_;  // per tile row: detail lines beyond the first
    SIZE size_{0, 0};
};
