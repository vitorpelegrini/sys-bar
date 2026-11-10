#include "gfx.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <vector>

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

bool Surface::Present(HWND hwnd, const POINT* screenPosition, BYTE alpha, const RECT* dirty) const {
    if (!dc_ || !hwnd) return false;
    POINT source{0, 0};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT position = screenPosition ? *screenPosition : POINT{};
    UPDATELAYEREDWINDOWINFO info{sizeof(info)};
    info.pptDst = screenPosition ? &position : nullptr;
    info.psize = &size;
    info.hdcSrc = dc_;
    info.pptSrc = &source;
    info.pblend = &blend;
    info.dwFlags = ULW_ALPHA;
    info.prcDirty = dirty;  // only this part of the bitmap is copied (nullptr: all of it)
    return UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
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
    static constexpr uint32_t kDark[kMetricCount] = {0x4CC2FF, 0xB69CFF, 0x4ADE80, 0xF67EC1,
                                                     0x2DD4BF, 0xFB923C, 0xA3E635};
    static constexpr uint32_t kLight[kMetricCount] = {0x0067C0, 0x6D3FD9, 0x13803F, 0xC81E73,
                                                      0x0F766E, 0xC2410C, 0x4D7C0F};
    if (theme.colorByLoad) {
        if (load >= kCriticalLoad) return Rgb(theme.light ? 0xC42B1C : 0xFF6B6B);
        if (load >= kWarnLoad) return Rgb(theme.light ? 0xB35C00 : 0xFFB340);
    }
    const int index = static_cast<int>(id);
    return Rgb(theme.light ? kLight[index] : kDark[index]);
}

namespace {

// GUID_WICPixelFormat32bppPBGRA, spelled out so nothing has to be linked from the imaging library.
constexpr GUID kPremultipliedBgra = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};

// A locked rectangle of the pixels.
class PixelLock final : public IWICBitmapLock {
public:
    PixelLock(BYTE* data, UINT width, UINT height, UINT stride)
        : data_(data), width_(width), height_(height), stride_(stride) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IWICBitmapLock)) {
            *out = static_cast<IWICBitmapLock*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&refs_);
        if (left == 0) delete this;
        return static_cast<ULONG>(left);
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_POINTER;
        *width = width_;
        *height = height_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStride(UINT* stride) override {
        if (!stride) return E_POINTER;
        *stride = stride_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* size, WICInProcPointer* data) override {
        if (!size || !data) return E_POINTER;
        *size = height_ ? stride_ * (height_ - 1) + width_ * 4 : 0;
        *data = data_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* format) override {
        if (!format) return E_POINTER;
        *format = kPremultipliedBgra;
        return S_OK;
    }

private:
    ~PixelLock() = default;
    LONG refs_ = 1;
    BYTE* data_;
    UINT width_, height_, stride_;
};

// The surface's pixels as an imaging bitmap Direct2D can render into, without owning or copying them.
class PixelBitmap final : public IWICBitmap {
public:
    PixelBitmap(BYTE* data, UINT width, UINT height) : data_(data), width_(width), height_(height) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IWICBitmapSource) || iid == __uuidof(IWICBitmap)) {
            *out = static_cast<IWICBitmap*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&refs_);
        if (left == 0) delete this;
        return static_cast<ULONG>(left);
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if (!width || !height) return E_POINTER;
        *width = width_;
        *height = height_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* format) override {
        if (!format) return E_POINTER;
        *format = kPremultipliedBgra;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) override {
        if (!dpiX || !dpiY) return E_POINTER;
        *dpiX = *dpiY = 96.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette*) override { return WINCODEC_ERR_PALETTEUNAVAILABLE; }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT size, BYTE* buffer) override {
        WICRect r{0, 0, static_cast<INT>(width_), static_cast<INT>(height_)};
        if (rect) r = *rect;
        if (!buffer || !Inside(r) || stride < static_cast<UINT>(r.Width) * 4 ||
            size < (r.Height ? stride * static_cast<UINT>(r.Height - 1) + static_cast<UINT>(r.Width) * 4 : 0)) {
            return E_INVALIDARG;
        }
        for (INT y = 0; y < r.Height; ++y) {
            memcpy(buffer + static_cast<size_t>(y) * stride, Row(r.X, r.Y + y), static_cast<size_t>(r.Width) * 4);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(const WICRect* rect, DWORD, IWICBitmapLock** lock) override {
        if (!lock) return E_POINTER;
        *lock = nullptr;
        WICRect r{0, 0, static_cast<INT>(width_), static_cast<INT>(height_)};
        if (rect) r = *rect;
        if (!Inside(r)) return E_INVALIDARG;
        *lock = new (std::nothrow)
            PixelLock(Row(r.X, r.Y), static_cast<UINT>(r.Width), static_cast<UINT>(r.Height), width_ * 4);
        return *lock ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetPalette(IWICPalette*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetResolution(double, double) override { return S_OK; }

private:
    ~PixelBitmap() = default;
    bool Inside(const WICRect& r) const {
        return r.X >= 0 && r.Y >= 0 && r.Width >= 0 && r.Height >= 0 && static_cast<UINT>(r.X) <= width_ &&
               static_cast<UINT>(r.Y) <= height_ && static_cast<UINT>(r.Width) <= width_ - static_cast<UINT>(r.X) &&
               static_cast<UINT>(r.Height) <= height_ - static_cast<UINT>(r.Y);
    }
    BYTE* Row(INT x, INT y) const { return data_ + (static_cast<size_t>(y) * width_ + static_cast<size_t>(x)) * 4; }

    LONG refs_ = 1;
    BYTE* data_;
    UINT width_, height_;
};

}  // namespace

ComPtr<ID2D1RenderTarget> CreateSurfaceTarget(ID2D1Factory* factory, Surface& surface) {
    if (!factory || !surface.bits() || surface.width() <= 0 || surface.height() <= 0) return nullptr;
    ComPtr<IWICBitmap> pixels;
    pixels.Attach(new (std::nothrow)
                      PixelBitmap(reinterpret_cast<BYTE*>(surface.bits()), static_cast<UINT>(surface.width()),
                                  static_cast<UINT>(surface.height())));
    if (!pixels) return nullptr;
    // Software rendering: the bitmaps are tiny, and this way we never add load to the GPU we measure.
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(factory->CreateWicBitmapRenderTarget(pixels.Get(), &props, &target))) return nullptr;
    // ClearType cannot be used on a transparent background.
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return target;
}

float TextBaseline(IDWriteTextLayout* layout) {
    // Baseline of the first line (wrapped layouts have several: ask for all of them).
    DWRITE_LINE_METRICS line{};
    UINT32 lines = 0;
    if (SUCCEEDED(layout->GetLineMetrics(&line, 1, &lines))) return line.baseline;
    std::vector<DWRITE_LINE_METRICS> all(lines);
    if (lines == 0 || FAILED(layout->GetLineMetrics(all.data(), lines, &lines))) return 0.0f;
    return all[0].baseline;
}

float TextWidth(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

namespace {

uint32_t Channel(uint32_t pixel, int shift) {
    return (pixel >> shift) & 0xFF;
}

// Direct2D draws a rounded corner as a cubic Bezier quarter arc, flattened into straight segments: the
// corner is matched point for point so the bars look exactly as when Direct2D drew them.
struct Corner {
    float cx = 0, cy = 0;      // arc center
    float px[17]{}, py[17]{};  // arc points, from the horizontal edge to the vertical one
    int segments = 1;
};

int ArcSegments(float radius) {
    // Wang's bound for a cubic Bezier quarter circle; the tolerance is the one that best matches Direct2D.
    constexpr float kKappa = 0.5522847f, kTolerance = 0.2f;
    const float second = radius * std::hypot(1.0f - kKappa, 1.0f - 2.0f * kKappa);
    return std::clamp(static_cast<int>(std::ceil(std::sqrt(0.75f * second / kTolerance))), 1, 16);
}

Corner MakeCorner(float cx, float cy, float radius, float sx, float sy, int segments) {
    // Quarter arc from (cx + sx*r, cy) to (cx, cy + sy*r).
    constexpr float kKappa = 0.5522847f;
    Corner c;
    c.cx = cx;
    c.cy = cy;
    c.segments = segments;
    const float p0x = cx + sx * radius, p0y = cy;
    const float p1x = cx + sx * radius, p1y = cy + sy * radius * kKappa;
    const float p2x = cx + sx * radius * kKappa, p2y = cy + sy * radius;
    const float p3x = cx, p3y = cy + sy * radius;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments), u = 1.0f - t;
        const float a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
        c.px[i] = a * p0x + b * p1x + d * p2x + e * p3x;
        c.py[i] = a * p0y + b * p1y + d * p2y + e * p3y;
    }
    return c;
}

// Inside the corner's fan (center + flattened arc)?
bool InCorner(const Corner& c, float x, float y) {
    for (int i = 0; i < c.segments; ++i) {
        const float ex = c.px[i + 1] - c.px[i], ey = c.py[i + 1] - c.py[i];
        const float side = ex * (y - c.py[i]) - ey * (x - c.px[i]);
        const float center = ex * (c.cy - c.py[i]) - ey * (c.cx - c.px[i]);
        if ((side > 0) != (center > 0) && side != 0.0f) return false;
    }
    return true;
}

}  // namespace

void FillRoundedRect(uint32_t* pixels, int stride, const RECT& clip, const D2D1_RECT_F& rect, float radius,
                     const D2D1_COLOR_F& color) {
    const float x0 = rect.left, y0 = rect.top, x1 = rect.right, y1 = rect.bottom;
    radius = std::max(0.0f, std::min({radius, (x1 - x0) / 2.0f, (y1 - y0) / 2.0f}));
    const int segments = ArcSegments(radius);
    const float ix0 = x0 + radius, ix1 = x1 - radius, iy0 = y0 + radius, iy1 = y1 - radius;
    const Corner corners[4] = {
        MakeCorner(ix0, iy0, radius, -1, -1, segments), MakeCorner(ix1, iy0, radius, 1, -1, segments),
        MakeCorner(ix0, iy1, radius, -1, 1, segments), MakeCorner(ix1, iy1, radius, 1, 1, segments)};
    auto inside = [&](float x, float y) {
        if (x < x0 || x >= x1 || y < y0 || y >= y1) return false;
        if (x < ix0 && y < iy0) return InCorner(corners[0], x, y);
        if (x >= ix1 && y < iy0) return InCorner(corners[1], x, y);
        if (x < ix0 && y >= iy1) return InCorner(corners[2], x, y);
        if (x >= ix1 && y >= iy1) return InCorner(corners[3], x, y);
        return true;
    };

    const float premultiplied[4] = {color.b * color.a, color.g * color.a, color.r * color.a, color.a};
    const LONG left = std::max(clip.left, static_cast<LONG>(std::floor(x0)));
    const LONG right = std::min(clip.right, static_cast<LONG>(std::ceil(x1)));
    const LONG top = std::max(clip.top, static_cast<LONG>(std::floor(y0)));
    const LONG bottom = std::min(clip.bottom, static_cast<LONG>(std::ceil(y1)));
    for (LONG y = top; y < bottom; ++y) {
        uint32_t* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride);
        const float py = static_cast<float>(y);
        const float oy = std::min(py + 1.0f, y1) - std::max(py, y0);
        for (LONG x = left; x < right; ++x) {
            const float px = static_cast<float>(x);
            float cover;
            const bool straightX = px >= ix0 && px + 1.0f <= ix1;
            const bool straightY = py >= iy0 && py + 1.0f <= iy1;
            if (straightX || straightY) {
                cover = std::min(std::min(px + 1.0f, x1) - std::max(px, x0), 1.0f) * std::min(oy, 1.0f);
            } else {
                constexpr int kSamples = 8;
                int hits = 0;
                for (int sy = 0; sy < kSamples; ++sy) {
                    for (int sx = 0; sx < kSamples; ++sx) {
                        hits += inside(px + (static_cast<float>(sx) + 0.5f) / kSamples,
                                       py + (static_cast<float>(sy) + 0.5f) / kSamples);
                    }
                }
                cover = static_cast<float>(hits) / (kSamples * kSamples);
            }
            if (cover <= 0.0f) continue;
            // 8-bit premultiplied arithmetic, rounded like Direct2D's software blending.
            const float cover8 = std::round(cover * 255.0f);
            const float alpha8 = std::round(std::round(color.a * 255.0f) * cover8 / 255.0f);
            const uint32_t dst = row[x];
            uint32_t out = 0;
            for (int c = 0; c < 4; ++c) {  // B, G, R, A
                const float source = std::round(std::round(premultiplied[c] * 255.0f) * cover8 / 255.0f);
                const float kept = std::round(static_cast<float>(Channel(dst, 8 * c)) * (255.0f - alpha8) / 255.0f);
                out |= static_cast<uint32_t>(std::clamp(source + kept, 0.0f, 255.0f)) << (8 * c);
            }
            row[x] = out;
        }
    }
}
