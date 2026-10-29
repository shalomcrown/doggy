#include "utils.h"

#include <fcntl.h>
#include <iostream>
#include <unistd.h>

static int failures = 0;

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

int main() {
    unsigned char empty[] = {0};
    expect(to_hex(empty, 0) == "", "to_hex empty is empty string");

    const unsigned char one[] = {0x0f};
    expect(to_hex(one, 1) == "0f", "to_hex one byte is lowercase");

    const unsigned char two[] = {0x00, 0xff};
    expect(to_hex(two, 2) == "00ff", "to_hex two bytes");

    expect(hashes_equal("abc", "abc"), "hashes_equal same strings");
    expect(hashes_equal("abc", "abd") == false, "hashes_equal different last byte");
    expect(hashes_equal("ab", "abc") == false, "hashes_equal different lengths");
    expect(hashes_equal(nullptr, "abc") == false, "hashes_equal null left");
    expect(hashes_equal("abc", nullptr) == false, "hashes_equal null right");
    expect(is_hex_digits("AaFf09"), "is_hex_digits accepts hex");
    expect(is_hex_digits("zz") == false, "is_hex_digits rejects other letters");

    expect(json_escape("plain text") == "plain text", "json_escape keeps plain text");
    expect(json_escape("say \"hi\"") == "say \\\"hi\\\"", "json_escape escapes quotes");
    expect(json_escape("a\\b") == "a\\\\b", "json_escape escapes backslash");
    expect(json_escape("line\nbreak") == "line\\nbreak", "json_escape escapes newline");
    expect(json_escape(std::string("bell\x07")) == "bell\\u0007",
           "json_escape escapes other control characters");

    const int extra = open("/dev/null", O_RDONLY);
    expect(extra > STDERR_FILENO, "opened a descriptor above stderr");
    close_inherited_descriptors();
    expect(fcntl(extra, F_GETFD) < 0, "close_inherited_descriptors closes it");
    expect(fcntl(STDOUT_FILENO, F_GETFD) >= 0, "stdout stays open");

    if (failures != 0) {
        return 1;
    }

    return 0;
}
