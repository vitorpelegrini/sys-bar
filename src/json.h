#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal, strict JSON reader (RFC 8259) for the few small documents sys-bar consumes
// (the GitHub releases API). Strings are returned as UTF-16.
namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::wstring string;
    std::vector<Value> array;
    std::vector<std::pair<std::wstring, Value>> object;

    // Object member lookup; nullptr when missing or when this is not an object.
    const Value* Find(std::wstring_view key) const;
    std::wstring String(std::wstring_view key) const;  // "" when missing or not a string
    bool Bool(std::wstring_view key) const;            // false when missing or not a bool
};

// Parses UTF-8 text. Returns nothing on any syntax error, invalid UTF-8 or excessive nesting.
std::optional<Value> Parse(std::string_view utf8);

}  // namespace json
