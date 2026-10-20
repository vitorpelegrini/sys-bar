#include "update.h"

#include "json.h"
#include "signature.h"

#include <objbase.h>

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace update {
namespace {

constexpr wchar_t kUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{8C5D4F7A-2E1B-4D63-9A7E-5B2C1F0D3E91}_is1";
constexpr uint64_t kMaxJsonBytes = 1u << 20;        // 1 MB
constexpr uint64_t kMaxInstallerBytes = 64u << 20;  // 64 MB
constexpr int kTimeoutMs = 15000;

std::wstring Lower(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) c = static_cast<wchar_t>(std::towlower(c));
    return out;
}

bool EndsWith(std::wstring_view s, std::wstring_view suffix) {
    return s.size() >= suffix.size() && Lower(s.substr(s.size() - suffix.size())) == Lower(suffix);
}

// Only plain file names are accepted for anything that ends up on disk.
bool SafeFileName(std::wstring_view name) {
    if (name.empty() || name.size() > 100 || name[0] == L'.') return false;
    return std::all_of(name.begin(), name.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' ||
               c == L'-' || c == L'_';
    });
}

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring dir(path, n);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash);
}

// Only a per-user installation updates itself: one installed for all users (Program Files) needs an
// administrator, so for it an update is announced and the release page opened instead.
bool IsInstalledCopy() {
    wchar_t location[MAX_PATH * 2] = {};
    DWORD size = sizeof(location);
    return RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, location,
                        &size) == ERROR_SUCCESS &&
           SameDirectory(location, ExecutableDirectory());
}

std::wstring NewGuid() {
    GUID guid{};
    wchar_t text[64] = {};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, static_cast<int>(std::size(text)))) return {};
    std::wstring out = text;
    out.erase(std::remove_if(out.begin(), out.end(), [](wchar_t c) { return c == L'{' || c == L'}'; }), out.end());
    return out;
}

// HTTPS GET into memory (`body`) or into a file (`file`), with size and time limits. `cancel` (when given)
// lets another thread abort it at once, even in the middle of a read.
bool HttpGet(const std::wstring& url, const std::wstring& userAgent, std::string* body, HANDLE file, uint64_t maxBytes,
             DWORD* statusOut = nullptr, Cancel* cancel = nullptr) {
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[2048] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring object = std::wstring(path) + extra;

    bool ok = false;
    HINTERNET session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    if (session) {
        WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
        connection = WinHttpConnect(session, host, parts.nPort, 0);
    }
    if (connection) {
        request = WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (request && cancel) {
        std::lock_guard lock(cancel->mutex);
        if (cancel->cancelled) {
            WinHttpCloseHandle(request);
            request = nullptr;
        } else {
            cancel->request = request;
        }
    }
    if (request) {
        // Only HTTPS redirects (GitHub serves assets from a CDN).
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0;
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (statusOut) *statusOut = status;
            if (status == 200) {
                uint64_t total = 0;
                ok = true;
                std::vector<char> buffer(64 * 1024);
                for (;;) {
                    if (cancel && cancel->cancelled) {  // read under no lock: a stale "false" only costs a chunk
                        ok = false;
                        break;
                    }
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
                        ok = false;
                        break;
                    }
                    if (read == 0) break;
                    total += read;
                    if (total > maxBytes) {
                        ok = false;
                        break;
                    }
                    if (body) body->append(buffer.data(), read);
                    DWORD written = 0;
                    if (file && (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
    }
    if (request) {
        // The canceller may have closed it already (that is how a pending read gets aborted).
        bool close = true;
        if (cancel) {
            std::lock_guard lock(cancel->mutex);
            close = cancel->request == request;
            cancel->request = nullptr;
        }
        if (close) WinHttpCloseHandle(request);
    }
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

}  // namespace

std::optional<std::string> Sha256OfFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    auto result = Sha256OfHandle(file);
    CloseHandle(file);
    return result;
}

std::optional<std::string> Sha256OfHandle(HANDLE file) {
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return std::nullopt;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::string> result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<UCHAR> buffer(64 * 1024);
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
                ok = false;
                break;
            }
        }
        UCHAR digest[32] = {};
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string hex;
            for (UCHAR b : digest) {
                hex += kHex[b >> 4];
                hex += kHex[b & 15];
            }
            result = hex;
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

// ---- Pure helpers -------------------------------------------------------------------------------------

std::optional<Version> ParseVersion(std::wstring_view text) {
    if (!text.empty() && (text[0] == L'v' || text[0] == L'V')) text.remove_prefix(1);
    Version v;
    int* fields[3] = {&v.major, &v.minor, &v.patch};
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        if (pos >= text.size() || !std::iswdigit(text[pos])) return std::nullopt;
        int value = 0;
        while (pos < text.size() && std::iswdigit(text[pos])) {
            value = value * 10 + (text[pos] - L'0');
            if (value > 100000) return std::nullopt;
            ++pos;
        }
        *fields[i] = value;
        if (i < 2) {
            if (pos >= text.size() || text[pos] != L'.') return std::nullopt;
            ++pos;
        }
    }
    // Anything after x.y.z (pre-release/build suffixes) is not a stable version.
    return pos == text.size() ? std::optional<Version>(v) : std::nullopt;
}

int Compare(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::wstring ToString(const Version& v) {
    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
}

std::optional<Release> ParseLatestRelease(std::string_view text) {
    const auto doc = json::Parse(text);
    if (!doc || doc->type != json::Value::Type::Object) return std::nullopt;
    if (doc->Bool(L"draft") || doc->Bool(L"prerelease")) return std::nullopt;
    Release release;
    release.tag = doc->String(L"tag_name");
    const auto version = ParseVersion(release.tag);
    if (!version) return std::nullopt;
    release.version = *version;
    release.pageUrl = doc->String(L"html_url");

    const std::wstring setupName = L"sys-bar-" + ToString(*version) + L"-setup.exe";
    if (const json::Value* assets = doc->Find(L"assets"); assets && assets->type == json::Value::Type::Array) {
        for (const json::Value& asset : assets->array) {
            const std::wstring name = asset.String(L"name");
            const std::wstring url = asset.String(L"browser_download_url");
            if (url.rfind(L"https://", 0) != 0) continue;
            if (_wcsicmp(name.c_str(), setupName.c_str()) == 0) {
                release.setupUrl = url;
                release.setupName = name;
            } else if (_wcsicmp(name.c_str(), (setupName + L".sha256").c_str()) == 0) {
                release.setupHashUrl = url;
            }
        }
    }
    return release;
}

std::optional<std::string> ParseSha256File(std::string_view text, std::wstring_view fileName) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
    if (text.size() < 64) return std::nullopt;
    std::string hex(text.substr(0, 64));
    for (char& c : hex) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return std::nullopt;
    }
    std::string_view rest = text.substr(64);
    if (rest.empty()) return hex;  // bare digest
    if (rest[0] != ' ' && rest[0] != '\t') return std::nullopt;
    while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t' || rest[0] == '*')) rest.remove_prefix(1);
    const std::wstring name(rest.begin(), rest.end());
    return _wcsicmp(name.c_str(), std::wstring(fileName).c_str()) == 0 ? std::optional<std::string>(hex) : std::nullopt;
}

bool SameDirectory(std::wstring_view a, std::wstring_view b) {
    auto trim = [](std::wstring_view s) {
        while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.remove_suffix(1);
        return Lower(s);
    };
    const std::wstring x = trim(a), y = trim(b);
    return !x.empty() && x == y;
}

// ---- Updater -----------------------------------------------------------------------------------------

Updater::~Updater() {
    // Exiting during a download: abort it now (closing the request ends a pending read) instead of
    // keeping the process, its files and its single-instance mutex alive for minutes.
    {
        std::lock_guard lock(cancel_.mutex);
        cancel_.cancelled = true;
        if (cancel_.request) WinHttpCloseHandle(static_cast<HINTERNET>(cancel_.request));
        cancel_.request = nullptr;
    }
    if (worker_.joinable()) worker_.join();
    if (installerFile_ != INVALID_HANDLE_VALUE) CloseHandle(installerFile_);
}

void Updater::Init(HWND notify, UINT message, std::wstring currentVersion, std::wstring releasesApiUrl) {
    notify_ = notify;
    message_ = message;
    current_ = std::move(currentVersion);
    apiUrl_ = std::move(releasesApiUrl);
    installed_ = IsInstalledCopy();
}

std::optional<Release> Updater::latest() const {
    std::lock_guard lock(mutex_);
    return latest_;
}

void Updater::SetState(State state) {
    state_ = state;
    if (notify_) PostMessageW(notify_, message_, static_cast<WPARAM>(state), 0);
}

bool Updater::Run(void (Updater::*job)(), State busy) {
    // The busy state is taken here, atomically, before the thread exists: a second request (timer and menu
    // at once) sees it and returns, so the UI thread never joins a worker that is still running.
    State s = state_.load();
    do {
        if (s == State::Checking || s == State::Downloading || s == State::Installing) return false;
    } while (!state_.compare_exchange_weak(s, busy));
    if (notify_) PostMessageW(notify_, message_, static_cast<WPARAM>(busy), 0);
    if (worker_.joinable()) worker_.join();  // finished: it set a final state before ending
    worker_ = std::thread([this, job] { (this->*job)(); });
    return true;
}

void Updater::Check() {
    if (state_ == State::Ready) return;  // already downloaded: nothing new to learn
    Run(&Updater::DoCheck, State::Checking);
}

void Updater::Download() {
    if (state_ != State::Available || !installed_) return;
    Run(&Updater::DoDownload, State::Downloading);
}

void Updater::DoCheck() {
    std::string body;
    const std::wstring agent = L"sys-bar/" + current_;
    std::optional<Release> release;
    DWORD status = 0;
    if (HttpGet(apiUrl_, agent, &body, nullptr, kMaxJsonBytes, &status, &cancel_)) release = ParseLatestRelease(body);
    if (status == 404) return SetState(State::UpToDate);  // nothing published yet
    const auto current = ParseVersion(current_);
    if (!release || !current) return SetState(State::Failed);
    {
        std::lock_guard lock(mutex_);
        latest_ = release;
    }
    SetState(Compare(release->version, *current) > 0 ? State::Available : State::UpToDate);
}

void Updater::DoDownload() {
    const auto release = latest();
    if (!release || release->setupUrl.empty() || release->setupHashUrl.empty() || !SafeFileName(release->setupName) ||
        !EndsWith(release->setupName, L".exe")) {
        return SetState(State::Failed);
    }
    const std::wstring agent = L"sys-bar/" + current_;

    // 1) Expected digest, published next to the installer.
    std::string hashText;
    if (!HttpGet(release->setupHashUrl, agent, &hashText, nullptr, 4096, nullptr, &cancel_)) {
        return SetState(State::Failed);
    }
    const auto expected = ParseSha256File(hashText, release->setupName);
    if (!expected) return SetState(State::Failed);

    // 2) Installer, into a new folder with a random name under %TEMP% (never a predictable, reused path).
    wchar_t temp[MAX_PATH] = {};
    const DWORD tempLength = GetTempPathW(MAX_PATH, temp);
    const std::wstring guid = NewGuid();
    if (tempLength == 0 || tempLength >= MAX_PATH || guid.empty()) return SetState(State::Failed);
    const std::wstring dir = std::wstring(temp) + L"sys-bar-update-" + guid;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) return SetState(State::Failed);
    const std::wstring path = dir + L"\\" + release->setupName;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return SetState(State::Failed);
    const bool downloaded = HttpGet(release->setupUrl, agent, nullptr, file, kMaxInstallerBytes, nullptr, &cancel_);
    CloseHandle(file);

    // 3) Verify, through a handle that stays open until the installer starts: it lets others read (the
    // loader) but nobody write, rename or delete, so the file that runs is the file that was checked.
    HANDLE locked = downloaded ? CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                             FILE_ATTRIBUTE_NORMAL, nullptr)
                               : INVALID_HANDLE_VALUE;
    const auto actual = locked != INVALID_HANDLE_VALUE ? Sha256OfHandle(locked) : std::nullopt;
    bool trusted = actual && *actual == *expected;
    // Once releases are code-signed, an update must also be signed by the same publisher as this copy.
    if (trusted) {
        if (const auto own = signature::OwnSigner()) trusted = signature::TrustedSigner(path) == own;
    }
    if (!trusted) {
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        DeleteFileW(path.c_str());
        RemoveDirectoryW(dir.c_str());
        return SetState(State::Failed);
    }
    {
        std::lock_guard lock(mutex_);
        installerPath_ = path;
        if (installerFile_ != INVALID_HANDLE_VALUE) CloseHandle(installerFile_);
        installerFile_ = locked;
    }
    SetState(State::Ready);
}

bool Updater::Install() {
    if (state_ != State::Ready) return false;
    std::wstring path;
    {
        std::lock_guard lock(mutex_);
        path = installerPath_;
    }
    // The installer closes this instance gracefully (restoring the taskbar), updates the files and
    // starts the new version again. Per-user, like the installation being updated.
    std::wstring command = L"\"" + path + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) {
        SetState(State::Failed);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    {
        std::lock_guard lock(mutex_);
        if (installerFile_ != INVALID_HANDLE_VALUE) CloseHandle(installerFile_);
        installerFile_ = INVALID_HANDLE_VALUE;
    }
    state_ = State::Installing;
    return true;
}

}  // namespace update
