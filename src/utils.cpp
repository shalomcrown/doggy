#include "utils.h"

#include <cstdio>
#include <string>

// ================================================================================

std::string to_hex(const unsigned char *data, std::size_t length) {
    static const char kHex[] = "0123456789abcdef";
    std::string hex(length * 2, '0');
    for (std::size_t i = 0; i < length; ++i) {
        hex[i * 2] = kHex[data[i] >> 4];
        hex[i * 2 + 1] = kHex[data[i] & 0x0f];
    }

    return hex;
}

// ================================================================================

bool is_hex_digits(const std::string &text) {
    for (const unsigned char c : text) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                || (c >= 'A' && c <= 'F');
        if (ok == false) {
            return false;
        }
    }
    return true;
}

// ================================================================================

bool hashes_equal(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }

    const std::size_t left_n = std::char_traits<char>::length(left);
    const std::size_t right_n = std::char_traits<char>::length(right);
    const std::size_t n = left_n > right_n ? left_n : right_n;
    unsigned char acc = left_n == right_n ? 0 : 1;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char a = i < left_n ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right_n ? static_cast<unsigned char>(right[i]) : 0;
        acc = static_cast<unsigned char>(acc | (a ^ b));
    }

    return acc == 0;
}

// ================================================================================

std::string json_escape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char unicode[7];
                    std::snprintf(unicode, sizeof(unicode), "\\u%04x", c);
                    out += unicode;
                    break;
                }
                out += static_cast<char>(c);
                break;
        }
    }

    return out;
}
