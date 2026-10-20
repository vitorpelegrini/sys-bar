#include "card.h"

#include <algorithm>
#include <cmath>

namespace {

// Measurements in DIPs.
constexpr float kMinWidth = 316.0f;
constexpr float kMaxWidth = 540.0f;  // beyond this, long texts wrap (the card grows taller)
constexpr float kShadow = 18.0f;     // transparent margin around the card for the shadow
constexpr float kPad = 16.0f;
constexpr float kRadius = 8.0f;
constexpr float kCap = 0.70f;

constexpr float kHeaderSize = 12.5f;
constexpr float kTitleSize = 12.5f;
constexpr float kValueSize = 18.0f;
constexpr float kSmallSize = 11.0f;

constexpr float kHeaderToRow = 18.0f;
constexpr float kValueToSub = 17.0f;
constexpr float kSubToSpark = 9.0f;
constexpr float kSparkHeight = 30.0f;
constexpr float kSparkRadius = 4.0f;
constexpr float kRowGap = 17.0f;
constexpr float kFooterGap = 14.0f;
constexpr float kSepToHint = 13.0f;
constexpr float kIndent = 15.0f;     // text indent next to the colored marker
constexpr float kMinGap = 20.0f;     // minimum space between left and right aligned texts
constexpr float kSubToPeak = 10.0f;  // subtitle -> peak, on the same line
constexpr int kShadowSteps = 14;
constexpr float kSectionGap = 18.0f;       // last graph -> "Sensors" title
constexpr float kSectionTitleGap = 10.0f;  // title baseline -> first tile
constexpr float kTileGap = 8.0f;           // between tiles, both directions
constexpr float kTilePad = 10.0f;          // inside a tile
constexpr float kTileRadius = 6.0f;
constexpr float kTileTitleSize = 11.0f;
constexpr float kTileValueSize = 13.0f;
constexpr float kTileDetailSize = 10.5f;
constexpr float kTileToGauge = 8.0f;  // value baseline -> gauge
constexpr float kGaugeHeight = 3.0f;
constexpr float kGaugeToDetail = 7.0f;  // gauge -> detail cap height
constexpr float kHintLine = 18.0f;      // the faint line under the tiles
constexpr int kTileColumns = 2;
constexpr float kNoticeGap = 12.0f;

float RowHeight() {
    return kValueSize * kCap + kValueToSub + kSubToSpark + kSparkHeight;
}

float TileHeight() {
    return kTilePad + kTileValueSize * kCap + kTileToGauge + kGaugeHeight + kGaugeToDetail + kTileDetailSize * kCap +
           kTilePad;
}

size_t TileRows(size_t tiles) {
    return (tiles + kTileColumns - 1) / kTileColumns;
}

bool HasSensors(const CardModel& m) {
    return !m.sensorsTitle.empty() && (!m.sensors.empty() || !m.sensorsHint.empty());
}

// Height of the optional sections between the graphs and the footer (sensors, notice).
float ExtraHeight(const CardModel& m) {
    float h = 0.0f;
    if (HasSensors(m)) {
        const size_t rows = TileRows(m.sensors.size());
        h += kSectionGap + kSmallSize * kCap + kSectionTitleGap;
        if (rows) h += static_cast<float>(rows) * TileHeight() + static_cast<float>(rows - 1) * kTileGap;
        if (!m.sensorsHint.empty()) h += (rows ? 6.0f : 0.0f) + kHintLine;
    }
    if (!m.notice.empty()) h += kNoticeGap + kSmallSize * kCap;
    return h;
}

float TotalHeight(size_t rows) {
    const float header = kHeaderSize * kCap + kHeaderToRow;
    const float body = rows ? static_cast<float>(rows) * RowHeight() + static_cast<float>(rows - 1) * kRowGap : 0.0f;
    const float footer = kFooterGap + kSepToHint + kSmallSize * kCap + 4.0f;
    return kPad + header + body + footer + kPad - 2.0f;
}

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

}  // namespace

void Card::SetTheme(const Theme& theme) {
    if (theme.locale != theme_.locale) formats_.clear();
    theme_ = theme;
}

float Card::Snap(float dip) const {
    return std::round(dip * scale_) / scale_;
}

int Card::ShadowMarginPx() const {
    return static_cast<int>(std::lround(kShadow * scale_));
}

ComPtr<IDWriteTextLayout> Card::Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight) {
    const auto key = std::make_pair(static_cast<int>(size * 10.0f), static_cast<int>(weight));
    ComPtr<IDWriteTextFormat>& format = formats_[key];
    if (!format) {
        if (FAILED(gfx_.dwrite()->CreateTextFormat(gfx_.family().c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL, size, theme_.locale.c_str(), &format))) {
            return nullptr;
        }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx_.dwrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), 4000.0f,
                                               100.0f, &layout))) {
        return nullptr;
    }
    gfx_.UseTabularFigures(layout.Get(), static_cast<UINT32>(text.size()));
    return layout;
}

SIZE Card::Measure(UINT dpi, const CardModel& model) {
    scale_ = static_cast<float>(std::max<UINT>(dpi, 96)) / 96.0f;

    // Widest line decides the width (translations vary a lot in length).
    auto width = [&](const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight) {
        if (s.empty()) return 0.0f;
        auto layout = Text(s, size, weight);
        return layout ? TextWidth(layout.Get()) : 0.0f;
    };
    float needed = width(model.title, kHeaderSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) + kMinGap +
                   width(model.window, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
    for (const CardRow& row : model.rows) {
        needed = std::max(needed, kIndent + width(row.title, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) + kMinGap +
                                      width(row.value, kValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        needed = std::max(needed, kIndent + width(row.subtitle, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL) + kSubToPeak +
                                      width(row.peak, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL));
    }
    const float hintLeft = width(model.hintLeft, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
    const float hintRight = width(model.hintRight, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
    needed = std::max(needed, hintLeft + hintRight + (hintLeft > 0 && hintRight > 0 ? kMinGap : 0.0f));
    for (const CardSensorLine& tile : model.sensors) {
        // Everything at full length: the card widens for it, and wraps the detail beyond the maximum width.
        const float tileWidth =
            2.0f * kTilePad + std::max(11.0f + width(tile.title, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 12.0f +
                                           width(tile.value, kTileValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD),
                                       width(tile.detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL));
        needed = std::max(needed, kTileColumns * tileWidth + (kTileColumns - 1) * kTileGap);
    }
    needed = std::max(needed, width(model.sensorsHint, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL));
    needed = std::max(needed, width(model.notice, kSmallSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));

    width_ = std::clamp(std::ceil(needed + 2.0f * kPad), kMinWidth, kMaxWidth);
    height_ = TotalHeight(model.rows.size()) + ExtraHeight(model);

    // What still does not fit wraps onto more lines, and the card grows taller instead.
    rowExtra_.assign(model.rows.size(), 0.0f);
    for (size_t i = 0; i < model.rows.size(); ++i) {
        Wrapped(model.rows[i].subtitle, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL, SubtitleWidth(model.rows[i]),
                &rowExtra_[i]);
        height_ += rowExtra_[i];
    }
    tileExtra_.assign(TileRows(model.sensors.size()), 0.0f);
    for (size_t i = 0; i < model.sensors.size(); ++i) {
        float extra = 0.0f;
        Wrapped(model.sensors[i].detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL, TileDetailWidth(), &extra);
        float& row = tileExtra_[i / kTileColumns];
        row = std::max(row, extra);
    }
    for (float extra : tileExtra_) height_ += extra;
    size_ = {static_cast<LONG>(std::ceil((width_ + 2 * kShadow) * scale_)),
             static_cast<LONG>(std::ceil((height_ + 2 * kShadow) * scale_))};
    return size_;
}

ComPtr<IDWriteTextLayout> Card::Wrapped(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, float maxWidth,
                                        float* extra) {
    if (extra) *extra = 0.0f;
    if (text.empty()) return nullptr;
    auto layout = Text(text, size, weight);
    if (!layout) return nullptr;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetMaxWidth(std::max(maxWidth, 20.0f));
    DWRITE_TEXT_METRICS metrics{};
    if (extra && SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.lineCount > 1) {
        *extra = metrics.height - metrics.height / static_cast<float>(metrics.lineCount);
    }
    return layout;
}

float Card::SubtitleWidth(const CardRow& row) {
    auto peak = Text(row.peak, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
    const float peakWidth = peak && !row.peak.empty() ? TextWidth(peak.Get()) + kSubToPeak : 0.0f;
    return width_ - 2.0f * kPad - kIndent - peakWidth;
}

float Card::TileWidth() const {
    return (width_ - 2.0f * kPad - (kTileColumns - 1) * kTileGap) / kTileColumns;
}

float Card::TileDetailWidth() const {
    return TileWidth() - 2.0f * kTilePad;
}

bool Card::EnsureTarget() {
    if (target_) return true;
    target_ = CreateDcTarget(gfx_.d2d());
    if (!target_) return false;
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_))) {
        target_.Reset();
        return false;
    }
    if (!roundStroke_) {
        gfx_.d2d()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                      nullptr, 0, &roundStroke_);
    }
    return true;
}

void Card::DrawText(IDWriteTextLayout* layout, float x, float baseline, const D2D1_COLOR_F& color) {
    if (!layout) return;
    brush_->SetColor(color);
    target_->DrawTextLayout(D2D1::Point2F(Snap(x), Snap(baseline) - TextBaseline(layout)), layout, brush_.Get());
}

void Card::DrawTextRight(IDWriteTextLayout* layout, float right, float baseline, const D2D1_COLOR_F& color) {
    if (!layout) return;
    DrawText(layout, right - TextWidth(layout), baseline, color);
}

void Card::DrawSparkline(const CardRow& row, int capacity, const D2D1_RECT_F& area) {
    const bool light = theme_.light;

    // Track + 50% guide line.
    brush_->SetColor(Ink(theme_, light ? 0.035f : 0.045f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(area, kSparkRadius, kSparkRadius), brush_.Get());
    const float mid = Snap((area.top + area.bottom) / 2.0f) + 0.5f / scale_;
    brush_->SetColor(Ink(theme_, 0.06f));
    target_->DrawLine(D2D1::Point2F(area.left + kSparkRadius, mid), D2D1::Point2F(area.right - kSparkRadius, mid),
                      brush_.Get(), 1.0f / scale_);

    const size_t count = std::min(row.history.size(), static_cast<size_t>(std::max(capacity, 0)));
    if (count < 2 || capacity < 2) return;

    const float inset = 1.5f;  // keeps the line off the edges
    const float left = area.left + inset, right = area.right - inset;
    const float top = area.top + inset, bottom = area.bottom - 0.5f;
    const float step = (right - left) / static_cast<float>(capacity - 1);
    const size_t first = row.history.size() - count;
    auto point = [&](size_t i) {
        const float v = std::clamp(row.history[first + i], 0.0f, 1.0f);
        const float x = right - static_cast<float>(count - 1 - i) * step;  // newest sample on the right edge
        return D2D1::Point2F(x, bottom - v * (bottom - top));
    };

    ComPtr<ID2D1PathGeometry> fill, line;
    ComPtr<ID2D1GeometrySink> sink;
    ID2D1Factory* factory = gfx_.d2d();
    if (FAILED(factory->CreatePathGeometry(&fill)) || FAILED(fill->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(point(0).x, area.bottom), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 0; i < count; ++i) sink->AddLine(point(i));
    sink->AddLine(D2D1::Point2F(point(count - 1).x, area.bottom));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink.Reset();

    if (FAILED(factory->CreatePathGeometry(&line)) || FAILED(line->Open(&sink))) return;
    sink->BeginFigure(point(0), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < count; ++i) sink->AddLine(point(i));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    // Colors by load band along the vertical axis (same thresholds as the strip): history above 80%
    // turns amber and above 92% red, showing *when* the load was high.
    const D2D1_COLOR_F base = MetricColor(row.id, 0.0f, theme_);
    const D2D1_COLOR_F warn = theme_.colorByLoad ? MetricColor(row.id, kWarnLoad, theme_) : base;
    const D2D1_COLOR_F crit = theme_.colorByLoad ? MetricColor(row.id, kCriticalLoad, theme_) : base;
    const float fillAlpha = light ? 0.26f : 0.34f;
    constexpr float kEdge = 0.0005f;  // hard transition between bands
    // Gradient position 0 = value 100% (top), 1 = value 0% (bottom).
    const float warnPos = 1.0f - kWarnLoad, critPos = 1.0f - kCriticalLoad;
    const D2D1_GRADIENT_STOP lineStops[] = {{0.0f, crit},    {critPos, crit},         {critPos + kEdge, warn},
                                            {warnPos, warn}, {warnPos + kEdge, base}, {1.0f, base}};
    const D2D1_GRADIENT_STOP fillStops[] = {{0.0f, WithAlpha(crit, fillAlpha)},
                                            {critPos, WithAlpha(crit, fillAlpha * 0.92f)},
                                            {critPos + kEdge, WithAlpha(warn, fillAlpha * 0.92f)},
                                            {warnPos, WithAlpha(warn, fillAlpha * 0.80f)},
                                            {warnPos + kEdge, WithAlpha(base, fillAlpha * 0.80f)},
                                            {1.0f, WithAlpha(base, 0.0f)}};
    const auto vertical = D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, top), D2D1::Point2F(0, bottom));

    ComPtr<ID2D1GradientStopCollection> fillCollection;
    ComPtr<ID2D1LinearGradientBrush> fillBrush;
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    ComPtr<ID2D1Layer> layer;
    if (SUCCEEDED(target_->CreateGradientStopCollection(fillStops, 6, &fillCollection)) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(vertical, fillCollection.Get(), &fillBrush)) &&
        SUCCEEDED(
            factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(area, kSparkRadius, kSparkRadius), &clip)) &&
        SUCCEEDED(target_->CreateLayer(&layer))) {
        // Clip the area to the rounded track.
        target_->PushLayer(D2D1::LayerParameters(area, clip.Get()), layer.Get());
        target_->FillGeometry(fill.Get(), fillBrush.Get());
        target_->PopLayer();
    }

    ComPtr<ID2D1GradientStopCollection> lineCollection;
    ComPtr<ID2D1LinearGradientBrush> lineBrush;
    if (SUCCEEDED(target_->CreateGradientStopCollection(lineStops, 6, &lineCollection)) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(vertical, lineCollection.Get(), &lineBrush))) {
        target_->DrawGeometry(line.Get(), lineBrush.Get(), 1.5f, roundStroke_.Get());
    }

    // Dot on the current value, in the color of the current load (same as the strip).
    const D2D1_POINT_2F last = point(count - 1);
    brush_->SetColor(theme_.highContrast ? FromColorRef(theme_.hcBackground) : (light ? Rgb(0xFBFBFB) : Rgb(0x232323)));
    target_->FillEllipse(D2D1::Ellipse(last, 3.6f, 3.6f), brush_.Get());
    brush_->SetColor(MetricColor(row.id, row.load, theme_));
    target_->FillEllipse(D2D1::Ellipse(last, 2.4f, 2.4f), brush_.Get());
}

void Card::DrawSensorTile(const CardSensorLine& tile, const D2D1_RECT_F& area) {
    const bool light = theme_.light;
    const D2D1_COLOR_F strong = Ink(theme_, light ? 0.90f : 0.96f);
    const D2D1_COLOR_F medium = Ink(theme_, light ? 0.62f : 0.66f);
    const D2D1_COLOR_F faint = Ink(theme_, 0.46f);
    const D2D1_COLOR_F accent = tile.fan ? Ink(theme_, 0.40f) : MetricColor(tile.id, 0.0f, theme_);
    const D2D1_COLOR_F hot = tile.level >= 2   ? MetricColor(tile.id, kCriticalLoad, theme_)
                             : tile.level == 1 ? MetricColor(tile.id, kWarnLoad, theme_)
                                               : accent;

    // Tile surface: a faint fill and a hairline, the same language as the graph tracks.
    brush_->SetColor(Ink(theme_, light ? 0.035f : 0.045f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(area, kTileRadius, kTileRadius), brush_.Get());
    if (theme_.highContrast) {
        brush_->SetColor(FromColorRef(theme_.hcText));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(area, kTileRadius, kTileRadius), brush_.Get(), 1.0f / scale_);
    }

    const float left = area.left + kTilePad;
    const float right = area.right - kTilePad;
    const float base = area.top + kTilePad + kTileValueSize * kCap;

    // Marker dot, title and value (the value turns amber/red when the device runs warm/hot).
    brush_->SetColor(tile.level > 0 ? hot : accent);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(left + 3.0f, base - kTileTitleSize * kCap / 2.0f), 3.0f, 3.0f),
                         brush_.Get());
    auto value = Text(tile.value, kTileValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto title = Text(tile.title, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);  // Measure() made room for it
    DrawText(title.Get(), left + 11.0f, base, medium);
    DrawTextRight(value.Get(), right, base, tile.level > 0 && !theme_.highContrast ? hot : strong);

    // Temperature gauge.
    const float gaugeTop = Snap(base + kTileToGauge);
    const float gh = std::max(kGaugeHeight, 2.0f / scale_);
    if (tile.gauge >= 0.0f) {
        const float r = gh / 2.0f;
        brush_->SetColor(Ink(theme_, light ? 0.09f : 0.12f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, gaugeTop, right, gaugeTop + gh), r, r),
                                      brush_.Get());
        const float fill = std::max((right - left) * std::clamp(tile.gauge, 0.0f, 1.0f), gh);
        brush_->SetColor(tile.level > 0 ? hot : accent);
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, gaugeTop, left + fill, gaugeTop + gh), r, r),
                                      brush_.Get());
    }

    // Detail: the fans of this device, or its model.
    if (!tile.detail.empty()) {
        auto detail = Wrapped(tile.detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL, TileDetailWidth());
        DrawText(detail.Get(), left, gaugeTop + gh + kGaugeToDetail + kTileDetailSize * kCap, faint);
    }
}

bool Card::Paint(const CardModel& model) {
    if (size_.cx <= 0 || !EnsureTarget() || !surface_.Ensure(size_.cx, size_.cy)) return false;
    const RECT bounds{0, 0, size_.cx, size_.cy};
    if (FAILED(target_->BindDC(surface_.dc(), &bounds))) return false;
    target_->SetDpi(96.0f * scale_, 96.0f * scale_);

    const bool light = theme_.light;
    const D2D1_COLOR_F strong = Ink(theme_, light ? 0.90f : 0.96f);
    const D2D1_COLOR_F medium = Ink(theme_, light ? 0.62f : 0.66f);
    const D2D1_COLOR_F faint = Ink(theme_, 0.46f);

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const D2D1_RECT_F card = D2D1::RectF(kShadow, kShadow, kShadow + width_, kShadow + height_);

    // Soft shadow: concentric layers with decreasing opacity, slightly offset downwards.
    if (!theme_.highContrast) {
        for (int i = kShadowSteps; i >= 1; --i) {
            const float t = static_cast<float>(i) / kShadowSteps;
            const float grow = t * (kShadow - 4.0f);
            const float alpha = (light ? 0.030f : 0.055f) * (1.0f - t) * (1.0f - t) + 0.004f;
            brush_->SetColor(D2D1::ColorF(0, 0, 0, alpha));
            const D2D1_RECT_F r =
                D2D1::RectF(card.left - grow, card.top - grow + 3.0f, card.right + grow, card.bottom + grow + 3.0f);
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, kRadius + grow, kRadius + grow), brush_.Get());
        }
    }

    // Body + hairline border (half a pixel inwards, crisp at any DPI).
    brush_->SetColor(theme_.highContrast ? FromColorRef(theme_.hcBackground) : (light ? Rgb(0xFBFBFB) : Rgb(0x232323)));
    target_->FillRoundedRectangle(D2D1::RoundedRect(card, kRadius, kRadius), brush_.Get());
    const float hair = (theme_.highContrast ? 2.0f : 1.0f) / scale_;
    brush_->SetColor(theme_.highContrast ? FromColorRef(theme_.hcText) : Ink(theme_, 0.10f));
    const D2D1_RECT_F edge =
        D2D1::RectF(card.left + hair / 2, card.top + hair / 2, card.right - hair / 2, card.bottom - hair / 2);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(edge, kRadius, kRadius), brush_.Get(), hair);

    const float left = card.left + kPad;
    const float right = card.right - kPad;

    // Header.
    float y = card.top + kPad;
    const float headerBase = y + kHeaderSize * kCap;
    DrawText(Text(model.title, kHeaderSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left, headerBase, strong);
    DrawTextRight(Text(model.window, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), right, headerBase, faint);
    y = headerBase + kHeaderToRow;

    for (size_t rowIndex = 0; rowIndex < model.rows.size(); ++rowIndex) {
        const CardRow& row = model.rows[rowIndex];
        const float extra = rowIndex < rowExtra_.size() ? rowExtra_[rowIndex] : 0.0f;
        // Marker and value use the color of the current load, exactly like the strip.
        const D2D1_COLOR_F accent = MetricColor(row.id, row.load, theme_);
        const bool hot = theme_.colorByLoad && row.load >= kWarnLoad;
        const D2D1_COLOR_F valueColor = hot && !theme_.highContrast ? accent : strong;
        const float valueBase = y + kValueSize * kCap;
        const float subBase = valueBase + kValueToSub;
        const float sparkTop = subBase + extra + kSubToSpark;

        brush_->SetColor(accent);
        const float dotY = valueBase - kTitleSize * kCap / 2.0f;
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(left + 4.0f, dotY), 3.5f, 3.5f), brush_.Get());
        DrawText(Text(row.title, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left + kIndent, valueBase, strong);
        DrawTextRight(Text(row.value, kValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), right, valueBase, valueColor);

        // Subtitle (wrapped, never cut, when it would touch the peak) + peak on the right of its first line.
        auto peak = Text(row.peak, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
        auto sub = Wrapped(row.subtitle, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL, SubtitleWidth(row));
        DrawText(sub.Get(), left + kIndent, subBase, medium);
        DrawTextRight(peak.Get(), right, subBase, faint);

        DrawSparkline(row, model.capacity, D2D1::RectF(left, Snap(sparkTop), right, Snap(sparkTop + kSparkHeight)));
        y = sparkTop + kSparkHeight + kRowGap;
    }
    if (!model.rows.empty()) y -= kRowGap;

    // Sensors: a grid of tiles, one per device, with its fans (see CardSensorLine).
    if (HasSensors(model)) {
        y += kSectionGap;
        const float titleBase = y + kSmallSize * kCap;
        DrawText(Text(model.sensorsTitle, kSmallSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left, titleBase, medium);
        y = titleBase + kSectionTitleGap;
        const float tileW = TileWidth();
        const size_t rows = TileRows(model.sensors.size());
        for (size_t r = 0; r < rows; ++r) {
            const float tileH = TileHeight() + (r < tileExtra_.size() ? tileExtra_[r] : 0.0f);
            for (size_t c = 0; c < static_cast<size_t>(kTileColumns); ++c) {
                const size_t i = r * kTileColumns + c;
                if (i >= model.sensors.size()) break;
                const float x = left + static_cast<float>(c) * (tileW + kTileGap);
                DrawSensorTile(model.sensors[i], D2D1::RectF(Snap(x), Snap(y), Snap(x + tileW), Snap(y + tileH)));
            }
            y += tileH + (r + 1 < rows ? kTileGap : 0.0f);
        }
        if (!model.sensorsHint.empty()) {
            if (rows) y += 6.0f;
            DrawText(Text(model.sensorsHint, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), left, y + kHintLine - 5.0f,
                     faint);
            y += kHintLine;
        }
    }

    // Notice (e.g. an available update), in the accent color.
    if (!model.notice.empty()) {
        y += kNoticeGap;
        const float base = y + kSmallSize * kCap;
        DrawText(Text(model.notice, kSmallSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left, base,
                 MetricColor(MetricId::Cpu, 0.0f, theme_));
        y = base;
    }

    // Footer with usage hints.
    const float sep = Snap(y + kFooterGap) + 0.5f / scale_;
    brush_->SetColor(Ink(theme_, 0.08f));
    target_->DrawLine(D2D1::Point2F(card.left + 1.0f, sep), D2D1::Point2F(card.right - 1.0f, sep), brush_.Get(),
                      1.0f / scale_);
    const float hintBase = sep + kSepToHint + kSmallSize * kCap;
    if (!model.hintLeft.empty()) {
        DrawText(Text(model.hintLeft, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), left, hintBase, faint);
        DrawTextRight(Text(model.hintRight, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), right, hintBase, faint);
    } else {
        // Only one hint left: center it.
        auto hint = Text(model.hintRight, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
        if (hint) DrawText(hint.Get(), (left + right - TextWidth(hint.Get())) / 2.0f, hintBase, faint);
    }

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        target_.Reset();
        brush_.Reset();
        return false;
    }
    return SUCCEEDED(hr);
}
