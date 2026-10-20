#include "test.h"

#include "format.h"
#include "i18n.h"

#include <set>

using i18n::Lang;
using i18n::Str;

TEST(Every_language_defines_every_string) {
    for (int l = 0; l < i18n::kLanguageCount; ++l) {
        const auto lang = static_cast<Lang>(l);
        for (int s = 0; s < static_cast<int>(Str::Count); ++s) {
            const wchar_t* value = i18n::Get(lang, static_cast<Str>(s));
            if (!value || !*value) {
                t::Fail(__FILE__, __LINE__,
                        "missing string " + std::to_string(s) + " for " + t::Narrow(i18n::Code(lang)));
            }
        }
    }
}

TEST(Placeholders_are_consistent_across_languages) {
    // A translation must use exactly the same placeholders as English.
    auto placeholders = [](const std::wstring& s) {
        std::set<wchar_t> found;
        for (size_t i = 0; i + 2 < s.size(); ++i) {
            if (s[i] == L'{' && s[i + 2] == L'}') found.insert(s[i + 1]);
        }
        return found;
    };
    for (int s = 0; s < static_cast<int>(Str::Count); ++s) {
        const auto expected = placeholders(i18n::Get(Lang::English, static_cast<Str>(s)));
        for (int l = 1; l < i18n::kLanguageCount; ++l) {
            const auto lang = static_cast<Lang>(l);
            if (placeholders(i18n::Get(lang, static_cast<Str>(s))) != expected) {
                t::Fail(__FILE__, __LINE__,
                        "placeholder mismatch in string " + std::to_string(s) + " for " + t::Narrow(i18n::Code(lang)));
            }
        }
    }
}

TEST(Language_detection_from_tags) {
    CHECK(i18n::FromTag(L"pt-BR") == Lang::Portuguese);
    CHECK(i18n::FromTag(L"pt-PT") == Lang::Portuguese);
    CHECK(i18n::FromTag(L"en-US") == Lang::English);
    CHECK(i18n::FromTag(L"es-MX") == Lang::Spanish);
    CHECK(i18n::FromTag(L"DE-de") == Lang::German);
    CHECK(i18n::FromTag(L"zh-CN") == Lang::ChineseSimplified);
    CHECK(i18n::FromTag(L"zh-Hans-SG") == Lang::ChineseSimplified);
    CHECK(i18n::FromTag(L"zh-TW") == Lang::ChineseTraditional);
    CHECK(i18n::FromTag(L"zh-HK") == Lang::ChineseTraditional);
    CHECK(i18n::FromTag(L"zh-Hant") == Lang::ChineseTraditional);
    CHECK(i18n::FromTag(L"uk-UA") == Lang::Ukrainian);
    CHECK(i18n::FromTag(L"nl-NL") == Lang::English);  // unsupported -> English
    CHECK(i18n::FromTag(L"") == Lang::English);
}

TEST(Language_codes_round_trip) {
    std::set<std::wstring> codes;
    for (int l = 0; l < i18n::kLanguageCount; ++l) {
        const auto lang = static_cast<Lang>(l);
        Lang parsed = Lang::English;
        CHECK(i18n::FromCode(i18n::Code(lang), parsed));
        CHECK(parsed == lang);
        CHECK(*i18n::NativeName(lang) != L'\0');
        CHECK(codes.insert(i18n::Code(lang)).second);  // codes are unique
    }
    Lang parsed = Lang::German;
    CHECK(!i18n::FromCode(L"auto", parsed));
    CHECK(!i18n::FromCode(L"", parsed));
}

TEST(Out_of_range_ids_are_safe) {
    CHECK_EQ(std::wstring(i18n::Get(Lang::Count, Str::MenuExit)), std::wstring(L""));
    CHECK_EQ(std::wstring(i18n::Get(Lang::English, Str::Count)), std::wstring(L""));
}

TEST(Percent_format_per_language) {
    CHECK_EQ(text::Format(i18n::Get(Lang::English, Str::PercentFormat), {L"23"}), std::wstring(L"23%"));
    CHECK_EQ(text::Format(i18n::Get(Lang::Turkish, Str::PercentFormat), {L"23"}), std::wstring(L"%23"));
    CHECK_EQ(text::Format(i18n::Get(Lang::French, Str::PercentFormat), {L"23"}), std::wstring(L"23 %"));
}
