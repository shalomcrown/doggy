#ifndef UTILS_H
#define UTILS_H

#include <cstddef>
#include <string>

// ================================================================================

std::string to_hex(const unsigned char *data, std::size_t length);

// ================================================================================

bool is_hex_digits(const std::string &text);

// ================================================================================

bool hashes_equal(const char *left, const char *right);

// ================================================================================

// Escapes text for use inside a JSON string literal (quotes, backslash, control chars).
std::string json_escape(const std::string &text);

#endif
