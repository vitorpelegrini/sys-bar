#include "logic.h"

#include "format.h"
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

int RightControlsWidth(const RECT& tray, const std::vector<RECT>& elements, int maxGapPx) {
    const int width = Width(tray);
    if (width <= 0 || tray.bottom <= tray.top) return -1;
    std::vector<RECT> candidates;
    for (const RECT& r : elements) {
        if (r.right <= r.left || r.bottom <= r.top) continue;
        // Inside the taskbar (a little slack for borders), and not a container spanning much of it.
        if (r.left < tray.left - 2 || r.right > tray.right + 2 || r.bottom <= tray.top || r.top >= tray.bottom)
            continue;
        if (Width(r) > width * 2 / 5) continue;
        candidates.push_back(r);
    }
    // Grow the cluster leftwards from the right edge while the next element is close enough.
    int edge = tray.right;
    bool any = false;
    for (bool grew = true; grew;) {
        grew = false;
        for (const RECT& r : candidates) {
            if (r.left < edge && r.right >= edge - maxGapPx) {
                edge = r.left;
                grew = any = true;
            }
        }
    }
    const int reserve = tray.right - edge;
    return any && reserve <= width / 2 ? reserve : -1;
}

std::vector<MetricId> VisibleMetrics(const Settings& s, const Availability& a) {
    std::vector<MetricId> out;
    if (s.showCpu) out.push_back(MetricId::Cpu);
    if (s.showRam) out.push_back(MetricId::Ram);
    if (s.showGpu && a.gpu) out.push_back(MetricId::Gpu);
    if (s.showVram && a.gpu && a.vram) out.push_back(MetricId::Vram);
    if (s.showNet && a.net) out.push_back(MetricId::Net);
    if (s.showDisk && a.disk) out.push_back(MetricId::Disk);
    if (s.showBattery && a.battery) out.push_back(MetricId::Battery);
    if (out.empty()) out.push_back(MetricId::Cpu);  // never leave the user without anything to click
    return out;
}

bool CanHide(const Settings& s, MetricId id, const Availability& a) {
    Settings copy = s;
    switch (id) {
        case MetricId::Cpu: copy.showCpu = false; break;
        case MetricId::Ram: copy.showRam = false; break;
        case MetricId::Gpu: copy.showGpu = false; break;
        case MetricId::Vram: copy.showVram = false; break;
        case MetricId::Disk: copy.showDisk = false; break;
        case MetricId::Net: copy.showNet = false; break;
        case MetricId::Battery: copy.showBattery = false; break;
    }
    // Hiding is allowed only if something that is actually available stays enabled.
    return copy.showCpu || copy.showRam || (copy.showGpu && a.gpu) || (copy.showVram && a.gpu && a.vram) ||
           (copy.showDisk && a.disk) || (copy.showNet && a.net) || (copy.showBattery && a.battery);
}

bool SeparateDisks(const Settings& s, size_t diskCount) {
    return s.disksSeparate && diskCount >= 2 && diskCount <= kMaxDiskColumns;
}

std::wstring DiskStripLabel(hw::DiskKind kind, const std::wstring& letters, int number, const wchar_t* fallback) {
    const wchar_t* name = hw::DiskKindName(kind);
    std::wstring label = *name ? name : (fallback ? fallback : L"");
    const std::wstring letter = text::FirstDriveLetter(letters);
    label += L' ';
    label += letter.empty() ? std::to_wstring(number) : letter;
    return label;
}

namespace {

constexpr std::pair<FanRole, const wchar_t*> kRoleCodes[] = {{FanRole::Cpu, L"cpu"},
                                                             {FanRole::Gpu, L"gpu"},
                                                             {FanRole::Case, L"case"},
                                                             {FanRole::Pump, L"pump"},
                                                             {FanRole::Psu, L"psu"}};

}  // namespace

std::vector<std::pair<std::wstring, FanRole>> ParseFanRoles(const std::wstring& text) {
    std::vector<std::pair<std::wstring, FanRole>> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L';', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring item = text.substr(start, end - start);
        if (const auto eq = item.rfind(L'='); eq != std::wstring::npos && eq > 0) {
            const std::wstring name = text::Trim(item.substr(0, eq));
            const std::wstring code = text::Trim(item.substr(eq + 1));
            for (const auto& [role, roleCode] : kRoleCodes) {
                if (_wcsicmp(code.c_str(), roleCode) == 0 && !name.empty()) {
                    std::erase_if(out, [&](const auto& p) { return p.first == name; });
                    out.emplace_back(name, role);
                }
            }
        }
        start = end + 1;
    }
    return out;
}

std::wstring FormatFanRoles(const std::vector<std::pair<std::wstring, FanRole>>& roles) {
    std::wstring out;
    for (const auto& [name, role] : roles) {
        if (name.empty() || name.find_first_of(L"=;") != std::wstring::npos) continue;
        for (const auto& [known, code] : kRoleCodes) {
            if (known == role) out += (out.empty() ? L"" : L";") + name + L"=" + code;
        }
    }
    return out;
}

std::wstring WithFanRole(const std::wstring& text, const std::wstring& name, FanRole role) {
    auto roles = ParseFanRoles(text);
    std::erase_if(roles, [&](const auto& p) { return p.first == name; });
    if (role != FanRole::Unknown) roles.emplace_back(name, role);
    return FormatFanRoles(roles);
}

FanRole EffectiveFanRole(const std::wstring& text, const std::wstring& name, FanRole detected) {
    for (const auto& [n, role] : ParseFanRoles(text)) {
        if (n == name) return role;
    }
    return detected;
}

std::wstring FanNumber(const std::wstring& name) {
    const auto hash = name.rfind(L'#');
    if (hash == std::wstring::npos || hash + 1 >= name.size()) return name;
    const std::wstring digits = name.substr(hash + 1);
    for (wchar_t c : digits) {
        if (c < L'0' || c > L'9') return name;
    }
    return digits;
}

float TemperatureGauge(MetricId id, double celsius) {
    const double full = id == MetricId::Disk ? 80.0 : 100.0;
    return static_cast<float>(std::clamp((celsius - 20.0) / (full - 20.0), 0.0, 1.0));
}

int TemperatureLevel(MetricId id, double celsius) {
    const bool drive = id == MetricId::Disk;
    const double warm = drive ? 60.0 : 85.0;
    const double hot = drive ? 70.0 : 95.0;
    return celsius >= hot ? 2 : celsius >= warm ? 1 : 0;
}

}  // namespace logic