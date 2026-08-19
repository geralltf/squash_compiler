/* Implementation of net_client.h -- see that header's own comment. #include-d
 * directly into sqw_main.c (not linked as a separate .sqo), same convention
 * as every other SQW/*.c file, for the same documented cross-object-link
 * bug (see sqw_main.c's own comment on vk_context.c et al). */
#include "net_client.h"
#include "dns_resolver.c"

typedef struct {
    SqwNetResult *result;
    char url[SQW_NET_URL_MAX];
    char method[SQW_NET_METHOD_MAX];
    char *body;      /* heap, owned by this struct; NULL if no body */
    long body_len;
} SqwNetFetchArgs;

/* True if every character is a digit or '.' and there's at least one
 * digit -- enough to distinguish "127.0.0.1" from a real hostname like
 * "example.com" for the routing decision below (not a full IPv4
 * validator -- doesn't check octet ranges/count, which doesn't matter
 * here: inet_addr() is the real parser once a literal is confirmed; this
 * only decides whether to resolve `host` via DNS first). */
static int sqw_net_looks_like_ip_literal(const char *host) {
    int i;
    int saw_digit = 0;
    for (i = 0; host[i]; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') { saw_digit = 1; continue; }
        if (c == '.') continue;
        return 0;
    }
    return saw_digit;
}

/* A plausible, current-looking Microsoft Edge (Chromium-based) UA string --
 * matches real Edge's own format exactly (Mozilla/Chrome/Safari/Edg
 * tokens, in that order, is what a genuine Chromium Edge build sends).
 * Sent on every request and NEVER varies with anything about this actual
 * process (window size, OS, real hostname, SQW's own name/version, etc.)
 * -- see sqw_net_worker()'s own comment on why the request is otherwise
 * held completely static across a live window resize, too. */
#define SQW_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0"

/* Parses "http(s)://HOST[:PORT][/PATH][?QUERY][#FRAGMENT]" into its parts.
 * HOST may be an IPv4 dotted-quad literal (e.g. "127.0.0.1") OR a real
 * hostname (e.g. "example.com") -- sqw_net_worker() below resolves a
 * non-literal host via SQW/dns_resolver.c before connecting. `path` comes
 * out including its own "?query" (a query string is not split out
 * specially -- it's sent to the server exactly as received, inside the
 * request line's path, same as every real HTTP client does; SQS,
 * SQS/sqs_main.c, is the one that splits it back out into $_GET). A
 * trailing "#fragment" is stripped and never included in `path` --
 * fragments are a client-side-only concept in real HTTP, never sent to
 * the server. Returns 0 on an unrecognized scheme. */
static int sqw_net_parse_url(const char *url, int *is_https, char *host, int *port, char *path) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) { *is_https = 1; *port = 443; p += 8; }
    else if (strncmp(p, "http://", 7) == 0) { *is_https = 0; *port = 80; p += 7; }
    else return 0;
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < SQW_NET_HOST_MAX - 1) host[i++] = *p++;
    host[i] = 0;
    if (*p == ':') {
        p++;
        int port_val = 0;
        while (*p >= '0' && *p <= '9') { port_val = port_val * 10 + (*p - '0'); p++; }
        *port = port_val;
    }
    if (*p == '\0') { path[0] = '/'; path[1] = 0; }
    else { strncpy(path, p, SQW_NET_PATH_MAX - 1); path[SQW_NET_PATH_MAX - 1] = 0; }
    {
        char *frag = strchr(path, '#');
        if (frag) *frag = 0;
    }
    return 1;
}

/* Blocking connect + request + read-to-EOF, run entirely on the
 * background thread sqw_net_fetch_async_ex() spawns -- never touches
 * DOM/layout state, only writes into the heap SqwNetResult the main
 * thread polls. */
static void *sqw_net_worker(void *arg) {
    SqwNetFetchArgs *fa = (SqwNetFetchArgs *)arg;
    SqwNetResult *r = fa->result;
    int is_https = 0, port = 80;
    char host[SQW_NET_HOST_MAX], path[SQW_NET_PATH_MAX];
    int parsed = sqw_net_parse_url(fa->url, &is_https, host, &port, path);
    int success = 0;
    char *body = NULL;
    long body_len = 0;

    /* `host` may be a real hostname rather than an IPv4 dotted-quad
     * literal -- resolve it via SQW/dns_resolver.c first, replacing
     * `host` with the resolved literal, so everything below (inet_addr(),
     * the TLS IP-SAN check) keeps working exactly as it already did for
     * literal hosts. Every URL this project's own testing ever fetches
     * uses "127.0.0.1" (a literal), so this branch is never actually
     * taken by anything run in this session -- see dns_resolver.h/c's own
     * top comments. */
    if (parsed && !sqw_net_looks_like_ip_literal(host)) {
        char resolved[SQW_NET_HOST_MAX];
        if (sqw_dns_resolve(host, resolved, sizeof resolved)) {
            strncpy(host, resolved, sizeof host - 1);
            host[sizeof host - 1] = 0;
        } else {
            parsed = 0; /* couldn't resolve -- fail the fetch cleanly */
        }
    }

    if (parsed) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof addr);
            addr.sin_family = AF_INET;
            addr.sin_port = htons((unsigned short)port);
            addr.sin_addr.s_addr = inet_addr(host);
            if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
                SSL_CTX *ctx = NULL;
                SSL *ssl = NULL;
                int conn_ok = 1;
                if (is_https) {
                    ctx = SSL_CTX_new(TLS_client_method());
                    /* Real, proper certificate verification -- prepared
                     * now so this client is ready for real external HTTPS
                     * sites LATER, but per the project brief NOT exercised
                     * against anything but localhost SQS yet (see this
                     * file's own top comment on the IP-literal-only URL
                     * parsing that currently makes a real external host
                     * unreachable through this client regardless).
                     * SQW/ca_certificates.pem is a real copy of this
                     * machine's own system CA bundle (/etc/ssl/certs/
                     * ca-certificates.crt) -- the same Mozilla root
                     * program Firefox itself draws from, just already in
                     * the portable PEM format OpenSSL consumes directly,
                     * rather than Firefox's own cert9.db: that's an NSS
                     * SQLite database holding only USER-imported/
                     * overridden certs, not Firefox's actual builtin root
                     * list (which lives compiled into libnssckbi.so, not
                     * in any file cert9.db-adjacent tooling can export) --
                     * extracting "Firefox's store" from cert9.db would
                     * have silently produced an empty-or-near-empty trust
                     * store, not a working substitute. SQS/dev_cert.pem
                     * (the local dev server's own self-signed cert -- see
                     * SQS/gen_dev_cert.sh) is loaded as a SECOND, additional
                     * trust anchor purely so today's local-only SQS testing
                     * keeps passing full real verification too, without
                     * weakening it back to SSL_VERIFY_NONE. */
                    SSL_CTX_load_verify_locations(ctx, "SQW/ca_certificates.pem", (void *)0);
                    SSL_CTX_load_verify_locations(ctx, "SQS/dev_cert.pem", (void *)0);
                    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, (void *)0);
                    ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, fd);
                    /* IP-literal hostname check (X509_VERIFY_PARAM_set1_ip_asc
                     * via SSL_get0_param, not SSL_set1_host -- see
                     * include/openssl/ssl.h's own comment on why those are
                     * genuinely different verification rules and why the
                     * SSL-level convenience wrapper doesn't exist for IP
                     * literals): `host` is always a dotted-quad per
                     * sqw_net_parse_url()'s own documented limitation,
                     * matched here against the cert's SAN "IP Address"
                     * entries (SQS/gen_dev_cert.sh bakes in
                     * "IP:127.0.0.1" for exactly this). A real DNS
                     * hostname target would need SSL_set1_host() instead,
                     * once this client grows real DNS resolution. */
                    X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host);
                    if (SSL_connect(ssl) <= 0) conn_ok = 0;
                }
                if (conn_ok) {
                    /* Request is built from ONLY the target URL's own
                     * host/path plus a fixed User-Agent string -- nothing
                     * here is ever derived from this process's own state
                     * (window size, whether/how the window was just
                     * resized, real OS/hostname, SQW's own name, thread
                     * ID, timing, etc). A live window resize while a fetch
                     * is in flight touches viewport_w/viewport_h and
                     * triggers a relayout (see main()'s own
                     * SDL_EVENT_WINDOW_RESIZED handling) but NEVER reaches
                     * this function at all -- sqw_net_worker() runs on its
                     * own detached background thread with its own stack,
                     * with no reference to the window/viewport state in
                     * any way, so there is no code path by which a resize
                     * could change what gets sent here, deliberately. */
                    char req[512];
                    int rl;
                    if (fa->body && fa->body_len > 0) {
                        rl = snprintf(req, sizeof req,
                            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " SQW_USER_AGENT
                            "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
                            fa->method, path, host, fa->body_len);
                    } else {
                        rl = snprintf(req, sizeof req,
                            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " SQW_USER_AGENT "\r\nConnection: close\r\n\r\n",
                            fa->method, path, host);
                    }
                    if (is_https) SSL_write(ssl, req, rl); else send(fd, req, (unsigned long)rl, 0);
                    if (fa->body && fa->body_len > 0) {
                        if (is_https) SSL_write(ssl, fa->body, (int)fa->body_len);
                        else send(fd, fa->body, (unsigned long)fa->body_len, 0);
                    }

                    long cap = 65536, len = 0;
                    char *buf = (char *)malloc((size_t)cap);
                    for (;;) {
                        if (cap - len < 4097) { cap *= 2; buf = (char *)realloc(buf, (size_t)cap); }
                        long n = is_https ? (long)SSL_read(ssl, buf + len, 4096)
                                          : recv(fd, buf + len, 4096, 0);
                        if (n <= 0) break;
                        len += n;
                    }
                    if (cap - len < 1) { cap += 1; buf = (char *)realloc(buf, (size_t)cap); }
                    buf[len] = 0;

                    char *hdr_end = strstr(buf, "\r\n\r\n");
                    if (hdr_end) {
                        char *b = hdr_end + 4;
                        long blen = len - (long)(b - buf);
                        body = (char *)malloc((size_t)blen + 1);
                        memcpy(body, b, (size_t)blen);
                        body[blen] = 0;
                        body_len = blen;
                        success = 1;
                    }
                    free(buf);
                }
                if (is_https) {
                    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
                    if (ctx) SSL_CTX_free(ctx);
                }
            }
            close(fd);
        }
    }

    pthread_mutex_lock(&r->mutex);
    r->success = success;
    r->body = body;
    r->body_len = body_len;
    r->ready = 1;
    pthread_mutex_unlock(&r->mutex);
    if (fa->body) free(fa->body);
    free(fa);
    return NULL;
}

SqwNetResult *sqw_net_fetch_async_ex(const char *url, const char *method, const char *body, long body_len) {
    SqwNetResult *r = (SqwNetResult *)malloc(sizeof(SqwNetResult));
    pthread_mutex_init(&r->mutex, NULL);
    r->ready = 0;
    r->success = 0;
    r->body = NULL;
    r->body_len = 0;

    SqwNetFetchArgs *fa = (SqwNetFetchArgs *)malloc(sizeof(SqwNetFetchArgs));
    fa->result = r;
    strncpy(fa->url, url, sizeof fa->url - 1);
    fa->url[sizeof fa->url - 1] = 0;
    strncpy(fa->method, method, sizeof fa->method - 1);
    fa->method[sizeof fa->method - 1] = 0;
    if (body && body_len > 0) {
        fa->body = (char *)malloc((size_t)body_len);
        memcpy(fa->body, body, (size_t)body_len);
        fa->body_len = body_len;
    } else {
        fa->body = NULL;
        fa->body_len = 0;
    }

    pthread_t th;
    pthread_create(&th, NULL, sqw_net_worker, fa);
    pthread_detach(th);
    return r;
}

SqwNetResult *sqw_net_fetch_async(const char *url) {
    return sqw_net_fetch_async_ex(url, "GET", NULL, 0);
}

void sqw_net_result_free(SqwNetResult *r) {
    pthread_mutex_destroy(&r->mutex);
    if (r->body) free(r->body);
    free(r);
}
