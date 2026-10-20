#pragma once

#include "common.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

// Self-update through GitHub Releases: check, download, verify (SHA-256) and run the installer.
namespace update {

// ---- Pure helpers (unit-tested) ------------------------------------------------------------------

struct Version {
    int major = 0, minor = 0, patch = 0;
};
std::optional<Version> ParseVersion(std::wstring_view text);  // "v1.2.3" or "1.2.3"
int Compare(const Version& a, const Version& b);              // <0, 0, >0
std::wstring ToString(const Version& v);

struct Release {
    Version version;
    std::wstring tag;           // "v1.2.3"
    std::wstring pageUrl;       // human-readable release page
    std::wstring setupUrl;      // sys-bar-1.2.3-setup.exe
    std::wstring setupHashUrl;  // sys-bar-1.2.3-setup.exe.sha256
    std::wstring setupName;
};
// Reads the JSON of GET /repos/{owner}/{repo}/releases/latest. Drafts, pre-releases and releases
// without a version tag are rejected.
std::optional<Release> ParseLatestRelease(std::string_view json);
// "<64 hex>  <file name>" (sha256sum format) -> lowercase hex digest, checking the file name.
std::optional<std::string> ParseSha256File(std::string_view text, std::wstring_view fileName);
// Case-insensitive comparison of two directories, ignoring trailing separators.
bool SameDirectory(std::wstring_view a, std::wstring_view b);
// SHA-256 of a file as lowercase hex (Windows CNG).
std::optional<std::string> Sha256OfFile(const std::wstring& path);
std::optional<std::string> Sha256OfHandle(HANDLE file);  // from the start of an open file

// ---- Updater ----------------------------------------------------------------------------------------

// Lets the owner abort a download from another thread (closing the request ends a pending read).
struct Cancel {
    std::mutex mutex;
    void* request = nullptr;  // HINTERNET of the transfer in flight
    bool cancelled = false;
};

enum class State {
    Idle,
    Checking,
    UpToDate,
    Available,  // a newer release exists (portable copies stop here)
    Downloading,
    Ready,  // installer downloaded and verified, waiting for a quiet moment
    Installing,
    Failed,
};

// Network work runs on a background thread; every state change posts `message` to `notify`.
class Updater {
public:
    Updater() = default;
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    void Init(HWND notify, UINT message, std::wstring currentVersion, std::wstring releasesApiUrl);
    void Check();     // no-op while busy
    void Download();  // requires State::Available on an installed copy
    bool Install();   // requires State::Ready; launches the installer, which restarts sys-bar
    State state() const { return state_.load(); }
    std::optional<Release> latest() const;
    bool installedCopy() const { return installed_; }

private:
    bool Run(void (Updater::*job)(), State busy);
    void DoCheck();
    void DoDownload();
    void SetState(State state);

    HWND notify_ = nullptr;
    UINT message_ = 0;
    std::wstring current_;
    std::wstring apiUrl_;
    bool installed_ = false;
    std::atomic<State> state_{State::Idle};
    mutable std::mutex mutex_;
    std::optional<Release> latest_;
    std::wstring installerPath_;
    HANDLE installerFile_ = INVALID_HANDLE_VALUE;  // keeps the verified installer from being changed until it runs
    Cancel cancel_;
    std::thread worker_;
};

}  // namespace update
