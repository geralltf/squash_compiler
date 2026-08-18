#include <stdio.h>
#include <string.h>
#include "dom_walk.c"
#include "layout.c"

static const char *TEST_HTML =
    "<html><body>"
    "<div class=\"outer\">"
      "<center><p>Hello <span>world</span></p></center>"
      "<a href=\"http://example.com\">link</a>"
      "<img src=\"pic.png\">"
    "</div>"
    "</body></html>";

static const char *kind_name(SqwBoxKind k) {
    switch (k) {
        case SQW_BOX_DIV: return "div";
        case SQW_BOX_CENTER: return "center";
        case SQW_BOX_P: return "p";
        case SQW_BOX_SPAN: return "span";
        case SQW_BOX_A: return "a";
        case SQW_BOX_IMG: return "img";
        default: return "other";
    }
}

int main(void) {
    DomNode *root = dom_parse(TEST_HTML);

    LayoutList list;
    layout_compute(root, 1024.0f, 768.0f, &list);

    fprintf(stderr, "=== SQW layout test ===\n");
    fprintf(stderr, "%d boxes:\n", list.count);
    int i;
    int pass = 1;
    for (i = 0; i < list.count; i++) {
        LayoutBox *b = &list.boxes[i];
        fprintf(stderr, "  [%d] kind=%s x=%d y=%d w=%d h=%d\n",
                i, kind_name(b->kind), (int)b->x, (int)b->y, (int)b->w, (int)b->h);
        if (b->w < 0 || b->h < 0) { fprintf(stderr, "  [FAIL] negative size\n"); pass = 0; }
    }
    if (list.count == 0) { fprintf(stderr, "  [FAIL] no boxes produced\n"); pass = 0; }

    fprintf(stderr, "---\n");
    fprintf(stderr, "%s\n", pass ? "ALL LAYOUT SANITY CHECKS PASSED" : "LAYOUT SANITY CHECKS FAILED");

    layout_list_free(&list);
    dom_free(root);
    return pass ? 0 : 1;
}
