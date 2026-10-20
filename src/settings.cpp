#include "settings.h"

#include <shlobj.h>

#include <algorithm>

namespace {

constexpr wchar_t kSection[] = L"sys-bar";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"sys-bar";

bool ReadBool(const std::wstring& file, const wchar_t* key, bool fallback) {
    return GetPrivateProfileIntW(kSection, key, fallback ? 1 : 0, file.c_str()) != 0;
}

void WriteValue(const std::wstring& file, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), file.c_str());
}

std::wstring ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring AutostartCommand() {
    return L"\"" + ExecutablePath() + L"\"";
}

}  // namespace

std::wstring Settings::FilePath() {
    wchar_t overrideDir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"SYSBAR_CONFIG_DIR", overrideDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        CreateDirectoryW(overrideDir, nullptr);
        return std::wstring(overrideDir) + L"\\settings.ini";
    }

    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData))) {
        path = appData;
        path += L"\\sys-bar";
        CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\settings.ini";
    }
    CoTaskMemFree(appData);
    return path;
}

void Settings::Load() {
    const std::wstring file = FilePath();
    if (file.empty()) return;
    showCpu = ReadBool(file, L"ShowCpu", showCpu);
    showRam = ReadBool(file, L"ShowRam", showRam);
    showGpu = ReadBool(file, L"ShowGpu", showGpu);
    showVram = ReadBool(file, L"ShowVram", showVram);
    showDisk = ReadBool(file, L"ShowDisk", showDisk);
    showTemperatures = ReadBool(file, L"ShowTemperatures", showTemperatures);
    colorByLoad = ReadBool(file, L"ColorByLoad", colorByLoad);
    clickOpensTaskManager = ReadBool(file, L"ClickOpensTaskManager", clickOpensTaskManager);
    intervalMs = std::clamp<DWORD>(GetPrivateProfileIntW(kSection, L"IntervalMs", intervalMs, file.c_str()),
                                   kMinIntervalMs, kMaxIntervalMs);
    offsetDip = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"OffsetDip", offsetDip, file.c_str())), 0,
                           kMaxOffsetDip);
    wchar_t lang[32] = {};
    GetPrivateProfileStringW(kSection, L"Language", language.c_str(), lang, 32, file.c_str());
    language = lang[0] ? lang : L"auto";
    if (!showCpu && !showRam && !showGpu && !showVram && !showDisk) showCpu = true;
}

void Settings::Save() const {
    const std::wstring file = FilePath();
    if (file.empty()) return;
    WriteValue(file, L"ShowCpu", std::to_wstring(showCpu));
    WriteValue(file, L"ShowRam", std::to_wstring(showRam));
    WriteValue(file, L"ShowGpu", std::to_wstring(showGpu));
    WriteValue(file, L"ShowVram", std::to_wstring(showVram));
    WriteValue(file, L"ShowDisk", std::to_wstring(showDisk));
    WriteValue(file, L"ShowTemperatures", std::to_wstring(showTemperatures));
    WriteValue(file, L"ColorByLoad", std::to_wstring(colorByLoad));
    WriteValue(file, L"ClickOpensTaskManager", std::to_wstring(clickOpensTaskManager));
    WriteValue(file, L"IntervalMs", std::to_wstring(intervalMs));
    WriteValue(file, L"OffsetDip", std::to_wstring(offsetDip));
    WriteValue(file, L"Language", language);
}

bool Settings::IsAutostartEnabled() {
    wchar_t value[2048] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) {
        return false;
    }
    return _wcsicmp(value, AutostartCommand().c_str()) == 0;
}

bool Settings::SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS status;
    if (enable) {
        const std::wstring command = AutostartCommand();
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
