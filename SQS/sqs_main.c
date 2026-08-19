/* SQS (squash server): a minimal local HTTP + HTTPS test server, built to
 * give SQW's net_client.c something real to fetch from. Real BSD sockets
 * (include/sys/socket.h) and real system OpenSSL (include/openssl/ssl.h)
 * for TLS -- never hand-rolled crypto, see that header's own comment.
 * One pthread per accepted connection on each of two listening sockets
 * (plain HTTP and HTTPS on separate ports, run from two more pthreads --
 * avoids fragile TLS-vs-plain-HTTP byte-sniffing on a single port).
 * Serves the exact same local pages SQW's own local multi-page navigation
 * uses (SQW/testpages/*.html), so the identical content can be exercised
 * over file://, http://, and https:// through the one browser. Run from
 * the repo root, matching every other Makefile.*.linux target's own
 * convention (e.g. `./SQS/SQS`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>

#define SQS_HTTP_PORT 8080
#define SQS_HTTPS_PORT 8443
#define SQS_WWWROOT "SQW/testpages/"
#define SQS_REQ_BUF 8192

typedef struct {
    int fd;
    SSL *ssl; /* NULL for plain HTTP connections */
    int is_https;
    char peer_ip[64];
    int peer_port;
} SqsConn;

/* Console visibility into WHO is connecting and WHAT they're asking for --
 * explicitly requests only, never responses/bodies (see sqs_handle_request's
 * own logging, which stops right after the request line + User-Agent).
 * total_connections only ever increments (a running count of every
 * connection accepted since startup); active_connections tracks how many
 * are open RIGHT NOW. Mutex-guarded since every acceptor thread and every
 * per-connection thread touches these concurrently. */
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static long g_total_connections = 0;
static long g_active_connections = 0;

/* Real send()/recv() for plain HTTP, SSL_write()/SSL_read() for HTTPS --
 * every other function in this file only ever calls these two, never the
 * raw socket/SSL calls directly, so the request-handling logic itself
 * doesn't need to know which transport it's on. */
static long sqs_conn_read(SqsConn *c, void *buf, long len) {
    if (c->ssl) return (long)SSL_read(c->ssl, buf, (int)len);
    return recv(c->fd, buf, (unsigned long)len, 0);
}
static long sqs_conn_write(SqsConn *c, const void *buf, long len) {
    if (c->ssl) return (long)SSL_write(c->ssl, buf, (int)len);
    return send(c->fd, buf, (unsigned long)len, 0);
}

/* Rejects any path containing ".." (basic path-traversal protection --
 * "simple but secure" per the project brief) and maps "/" to "index.html".
 * Returns 1 and fills `out` (size SQS_REQ_BUF) with the real on-disk path
 * on success, 0 if the request path is malformed/unsafe. */
static int sqs_resolve_path(const char *req_path, char *out) {
    if (req_path[0] != '/') return 0;
    const char *rel = req_path + 1;
    if (rel[0] == '\0') rel = "index.html";
    if (strstr(rel, "..") != NULL) return 0;
    snprintf(out, SQS_REQ_BUF, "%s%s", SQS_WWWROOT, rel);
    return 1;
}

static void sqs_send_response(SqsConn *c, int status, const char *status_text,
                               const char *content_type, const char *body, long body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
        status, status_text, content_type, body_len);
    sqs_conn_write(c, header, hlen);
    if (body_len > 0) sqs_conn_write(c, body, body_len);
}

/* Case-insensitive search for a "\r\nHeaderName: " line within the raw
 * request buffer, copying its value (up to the trailing \r\n) into `out`.
 * Used only for the User-Agent header -- see sqs_handle_request's own
 * console-logging comment on why (request visibility, not response
 * inspection). Returns 1 and fills out on success, 0 (out set to "(none)")
 * if the header is absent -- a request needn't be, and often isn't, sent
 * by anything that bothers with one at all (curl -A "" style tools, raw
 * netcat testing, etc). */
static int sqs_find_header(const char *req, const char *name, char *out, int outcap) {
    int name_len = (int)strlen(name);
    const char *p = req;
    while ((p = strstr(p, name)) != NULL) {
        /* Must start right after a line boundary (or the very start of the
         * buffer) to avoid matching the header name as a substring of some
         * unrelated header value. */
        if (p != req && !(p[-1] == '\n')) { p++; continue; }
        const char *v = p + name_len;
        if (*v != ':') { p++; continue; }
        v++;
        while (*v == ' ') v++;
        int i = 0;
        while (v[i] && v[i] != '\r' && v[i] != '\n' && i < outcap - 1) { out[i] = v[i]; i++; }
        out[i] = 0;
        return 1;
    }
    strncpy(out, "(none)", outcap - 1);
    out[outcap - 1] = 0;
    return 0;
}

static void sqs_handle_request(SqsConn *c) {
    char req[SQS_REQ_BUF];
    long n = sqs_conn_read(c, req, sizeof req - 1);
    if (n <= 0) return;
    req[n] = 0;

    /* Parse only the request line ("GET /path HTTP/1.1") plus the
     * User-Agent header for console logging -- every other header, and
     * the request body, are read but otherwise ignored (irrelevant for a
     * GET-only static file server). */
    char method[16]; char path[SQS_REQ_BUF];
    method[0] = 0; path[0] = 0;
    {
        int i = 0, j = 0;
        while (req[i] && req[i] != ' ' && j < (int)sizeof method - 1) method[j++] = req[i++];
        method[j] = 0;
        while (req[i] == ' ') i++;
        j = 0;
        while (req[i] && req[i] != ' ' && req[i] != '\r' && req[i] != '\n' && j < SQS_REQ_BUF - 1) path[j++] = req[i++];
        path[j] = 0;
    }
    char user_agent[256];
    sqs_find_header(req, "User-Agent", user_agent, sizeof user_agent);
    /* Console output is deliberately request-only -- method, path, User-
     * Agent, and (right below) the path-resolution outcome -- never the
     * response status/body/headers this server sends back, per the "just
     * show incoming requests" brief. */
    fprintf(stderr, "SQS: %s %s [%s] from %s:%d  User-Agent: %s\n",
        method, path, c->is_https ? "https" : "http", c->peer_ip, c->peer_port, user_agent);
    fflush(stderr);

    if (strcmp(method, "GET") != 0) {
        sqs_send_response(c, 405, "Method Not Allowed", "text/plain", "method not allowed", 19);
        return;
    }

    char fs_path[SQS_REQ_BUF];
    if (!sqs_resolve_path(path, fs_path)) {
        sqs_send_response(c, 400, "Bad Request", "text/plain", "bad request", 11);
        return;
    }
    /* "/" resolving to "index.html" is the one implicit path rewrite this
     * server does -- the closest thing to a "redirect" a GET-only static
     * file server has (there's no real 3xx Location-header redirect
     * anywhere in this minimal server; see this file's own top comment on
     * scope). Logged separately, clearly labeled, so it's not confused
     * with a genuine distinct request. */
    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "SQS: redirect: / -> /index.html\n"); fflush(stderr);
    }

    FILE *fp = fopen(fs_path, "rb");
    if (!fp) {
        const char *body = "404 not found";
        sqs_send_response(c, 404, "Not Found", "text/plain", body, (long)strlen(body));
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    char *content = (char *)malloc((size_t)sz);
    long got = (long)fread(content, 1, (size_t)sz, fp);
    fclose(fp);
    sqs_send_response(c, 200, "OK", "text/html; charset=utf-8", content, got);
    free(content);
}

static void *sqs_conn_thread(void *arg) {
    SqsConn *c = (SqsConn *)arg;
    if (c->ssl) {
        int ar = SSL_accept(c->ssl);
        if (ar <= 0) {
            fprintf(stderr, "SQS: TLS handshake failed (SSL_accept ret=%d err=%d)\n", ar, SSL_get_error(c->ssl, ar)); fflush(stderr);
        } else {
            sqs_handle_request(c);
        }
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    } else {
        sqs_handle_request(c);
    }
    close(c->fd);
    pthread_mutex_lock(&g_stats_mutex);
    g_active_connections--;
    pthread_mutex_unlock(&g_stats_mutex);
    free(c);
    return NULL;
}

static int sqs_listen_on(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { fprintf(stderr, "SQS: socket() failed for port %d\n", port); return -1; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "SQS: bind() failed for port %d\n", port); close(fd); return -1;
    }
    if (listen(fd, 32) != 0) {
        fprintf(stderr, "SQS: listen() failed for port %d\n", port); close(fd); return -1;
    }
    return fd;
}

typedef struct {
    int listen_fd;
    SSL_CTX *ssl_ctx; /* NULL for the plain HTTP acceptor */
} SqsAcceptorArgs;

static void *sqs_accept_loop(void *arg) {
    SqsAcceptorArgs *aa = (SqsAcceptorArgs *)arg;
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof peer;
        int cfd = accept(aa->listen_fd, (struct sockaddr *)&peer, &peerlen);
        if (cfd < 0) continue;
        SqsConn *c = (SqsConn *)malloc(sizeof(SqsConn));
        c->fd = cfd;
        c->ssl = NULL;
        c->is_https = aa->ssl_ctx != ((void*)0);
        inet_ntop(AF_INET, &peer.sin_addr, c->peer_ip, sizeof c->peer_ip);
        c->peer_port = (int)ntohs(peer.sin_port);
        if (aa->ssl_ctx) {
            c->ssl = SSL_new(aa->ssl_ctx);
            SSL_set_fd(c->ssl, cfd);
        }
        long total, active;
        pthread_mutex_lock(&g_stats_mutex);
        g_total_connections++;
        g_active_connections++;
        total = g_total_connections; active = g_active_connections;
        pthread_mutex_unlock(&g_stats_mutex);
        /* Connection-level visibility, separate from the per-request log
         * line sqs_handle_request prints once it's actually parsed a
         * request off this connection -- a connection that never sends a
         * valid request (or fails its TLS handshake) still shows up here. */
        fprintf(stderr, "SQS: connection accepted from %s:%d [%s]  (active=%ld, total=%ld)\n",
            c->peer_ip, c->peer_port, c->is_https ? "https" : "http", active, total);
        fflush(stderr);
        pthread_t th;
        pthread_attr_t th_attr;
        pthread_attr_init(&th_attr);
        pthread_attr_setdetachstate(&th_attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, &th_attr, sqs_conn_thread, c);
        pthread_attr_destroy(&th_attr);
    }
    return NULL;
}

int main(void) {
    fprintf(stderr, "SQS: starting (http :%d, https :%d, wwwroot %s)\n", SQS_HTTP_PORT, SQS_HTTPS_PORT, SQS_WWWROOT); fflush(stdout);

    int http_fd = sqs_listen_on(SQS_HTTP_PORT);
    if (http_fd < 0) return 1;

    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) { fprintf(stderr, "SQS: SSL_CTX_new failed\n"); return 1; }
    /* Real X.509/TLS via the system's own audited OpenSSL, never hand-
     * rolled crypto -- see include/openssl/ssl.h's own comment. Pinned to
     * TLS 1.2+ (never negotiates down to SSLv3/TLS1.0/TLS1.1) per the
     * "especially secure" part of the original brief. */
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_file(ssl_ctx, "SQS/dev_cert.pem", SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "SQS: failed to load SQS/dev_cert.pem -- run SQS/gen_dev_cert.sh first\n"); return 1;
    }
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "SQS/dev_key.pem", SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "SQS: failed to load SQS/dev_key.pem -- run SQS/gen_dev_cert.sh first\n"); return 1;
    }
    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        fprintf(stderr, "SQS: certificate/private key mismatch\n"); return 1;
    }

    int https_fd = sqs_listen_on(SQS_HTTPS_PORT);
    if (https_fd < 0) return 1;

    SqsAcceptorArgs http_args; http_args.listen_fd = http_fd; http_args.ssl_ctx = NULL;
    SqsAcceptorArgs https_args; https_args.listen_fd = https_fd; https_args.ssl_ctx = ssl_ctx;

    pthread_t http_thread, https_thread;
    pthread_create(&http_thread, NULL, sqs_accept_loop, &http_args);
    pthread_create(&https_thread, NULL, sqs_accept_loop, &https_args);

    fprintf(stderr, "SQS: ready\n"); fflush(stdout);
    pthread_join(http_thread, NULL);
    pthread_join(https_thread, NULL);
    return 0;
}
