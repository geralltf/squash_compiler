/* Implementation of net_client.h -- see that header's own comment. #include-d
 * directly into sqw_main.c (not linked as a separate .sqo), same convention
 * as every other SQW/*.c file, for the same documented cross-object-link
 * bug (see sqw_main.c's own comment on vk_context.c et al). */
#include "net_client.h"

typedef struct {
    SqwNetResult *result;
    char url[SQW_NET_URL_MAX];
} SqwNetFetchArgs;

/* Parses "http(s)://HOST[:PORT][/PATH]" into its parts. HOST must be an IPv4
 * dotted-quad literal (e.g. "127.0.0.1") -- there is no DNS resolver here
 * (no getaddrinfo declared anywhere in this project's include/ headers),
 * and every real target in this project (SQS, see SQS/sqs_main.c) is
 * addressed by IP literal for exactly this reason. Returns 0 on an
 * unrecognized scheme. */
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
    return 1;
}

/* Blocking connect + GET + read-to-EOF, run entirely on the background
 * thread sqw_net_fetch_async() spawns -- never touches DOM/layout state,
 * only writes into the heap SqwNetResult the main thread polls. */
static void *sqw_net_worker(void *arg) {
    SqwNetFetchArgs *fa = (SqwNetFetchArgs *)arg;
    SqwNetResult *r = fa->result;
    int is_https = 0, port = 80;
    char host[SQW_NET_HOST_MAX], path[SQW_NET_PATH_MAX];
    int parsed = sqw_net_parse_url(fa->url, &is_https, host, &port, path);
    int success = 0;
    char *body = NULL;
    long body_len = 0;

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
                    /* SQS's cert is a local self-signed dev cert (see
                     * SQS/gen_dev_cert.sh) not chained to any trusted CA --
                     * no CA bundle path exists in this sandbox to verify
                     * against anyway. Local-only test client, so this is an
                     * accepted, documented limitation, not a real-world
                     * default. */
                    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, (void *)0);
                    ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, fd);
                    if (SSL_connect(ssl) <= 0) conn_ok = 0;
                }
                if (conn_ok) {
                    char req[512];
                    int rl = snprintf(req, sizeof req,
                        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
                    if (is_https) SSL_write(ssl, req, rl); else send(fd, req, (unsigned long)rl, 0);

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
    free(fa);
    return NULL;
}

SqwNetResult *sqw_net_fetch_async(const char *url) {
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

    pthread_t th;
    pthread_create(&th, NULL, sqw_net_worker, fa);
    pthread_detach(th);
    return r;
}

void sqw_net_result_free(SqwNetResult *r) {
    pthread_mutex_destroy(&r->mutex);
    if (r->body) free(r->body);
    free(r);
}
