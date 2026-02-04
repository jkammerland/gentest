#include "tls_backend.h"

#include <mutex>

namespace coord::tls_backend {

#if COORD_ENABLE_TLS
#if COORD_TLS_BACKEND_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#elif COORD_TLS_BACKEND_WOLFSSL
#include <wolfssl/options.h>
#include <wolfssl/openssl/ssl.h>
#else
#error "COORD_ENABLE_TLS requires COORD_TLS_BACKEND_OPENSSL or COORD_TLS_BACKEND_WOLFSSL"
#endif

static void tls_global_init() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        OPENSSL_init_ssl(0, nullptr);
    });
}

static bool init_tls_ctx(SSL_CTX *ctx, const TlsConfig &cfg, bool is_server, std::string *error) {
    if (!cfg.ca_file.empty()) {
        if (SSL_CTX_load_verify_locations(ctx, cfg.ca_file.c_str(), nullptr) != 1) {
            if (error) *error = "failed to load CA";
            return false;
        }
    }
    if (!cfg.cert_file.empty()) {
        if (SSL_CTX_use_certificate_file(ctx, cfg.cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            if (error) *error = "failed to load certificate";
            return false;
        }
    }
    if (!cfg.key_file.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx, cfg.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            if (error) *error = "failed to load private key";
            return false;
        }
    }
    if (cfg.verify_peer) {
        int mode = SSL_VERIFY_PEER;
        if (is_server) {
            mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
        SSL_CTX_set_verify(ctx, mode, nullptr);
    }
    return true;
}

bool init(void *&ctx_out, void *&ssl_out, int fd, const TlsConfig &cfg, bool is_server, std::string *error) {
    tls_global_init();
    SSL_CTX *ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
    if (!ctx) {
        if (error) *error = "TLS_CTX_new failed";
        return false;
    }
    if (!init_tls_ctx(ctx, cfg, is_server, error)) {
        SSL_CTX_free(ctx);
        return false;
    }
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        if (error) *error = "TLS_new failed";
        return false;
    }
    SSL_set_fd(ssl, fd);
    int rc = is_server ? SSL_accept(ssl) : SSL_connect(ssl);
    if (rc != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        if (error) *error = "TLS handshake failed";
        return false;
    }
    ctx_out = ctx;
    ssl_out = ssl;
    return true;
}

void shutdown(void *&ctx, void *&ssl) {
    if (ssl) {
        SSL_shutdown(reinterpret_cast<SSL *>(ssl));
        SSL_free(reinterpret_cast<SSL *>(ssl));
        ssl = nullptr;
    }
    if (ctx) {
        SSL_CTX_free(reinterpret_cast<SSL_CTX *>(ctx));
        ctx = nullptr;
    }
}

int read(void *ssl, void *buf, std::size_t len, std::string *error) {
    int rc = SSL_read(reinterpret_cast<SSL *>(ssl), buf, static_cast<int>(len));
    if (rc <= 0 && error) {
        *error = "TLS read failed";
    }
    return rc;
}

int write(void *ssl, const void *buf, std::size_t len, std::string *error) {
    int rc = SSL_write(reinterpret_cast<SSL *>(ssl), buf, static_cast<int>(len));
    if (rc <= 0 && error) {
        *error = "TLS write failed";
    }
    return rc;
}
#else
bool init(void *&, void *&, int, const TlsConfig &, bool, std::string *error) {
    if (error) *error = "TLS disabled in this build";
    return false;
}

void shutdown(void *&, void *&) {}

int read(void *, void *, std::size_t, std::string *error) {
    if (error) *error = "TLS disabled in this build";
    return -1;
}

int write(void *, const void *, std::size_t, std::string *error) {
    if (error) *error = "TLS disabled in this build";
    return -1;
}
#endif

} // namespace coord::tls_backend
