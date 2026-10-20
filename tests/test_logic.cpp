#include "test.h"

#include "logic.h"
#include "settings.h"

using namespace logic;

namespace {

std::string Show(const RECT& r) {
    return "{" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," +
           std::to_string(r.bottom) + "}";
}

void CheckRect(const char* file, int line, const RECT& actual, const RECT& expected) {
    if (actual.left != expected.left || actual.top != expected.top || actual.right != expected.right ||
        actual.bottom != expected.bottom) {
        t::Fail(file, line, Show(actual) + " vs expected " + Show(expected));
    }
}
#define CHECK_RECT(a, b) CheckRect(__FILE__, __LINE__, (a), (b))

}  // namespace

// ---- Classic taskbar ---------------------------------------------------------------------------

TEST(Classic_strip_takes_the_end_of_the_task_list) {
    ClassicInput in;
    in.rebarClient = {0, 0, 1500, 40};
    in.taskList = {2, 0, 1500, 40};
    in.strip = {250, 40};
    const auto p = PlaceClassic(in);
    CHECK_RECT(p.strip, (RECT{1250, 0, 1500, 40}));
    CHECK_RECT(p.taskList, (RECT{2, 0, 1250, 40}));
    CHECK_RECT(p.natural, (RECT{2, 0, 1500, 40}));
}

TEST(Classic_is_stable_when_already_shrunk) {
    // Running Reposition again on an already shrunk task list must give the same answer.
    ClassicInput in;
    in.rebarClient = {0, 0, 1500, 40};
    in.taskList = {2, 0, 1250, 40};
    in.strip = {250, 40};
    const auto p = PlaceClassic(in);
    CHECK_RECT(p.strip, (RECT{1250, 0, 1500, 40}));
    CHECK_RECT(p.taskList, in.taskList);
}

TEST(Classic_respects_toolbars_on_the_right) {
    // "Desktop"/"Links" toolbars live in the ReBar to the right of the task list.
    ClassicInput in;
    in.rebarClient = {0, 0, 1500, 40};
    in.taskList = {2, 0, 1380, 40};
    in.toolbars = {{1390, 0, 1500, 40}};
    in.strip = {250, 40};
    const auto p = PlaceClassic(in);
    CHECK_RECT(p.strip, (RECT{1140, 0, 1390, 40}));
    CHECK_RECT(p.natural, (RECT{2, 0, 1390, 40}));  // restore never grows over the toolbar
}

TEST(Classic_ignores_toolbars_on_another_row_or_to_the_left) {
    ClassicInput in;
    in.rebarClient = {0, 0, 1500, 80};
    in.taskList = {200, 0, 1500, 40};
    in.toolbars = {{0, 0, 190, 40}, {900, 40, 1500, 80}};  // left of the list / second row
    in.strip = {250, 40};
    const auto p = PlaceClassic(in);
    CHECK_RECT(p.strip, (RECT{1250, 0, 1500, 40}));
}

TEST(Classic_never_moves_left_of_the_task_list) {
    ClassicInput in;
    in.rebarClient = {0, 0, 300, 40};
    in.taskList = {100, 0, 300, 40};
    in.strip = {500, 40};  // wider than the room available
    const auto p = PlaceClassic(in);
    CHECK(p.strip.left >= in.taskList.left);
    CHECK(p.taskList.right >= p.taskList.left);
}

TEST(Classic_vertical_taskbar) {
    ClassicInput in;
    in.vertical = true;
    in.rebarClient = {0, 0, 62, 1000};
    in.taskList = {0, 2, 62, 1000};
    in.strip = {62, 180};
    const auto p = PlaceClassic(in);
    CHECK_RECT(p.strip, (RECT{0, 820, 62, 1000}));
    CHECK_RECT(p.taskList, (RECT{0, 2, 62, 820}));
}

// ---- Modern taskbar ----------------------------------------------------------------------------

TEST(Modern_strip_sits_left_of_the_notification_area) {
    ModernInput in;
    in.trayClient = {0, 0, 2560, 48};
    in.notify = {2200, 0, 2560, 48};
    in.strip = {260, 48};
    in.gapPx = 4;
    in.fallbackRightPx = 380;
    CHECK_RECT(PlaceModern(in), (RECT{1936, 0, 2196, 48}));
}

TEST(Modern_user_offset_moves_the_strip_left) {
    ModernInput in;
    in.trayClient = {0, 0, 2560, 48};
    in.notify = {2200, 0, 2560, 48};
    in.strip = {260, 48};
    in.offsetPx = 100;
    CHECK_RECT(PlaceModern(in), (RECT{1840, 0, 2100, 48}));
}

TEST(Modern_bogus_notify_rect_uses_the_fallback) {
    ModernInput in;
    in.trayClient = {0, 0, 2560, 48};
    in.strip = {260, 48};
    in.fallbackRightPx = 380;
    const RECT expected{2560 - 380 - 260, 0, 2560 - 380, 48};
    in.notify = {};  // missing
    CHECK_RECT(PlaceModern(in), expected);
    in.notify = {0, 0, 2560, 48};  // covers the whole bar
    CHECK_RECT(PlaceModern(in), expected);
    in.notify = {300, 0, 600, 48};  // on the left half
    CHECK_RECT(PlaceModern(in), expected);
    in.notify = {2200, 0, 2200, 48};  // zero width
    CHECK_RECT(PlaceModern(in), expected);
}

TEST(Modern_never_goes_off_the_bar) {
    ModernInput in;
    in.trayClient = {0, 0, 300, 48};
    in.strip = {260, 48};
    in.fallbackRightPx = 380;
    CHECK(PlaceModern(in).left >= 0);
}

TEST(Plausible_notify_rect) {
    const RECT tray{0, 0, 1920, 48};
    CHECK(IsPlausibleNotifyRect(tray, {1600, 0, 1920, 48}));
    CHECK(IsPlausibleNotifyRect(tray, {1600, 0, 1900, 48}));  // "show desktop" sliver on the right
    CHECK(!IsPlausibleNotifyRect(tray, {}));
    CHECK(!IsPlausibleNotifyRect(tray, {0, 0, 1920, 48}));
    CHECK(!IsPlausibleNotifyRect(tray, {980, 0, 1100, 48}));  // ends far from the right edge
}

// ---- Hover card --------------------------------------------------------------------------------

TEST(Card_above_a_bottom_taskbar_centered_on_the_strip) {
    CardInput in;
    in.strip = {1600, 1400, 1860, 1440};
    in.monitor = {0, 0, 2560, 1440};
    in.card = {388, 500};
    in.marginPx = 18;
    in.gapPx = 10;
    in.slidePx = 6;
    const auto p = PlaceCard(in);
    CHECK_EQ(p.position.x, 1730L - 194L);
    CHECK_EQ(p.position.y + in.card.cy - in.marginPx, 1400L - 10L);  // visible bottom = strip top - gap
    CHECK_EQ(p.slide.y, 6L);
    CHECK_EQ(p.slide.x, 0L);
}

TEST(Card_below_a_top_taskbar) {
    CardInput in;
    in.strip = {1600, 0, 1860, 40};
    in.monitor = {0, 0, 2560, 1440};
    in.card = {388, 500};
    in.marginPx = 18;
    in.gapPx = 10;
    in.slidePx = 6;
    const auto p = PlaceCard(in);
    CHECK_EQ(p.position.y + in.marginPx, 40L + 10L);
    CHECK_EQ(p.slide.y, -6L);
}

TEST(Card_stays_inside_the_monitor) {
    CardInput in;
    in.strip = {2400, 1400, 2560, 1440};  // strip at the far right
    in.monitor = {0, 0, 2560, 1440};
    in.card = {388, 500};
    in.marginPx = 18;
    in.gapPx = 10;
    const auto p = PlaceCard(in);
    CHECK(p.position.x + in.card.cx - in.marginPx <= 2560 - 10);
}

TEST(Card_on_a_negative_coordinate_monitor) {
    CardInput in;
    in.strip = {-1800, 1040, -1500, 1080};
    in.monitor = {-1920, 0, 0, 1080};
    in.card = {388, 500};
    in.marginPx = 18;
    in.gapPx = 10;
    const auto p = PlaceCard(in);
    CHECK(p.position.x + in.marginPx >= -1920 + 10);
    CHECK(p.position.x + in.card.cx - in.marginPx <= 0 - 10);
}

TEST(Card_beside_a_vertical_taskbar) {
    CardInput in;
    in.strip = {0, 800, 62, 980};
    in.monitor = {0, 0, 1920, 1080};
    in.card = {388, 500};
    in.marginPx = 18;
    in.gapPx = 10;
    in.slidePx = 6;
    in.verticalTaskbar = true;
    const auto p = PlaceCard(in);
    CHECK_EQ(p.position.x + in.marginPx, 62L + 10L);  // to the right of a left taskbar
    CHECK_EQ(p.slide.x, -6L);
}

// ---- Visible metrics ---------------------------------------------------------------------------

TEST(Visible_metrics_follow_settings_and_availability) {
    Settings s;
    CHECK_EQ(VisibleMetrics(s, {true, true, true}).size(), size_t{5});
    CHECK_EQ(VisibleMetrics(s, {false, false, true}).size(), size_t{3});  // no GPU counters
    CHECK_EQ(VisibleMetrics(s, {true, false, true}).size(), size_t{4});   // integrated GPU: no VRAM
    CHECK_EQ(VisibleMetrics(s, {true, true, false}).size(), size_t{4});   // no disk counters
    s.showDisk = false;
    CHECK_EQ(VisibleMetrics(s, {true, true, true}).size(), size_t{4});
}

TEST(Strip_is_never_empty) {
    Settings s;
    s.showCpu = s.showRam = s.showDisk = false;                    // only GPU metrics enabled...
    const auto visible = VisibleMetrics(s, {false, false, true});  // ...but there is no GPU
    CHECK_EQ(visible.size(), size_t{1});
    CHECK(visible[0] == MetricId::Cpu);
}

TEST(Cannot_hide_the_last_available_metric) {
    Settings s;
    s.showRam = s.showDisk = false;
    // GPU unavailable: CPU is the last real metric even though GPU/VRAM are still "enabled".
    CHECK(!CanHide(s, MetricId::Cpu, {false, false, false}));
    CHECK(CanHide(s, MetricId::Cpu, {true, true, false}));
    s.showGpu = s.showVram = false;
    CHECK(!CanHide(s, MetricId::Cpu, {true, true, true}));
    CHECK(CanHide(s, MetricId::Ram, {true, true, true}));  // hiding something already hidden is harmless
    s.showDisk = true;
    CHECK(CanHide(s, MetricId::Cpu, {false, false, true}));  // the disk remains
}

TEST(Temperature_levels) {
    CHECK_EQ(TemperatureLevel(MetricId::Cpu, 60.0), 0);
    CHECK_EQ(TemperatureLevel(MetricId::Cpu, 85.0), 1);
    CHECK_EQ(TemperatureLevel(MetricId::Gpu, 94.9), 1);
    CHECK_EQ(TemperatureLevel(MetricId::Gpu, 95.0), 2);
    CHECK_EQ(TemperatureLevel(MetricId::Disk, 45.0), 0);  // drives run cooler...
    CHECK_EQ(TemperatureLevel(MetricId::Disk, 60.0), 1);  // ...and get worried much earlier
    CHECK_EQ(TemperatureLevel(MetricId::Disk, 71.0), 2);
}