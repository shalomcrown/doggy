#ifndef TLS_CERT_H
#define TLS_CERT_H

#include <string>

// ================================================================================

bool ensure_self_signed_tls_files(const std::string &cert_path,
                                  const std::string &key_path,
                                  std::string *error);

#endif
