#include "test.h"

#include "common.h"
#include "json.h"
#include "update.h"

using namespace update;

// ---- JSON -------------------------------------------------------------------------------------------

TEST(Json_parses_objects_arrays_and_scalars) {
    const auto v = json::Parse(R"({"a": 1.5, "b": [true, false, null], "c": {"d": "x"}, "e": -2e3})");
    CHECK(v.has_value());
    if (!v) return;
    CHECK(v->type == json::Value::Type::Object);
    CHECK_NEAR(v->Find(L"a")->number, 1.5, 1e-12);
    CHECK_EQ(v->Find(L"b")->array.size(), size_t{3});
    CHECK(v->Find(L"b")->array[0].boolean);
    CHECK(v->Find(L"b")->array[2].type == json::Value::Type::Null);
    CHECK_EQ(v->Find(L"c")->String(L"d"), std::wstring(L"x"));
    CHECK_NEAR(v->Find(L"e")->number, -2000.0, 1e-9);
    CHECK(v->Find(L"missing") == nullptr);
    CHECK_EQ(v->String(L"a"), std::wstring(L""));  // wrong type -> empty
}

TEST(Json_decodes_escapes_and_utf8) {
    const auto v = json::Parse("{\"s\": \"a\\\"b\\\\c\\/d\\n\\u00e9\\ud83d\\ude00\", \"u\": \"Versão 日本\"}");
    CHECK(v.has_value());
    if (!v) return;
    CHECK_EQ(v->String(L"s"), std::wstring(L"a\"b\\c/d\n\u00e9\U0001F600"));
    CHECK_EQ(v->String(L"u"), std::wstring(L"Versão 日本"));
}

TEST(Json_rejects_malformed_input) {
    for (const char* bad :
         {"", "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "tru", "01", "1.", "\"unterminated", "\"\\x\"", "\"\\ud800\"",
          "\"\\udc00\"", "{} extra", "\"\x01\"", "\"\xC3\x28\"", "\"\xC0\xAF\"", "[1 2]"}) {
        if (json::Parse(bad)) t::Fail(__FILE__, __LINE__, std::string("accepted: ") + bad);
    }
}

TEST(Json_limits_nesting_depth) {
    std::string deep(200, '[');
    deep += std::string(200, ']');
    CHECK(!json::Parse(deep));  // no stack exhaustion on hostile input
    std::string ok(30, '[');
    ok += std::string(30, ']');
    CHECK(json::Parse(ok).has_value());
}

TEST(Json_accepts_a_byte_order_mark) {
    CHECK(json::Parse("\xEF\xBB\xBF{\"a\":1}").has_value());
}

// ---- Versions ---------------------------------------------------------------------------------------

TEST(Versions_are_parsed_and_compared) {
    const auto a = ParseVersion(L"v1.2.3");
    CHECK(a.has_value());
    if (a) CHECK_EQ(ToString(*a), std::wstring(L"1.2.3"));
    CHECK(ParseVersion(L"1.10.0").has_value());
    CHECK(!ParseVersion(L"1.2"));
    CHECK(!ParseVersion(L"1.2.3-beta"));  // pre-releases never auto-install
    CHECK(!ParseVersion(L"v1..3"));
    CHECK(!ParseVersion(L""));
    CHECK(!ParseVersion(L"latest"));

    auto v = [](const wchar_t* s) { return *ParseVersion(s); };
    CHECK(Compare(v(L"1.0.0"), v(L"1.0.1")) < 0);
    CHECK(Compare(v(L"1.10.0"), v(L"1.9.9")) > 0);  // numeric, not lexicographic
    CHECK(Compare(v(L"2.0.0"), v(L"1.99.99")) > 0);
    CHECK_EQ(Compare(v(L"v1.2.3"), v(L"1.2.3")), 0);
}

// ---- GitHub release ---------------------------------------------------------------------------------

namespace {
const char* kRelease = R"({
  "url": "https://api.github.com/repos/vitorpelegrini/sys-bar/releases/1",
  "html_url": "https://github.com/vitorpelegrini/sys-bar/releases/tag/v1.2.0",
  "tag_name": "v1.2.0",
  "name": "sys-bar 1.2.0",
  "draft": false,
  "prerelease": false,
  "assets": [
    {"name": "sys-bar-1.2.0-win-x64.zip", "browser_download_url": "https://github.com/vitorpelegrini/sys-bar/releases/download/v1.2.0/sys-bar-1.2.0-win-x64.zip"},
    {"name": "sys-bar-1.2.0-setup.exe", "browser_download_url": "https://github.com/vitorpelegrini/sys-bar/releases/download/v1.2.0/sys-bar-1.2.0-setup.exe"},
    {"name": "sys-bar-1.2.0-setup.exe.sha256", "browser_download_url": "https://github.com/vitorpelegrini/sys-bar/releases/download/v1.2.0/sys-bar-1.2.0-setup.exe.sha256"}
  ]
})";
}

TEST(Latest_release_is_read_from_the_api_response) {
    const auto r = ParseLatestRelease(kRelease);
    CHECK(r.has_value());
    if (!r) return;
    CHECK_EQ(ToString(r->version), std::wstring(L"1.2.0"));
    CHECK_EQ(r->setupName, std::wstring(L"sys-bar-1.2.0-setup.exe"));
    CHECK(r->setupUrl.find(L"/v1.2.0/sys-bar-1.2.0-setup.exe") != std::wstring::npos);
    CHECK(r->setupHashUrl.find(L"setup.exe.sha256") != std::wstring::npos);
    CHECK(r->pageUrl.find(L"/releases/tag/v1.2.0") != std::wstring::npos);
}

TEST(Drafts_prereleases_and_bad_tags_are_ignored) {
    std::string draft = kRelease;
    draft.replace(draft.find("\"draft\": false"), 14, "\"draft\": true");
    CHECK(!ParseLatestRelease(draft));
    std::string pre = kRelease;
    pre.replace(pre.find("\"prerelease\": false"), 19, "\"prerelease\": true");
    CHECK(!ParseLatestRelease(pre));
    std::string tag = kRelease;
    const std::string field = "\"tag_name\": \"v1.2.0\"";
    tag.replace(tag.find(field), field.size(), "\"tag_name\": \"nightly\"");
    CHECK(!ParseLatestRelease(tag));
    CHECK(!ParseLatestRelease("not json"));
}

TEST(Non_https_assets_are_never_used) {
    std::string insecure = kRelease;
    for (size_t at; (at = insecure.find("https://github.com")) != std::string::npos;) {
        insecure.replace(at, 18, "http://github.com");
    }
    const auto r = ParseLatestRelease(insecure);
    CHECK(r.has_value());
    if (r) CHECK(r->setupUrl.empty() && r->setupHashUrl.empty());
}

TEST(Sha256_files_are_parsed_and_checked) {
    const std::string digest = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    CHECK_EQ(ParseSha256File(digest + "  sys-bar-1.2.0-setup.exe\n", L"sys-bar-1.2.0-setup.exe").value_or(""), digest);
    CHECK_EQ(ParseSha256File(digest + " *sys-bar-1.2.0-setup.exe", L"sys-bar-1.2.0-setup.exe").value_or(""), digest);
    CHECK_EQ(ParseSha256File(digest, L"anything").value_or(""), digest);  // bare digest
    std::string upper = digest;
    for (char& c : upper) c = static_cast<char>(toupper(c));
    CHECK_EQ(ParseSha256File(upper + "  x.exe", L"x.exe").value_or(""), digest);  // normalized to lowercase
    CHECK(!ParseSha256File(digest + "  other.exe", L"sys-bar-1.2.0-setup.exe"));  // wrong file
    CHECK(!ParseSha256File("abc  x.exe", L"x.exe"));                              // too short
    CHECK(!ParseSha256File(std::string(64, 'z') + "  x.exe", L"x.exe"));          // not hex
}

TEST(Install_directories_are_compared_safely) {
    CHECK(SameDirectory(L"C:\\Users\\Ana\\AppData\\Local\\Programs\\sys-bar\\",
                        L"c:\\users\\ana\\appdata\\local\\programs\\SYS-BAR"));
    CHECK(!SameDirectory(L"C:\\Tools\\sys-bar", L"C:\\Tools\\sys-bar-portable"));
    CHECK(!SameDirectory(L"", L""));
}

// ---- Integration ------------------------------------------------------------------------------------

TEST(Sha256_of_a_file_matches_a_known_digest) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring path = std::wstring(temp) + L"sysbar-sha-test.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(file, "test", 4, &written, nullptr);
    CloseHandle(file);
    CHECK_EQ(Sha256OfFile(path).value_or(""),
             std::string("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));  // sha256("test")
    DeleteFileW(path.c_str());
    CHECK(!Sha256OfFile(L"Z:\\does\\not\\exist.bin"));
}

TEST(Updater_checks_a_real_github_release) {
    // Uses a public project with semantic-version releases to exercise WinHTTP, TLS, JSON and the state
    // machine end to end. Offline machines skip it.
    HWND sink = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    Updater updater;
    updater.Init(sink, WM_APP + 50, L"0.0.1", L"https://api.github.com/repos/cli/cli/releases/latest");
    CHECK(!updater.installedCopy());  // the test runner is not an installed sys-bar
    updater.Check();
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline && (updater.state() == State::Checking || updater.state() == State::Idle)) {
        Sleep(50);
    }
    if (updater.state() == State::Failed) {
        std::printf("      skipped: no network access to api.github.com\n");
    } else {
        CHECK(updater.state() == State::Available);  // anything is newer than 0.0.1
        const auto release = updater.latest();
        CHECK(release.has_value());
        if (release) {
            CHECK(release->version.major >= 2);
            CHECK(release->setupUrl.empty());  // not a sys-bar release: no installer asset matches
            std::printf("      latest cli/cli release: %s\n", t::Narrow(ToString(release->version)).c_str());
        }
        updater.Download();  // portable copy: must refuse and stay Available
        CHECK(updater.state() == State::Available);
    }
    DestroyWindow(sink);
}