#include <stdio.h>
#include <string.h>
/* #include, not separate `squash -c`/.sqo compilation+link: linking
 * dom.sqo/html_lexer.sqo/dom_walk.sqo into this test crashes (access
 * violation, no output at all) -- the same class of squash cross-object
 * .sqo-link bug already worked around in SQW/sqw_main.c (see its comment).
 * #include sidesteps it; dom_walk.c pulls in dom.c and html_lexer.c the
 * same way. */
#include "dom_walk.c"

/* Diagnostic output goes to stderr, not stdout: squash's stdout buffering
 * silently drops output that isn't followed by a real flush point when the
 * process is piped/redirected (reproduced directly -- switching these to
 * stderr, matching SQW/sqw_main.c's own convention, was what made any
 * output show up at all). */
static int pass_count = 0;
static int fail_count = 0;

static void check(int cond, const char *name) {
    if (cond) { fprintf(stderr, "  [PASS] %s\n", name); pass_count++; }
    else { fprintf(stderr, "  [FAIL] %s\n", name); fail_count++; }
}

#define VISIT_LOG_MAX 64
typedef struct {
    char tags[VISIT_LOG_MAX][HTML_MAX_TAG_LEN];
    int depths[VISIT_LOG_MAX];
    int count;
} VisitLog;

static void log_visit(DomNode *node, int depth, void *ctx) {
    VisitLog *log = (VisitLog *)ctx;
    if (log->count >= VISIT_LOG_MAX) return;
    if (dom_is_text(node)) {
        strncpy(log->tags[log->count], "#text", HTML_MAX_TAG_LEN - 1);
    } else {
        strncpy(log->tags[log->count], node->tag, HTML_MAX_TAG_LEN - 1);
    }
    log->tags[log->count][HTML_MAX_TAG_LEN - 1] = 0;
    log->depths[log->count] = depth;
    log->count++;
}

/* Expected pre-order DFS sequence (depth in parens), text nodes included;
 * "world" is nested inside span inside p inside center. File-scope, not a
 * local "static const T arr[] = {...}" inside main() -- local static
 * arrays-of-pointers have a real squash codegen bug (sizeof() on one
 * computed 1 instead of 11, and indexing past the first element read back
 * garbage pointers that crashed strcmp -- a follow-up worth root-causing;
 * this file-scope form is confirmed to work correctly instead). */
static const char *expected_tags[] = {
    "html", "body", "div", "center", "p", "#text", "span", "#text",
    "a", "#text", "img"
};
static const int expected_depths[] = {
    0, 1, 2, 3, 4, 5, 5, 6, 3, 4, 3
};
#define N_EXPECTED 11

static const char *TEST_HTML =
    "<html><body>"
    "<div class=\"outer\">"
      "<center><p>Hello <span>world</span></p></center>"
      "<a href=\"http://example.com\">link</a>"
      "<img src=\"pic.png\">"
    "</div>"
    "</body></html>";

int main(void) {
    fprintf(stderr, "=== SQW DOM test suite ===\n");

    DomNode *root = dom_parse(TEST_HTML);
    check(root != 0, "dom_parse returns non-NULL root");
    check(strcmp(root->tag, "#document") == 0, "root tag is #document");
    check(root->child_count == 1, "root has 1 child (html)");

    /* Heap-allocated, not a stack local: VisitLog (64 x 32-byte tag bufs)
     * is ~2KB, the same large-stack-local class of squash codegen issue
     * worked around elsewhere in this project (see SQW/dom.c's dom_parse). */
    VisitLog *log = (VisitLog *)malloc(sizeof(VisitLog));
    log->count = 0;
    dom_walk(root, log_visit, log);

    int n_expected = N_EXPECTED;

    fprintf(stderr, "Visited %d nodes (expected %d):\n", log->count, n_expected);
    int i;
    for (i = 0; i < log->count; i++) {
        fprintf(stderr, "  [%d] depth=%d tag=%s\n", i, log->depths[i], log->tags[i]);
    }

    check(log->count == n_expected, "DFS visited node count matches");

    int order_ok = 1;
    int depth_ok = 1;
    int lim = (log->count < n_expected) ? log->count : n_expected;
    for (i = 0; i < lim; i++) {
        if (strcmp(log->tags[i], expected_tags[i]) != 0) order_ok = 0;
        if (log->depths[i] != expected_depths[i]) depth_ok = 0;
    }
    check(order_ok, "DFS pre-order tag sequence matches expected");
    check(depth_ok, "DFS depth sequence matches expected");

    /* Attribute lookup */
    DomNode *html = root->children[0];
    DomNode *body = html->children[0];
    DomNode *div = body->children[0];
    check(div != 0 && strcmp(div->tag, "div") == 0, "found <div>");
    const char *cls = dom_get_attr(div, "class");
    check(cls != 0 && strcmp(cls, "outer") == 0, "div class attribute == \"outer\"");

    DomNode *center = div->children[0];
    DomNode *a = div->children[1];
    DomNode *img = div->children[2];
    check(center != 0 && strcmp(center->tag, "center") == 0, "found <center>");
    check(a != 0 && strcmp(a->tag, "a") == 0, "found <a>");
    const char *href = a ? dom_get_attr(a, "href") : 0;
    check(href != 0 && strcmp(href, "http://example.com") == 0, "a href attribute correct");
    check(img != 0 && strcmp(img->tag, "img") == 0, "found <img>");
    const char *src = img ? dom_get_attr(img, "src") : 0;
    check(src != 0 && strcmp(src, "pic.png") == 0, "img src attribute correct");
    check(img != 0 && img->child_count == 0, "img (void element) has no children");

    dom_free(root);

    fprintf(stderr, "---\n");
    fprintf(stderr, "pass=%d fail=%d\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
