#include "test.h"

#include "icons.h"

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

ItemView WithBadge(ItemView item, int celsius, int level = 0) {
    item.badge = std::to_wstring(celsius) + L"°";
    item.worstBadge = L"100°";
    item.badgeLevel = level;
    return item;
}

std::vector<ItemView> Sample(double cpu, uint64_t ram, double gpu, uint64_t vram, const wchar_t* unit = L"GB",
                             const wchar_t* diskLabel = L"DISK") {
    const int temperature = static_cast<int>(30 + gpu * 0.7);  // 30..100 °C as the load grows
    return {WithBadge(Percent(MetricId::Cpu, L"CPU", cpu), temperature),
            Memory(MetricId::Ram, L"RAM", ram, kRamTotal, unit),
            WithBadge(Percent(MetricId::Gpu, L"GPU", gpu), temperature, temperature >= 95 ? 2 : temperature >= 85),
            Memory(MetricId::Vram, L"VRAM", vram, kVramTotal, unit),
            WithBadge(Percent(MetricId::Disk, diskLabel, cpu / 2), temperature / 2)};
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
    model.footerLeft = text::Chip(icon::kInfo, L"sys-bar 1.0.0");
    model.footerRight = text::Chip(icon::kUpdate, text::Format(S(Str::CardUpdateAvailable), {L"1.1.0"}));
    model.footerAccent = true;
    auto row = [&](MetricId id, std::wstring title, std::wstring value, std::wstring sub, std::wstring peak, float base,
                   float amp, float freq) {
        CardRow r;
        r.id = id;
        r.title = std::move(title);
        r.value = std::move(value);
        r.subtitle = std::move(sub);
        r.peak = text::Chip(icon::kPeak, text::Format(S(Str::CardPeak), {peak}));
        for (int i = 0; i < 60; ++i) {
            const float x = static_cast<float>(i);
            r.history.push_back(
                std::clamp(base + amp * std::sin(x * freq) + 0.5f * amp * std::sin(x * freq * 3.1f), 0.0f, 1.0f));
        }
        r.load = r.history.back();
        model.rows.push_back(r);
    };
    row(MetricId::Cpu, S(Str::CardCpu), pct(L"23"),
        text::JoinChips({text::Chip(icon::kChip, L"Intel Xeon E5-2699 v3"),
                         text::Chip(icon::kThreads, text::Format(S(Str::CardThreads), {L"36"}))}),
        pct(L"61"), 0.25f, 0.18f, 0.35f);
    row(MetricId::Ram, S(Str::CardMemory), gb(L"18,2"),
        text::IconParts(text::Format(S(Str::CardOfTotal), {gb(L"31,8"), pct(L"57")}), {icon::kMemory, icon::kUsage}),
        gb(L"19,0"), 0.57f, 0.02f, 0.1f);
    row(MetricId::Gpu, S(Str::CardGpu), pct(L"94"), text::Chip(icon::kDisplay, L"AMD Radeon RX 6700 XT"), pct(L"99"),
        0.75f, 0.2f, 0.25f);
    row(MetricId::Vram, S(Str::CardGpuMemory), gb(L"2,6"),
        text::IconParts(text::Format(S(Str::CardOfTotal), {gb(L"12,0"), pct(L"21")}), {icon::kMemory, icon::kUsage}),
        gb(L"3,1"), 0.22f, 0.03f, 0.2f);
    const std::wstring mbps = S(Str::UnitMBps);
    row(MetricId::Net, S(Str::CardNetwork), L"4,6 " + mbps,
        text::JoinChips({text::IconParts(text::Format(S(Str::CardNetIo),
                                                      {L"4,2 " + mbps, L"412 " + std::wstring(S(Str::UnitKBps))}),
                                         {icon::kDown, icon::kUp}),
                         text::Chip(icon::kEthernet, L"Ethernet · 1 Gbps")}),
        L"38 " + mbps, 0.3f, 0.25f, 0.45f);
    model.rows.back().neutral = true;
    row(MetricId::Disk, S(Str::CardDisk), pct(L"7"),
        text::IconParts(text::Format(S(Str::CardDiskIo), {L"12,4 " + mbps, L"0,8 " + mbps}), {icon::kDown, icon::kUp}),
        pct(L"64"), 0.05f, 0.08f, 0.6f);
    model.sensorsTitle = S(Str::CardSensors);
    const auto rpm = [&](const wchar_t* v) { return text::Format(S(Str::CardRpm), {v}); };
    model.sensors = {
        {L"CPU", L"59 °C", rpm(L"2591"), 0.49f, 0, MetricId::Cpu, false},
        {L"GPU", L"88 °C", rpm(L"1460"), 0.85f, 1, MetricId::Gpu, false},
        {L"NVMe C:", L"41 °C", L"Samsung SSD 980 PRO 1TB", 0.35f, 0, MetricId::Disk, false},
        {L"SSD D:", L"72 °C", L"TGT-SLST-120", 0.87f, 2, MetricId::Disk, false},
    };
    model.sensors[0].detail = rpm(L"2591") + L" · 45%";
    model.sensors[0].fanDetail = true;
    model.sensors[1].detail = rpm(L"1460") + L" · 38%";
    model.sensors[1].fanDetail = true;
    model.fans = {{S(Str::CardFanCase), rpm(L"1842"), L"62%"},
                  {S(Str::CardFanPump), rpm(L"2400"), L"80%"},
                  {S(Str::CardFanCase), S(Str::CardFanStopped), L""}};
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

TEST(Animation_frames_redraw_only_the_bars_and_match_a_full_paint) {
    // An animation frame redraws just the bars that moved; the pixels must be exactly those of a full paint.
    ItemView net;
    net.id = MetricId::Net;
    net.label = L"NET";
    net.strong = L"↓ 4,2";  // the comma descends towards the bar
    net.weak = L" MB/s";
    net.worstStrong = L"↓ 999";
    net.worstWeak = L" MB/s";
    net.badge = L"↑ 412 KB/s";
    net.worstBadge = L"↑ 999 MB/s";
    net.fraction = 0.04f;
    auto start = Sample(23, kRamTotal / 2, 94, kVramTotal / 3);
    start[0] = WithBadge(start[0], 47);
    start.push_back(net);
    start.push_back(WithBadge(Percent(MetricId::Disk, L"SSD D:", 0), 33));
    start.back().fraction = 0.0f;

    // Frames of an animation: fills move (one grows into the warning color, one appears as a dot, one
    // disappears), the text stays.
    auto frame = [&](float t) {
        auto items = start;
        items[0].fraction = 0.23f + 0.6f * t;
        items[0].load = items[0].fraction;  // crosses the amber and red thresholds
        items[1].fraction = 0.5f - 0.3f * t;
        items[2].fraction = 0.94f * (1.0f - t);
        items[4].fraction = 0.04f + 0.5f * t;
        items[5].fraction = t < 0.5f ? 0.0005f : 0.0f;
        return items;
    };

    struct Variant {
        UINT dpi;
        float thicknessDip;
        bool vertical, hover, light;
    };
    int partial = 0, compared = 0;
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        for (const Variant v : {Variant{dpi, 40, false, false, false}, Variant{dpi, 48, false, true, true},
                                Variant{dpi, 30, false, false, true}, Variant{dpi, 62, true, true, false}}) {
            const LayoutInput in{v.dpi, v.vertical, static_cast<int>(std::lround(v.thicknessDip * v.dpi / 96.0f))};
            Renderer animated(SharedGfx());
            animated.SetTheme(MakeTheme(v.light));
            animated.Layout(in, start);
            CHECK(animated.Paint(start, v.hover));
            for (float t : {0.1f, 0.35f, 0.6f, 1.0f}) {
                const auto items = frame(t);
                animated.Layout(in, items);
                CHECK(animated.Paint(items, v.hover));
                partial += animated.LastPaintWasPartial() ? 1 : 0;

                Renderer fresh(SharedGfx());
                fresh.SetTheme(MakeTheme(v.light));
                fresh.Layout(in, items);
                CHECK(fresh.Paint(items, v.hover));
                const Surface& a = animated.surface();
                const Surface& b = fresh.surface();
                CHECK_EQ(a.width(), b.width());
                CHECK_EQ(a.height(), b.height());
                if (a.width() != b.width() || a.height() != b.height()) continue;
                ++compared;
                for (int i = 0; i < a.width() * a.height(); ++i) {
                    if (a.pixels()[i] != b.pixels()[i]) {
                        t::Fail(__FILE__, __LINE__,
                                "dpi " + std::to_string(v.dpi) + " thickness " + std::to_string(v.thicknessDip) +
                                    ": pixel " + std::to_string(i % a.width()) + "," + std::to_string(i / a.width()) +
                                    " differs from a full paint");
                        break;
                    }
                }
            }
        }
    }
    CHECK(compared > 0);
    CHECK(partial >= compared * 3 / 4);  // the cheap path is the normal one
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
    model.rows.resize(1);  // a narrow card, so the footer decides the width
    model.sensors.clear();
    card.Measure(96, model);
    const float normal = card.WidthDip();
    card.ForgetWidth();
    model.footerLeft = L"A very long hint text that would never fit in the default width";
    card.Measure(96, model);
    CHECK(card.WidthDip() > normal);
    CHECK(card.WidthDip() <= 540.0f);  // but never absurdly wide (beyond that, text wraps)
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
    // Showcase: network, then one column per drive ("NVMe C:", "SSD D:"), as on a typical desktop.
    auto items = Sample(23, 19'540'000'000ull, 94, 2'800'000'000ull);
    items.pop_back();
    items[0] = WithBadge(Percent(MetricId::Cpu, L"CPU", 23), 47);
    ItemView net;
    net.id = MetricId::Net;
    net.label = L"NET";
    net.strong = L"\u2193 4,2";
    net.weak = L" MB/s";
    net.worstStrong = L"\u2193 999";
    net.worstWeak = L" MB/s";
    net.badge = L"\u2191 412 KB/s";
    net.worstBadge = L"\u2191 999 MB/s";
    net.fraction = 0.04f;
    items.push_back(net);
    items.push_back(WithBadge(Percent(MetricId::Disk, L"NVMe C:", 12), 41));
    items.push_back(WithBadge(Percent(MetricId::Disk, L"SSD D:", 3), 33));
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

TEST(Layout_makes_room_when_a_badge_appears_later) {
    // Regression: a disk temperature only becomes known once the drive is busy. The column was laid out
    // without the badge and never widened, so "40°" was drawn over the "DISK" label.
    Renderer r(SharedGfx());
    ItemView disk = Percent(MetricId::Disk, L"DISCO", 0);
    r.Layout({96, false, 40}, {disk});
    disk = WithBadge(disk, 40);
    r.Layout({96, false, 40}, {disk});
    float excess = 0;
    CHECK_EQ(r.FirstOverflow({disk}, &excess), -1);
}
TEST(Network_and_battery_columns_fit) {
    ItemView net;
    net.id = MetricId::Net;
    net.label = L"NET";
    net.strong = L"\u2193 999";
    net.weak = L" MB/s";
    net.worstStrong = net.strong;
    net.worstWeak = net.weak;
    net.badge = L"\u2191 999 MB/s";
    net.worstBadge = net.badge;
    net.fraction = 0.4f;
    ItemView bat = Percent(MetricId::Battery, L"BAT", 100);
    bat.badge = L"\u221299 W";
    bat.worstBadge = bat.badge;
    const std::vector<ItemView> items = {Percent(MetricId::Cpu, L"CPU", 23), net, bat};
    for (UINT dpi : {96u, 144u}) {
        Renderer r(SharedGfx());
        r.Layout({dpi, false, static_cast<int>(40 * dpi / 96)}, items);
        CHECK_EQ(r.FirstOverflow(items), -1);
        CHECK(r.Paint(items, false));
        SavePreview(r.surface(), L"bar-net-battery-" + std::to_wstring(dpi * 100 / 96), 0x101010);
    }
}

TEST(Layout_per_disk_columns_and_fahrenheit_badges_fit) {
    // One column per drive ("NVMe C:", "SSD D:") with Fahrenheit badges, the widest strip we produce.
    auto fahrenheit = [](ItemView item, double celsius) {
        item.badge = text::TemperatureBadge(celsius, true);
        item.worstBadge = text::TemperatureBadge(100.0, true);
        return item;
    };
    std::vector<ItemView> items = {
        fahrenheit(Percent(MetricId::Cpu, L"CPU", 100), 99.6),
        Memory(MetricId::Ram, L"RAM", 31'000'000'000ull, kRamTotal),
        fahrenheit(Percent(MetricId::Disk, L"NVMe C:", 100), 99.0),
        fahrenheit(Percent(MetricId::Disk, L"SSD D:", 100), 99.0),
    };
    for (UINT dpi : {96u, 144u}) {
        Renderer r(SharedGfx());
        r.Layout({dpi, false, static_cast<int>(40 * dpi / 96)}, items);
        float excess = 0;
        CHECK_EQ(r.FirstOverflow(items, &excess), -1);
        CHECK(r.Paint(items, false));
        SavePreview(r.surface(), L"bar-disks-fahrenheit-" + std::to_wstring(dpi * 100 / 96), 0x101010);
    }
}

TEST(Card_never_cuts_text_it_widens_then_wraps) {
    // Regression: "TGT-SLST-120 · Leitura 0,0 MB/s · Gravação 0,..." was cut with an ellipsis.
    using i18n::Str;
    const auto lang = i18n::Lang::Portuguese;
    CardModel model = DemoCard(lang);
    const std::wstring mbps = i18n::Get(lang, Str::UnitMBps);
    model.rows.back().title = L"SSD (D:)";
    model.rows.back().subtitle =
        L"TGT-SLST-120 · " + text::Format(i18n::Get(lang, Str::CardDiskIo), {L"0,0 " + mbps, L"0,0 " + mbps});
    Card card(SharedGfx());
    card.SetTheme(MakeTheme(false));
    const SIZE fits = card.Measure(96, model);
    const float widthWhenItFits = card.WidthDip();

    // Something too long for any width: the card stops at its maximum width and grows taller instead.
    model.rows.back().subtitle =
        model.rows.back().subtitle + L" · " + model.rows.back().subtitle + L" · " + model.rows.back().subtitle;
    model.sensors[2].detail = L"Samsung SSD 990 PRO with Heatsink 4TB NVMe M.2 PCIe 4.0 (firmware 4B2QJXD7)";
    const SIZE wraps = card.Measure(96, model);
    CHECK(card.WidthDip() >= widthWhenItFits);
    CHECK(wraps.cy > fits.cy);
    CHECK(card.Paint(model));
    SavePreview(card.surface(), L"card-long-texts", 0x2B2F36);
}

TEST(Card_shows_a_laptop_battery) {
    using i18n::Str;
    for (const auto lang : {i18n::Lang::Portuguese, i18n::Lang::English}) {
        CardModel model = DemoCard(lang);
        model.rows.resize(3);  // CPU, memory, GPU: a laptop without dedicated video memory
        model.rows.back().subtitle = text::Chip(icon::kDisplay, L"NVIDIA GeForce RTX 4060 Laptop GPU");
        model.rows[0].subtitle =
            text::JoinChips({text::Chip(icon::kChip, L"Intel Core i7-13700H"),
                             text::Chip(icon::kThreads, text::Format(i18n::Get(lang, Str::CardThreads), {L"20"}))});
        CardRow battery;
        battery.id = MetricId::Battery;
        battery.title = i18n::Get(lang, Str::CardBattery);
        battery.value = L"18%";
        battery.subtitle = text::JoinChips({text::Chip(icon::kBattery, i18n::Get(lang, Str::CardBatteryOnBattery)),
                                            text::Chip(icon::kPower, text::Watts(-9.2, L',')),
                                            text::Chip(icon::kClock, text::Format(i18n::Get(lang, Str::CardBatteryLeft),
                                                                                  {text::HoursMinutes(3000)}))});
        battery.load = kWarnLoad;
        battery.neutral = true;
        for (int i = 0; i < 60; ++i) battery.history.push_back(0.48f - 0.005f * static_cast<float>(i));
        model.rows.push_back(battery);
        model.sensors.resize(2);
        model.fans.clear();
        Card card(SharedGfx());
        card.SetTheme(MakeTheme(false));
        card.Measure(144, model);
        CHECK(card.Paint(model));
        SavePreview(card.surface(), lang == i18n::Lang::English ? L"card-battery-en" : L"card-battery", 0x2B2F36);
    }
}

TEST(Card_fits_short_screens_with_many_disks) {
    // Worst case: 4 drives (side by side, two per line) plus every sensor, on a 1080p screen at 150% and a
    // 1366x768 laptop. The visible card must never be taller than the work area.
    using i18n::Str;
    CardModel model = DemoCard(i18n::Lang::English);
    model.rows.pop_back();  // the combined disk row becomes one row per drive
    const wchar_t* names[] = {L"NVMe (C:)", L"SSD (D:)", L"HDD (E: F:)", L"USB (G:)"};
    for (const wchar_t* name : names) {
        CardRow r;
        r.id = MetricId::Disk;
        r.title = name;
        r.value = L"7%";
        r.subtitle = L"Samsung SSD 980 PRO 1TB · Read 12,4 MB/s · Write 0,8 MB/s";
        r.peak = L"max. 64%";
        r.half = true;
        for (int i = 0; i < 60; ++i) r.history.push_back(0.1f + 0.05f * static_cast<float>(i % 7));
        r.load = r.history.back();
        model.rows.push_back(r);
    }
    model.sensors.push_back(model.sensors.back());
    model.sensors.push_back(model.sensors.back());

    struct Screen {
        UINT dpi;
        int workHeightPx;
    };
    for (const Screen screen : {Screen{144, 1032 - 30}, Screen{96, 728 - 20}, Screen{96, 1400}}) {
        Card card(SharedGfx());
        card.SetTheme(MakeTheme(false));
        const SIZE size = card.Measure(screen.dpi, model, screen.workHeightPx);
        const int visible = size.cy - 2 * card.ShadowMarginPx();
        CHECK(visible <= screen.workHeightPx + 1);
        CHECK(card.SparkHeightDip() >= 14.0f);
        CHECK(card.Paint(model));
        SavePreview(card.surface(), L"card-many-disks-" + std::to_wstring(screen.workHeightPx), 0x2B2F36);
    }
}

TEST(Card_size_does_not_follow_live_values) {
    // Regression: "36 KB/s" -> "128 KB/s" made the card reflow and change width every second.
    auto withRates = [](const wchar_t* read, const wchar_t* write, const wchar_t* percent) {
        CardModel model = DemoCard(i18n::Lang::Portuguese);
        CardRow& disk = model.rows.back();
        disk.half = true;
        disk.value = percent;
        disk.subtitle = text::Chip(icon::kDrive, L"GVR1TB") + L"\n" +
                        text::JoinChips({text::Chip(icon::kDown, read), text::Chip(icon::kUp, write)});
        CardRow second = disk;
        model.rows.push_back(second);
        return model;
    };
    Card a(SharedGfx()), b(SharedGfx());
    const SIZE low = a.Measure(96, withRates(L"0,0 KB/s", L"1,1 KB/s", L"0%"));
    const SIZE high = b.Measure(96, withRates(L"128 MB/s", L"999 KB/s", L"100%"));
    CHECK_EQ(low.cx, high.cx);
    CHECK_EQ(low.cy, high.cy);
}

namespace {

// Saves a premultiplied BGRA bitmap as a PNG with real (straight) alpha, shadow included.
void SaveWithAlpha(const Surface& s, const std::wstring& name) {
    if (t::previewDir.empty() || !s.pixels()) return;
    const int w = s.width(), h = s.height();
    std::vector<uint32_t> out(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.size(); ++i) {
        const uint32_t p = s.pixels()[i];
        const uint32_t a = p >> 24;
        auto channel = [&](int shift) {
            const uint32_t c = (p >> shift) & 0xFF;
            return a ? std::min<uint32_t>(255, (c * 255 + a / 2) / a) : 0u;
        };
        out[i] = (a << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
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

}  // namespace

TEST(Banner_card) {
    // The hover card of the README banner (tools/update-banner.ps1 lays it over the artwork): the real card,
    // in English, with the banner's four metrics.
    if (t::previewDir.empty()) return;
    using i18n::Str;
    const auto lang = i18n::Lang::English;
    CardModel model = DemoCard(lang);
    model.rows.resize(4);
    model.sensors.clear();
    model.fans.clear();
    model.sensorsTitle.clear();
    model.footerRight = text::Chip(icon::kCheck, i18n::Get(lang, Str::CardUpToDate));
    model.footerAccent = false;
    Card card(SharedGfx());
    card.SetTheme(MakeTheme(false));
    card.Measure(141, model);  // the size of the artwork's card
    CHECK(card.Paint(model));
    SaveWithAlpha(card.surface(), L"banner-card");
}

TEST(Bars_drawn_without_Direct2D_look_the_same) {
    // The bars are painted by our own rasterizer (cheap animation frames): it must match Direct2D, which drew
    // them before, to within one level almost everywhere (only the antialiasing of the round ends may differ).
    const Gfx& gfx = SharedGfx();
    auto target = CreateDcTarget(gfx.d2d());
    ComPtr<ID2D1SolidColorBrush> brush;
    CHECK(target && SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush)));
    if (!target || !brush) return;
    long long total = 0, close = 0;
    int worst = 0;
    const int width = 160, height = 24;
    const RECT bounds{0, 0, width, height};
    for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
        for (float barHeight : {3.0f, 2.0f}) {
            for (bool hover : {false, true}) {
                for (float fill : {0.0f, 3.0f, 3.4f, 7.25f, 20.5f, 43.0f}) {
                    const float x0 = 9.0f * scale, y0 = std::round(8.0f * scale), h = barHeight * scale;
                    const D2D1_RECT_F track = D2D1::RectF(x0, y0, x0 + 43.0f * scale, y0 + h);
                    const D2D1_RECT_F filled = D2D1::RectF(x0, y0, x0 + std::max(fill * scale, h), y0 + h);
                    const D2D1_COLOR_F trackColor = D2D1::ColorF(1, 1, 1, 0.14f),
                                       fillColor = D2D1::ColorF(0.3f, 0.76f, 1.0f);
                    Surface reference, ours;
                    CHECK(reference.Ensure(width, height) && ours.Ensure(width, height));
                    for (Surface* s : {&reference, &ours}) {
                        target->BindDC(s->dc(), &bounds);
                        target->BeginDraw();
                        target->Clear(D2D1::ColorF(0, 0, 0, 1.0f / 255.0f));
                        if (hover) {
                            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.09f));
                            target->FillRoundedRectangle(
                                D2D1::RoundedRect(D2D1::RectF(1, 2, width - 1.0f, height - 2.0f), 4, 4), brush.Get());
                        }
                        if (s == &reference) {
                            brush->SetColor(trackColor);
                            target->FillRoundedRectangle(D2D1::RoundedRect(track, h / 2, h / 2), brush.Get());
                            if (fill > 0) {
                                brush->SetColor(fillColor);
                                target->FillRoundedRectangle(D2D1::RoundedRect(filled, h / 2, h / 2), brush.Get());
                            }
                        }
                        CHECK(SUCCEEDED(target->EndDraw()));
                    }
                    GdiFlush();
                    FillRoundedRect(ours.bits(), width, bounds, track, h / 2, trackColor);
                    if (fill > 0) FillRoundedRect(ours.bits(), width, bounds, filled, h / 2, fillColor);
                    for (int i = 0; i < width * height; ++i) {
                        const uint32_t p = reference.pixels()[i], q = ours.pixels()[i];
                        int d = 0;
                        for (int c = 0; c < 32; c += 8)
                            d = std::max(d, std::abs(int((p >> c) & 255) - int((q >> c) & 255)));
                        ++total;
                        close += d <= 1;
                        worst = std::max(worst, d);
                    }
                }
            }
        }
    }
    std::printf("      %.3f%% of the pixels within one level of Direct2D, worst %d/255\n",
                100.0 * static_cast<double>(close) / static_cast<double>(total), worst);
    CHECK(close * 1000 >= total * 998);
    CHECK(worst <= 40);
}
