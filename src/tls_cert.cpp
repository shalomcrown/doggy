#include "tls_cert.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ================================================================================

static std::string mbedtls_error(int rc) {
    char buf[128] = {};
    mbedtls_strerror(rc, buf, sizeof(buf));
    return buf;
}

// ================================================================================

static bool write_file(const std::string &path, const char *data, mode_t mode,
                       std::string *error) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        if (error != nullptr) {
            *error = std::string("could not write ") + path + ": " + std::strerror(errno);
        }

        return false;
    }

    const std::size_t n = std::strlen(data);
    const ssize_t wrote = write(fd, data, n);
    if (fchmod(fd, mode) != 0 || wrote != static_cast<ssize_t>(n)) {
        close(fd);
        if (error != nullptr) {
            *error = std::string("could not write ") + path;
        }

        return false;
    }

    close(fd);
    return true;
}

// ================================================================================

static bool generate_self_signed(const std::string &cert_path, const std::string &key_path,
                                 std::string *error) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);

    const char pers[] = "doggy-tls";
    int rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   reinterpret_cast<const unsigned char *>(pers),
                                   sizeof(pers) - 1);
    if (rc != 0) {
        if (error != nullptr) {
            *error = std::string("TLS RNG seed failed: ") + mbedtls_error(rc);
        }

        mbedtls_x509write_crt_free(&crt);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (rc == 0) {
        rc = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg,
                                 2048, 65537);
    }

    if (rc != 0) {
        if (error != nullptr) {
            *error = std::string("TLS key generation failed: ") + mbedtls_error(rc);
        }

        mbedtls_x509write_crt_free(&crt);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    unsigned char serial[16] = {};
    rc = mbedtls_ctr_drbg_random(&ctr_drbg, serial, sizeof(serial));
    serial[0] = static_cast<unsigned char>(serial[0] & 0x7f);
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    }

    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_subject_name(&crt, "CN=doggy");
    }

    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_issuer_name(&crt, "CN=doggy");
    }

    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20460101000000");
    }

    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    }

    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_key_usage(
                &crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    }

    std::vector<unsigned char> cert_pem(8192, 0);
    std::vector<unsigned char> key_pem(8192, 0);
    if (rc == 0) {
        rc = mbedtls_x509write_crt_pem(&crt, cert_pem.data(), cert_pem.size(),
                                       mbedtls_ctr_drbg_random, &ctr_drbg);
    }

    if (rc == 0) {
        rc = mbedtls_pk_write_key_pem(&key, key_pem.data(), key_pem.size());
    }

    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (rc != 0) {
        if (error != nullptr) {
            *error = std::string("TLS certificate write failed: ") + mbedtls_error(rc);
        }

        return false;
    }

    const fs::path key_dir = fs::path(key_path).parent_path();
    const fs::path cert_dir = fs::path(cert_path).parent_path();
    std::error_code ec;
    if (key_dir.empty() == false) {
        fs::create_directories(key_dir, ec);
    }

    if (cert_dir.empty() == false) {
        fs::create_directories(cert_dir, ec);
    }

    if (write_file(key_path, reinterpret_cast<const char *>(key_pem.data()), 0600,
                   error) == false) {
        return false;
    }

    if (write_file(cert_path, reinterpret_cast<const char *>(cert_pem.data()), 0644,
                   error) == false) {
        fs::remove(key_path);
        return false;
    }

    return true;
}

// ================================================================================

bool ensure_self_signed_tls_files(const std::string &cert_path, const std::string &key_path,
                                  std::string *error) {
    if (cert_path.empty() || key_path.empty()) {
        if (error != nullptr) {
            *error = "TLS cert and key paths are required";
        }

        return false;
    }

    const bool have_cert = fs::is_regular_file(cert_path);
    const bool have_key = fs::is_regular_file(key_path);
    if (have_cert && have_key) {
        return true;
    }

    if (have_cert || have_key) {
        if (error != nullptr) {
            *error = "TLS cert/key pair is incomplete";
        }

        return false;
    }

    return generate_self_signed(cert_path, key_path, error);
}
