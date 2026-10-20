#include "logic.h"

#include "settings.h"

#include <algorithm>

namespace logic {
namespace {

int Width(const RECT& r) {
    return r.right - r.left;
}
int Height(const RECT& r) {
    return r.bottom - r.top;
}
bool Empty(const RECT& r) {
    return Width(r) <= 0 || Height(r) <= 0;
}

}  // namespace

ClassicPlacement PlaceClassic(const ClassicInput& in) {
    ClassicPlacement out;
    const RECT& list = in.taskList;

    if (!in.vertical) {
        // The task list may grow up to the first toolbar on its right (same row), or the ReBar edge.
        int limit = in.rebarClient.right;
        for (const RECT& t : in.toolbars) {
            const bool sameRow = t.top < list.bottom && t.bottom > list.top;
            if (!Empty(t) && sameRow && t.left > list.left) limit = std::min(limit, static_cast<int>(t.left));
        }
        limit = std::max(limit, static_cast<int>(list.left));
        const int stripLeft = std::max<int>(limit - in.strip.cx, list.left);
        out.natural = {list.left, list.top, limit, list.bottom};
        out.taskList = {list.left, list.top, stripLeft, list.bottom};
        out.strip = {stripLeft, list.top, limit, list.bottom};
    } else {
        int limit = in.rebarClient.bottom;
        for (const RECT& t : in.toolbars) {
            const bool sameColumn = t.left < list.right && t.right > list.left;
            if (!Empty(t) && sameColumn && t.top > list.top) limit = std::min(limit, static_cast<int>(t.top));
        }
        limit = std::max(limit, static_cast<int>(list.top));
        const int stripTop = std::max<int>(limit - in.strip.cy, list.top);
        out.natural = {list.left, list.top, list.right, limit};
        out.taskList = {list.left, list.top, list.right, stripTop};
        out.strip = {list.left, stripTop, list.right, limit};
    }
    return out;
}

bool IsPlausibleNotifyRect(const RECT& trayClient, const RECT& notify) {
    const int trayWidth = Width(trayClient);
    if (Empty(notify) || trayWidth <= 0) return false;
    // It must sit in the right half of the bar, end near the right edge and not be absurdly wide.
    return notify.left >= trayClient.left + trayWidth / 2 && notify.right <= trayClient.right + 1 &&
           notify.right >= trayClient.right - trayWidth / 4 && Width(notify) < trayWidth / 2;
}

RECT PlaceModern(const ModernInput& in) {
    int right = in.trayClient.right - in.fallbackRightPx;
    if (IsPlausibleNotifyRect(in.trayClient, in.notify)) right = in.notify.left;
    right -= in.gapPx + in.offsetPx;
    const int left = std::max<int>(right - in.strip.cx, in.trayClient.left);
    return {left, in.trayClient.top, left + in.strip.cx, in.trayClient.bottom};
}

CardPlacement PlaceCard(const CardInput& in) {
    CardPlacement out;
    const RECT& b = in.strip;
    const RECT& m = in.monitor;
    const int w = in.card.cx, h = in.card.cy, margin = in.marginPx, gap = in.gapPx;

    // The visible card is the window minus the shadow margin.
    int x, y;
    if (!in.verticalTaskbar) {
        x = (b.left + b.right) / 2 - w / 2;
        const bool bottom = b.top > (m.top + m.bottom) / 2;
        y = bottom ? b.top - gap - h + margin : b.bottom + gap - margin;
        out.slide.y = bottom ? in.slidePx : -in.slidePx;
    } else {
        y = (b.top + b.bottom) / 2 - h / 2;
        const bool rightSide = b.left > (m.left + m.right) / 2;
        x = rightSide ? b.left - gap - w + margin : b.right + gap - margin;
        out.slide.x = rightSide ? in.slidePx : -in.slidePx;
    }
    // Keep the visible card inside the monitor (coordinates may be negative on multi-monitor setups).
    x = std::clamp<int>(x, m.left + gap - margin, std::max<int>(m.left + gap - margin, m.right - w - gap + margin));
    y = std::clamp<int>(y, m.top + gap - margin, std::max<int>(m.top + gap - margin, m.bottom - h - gap + margin));
    out.position = {x, y};
    return out;
}

std::vector<MetricId> VisibleMetrics(const Settings& s, bool gpuAvailable, bool vramAvailable) {
    std::vector<MetricId> out;
    if (s.showCpu) out.push_back(MetricId::Cpu);
    if (s.showRam) out.push_back(MetricId::Ram);
    if (s.showGpu && gpuAvailable) out.push_back(MetricId::Gpu);
    if (s.showVram && gpuAvailable && vramAvailable) out.push_back(MetricId::Vram);
    if (out.empty()) out.push_back(MetricId::Cpu);  // never leave the user without anything to click
    return out;
}

bool CanHide(const Settings& s, MetricId id, bool gpuAvailable, bool vramAvailable) {
    Settings copy = s;
    switch (id) {
        case MetricId::Cpu: copy.showCpu = false; break;
        case MetricId::Ram: copy.showRam = false; break;
        case MetricId::Gpu: copy.showGpu = false; break;
        case MetricId::Vram: copy.showVram = false; break;
    }
    // Hiding is allowed only if something that is actually available stays enabled.
    return (copy.showCpu || copy.showRam || (copy.showGpu && gpuAvailable) ||
            (copy.showVram && gpuAvailable && vramAvailable));
}

}  // namespace logic
