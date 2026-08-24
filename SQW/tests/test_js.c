/* Standalone unit test for js_engine.c -- builds and runs against a real
 * DOM tree (no Vulkan/X11/GPU needed), same convention as test_layout.c/
 * test_dom.c (see either's own top comment). Exercises the interpreter
 * end to end: variables, arithmetic, control flow, functions/closures,
 * and the DOM/console/Math builtins, asserting real observable effects
 * (mutated DOM text, mutated computed style) rather than just "did it
 * crash". */
#include <stdio.h>
#include <string.h>
#include "dom_walk.c"
#include "css.c"
#include "layout.c"
#include "js_engine.c"

static int g_pass = 0, g_fail = 0;

static void check(int cond, const char *what) {
    if (cond) { g_pass++; fprintf(stderr, "  [PASS] %s\n", what); }
    else { g_fail++; fprintf(stderr, "  [FAIL] %s\n", what); }
}

int main(void) {
    fprintf(stderr, "=== SQW JS engine test ===\n");

    /* 1. Arithmetic, string concat, variables, functions, closures, loops --
       verified via a DOM mutation (document.getElementById('out').textContent)
       so the test is checking REAL observable interpreter behavior, not just
       "didn't crash". */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "function add(a, b) { return a + b; }\n"
            "function makeCounter() {\n"
            "  var n = 0;\n"
            "  return function() { n = n + 1; return n; };\n"
            "}\n"
            "var counter = makeCounter();\n"
            "counter(); counter();\n"
            "var lastCount = counter();\n"
            "var sum = add(2, 3) * 2;\n"
            "var s = 'hello' + ' ' + 'world';\n"
            "var total = 0;\n"
            "for (var i = 0; i < 5; i = i + 1) { total = total + i; }\n"
            "var msg = s + ':' + sum + ':' + total + ':' + lastCount;\n"
            "document.getElementById('out').textContent = msg;\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        check(interp != 0, "script parsed and ran (interp non-NULL)");
        check(relayout != 0, "textContent mutation set relayout_needed");

        DomNode *out = js_dom_find_by_id(root, "out");
        check(out != 0, "found #out element");
        char buf[256]; int len = 0;
        if (out) js_append_text_content(out, buf, &len, sizeof buf);
        buf[len] = 0;
        fprintf(stderr, "  textContent = \"%s\"\n", buf);
        /* sum = (2+3)*2 = 10; total = 0+1+2+3+4 = 10; lastCount = 3 */
        check(strcmp(buf, "hello world:10:10:3") == 0, "closures/functions/arithmetic/for-loop/string-concat produced expected text");

        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 2. style.X mutation -- real CSS property change via camelCase JS
       property name, verified by reading the resulting computed style
       field back off the DomNode directly. */
    {
        const char *html = "<html><body><p id=\"p1\">hi</p></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "var el = document.getElementById('p1');\n"
            "el.style.fontSize = '32px';\n"
            "el.style.opacity = '0.5';\n";
        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        check(relayout != 0, "style mutation set relayout_needed");
        DomNode *p1 = js_dom_find_by_id(root, "p1");
        check(p1 != 0, "found #p1 element");
        if (p1) {
            fprintf(stderr, "  font_size=%d opacity_x100=%d\n", (int)p1->css_font_size, (int)(p1->css_opacity * 100.0f));
            check(p1->css_font_size > 31.0f && p1->css_font_size < 33.0f, "style.fontSize mutated computed css_font_size");
            check(p1->css_opacity > 0.49f && p1->css_opacity < 0.51f, "style.opacity mutated computed css_opacity");
        }
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 3. innerHTML mutation -- real DOM tree replacement, verified by
       finding the newly-inserted element and checking ITS text. */
    {
        const char *html = "<html><body><div id=\"box\">old</div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "document.getElementById('box').innerHTML = '<span id=\"s1\">new content</span>';\n";
        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        check(relayout != 0, "innerHTML mutation set relayout_needed");
        /* Re-resolve computed style for whatever the script just inserted
           -- a real page load does this via sqw_apply_css()'s own
           script-then-css_apply ordering (see its comment); this test
           does the same "css_apply AFTER the script ran" step manually
           since it doesn't go through sqw_apply_css() itself. */
        css_apply(root, &sheet);
        DomNode *s1 = js_dom_find_by_id(root, "s1");
        check(s1 != 0, "innerHTML-inserted <span id=s1> found in the DOM");
        if (s1) {
            char buf[128]; int len = 0;
            js_append_text_content(s1, buf, &len, sizeof buf);
            buf[len] = 0;
            fprintf(stderr, "  inserted span text = \"%s\"\n", buf);
            check(strcmp(buf, "new content") == 0, "innerHTML-inserted element has expected text");
        }
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 4. onclick="" HTML attribute -- compiled without any <script> tag
       at all, dispatched via js_dispatch_click(). */
    {
        /* Kept under HTML_MAX_ATTR_LEN (64) -- an attribute value, real
           HTML5 or this project's own parser, is not unbounded. */
        const char *html = "<html><body><button id=\"btn\" onclick=\"document.getElementById('btn').setAttribute('d','y')\">Go</button></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        int relayout = 0;
        JSInterp *interp = js_run_script("", root, &relayout); /* no <script> text at all */
        DomNode *btn = js_dom_find_by_id(root, "btn");
        check(btn != 0, "found #btn element");
        check(btn && btn->js_onclick != 0, "onclick=\"\" attribute was compiled and wired");
        if (btn) {
            int clicked_relayout = 0;
            int dispatched = js_dispatch_click(interp, btn, &clicked_relayout);
            check(dispatched == 1, "js_dispatch_click found and called the handler");
            const char *v = dom_get_attr(btn, "d");
            check(v && !strcmp(v, "y"), "onclick handler's setAttribute call took effect");
        }
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 5. A script with a genuine syntax error must be safely skipped --
       never crash, never partially execute. */
    {
        const char *html = "<html><body></body></html>";
        DomNode *root = dom_parse(html);
        int relayout = 0;
        JSInterp *interp = js_run_script("var x = ;;; totally not valid {{{", root, &relayout);
        check(interp != 0, "a script with a syntax error still returns a usable interp (onclick wiring etc. still ran)");
        js_interp_free(interp);
        dom_free(root);
    }

    fprintf(stderr, "---\n%s (pass=%d fail=%d)\n",
        g_fail == 0 ? "ALL JS ENGINE TESTS PASSED" : "JS ENGINE TESTS FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
