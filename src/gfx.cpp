#include "gfx.h"

bool Gfx::Init() {
    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options,
                                 reinterpret_cast<void**>(d2d_.GetAddressOf())))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        return false;
    }
    // Windows 11 ships Segoe UI Variable (the taskbar's own font); Windows 10 has Segoe UI.
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) {
            family_ = L"Segoe UI Variable Text";
        }
    }
    if (SUCCEEDED(dwrite_->CreateTypography(&tabular_))) {
        tabular_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    }
    return true;
}

void Gfx::UseTabularFigures(IDWriteTextLayout* layout, UINT32 length) const {
    if (layout && tabular_) layout->SetTypography(tabular_.Get(), {0, length});
}

bool Surface::Ensure(int width, int height) {
    if (dc_ && width == width_ && height == height_) return true;
    Release();
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dc_ = CreateCompatibleDC(nullptr);
    if (dc_) bitmap_ = CreateDIBSection(dc_, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!dc_ || !bitmap_) {
        Release();
        return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    width_ = width;
    height_ = height;
    return true;
}

void Surface::Release() {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    width_ = height_ = 0;
}

bool Surface::Present(HWND hwnd, const POINT* screenPosition, BYTE alpha) const {
    if (!dc_ || !hwnd) return false;
    POINT source{0, 0};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT position = screenPosition ? *screenPosition : POINT{};
    return UpdateLayeredWindow(hwnd, nullptr, screenPosition ? &position : nullptr, &size, dc_, &source, 0, &blend,
                               ULW_ALPHA) != FALSE;
}

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha) {
    return D2D1::ColorF(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

D2D1_COLOR_F FromColorRef(COLORREF color, float alpha) {
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, alpha);
}

D2D1_COLOR_F Ink(const Theme& theme, float alpha) {
    if (theme.highContrast) {
        // Text must be solid in high contrast; translucent ink is kept only for subtle decorations.
        return FromColorRef(theme.hcText, alpha >= 0.4f ? 1.0f : alpha * 2.0f);
    }
    return theme.light ? D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha) : D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha);
}

D2D1_COLOR_F MetricColor(MetricId id, float load, const Theme& theme) {
    if (theme.highContrast) return FromColorRef(theme.hcHighlight);
    static constexpr uint32_t kDark[kMetricCount] = {0x4CC2FF, 0xB69CFF, 0x4ADE80, 0xF67EC1};
    static constexpr uint32_t kLight[kMetricCount] = {0x0067C0, 0x6D3FD9, 0x13803F, 0xC81E73};
    if (theme.colorByLoad) {
        if (load >= kCriticalLoad) return Rgb(theme.light ? 0xC42B1C : 0xFF6B6B);
        if (load >= kWarnLoad) return Rgb(theme.light ? 0xB35C00 : 0xFFB340);
    }
    const int index = static_cast<int>(id);
    return Rgb(theme.light ? kLight[index] : kDark[index]);
}

ComPtr<ID2D1DCRenderTarget> CreateDcTarget(ID2D1Factory* factory) {
    // Software rendering: the bitmaps are tiny, and this way we never add load to the GPU we measure.
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    ComPtr<ID2D1DCRenderTarget> target;
    if (FAILED(factory->CreateDCRenderTarget(&props, &target))) return nullptr;
    // ClearType cannot be used on a transparent background.
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return target;
}

float TextBaseline(IDWriteTextLayout* layout) {
    DWRITE_LINE_METRICS line{};
    UINT32 lines = 0;
    layout->GetLineMetrics(&line, 1, &lines);
    return line.baseline;
}

float TextWidth(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}
