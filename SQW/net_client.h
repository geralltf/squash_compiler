/* SQW's HTTP/HTTPS client: fetches a page body from a real socket (plain
 * HTTP) or real system OpenSSL (HTTPS), on a background pthread, so the
 * render loop never blocks on network I/O -- see the project plan's
 * "async model" scope decision (real OS thread + mutex handoff, not
 * hand-rolled coroutines). Pointed at SQS (SQS/sqs_main.c) on localhost
 * for all of this project's own testing; URL parsing supports a real
 * hostname (via SQW/dns_resolver.c) in addition to an IPv4 dotted-quad
 * literal, plus a query string/fragment -- see sqw_net_parse_url()'s own
 * comment for exactly what's covered. GET/POST/PUT/DELETE are all
 * supported via sqw_net_fetch_async_ex(); sqw_net_fetch_async() is a
 * thin GET-only convenience wrapper kept for existing call sites (local
 * navigation, anchor clicks) that never need a body. */
#ifndef SQW_NET_CLIENT_H
#define SQW_NET_CLIENT_H

#define SQW_NET_HOST_MAX 128
#define SQW_NET_PATH_MAX 256
#define SQW_NET_URL_MAX 384
#define SQW_NET_METHOD_MAX 16
/* Hard cap on a single response's total size (headers + body). Without
 * this, a malicious or just-misbehaving server that never closes its
 * connection and keeps streaming data can grow net_client.c's own read
 * buffer without bound, exhausting memory on the fetching side -- found
 * during this project's own pentest of SQS/SQW (a real server can lie
 * about Content-Length freely; this client never trusted that header for
 * sizing reads anyway, so the only real backstop is a cap on bytes
 * actually received). 32MB is generous for this project's own local test
 * pages while still being far short of "attacker can OOM the process". */
#define SQW_NET_MAX_RESPONSE (32L * 1024 * 1024)

typedef struct {
    pthread_mutex_t mutex;
    int ready;      /* 1 once the worker thread has finished (success or not) */
    int success;
    char *body;     /* heap, NUL-terminated response body; owned by this struct */
    long body_len;
    /* Progress fields, updated by the worker thread under `mutex` as bytes
     * arrive (not just once at the end) -- read by the main thread once per
     * frame while a fetch is outstanding to drive the loading-progress bar
     * (see sqw_main.c's own sqw_draw_loading_bar()). content_length is the
     * response's own "Content-Length" header, parsed as soon as the header
     * block itself has been fully read; -1 until then, and permanently -1
     * for a response that never sends one (e.g. chunked/close-delimited) --
     * the progress bar falls back to an indeterminate animation in that
     * case rather than a real fraction. bytes_received counts BODY bytes
     * only (header bytes don't count), same units as content_length. */
    long content_length;
    long bytes_received;
    /* Set by sqw_net_result_abandon() when the main thread starts a NEW
     * fetch while this one is still in flight (a fast Go-button/anchor
     * click, or -- confirmed as a REAL, reproducible crash during this
     * project's own live testing -- another window-event handler firing
     * a second fetch before the first completed) -- see that function's
     * own comment for why this exists at all: freeing `r` immediately on
     * the main thread the moment it's superseded is a real
     * use-after-free once the background worker thread (still running,
     * with no way to know it's been cancelled) later locks `mutex` and
     * writes through this same, by-then-freed pointer. */
    int abandoned;
} SqwNetResult;

/* Starts a fetch of `url` via `method` ("GET"/"POST"/"PUT"/"DELETE") on a
 * new detached background thread and returns immediately with a
 * heap-allocated result handle. `body`/`body_len` (may be NULL/0) are
 * copied before this returns, sent as the request body with a matching
 * Content-Length. Poll `result->ready` (holding `result->mutex`) once per
 * frame; once ready, read `success`/`body`/`body_len` and then call
 * sqw_net_result_free(). */
SqwNetResult *sqw_net_fetch_async_ex(const char *url, const char *method, const char *body, long body_len);
/* GET-only convenience wrapper: sqw_net_fetch_async_ex(url, "GET", NULL, 0). */
SqwNetResult *sqw_net_fetch_async(const char *url);
void sqw_net_result_free(SqwNetResult *r);
/* Call this instead of sqw_net_result_free() when giving up on a fetch
 * that might still be in flight (starting a replacement fetch before the
 * old one's `ready` was ever observed true) -- see SqwNetResult's own
 * "abandoned" field comment for exactly why sqw_net_result_free() itself
 * isn't safe here. Ownership of `r` transfers to its own background
 * worker thread: if the worker is already done (ready), this frees `r`
 * immediately, same as sqw_net_result_free(); otherwise it just marks
 * `r` abandoned and returns without touching its memory again -- the
 * worker thread frees it itself once it finishes, instead of the usual
 * "caller reads ready/success/body then frees" hand-off. Either way, the
 * caller must not touch `r` again after calling this. */
void sqw_net_result_abandon(SqwNetResult *r);

#endif
