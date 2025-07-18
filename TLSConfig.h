#ifndef TLS_CONFIG_H
#define TLS_CONFIG_H

#include <string>
#include <memory>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

struct TLSConfig {
    bool enabled = false;
    bool mutual_tls = false;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    std::string cipher_list = "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS";
    int worker_threads_per_epoll = 4;
};

class TLSManager {
public:
    static TLSManager& getInstance() {
        static TLSManager instance;
        return instance;
    }

    bool initialize(const TLSConfig& config) {
        config_ = config;
        
        if (!config_.enabled) {
            return true;
        }

        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        ctx_ = SSL_CTX_new(TLS_server_method());
        if (!ctx_) {
            return false;
        }

        SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

        if (!config_.cert_file.empty() && !config_.key_file.empty()) {
            if (SSL_CTX_use_certificate_file(ctx_, config_.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
                return false;
            }
            if (SSL_CTX_use_PrivateKey_file(ctx_, config_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
                return false;
            }
            if (!SSL_CTX_check_private_key(ctx_)) {
                return false;
            }
        }

        if (config_.mutual_tls && !config_.ca_file.empty()) {
            if (!SSL_CTX_load_verify_locations(ctx_, config_.ca_file.c_str(), nullptr)) {
                return false;
            }
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        }

        if (!config_.cipher_list.empty()) {
            if (!SSL_CTX_set_cipher_list(ctx_, config_.cipher_list.c_str())) {
                return false;
            }
        }

        return true;
    }

    SSL* createSSL() {
        if (!ctx_) return nullptr;
        return SSL_new(ctx_);
    }

    bool isEnabled() const { return config_.enabled; }
    const TLSConfig& getConfig() const { return config_; }

    ~TLSManager() {
        if (ctx_) {
            SSL_CTX_free(ctx_);
        }
    }

private:
    TLSManager() = default;
    SSL_CTX* ctx_ = nullptr;
    TLSConfig config_;
};

#endif
