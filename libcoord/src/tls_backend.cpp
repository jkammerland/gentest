#include "tls_backend.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>

namespace coord::tls_backend {

#if COORD_ENABLE_TLS
#if COORD_TLS_BACKEND_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#elif COORD_TLS_BACKEND_WOLFSSL
#include <wolfssl/options.h>
#include <wolfssl/openssl/ssl.h>
#include <wolfssl/openssl/err.h>
#else
#error "COORD_ENABLE_TLS requires COORD_TLS_BACKEND_OPENSSL or COORD_TLS_BACKEND_WOLFSSL"
#endif

static void tls_global_init() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        OPENSSL_init_ssl(0, nullptr);
    });
}

static const char *ssl_error_name(int err) {
    switch (err) {
        case SSL_ERROR_NONE:
            return "none";
        case SSL_ERROR_ZERO_RETURN:
            return "zero_return";
        case SSL_ERROR_WANT_READ:
            return "want_read";
        case SSL_ERROR_WANT_WRITE:
            return "want_write";
        case SSL_ERROR_WANT_CONNECT:
            return "want_connect";
        case SSL_ERROR_WANT_ACCEPT:
            return "want_accept";
        case SSL_ERROR_WANT_X509_LOOKUP:
            return "want_x509_lookup";
        case SSL_ERROR_SYSCALL:
            return "syscall";
        case SSL_ERROR_SSL:
            return "ssl";
        default:
            return "unknown";
    }
}

static std::string format_tls_error(const char *context, SSL *ssl, int rc) {
    int err = SSL_get_error(ssl, rc);
    std::string msg = context;
    msg += " (";
    msg += ssl_error_name(err);
    msg += ")";
    unsigned long lib_err = ERR_get_error();
    if (lib_err != 0) {
        char buf[256];
        ERR_error_string_n(lib_err, buf, sizeof(buf));
        msg += ": ";
        msg += buf;
    } else if (err == SSL_ERROR_SYSCALL && errno != 0) {
        msg += ": ";
        msg += std::strerror(errno);
    }
    return msg;
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
    if (!cfg.cert_file.empty() && !cfg.key_file.empty()) {
        if (SSL_CTX_check_private_key(ctx) != 1) {
            if (error) *error = "private key does not match certificate";
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

bool init(void *&ctx_out, void *&ssl_out, SocketHandle fd, const TlsConfig &cfg, bool is_server, std::string *error) {
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
    if (fd > static_cast<SocketHandle>((std::numeric_limits<int>::max)())) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        if (error) *error = "socket handle out of range for TLS backend";
        return false;
    }
    int ssl_fd = static_cast<int>(fd);
    if (SSL_set_fd(ssl, ssl_fd) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        if (error) *error = "TLS_set_fd failed";
        return false;
    }
    for (;;) {
        ERR_clear_error();
        int rc = is_server ? SSL_accept(ssl) : SSL_connect(ssl);
        if (rc == 1) {
            break;
        }
        int err = SSL_get_error(ssl, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (error) *error = format_tls_error("TLS handshake failed", ssl, rc);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
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
    SSL *ssl_ptr = reinterpret_cast<SSL *>(ssl);
    if (len > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        if (error) *error = "TLS read chunk too large";
        return -1;
    }
    for (;;) {
        ERR_clear_error();
        int rc = SSL_read(ssl_ptr, buf, static_cast<int>(len));
        if (rc > 0) {
            return rc;
        }
        int err = SSL_get_error(ssl_ptr, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (err == SSL_ERROR_SYSCALL && errno == EINTR) {
            continue;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            if (error) *error = "TLS peer closed";
            return 0;
        }
        if (error) {
            *error = format_tls_error("TLS read failed", ssl_ptr, rc);
        }
        return -1;
    }
}

int write(void *ssl, const void *buf, std::size_t len, std::string *error) {
    SSL *ssl_ptr = reinterpret_cast<SSL *>(ssl);
    if (len > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        if (error) *error = "TLS write chunk too large";
        return -1;
    }
    for (;;) {
        ERR_clear_error();
        int rc = SSL_write(ssl_ptr, buf, static_cast<int>(len));
        if (rc > 0) {
            return rc;
        }
        int err = SSL_get_error(ssl_ptr, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (err == SSL_ERROR_SYSCALL && errno == EINTR) {
            continue;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            if (error) *error = "TLS peer closed";
            return 0;
        }
        if (error) {
            *error = format_tls_error("TLS write failed", ssl_ptr, rc);
        }
        return -1;
    }
}
#else
bool init(void *&, void *&, SocketHandle, const TlsConfig &, bool, std::string *error) {
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
