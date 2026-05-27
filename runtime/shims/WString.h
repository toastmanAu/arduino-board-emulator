#pragma once
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int n)            { char b[32]; std::snprintf(b, sizeof(b), "%d",  n); s_ = b; }
    String(unsigned int n)   { char b[32]; std::snprintf(b, sizeof(b), "%u",  n); s_ = b; }
    String(long n)           { char b[32]; std::snprintf(b, sizeof(b), "%ld", n); s_ = b; }
    String(unsigned long n)  { char b[32]; std::snprintf(b, sizeof(b), "%lu", n); s_ = b; }
    String(double v, int decimals = 2) {
        char b[64]; std::snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b;
    }

    const char* c_str() const   { return s_.c_str(); }
    size_t      length() const  { return s_.size(); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String  operator+ (const String& o) const { String r(*this); r += o; return r; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }

    long  toInt() const   { try { return std::stol(s_); } catch (...) { return 0; } }
    float toFloat() const { try { return std::stof(s_); } catch (...) { return 0.0f; } }

    int indexOf(const String& needle) const {
        auto pos = s_.find(needle.s_);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    String substring(size_t from) const { return String(s_.substr(from)); }
    String substring(size_t from, size_t to) const {
        if (to <= from) return String();
        return String(s_.substr(from, to - from));
    }

    bool startsWith(const String& p) const {
        return s_.size() >= p.s_.size() && s_.compare(0, p.s_.size(), p.s_) == 0;
    }
    bool endsWith(const String& p) const {
        return s_.size() >= p.s_.size() &&
               s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }

    char charAt(size_t i) const { return i < s_.size() ? s_[i] : '\0'; }

private:
    std::string s_;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}
