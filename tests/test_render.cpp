#include "test.h"

#include "card.h"
#include "format.h"
#include "i18n.h"
#include "renderer.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>

namespace {

const Gfx& SharedGfx() {
    static Gfx gfx;
    static const bool ok = gfx.Init();
    (void)ok;
    return gfx;
}

constexpr uint64_t kRamTotal = 34195574784ull;   // 31.8 GB
constexpr uint64_t kVramTotal = 12868124672ull;  // 12.0 GB

Theme MakeTheme(bool light, i18n::Lang lang = i18n::Lang::English) {
    Theme theme;
    theme.light = light;
    theme.locale = i18n::Locale(lang);
    return theme;
}

ItemView Percent(MetricId id, const wchar_t* label, double v) {
    ItemView item;
    item.id = id;
    item.label = label;
    item.strong = text::Percent(v);
    item.weak = L"%";
    item.worstStrong = L"100";
    item.worstWeak = L"%";
    item.load = static_cast<float>(v / 100.0);
    item.fraction = item.load;
    return item;
}

ItemView Memory(MetricId id, const wchar_t* label, uint64_t used, uint64_t total, const wchar_t* unit = L"GB") {
    ItemView item;
    item.id = id;
    item.label = label;
    const std::wstring totalText = text::Fixed1(text::GiB(total), L',');
    item.strong = text::Fixed1(text::GiB(used), L',');
    item.weak = L" / " + totalText + L" " + unit;
    item.worstStrong = totalText;
    item.worstWeak = item.weak;
    item.load = text::Ratio(used, total);
    item.fraction = item.load;
    return item;
}

std::vector<ItemView> Sample(double cpu, uint64_t ram, double gpu, uint64_t vram, const wchar_t* unit = L"GB") {
    return {Percent(MetricId::Cpu, L"CPU", cpu), Memory(MetricId::Ram, L"RAM", ram, kRamTotal, unit),
            Percent(MetricId::Gpu, L"GPU", gpu), Memory(MetricId::Vram, L"VRAM", vram, kVramTotal, unit)};
}

uint8_t Alpha(uint32_t px) {
    return static_cast<uint8_t>(px >> 24);
}

// Composites the (premultiplied BGRA) bitmap over a background color and saves it as PNG via WIC.
void SavePreview(const Surface& s, const std::wstring& name, uint32_t background) {
    if (t::previewDir.empty() || !s.pixels()) return;
    CreateDirectoryW(t::previewDir.c_str(), nullptr);
    const int w = s.width(), h = s.height();
    std::vector<uint32_t> out(static_cast<size_t>(w) * h);
    const float br = static_cast<float>((background >> 16) & 0xFF);
    const float bg = static_cast<float>((background >> 8) & 0xFF);
    const float bb = static_cast<float>(background & 0xFF);
    for (size_t i = 0; i < out.size(); ++i) {
        const uint32_t p = s.pixels()[i];
        const float a = static_cast<float>(p >> 24) / 255.0f;
        const auto mix = [&](uint32_t c, float bgv) {
            return static_cast<uint32_t>(std::lround(std::min(255.0f, static_cast<float>(c) + bgv * (1.0f - a))));
        };
        out[i] = 0xFF000000u | (mix((p >> 16) & 0xFF, br) << 16) | (mix((p >> 8) & 0xFF, bg) << 8) | mix(p & 0xFF, bb);
    }

    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    const std::wstring path = t::previewDir + L"\\" + name + L".png";
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, w * h * 4,
                                           reinterpret_cast<BYTE*>(out.data()), &bitmap)) ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        t::Fail(__FILE__, __LINE__, "could not save " + t::Narrow(path));
    }
}

// A card built from the real translations, with synthetic history.
CardModel DemoCard(i18n::Lang lang) {
    using i18n::Str;
    auto S = [&](Str id) { return i18n::Get(lang, id); };
    auto pct = [&](const wchar_t* v) { return text::Format(S(Str::PercentFormat), {v}); };
    auto gb = [&](const wchar_t* v) { return std::wstring(v) + L" " + S(Str::UnitGB); };

    CardModel model;
    model.title = S(Str::CardTitle);
    model.window = text::HistoryWindow(60, 1000, S(Str::CardLastSeconds), S(Str::CardLastMinutes));
    model.hintLeft = S(Str::CardHintClick);
    model.hintRight = S(Str::CardHintMenu);
    auto row = [&](MetricId id, std::wstring title, std::wstring value, std::wstring sub, std::wstring peak, float base,
                   float amp, float freq) {
        CardRow r;
        r.id = id;
        r.title = std::move(title);
        r.value = std::move(value);
        r.subtitle = std::move(sub);
        r.peak = text::Format(S(Str::CardPeak), {peak});
        for (int i = 0; i < 60; ++i) {
            const float x = static_cast<float>(i);
            r.history.push_back(
                std::clamp(base + amp * std::sin(x * freq) + 0.5f * amp * std::sin(x * freq * 3.1f), 0.0f, 1.0f));
        }
        r.load = r.history.back();
        model.rows.push_back(r);
    };
    row(MetricId::Cpu, S(Str::CardCpu), pct(L"23"),
        L"Intel Xeon E5-2699 v3 · " + text::Format(S(Str::CardThreads), {L"36"}), pct(L"61"), 0.25f, 0.18f, 0.35f);
    row(MetricId::Ram, S(Str::CardMemory), gb(L"18,2"), text::Format(S(Str::CardOfTotal), {gb(L"31,8"), pct(L"57")}),
        gb(L"19,0"), 0.57f, 0.02f, 0.1f);
    row(MetricId::Gpu, S(Str::CardGpu), pct(L"94"), L"AMD Radeon RX 6700 XT", pct(L"99"), 0.75f, 0.2f, 0.25f);
    row(MetricId::Vram, S(Str::CardGpuMemory), gb(L"2,6"), text::Format(S(Str::CardOfTotal), {gb(L"12,0"), pct(L"21")}),
        gb(L"3,1"), 0.22f, 0.03f, 0.2f);
    return model;
}

}  // namespace

TEST(Layout_width_never_depends_on_the_values) {
    // Zero jitter: the width depends only on the widest possible text, never on the current one.
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        for (float thicknessDip : {30.0f, 40.0f, 48.0f}) {
            Renderer r(SharedGfx());
            const LayoutInput in{dpi, false, static_cast<int>(std::lround(thicknessDip * dpi / 96.0f))};
            const SIZE first = r.Layout(in, Sample(0, 0, 0, 0));
            CHECK(first.cx > 0);
            CHECK_EQ(first.cy, in.thicknessPx);
            for (int v = 0; v <= 100; ++v) {
                const auto items = Sample(v, kRamTotal / 100 * v, 100 - v, kVramTotal / 100 * v);
                const SIZE s = r.Layout(in, items);
                CHECK_EQ(s.cx, first.cx);
                float excess = 0;
                const int bad = r.FirstOverflow(items, &excess);
                if (bad >= 0) {
                    t::Fail(__FILE__, __LINE__,
                            "dpi " + std::to_string(dpi) + ": \"" + t::Narrow(items[bad].strong + items[bad].weak) +
                                "\" exceeds its column by " + std::to_string(excess) + " DIP");
                }
            }
        }
    }
}

TEST(Layout_every_memory_value_fits_its_column) {
    for (const wchar_t* unit : {L"GB", L"Go", L"ГБ"}) {
        Renderer r(SharedGfx());
        r.Layout({96, false, 40}, Sample(0, 0, 0, 0, unit));
        for (uint64_t used = 0; used <= kRamTotal; used += 107374182ull) {  // 0.1 GB steps
            CHECK(r.ValuesFit(Sample(100, used, 100, std::min<uint64_t>(used, kVramTotal), unit)));
        }
    }
}

TEST(Layout_compact_for_small_taskbar_buttons) {
    Renderer r(SharedGfx());
    r.Layout({96, false, 30}, Sample(10, 1, 10, 1));
    CHECK(r.compact());
    r.Layout({96, false, 40}, Sample(10, 1, 10, 1));
    CHECK(!r.compact());
    r.Layout({144, false, 45}, Sample(10, 1, 10, 1));  // 30 DIPs at 150%
    CHECK(r.compact());
}

TEST(Layout_vertical_uses_the_taskbar_width) {
    Renderer r(SharedGfx());
    const auto items = Sample(55, kRamTotal / 2, 7, kVramTotal / 3);
    const SIZE s = r.Layout({96, true, 62}, items);
    CHECK_EQ(s.cx, 62);
    CHECK(s.cy > 4 * 30);
    CHECK(r.ValuesFit(items));
}

TEST(Layout_handles_a_single_item) {
    Renderer r(SharedGfx());
    const std::vector<ItemView> one = {Percent(MetricId::Cpu, L"CPU", 12)};
    const SIZE s = r.Layout({96, false, 40}, one);
    CHECK(s.cx > 30 && s.cx < 80);
    CHECK(r.Paint(one, false));
}

TEST(Paint_covers_the_whole_window_so_it_receives_the_mouse) {
    Renderer r(SharedGfx());
    const auto items = Sample(37, kRamTotal / 2, 81, kVramTotal / 5);
    r.Layout({96, false, 40}, items);
    CHECK(r.Paint(items, false));
    const Surface& s = r.surface();
    bool opaqueSomewhere = false;
    for (int i = 0; i < s.width() * s.height(); ++i) {
        const uint8_t a = Alpha(s.pixels()[i]);
        if (a == 0) {
            t::Fail(__FILE__, __LINE__, "pixel with alpha 0: clicks would fall through the window");
            break;
        }
        opaqueSomewhere |= a > 200;
    }
    CHECK(opaqueSomewhere);  // text and bars were drawn
}

TEST(Paint_is_skipped_only_when_nothing_visible_changed) {
    Renderer r(SharedGfx());
    r.SetTheme(MakeTheme(false));
    auto items = Sample(40, kRamTotal / 2, 10, kVramTotal / 4);
    r.Layout({96, false, 40}, items);
    CHECK(r.NeedsPaint(items, false));
    CHECK(r.Paint(items, false));
    CHECK(!r.NeedsPaint(items, false));  // identical frame

    CHECK(r.NeedsPaint(items, true));  // hover

    auto tiny = items;
    tiny[0].fraction += 0.0005f;  // far below half a pixel
    CHECK(!r.NeedsPaint(tiny, false));

    auto moved = items;
    moved[0].fraction += 0.10f;
    CHECK(r.NeedsPaint(moved, false));

    auto text = items;
    text[0].strong = L"41";
    CHECK(r.NeedsPaint(text, false));

    auto hot = items;
    hot[1].load = 0.85f;  // crosses the amber threshold
    CHECK(r.NeedsPaint(hot, false));

    r.SetTheme(MakeTheme(true));
    CHECK(r.NeedsPaint(items, false));  // theme change
}

TEST(High_contrast_paints_with_system_colors) {
    Renderer r(SharedGfx());
    Theme theme;
    theme.highContrast = true;
    theme.hcText = RGB(255, 255, 0);
    theme.hcHighlight = RGB(0, 255, 0);
    r.SetTheme(theme);
    const auto items = Sample(50, kRamTotal / 2, 50, kVramTotal / 2);
    r.Layout({96, false, 40}, items);
    CHECK(r.Paint(items, false));
    // Some pixel must be pure highlight green (the bars).
    bool green = false;
    const Surface& s = r.surface();
    for (int i = 0; i < s.width() * s.height() && !green; ++i) green = s.pixels()[i] == 0xFF00FF00u;
    CHECK(green);
}

TEST(Card_has_a_transparent_shadow_and_an_opaque_body) {
    Card card(SharedGfx());
    card.SetTheme(MakeTheme(false));
    const CardModel model = DemoCard(i18n::Lang::English);
    const SIZE size = card.Measure(96, model);
    CHECK(size.cx > 300 && size.cy > 300);
    CHECK(card.Paint(model));
    const Surface& s = card.surface();
    CHECK_EQ(Alpha(s.pixels()[0]), static_cast<uint8_t>(0));                                        // corner
    CHECK_EQ(Alpha(s.pixels()[(size.cy / 2) * size.cx + size.cx / 2]), static_cast<uint8_t>(255));  // center
    CHECK(card.ShadowMarginPx() > 0);
}

TEST(Card_scales_with_dpi) {
    Card card(SharedGfx());
    const CardModel model = DemoCard(i18n::Lang::English);
    const SIZE a = card.Measure(96, model);
    const SIZE b = card.Measure(192, model);
    CHECK_NEAR(b.cx, a.cx * 2, 2);
    CHECK_NEAR(b.cy, a.cy * 2, 2);
}

TEST(Card_grows_for_long_translations) {
    Card card(SharedGfx());
    CardModel model = DemoCard(i18n::Lang::English);
    card.Measure(96, model);
    const float normal = card.WidthDip();
    model.hintLeft = L"A very long hint text that would never fit in the default width";
    card.Measure(96, model);
    CHECK(card.WidthDip() > normal);
    CHECK(card.WidthDip() <= 460.0f);  // but never absurdly wide
}

TEST(Card_renders_in_every_language) {
    for (int l = 0; l < i18n::kLanguageCount; ++l) {
        const auto lang = static_cast<i18n::Lang>(l);
        Card card(SharedGfx());
        card.SetTheme(MakeTheme(false, lang));
        const CardModel model = DemoCard(lang);
        card.Measure(96, model);
        CHECK(card.Paint(model));
        SavePreview(card.surface(), std::wstring(L"card-lang-") + i18n::Code(lang), 0x2B2F36);
    }
}

TEST(Previews_bar_and_card) {
    if (t::previewDir.empty()) return;
    struct Variant {
        const wchar_t* name;
        UINT dpi;
        bool vertical;
        int thicknessDip;
        bool light;
        bool hover;
    };
    const Variant variants[] = {
        {L"bar-dark-100", 96, false, 40, false, false},  {L"bar-dark-hover", 96, false, 40, false, true},
        {L"bar-light-100", 96, false, 40, true, false},  {L"bar-dark-150", 144, false, 40, false, false},
        {L"bar-dark-200", 192, false, 40, false, false}, {L"bar-small-100", 96, false, 30, false, false},
        {L"bar-win11-48", 96, false, 48, false, false},  {L"bar-vertical", 96, true, 62, false, false},
    };
    const auto items = Sample(23, 19'540'000'000ull, 94, 2'800'000'000ull);
    for (const auto& v : variants) {
        Renderer r(SharedGfx());
        r.SetTheme(MakeTheme(v.light));
        r.Layout({v.dpi, v.vertical, static_cast<int>(v.thicknessDip * v.dpi / 96)}, items);
        CHECK(r.Paint(items, v.hover));
        SavePreview(r.surface(), v.name, v.light ? 0xE8E8EC : 0x101010);
    }
    for (bool light : {false, true}) {
        for (UINT dpi : {96u, 144u, 192u}) {
            Card card(SharedGfx());
            card.SetTheme(MakeTheme(light));
            const CardModel model = DemoCard(i18n::Lang::English);
            card.Measure(dpi, model);
            CHECK(card.Paint(model));
            SavePreview(card.surface(),
                        std::wstring(L"card-") + (light ? L"light-" : L"dark-") + std::to_wstring(dpi * 100 / 96),
                        light ? 0xD9DDE3 : 0x2B2F36);
        }
    }
}
