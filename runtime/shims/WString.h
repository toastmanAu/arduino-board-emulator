#pragma once
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>

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

    // Implicit conversion to const char* — matches Arduino's String behaviour
    // and lets calls like `lcd.print(myString)` resolve to LovyanGFX's
    // print(const char*) overload (which doesn't have a String overload).
    operator const char*() const { return s_.c_str(); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String  operator+ (const String& o) const { String r(*this); r += o; return r; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }

    // Explicit const char* overloads — without these, `ssid == ""` is
    // ambiguous between the String overload (via implicit String construction)
    // and the pointer comparison from the implicit const char* conversion.
    bool operator==(const char* rhs) const { return rhs && s_ == rhs; }
    bool operator!=(const char* rhs) const { return !(*this == rhs); }
    friend bool operator==(const char* lhs, const String& rhs) { return rhs == lhs; }
    friend bool operator!=(const char* lhs, const String& rhs) { return rhs != lhs; }

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

    // toCharArray — Arduino API: copies up to len-1 chars into buf and
    // null-terminates. Used by ESP32Time and many older libraries.
    void toCharArray(char* buf, size_t len, size_t index = 0) const {
        if (!buf || len == 0) return;
        if (index >= s_.size()) { buf[0] = '\0'; return; }
        size_t copy = std::min(len - 1, s_.size() - index);
        std::memcpy(buf, s_.data() + index, copy);
        buf[copy] = '\0';
    }
    void getBytes(uint8_t* buf, size_t len, size_t index = 0) const {
        toCharArray(reinterpret_cast<char*>(buf), len, index);
    }

    // In-place case conversion — matches Arduino's String API (modifies in place).
    void toUpperCase() {
        for (auto& c : s_) if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    }
    void toLowerCase() {
        for (auto& c : s_) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    void trim() {
        size_t a = s_.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { s_.clear(); return; }
        size_t b = s_.find_last_not_of(" \t\r\n");
        s_ = s_.substr(a, b - a + 1);
    }
    void replace(const String& from, const String& to) {
        if (from.s_.empty()) return;
        size_t pos = 0;
        while ((pos = s_.find(from.s_, pos)) != std::string::npos) {
            s_.replace(pos, from.s_.size(), to.s_);
            pos += to.s_.size();
        }
    }
    void replace(char from, char to) {
        for (auto& c : s_) if (c == from) c = to;
    }
    int lastIndexOf(const String& needle) const {
        auto pos = s_.rfind(needle.s_);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        auto pos = s_.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    void remove(size_t from)              { if (from < s_.size()) s_.erase(from); }
    void remove(size_t from, size_t count){ if (from < s_.size()) s_.erase(from, count); }

    // write() / concat() — needed by ArduinoJson 7.x Writer<String> and
    // ArduinoStringWriter specialisation when ARDUINO is defined.
    size_t write(uint8_t c) { s_ += static_cast<char>(c); return 1; }
    size_t write(const uint8_t* buf, size_t n) {
        s_.append(reinterpret_cast<const char*>(buf), n); return n;
    }
    bool concat(const char* cstr) {
        if (cstr) s_ += cstr;
        return true;
    }
    bool concat(const String& o) { s_ += o.s_; return true; }

    // Stream-like read interface — needed by ArduinoJson 7.x's generic
    // Reader<String> (deserializeJson(doc, someString)). Reads bytes from
    // an internal cursor and returns -1 at EOF. Assignment resets the cursor.
    int read() {
        if (read_pos_ >= s_.size()) return -1;
        return static_cast<unsigned char>(s_[read_pos_++]);
    }
    int peek() {
        if (read_pos_ >= s_.size()) return -1;
        return static_cast<unsigned char>(s_[read_pos_]);
    }
    int available() {
        return read_pos_ >= s_.size() ? 0 : static_cast<int>(s_.size() - read_pos_);
    }

    // Assignment from pointer (including null) — matches Arduino semantics.
    // Resets read cursor so reads start from the new content.
    String& operator=(const char* s) { s_ = s ? s : ""; read_pos_ = 0; return *this; }
    String& operator=(const String& o) {
        if (this != &o) { s_ = o.s_; read_pos_ = 0; }
        return *this;
    }

private:
    std::string s_;
    size_t read_pos_ = 0;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}
