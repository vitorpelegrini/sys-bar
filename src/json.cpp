#include "json.h"

#include <cstdlib>

namespace json {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<Value> Document() {
        Value value;
        SkipSpace();
        if (!ParseValue(value, 0)) return std::nullopt;
        SkipSpace();
        if (pos_ != s_.size()) return std::nullopt;  // trailing garbage
        return value;
    }

private:
    bool AtEnd() const { return pos_ >= s_.size(); }
    char Peek() const { return AtEnd() ? '\0' : s_[pos_]; }

    void SkipSpace() {
        while (!AtEnd() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool Literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) return false;
        pos_ += word.size();
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return false;
        switch (Peek()) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': out.type = Value::Type::String; return ParseString(out.string);
            case 't':
                out.type = Value::Type::Bool;
                out.boolean = true;
                return Literal("true");
            case 'f':
                out.type = Value::Type::Bool;
                out.boolean = false;
                return Literal("false");
            case 'n': out.type = Value::Type::Null; return Literal("null");
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(Value& out, int depth) {
        out.type = Value::Type::Object;
        ++pos_;  // '{'
        SkipSpace();
        if (Peek() == '}') return ++pos_, true;
        for (;;) {
            SkipSpace();
            std::wstring key;
            if (Peek() != '"' || !ParseString(key)) return false;
            SkipSpace();
            if (Peek() != ':') return false;
            ++pos_;
            SkipSpace();
            Value value;
            if (!ParseValue(value, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (Peek() == '}') return ++pos_, true;
            return false;
        }
    }

    bool ParseArray(Value& out, int depth) {
        out.type = Value::Type::Array;
        ++pos_;  // '['
        SkipSpace();
        if (Peek() == ']') return ++pos_, true;
        for (;;) {
            SkipSpace();
            Value value;
            if (!ParseValue(value, depth + 1)) return false;
            out.array.push_back(std::move(value));
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (Peek() == ']') return ++pos_, true;
            return false;
        }
    }

    bool ParseNumber(Value& out) {
        const size_t start = pos_;
        if (Peek() == '-') ++pos_;
        if (Peek() == '0') {
            ++pos_;
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (Peek() >= '0' && Peek() <= '9') ++pos_;
        } else {
            return false;
        }
        if (Peek() == '.') {
            ++pos_;
            if (!(Peek() >= '0' && Peek() <= '9')) return false;
            while (Peek() >= '0' && Peek() <= '9') ++pos_;
        }
        if (Peek() == 'e' || Peek() == 'E') {
            ++pos_;
            if (Peek() == '+' || Peek() == '-') ++pos_;
            if (!(Peek() >= '0' && Peek() <= '9')) return false;
            while (Peek() >= '0' && Peek() <= '9') ++pos_;
        }
        out.type = Value::Type::Number;
        out.number = std::strtod(std::string(s_.substr(start, pos_ - start)).c_str(), nullptr);
        return true;
    }

    static void AppendCodePoint(std::wstring& out, unsigned cp) {
        if (cp < 0x10000) {
            out += static_cast<wchar_t>(cp);
        } else {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        }
    }

    bool Hex4(unsigned& value) {
        if (pos_ + 4 > s_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    // Decodes one UTF-8 sequence starting at pos_.
    bool Utf8(unsigned& cp) {
        const auto b0 = static_cast<unsigned char>(s_[pos_]);
        int extra;
        if (b0 < 0x80) {
            cp = b0;
            extra = 0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            extra = 3;
        } else {
            return false;
        }
        if (pos_ + 1 + extra > s_.size()) return false;
        for (int i = 1; i <= extra; ++i) {
            const auto b = static_cast<unsigned char>(s_[pos_ + i]);
            if ((b & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3F);
        }
        static constexpr unsigned kMin[] = {0, 0x80, 0x800, 0x10000};
        if (cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;  // overlong/invalid
        pos_ += 1 + extra;
        return true;
    }

    bool ParseString(std::wstring& out) {
        ++pos_;  // opening quote
        out.clear();
        while (!AtEnd()) {
            const char c = s_[pos_];
            if (c == '"') return ++pos_, true;
            if (static_cast<unsigned char>(c) < 0x20) return false;  // raw control characters are not allowed
            if (c != '\\') {
                unsigned cp = 0;
                if (!Utf8(cp)) return false;
                AppendCodePoint(out, cp);
                continue;
            }
            ++pos_;
            if (AtEnd()) return false;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += L'"'; break;
                case '\\': out += L'\\'; break;
                case '/': out += L'/'; break;
                case 'b': out += L'\b'; break;
                case 'f': out += L'\f'; break;
                case 'n': out += L'\n'; break;
                case 'r': out += L'\r'; break;
                case 't': out += L'\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
                        unsigned low = 0;
                        if (!Literal("\\u") || !Hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;  // lone low surrogate
                    }
                    AppendCodePoint(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;  // unterminated
    }

    std::string_view s_;
    size_t pos_ = 0;
};

}  // namespace

const Value* Value::Find(std::wstring_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : object) {
        if (k == key) return &v;
    }
    return nullptr;
}

std::wstring Value::String(std::wstring_view key) const {
    const Value* v = Find(key);
    return v && v->type == Type::String ? v->string : std::wstring();
}

bool Value::Bool(std::wstring_view key) const {
    const Value* v = Find(key);
    return v && v->type == Type::Bool && v->boolean;
}

std::optional<Value> Parse(std::string_view utf8) {
    if (utf8.size() >= 3 && utf8.substr(0, 3) == "\xEF\xBB\xBF") utf8.remove_prefix(3);  // BOM
    return Parser(utf8).Document();
}

}  // namespace json
