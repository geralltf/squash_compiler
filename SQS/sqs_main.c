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
#include <ctype.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include "php_mini.c"
#include "sqs_dns.c"
#include "include/spoof_identity.h"

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

/* Percent-decodes `in` into `out` (real RFC 3986 %XX decoding -- a bare
 * '%' or a '%' not followed by two real hex digits is left as a literal
 * '%', not treated as an error, matching how real servers tolerate
 * malformed encoding in a path rather than 400ing on it). Never expands
 * (each 3-byte "%XX" collapses to 1 byte), so `out` sized the same as
 * `in`'s own buffer is always enough room. */
static void sqs_url_decode(const char *in, char *out, int outcap) {
    int i = 0, j = 0;
    while (in[i] && j < outcap - 1) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i+1]) && isxdigit((unsigned char)in[i+2])) {
            char hex[3]; hex[0] = in[i+1]; hex[1] = in[i+2]; hex[2] = 0;
            out[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else {
            out[j++] = in[i++];
        }
    }
    out[j] = 0;
}

/* True if `path` ends in ".php", case-insensitively (real filesystems
 * that back this server -- Linux ext4/etc -- are case-sensitive, but a
 * client could still PUT "shell.PHP"/"shell.Php"/etc, and this server's
 * own PHP-detection at GET time (below) needs to actually treat that as
 * PHP for the case-insensitive check on PUT to mean anything -- so this
 * one helper is used for BOTH, not just the PUT-blocking check it was
 * added for. */
static int sqs_path_is_php(const char *path) {
    int plen = (int)strlen(path);
    if (plen <= 4) return 0;
    const char *ext = path + plen - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'p' || ext[1] == 'P') &&
           (ext[2] == 'h' || ext[2] == 'H') &&
           (ext[3] == 'p' || ext[3] == 'P');
}

/* Percent-decodes the request path FIRST, then rejects any resulting ".."
 * (basic path-traversal protection -- "simple but secure" per the project
 * brief) and maps "/" to "index.html". Decoding before validating is the
 * point: checking the raw (still-encoded) bytes for ".." would let
 * "%2e%2e" sail through as a literal, harmless-looking "%2e%2e" path
 * segment that 404s -- but only because this server happens not to decode
 * it anywhere else either. Decode-then-validate is correct regardless of
 * what happens downstream. Returns 1 and fills `out` (size SQS_REQ_BUF)
 * with the real on-disk path on success, 0 if the request path is
 * malformed/unsafe. */
static int sqs_resolve_path(const char *req_path, char *out) {
    if (req_path[0] != '/') return 0;
    const char *rel_raw = req_path + 1;
    if (rel_raw[0] == '\0') rel_raw = "index.html";
    char rel[SQS_REQ_BUF];
    sqs_url_decode(rel_raw, rel, sizeof rel);
    if (strstr(rel, "..") != NULL) return 0;
    snprintf(out, SQS_REQ_BUF, "%s%s", SQS_WWWROOT, rel);
    return 1;
}

/* Every response header this server ever sends -- Content-Type/Length and
 * Connection are protocol-required/needed for the client to parse the
 * response at all, and Server is the SAME compile-time-selected spoofed
 * OS+browser identity SQW sends as its own User-Agent (see
 * include/spoof_identity.h's own comment on why one shared value, not two
 * independently "fake" ones). Nothing else -- no X-Powered-By, no real
 * hostname/version banner, no directory-listing/error-page detail beyond
 * a plain status line -- so this is the only place SQS says anything
 * about what it is, and what it says is deliberately not true. A
 * Server header carrying a browser-shaped string is unusual for a real
 * server (a real one would say "Apache/..."/"nginx/..." instead) but is
 * still a syntactically ordinary HTTP header value (RFC 9110 §10.2.4:
 * product tokens with the same freeform structure a real browser's own
 * User-Agent uses) -- correct and standards-compliant, just not what a
 * real off-the-shelf server would put there, which is exactly the point:
 * nothing here should look like what it actually is. */
static void sqs_send_response(SqsConn *c, int status, const char *status_text,
                               const char *content_type, const char *body, long body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nServer: " SQ_SPOOF_IDENTITY "\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
        status, status_text, content_type, body_len);
    sqs_conn_write(c, header, hlen);
    if (body_len > 0) sqs_conn_write(c, body, body_len);
}

/* Same as sqs_send_response(), but also emits any extra raw response
 * headers PHP code itself set via header()/setcookie() (see PhpState.
 * headers_buf's own comment in php_mini.c) -- one already-formatted
 * "Name: value" line per line of `extra_headers`. If any of those lines
 * is a real "Location:" redirect, the status is upgraded to a real 302
 * (a plain 200 with a Location header is NOT how a real browser follows
 * a redirect) UNLESS the caller already asked for a specific non-200
 * status itself. */
static void sqs_send_response_ex(SqsConn *c, int status, const char *status_text,
                                  const char *content_type, const char *body, long body_len,
                                  const char *extra_headers) {
    if (status == 200 && extra_headers && extra_headers[0] && strstr(extra_headers, "Location:")) {
        status = 302; status_text = "Found";
    }
    char header[512];
    int hlen = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nServer: " SQ_SPOOF_IDENTITY "\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n",
        status, status_text, content_type, body_len);
    sqs_conn_write(c, header, hlen);
    if (extra_headers && extra_headers[0]) {
        /* headers_buf lines are "\n"-terminated (see header()'s own
         * builtin), not the real "\r\n" an HTTP response header line
         * needs -- rewrite each line's terminator when forwarding it. */
        const char *p = extra_headers;
        while (*p) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            if (len > 0) { sqs_conn_write(c, p, len); sqs_conn_write(c, "\r\n", 2); }
            p = nl ? nl + 1 : p + len;
        }
    }
    sqs_conn_write(c, "\r\n", 2);
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
/* Parses a real HTTP "Cookie:" header value ("name1=val1; name2=val2",
 * semicolon-space-separated -- NOT the "&"-separated query-string shape
 * php_parse_kv_string() already handles) into $_COOKIE's own backing
 * PhpKVArray. No URL-decoding (real browsers don't URL-encode ordinary
 * cookie values either, and this project's own cookies -- the login
 * test-cookie, the auth cookie -- never contain characters that would
 * need it; a real edge case, not silently wrong for the cookies this
 * project actually sets/reads). */
static void sqs_parse_cookie_header(const char *hdr, PhpKVArray *arr) {
    const char *p = hdr;
    while (*p) {
        while (*p == ' ') p++;
        const char *semi = strchr(p, ';');
        if (!semi) semi = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(semi - p));
        if (eq) {
            char key[128], val[PHP_VAL_MAX];
            int klen = (int)(eq - p); if (klen >= (int)sizeof key) klen = (int)sizeof key - 1;
            memcpy(key, p, (size_t)klen); key[klen] = 0;
            int vlen = (int)(semi - (eq + 1)); if (vlen >= (int)sizeof val) vlen = (int)sizeof val - 1;
            if (vlen > 0) memcpy(val, eq + 1, (size_t)vlen);
            val[vlen > 0 ? vlen : 0] = 0;
            if (key[0]) php_kv_add(arr, key, val);
        }
        p = *semi ? semi + 1 : semi;
    }
}

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

/* Reads a full request off the connection -- request line + headers +
 * (for POST/PUT) a body sized by its own "Content-Length" header. Unlike
 * the old GET-only version, a single SQS_REQ_BUF recv() is no longer
 * guaranteed to contain the whole thing, so this grows a heap buffer and
 * keeps reading until either the peer closes or the declared
 * Content-Length has actually been received. Returns a malloc'd buffer
 * (caller frees) and sets *out_len; returns NULL on a connection that
 * sends nothing at all. */
/* Caps total request size (headers + body) this server will ever buffer
 * for one request -- found missing via an msfconsole-driven security
 * review: a client could previously send an arbitrary huge
 * "Content-Length" header (or just keep streaming body bytes past any
 * sane size with none at all) and this function would keep doubling
 * `cap`/calling realloc() forever trying to buffer the whole thing
 * before doing anything else with it, a straightforward memory-
 * exhaustion DoS against a server that has no other request-size limit
 * anywhere. 16 MiB is comfortably above anything this test server's own
 * use cases (serving local test pages, PUTting a test file) need. */
#define SQS_MAX_REQUEST (16 * 1024 * 1024)

static char *sqs_read_full_request(SqsConn *c, long *out_len) {
    long cap = SQS_REQ_BUF, len = 0;
    char *req = (char *)malloc((size_t)cap);
    for (;;) {
        if (len >= SQS_MAX_REQUEST) { free(req); return NULL; }
        if (cap - len < 4097) {
            cap *= 2;
            if (cap > SQS_MAX_REQUEST) cap = SQS_MAX_REQUEST + 4097;
            req = (char *)realloc(req, (size_t)cap);
        }
        long n = sqs_conn_read(c, req + len, 4096);
        if (n <= 0) break;
        len += n;
        req[len] = 0;
        char *hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end) {
            char cl_str[32];
            sqs_find_header(req, "Content-Length", cl_str, sizeof cl_str);
            long content_length = atol(cl_str);
            if (content_length < 0) content_length = 0;
            if (content_length > SQS_MAX_REQUEST) { free(req); return NULL; }
            long body_have = len - (long)((hdr_end + 4) - req);
            if (body_have >= content_length) break;
        }
    }
    if (len == 0) { free(req); return NULL; }
    req[len] = 0;
    *out_len = len;
    return req;
}

static void sqs_handle_request(SqsConn *c) {
    long len = 0;
    char *req = sqs_read_full_request(c, &len);
    if (!req) return;

    /* Parse the request line ("METHOD /path?query HTTP/1.1") plus the
     * User-Agent header for console logging -- every other header (besides
     * Content-Length, read above to size the body) is read but otherwise
     * ignored. */
    char method[16]; char path_full[SQS_REQ_BUF];
    method[0] = 0; path_full[0] = 0;
    {
        int i = 0, j = 0;
        while (req[i] && req[i] != ' ' && j < (int)sizeof method - 1) method[j++] = req[i++];
        method[j] = 0;
        while (req[i] == ' ') i++;
        j = 0;
        while (req[i] && req[i] != ' ' && req[i] != '\r' && req[i] != '\n' && j < SQS_REQ_BUF - 1) path_full[j++] = req[i++];
        path_full[j] = 0;
    }
    /* Split "/path?query" into the two parts -- the path resolves to a
     * file on disk (or routes to the PHP interpreter below), the query
     * string becomes $_GET for a .php page (see php_mini.c). */
    char path[SQS_REQ_BUF]; char query[SQS_REQ_BUF];
    {
        const char *q = strchr(path_full, '?');
        if (q) {
            int plen = (int)(q - path_full);
            if (plen >= SQS_REQ_BUF) plen = SQS_REQ_BUF - 1;
            memcpy(path, path_full, (size_t)plen); path[plen] = 0;
            strncpy(query, q + 1, SQS_REQ_BUF - 1); query[SQS_REQ_BUF - 1] = 0;
        } else {
            strncpy(path, path_full, SQS_REQ_BUF - 1); path[SQS_REQ_BUF - 1] = 0;
            query[0] = 0;
        }
    }
    char *hdr_end = strstr(req, "\r\n\r\n");
    char *body = hdr_end ? hdr_end + 4 : req + len;
    long body_len = hdr_end ? len - (long)(body - req) : 0;

    char user_agent[256];
    sqs_find_header(req, "User-Agent", user_agent, sizeof user_agent);
    /* Console output is deliberately request-only -- method, path (with
     * query string), User-Agent, and (right below) the path-resolution
     * outcome -- never the response status/body/headers this server sends
     * back, per the "just show incoming requests" brief. */
    fprintf(stderr, "SQS: %s %s [%s] from %s:%d  User-Agent: %s\n",
        method, path_full, c->is_https ? "https" : "http", c->peer_ip, c->peer_port, user_agent);
    fflush(stderr);

    int method_ok = strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 ||
                     strcmp(method, "PUT") == 0 || strcmp(method, "DELETE") == 0;
    if (!method_ok) {
        sqs_send_response(c, 405, "Method Not Allowed", "text/plain", "method not allowed", 19);
        free(req);
        return;
    }

    char fs_path[SQS_REQ_BUF];
    if (!sqs_resolve_path(path, fs_path)) {
        sqs_send_response(c, 400, "Bad Request", "text/plain", "bad request", 11);
        free(req);
        return;
    }
    /* "/" resolving to "index.html" is the one implicit path rewrite this
     * server does -- the closest thing to a "redirect" this minimal server
     * has (there's no real 3xx Location-header redirect anywhere in it;
     * see this file's own top comment on scope). Logged separately,
     * clearly labeled, so it's not confused with a genuine distinct
     * request. */
    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "SQS: redirect: / -> /index.html\n"); fflush(stderr);
    }

    /* A request path resolving to a real ON-DISK DIRECTORY (e.g. "/blog/"
     * or, critically, any WordPress sub-page reached by directory URL
     * rather than a literal "...index.php" -- exactly how a real browser
     * or WordPress's own internal links navigate) used to fall straight
     * through to fopen(fs_path, "rb") below with NO directory check at
     * all. On Linux, fopen() on a directory SUCCEEDS (glibc doesn't
     * reject it), but the stream is unreadable in the normal sense --
     * this confirmed, via a real crash while testing, to end in a
     * SIGSEGV (NULL-pointer write) somewhere downstream once the rest of
     * this function's request-handling code (Content-Type sniffing/PHP
     * detection/etc, all written assuming a real FILE's worth of bytes)
     * operates on that directory "file". Real web servers instead try a
     * configured index file for a directory request -- this does the
     * same, real-WordPress-relevant subset: try "index.php" first (a
     * WordPress site's real front controller), then "index.html", both
     * confined to the already-validated `fs_path` (sqs_resolve_path()'s
     * own ".." rejection already ran above, so appending a fixed literal
     * filename here can't escape SQS_WWWROOT), falling through to the
     * existing 404 path if neither exists -- never fopen()ing the bare
     * directory path itself. */
    {
        struct stat st;
        if (stat(fs_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char idx_path[SQS_REQ_BUF];
            int base_len = (int)strlen(fs_path);
            int has_slash = base_len > 0 && fs_path[base_len - 1] == '/';
            int found = 0;
            const char *candidates[2] = { "index.php", "index.html" };
            int ci;
            for (ci = 0; ci < 2 && !found; ci++) {
                snprintf(idx_path, sizeof idx_path, "%s%s%s", fs_path, has_slash ? "" : "/", candidates[ci]);
                struct stat ist;
                if (stat(idx_path, &ist) == 0 && S_ISREG(ist.st_mode)) {
                    strncpy(fs_path, idx_path, sizeof fs_path - 1);
                    fs_path[sizeof fs_path - 1] = 0;
                    found = 1;
                }
            }
            if (!found) {
                const char *body404 = "404 not found";
                sqs_send_response(c, 404, "Not Found", "text/plain", body404, (long)strlen(body404));
                free(req);
                return;
            }
        }
    }

    /* PUT/DELETE are RESERVED, not implemented: neither one touches the
     * filesystem at all right now. They used to have real write/delete
     * semantics (confined to SQS_WWWROOT, with a PUT PHP-extension block
     * added on top after an msfconsole-driven security review flagged
     * unauthenticated PUT+auto-executed-.php as a write-then-execute
     * primitive) -- the user then asked, explicitly, for PUT/DELETE to
     * perform NO filesystem writes/deletes at all, kept reserved for a
     * future, specifically-scoped, secure use (e.g. if WordPress itself
     * ever needs one for something -- that would be a deliberate,
     * narrowly-authenticated addition at that point, not a blanket
     * unauthenticated raw-filesystem endpoint like this used to be).
     * Both methods are still accepted at the protocol level (kept in
     * method_ok above, not folded into the generic 405 case) and get a
     * real 501 Not Implemented -- "recognized, currently inert" -- rather
     * than either quietly doing nothing with a misleading 2xx (the
     * original bug this whole block's history starts from) or a flat 405
     * that would suggest they're rejected outright rather than reserved. */
    if (strcmp(method, "PUT") == 0 || strcmp(method, "DELETE") == 0) {
        const char *body501 = "PUT/DELETE are reserved and currently perform no filesystem changes";
        sqs_send_response(c, 501, "Not Implemented", "text/plain", body501, (long)strlen(body501));
        free(req);
        return;
    }

    FILE *fp = fopen(fs_path, "rb");
    if (!fp) {
        const char *body404 = "404 not found";
        sqs_send_response(c, 404, "Not Found", "text/plain", body404, (long)strlen(body404));
        free(req);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    /* "+ 2", not "+ 1", with BOTH trailing bytes zeroed -- see
     * php_mini.c's own require_once buffer allocation for the full
     * explanation: this content buffer is what becomes php_run()'s own
     * `source` for a .php request, and this engine's tokenizer reads a
     * 2-byte lookahead in many places without always checking the first
     * byte for NUL first -- a real heap-buffer-overflow READ, found via
     * fuzzing, that this one extra guaranteed-zero byte closes off
     * regardless of which specific lookahead site would otherwise have
     * hit it. Harmless for the non-PHP static-file-serving path this
     * same buffer is also used for. */
    char *content = (char *)malloc((size_t)sz + 2);
    long got = (long)fread(content, 1, (size_t)sz, fp);
    content[got] = 0;
    content[got + 1] = 0;
    fclose(fp);

    int is_php = sqs_path_is_php(fs_path);
    if (is_php) {
        /* Dynamic page: $_GET from this request's own query string,
         * $_POST from the request body (parsed the same
         * application/x-www-form-urlencoded way real PHP does, regardless
         * of the actual Content-Type -- a deliberate simplification for
         * this test/dev subset, see php_mini.c's own top comment on
         * scope), $_SERVER['REQUEST_METHOD'] from the real HTTP method. */
        PhpKVArray get_arr, post_arr, cookie_arr;
        memset(&get_arr, 0, sizeof get_arr);
        memset(&post_arr, 0, sizeof post_arr);
        memset(&cookie_arr, 0, sizeof cookie_arr);
        php_parse_kv_string(query, &get_arr);
        if (body_len > 0) {
            char *body_cstr = (char *)malloc((size_t)body_len + 1);
            memcpy(body_cstr, body, (size_t)body_len);
            body_cstr[body_len] = 0;
            php_parse_kv_string(body_cstr, &post_arr);
            free(body_cstr);
        }
        {
            char cookie_hdr[PHP_VAL_MAX];
            if (sqs_find_header(req, "Cookie", cookie_hdr, sizeof cookie_hdr)) {
                sqs_parse_cookie_header(cookie_hdr, &cookie_arr);
            }
        }
        char *php_out = (char *)malloc(PHP_OUT_MAX);
        char *php_headers = (char *)malloc(PHP_OUT_MAX);
        php_run(content, fs_path, &get_arr, &post_arr, &cookie_arr, method, php_out, PHP_OUT_MAX, php_headers, PHP_OUT_MAX);
        sqs_send_response_ex(c, 200, "OK", "text/html; charset=utf-8", php_out, (long)strlen(php_out), php_headers);
        free(php_out);
        free(php_headers);
    } else {
        sqs_send_response(c, 200, "OK", "text/html; charset=utf-8", content, got);
    }
    free(content);
    free(req);
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
    /* 127.0.0.1, not INADDR_ANY: this file's own top comment describes SQS
     * as "a minimal LOCAL HTTP + HTTPS test server" -- binding all
     * interfaces contradicted that, and combined with unauthenticated
     * PUT (real filesystem write, confined to SQS_WWWROOT but not
     * otherwise gated) and this server's own PHP execution of any .php
     * file placed there, made it a genuine network-reachable
     * write-then-execute primitive for anyone who could reach the host,
     * not just this machine (found via an msfconsole-driven security
     * review; SQS-DNS's own listener already got this right, binding
     * 127.0.0.1 explicitly -- see sqs_dns.c). */
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
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
        /* 64MB, not the platform default (commonly ~8MB) -- confirmed
         * this session via a real gcc-vs-squash divergence: a genuine
         * real-world WordPress page (twentyseventeen theme's own
         * index.php, once php_mini.c could actually render it correctly
         * at all -- see that file's own top-comment history) segfaulted
         * from a plain stack overflow when served through a squash-
         * compiled SQS, while the IDENTICAL source compiled with gcc
         * rendered it correctly within the default 8MB stack. Root
         * cause: squash's own codegen gives every function a much
         * bigger, unoptimized per-call stack frame than gcc's (no
         * register allocation/spilling tricks to shrink it), so the SAME
         * logical call depth through a real page's full boot chain
         * (get_header()/locate_template()/deeply nested isset()/empty()/
         * "$arr[key]" expression evaluation, ...) consumes dramatically
         * more real stack under squash even though neither build has any
         * actual infinite/runaway recursion -- confirmed via direct
         * bisection (a throwaway harness with the SAME php_mini.c source
         * completed successfully once given a 64MB stack ulimit; smaller
         * limits up to and including 64MB's own neighbors below it still
         * overflowed). 64MB is comfortably above the ~64MB threshold
         * that bisection found necessary, applied here (rather than
         * expecting operators to raise `ulimit -s` for the whole SQS
         * process) since it's set once per accepted connection and costs
         * nothing but virtual address space until actually touched. */
        pthread_attr_setstacksize(&th_attr, 64 * 1024 * 1024);
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

    pthread_t http_thread, https_thread, dns_thread;
    pthread_create(&http_thread, NULL, sqs_accept_loop, &http_args);
    pthread_create(&https_thread, NULL, sqs_accept_loop, &https_args);
    /* Local-only DNS responder (SQS/sqs_dns.c) -- answers the reserved
     * test domain "sqs.test" with 127.0.0.1, purely so SQW's own DNS
     * resolver (SQW/dns_resolver.c) can be exercised end to end without
     * ever making a real DNS query; see that file's own top comment. */
    pthread_create(&dns_thread, NULL, sqs_dns_serve, NULL);

    fprintf(stderr, "SQS: ready\n"); fflush(stdout);
    pthread_join(http_thread, NULL);
    pthread_join(https_thread, NULL);
    pthread_join(dns_thread, NULL);
    return 0;
}
