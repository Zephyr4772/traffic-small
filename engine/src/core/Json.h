#pragma once
// A tiny streaming JSON writer. Commas are handled automatically.

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace sim {

class JsonWriter {
public:
    std::string str() const { return out_.str(); }

    JsonWriter& beginObject() { separator(); out_ << '{'; first_.push_back(1); return *this; }
    JsonWriter& endObject() { out_ << '}'; first_.pop_back(); return *this; }
    JsonWriter& beginArray() { separator(); out_ << '['; first_.push_back(1); return *this; }
    JsonWriter& endArray() { out_ << ']'; first_.pop_back(); return *this; }

    JsonWriter& key(const std::string& k) {
        separator();
        writeString(k);
        out_ << ':';
        afterKey_ = true;
        return *this;
    }

    JsonWriter& value(double v, int decimals = 2) {
        separator();
        if (!std::isfinite(v)) { out_ << "null"; return *this; }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        if (s == "-0") s = "0";
        out_ << s;
        return *this;
    }
    JsonWriter& value(int v) { separator(); out_ << v; return *this; }
    JsonWriter& value(long long v) { separator(); out_ << v; return *this; }
    JsonWriter& value(unsigned long long v) { separator(); out_ << v; return *this; }
    JsonWriter& value(bool v) { separator(); out_ << (v ? "true" : "false"); return *this; }
    JsonWriter& value(const std::string& v) { separator(); writeString(v); return *this; }
    JsonWriter& value(const char* v) { return value(std::string(v)); }

    template <class T>
    JsonWriter& field(const std::string& k, const T& v) { key(k); return value(v); }

private:
    void separator() {
        if (afterKey_) { afterKey_ = false; return; }
        if (!first_.empty()) {
            if (!first_.back()) out_ << ',';
            first_.back() = 0;
        }
    }

    void writeString(const std::string& s) {
        out_ << '"';
        for (char c : s) {
            switch (c) {
                case '"': out_ << "\\\""; break;
                case '\\': out_ << "\\\\"; break;
                case '\n': out_ << "\\n"; break;
                case '\r': out_ << "\\r"; break;
                case '\t': out_ << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out_ << buf;
                    } else {
                        out_ << c;
                    }
            }
        }
        out_ << '"';
    }

    std::ostringstream out_;
    std::vector<char> first_;
    bool afterKey_ = false;
};

}  // namespace sim
