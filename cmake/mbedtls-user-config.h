/*
 * Appended to Mbed TLS's default configuration.
 *
 * cpp-httplib's SSLServer shares one mbedtls_ctr_drbg_context and one private
 * key context across every worker thread. Mbed TLS only locks those contexts
 * when threading support is compiled in; without it, concurrent TLS handshakes
 * corrupt the DRBG state and the server then signs every handshake invalidly
 * until it restarts.
 */

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
