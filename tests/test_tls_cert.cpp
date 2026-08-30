#include "tls_cert.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path()
            / ("doggy-tls-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string cert = (dir / "tls.crt").string();
    const std::string key = (dir / "tls.key").string();

    std::string error;
    expect(ensure_self_signed_tls_files(cert, key, &error), "generate self-signed pair");
    expect(error.empty(), "generate leaves error empty");
    expect(std::filesystem::is_regular_file(cert), "cert file exists");
    expect(std::filesystem::is_regular_file(key), "key file exists");

    std::ifstream cert_in(cert);
    const std::string cert_text{
            std::istreambuf_iterator<char>(cert_in),
            std::istreambuf_iterator<char>()};
    std::ifstream key_in(key);
    const std::string key_text{
            std::istreambuf_iterator<char>(key_in),
            std::istreambuf_iterator<char>()};
    expect(cert_text.find("BEGIN CERTIFICATE") != std::string::npos, "cert is PEM");
    expect(key_text.find("BEGIN") != std::string::npos && key_text.find("KEY") != std::string::npos,
           "key is PEM");

    const auto key_perms = std::filesystem::status(key).permissions();
    const bool group_read =
            (key_perms & std::filesystem::perms::group_read) != std::filesystem::perms::none;
    const bool other_read =
            (key_perms & std::filesystem::perms::others_read) != std::filesystem::perms::none;
    expect(group_read == false && other_read == false, "key is not group/world readable");

    expect(ensure_self_signed_tls_files(cert, key, &error), "existing pair is a no-op");
    std::ifstream cert_again(cert);
    const std::string cert_again_text{
            std::istreambuf_iterator<char>(cert_again),
            std::istreambuf_iterator<char>()};
    expect(cert_again_text == cert_text, "existing cert is not regenerated");

    std::filesystem::remove(key);
    error.clear();
    expect(ensure_self_signed_tls_files(cert, key, &error) == false, "incomplete pair fails");
    expect(error.find("incomplete") != std::string::npos, "incomplete pair names the problem");

    std::filesystem::remove_all(dir);

    if (failures != 0) {
        return 1;
    }

    return 0;
}
