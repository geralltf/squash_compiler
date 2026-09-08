/* Implementation of net_client.h -- see that header's own comment. #include-d
 * directly into sqw_main.c (not linked as a separate .sqo), same convention
 * as every other SQW/*.c file, for the same documented cross-object-link
 * bug (see sqw_main.c's own comment on vk_context.c et al). */
#include "net_client.h"
#include "dns_resolver.c"
#include "include/spoof_identity.h"

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

/* The User-Agent sent on every request -- picked at COMPILE time by
 * include/spoof_identity.h from the platform SQW was actually built for,
 * always claiming a DIFFERENT OS+browser than the real one (see that
 * header's own comment for the exact mapping and why). NEVER varies with
 * anything about this actual running process either (window size, real
 * hostname, SQW's own name/version, etc.) -- see sqw_net_worker()'s own
 * comment on why the request is otherwise held completely static across
 * a live window resize, too. */
#define SQW_USER_AGENT SQ_SPOOF_IDENTITY

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

/* Case-insensitive search for a "Content-Length: N" header within the
 * already-received header block [buf, hdr_end) and returns N, or -1 if the
 * header isn't present/parseable -- used to give sqw_net_worker() a real
 * expected-body-size the instant headers finish arriving, so the loading
 * bar can show a real fraction instead of an indeterminate pulse for any
 * server that sends this (near-universal for a non-chunked response). Only
 * searches within the header block itself (never past hdr_end), so a
 * "Content-Length:"-looking string appearing in the body itself can't be
 * mistaken for the real header. */
static long sqw_net_parse_content_length(const char *buf, const char *hdr_end) {
    const char *p = buf;
    long name_len = (long)strlen("content-length:");
    while (p < hdr_end) {
        const char *line_end = p;
        while (line_end < hdr_end - 1 && !(line_end[0] == '\r' && line_end[1] == '\n')) line_end++;
        if (line_end - p >= name_len) {
            int i, match = 1;
            for (i = 0; i < name_len; i++) {
                char c = p[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != "content-length:"[i]) { match = 0; break; }
            }
            if (match) {
                const char *v = p + name_len;
                while (v < line_end && (*v == ' ' || *v == '\t')) v++;
                long val = 0; int saw_digit = 0;
                while (v < line_end && *v >= '0' && *v <= '9') { val = val * 10 + (*v - '0'); v++; saw_digit = 1; }
                return saw_digit ? val : -1;
            }
        }
        p = line_end + 2;
    }
    return -1;
}

/* Parses the numeric status code out of a response's own first line
 * ("HTTP/1.1 301 Moved Permanently" -> 301). `buf` is already known to
 * start with "HTTP/" (checked by the caller before this is ever called).
 * Returns -1 if the line is malformed/the code isn't there. */
static int sqw_net_parse_status_code(const char *buf) {
    const char *p = buf;
    while (*p && *p != ' ') p++;      /* skip "HTTP/1.1" */
    while (*p == ' ') p++;
    int val = 0; int saw_digit = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; saw_digit = 1; }
    return saw_digit ? val : -1;
}

/* Case-insensitive search for a "<name>: <value>" header within the
 * header block [buf, hdr_end) -- same line-scanning shape as
 * sqw_net_parse_content_length() above, generalized to any header name
 * and a string value (trimmed, NUL-terminated into `out`) instead of a
 * parsed integer. Used for "Location:" (redirect-following, below) --
 * kept general rather than a Location-specific copy in case a later
 * pass needs another header (e.g. Set-Cookie) read the same way.
 * Returns 1 if found (and fits in `outsz`), 0 otherwise. */
static int sqw_net_find_header(const char *buf, const char *hdr_end, const char *name, char *out, size_t outsz) {
    const char *p = buf;
    long name_len = (long)strlen(name);
    while (p < hdr_end) {
        const char *line_end = p;
        while (line_end < hdr_end - 1 && !(line_end[0] == '\r' && line_end[1] == '\n')) line_end++;
        if (line_end - p >= name_len) {
            long i; int match = 1;
            for (i = 0; i < name_len; i++) {
                char c = p[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != name[i]) { match = 0; break; }
            }
            if (match) {
                const char *v = p + name_len;
                while (v < line_end && (*v == ' ' || *v == '\t')) v++;
                long vlen = (long)(line_end - v);
                while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t' || v[vlen - 1] == '\r')) vlen--;
                if ((size_t)vlen + 1 > outsz) return 0;
                memcpy(out, v, (size_t)vlen);
                out[vlen] = 0;
                return 1;
            }
        }
        p = line_end + 2;
    }
    return 0;
}

/* Resolves a "Location:" header value against the URL it was received
 * for -- a real server's redirect target is COMMONLY a full absolute URL
 * (Wikipedia's own "https://en.wikipedia.org" -> 301 with
 * "Location: https://en.wikipedia.org/wiki/Main_Page" is exactly this
 * shape) but the HTTP spec also allows a scheme-relative ("//host/path")
 * or absolute-path ("/path") value, both real and seen in the wild.
 * Deliberately does NOT handle a plain relative path ("path" or
 * "../path", no leading "/") -- rare for a redirect specifically (as
 * opposed to an ordinary in-page link, which sqw_resolve_url()-equivalent
 * logic elsewhere already handles) and not worth the extra path-joining
 * complexity for this pass; falls back to treating it as already-
 * absolute, which will simply fail to parse/connect and end the fetch
 * the same honest way an unresolvable URL always has, not a crash or
 * infinite loop. */
static void sqw_net_resolve_redirect_url(const char *base_url, const char *location, char *out, size_t outsz) {
    if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
        strncpy(out, location, outsz - 1);
        out[outsz - 1] = 0;
        return;
    }
    if (location[0] == '/' && location[1] == '/') {
        /* scheme-relative "//host/path" -- reuse base_url's own scheme */
        int base_is_https = strncmp(base_url, "https://", 8) == 0;
        snprintf(out, outsz, "%s:%s", base_is_https ? "https" : "http", location);
        return;
    }
    if (location[0] == '/') {
        /* absolute-path "/wiki/Main_Page" -- reuse base_url's own scheme+host */
        int is_https = 0, port = 0;
        char host[SQW_NET_HOST_MAX], dummy_path[SQW_NET_PATH_MAX];
        if (sqw_net_parse_url(base_url, &is_https, host, &port, dummy_path)) {
            const char *scheme = is_https ? "https" : "http";
            if ((is_https && port == 443) || (!is_https && port == 80)) {
                snprintf(out, outsz, "%s://%s%s", scheme, host, location);
            } else {
                snprintf(out, outsz, "%s://%s:%d%s", scheme, host, port, location);
            }
            return;
        }
    }
    strncpy(out, location, outsz - 1);
    out[outsz - 1] = 0;
}

/* Blocking connect + request + read-to-EOF, run entirely on the
 * background thread sqw_net_fetch_async_ex() spawns -- never touches
 * DOM/layout state, only writes into the heap SqwNetResult the main
 * thread polls. */
static void *sqw_net_worker(void *arg) {
    SqwNetFetchArgs *fa = (SqwNetFetchArgs *)arg;
    SqwNetResult *r = fa->result;
    int success = 0;
    char *body = NULL;
    long body_len = 0;

    /* Real sites redirect a bare-domain request (see SQW_NET_MAX_REDIRECTS'
     * own comment) -- `current_url` is re-pointed at the "Location:"
     * target and the whole connect/request/read sequence below re-runs
     * against it, up to SQW_NET_MAX_REDIRECTS hops, whenever the response
     * is a real 301/302/303/307/308 with a Location header. */
    char current_url[SQW_NET_URL_MAX];
    strncpy(current_url, fa->url, sizeof current_url - 1);
    current_url[sizeof current_url - 1] = 0;

    int redirect_iter;
    for (redirect_iter = 0; redirect_iter < SQW_NET_MAX_REDIRECTS; redirect_iter++) {
    int is_https = 0, port = 80;
    char host[SQW_NET_HOST_MAX], path[SQW_NET_PATH_MAX];
    int parsed = sqw_net_parse_url(current_url, &is_https, host, &port, path);
    int got_redirect = 0;
    char redirect_to[SQW_NET_URL_MAX];

    /* `host` is kept as the URL's OWN host string for the rest of this
     * function -- the real hostname (e.g. "www.wikipedia.org") if the URL
     * named one, an IP literal otherwise -- since it's needed verbatim
     * for the HTTP Host header and, for a real hostname, TLS SNI and
     * hostname-based certificate verification (see the HTTPS setup
     * below). `connect_ip` is what the TCP connect() actually targets:
     * `host` itself when it's already an IP literal, or the address
     * SQW/dns_resolver.c resolves `host` to otherwise. Conflating the two
     * (overwriting `host` with the resolved IP, an earlier version of
     * this function's own approach) breaks virtual hosting -- a request
     * with "Host: 91.198.174.192" instead of "Host: www.wikipedia.org"
     * either hits the wrong site behind a shared IP or, same as an
     * incorrect/absent SNI, fails certificate verification outright,
     * since the leaf cert a real host like this serves is issued for its
     * DNS name, not for whatever IP it happens to resolve to. */
    char connect_ip[SQW_NET_HOST_MAX];
    if (parsed) {
        if (sqw_net_looks_like_ip_literal(host)) {
            strncpy(connect_ip, host, sizeof connect_ip - 1);
            connect_ip[sizeof connect_ip - 1] = 0;
        } else if (sqw_dns_resolve(host, connect_ip, sizeof connect_ip)) {
            /* resolved -- host itself is left untouched */
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
            addr.sin_addr.s_addr = inet_addr(connect_ip);
            if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
                SSL_CTX *ctx = NULL;
                SSL *ssl = NULL;
                int conn_ok = 1;
                if (is_https) {
                    ctx = SSL_CTX_new(TLS_client_method());
                    /* Real, proper certificate verification -- SQW/ca_
                     * certificates.pem is a real copy of this machine's
                     * own system CA bundle (/etc/ssl/certs/ca-certificates
                     * .crt) -- the same Mozilla root program Firefox
                     * itself draws from, just already in the portable PEM
                     * format OpenSSL consumes directly, rather than
                     * Firefox's own cert9.db: that's an NSS SQLite
                     * database holding only USER-imported/overridden
                     * certs, not Firefox's actual builtin root list (which
                     * lives compiled into libnssckbi.so, not in any file
                     * cert9.db-adjacent tooling can export) -- extracting
                     * "Firefox's store" from cert9.db would have silently
                     * produced an empty-or-near-empty trust store, not a
                     * working substitute. SQS/dev_cert.pem (the local dev
                     * server's own self-signed cert -- see SQS/gen_dev_
                     * cert.sh) is loaded as a SECOND, additional trust
                     * anchor purely so local SQS testing keeps passing
                     * full real verification too, without weakening it
                     * back to SSL_VERIFY_NONE -- it plays no part in
                     * verifying a real site's own, real-CA-issued cert. */
                    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
                    SSL_CTX_load_verify_locations(ctx, "SQW/ca_certificates.pem", (void *)0);
                    SSL_CTX_load_verify_locations(ctx, "SQS/dev_cert.pem", (void *)0);
                    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, (void *)0);
                    ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, fd);
                    /* Two genuinely different verification rules depending
                     * on what kind of `host` this URL actually named (see
                     * include/openssl/ssl.h's own comment on why an IP
                     * literal can't use SSL_set1_host): a dotted-quad
                     * literal (SQS, always -- SQS/gen_dev_cert.sh bakes in
                     * "IP:127.0.0.1" as a SAN "IP Address" entry
                     * specifically for this) is matched via
                     * X509_VERIFY_PARAM_set1_ip_asc; a real hostname
                     * (any actual internet site) is matched via
                     * SSL_set1_host against the cert's SAN "DNS Name"
                     * entries instead -- and ALSO needs SNI
                     * (SSL_set_tlsext_host_name) so a server hosting
                     * multiple sites on one IP (true of essentially every
                     * real HTTPS site today, behind a CDN or otherwise)
                     * serves the right certificate in the first place; SNI
                     * is meaningless for an IP-literal target (there's no
                     * name to indicate) so it's only sent in this branch. */
                    if (sqw_net_looks_like_ip_literal(host)) {
                        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host);
                    } else {
                        SSL_set1_host(ssl, host);
                        SSL_set_tlsext_host_name(ssl, host);
                    }
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
                    /* Real request-line/header size varies a lot in
                     * practice -- a real, sourced-from-a-real-site bug
                     * confirmed this the hard way: MediaWiki's own
                     * load.php combines many CSS modules into one long
                     * query string (400-800+ bytes), which a fixed 512-
                     * byte stack buffer can't hold. snprintf()'s return
                     * value is the length it WOULD have written if
                     * unbounded, not the truncated length actually
                     * stored -- using that as the send() length past a
                     * too-small fixed buffer sent garbage stack memory
                     * as part of the request, corrupting/truncating the
                     * Host header and path and making the server reject
                     * or hang on the request. Sized generously (path can
                     * be up to SQW_NET_PATH_MAX, host up to
                     * SQW_NET_HOST_MAX, plus headers/UA/method) and
                     * heap-allocated so no fixed cap can be exceeded. */
                    int req_cap = SQW_NET_PATH_MAX + SQW_NET_HOST_MAX + SQW_NET_METHOD_MAX + 512;
                    char *req = (char *)malloc((size_t)req_cap);
                    int rl;
                    if (fa->body && fa->body_len > 0) {
                        rl = snprintf(req, (size_t)req_cap,
                            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " SQW_USER_AGENT
                            "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
                            fa->method, path, host, fa->body_len);
                    } else {
                        rl = snprintf(req, (size_t)req_cap,
                            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " SQW_USER_AGENT "\r\nConnection: close\r\n\r\n",
                            fa->method, path, host);
                    }
                    if (rl > req_cap - 1) rl = req_cap - 1;
                    if (is_https) SSL_write(ssl, req, rl); else send(fd, req, (unsigned long)rl, 0);
                    free(req);
                    if (fa->body && fa->body_len > 0) {
                        if (is_https) SSL_write(ssl, fa->body, (int)fa->body_len);
                        else send(fd, fa->body, (unsigned long)fa->body_len, 0);
                    }

                    long cap = 65536, len = 0;
                    char *buf = (char *)malloc((size_t)cap);
                    int too_big = 0;
                    /* Offset (not a pointer -- `buf` itself can move on any
                     * iteration's realloc() above, which would leave a
                     * stored pointer dangling) of the first byte AFTER the
                     * header block's "\r\n\r\n", once it's actually finished
                     * arriving (may take several reads). -1 until then;
                     * everything from here on is counted as BODY progress,
                     * matching content_length/bytes_received's own units
                     * (see SqwNetResult's field comment). */
                    long hdr_end_off = -1;
                    for (;;) {
                        if (len >= SQW_NET_MAX_RESPONSE) { too_big = 1; break; }
                        if (cap - len < 4097) { cap *= 2; buf = (char *)realloc(buf, (size_t)cap); }
                        long n = is_https ? (long)SSL_read(ssl, buf + len, 4096)
                                          : recv(fd, buf + len, 4096, 0);
                        if (n <= 0) break;
                        len += n;
                        if (hdr_end_off < 0) {
                            /* A bounded scan over [0, len), NOT strstr():
                             * `buf` isn't NUL-terminated yet at this point
                             * (that only happens once the whole read loop
                             * ends, below) -- strstr() on it here would
                             * read uninitialized bytes past `len` looking
                             * for one, an out-of-bounds/UB risk for
                             * whatever garbage malloc() handed back. */
                            long si;
                            for (si = 0; si + 4 <= len; si++) {
                                if (buf[si] == '\r' && buf[si+1] == '\n' && buf[si+2] == '\r' && buf[si+3] == '\n') {
                                    hdr_end_off = si + 4;
                                    break;
                                }
                            }
                        }
                        pthread_mutex_lock(&r->mutex);
                        if (hdr_end_off >= 0) {
                            if (r->content_length < 0) {
                                r->content_length = sqw_net_parse_content_length(buf, buf + hdr_end_off - 4);
                            }
                            r->bytes_received = len - hdr_end_off;
                        }
                        pthread_mutex_unlock(&r->mutex);
                    }
                    if (cap - len < 1) { cap += 1; buf = (char *)realloc(buf, (size_t)cap); }
                    buf[len] = 0;

                    /* Require a real HTTP status line before trusting
                     * anything after "\r\n\r\n" as a page body -- found
                     * during this project's own pentest that ANY TCP
                     * responder sending garbage followed by "\r\n\r\n" and
                     * some HTML got silently rendered as if it were a
                     * real HTTP response. Deliberately lenient beyond that
                     * one check (doesn't parse/require a specific status
                     * CODE) -- a real 404/500 page still has a real body
                     * worth showing, same as any real browser). */
                    int looks_like_http = (len >= 5) && strncmp(buf, "HTTP/", 5) == 0;
                    char *hdr_end = (!too_big && looks_like_http) ? strstr(buf, "\r\n\r\n") : NULL;
                    if (hdr_end) {
                        int status = sqw_net_parse_status_code(buf);
                        char location[SQW_NET_URL_MAX];
                        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
                            sqw_net_find_header(buf, hdr_end, "location:", location, sizeof location)) {
                            sqw_net_resolve_redirect_url(current_url, location, redirect_to, sizeof redirect_to);
                            got_redirect = 1;
                        } else {
                            char *b = hdr_end + 4;
                            long blen = len - (long)(b - buf);
                            body = (char *)malloc((size_t)blen + 1);
                            memcpy(body, b, (size_t)blen);
                            body[blen] = 0;
                            body_len = blen;
                            success = 1;
                        }
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

    if (got_redirect) {
        strncpy(current_url, redirect_to, sizeof current_url - 1);
        current_url[sizeof current_url - 1] = 0;
        /* Reset the loading-bar progress fields for the NEW request --
         * otherwise a stale Content-Length/bytes-received from the
         * (empty-bodied, discarded) redirect response would leak into
         * the real fetch's own progress display. */
        pthread_mutex_lock(&r->mutex);
        r->content_length = -1;
        r->bytes_received = 0;
        pthread_mutex_unlock(&r->mutex);
        continue;
    }
    break;
    } /* redirect_iter loop */

    pthread_mutex_lock(&r->mutex);
    r->success = success;
    r->body = body;
    r->body_len = body_len;
    strncpy(r->final_url, current_url, sizeof r->final_url - 1);
    r->final_url[sizeof r->final_url - 1] = 0;
    r->ready = 1;
    /* If the main thread abandoned this fetch while it was still in
     * flight (see SqwNetResult's own "abandoned" field comment and
     * sqw_net_result_abandon()'s), nobody else holds a pointer to `r`
     * any more -- this worker is the only thing that still does, so it's
     * the one that must free it, right here, still under the lock (no
     * other thread can be touching `r` at this point: the main thread
     * gave up its own reference before setting `abandoned`, and no third
     * thread ever gets one). This is the actual fix for a real,
     * reproducible "free(): invalid pointer" crash hit during this
     * project's own live testing -- the OLD code had the main thread
     * free `r` immediately on cancellation, which this exact worker
     * would then still write through after it was already freed. */
    int was_abandoned = r->abandoned;
    pthread_mutex_unlock(&r->mutex);
    if (was_abandoned) {
        pthread_mutex_destroy(&r->mutex);
        if (r->body) free(r->body);
        free(r);
    }
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
    r->abandoned = 0;
    r->content_length = -1;
    r->bytes_received = 0;
    strncpy(r->final_url, url, sizeof r->final_url - 1);
    r->final_url[sizeof r->final_url - 1] = 0;

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

void sqw_net_result_abandon(SqwNetResult *r) {
    pthread_mutex_lock(&r->mutex);
    int already_ready = r->ready;
    if (!already_ready) r->abandoned = 1;
    pthread_mutex_unlock(&r->mutex);
    /* Already finished (its worker thread is done and will never touch
     * `r` again) -- safe to free right here, right now, exactly like
     * sqw_net_result_free() itself. Otherwise leave `r` alone entirely:
     * its still-running worker thread now owns it (via the "abandoned"
     * flag just set) and will free it itself once it finishes -- see
     * that worker's own comment. */
    if (already_ready) {
        pthread_mutex_destroy(&r->mutex);
        if (r->body) free(r->body);
        free(r);
    }
}
