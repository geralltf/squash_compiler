#ifndef _OPENSSL_SSL_H
#define _OPENSSL_SSL_H
/* Minimal hand-declared surface of the real system OpenSSL 3.x libssl/
 * libcrypto (linked via "-lssl -lcrypto", resolved the same versioned-.so
 * fallback way "-lvulkan"/"-lX11" already are elsewhere in this project --
 * see linker.c) -- same convention as include/vulkan_xlib.h/pthread.h:
 * hand-matched to the real ABI rather than pulling in the genuine (much
 * larger, macro-heavy) system openssl/ssl.h, which squash's C frontend
 * has known limits parsing. Real TLS via the system's own audited
 * OpenSSL, never hand-rolled crypto -- see SQS/sqs_main.c and
 * SQW/net_client.c, the only two consumers.
 *
 * SSL_CTX_set_min_proto_version() and SSL_set_tlsext_host_name() are real
 * OpenSSL *macros* around the actual exported ctrl functions (SSL_CTX_ctrl/
 * SSL_ctrl), not distinct exported symbols themselves -- redefined here the
 * same way, not as fake standalone functions, so they compile to the exact
 * same real calls a normal C program linking real OpenSSL would make. */
#include "include/stddef.h"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;

extern const SSL_METHOD *TLS_server_method(void);
extern const SSL_METHOD *TLS_client_method(void);
extern SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
extern void SSL_CTX_free(SSL_CTX *ctx);
extern int SSL_CTX_use_certificate_file(SSL_CTX *ctx, const char *file, int type);
extern int SSL_CTX_use_certificate_chain_file(SSL_CTX *ctx, const char *file);
extern int SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx, const char *file, int type);
extern int SSL_CTX_check_private_key(const SSL_CTX *ctx);
extern int SSL_CTX_load_verify_locations(SSL_CTX *ctx, const char *CAfile, const char *CApath);
extern void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *callback);
extern long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg);

extern SSL *SSL_new(SSL_CTX *ctx);
extern void SSL_free(SSL *ssl);
extern int SSL_set_fd(SSL *ssl, int fd);
extern int SSL_accept(SSL *ssl);
extern int SSL_connect(SSL *ssl);
extern int SSL_read(SSL *ssl, void *buf, int num);
extern int SSL_write(SSL *ssl, const void *buf, int num);
extern int SSL_shutdown(SSL *ssl);
extern int SSL_get_error(const SSL *ssl, int ret);
extern long SSL_get_verify_result(const SSL *ssl);
extern long SSL_ctrl(SSL *ssl, int cmd, long larg, void *parg);

#define SSL_FILETYPE_PEM 1

#define SSL_VERIFY_NONE 0
#define SSL_VERIFY_PEER 1

#define SSL_ERROR_NONE             0
#define SSL_ERROR_SSL              1
#define SSL_ERROR_WANT_READ        2
#define SSL_ERROR_WANT_WRITE       3
#define SSL_ERROR_SYSCALL          5
#define SSL_ERROR_ZERO_RETURN      6

#define X509_V_OK 0

/* TLS1_2_VERSION: the floor we pin servers/clients to (see both
 * consumers' SSL_CTX_set_min_proto_version calls) -- "especially secure"
 * per the original request means never negotiating down to the long-
 * deprecated SSLv3/TLS1.0/TLS1.1. */
#define TLS1_2_VERSION 0x0303
#define SSL_CTRL_SET_MIN_PROTO_VERSION 123
#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define TLSEXT_NAMETYPE_host_name 0

#define SSL_CTX_set_min_proto_version(ctx, version) \
    SSL_CTX_ctrl((ctx), SSL_CTRL_SET_MIN_PROTO_VERSION, (version), (void*)0)
#define SSL_set_tlsext_host_name(ssl, name) \
    SSL_ctrl((ssl), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, (void*)(name))

/* libcrypto error-string helpers, for diagnostics only. */
extern unsigned long ERR_get_error(void);
extern void ERR_error_string_n(unsigned long e, char *buf, size_t len);

#endif /* _OPENSSL_SSL_H */
