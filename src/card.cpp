#include "card.h"

#include "icons.h"

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

constexpr float kHeaderToRow = 12.0f;
constexpr float kValueToSub = 17.0f;
constexpr float kSubToSpark = 9.0f;
constexpr float kSparkHeight = 30.0f;
constexpr float kSparkRadius = 4.0f;
constexpr float kMinSparkHeight = 14.0f;  // graphs shrink down to this on short screens
constexpr float kRowPad = 10.0f;          // inside each metric's tile
constexpr float kHalfGap = 8.0f;          // between the two half rows of a line (same as between tiles)
constexpr float kHalfValueSize = 15.0f;
constexpr float kRowGap = 8.0f;  // between metric tiles
constexpr float kMinRowGap = 5.0f;
constexpr float kWideWidth = 720.0f;  // short screens only: a wider card instead of a taller one
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
constexpr float kTileToGauge = 8.0f;   // value baseline -> gauge
constexpr float kGaugeHeight = 14.0f;  // thermometer: bulb diameter plus the scale marks below the tube
constexpr float kBulbSize = 12.0f;
constexpr float kEmojiSize = 11.5f;     // the snowflake and the flame at the thermometer's ends
constexpr float kGaugeToDetail = 6.0f;  // thermometer -> detail cap height
constexpr float kHintLine = 18.0f;      // the faint line under the tiles
constexpr float kNoticeGap = 12.0f;

float TileHeight() {
    return kTilePad + kTileValueSize * kCap + kTileToGauge + kGaugeHeight + kGaugeToDetail + kTileDetailSize * kCap +
           kTilePad;
}

size_t TileRows(size_t tiles, int columns) {
    return (tiles + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
}

bool HasSensors(const CardModel& m) {
    return !m.sensorsTitle.empty() && (!m.sensors.empty() || !m.fans.empty() || !m.sensorsHint.empty());
}

constexpr float kFanLineHeight = 30.0f;  // a compact fan tile
constexpr float kFanIconSize = 13.0f;

float FanRowsHeight(const CardModel& m, int columns, bool afterTiles) {
    const size_t rows = TileRows(m.fans.size(), columns);
    if (!rows) return 0.0f;
    return (afterTiles ? kTileGap : 0.0f) + static_cast<float>(rows) * kFanLineHeight +
           static_cast<float>(rows - 1) * kTileGap;
}

// Height of the optional sections between the graphs and the footer (sensors, notice).
float ExtraHeight(const CardModel& m, int tileColumns) {
    float h = 0.0f;
    if (HasSensors(m)) {
        const size_t rows = TileRows(m.sensors.size(), tileColumns);
        h += kSectionGap + kSmallSize * kCap + kSectionTitleGap;
        if (rows) h += static_cast<float>(rows) * TileHeight() + static_cast<float>(rows - 1) * kTileGap;
        h += FanRowsHeight(m, tileColumns, rows > 0);
        if (!m.sensorsHint.empty()) h += (rows || !m.fans.empty() ? 6.0f : 0.0f) + kHintLine;
    }
    if (!m.notice.empty()) h += kNoticeGap + kSmallSize * kCap;
    return h;
}

// Header and footer (everything but the graph rows and the optional sections).
float FrameHeight() {
    const float header = kHeaderSize * kCap + kHeaderToRow;
    const float footer = kFooterGap + kSepToHint + kSmallSize * kCap + 4.0f;
    return kPad + header + footer + kPad - 2.0f;
}

// Text part of a graph line, above its sparkline.
float LineTextHeight(bool half, float extra) {
    return (half ? kHalfValueSize : kValueSize) * kCap + kValueToSub + extra + kSubToSpark;
}

// A whole metric tile: padding, texts and graph.
float LineHeight(bool half, float extra, float spark) {
    return kRowPad + LineTextHeight(half, extra) + spark + kRowPad;
}

// Live text measured at its widest: every number (with its separators) counts as "888,8", the widest a
// value gets. The card's width then depends on the labels, never on the readings of the moment.
std::wstring Stable(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size();) {
        if (s[i] >= L'0' && s[i] <= L'9') {
            while (i < s.size() && ((s[i] >= L'0' && s[i] <= L'9') || s[i] == L',' || s[i] == L'.')) ++i;
            out += L"888,8";
        } else {
            out += s[i++];
        }
    }
    return out;
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
    // Inline icons: Private Use Area characters drawn with Segoe MDL2 Assets, slightly smaller.
    for (UINT32 i = 0; i < text.size();) {
        if (!icon::IsIcon(text[i])) {
            ++i;
            continue;
        }
        UINT32 end = i + 1;
        while (end < text.size() && icon::IsIcon(text[end])) ++end;
        const DWRITE_TEXT_RANGE range{i, end - i};
        layout->SetFontFamilyName(L"Segoe MDL2 Assets", range);
        layout->SetFontSize(size * 0.92f, range);
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL, range);
        if (iconBrush_) layout->SetDrawingEffect(iconBrush_.Get(), range);
        i = end;
    }
    return layout;
}

SIZE Card::Measure(UINT dpi, const CardModel& model, int maxHeightPx, int maxWidthPx) {
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
        if (row.half) {  // two per line: each half needs its title and value side by side, and each detail line
            float half = kIndent + width(row.title, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) + kMinGap +
                         width(Stable(row.value), kHalfValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            const std::wstring details = Stable(row.peak.empty() ? row.subtitle : row.subtitle + L"\u2003" + row.peak);
            for (size_t start = 0; start <= details.size();) {
                size_t end = details.find(L'\n', start);
                if (end == std::wstring::npos) end = details.size();
                half = std::max(
                    half, kIndent + width(details.substr(start, end - start), kSmallSize, DWRITE_FONT_WEIGHT_NORMAL));
                start = end + 1;
            }
            needed = std::max(needed, 2.0f * (half + 2.0f * kRowPad) + kHalfGap);
            continue;
        }
        needed =
            std::max(needed, 2.0f * kRowPad + kIndent + width(row.title, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) +
                                 kMinGap + width(Stable(row.value), kValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        needed = std::max(needed, 2.0f * kRowPad + kIndent +
                                      width(Stable(row.subtitle), kSmallSize, DWRITE_FONT_WEIGHT_NORMAL) + kSubToPeak +
                                      width(Stable(row.peak), kSmallSize, DWRITE_FONT_WEIGHT_NORMAL));
    }
    const float footerLeft = width(model.footerLeft, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
    const float footerRight = width(model.footerRight, kSmallSize,
                                    model.footerAccent ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    needed = std::max(needed, footerLeft + footerRight + (footerLeft > 0 && footerRight > 0 ? kMinGap : 0.0f));
    float tileNeed = 0.0f;  // widest tile, everything at full length
    for (const CardSensorLine& tile : model.sensors) {
        tileNeed =
            std::max(tileNeed, 2.0f * kTilePad +
                                   std::max(11.0f + width(tile.title, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) +
                                                12.0f + width(tile.value, kTileValueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD),
                                            width(tile.detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL)));
    }
    for (const CardFanLine& fan : model.fans) {
        const std::wstring speed = fan.percent.empty() ? fan.speed : fan.speed + L"  " + fan.percent;
        tileNeed = std::max(tileNeed, 2.0f * kTilePad + kFanIconSize + 7.0f +
                                          width(fan.title, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 12.0f +
                                          width(Stable(speed), kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    }
    if (tileNeed > 0.0f) needed = std::max(needed, 2.0f * tileNeed + kTileGap);
    needed = std::max(needed, width(model.sensorsHint, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL));
    needed = std::max(needed, width(model.notice, kSmallSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));

    // Lays everything out at a given width, graph height and row gap; returns the card height (DIPs).
    auto layoutAt = [&](float cardWidth, float spark, float rowGap) {
        width_ = cardWidth;
        // A wide card fits three sensor tiles per row, when each still holds its widest content.
        tileColumns_ = 2;
        if (width_ >= kWideWidth - 1.0f && (width_ - 2.0f * kPad - 2.0f * kTileGap) / 3.0f >= tileNeed)
            tileColumns_ = 3;
        float height = FrameHeight() + ExtraHeight(model, tileColumns_);

        // Graph lines: a full row, or two half rows side by side. What still does not fit wraps onto
        // more lines, and the card grows taller instead.
        const float content = width_ - 2.0f * kPad;
        const float halfWidth = (content - kHalfGap) / 2.0f;
        boxes_.clear();
        lines_.clear();
        for (size_t i = 0; i < model.rows.size();) {
            const bool pair = model.rows[i].half && i + 1 < model.rows.size() && model.rows[i + 1].half;
            const bool half = model.rows[i].half;
            RowLine line;
            line.half = half;
            for (size_t k = 0; k < (pair ? 2u : 1u); ++k) {
                const CardRow& row = model.rows[i + k];
                RowBox box;
                box.row = i + k;
                box.line = lines_.size();
                // The box is the tile's inside: the tile adds kRowPad all around.
                box.x = (k == 0 ? 0.0f : halfWidth + kHalfGap) + kRowPad;
                box.w = (half ? halfWidth : content) - 2.0f * kRowPad;
                if (half) {
                    box.subtitle = row.peak.empty()       ? row.subtitle
                                   : row.subtitle.empty() ? row.peak
                                                          : row.subtitle + L"\u2003" + row.peak;
                    box.subWidth = box.w - kIndent;
                } else {
                    box.subtitle = row.subtitle;
                    const float peak =
                        row.peak.empty() ? 0.0f : width(Stable(row.peak), kSmallSize, DWRITE_FONT_WEIGHT_NORMAL);
                    box.subWidth = box.w - kIndent - (peak > 0.0f ? peak + kSubToPeak : 0.0f);
                }
                // Heights from the widest form of the text too: a reading never adds or removes a line.
                float extra = 0.0f;
                Wrapped(Stable(box.subtitle), kSmallSize, DWRITE_FONT_WEIGHT_NORMAL, box.subWidth, &extra);
                line.extra = std::max(line.extra, extra);
                boxes_.push_back(std::move(box));
            }
            lines_.push_back(line);
            i += pair ? 2 : 1;
        }
        float y = 0.0f;
        for (RowLine& line : lines_) {
            line.y = y;
            y += LineHeight(line.half, line.extra, spark) + rowGap;
        }
        height += lines_.empty() ? 0.0f : y - rowGap;
        sparkHeight_ = spark;

        tileExtra_.assign(TileRows(model.sensors.size(), tileColumns_), 0.0f);
        for (size_t i = 0; i < model.sensors.size(); ++i) {
            float extra = 0.0f;
            Wrapped(model.sensors[i].detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL, TileDetailWidth(), &extra);
            float& row = tileExtra_[i / static_cast<size_t>(tileColumns_)];
            row = std::max(row, extra);
        }
        for (float extra : tileExtra_) height += extra;
        return height;
    };

    // Natural size first. On a short screen: wider (less wrapping, three tiles per row), then shorter
    // graphs, then tighter rows. The text itself is never shrunk or cut.
    const float natural = std::max(std::clamp(std::ceil(needed + 2.0f * kPad), kMinWidth, kMaxWidth), minWidth_);
    const float maxHeight = maxHeightPx > 0 ? static_cast<float>(maxHeightPx) / scale_ : 0.0f;
    height_ = layoutAt(natural, kSparkHeight, kRowGap);
    if (maxHeight > 0.0f && height_ > maxHeight) {
        // Wider only within the screen, and only kept when it actually saves height.
        const float widest = maxWidthPx > 0 ? static_cast<float>(maxWidthPx) / scale_ - 2.0f * kShadow : kWideWidth;
        for (float wider : {kMaxWidth, kWideWidth}) {
            wider = std::min(wider, widest);
            if (wider <= width_) continue;
            const float previousWidth = width_, previousHeight = height_;
            height_ = layoutAt(wider, kSparkHeight, kRowGap);
            if (height_ >= previousHeight) {
                height_ = layoutAt(previousWidth, kSparkHeight, kRowGap);
                break;
            }
            if (height_ <= maxHeight) break;
        }
        if (height_ > maxHeight && !lines_.empty()) {
            const float lines = static_cast<float>(lines_.size());
            const float spark = std::max(kMinSparkHeight, kSparkHeight - (height_ - maxHeight) / lines);
            height_ = layoutAt(width_, spark, kRowGap);
            if (height_ > maxHeight) {
                const float gap = std::max(kMinRowGap, kRowGap - (height_ - maxHeight) / std::max(lines - 1.0f, 1.0f));
                height_ = layoutAt(width_, spark, gap);
            }
        }
        // Last resort (extreme setups on small screens): the whole card is drawn slightly smaller.
        if (height_ > maxHeight) scale_ *= maxHeight / height_;
    }
    minWidth_ = std::max(minWidth_, width_);  // never narrower while shown
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

float Card::TileWidth() const {
    const auto columns = static_cast<float>(tileColumns_);
    return (width_ - 2.0f * kPad - (columns - 1.0f) * kTileGap) / columns;
}

float Card::TileDetailWidth() const {
    return TileWidth() - 2.0f * kTilePad;
}

bool Card::EnsureTarget() {
    if (target_) return true;
    target_ = CreateDcTarget(gfx_.d2d());
    if (!target_) return false;
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_)) ||
        FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &iconBrush_))) {
        target_.Reset();
        brush_.Reset();
        iconBrush_.Reset();
        return false;
    }
    if (!roundStroke_) {
        gfx_.d2d()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                      nullptr, 0, &roundStroke_);
    }
    return true;
}

void Card::DrawText(IDWriteTextLayout* layout, float x, float baseline, const D2D1_COLOR_F& color,
                    const D2D1_COLOR_F* iconColor) {
    if (!layout) return;
    brush_->SetColor(color);
    if (iconBrush_) iconBrush_->SetColor(iconColor ? *iconColor : color);
    target_->DrawTextLayout(D2D1::Point2F(Snap(x), Snap(baseline) - TextBaseline(layout)), layout, brush_.Get());
}

void Card::DrawTextRight(IDWriteTextLayout* layout, float right, float baseline, const D2D1_COLOR_F& color,
                         const D2D1_COLOR_F* iconColor) {
    if (!layout) return;
    DrawText(layout, right - TextWidth(layout), baseline, color, iconColor);
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
    const bool bands = theme_.colorByLoad && !row.neutral;
    const D2D1_COLOR_F warn = bands ? MetricColor(row.id, kWarnLoad, theme_) : base;
    const D2D1_COLOR_F crit = bands ? MetricColor(row.id, kCriticalLoad, theme_) : base;
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

    const float gaugeTop = Snap(base + kTileToGauge);
    const float gh = kGaugeHeight;
    if (tile.gauge >= 0.0f) DrawThermometer(tile, left, right, gaugeTop);

    // Detail: the fans of this device (after a fan icon), or its model.
    if (!tile.detail.empty()) {
        const float detailBase = gaugeTop + gh + kGaugeToDetail + kTileDetailSize * kCap;
        float x = left;
        if (tile.fanDetail) {
            const float r = kTileDetailSize * 0.5f;
            DrawFanIcon(left + r, detailBase - kTileDetailSize * kCap / 2.0f, r, faint);
            x += 2.0f * r + 5.0f;
        }
        auto detail = Wrapped(tile.detail, kTileDetailSize, DWRITE_FONT_WEIGHT_NORMAL, TileDetailWidth() - (x - left));
        DrawText(detail.Get(), x, detailBase, faint);
    }
}

// A four-blade fan seen from the front: curved blades around a hub, drawn with ellipses so it stays crisp at
// any size and in any color.
void Card::DrawFanIcon(float cx, float cy, float radius, const D2D1_COLOR_F& color) {
    brush_->SetColor(color);
    D2D1_MATRIX_3X2_F saved;
    target_->GetTransform(&saved);
    for (int blade = 0; blade < 4; ++blade) {
        const float angle = 22.0f + 90.0f * static_cast<float>(blade);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(cx, cy)) * saved);
        target_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx + radius * 0.48f, cy - radius * 0.12f), radius * 0.50f, radius * 0.26f),
            brush_.Get());
    }
    target_->SetTransform(saved);
    // Hub: a ring in the tile's color with the fan's color inside, so the blades read as separate.
    brush_->SetColor(Ink(theme_, theme_.light ? 0.035f : 0.045f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.30f, radius * 0.30f), brush_.Get());
    brush_->SetColor(color);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.18f, radius * 0.18f), brush_.Get());
}

void Card::DrawFanTile(const CardFanLine& fan, const D2D1_RECT_F& area) {
    const bool light = theme_.light;
    const D2D1_COLOR_F strong = Ink(theme_, light ? 0.90f : 0.96f);
    const D2D1_COLOR_F medium = Ink(theme_, light ? 0.62f : 0.66f);
    const D2D1_COLOR_F faint = Ink(theme_, 0.46f);
    brush_->SetColor(Ink(theme_, light ? 0.035f : 0.045f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(area, kTileRadius, kTileRadius), brush_.Get());
    if (theme_.highContrast) {
        brush_->SetColor(FromColorRef(theme_.hcText));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(area, kTileRadius, kTileRadius), brush_.Get(), 1.0f / scale_);
    }
    const float left = area.left + kTilePad, right = area.right - kTilePad;
    const float cy = (area.top + area.bottom) / 2.0f;
    const float base = cy + kTileTitleSize * kCap / 2.0f;
    DrawFanIcon(left + kFanIconSize / 2.0f, cy, kFanIconSize / 2.0f, medium);
    DrawText(Text(fan.title, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left + kFanIconSize + 7.0f, base,
             medium);
    // Speed in front, how hard it is driven after it, quieter.
    float x = right;
    if (!fan.percent.empty()) {
        auto percent = Text(fan.percent, kTileTitleSize, DWRITE_FONT_WEIGHT_NORMAL);
        DrawTextRight(percent.Get(), x, base, faint);
        if (percent) x -= TextWidth(percent.Get()) + 8.0f;
    }
    DrawTextRight(Text(fan.speed, kTileTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), x, base, strong);
}

namespace {

D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

}  // namespace

// A small, realistic thermometer: a snowflake on the cold end, a flame on the hot end, a glass tube with
// scale marks every 10 °C (the warm and hot thresholds highlighted), and a liquid column that stays neutral
// grey while the device is comfortable and only warms up towards yellow, amber (warm) and red (hot). The bulb
// holds the current color.
void Card::DrawThermometer(const CardSensorLine& tile, float left, float right, float top) {
    const bool light = theme_.light;
    const float bulbR = kBulbSize / 2.0f;
    const float cy = top + bulbR;

    // Ends: color emoji (Segoe UI Emoji), vertically centered on the tube.
    auto emoji = [&](const wchar_t* glyph, float x, bool alignRight) {
        if (!emojiFormat_ && FAILED(gfx_.dwrite()->CreateTextFormat(
                                 L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL, kEmojiSize, L"en-us", &emojiFormat_))) {
            return 0.0f;
        }
        ComPtr<IDWriteTextLayout> layout;
        const UINT32 length = static_cast<UINT32>(wcslen(glyph));
        if (FAILED(gfx_.dwrite()->CreateTextLayout(glyph, length, emojiFormat_.Get(), 100.0f, 100.0f, &layout))) {
            return 0.0f;
        }
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        brush_->SetColor(Ink(theme_, 0.6f));  // only used where color fonts are not available
        const float px = alignRight ? x - m.widthIncludingTrailingWhitespace : x;
        target_->DrawTextLayout(D2D1::Point2F(Snap(px), Snap(cy - m.height / 2.0f)), layout.Get(), brush_.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        return m.widthIncludingTrailingWhitespace;
    };
    const float coldWidth = emoji(L"\u2744\uFE0F", left, false);
    const float hotWidth = emoji(L"\U0001F525", right, true);

    const float bulbX = left + coldWidth + 4.0f + bulbR;
    const float tubeLeft = bulbX;  // the tube starts inside the bulb
    const float tubeRight = right - hotWidth - 5.0f;
    if (tubeRight - tubeLeft < 10.0f) return;
    const float wall = std::max(1.4f, 2.0f / scale_);  // glass thickness around the liquid
    const float glassR = 3.4f;                         // half the tube's outer height
    const float liquidR = std::max(glassR - wall, 1.0f / scale_);
    auto at = [&](float gauge) { return tubeLeft + (tubeRight - wall - tubeLeft) * std::clamp(gauge, 0.0f, 1.0f); };

    // Scale: neutral grey while comfortable -> yellow -> amber at the warm mark -> red at the hot mark.
    const D2D1_COLOR_F cold = Rgb(light ? 0x8A8F98 : 0x9AA0A8);
    const D2D1_COLOR_F fine = cold;  // still neutral halfway to the warm mark
    const D2D1_COLOR_F yellow = Rgb(light ? 0xA16207 : 0xF5D04A);
    const D2D1_COLOR_F warm = Rgb(light ? 0xB35C00 : 0xFFB340);
    const D2D1_COLOR_F hot = Rgb(light ? 0xC42B1C : 0xFF6B6B);
    const float warmAt = std::clamp(tile.warmAt, 0.05f, 0.98f);
    const float hotAt = std::clamp(tile.hotAt, warmAt + 0.01f, 0.99f);
    const float fineAt = warmAt * 0.55f;
    const float yellowAt = warmAt * 0.85f;
    auto colorAt = [&](float g) {
        if (theme_.highContrast) return FromColorRef(theme_.hcHighlight);
        if (g <= fineAt) return Mix(cold, fine, g / fineAt);
        if (g <= yellowAt) return Mix(fine, yellow, (g - fineAt) / (yellowAt - fineAt));
        if (g <= warmAt) return Mix(yellow, warm, (g - yellowAt) / (warmAt - yellowAt));
        if (g <= hotAt) return Mix(warm, hot, (g - warmAt) / (hotAt - warmAt));
        return hot;
    };
    const float level = std::clamp(tile.gauge, 0.0f, 1.0f);
    const D2D1_COLOR_F current = colorAt(level);

    // Glass: tube and bulb, with a faint rim.
    const D2D1_COLOR_F glass = Ink(theme_, light ? 0.10f : 0.13f);
    brush_->SetColor(glass);
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(tubeLeft, cy - glassR, tubeRight, cy + glassR), glassR, glassR), brush_.Get());
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bulbX, cy), bulbR, bulbR), brush_.Get());

    // Liquid column with the scale's gradient (so its tip shows the current color), and the bulb.
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, colorAt(0.0f)},         {fineAt, colorAt(fineAt)},
                                        {yellowAt, colorAt(yellowAt)}, {warmAt, colorAt(warmAt)},
                                        {hotAt, colorAt(hotAt)},       {1.0f, colorAt(1.0f)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    const float tip = std::max(at(level), tubeLeft + 2.0f * liquidR);
    const D2D1_ROUNDED_RECT column =
        D2D1::RoundedRect(D2D1::RectF(tubeLeft, cy - liquidR, tip, cy + liquidR), liquidR, liquidR);
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, static_cast<UINT32>(std::size(stops)), &collection)) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(at(0.0f), cy), D2D1::Point2F(at(1.0f), cy)),
            collection.Get(), &gradient))) {
        target_->FillRoundedRectangle(column, gradient.Get());
    } else {
        brush_->SetColor(current);
        target_->FillRoundedRectangle(column, brush_.Get());
    }
    brush_->SetColor(current);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bulbX, cy), bulbR - wall, bulbR - wall), brush_.Get());

    // Glass reflections: a thin line along the tube and a spot on the bulb.
    if (!theme_.highContrast) {
        brush_->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, light ? 0.55f : 0.22f));
        target_->DrawLine(D2D1::Point2F(tubeLeft + bulbR, cy - glassR + wall * 0.55f),
                          D2D1::Point2F(tubeRight - glassR, cy - glassR + wall * 0.55f), brush_.Get(), 0.8f / scale_);
        brush_->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f));
        target_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(bulbX - bulbR * 0.32f, cy - bulbR * 0.32f), bulbR * 0.22f, bulbR * 0.22f),
            brush_.Get());
    }

    // Scale marks under the tube: every 10 °C, the warm and hot thresholds longer and in their colors.
    const float markTop = cy + glassR + 1.0f;
    const float step = tile.tickStep > 0.02f ? tile.tickStep : 0.125f;
    brush_->SetColor(Ink(theme_, light ? 0.28f : 0.30f));
    for (float g = step; g < 0.999f; g += step) {
        if (std::fabs(g - warmAt) < step * 0.3f || std::fabs(g - hotAt) < step * 0.3f) continue;
        const float x = Snap(at(g)) + 0.5f / scale_;
        target_->DrawLine(D2D1::Point2F(x, markTop), D2D1::Point2F(x, markTop + 2.0f), brush_.Get(), 1.0f / scale_);
    }
    for (const auto& [g, color] : {std::pair{warmAt, warm}, std::pair{hotAt, hot}}) {
        const float x = Snap(at(g)) + 0.5f / scale_;
        brush_->SetColor(theme_.highContrast ? FromColorRef(theme_.hcText) : color);
        target_->DrawLine(D2D1::Point2F(x, markTop), D2D1::Point2F(x, markTop + 3.0f), brush_.Get(), 1.2f / scale_);
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

    const float rowsTop = y;
    for (const RowBox& box : boxes_) {
        if (box.row >= model.rows.size() || box.line >= lines_.size()) continue;
        const CardRow& row = model.rows[box.row];
        const RowLine& line = lines_[box.line];
        const float x0 = left + box.x, x1 = x0 + box.w;
        const float valueSize = line.half ? kHalfValueSize : kValueSize;
        // Marker and value use the color of the current load, exactly like the strip.
        const D2D1_COLOR_F accent = MetricColor(row.id, row.load, theme_);
        const bool hot = theme_.colorByLoad && row.load >= kWarnLoad;
        const D2D1_COLOR_F valueColor = hot && !theme_.highContrast ? accent : strong;
        const float lineTop = rowsTop + line.y;
        const float contentTop = lineTop + kRowPad;
        const float valueBase = contentTop + valueSize * kCap;
        const float subBase = valueBase + kValueToSub;
        const float sparkTop = contentTop + LineTextHeight(line.half, line.extra);

        // The tile holding this metric, the same surface as the sensor tiles.
        const D2D1_RECT_F tile = D2D1::RectF(Snap(x0 - kRowPad), Snap(lineTop), Snap(x1 + kRowPad),
                                             Snap(lineTop + LineHeight(line.half, line.extra, sparkHeight_)));
        brush_->SetColor(Ink(theme_, light ? 0.035f : 0.045f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(tile, kTileRadius, kTileRadius), brush_.Get());
        if (theme_.highContrast) {
            brush_->SetColor(FromColorRef(theme_.hcText));
            target_->DrawRoundedRectangle(D2D1::RoundedRect(tile, kTileRadius, kTileRadius), brush_.Get(),
                                          1.0f / scale_);
        }

        brush_->SetColor(accent);
        const float dotY = valueBase - kTitleSize * kCap / 2.0f;
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0 + 4.0f, dotY), 3.5f, 3.5f), brush_.Get());
        DrawText(Text(row.title, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), x0 + kIndent, valueBase, strong);
        DrawTextRight(Text(row.value, valueSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), x1, valueBase, valueColor);

        // Subtitle (wrapped, never cut); full rows show the peak on the right of its first line. Icons stay
        // neutral and quiet: color is kept for what carries information (marker, value, graph).
        const D2D1_COLOR_F iconColor = theme_.highContrast ? medium : Ink(theme_, light ? 0.40f : 0.42f);
        auto sub = Wrapped(box.subtitle, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL, box.subWidth);
        DrawText(sub.Get(), x0 + kIndent, subBase, medium, &iconColor);
        if (!line.half) {
            DrawTextRight(Text(row.peak, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), x1, subBase, faint, &iconColor);
        }

        DrawSparkline(row, model.capacity, D2D1::RectF(x0, Snap(sparkTop), x1, Snap(sparkTop + sparkHeight_)));
    }
    if (!lines_.empty()) {
        const RowLine& last = lines_.back();
        y = rowsTop + last.y + LineHeight(last.half, last.extra, sparkHeight_);
    }

    // Sensors: a grid of tiles, one per device, with its fans (see CardSensorLine).
    if (HasSensors(model)) {
        y += kSectionGap;
        const float titleBase = y + kSmallSize * kCap;
        DrawText(Text(model.sensorsTitle, kSmallSize, DWRITE_FONT_WEIGHT_SEMI_BOLD).Get(), left, titleBase, medium);
        y = titleBase + kSectionTitleGap;
        const float tileW = TileWidth();
        const size_t rows = TileRows(model.sensors.size(), tileColumns_);
        const auto columns = static_cast<size_t>(tileColumns_);
        for (size_t r = 0; r < rows; ++r) {
            const float tileH = TileHeight() + (r < tileExtra_.size() ? tileExtra_[r] : 0.0f);
            for (size_t c = 0; c < columns; ++c) {
                const size_t i = r * columns + c;
                if (i >= model.sensors.size()) break;
                const float x = left + static_cast<float>(c) * (tileW + kTileGap);
                DrawSensorTile(model.sensors[i], D2D1::RectF(Snap(x), Snap(y), Snap(x + tileW), Snap(y + tileH)));
            }
            y += tileH + (r + 1 < rows ? kTileGap : 0.0f);
        }
        // Fans of their own: compact lines, two (or three) per row.
        const size_t fanRows = TileRows(model.fans.size(), tileColumns_);
        if (fanRows && rows) y += kTileGap;
        for (size_t r = 0; r < fanRows; ++r) {
            for (size_t c = 0; c < columns; ++c) {
                const size_t i = r * columns + c;
                if (i >= model.fans.size()) break;
                const float x = left + static_cast<float>(c) * (tileW + kTileGap);
                DrawFanTile(model.fans[i], D2D1::RectF(Snap(x), Snap(y), Snap(x + tileW), Snap(y + kFanLineHeight)));
            }
            y += kFanLineHeight + (r + 1 < fanRows ? kTileGap : 0.0f);
        }
        if (!model.sensorsHint.empty()) {
            if (rows || fanRows) y += 6.0f;
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

    // Footer: version on the left, update status on the right (in the accent color when there is news).
    const float sep = Snap(y + kFooterGap) + 0.5f / scale_;
    brush_->SetColor(Ink(theme_, 0.08f));
    target_->DrawLine(D2D1::Point2F(card.left + 1.0f, sep), D2D1::Point2F(card.right - 1.0f, sep), brush_.Get(),
                      1.0f / scale_);
    const float footerBase = sep + kSepToHint + kSmallSize * kCap;
    DrawText(Text(model.footerLeft, kSmallSize, DWRITE_FONT_WEIGHT_NORMAL).Get(), left, footerBase, faint);
    const D2D1_COLOR_F accent = MetricColor(MetricId::Cpu, 0.0f, theme_);
    DrawTextRight(Text(model.footerRight, kSmallSize,
                       model.footerAccent ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)
                      .Get(),
                  right, footerBase, model.footerAccent ? accent : faint);

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        target_.Reset();
        brush_.Reset();
        iconBrush_.Reset();
        return false;
    }
    return SUCCEEDED(hr);
}
