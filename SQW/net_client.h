/* SQW's HTTP/HTTPS client: fetches a page body from a real socket (plain
 * HTTP) or real system OpenSSL (HTTPS), on a background pthread, so the
 * render loop never blocks on network I/O -- see the project plan's
 * "async model" scope decision (real OS thread + mutex handoff, not
 * hand-rolled coroutines). Only ever pointed at SQS (SQS/sqs_main.c) on
 * localhost in this project; URL parsing is deliberately minimal (IP
 * literal host, no DNS resolution) -- see sqw_net_parse_url()'s own
 * comment. */
#ifndef SQW_NET_CLIENT_H
#define SQW_NET_CLIENT_H

#define SQW_NET_HOST_MAX 128
#define SQW_NET_PATH_MAX 256
#define SQW_NET_URL_MAX 384

typedef struct {
    pthread_mutex_t mutex;
    int ready;      /* 1 once the worker thread has finished (success or not) */
    int success;
    char *body;     /* heap, NUL-terminated response body; owned by this struct */
    long body_len;
} SqwNetResult;

/* Starts a fetch of `url` on a new detached background thread and returns
 * immediately with a heap-allocated result handle. Poll `result->ready`
 * (holding `result->mutex`) once per frame; once ready, read `success`/
 * `body`/`body_len` and then call sqw_net_result_free(). */
SqwNetResult *sqw_net_fetch_async(const char *url);
void sqw_net_result_free(SqwNetResult *r);

#endif
