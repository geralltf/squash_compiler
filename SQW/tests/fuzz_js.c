/* Mutation-based fuzz driver for SQW's JS engine (js_engine.c). Not a
 * coverage-guided fuzzer (no clang/libFuzzer or afl available in this
 * environment) -- a self-contained "dumb" mutation fuzzer instead: a small
 * seed corpus of JS snippets is randomly mutated (byte flips, token
 * splicing from a JS keyword/punctuation dictionary, deliberately deep
 * nesting) and fed to js_run_script() over and over, each attempt run in
 * its own forked child (so a real crash doesn't kill the fuzzing session)
 * under a per-child wall-clock alarm (so a genuine hang, not just the
 * engine's own already-bounded loop/recursion guards, can't stall the
 * whole run). Any child that dies from a signal has its input saved to
 * SQW/tests/fuzz_crashes/ for offline reproduction.
 *
 * Build with ASan+UBSan (gcc, not squash -- see this file's own README-
 * style comment in the session notes on why: squash has no sanitizer
 * support, and this is exactly the kind of memory-safety bug class ASan
 * exists to catch). The _strnicmp shim below is needed because
 * html_lexer.c (pulled in transitively via dom_walk.c) calls the
 * MSVC-only _strnicmp, which squash's own codegen silently maps to
 * strncasecmp for the real Linux/squash build (see codegen.c) but gcc
 * has no idea about -- same substitution, done by hand for this gcc-only
 * fuzz build:
 *
 *   gcc -D_GNU_SOURCE -D_strnicmp=strncasecmp -ISQW \
 *       -fsanitize=address,undefined -g -O1 \
 *       SQW/tests/fuzz_js.c -o SQW/tests/fuzz_js -lm
 *
 * Run with a real stack ulimit -- ASan's own redzone instrumentation
 * inflates every native stack frame well beyond what the same code needs
 * uninstrumented (confirmed this session: js_engine.c's own call-depth
 * cap, sized with a large safety margin against the real squash-compiled
 * binary's stack usage, still stack-overflows under plain ASan
 * instrumentation with the default 8MB stack) -- so a bare ASan crash at
 * shallow recursion is a sanitizer-overhead artifact, not a real product
 * bug, unless it also reproduces with a generous ulimit:
 *
 *   ulimit -s 65536 && SQW/tests/fuzz_js [iterations] [seconds-per-case]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "dom_walk.c"
#include "css.c"
#include "layout.c"
#include "js_engine.c"

#define FUZZ_MAX_LEN 4096

static const char *g_seeds[] = {
    "var x = 1 + 2 * 3; console.log(x);",
    "function f(n) { if (n <= 1) return 1; return n * f(n - 1); } f(5);",
    "class Animal { constructor(name) { this.name = name; } speak() { return this.name; } }\n"
    "class Dog extends Animal { speak() { return super.speak() + ' woof'; } }\n"
    "new Dog('Rex').speak();",
    "var arr = [1,2,3]; arr.push(4); arr.map(function(v){return v*2;}).filter(function(v){return v>2;});",
    "var obj = {a:1,b:[1,2,{c:3}]}; JSON.stringify(obj); JSON.parse(JSON.stringify(obj));",
    "try { throw 'err'; } catch (e) { console.log(e); } finally { console.log('done'); }",
    "for (var i = 0; i < 10; i++) { if (i == 5) break; }",
    "for (var k in {a:1,b:2}) { console.log(k); }",
    "for (var v of [1,2,3]) { console.log(v); }",
    "var s = 'Hello World'; s.slice(1,3) + s.split(' ').join('-') + s.replace('World','There');",
    "document.getElementById('out').innerHTML = '<span>hi</span>';",
    "document.getElementById('out').style.color = 'red';",
    "document.getElementById('out').addEventListener('click', function(e) { this.textContent = 'clicked'; });",
    "var {a, b} = {a:1, b:2}; var [x, y] = [1, 2];",
    "var t = `${1+1} and ${'x'.toUpperCase()}`;",
    "setTimeout(function(){}, 10); setInterval(function(){}, 5); clearInterval(0);",
    "localStorage.setItem('k','v'); localStorage.getItem('k');",
    "fetch('http://x', function(body, ok) { console.log(body); });",
    "Object.keys({a:1}).concat(Object.values({b:2}));",
    "Array.from('abc', function(c,i){return c+i;}).join('-');",
    "[10,2,1].sort().reverse().splice(1,1,'x').join(',');",
    "null == undefined; NaN != NaN; 1/0; -1/0; 0/0;",
    "(function(){return (function(){return (function(){return 1;})();})();})();",
    "((((((((((1))))))))));",
    "var a = []; a[999999999] = 1; a.length;",
    "var a = []; a[2147483647] = 1;",
    "var a = new Array(); a[-1] = 1; a[1e9] = 2;",
    "var a = [1,2,3]; a.splice(2147483647, 0, 1, 2, 3, 4, 5, 6);",
    "var o = { _x: 1, get x() { return this._x * 2; }, set x(v) { this._x = v; } }; o.x = 5; o.x;",
    "class C { constructor() { this._v = 1; } get v() { return this._v; } set v(x) { this._v = x; } }",
    "new Promise(function(resolve, reject) { resolve(1); }).then(function(v){return v+1;}).catch(function(e){});",
    "Promise.resolve(1).then(function(v){throw v;}).catch(function(e){});",
    "Promise.all([Promise.resolve(1), Promise.reject(2)]).catch(function(e){});",
    "async function f(x) { return await Promise.resolve(x) + 1; } f(1).then(function(v){});",
    "async function g() { try { await Promise.reject('x'); } catch (e) {} } g();",
    "async function h() { return h(); } h();", /* recursive async -- exercises the call-depth guard through Promise wrapping */
    "function* gen() { yield 1; yield 2; return 3; } var g = gen(); g.next(); g.next(); g.next(); g.next();",
    "function* inf() { var i = 0; while (true) yield i++; } for (var v of inf()) { if (v > 5) break; }",
    "function* echo() { var x = yield 1; return x; } var e = echo(); e.next(); e.next(42);",
    "function* g2() { yield g2(); } var gg = g2(); gg.next();", /* nested generator creation from within a generator body */
    "export function foo(x) { return x; } export var y = 1; export default 42;",
    "import { a, b as c } from 'mod.js'; import def from 'other.js';",
};
#define N_SEEDS (int)(sizeof(g_seeds) / sizeof(g_seeds[0]))

static const char *g_tokens[] = {
    "function", "return", "var", "let", "const", "if", "else", "while", "for",
    "class", "extends", "new", "this", "super", "try", "catch", "finally",
    "throw", "break", "continue", "typeof", "in", "of", "true", "false",
    "null", "undefined", "(", ")", "{", "}", "[", "]", ";", ",", ".", "+",
    "-", "*", "/", "%", "=", "==", "===", "!=", "!==", "<", ">", "<=", ">=",
    "&&", "||", "!", "?", ":", "++", "--", "+=", "'", "\"", "`", "${", "}",
    "0", "1", "-1", "NaN", "Infinity", "3.14", "1e300", "-1e300",
    /* Boundary-hunting numeric literals -- deliberately shaped to probe
       array-index/length/capacity arithmetic (int overflow, huge
       malloc/realloc requests) and loop-bound edge cases, the exact
       class of bug this session's own manual audit (not the mutator
       itself) found in js_array_ensure_cap()/js_array_set() -- added so a
       FUTURE fuzz run has a real shot at rediscovering that bug class on
       its own instead of relying on manual review every time. */
    "999999999", "2147483647", "-2147483648", "4294967295", "1e9", "1e18",
    "0x7fffffff", "[", "]=1", "a[999999999]", "length",
    "get", "set", "async", "await", "Promise", "resolve", "reject", "then",
    "catch", "finally", "all",
    "function*", "yield", "next",
    "import", "export", "default", "from", "as", "get", "set",
};
#define N_TOKENS (int)(sizeof(g_tokens) / sizeof(g_tokens[0]))

static unsigned long g_rng_state = 0x9e3779b97f4a7c15ULL;
static unsigned long fuzz_rand(void) {
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 7;
    g_rng_state ^= g_rng_state << 17;
    return g_rng_state;
}
static int fuzz_rand_range(int n) { return (int)(fuzz_rand() % (unsigned long)n); }

/* Builds one mutated test case into `out` (cap outcap). Combines a random
   seed, random token splices, occasional byte flips, and (deliberately,
   for exactly the class of bug this session already found by hand --
   unbounded recursion/nesting) a run of repeated open-brackets/parens to
   stress the parser's own recursive-descent depth. */
static void fuzz_mutate(char *out, int outcap) {
    int len = 0;
    int strategy = fuzz_rand_range(4);
    if (strategy == 0) {
        /* Plain seed + light byte-level mutation. */
        const char *seed = g_seeds[fuzz_rand_range(N_SEEDS)];
        int slen = (int)strlen(seed);
        if (slen > outcap - 1) slen = outcap - 1;
        memcpy(out, seed, (size_t)slen);
        len = slen;
        int mutations = 1 + fuzz_rand_range(8);
        int m;
        for (m = 0; m < mutations && len > 0; m++) {
            int pos = fuzz_rand_range(len);
            int op = fuzz_rand_range(3);
            if (op == 0) out[pos] = (char)(fuzz_rand() % 256);
            else if (op == 1 && len > 1) { memmove(out + pos, out + pos + 1, (size_t)(len - pos - 1)); len--; }
            else if (len < outcap - 2) { memmove(out + pos + 1, out + pos, (size_t)(len - pos)); out[pos] = g_tokens[fuzz_rand_range(N_TOKENS)][0]; len++; }
        }
    } else if (strategy == 1) {
        /* Splice several seeds/tokens together. */
        int pieces = 2 + fuzz_rand_range(10);
        int i;
        for (i = 0; i < pieces && len < outcap - 2; i++) {
            const char *piece = (fuzz_rand_range(2) == 0) ? g_seeds[fuzz_rand_range(N_SEEDS)] : g_tokens[fuzz_rand_range(N_TOKENS)];
            int plen = (int)strlen(piece);
            if (len + plen + 1 >= outcap) break;
            memcpy(out + len, piece, (size_t)plen); len += plen;
            out[len++] = ' ';
        }
    } else if (strategy == 2) {
        /* Deliberately deep nesting -- the exact shape that already found
           a real parser-recursion stack-overflow risk this session. */
        const char *opens[] = { "(", "[", "{a:", "!", "-", "typeof " };
        const char *closes[] = { ")", "]", "}", "", "", "" };
        int depth = 50 + fuzz_rand_range(20000);
        int idx = fuzz_rand_range(6);
        int i;
        for (i = 0; i < depth && len < outcap / 2; i++) { int n = (int)strlen(opens[idx]); if (len + n >= outcap) break; memcpy(out + len, opens[idx], (size_t)n); len += n; }
        if (len < outcap - 2) out[len++] = '1';
        for (i = 0; i < depth && len < outcap - 4; i++) { int n = (int)strlen(closes[idx]); if (n == 0) break; if (len + n >= outcap) break; memcpy(out + len, closes[idx], (size_t)n); len += n; }
        if (len < outcap - 2) out[len++] = ';';
    } else {
        /* Pure random token soup -- explores syntax-error / parser-
           recovery paths, not just semantically-valid mutations. */
        int pieces = 5 + fuzz_rand_range(60);
        int i;
        for (i = 0; i < pieces && len < outcap - 8; i++) {
            const char *piece = g_tokens[fuzz_rand_range(N_TOKENS)];
            int plen = (int)strlen(piece);
            memcpy(out + len, piece, (size_t)plen); len += plen;
            out[len++] = ' ';
        }
    }
    if (len >= outcap) len = outcap - 1;
    out[len] = 0;
}

/* One fuzz case, run in a FRESH child process (fork isolation -- a real
   crash here must not take down the whole fuzzing run) with its own
   from-scratch DOM/interp, exercised against both js_run_script() and a
   handful of the dispatch/timer entry points real page interaction would
   also reach. */
static void fuzz_run_one(const char *src) {
    const char *html =
        "<html><body>"
        "<div id=\"out\"></div><input id=\"txt\"><input id=\"cb\" type=\"checkbox\">"
        "<button id=\"btn\">go</button>"
        "</body></html>";
    DomNode *root = dom_parse(html);
    CssStylesheet sheet;
    css_stylesheet_init(&sheet);
    css_apply(root, &sheet, 1024.0f);

    int relayout = 0;
    JSInterp *interp = js_run_script(src, root, &relayout);

    if (interp) {
        DomNode *btn = js_dom_find_by_id(root, "btn");
        DomNode *txt = js_dom_find_by_id(root, "txt");
        DomNode *cb = js_dom_find_by_id(root, "cb");
        int r2 = 0;
        if (btn) js_dispatch_click(interp, btn, &r2);
        if (txt) { js_dispatch_input(interp, txt, &r2); js_dispatch_change(interp, txt, &r2); js_dispatch_keydown(interp, txt, "Enter", &r2); }
        if (cb) js_dispatch_change(interp, cb, &r2);
        js_run_timers(interp, 50.0, &r2);
        js_run_timers(interp, 1e9, &r2); /* far-future tick: fires every still-pending repeating timer at least once more */
        js_deliver_fetch_result(interp, 0, "fuzzed body", 1, &r2);
        js_interp_free(interp);
    }
    css_stylesheet_free(&sheet);
    dom_free(root);
}

static void save_crash(const char *src, int n) {
    mkdir("SQW/tests/fuzz_crashes", 0755);
    char path[256];
    snprintf(path, sizeof path, "SQW/tests/fuzz_crashes/crash_%d.js", n);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(src, 1, strlen(src), f); fclose(f); }
    fprintf(stderr, "  -> saved reproducer to %s\n", path);
}

int main(int argc, char **argv) {
    int iterations = argc > 1 ? atoi(argv[1]) : 20000;
    int per_case_timeout = argc > 2 ? atoi(argv[2]) : 2;
    unsigned seed = argc > 3 ? (unsigned)atol(argv[3]) : (unsigned)time(0);
    g_rng_state ^= seed;
    fprintf(stderr, "fuzz_js: %d iterations, %ds/case, seed=%u\n", iterations, per_case_timeout, seed);

    int crashes = 0, hangs = 0, i;
    char src[FUZZ_MAX_LEN];
    for (i = 0; i < iterations; i++) {
        fuzz_mutate(src, sizeof src);

        pid_t pid = fork();
        if (pid == 0) {
            /* The engine's own console.log/lex-error/parse-error
               diagnostics are expected, frequent noise at fuzzing scale
               (most random mutations are simply invalid programs) --
               silencing them in the CHILD only keeps the parent's own
               crash/hang reporting legible and removes a large, pointless
               I/O cost from the hot loop. Real ASan/UBSan crash reports
               still go to stderr and are NOT redirected here (see below --
               only stdout and the engine's OWN fprintf(stderr,...) calls
               are silenced by dup2'ing fd 2 to /dev/null BEFORE running
               the case; a sanitizer fatal report bypasses buffered stdio
               and writes on its own fd/report path, so redirecting fd 2
               ahead of time still means we'd lose it here too -- instead,
               ASAN_OPTIONS=log_path is used by the caller to route
               sanitizer reports to a file per the run instructions
               instead of relying on inherited stderr). */
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
            alarm((unsigned)per_case_timeout);
            signal(SIGALRM, SIG_DFL);
            fuzz_run_one(src);
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGALRM) {
                hangs++;
                fprintf(stderr, "[HANG] case %d timed out after %ds\n", i, per_case_timeout);
            } else {
                crashes++;
                fprintf(stderr, "[CRASH] case %d killed by signal %d (%s)\n", i, sig, strsignal(sig));
            }
            save_crash(src, i);
        }
        if (i % 1000 == 0) { fprintf(stderr, "  ...%d/%d (crashes=%d hangs=%d)\n", i, iterations, crashes, hangs); fflush(stderr); }
    }
    fprintf(stderr, "fuzz_js: done. %d iterations, %d crashes, %d hangs.\n", iterations, crashes, hangs);
    return (crashes > 0 || hangs > 0) ? 1 : 0;
}
