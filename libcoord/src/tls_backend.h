#pragma once

#include <string>

#include "coord/transport.h"

namespace coord::tls_backend {

bool init(void *&ctx, void *&ssl, SocketHandle fd, const TlsConfig &cfg, bool is_server, std::string *error);
void shutdown(void *&ctx, void *&ssl);
int read(void *ssl, void *buf, std::size_t len, std::string *error);
int write(void *ssl, const void *buf, std::size_t len, std::string *error);

} // namespace coord::tls_backend
