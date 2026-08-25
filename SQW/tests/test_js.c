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

    /* 6. Arrays, objects, template literals, destructuring, for-of/for-in. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "var arr = [1, 2, 3];\n"
            "arr.push(4);\n"
            "var sum = 0;\n"
            "for (var v of arr) { sum = sum + v; }\n"
            "var doubled = arr.map(function(x) { return x * 2; });\n"
            "var evens = arr.filter(function(x) { return x % 2 == 0; });\n"
            "var obj = { name: 'sqw', count: arr.length };\n"
            "var keys = '';\n"
            "for (var k in obj) { keys = keys + k + ','; }\n"
            "var name = obj.name;\n"
            "var msg = `sum=${sum} doubled=${doubled.join('-')} evens=${evens.join('-')} name=${name} keys=${keys}`;\n"
            "var { name: n2, count: c2 } = obj;\n"
            "var [first, second] = arr;\n"
            "msg = msg + ` n2=${n2} c2=${c2} first=${first} second=${second}`;\n"
            "document.getElementById('out').textContent = msg;\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        char buf[512]; int len = 0;
        if (out) js_append_text_content(out, buf, &len, sizeof buf);
        buf[len] = 0;
        fprintf(stderr, "  arrays/objects/template/destructure = \"%s\"\n", buf);
        check(strcmp(buf, "sum=10 doubled=2-4-6-8 evens=2-4 name=sqw keys=name,count, n2=sqw c2=4 first=1 second=2") == 0,
            "arrays (push/map/filter/join/for-of), object literals/for-in, template literals, and destructuring all produced expected result");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 7. Classes: constructor, methods, inheritance via extends/super(). */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "class Animal {\n"
            "  constructor(name) { this.name = name; }\n"
            "  speak() { return this.name + ' makes a sound'; }\n"
            "}\n"
            "class Dog extends Animal {\n"
            "  constructor(name) { super(name); this.kind = 'dog'; }\n"
            "  speak() { return this.name + ' barks (' + this.kind + ')'; }\n"
            "}\n"
            "var a = new Animal('Generic');\n"
            "var d = new Dog('Rex');\n"
            "document.getElementById('out').textContent = a.speak() + ' | ' + d.speak();\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        char buf[256]; int len = 0;
        if (out) js_append_text_content(out, buf, &len, sizeof buf);
        buf[len] = 0;
        fprintf(stderr, "  classes = \"%s\"\n", buf);
        check(strcmp(buf, "Generic makes a sound | Rex barks (dog)") == 0,
            "class constructor/methods/extends/super() all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 8. try/catch/finally with a real thrown value. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "var log = '';\n"
            "function risky(x) {\n"
            "  if (x < 0) { throw 'negative!'; }\n"
            "  return x * 2;\n"
            "}\n"
            "try {\n"
            "  log = log + risky(5) + ',';\n"
            "  log = log + risky(-1) + ',';\n"
            "  log = log + 'unreached,';\n"
            "} catch (e) {\n"
            "  log = log + 'caught:' + e + ',';\n"
            "} finally {\n"
            "  log = log + 'done';\n"
            "}\n"
            "document.getElementById('out').textContent = log;\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        char buf[256]; int len = 0;
        if (out) js_append_text_content(out, buf, &len, sizeof buf);
        buf[len] = 0;
        fprintf(stderr, "  try/catch/finally = \"%s\"\n", buf);
        check(strcmp(buf, "10,caught:negative!,done") == 0,
            "try/catch/finally correctly stopped at the throw and ran the catch+finally blocks");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 9. DOM traversal/manipulation: querySelector(All), classList,
       children, appendChild/removeChild, createElement/createTextNode. */
    {
        const char *html = "<html><body><ul id=\"list\"><li class=\"item a\">one</li><li class=\"item b\">two</li></ul></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        const char *src =
            "var list = document.getElementById('list');\n"
            "var items = list.querySelectorAll('.item');\n"
            "var count = items.length;\n"
            "var second = document.querySelector('.b');\n"
            "second.classList.add('highlight');\n"
            "var hasHighlight = second.classList.contains('highlight');\n"
            "second.classList.remove('b');\n"
            "var stillHasA = document.querySelector('.a') !== null;\n"
            "var childCount1 = list.children.length;\n"
            "var newLi = document.createElement('li');\n"
            "newLi.appendChild(document.createTextNode('three'));\n"
            "newLi.className = 'item c';\n"
            "list.appendChild(newLi);\n"
            "var childCount2 = list.children.length;\n"
            "var thirdText = list.children[2].textContent;\n"
            "list.removeChild(list.children[0]);\n"
            "var childCount3 = list.children.length;\n"
            "document.getElementById('list').setAttribute('data-result',\n"
            "  count + ':' + hasHighlight + ':' + stillHasA + ':' + childCount1 + ':' + childCount2 + ':' + thirdText + ':' + childCount3);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *list = js_dom_find_by_id(root, "list");
        const char *result = list ? dom_get_attr(list, "data-result") : 0;
        fprintf(stderr, "  DOM traversal/manipulation = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "2:true:true:2:3:three:2"),
            "querySelector(All)/classList/children/appendChild/removeChild/createElement all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 10. setTimeout/setInterval, driven by js_run_timers() the way the
       real render loop would, and localStorage backed by a real file. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet);

        remove("SQW/localstorage.dat"); /* clean slate */

        const char *src =
            "var log = '';\n"
            "setTimeout(function() { log = log + 'A'; }, 100);\n"
            "var ticks = 0;\n"
            "var iv = setInterval(function() {\n"
            "  ticks = ticks + 1;\n"
            "  log = log + 'B';\n"
            "  if (ticks >= 3) {\n"
            "    clearInterval(iv);\n"
            /* Read the final result from INSIDE the last callback --
               "log" only reflects every timer's own mutation once each
               has actually fired, which (unlike the rest of this
               synchronous script) is driven by js_run_timers() calls the
               TEST ITSELF makes later, below -- reading it any earlier
               (e.g. right after registering the timers, before any of
               them have run) would only ever see the empty initial
               value, a mistake in the test's own design, not something
               this comment is claiming is an engine limitation. */
            "    var stored = localStorage.getItem('greeting');\n"
            "    var missing = localStorage.getItem('nope');\n"
            "    document.getElementById('out').setAttribute('data-log', log + ':' + stored + ':' + missing);\n"
            "  }\n"
            "}, 50);\n"
            "localStorage.setItem('greeting', 'hello-storage');\n"
            "localStorage.setItem('greeting', 'updated');\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        /* Simulate the render loop calling js_run_timers() once per
           frame with an advancing wall-clock time -- 5 ticks of 50ms
           covers the interval's own 3 fires plus the one-shot timeout
           at 100ms. */
        double t;
        for (t = 0.0; t <= 250.0; t += 50.0) js_run_timers(interp, t, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *result = out ? dom_get_attr(out, "data-log") : 0;
        fprintf(stderr, "  timers/localStorage = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "BABB:updated:null"),
            "setTimeout/setInterval (driven by js_run_timers) and localStorage.setItem/getItem all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
        remove("SQW/localstorage.dat");
    }

    fprintf(stderr, "---\n%s (pass=%d fail=%d)\n",
        g_fail == 0 ? "ALL JS ENGINE TESTS PASSED" : "JS ENGINE TESTS FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
