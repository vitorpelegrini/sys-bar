#pragma once

#include "common.h"

#include <d2d1.h>
#include <dwrite_1.h>

#include <cstdint>
#include <string>

enum class MetricId { Cpu = 0, Ram, Gpu, Vram, Disk };
constexpr int kMetricCount = 5;

// Load levels at which a metric turns amber and red.
constexpr float kWarnLoad = 0.80f;
constexpr float kCriticalLoad = 0.92f;

struct Theme {
    bool light = false;         // light taskbar (dark text)
    bool colorByLoad = true;    // amber/red at high load
    bool highContrast = false;  // Windows high-contrast theme: use the system colors only
    COLORREF hcText = RGB(255, 255, 255);
    COLORREF hcBackground = RGB(0, 0, 0);
    COLORREF hcHighlight = RGB(26, 235, 255);
    std::wstring locale = L"en-us";  // DirectWrite locale (shaping and font fallback)
};

// Direct2D/DirectWrite factories shared by every visual element.
class Gfx {
public:
    bool Init();
    ID2D1Factory* d2d() const { return d2d_.Get(); }
    IDWriteFactory* dwrite() const { return dwrite_.Get(); }
    const std::wstring& family() const { return family_; }

    // Tabular figures (OpenType "tnum"): every digit has the same advance width, so numbers do not
    // shift around as they change.
    void UseTabularFigures(IDWriteTextLayout* layout, UINT32 length) const;

private:
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTypography> tabular_;
    std::wstring family_ = L"Segoe UI";
};

// 32 bpp premultiplied BGRA bitmap + memory DC, presented to a layered window.
class Surface {
public:
    Surface() = default;
    ~Surface() { Release(); }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    bool Ensure(int width, int height);
    void Release();
    bool Present(HWND hwnd, const POINT* screenPosition, BYTE alpha) const;

    HDC dc() const { return dc_; }
    const uint32_t* pixels() const { return static_cast<const uint32_t*>(bits_); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.0f);
D2D1_COLOR_F FromColorRef(COLORREF color, float alpha = 1.0f);
// Foreground "ink" (white on dark, black on light, system text color in high contrast) with opacity.
D2D1_COLOR_F Ink(const Theme& theme, float alpha);
// Metric accent color, switching to amber/red at high load when enabled.
D2D1_COLOR_F MetricColor(MetricId id, float load, const Theme& theme);

// Software render target bound to a DC (grayscale text antialiasing, alpha-compatible).
ComPtr<ID2D1DCRenderTarget> CreateDcTarget(ID2D1Factory* factory);

float TextBaseline(IDWriteTextLayout* layout);
float TextWidth(IDWriteTextLayout* layout);
