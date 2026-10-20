#include "renderer.h"

#include <algorithm>
#include <cmath>

namespace {

// Typography and spacing in DIPs (1 DIP = 1 px at 100% scaling).
constexpr float kLabelSize = 9.0f;
constexpr float kLabelTracking = 0.7f;
constexpr float kValueSize = 13.0f;
constexpr float kWeakSize = 10.0f;
constexpr float kCompactValueSize = 12.0f;
constexpr float kCompactWeakSize = 9.5f;
constexpr float kCapRatio = 0.70f;  // cap/digit height of Segoe UI (1434/2048)

constexpr float kOuterPad = 9.0f;
constexpr float kGap = 15.0f;
constexpr float kLabelToValue = 4.5f;
constexpr float kValueToBar = 5.0f;
constexpr float kBarHeight = 3.0f;
constexpr float kCompactThreshold = 36.0f;  // below this ("small taskbar buttons") use a one-line layout
constexpr float kCompactInlineGap = 5.0f;
constexpr float kCompactValueToBar = 3.5f;
constexpr float kCompactBarHeight = 2.0f;
constexpr float kVerticalRow = 42.0f;
constexpr float kVerticalPad = 6.0f;
constexpr float kMinBlockWidth = 30.0f;
constexpr float kHoverRadius = 4.0f;

// FNV-1a, enough to detect "nothing visible changed".
struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void Bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ull;
    }
    template <typename T>
    void Add(const T& value) {
        Bytes(&value, sizeof(value));
    }
    void Add(const std::wstring& s) {
        Bytes(s.data(), s.size() * sizeof(wchar_t));
        Add(s.size());
    }
};

int LoadLevel(float load) {
    return load >= kCriticalLoad ? 2 : load >= kWarnLoad ? 1 : 0;
}

}  // namespace

void Renderer::SetTheme(const Theme& theme) {
    theme_ = theme;
    Hasher h;
    h.Add(theme.light);
    h.Add(theme.colorByLoad);
    h.Add(theme.highContrast);
    h.Add(theme.hcText);
    h.Add(theme.hcBackground);
    h.Add(theme.hcHighlight);
    themeStamp_ = h.h;
}

float Renderer::Snap(float dip) const {
    return std::round(dip * scale_) / scale_;
}

void Renderer::ResetTarget() {
    target_.Reset();
    brush_.Reset();
    weakBrush_.Reset();
    layoutKey_.clear();  // value layouts reference the old brush as a drawing effect
    lastSignature_ = 0;
}

bool Renderer::EnsureTarget() {
    if (target_) return true;
    target_ = CreateDcTarget(gfx_.d2d());
    if (!target_) return false;
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_)) ||
        FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &weakBrush_))) {
        ResetTarget();
        return false;
    }
    if (!labelFormat_) {
        IDWriteFactory* dw = gfx_.dwrite();
        // The strip only shows Latin labels and digits, so a fixed locale is fine here.
        if (FAILED(dw->CreateTextFormat(gfx_.family().c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kLabelSize, L"en-us",
                                        &labelFormat_)) ||
            FAILED(dw->CreateTextFormat(gfx_.family().c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kValueSize, L"en-us",
                                        &valueFormat_))) {
            return false;
        }
        labelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        valueFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    layoutKey_.clear();
    lastSignature_ = 0;
    return true;
}

ComPtr<IDWriteTextLayout> Renderer::MakeLabel(const std::wstring& text, float& baseline, float& width) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx_.dwrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), labelFormat_.Get(),
                                               1000.0f, 100.0f, &layout))) {
        return nullptr;
    }
    ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout.As(&layout1))) {
        layout1->SetCharacterSpacing(0.0f, kLabelTracking, 0.0f, {0, static_cast<UINT32>(text.size())});
    }
    baseline = TextBaseline(layout.Get());
    width = TextWidth(layout.Get()) - kLabelTracking;  // ignore the tracking after the last letter
    return layout;
}

ComPtr<IDWriteTextLayout> Renderer::MakeValue(const std::wstring& strong, const std::wstring& weak, float& baseline,
                                              float& width) {
    const std::wstring text = strong + weak;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx_.dwrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), valueFormat_.Get(),
                                               1000.0f, 100.0f, &layout))) {
        return nullptr;
    }
    gfx_.UseTabularFigures(layout.Get(), static_cast<UINT32>(text.size()));
    const DWRITE_TEXT_RANGE strongRange{0, static_cast<UINT32>(strong.size())};
    const DWRITE_TEXT_RANGE weakRange{static_cast<UINT32>(strong.size()), static_cast<UINT32>(weak.size())};
    layout->SetFontSize(compact_ ? kCompactValueSize : kValueSize, strongRange);
    if (!weak.empty()) {
        layout->SetFontSize(compact_ ? kCompactWeakSize : kWeakSize, weakRange);
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL, weakRange);
        if (weakBrush_) layout->SetDrawingEffect(weakBrush_.Get(), weakRange);
    }
    baseline = TextBaseline(layout.Get());
    width = TextWidth(layout.Get());
    return layout;
}

SIZE Renderer::Layout(const LayoutInput& input, const std::vector<ItemView>& items) {
    // Brushes must exist before the value layouts (the dimmed brush is used as a drawing effect).
    if (!EnsureTarget()) return size_;

    std::wstring key =
        std::to_wstring(input.dpi) + L'|' + std::to_wstring(input.vertical) + L'|' + std::to_wstring(input.thicknessPx);
    for (const auto& item : items) {
        key += L'|' + item.label + L'\x1f' + item.worstStrong + L'\x1f' + item.worstWeak;
    }
    if (key == layoutKey_ && blocks_.size() == items.size()) return size_;
    layoutKey_ = key;
    lastSignature_ = 0;

    scale_ = static_cast<float>(std::max<UINT>(input.dpi, 96)) / 96.0f;
    const float thickness = static_cast<float>(input.thicknessPx) / scale_;
    compact_ = !input.vertical && thickness < kCompactThreshold;

    blocks_.assign(items.size(), Block{});
    float cursor = kOuterPad;
    for (size_t i = 0; i < items.size(); ++i) {
        Block& b = blocks_[i];
        b.label = MakeLabel(items[i].label, b.labelBaseline, b.labelWidth);
        float worstBaseline = 0.0f, worstWidth = 0.0f;
        MakeValue(items[i].worstStrong, items[i].worstWeak, worstBaseline, worstWidth);

        const float natural =
            compact_ ? b.labelWidth + kCompactInlineGap + worstWidth : std::max(b.labelWidth, worstWidth);
        if (!input.vertical) {
            b.w = std::ceil(std::max(natural, kMinBlockWidth));
            b.x = cursor;
            b.h = thickness;
            cursor += b.w + kGap;
        } else {
            const float avail = std::max(thickness - 2.0f * kVerticalPad, 10.0f);
            b.x = kVerticalPad;
            b.w = avail;
            b.y = 4.0f + static_cast<float>(i) * kVerticalRow;
            b.h = kVerticalRow;
            b.contentScale = natural > avail ? avail / natural : 1.0f;
        }
    }

    if (!input.vertical) {
        const float width = items.empty() ? 2.0f * kOuterPad : cursor - kGap + kOuterPad;
        size_ = {static_cast<LONG>(std::ceil(width * scale_)), input.thicknessPx};
    } else {
        const float height = 8.0f + static_cast<float>(items.size()) * kVerticalRow;
        size_ = {input.thicknessPx, static_cast<LONG>(std::ceil(height * scale_))};
    }
    return size_;
}

int Renderer::FirstOverflow(const std::vector<ItemView>& items, float* excessDip) {
    if (items.size() != blocks_.size()) return 0;
    for (size_t i = 0; i < items.size(); ++i) {
        float baseline = 0.0f, width = 0.0f;
        MakeValue(items[i].strong, items[i].weak, baseline, width);
        const Block& b = blocks_[i];
        const float room = (compact_ ? b.w - b.labelWidth - kCompactInlineGap : b.w) / b.contentScale;
        if (width > room + 0.01f) {
            if (excessDip) *excessDip = width - room;
            return static_cast<int>(i);
        }
    }
    return -1;
}

uint64_t Renderer::Signature(const std::vector<ItemView>& items, bool hover) const {
    Hasher h;
    h.Add(themeStamp_);
    h.Add(hover);
    h.Add(size_.cx);
    h.Add(size_.cy);
    for (size_t i = 0; i < items.size() && i < blocks_.size(); ++i) {
        const ItemView& item = items[i];
        h.Add(item.strong);
        h.Add(item.weak);
        // Fill width in device pixels (half-pixel resolution): smaller changes are invisible.
        const int fill = static_cast<int>(std::lround(std::clamp(item.fraction, 0.0f, 1.0f) * blocks_[i].w /
                                                      blocks_[i].contentScale * scale_ * 2.0f));
        h.Add(fill);
        h.Add(theme_.colorByLoad ? LoadLevel(item.load) : 0);
    }
    return h.h == 0 ? 1 : h.h;
}

bool Renderer::NeedsPaint(const std::vector<ItemView>& items, bool hover) const {
    return lastSignature_ == 0 || items.size() != blocks_.size() || Signature(items, hover) != lastSignature_;
}

bool Renderer::Paint(const std::vector<ItemView>& items, bool hover) {
    if (size_.cx <= 0 || size_.cy <= 0 || items.size() != blocks_.size()) return false;
    if (!EnsureTarget() || layoutKey_.empty()) return false;  // target recreated: the caller re-runs Layout()
    if (!surface_.Ensure(size_.cx, size_.cy)) return false;

    const RECT bounds{0, 0, size_.cx, size_.cy};
    if (FAILED(target_->BindDC(surface_.dc(), &bounds))) return false;
    target_->SetDpi(96.0f * scale_, 96.0f * scale_);

    const bool light = theme_.light;
    const float widthDip = static_cast<float>(size_.cx) / scale_;
    const float heightDip = static_cast<float>(size_.cy) / scale_;

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    // Alpha 1/255 on the background: invisible, but it makes the whole window receive the mouse.
    target_->Clear(D2D1::ColorF(0, 0, 0, 1.0f / 255.0f));

    if (hover) {
        brush_->SetColor(Ink(theme_, light ? 0.07f : 0.09f));
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(1.0f, 2.0f, widthDip - 1.0f, heightDip - 2.0f), kHoverRadius, kHoverRadius),
            brush_.Get());
    }

    weakBrush_->SetColor(Ink(theme_, light ? 0.56f : 0.58f));

    for (size_t i = 0; i < items.size(); ++i) {
        const ItemView& item = items[i];
        Block& b = blocks_[i];

        if (!b.value || b.strong != item.strong || b.weak != item.weak) {
            b.value = MakeValue(item.strong, item.weak, b.valueBaseline, b.valueWidth);
            b.strong = item.strong;
            b.weak = item.weak;
        }
        if (!b.label || !b.value) continue;

        const float s = b.contentScale;
        const float capLabel = kLabelSize * kCapRatio;
        const float capValue = (compact_ ? kCompactValueSize : kValueSize) * kCapRatio;
        const float barH = std::max(compact_ ? kCompactBarHeight : kBarHeight, 2.0f / scale_);
        const float localH = b.h / s;

        float labelBase, valueBase, barTop, valueX = 0.0f;
        if (compact_) {
            const float visual = capValue + kCompactValueToBar + barH;
            valueBase = (localH - visual) / 2.0f + capValue;
            labelBase = valueBase;
            barTop = valueBase + kCompactValueToBar;
            valueX = b.labelWidth + kCompactInlineGap;
        } else {
            const float visual = capLabel + kLabelToValue + capValue + kValueToBar + barH;
            labelBase = (localH - visual) / 2.0f + capLabel;
            valueBase = labelBase + kLabelToValue + capValue;
            barTop = valueBase + kValueToBar;
        }

        // Content scale (only used by a narrow vertical taskbar), anchored at the block corner.
        target_->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(b.x, b.y));

        brush_->SetColor(Ink(theme_, light ? 0.60f : 0.62f));
        target_->DrawTextLayout(D2D1::Point2F(0.0f, Snap(labelBase) - b.labelBaseline), b.label.Get(), brush_.Get());

        brush_->SetColor(Ink(theme_, light ? 0.90f : 0.97f));
        target_->DrawTextLayout(D2D1::Point2F(valueX, Snap(valueBase) - b.valueBaseline), b.value.Get(), brush_.Get());

        const float barW = b.w / s;
        const float top = Snap(barTop);
        const float radius = barH / 2.0f;
        brush_->SetColor(Ink(theme_, light ? 0.11f : 0.14f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, top, barW, top + barH), radius, radius),
                                      brush_.Get());

        const float fraction = std::clamp(item.fraction, 0.0f, 1.0f);
        if (fraction > 0.001f) {
            const float fill = std::max(barW * fraction, barH);  // never smaller than a round dot
            brush_->SetColor(MetricColor(item.id, item.load, theme_));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, top, fill, top + barH), radius, radius),
                                          brush_.Get());
        }
    }

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ResetTarget();
        return false;
    }
    if (FAILED(hr)) return false;
    lastSignature_ = Signature(items, hover);
    return true;
}
