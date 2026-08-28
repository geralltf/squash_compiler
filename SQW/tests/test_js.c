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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);
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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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
        css_apply(root, &sheet, 1024.0f);

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

    /* 11. Object.keys/values/assign and JSON.stringify/parse. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var obj = { a: 1, b: 'two', c: [3, 4] };\n"
            "var keys = Object.keys(obj).join(',');\n"
            "var values = Object.values(obj).map(function(v) { return '' + v; }).join('|');\n"
            "var merged = Object.assign({}, { x: 1 }, { y: 2, x: 9 });\n"
            "var mergedStr = merged.x + ',' + merged.y;\n"
            "var json = JSON.stringify(obj);\n"
            "var roundtrip = JSON.parse(json);\n"
            "var roundtripStr = roundtrip.a + ':' + roundtrip.b + ':' + roundtrip.c.join('-');\n"
            "document.getElementById('out').setAttribute('data-result',\n"
            "  keys + '||' + values + '||' + mergedStr + '||' + json + '||' + roundtripStr);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *result = out ? dom_get_attr(out, "data-result") : 0;
        fprintf(stderr, "  Object/JSON = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "a,b,c||1|two|3,4||9,2||{\"a\":1,\"b\":\"two\",\"c\":[3,4]}||1:two:3-4"),
            "Object.keys/values/assign and JSON.stringify/parse all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 12. input/change/keydown events (addEventListener + property assignment). */
    {
        const char *html = "<html><body><input id=\"txt\"><input id=\"cb\" type=\"checkbox\"><div id=\"kd\"></div><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "document.getElementById('txt').oninput = function() {\n"
            "  document.getElementById('out').setAttribute('data-input', 'fired');\n"
            "};\n"
            "document.getElementById('txt').addEventListener('change', function() {\n"
            "  document.getElementById('out').setAttribute('data-change-txt', 'fired');\n"
            "});\n"
            "document.getElementById('cb').onchange = function() {\n"
            "  document.getElementById('out').setAttribute('data-change-cb', this.checked ? 'checked' : 'unchecked');\n"
            "};\n"
            "document.getElementById('kd').onkeydown = function(e) {\n"
            "  document.getElementById('out').setAttribute('data-keydown', e.key);\n"
            "};\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *txt = js_dom_find_by_id(root, "txt");
        DomNode *cb = js_dom_find_by_id(root, "cb");
        DomNode *kd = js_dom_find_by_id(root, "kd");
        DomNode *out = js_dom_find_by_id(root, "out");

        int fired_input = js_dispatch_input(interp, txt, &relayout);
        int fired_change_txt = js_dispatch_change(interp, txt, &relayout);
        cb->form_checked = 1;
        int fired_change_cb = js_dispatch_change(interp, cb, &relayout);
        int fired_keydown = js_dispatch_keydown(interp, kd, "Enter", &relayout);
        int no_handler = js_dispatch_input(interp, kd, &relayout);

        const char *in_val = dom_get_attr(out, "data-input");
        const char *change_txt_val = dom_get_attr(out, "data-change-txt");
        const char *change_cb_val = dom_get_attr(out, "data-change-cb");
        const char *keydown_val = dom_get_attr(out, "data-keydown");
        fprintf(stderr, "  events: input=%d/%s change_txt=%d/%s change_cb=%d/%s keydown=%d/%s no_handler=%d\n",
            fired_input, in_val ? in_val : "(null)", fired_change_txt, change_txt_val ? change_txt_val : "(null)",
            fired_change_cb, change_cb_val ? change_cb_val : "(null)", fired_keydown, keydown_val ? keydown_val : "(null)",
            no_handler);
        check(fired_input == 1 && in_val && !strcmp(in_val, "fired")
            && fired_change_txt == 1 && change_txt_val && !strcmp(change_txt_val, "fired")
            && fired_change_cb == 1 && change_cb_val && !strcmp(change_cb_val, "checked")
            && fired_keydown == 1 && keydown_val && !strcmp(keydown_val, "Enter")
            && no_handler == 0,
            "js_dispatch_input/change/keydown correctly called registered handlers (with real 'this'/event.key) and no-op'd when none registered");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 13. String methods, global parseInt/parseFloat/isNaN/Number/String/
       Boolean, and Array.isArray/Array.from, Object.entries/freeze. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        /* HTML_MAX_ATTR_LEN is only 64 bytes (html_lexer.h), so each
           checked result is split across several short attributes rather
           than one long concatenated string. */
        const char *src =
            "var s = '  Hello World  ';\n"
            "var out = document.getElementById('out');\n"
            "out.setAttribute('data-case1', s.trim().toUpperCase() + '|' + s.trim().toLowerCase()\n"
            "  + '|' + s.trim().charAt(1) + '|' + s.trim().charCodeAt(0));\n"
            "out.setAttribute('data-case2', s.trim().indexOf('World') + '|' + s.trim().includes('lo W')\n"
            "  + '|' + s.trim().startsWith('Hello') + '|' + s.trim().endsWith('World'));\n"
            "out.setAttribute('data-case3', s.trim().slice(0, 5) + '|' + s.trim().slice(-5)\n"
            "  + '|' + s.trim().substring(6) + '|' + s.trim().split(' ').join('-'));\n"
            "out.setAttribute('data-case4', s.trim().replace('World', 'There') + '|' + 'ab'.repeat(3)\n"
            "  + '|' + 'a'.concat('b', 'c'));\n"
            "out.setAttribute('data-case5', parseInt('42px') + ',' + parseFloat('3.14abc') + ',' + isNaN(parseInt('xx'))\n"
            "  + ',' + Number('7') + ',' + String(99) + ',' + Boolean(0) + ',' + Boolean('x'));\n"
            "out.setAttribute('data-case6', Array.isArray([1,2]) + ',' + Array.isArray('no') + ','\n"
            "  + Array.from('abc').join('-') + ',' + Array.from([1,2,3], function(v) { return v * 2; }).join('-'));\n"
            "var entries = Object.entries({a:1,b:2});\n"
            "out.setAttribute('data-case7', entries[0][0] + '=' + entries[0][1] + ',' + entries[1][0] + '=' + entries[1][1]);\n"
            "var frozen = Object.freeze({x:5});\n"
            "out.setAttribute('data-case8', '' + frozen.x);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *c1 = out ? dom_get_attr(out, "data-case1") : 0;
        const char *c2 = out ? dom_get_attr(out, "data-case2") : 0;
        const char *c3 = out ? dom_get_attr(out, "data-case3") : 0;
        const char *c4 = out ? dom_get_attr(out, "data-case4") : 0;
        const char *c5 = out ? dom_get_attr(out, "data-case5") : 0;
        const char *c6 = out ? dom_get_attr(out, "data-case6") : 0;
        const char *c7 = out ? dom_get_attr(out, "data-case7") : 0;
        const char *c8 = out ? dom_get_attr(out, "data-case8") : 0;
        fprintf(stderr, "  String/global/Array/Object statics: c1=\"%s\" c2=\"%s\" c3=\"%s\" c4=\"%s\" c5=\"%s\" c6=\"%s\" c7=\"%s\" c8=\"%s\"\n",
            c1 ? c1 : "(null)", c2 ? c2 : "(null)", c3 ? c3 : "(null)", c4 ? c4 : "(null)",
            c5 ? c5 : "(null)", c6 ? c6 : "(null)", c7 ? c7 : "(null)", c8 ? c8 : "(null)");
        check(c1 && !strcmp(c1, "HELLO WORLD|hello world|e|72"), "String toUpperCase/toLowerCase/charAt/charCodeAt correct");
        check(c2 && !strcmp(c2, "6|true|true|true"), "String indexOf/includes/startsWith/endsWith correct");
        check(c3 && !strcmp(c3, "Hello|World|World|Hello-World"), "String slice/substring/split correct");
        check(c4 && !strcmp(c4, "Hello There|ababab|abc"), "String replace/repeat/concat correct");
        check(c5 && !strcmp(c5, "42,3.14,true,7,99,false,true"), "parseInt/parseFloat/isNaN/Number/String/Boolean correct");
        check(c6 && !strcmp(c6, "true,false,a-b-c,2-4-6"), "Array.isArray/Array.from (with and without a map callback) correct");
        check(c7 && !strcmp(c7, "a=1,b=2"), "Object.entries correct");
        check(c8 && !strcmp(c8, "5"), "Object.freeze returns the object unchanged (documented no-op)");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 14. Array.prototype: find/findIndex/some/every/reduce/reverse/
       shift/unshift/splice/concat/flat/sort. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var out = document.getElementById('out');\n"
            "var nums = [3, 1, 4, 1, 5, 9, 2, 6];\n"
            "out.setAttribute('data-case1', nums.find(function(v) { return v > 4; }) + '|'\n"
            "  + nums.findIndex(function(v) { return v > 4; }) + '|'\n"
            "  + nums.some(function(v) { return v > 8; }) + '|' + nums.every(function(v) { return v > 0; }));\n"
            "out.setAttribute('data-case2', nums.reduce(function(a, b) { return a + b; }, 0) + '|'\n"
            "  + nums.reduce(function(a, b) { return a + b; }));\n"
            "var rev = [1, 2, 3]; rev.reverse();\n"
            "var sh = [1, 2, 3]; var shifted = sh.shift();\n"
            "var un = [2, 3]; un.unshift(0, 1);\n"
            "out.setAttribute('data-case3', rev.join(',') + '|' + shifted + ':' + sh.join(',') + '|' + un.join(','));\n"
            "var sp = [1, 2, 3, 4, 5]; var removed = sp.splice(1, 2, 'a', 'b', 'c');\n"
            "out.setAttribute('data-case4', sp.join(',') + '|' + removed.join(','));\n"
            "var c1 = [1, 2].concat([3, 4], 5);\n"
            "var fl = [1, [2, 3], [4, [5, 6]]].flat();\n"
            "out.setAttribute('data-case5', c1.join(',') + '|' + fl.join(','));\n"
            "var sortDefault = [10, 2, 1].sort();\n"
            "var sortCustom = [10, 2, 1].sort(function(a, b) { return a - b; });\n"
            "out.setAttribute('data-case6', sortDefault.join(',') + '|' + sortCustom.join(','));\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *c1 = out ? dom_get_attr(out, "data-case1") : 0;
        const char *c2 = out ? dom_get_attr(out, "data-case2") : 0;
        const char *c3 = out ? dom_get_attr(out, "data-case3") : 0;
        const char *c4 = out ? dom_get_attr(out, "data-case4") : 0;
        const char *c5 = out ? dom_get_attr(out, "data-case5") : 0;
        const char *c6 = out ? dom_get_attr(out, "data-case6") : 0;
        fprintf(stderr, "  Array.prototype: c1=\"%s\" c2=\"%s\" c3=\"%s\" c4=\"%s\" c5=\"%s\" c6=\"%s\"\n",
            c1 ? c1 : "(null)", c2 ? c2 : "(null)", c3 ? c3 : "(null)", c4 ? c4 : "(null)",
            c5 ? c5 : "(null)", c6 ? c6 : "(null)");
        check(c1 && !strcmp(c1, "5|4|true|true"), "Array find/findIndex/some/every correct");
        check(c2 && !strcmp(c2, "31|31"), "Array reduce (with and without an initial value) correct");
        check(c3 && !strcmp(c3, "3,2,1|1:2,3|0,1,2,3"), "Array reverse/shift/unshift correct");
        check(c4 && !strcmp(c4, "1,a,b,c,4,5|2,3"), "Array splice (remove+insert, and its own removed-elements return value) correct");
        check(c5 && !strcmp(c5, "1,2,3,4,5|1,2,3,4,5,6"), "Array concat and flat correct");
        check(c6 && !strcmp(c6, "1,10,2|1,2,10"), "Array sort (default string-order vs a real comparator) correct");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 15. Unbounded recursion (function AND constructor) is stopped by a
       real call-depth cap instead of overflowing the native C stack --
       raised as a real, catchable throw, matching real JS's own
       RangeError: Maximum call stack size exceeded. Found via this
       session's own fuzz testing (js_engine.c had no such guard before). */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var out = document.getElementById('out');\n"
            "function recurse(n) { return recurse(n + 1); }\n"
            "var caught1 = 'no';\n"
            "try { recurse(0); } catch (e) { caught1 = 'yes:' + e; }\n"
            "class Deep { constructor() { new Deep(); } }\n"
            "var caught2 = 'no';\n"
            "try { new Deep(); } catch (e) { caught2 = 'yes:' + e; }\n"
            "out.setAttribute('data-caught1', caught1);\n"
            "out.setAttribute('data-caught2', caught2);\n"
            "out.setAttribute('data-after', 'still-running');\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *caught1 = out ? dom_get_attr(out, "data-caught1") : 0;
        const char *caught2 = out ? dom_get_attr(out, "data-caught2") : 0;
        const char *after = out ? dom_get_attr(out, "data-after") : 0;
        fprintf(stderr, "  recursion guard: caught1=\"%s\" caught2=\"%s\" after=\"%s\"\n",
            caught1 ? caught1 : "(null)", caught2 ? caught2 : "(null)", after ? after : "(null)");
        check(caught1 && !strncmp(caught1, "yes:", 4), "unbounded function recursion was stopped and thrown as a catchable exception");
        check(caught2 && !strncmp(caught2, "yes:", 4), "unbounded constructor recursion was stopped and thrown as a catchable exception");
        check(after && !strcmp(after, "still-running"), "the interpreter kept running normally after both recursion limits were hit");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 16. Deeply nested source text (parens/unary chains) is rejected as
       a parse error instead of overflowing the parser's own native C
       stack -- found via this session's own fuzz testing (js_parse_
       primary()/js_parse_unary() had no depth guard before). */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        char deep_parens[2000]; int i, n = 0;
        strcpy(deep_parens + n, "var x = "); n += (int)strlen(deep_parens + n);
        for (i = 0; i < 900 && n < 990; i++) deep_parens[n++] = '(';
        deep_parens[n++] = '1';
        for (i = 0; i < 900 && n < 1998; i++) deep_parens[n++] = ')';
        deep_parens[n++] = ';'; deep_parens[n] = 0;

        char deep_unary[2000]; n = 0;
        strcpy(deep_unary + n, "var y = "); n += (int)strlen(deep_unary + n);
        for (i = 0; i < 900 && n < 1998; i++) deep_unary[n++] = '!';
        deep_unary[n++] = 'x'; deep_unary[n++] = ';'; deep_unary[n] = 0;

        /* js_run_script() never returns NULL even on a parse error (it
           still installs builtins/wires onclick="" attributes and just
           skips running the unparseable script -- see its own code); the
           real thing under test here is simply that reaching this line
           at all means the parser did NOT crash the process. */
        int relayout = 0;
        JSInterp *interp1 = js_run_script(deep_parens, root, &relayout);
        check(interp1 != 0, "900-deep nested parens rejected as a parse error, not a native stack overflow");
        if (interp1) js_interp_free(interp1);

        JSInterp *interp2 = js_run_script(deep_unary, root, &relayout);
        check(interp2 != 0, "900-deep unary '!' chain rejected as a parse error, not a native stack overflow");
        if (interp2) js_interp_free(interp2);

        /* The interpreter must keep working normally on ordinary scripts
           right after rejecting a too-deep one (no corrupted global
           parser/interp state left behind). */
        JSInterp *interp3 = js_run_script("document.getElementById('out').setAttribute('data-ok', 'yes');", root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *ok = out ? dom_get_attr(out, "data-ok") : 0;
        check(ok && !strcmp(ok, "yes"), "interpreter still works normally on an ordinary script after rejecting deep nesting");
        if (interp3) js_interp_free(interp3);

        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 17. A runaway loop (a script bug, e.g. a typo'd increment variable
       that never advances the real loop condition) is stopped quickly and
       cheaply instead of leaking gigabytes of memory before its iteration
       cap is ever reached -- found via this session's own fuzz testing:
       almost any loop body leaks a fresh, uncollected JSEnv (~6.7KB) per
       iteration (this engine's own documented "no GC" tradeoff), and the
       ORIGINAL 2,000,000-iteration cap was high enough for that alone to
       reliably exhaust memory and get the whole host process OOM-killed. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var out = document.getElementById('out');\n"
            "for (var i = 0; i < 10; ii++) { if (i == 5) break; }\n" /* real typo'd bug: "ii" instead of "i", i never advances */
            "out.setAttribute('data-ok', 'still-running');\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *ok = out ? dom_get_attr(out, "data-ok") : 0;
        check(ok && !strcmp(ok, "still-running"), "a runaway loop was stopped by the iteration cap and the rest of the script still ran");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 18. Writing to a huge array index (e.g. a script bug computing a
       bad numeric index, or a hostile page doing it deliberately) is
       silently refused instead of realloc()'ing an unreasonable amount of
       memory and then segfaulting through the NULL that failed realloc()
       left behind -- a real, confirmed crash found via this session's own
       manual security audit (js_array_ensure_cap()/js_array_set() had no
       upper bound before, and never checked realloc()'s own return
       value). */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var out = document.getElementById('out');\n"
            "var a = [];\n"
            "a[999999999] = 1;\n"
            "var b = [1,2,3];\n"
            "b.splice(2000000000, 0, 'x');\n"
            "var c = []; for (var i = 0; i < 5; i++) c.unshift(i);\n"
            "out.setAttribute('data-len', '' + a.length);\n"
            "out.setAttribute('data-b', b.join(','));\n"
            "out.setAttribute('data-c', c.join(','));\n"
            "out.setAttribute('data-ok', 'still-running');\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *ok = out ? dom_get_attr(out, "data-ok") : 0;
        const char *c_val = out ? dom_get_attr(out, "data-c") : 0;
        fprintf(stderr, "  huge array index: ok=\"%s\" c=\"%s\"\n", ok ? ok : "(null)", c_val ? c_val : "(null)");
        check(ok && !strcmp(ok, "still-running"), "writing to a huge array index (and splice with a huge start) did not crash, and the rest of the script still ran");
        check(c_val && !strcmp(c_val, "4,3,2,1,0"), "ordinary array mutation (unshift in a small loop) still works correctly after the huge-index guard was added");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 19. get/set accessors -- both object-literal shorthand and class
       body form, including real "this" binding inside each. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var obj = { _x: 5, get x() { return this._x * 2; }, set x(v) { this._x = v + 1; } };\n"
            "var a = obj.x;\n"
            "obj.x = 10;\n"
            "var b = obj.x;\n"
            "class Circle {\n"
            "  constructor(r) { this._r = r; }\n"
            "  get area() { return this._r * this._r; }\n"
            "  set radius(v) { this._r = v; }\n"
            "}\n"
            "var c = new Circle(2);\n"
            "var areaBefore = c.area;\n"
            "c.radius = 3;\n"
            "var areaAfter = c.area;\n"
            "document.getElementById('out').setAttribute('data-r', a + ':' + b + ':' + areaBefore + ':' + areaAfter);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *result = out ? dom_get_attr(out, "data-r") : 0;
        fprintf(stderr, "  get/set accessors = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "10:22:4:9"),
            "object-literal and class get/set accessors both worked correctly, including real 'this' binding");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 20. Promises: new Promise/resolve/reject, .then chaining, .catch,
       .finally passthrough, Promise.resolve/reject/all. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var log = '';\n"
            "var p = new Promise(function(resolve, reject) { resolve(42); });\n"
            "p.then(function(v) { log += 'A:' + v + ';'; return v + 1; })\n"
            " .then(function(v) { log += 'B:' + v + ';'; });\n"
            "Promise.resolve(7).then(function(v) { log += 'C:' + v + ';'; });\n"
            "Promise.reject('bad').catch(function(e) { log += 'D:' + e + ';'; });\n"
            "new Promise(function(resolve, reject) { reject('oops'); })\n"
            "  .then(function(v) { log += 'NOPE;'; })\n"
            "  .catch(function(e) { log += 'E:' + e + ';'; });\n"
            "Promise.all([1, Promise.resolve(2), 3]).then(function(vs) { log += 'F:' + vs.join(',') + ';'; });\n"
            "var finallyRan = false;\n"
            "Promise.resolve(9).finally(function() { finallyRan = true; }).then(function(v) { log += 'G:' + v + ':' + finallyRan + ';'; });\n"
            "document.getElementById('out').setAttribute('data-log', log);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *result = out ? dom_get_attr(out, "data-log") : 0;
        fprintf(stderr, "  Promises = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "A:42;B:43;C:7;D:bad;E:oops;F:1,2,3;G:9:true;"),
            "new Promise/resolve/reject, .then chaining, .catch, .finally passthrough, and Promise.resolve/reject/all all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 21. async/await -- async function declarations and expressions
       always return a Promise (fulfilled with the return value, or
       rejected if the body threw); "await" unwraps an already-settled
       promise's value, or re-throws (catchably) its rejection reason. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        const char *src =
            "var log = '';\n"
            "async function double(x) { return x * 2; }\n"
            "double(21).then(function(v) { log += 'A:' + v + ';'; });\n"
            "async function chained() {\n"
            "  var a = await double(5);\n"
            "  var b = await Promise.resolve(a + 1);\n"
            "  return b * 10;\n"
            "}\n"
            "chained().then(function(v) { log += 'B:' + v + ';'; });\n"
            "async function throwsFn() { throw 'boom'; }\n"
            "throwsFn().catch(function(e) { log += 'C:' + e + ';'; });\n"
            "async function catchesAwait() {\n"
            "  try { await Promise.reject('rej'); log += 'NOPE;'; }\n"
            "  catch (e) { log += 'D:' + e + ';'; }\n"
            "  return 'done';\n"
            "}\n"
            "catchesAwait().then(function(v) { log += 'E:' + v + ';'; });\n"
            "var f = async function(x) { return x + 100; };\n"
            "f(1).then(function(v) { log += 'F:' + v + ';'; });\n"
            "var g = await Promise.resolve(5);\n" /* top-level await of an already-settled promise -- also just works */
            "document.getElementById('out').setAttribute('data-log', log + 'G:' + g);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *result = out ? dom_get_attr(out, "data-log") : 0;
        fprintf(stderr, "  async/await = \"%s\"\n", result ? result : "(null)");
        check(result && !strcmp(result, "A:42;B:110;C:boom;D:rej;E:done;F:101;G:5"),
            "async function declarations/expressions and await (value unwrap, rejection-as-throw, try/catch around await) all worked correctly");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 22. Generators ("function*" / "yield"): real suspend/resume (not an
       eager approximation) -- two-way value passing (yield's own result
       receives whatever the NEXT .next(v) sends in), for-of iteration,
       and an abandoned/infinite generator not hanging or crashing the
       process. See the "Generators" section's own top comment
       (js_engine.c, near js_generator_thread_main()) for the thread-per-
       instance design this is built on. */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        /* HTML_MAX_ATTR_LEN is only 64 bytes (html_lexer.h -- same
           truncation limit test 13's own comment already documented), so
           each checked result is split across several short attributes
           rather than one long concatenated string. */
        const char *src =
            "var out = document.getElementById('out');\n"
            "function* gen() { yield 1; yield 2; yield 3; return 99; }\n"
            "var g = gen();\n"
            "var r1 = g.next(); var r2 = g.next(); var r3 = g.next(); var r4 = g.next();\n"
            "out.setAttribute('data-case1', r1.value + ':' + r1.done + ',' + r2.value + ':' + r2.done + ','\n"
            "  + r3.value + ':' + r3.done + ',' + r4.value + ':' + r4.done);\n"
            "var sum = 0;\n"
            "function* counter(n) { for (var i = 0; i < n; i++) yield i * 2; }\n"
            "for (var v of counter(5)) { sum += v; }\n"
            "out.setAttribute('data-case2', '' + sum);\n"
            "function* echo() { var x = yield 'first'; var y = yield ('got:' + x); return 'done:' + y; }\n"
            "var e = echo();\n"
            "var er1 = e.next(); var er2 = e.next('A'); var er3 = e.next('B');\n"
            "out.setAttribute('data-case3', er1.value + ',' + er2.value + ',' + er3.value + ':' + er3.done);\n"
            "function* infinite() { var i = 0; while (true) { yield i; i++; } }\n"
            "var abandoned = '';\n"
            "for (var iv of infinite()) { abandoned += iv + ','; if (iv >= 3) break; }\n"
            "var g2 = infinite(); g2.next(); g2.next();\n" /* started, then abandoned mid-iteration -- must not hang process exit */
            "out.setAttribute('data-case4', abandoned);\n";

        int relayout = 0;
        JSInterp *interp = js_run_script(src, root, &relayout);
        DomNode *out = js_dom_find_by_id(root, "out");
        const char *c1 = out ? dom_get_attr(out, "data-case1") : 0;
        const char *c2 = out ? dom_get_attr(out, "data-case2") : 0;
        const char *c3 = out ? dom_get_attr(out, "data-case3") : 0;
        const char *c4 = out ? dom_get_attr(out, "data-case4") : 0;
        fprintf(stderr, "  generators: c1=\"%s\" c2=\"%s\" c3=\"%s\" c4=\"%s\"\n",
            c1 ? c1 : "(null)", c2 ? c2 : "(null)", c3 ? c3 : "(null)", c4 ? c4 : "(null)");
        check(c1 && !strcmp(c1, "1:false,2:false,3:false,99:true"), "generator .next() sequence (values and done flags) correct");
        check(c2 && !strcmp(c2, "20"), "for-of over a generator correct");
        check(c3 && !strcmp(c3, "first,got:A,done:B:true"), "two-way yield value passing (yield's own result receiving .next(v)'s argument) correct");
        check(c4 && !strcmp(c4, "0,1,2,3,"), "an abandoned/infinite generator (for-of with break, plus a second started-then-abandoned instance) did not hang or crash");
        js_interp_free(interp);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    /* 23. ES modules (js_run_module()): named exports (function + var),
       default export, real isolated per-module top-level scope (a
       non-exported module-local declaration must NOT leak to an
       importer, and nothing a module declares should leak into the
       page's own ordinary global scope either). */
    {
        const char *html = "<html><body><div id=\"out\"></div></body></html>";
        DomNode *root = dom_parse(html);
        CssStylesheet sheet;
        css_stylesheet_init(&sheet);
        css_apply(root, &sheet, 1024.0f);

        int relayout = 0;
        JSInterp *interp = js_run_script("", root, &relayout);

        const char *mathmod =
            "export function add(a, b) { return a + b; }\n"
            "export var PI_APPROX = 3.14;\n"
            "function helper() { return 'internal'; }\n"; /* NOT exported */
        js_run_module(interp, mathmod, "./math.js");

        const char *mainmod =
            "import { add, PI_APPROX } from './math.js';\n"
            "var r1 = add(2, 3);\n"
            "var r2 = PI_APPROX;\n"
            "var r3 = typeof helper;\n"
            "document.getElementById('out').setAttribute('data-r', r1 + ':' + r2 + ':' + r3);\n";
        js_run_module(interp, mainmod, "./main.js");

        const char *defmod = "export default function() { return 'the default'; }\n";
        js_run_module(interp, defmod, "./def.js");
        const char *usedef =
            "import greet from './def.js';\n"
            "document.getElementById('out').setAttribute('data-d', greet());\n";
        js_run_module(interp, usedef, "./usedef.js");

        JSInterp *interp2 = js_run_script("document.getElementById('out').setAttribute('data-g', typeof add);\n", root, &relayout);

        DomNode *out = js_dom_find_by_id(root, "out");
        const char *r = out ? dom_get_attr(out, "data-r") : 0;
        const char *d = out ? dom_get_attr(out, "data-d") : 0;
        const char *g = out ? dom_get_attr(out, "data-g") : 0;
        fprintf(stderr, "  modules: r=\"%s\" d=\"%s\" g=\"%s\"\n", r ? r : "(null)", d ? d : "(null)", g ? g : "(null)");
        check(r && !strcmp(r, "5:3.14:undefined"), "named function/var exports and imports work, and a non-exported module-local name isn't visible to the importer");
        check(d && !strcmp(d, "the default"), "export default (of a function expression) and the matching default import work");
        check(g && !strcmp(g, "undefined"), "a module's own top-level declarations don't leak into the page's ordinary global scope");
        js_interp_free(interp);
        js_interp_free(interp2);
        css_stylesheet_free(&sheet);
        dom_free(root);
    }

    fprintf(stderr, "---\n%s (pass=%d fail=%d)\n",
        g_fail == 0 ? "ALL JS ENGINE TESTS PASSED" : "JS ENGINE TESTS FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
