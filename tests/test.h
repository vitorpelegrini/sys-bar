#pragma once

// Minimal dependency-free test framework.
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& Registry();
void Fail(const char* file, int line, const std::string& message);
std::string Narrow(const std::wstring& s);
extern std::wstring previewDir;  // empty = do not write preview images

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline std::string Show(const std::wstring& v) {
    return "\"" + Narrow(v) + "\"";
}
inline std::string Show(const std::string& v) {
    return "\"" + v + "\"";
}
inline std::string Show(const wchar_t* v) {
    return Show(std::wstring(v));
}
inline std::string Show(bool v) {
    return v ? "true" : "false";
}
template <typename T>
std::string Show(const T& v) {
    std::ostringstream out;
    out << v;
    return out.str();
}

}  // namespace t

#define TEST(name)                                     \
    static void name();                                \
    static t::Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) t::Fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        const auto& va_ = (a);                                                                                         \
        const auto& vb_ = (b);                                                                                         \
        if (!(va_ == vb_))                                                                                             \
            t::Fail(__FILE__, __LINE__, std::string(#a " == " #b "  (") + t::Show(va_) + " vs " + t::Show(vb_) + ")"); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do {                                                                                                               \
        const double va_ = static_cast<double>(a);                                                                     \
        const double vb_ = static_cast<double>(b);                                                                     \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                                                          \
            t::Fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  (") + t::Show(va_) + " vs " + t::Show(vb_) + ")"); \
    } while (0)
