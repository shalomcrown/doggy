#include "utils.h"

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
