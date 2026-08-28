#include <stdio.h>
#include <string.h>
#include "dom_walk.c"
#include "css.c"
#include "layout.c"

static const char *TEST_HTML =
    "<html><body>"
    "<div class=\"outer\">"
      "<center><p>Hello <span>world</span></p></center>"
      "<a href=\"http://example.com\">link</a>"
      "<img src=\"pic.png\">"
    "</div>"
    "</body></html>";

static const char *TEST_FORM_HTML =
    "<html><body>"
    "<form>"
      "<input type=\"text\" value=\"hi\">"
      "<input type=\"checkbox\" checked>"
      "<input type=\"submit\" value=\"Go\">"
      "<input type=\"hidden\" value=\"secret\">"
      "<textarea>hello\nworld</textarea>"
    "</form>"
    "</body></html>";

static const char *kind_name(SqwBoxKind k) {
    switch (k) {
        case SQW_BOX_DIV: return "div";
        case SQW_BOX_CENTER: return "center";
        case SQW_BOX_P: return "p";
        case SQW_BOX_SPAN: return "span";
        case SQW_BOX_A: return "a";
        case SQW_BOX_IMG: return "img";
        case SQW_BOX_BUTTON: return "button";
        case SQW_BOX_INPUT_TEXT: return "input_text";
        case SQW_BOX_TEXTAREA: return "textarea";
        case SQW_BOX_INPUT_CHECK: return "input_check";
        default: return "other";
    }
}

int main(void) {
    DomNode *root = dom_parse(TEST_HTML);
    /* layout_compute() now reads each node's computed CSS display/box
     * model (see css.h/css.c) -- css_apply() must run first, exactly as
     * sqw_main.c's own sqw_apply_css() does before every real navigation,
     * or every node's css_display stays at its zero-initialized value
     * (which aliases CSS_DISPLAY_BLOCK), making inline elements like <a>/
     * <img> wrongly block-level. An empty stylesheet still resolves each
     * tag's real default display (css_set_default_style()'s own per-tag
     * table), just with no author CSS rules on top of it. */
    CssStylesheet sheet;
    css_stylesheet_init(&sheet);
    css_apply(root, &sheet, 1024.0f);
    css_stylesheet_free(&sheet);

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

    /* --- form controls --- */
    DomNode *froot = dom_parse(TEST_FORM_HTML);
    CssStylesheet fsheet;
    css_stylesheet_init(&fsheet);
    css_apply(froot, &fsheet, 1024.0f);
    css_stylesheet_free(&fsheet);

    LayoutList flist;
    layout_compute(froot, 1024.0f, 768.0f, &flist);

    fprintf(stderr, "\n=== SQW form-control layout test ===\n");
    fprintf(stderr, "%d boxes:\n", flist.count);
    int found_text = 0, found_check = 0, found_button = 0, found_textarea = 0, found_hidden = 0;
    for (i = 0; i < flist.count; i++) {
        LayoutBox *b = &flist.boxes[i];
        fprintf(stderr, "  [%d] kind=%s x=%d y=%d w=%d h=%d\n",
                i, kind_name(b->kind), (int)b->x, (int)b->y, (int)b->w, (int)b->h);
        if (b->w < 0 || b->h < 0) { fprintf(stderr, "  [FAIL] negative size\n"); pass = 0; }
        if (b->kind == SQW_BOX_INPUT_TEXT) found_text = 1;
        if (b->kind == SQW_BOX_INPUT_CHECK) found_check = 1;
        if (b->kind == SQW_BOX_BUTTON) found_button = 1;
        if (b->kind == SQW_BOX_TEXTAREA) found_textarea = 1;
        if (b->node && strcmp(b->node->tag, "input") == 0) {
            const char *t = dom_get_attr(b->node, "type");
            if (t && strcmp(t, "hidden") == 0) found_hidden = 1;
        }
    }
    if (!found_text) { fprintf(stderr, "  [FAIL] no input_text box\n"); pass = 0; }
    if (!found_check) { fprintf(stderr, "  [FAIL] no input_check box\n"); pass = 0; }
    if (!found_button) { fprintf(stderr, "  [FAIL] no button box\n"); pass = 0; }
    if (!found_textarea) { fprintf(stderr, "  [FAIL] no textarea box\n"); pass = 0; }
    if (found_hidden) { fprintf(stderr, "  [FAIL] hidden input got a box\n"); pass = 0; }

    fprintf(stderr, "---\n");
    fprintf(stderr, "%s\n", pass ? "ALL LAYOUT SANITY CHECKS PASSED" : "LAYOUT SANITY CHECKS FAILED");

    layout_list_free(&flist);
    dom_free(froot);
    return pass ? 0 : 1;
}
