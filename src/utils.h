#ifndef UTILS_H
#define UTILS_H

#include <cstddef>
#include <string>

// ================================================================================

std::string to_hex(const unsigned char *data, std::size_t length);

// ================================================================================

bool hashes_equal(const char *left, const char *right);

#endif
