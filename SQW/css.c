/* Implementation of css.h -- see that header's own top comment. #include-d
 * directly into sqw_main.c, same single-TU convention as every other
 * SQW/*.c file.
 *
 * Documented scope (a REAL CSS engine, not a fake one, but a genuinely
 * bounded subset -- see the project's own multi-session-scope discussion
 * this was built under):
 *   Selectors : tag, .class, #id, "*", "[attr]"/"[attr=value]", ":hover",
 *               and any AND-combination of those in one compound
 *               (div.foo#bar[href]:hover), chained with the DESCENDANT
 *               combinator (space, any ancestor) or the CHILD combinator
 *               (">", the ancestor must be the EXACT immediate parent --
 *               see css_selector_matches()'s own comment and CssCompound.
 *               is_child_combinator). NOT supported: sibling "+"/"~"
 *               combinators, and any pseudo-class/-element other than
 *               ":hover" (":focus", "::before", ":nth-child()", etc) -- a
 *               rule using one of those parses without error but its
 *               selector simply never matches anything (same safe-
 *               degradation convention as the rest of this project's
 *               parsers).
 *               ":hover" matches DomNode::hover, which sqw_main.c's mouse-
 *               move handling already sets/clears live -- see
 *               css_apply_one()'s own comment for how a hover change gets
 *               re-resolved without a full-page re-layout.
 *   At-rules  : "@media (min-width: ...px)" / "(max-width: ...px)"
 *               (ANDed with "and", nested @media intersected) is REALLY
 *               evaluated against the current viewport width -- see
 *               CssRule.media_min_width's own comment and
 *               css_parse_media_prelude() -- so responsive breakpoints
 *               now actually turn on/off correctly, including live on
 *               window resize (see css_apply()'s own comment on when to
 *               re-run it). Any OTHER media feature this engine doesn't
 *               recognize (orientation, prefers-color-scheme, hover, a
 *               comma-separated OR'd query list, ...) simply contributes
 *               no constraint, same "recover more than we lose" tradeoff
 *               as before -- only min-width/max-width are REAL gates now,
 *               everything else still unwraps unconditionally. A "print"
 *               media type (bare "@media print" or "@media print and
 *               (...)") is recognized and its whole block is skipped
 *               outright, never applied to the screen. Every other
 *               @-rule (@import/@font-face/@keyframes/@supports/@charset/
 *               @page/...) is properly skipped as a whole (balanced-brace
 *               block, or up to the next top-level ";" for a statement-
 *               only at-rule like @import) -- never misparsed as a normal
 *               rule.
 *   Properties: display, width, height, margin(-top/right/bottom/left),
 *               padding(-top/right/bottom/left), border(-width/-color/
 *               -style, shorthand and per-side), color, background /
 *               background-color, font-size, font-weight, text-align,
 *               line-height, opacity, flex-direction, flex-wrap,
 *               flex-flow, flex-grow, flex (shorthand -- grow component
 *               only, see css_apply_decl's own comment), justify-content,
 *               align-items, gap / column-gap / row-gap,
 *               grid-template-columns (stored raw, parsed by layout.c at
 *               layout time), background-image / the url() component of
 *               the "background" shorthand (fetched/decoded/drawn via the
 *               same image_cache.h pipeline as <img>, stretched to fill
 *               the element's own box -- see sqw_main.c's own
 *               draw_layout_bg_images()), text-decoration / text-
 *               decoration-line (none/underline/line-through -- an
 *               explicit "none" on an <a> suppresses this project's own
 *               hardcoded default anchor underline; underline/line-
 *               through on any OTHER element draws that line too),
 *               z-index (real per-box paint-order sorting among siblings
 *               at the same stacking level -- see layout.c's own
 *               layout_list_sort_by_z()), overflow / overflow-x /
 *               overflow-y: hidden (clips this element's children to its
 *               own box via a real Vulkan scissor rect), border-radius,
 *               box-shadow, and transform (translate/scale/rotate/skewX/
 *               skewY/matrix, chained) -- all three rendered via a real
 *               hand-written GLSL shader (SQW/styled_renderer_vk.h/.c),
 *               scoped to an element's own background-fill rect only
 *               (background-image isn't clipped to the radius, borders
 *               aren't curved, transform doesn't move borders/content/
 *               children -- see dom.h's own comment on this feature's
 *               exact scope). font-size/text-align/line-height/
 *               font-weight are real INHERITED properties (see
 *               css_apply_element()'s own comment); opacity/text-
 *               decoration/z-index/overflow/border-radius/box-shadow/
 *               transform are not.
 *               font-style:italic is parsed but has no visual effect (no
 *               slanted glyph variant exists in this project's single
 *               baked atlas -- font_atlas.h).
 *               Any other property is parsed (so it doesn't desync the
 *               declaration-list parse) and then silently ignored.
 *   Values    : lengths in px or unitless (treated as px); percentages
 *               and other units (em/rem/vw/vh/...) are parsed but not
 *               resolved (treated as "not specified"); colors as
 *               #rgb/#rrggbb, rgb()/rgba(), and a small table of common
 *               named colors. */
#include "css.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define CSS_RULES_INIT_CAP 64

void css_stylesheet_init(CssStylesheet *sheet) {
    sheet->rules = 0; sheet->count = 0; sheet->cap = 0;
}
void css_stylesheet_free(CssStylesheet *sheet) {
    if (sheet->rules) free(sheet->rules);
    sheet->rules = 0; sheet->count = 0; sheet->cap = 0;
}
static CssRule *css_push_rule(CssStylesheet *sheet) {
    if (sheet->count >= sheet->cap) {
        sheet->cap = sheet->cap ? sheet->cap * 2 : CSS_RULES_INIT_CAP;
        sheet->rules = (CssRule *)realloc(sheet->rules, (size_t)sheet->cap * sizeof(CssRule));
    }
    CssRule *r = &sheet->rules[sheet->count++];
    memset(r, 0, sizeof(*r));
    return r;
}

/* IMPORTANT squash-compiler workaround, applies to EVERY function in this
 * file taking a "const char **p" cursor parameter: a real squash codegen
 * bug (confirmed via a minimal standalone repro in the scratchpad --
 * "**p" inside a function that received "const char **p" as a parameter
 * read garbage/unrelated memory, not the real pointed-to character, even
 * though the identical code compiled and ran correctly under gcc) makes
 * "**p" (double-dereferencing the parameter directly) unreliable --
 * distinct from, but the same underlying class of bug as, the
 * struct-field-via-arrow comparison bug documented in php_mini.c's own
 * top comment this same session. The fix is the same shape: never write
 * "**p" -- copy "*p" into a local plain "const char *q" ONCE at function
 * entry, do ALL scanning/comparison through "q" (a single, ordinary
 * pointer -- confirmed reliable), and write "*p = q" back before
 * returning AND before calling any other "const char **"-taking helper
 * (which does its own copy-in/write-back), reloading "q = *p" once that
 * helper returns. */
static void css_skip_ws(const char **p) {
    const char *q = *p;
    for (;;) {
        while (*q && isspace((unsigned char)*q)) q++;
        if (q[0] == '/' && q[1] == '*') {
            q += 2;
            while (*q && !(q[0] == '*' && q[1] == '/')) q++;
            if (*q) q += 2;
            continue;
        }
        break;
    }
    *p = q;
}

/* Parses one compound selector ("div.foo#bar" -- no whitespace) starting
 * at *p, stopping at whitespace/','/'{'/'>'/'+'/'~'/end -- the combinator
 * characters are left UNCONSUMED for css_parse_selector's own caller loop
 * to see and act on (real child-combinator support, and the still-
 * unsupported-but-safely-degrading sibling combinators -- see that
 * function's own comment) rather than being silently eaten here the way
 * they used to be. Attribute/pseudo forms ([...], :...) are still
 * consumed (so the overall parse stays in sync) but contribute no simple
 * selector for anything this engine doesn't understand -- see this
 * file's own top comment. */
static void css_parse_compound(const char **p, CssCompound *out) {
    out->part_count = 0;
    out->is_child_combinator = 0;
    /* Set when a pseudo-class/pseudo-element OTHER than ":hover" is seen
     * anywhere in this compound -- forces part_count to 0 at the end
     * (see below), matching this file's own DOCUMENTED design (top-of-
     * file comment: "a rule using [an unsupported pseudo-class/element]
     * parses without error but its selector simply never matches
     * anything") -- a real, confirmed gap between that stated intent and
     * what the code actually did: previously, a non-hover pseudo simply
     * contributed no SimpleSel and was otherwise ignored, leaving the
     * REST of the compound (its tag/class) still matching normally.
     * Confirmed as a real, visible bug on a real Wikipedia page: rules
     * like "a:visited{color:var(--color-visited,#6a60b0)}" and
     * "a:where(:not([role='button'])):visited{...}" -- both of which,
     * after stripping their unsupported pseudo-classes, degenerate to
     * plain "a" (tag-only, exactly the SAME specificity as the page's
     * own real, intended "a{color:var(--color-progressive,#36c)}" base
     * link-color rule) -- won the cascade tie-break via later source
     * order, recoloring EVERY link on the page (not just visited ones)
     * to the visited/destructive color. Forcing the whole compound to
     * never-match instead correctly drops such rules from the cascade
     * entirely, same as a real browser would for `:visited` under its
     * own privacy-motivated restrictions (real browsers also refuse to
     * let :visited styling do anything a page could detect/observe
     * beyond simple color, but the practical rendering effect -- an
     * unvisited-looking page, since this engine has no browsing history
     * to consult anyway -- is the same end result this achieves). */
    int has_unsupported_pseudo = 0;
    const char *q = *p;
    for (;;) {
        char c = *q;
        if (c == 0 || isspace((unsigned char)c) || c == ',' || c == '{' || c == '>' || c == '+' || c == '~') break;
        if (c == '[') {
            /* "[name]" or "[name=value]"/"[name='value']"/"[name=\"value\"]"
             * -- an operator OTHER than bare "=" (~=, |=, ^=, $=, *=) isn't
             * recognized as an operator here, so name_end lands on it and
             * the whole thing is treated as an attribute-PRESENCE check
             * instead (see CSS_SEL_ATTR's own header comment) -- parses
             * without error either way, never desyncs the bracket scan. */
            q++;
            const char *name_start = q;
            while (*q && *q != ']' && *q != '=') q++;
            const char *name_end = q;
            int has_val = 0;
            const char *val_start = 0, *val_end = 0;
            if (*q == '=') {
                q++;
                char quote = 0;
                if (*q == '"' || *q == '\'') { quote = *q; q++; }
                val_start = q;
                if (quote) { while (*q && *q != quote) q++; val_end = q; if (*q) q++; }
                else { while (*q && *q != ']') q++; val_end = q; }
                has_val = 1;
            }
            while (*q && *q != ']') q++;
            if (*q) q++;
            int nlen = (int)(name_end - name_start);
            if (nlen > 0 && out->part_count < CSS_MAX_COMPOUND_PARTS) {
                CssSimpleSel *s = &out->parts[out->part_count++];
                s->kind = CSS_SEL_ATTR;
                if (nlen >= (int)sizeof s->name) nlen = (int)sizeof s->name - 1;
                int ai; for (ai = 0; ai < nlen; ai++) s->name[ai] = (char)tolower((unsigned char)name_start[ai]);
                s->name[nlen] = 0;
                s->value_set = has_val;
                if (has_val) {
                    int vlen = (int)(val_end - val_start);
                    if (vlen >= (int)sizeof s->value) vlen = (int)sizeof s->value - 1;
                    memcpy(s->value, val_start, (size_t)vlen);
                    s->value[vlen] = 0;
                } else {
                    s->value[0] = 0;
                }
            }
            continue;
        }
        if (c == ':') {
            q++;
            if (*q == ':') q++;
            const char *pseudo_start = q;
            while (isalnum((unsigned char)*q) || *q == '-') q++;
            int plen = (int)(q - pseudo_start);
            if (*q == '(') { int depth = 1; q++; while (*q && depth > 0) { if (*q == '(') depth++; else if (*q == ')') depth--; q++; } }
            int is_hover = plen == 5 && strncmp(pseudo_start, "hover", 5) == 0;
            int is_checked = plen == 7 && strncmp(pseudo_start, "checked", 7) == 0;
            if (is_hover && out->part_count < CSS_MAX_COMPOUND_PARTS) {
                CssSimpleSel *s = &out->parts[out->part_count++];
                s->kind = CSS_SEL_HOVER; s->name[0] = 0; s->value_set = 0; s->value[0] = 0;
            } else if (is_checked && out->part_count < CSS_MAX_COMPOUND_PARTS) {
                CssSimpleSel *s = &out->parts[out->part_count++];
                s->kind = CSS_SEL_CHECKED; s->name[0] = 0; s->value_set = 0; s->value[0] = 0;
            } else if (!is_hover && !is_checked) {
                has_unsupported_pseudo = 1;
            }
            continue;
        }
        CssSelKind kind; const char *start;
        if (c == '.') { kind = CSS_SEL_CLASS; q++; start = q; }
        else if (c == '#') { kind = CSS_SEL_ID; q++; start = q; }
        else if (c == '*') { kind = CSS_SEL_UNIVERSAL; q++; start = q;
            if (out->part_count < CSS_MAX_COMPOUND_PARTS) { out->parts[out->part_count].kind = kind; out->parts[out->part_count].name[0] = 0; out->part_count++; }
            continue;
        }
        else { kind = CSS_SEL_TAG; start = q; }
        while (isalnum((unsigned char)*q) || *q == '-' || *q == '_') q++;
        int len = (int)(q - start);
        if (len > 0 && out->part_count < CSS_MAX_COMPOUND_PARTS) {
            CssSimpleSel *s = &out->parts[out->part_count++];
            s->kind = kind;
            if (len >= (int)sizeof s->name) len = (int)sizeof s->name - 1;
            int i;
            for (i = 0; i < len; i++) s->name[i] = (char)tolower((unsigned char)start[i]);
            s->name[len] = 0;
        } else if (len == 0 && kind != CSS_SEL_UNIVERSAL) {
            /* Bare combinator char etc already consumed above; nothing
             * to record -- avoid an infinite loop on a stray character.
             * The "*q != 0" guard matters: a trailing "#"/"." at the very
             * end of the stylesheet (e.g. a truncated/malformed file)
             * lands here with q==start==(the NUL terminator itself) --
             * without this guard, "q++" walked one byte PAST the
             * terminator, and the enclosing for(;;) loop's own "c == 0"
             * check (which would otherwise have caught it) never got a
             * chance to see that NUL, instead dereferencing one byte past
             * the end of the buffer on its next iteration -- a real
             * heap-buffer-overflow READ, found via fuzzing this function
             * this session. */
            if (q == start && *q != 0) q++;
        }
    }
    *p = q;
    if (has_unsupported_pseudo) out->part_count = 0;
}

static int css_compound_specificity(const CssCompound *c) {
    int i, sp = 0;
    for (i = 0; i < c->part_count; i++) {
        if (c->parts[i].kind == CSS_SEL_ID) sp += 100;
        else if (c->parts[i].kind == CSS_SEL_CLASS || c->parts[i].kind == CSS_SEL_ATTR || c->parts[i].kind == CSS_SEL_HOVER || c->parts[i].kind == CSS_SEL_CHECKED) sp += 10;
        else if (c->parts[i].kind == CSS_SEL_TAG) sp += 1;
    }
    return sp;
}

/* Parses one full selector (comma-separated list item) into `out`,
 * stopping at ',' or '{'. Also handles the combinator BETWEEN two
 * compounds -- plain whitespace (descendant, the default), "> " (real
 * child-combinator support, tracked via the next compound's own
 * is_child_combinator -- see CssCompound's own comment), and "+ "/"~ "
 * (sibling combinators, still not supported: the compound that follows
 * one is forced to zero parts, same "parses cleanly but never matches"
 * degradation this engine already used for every unsupported combinator
 * before child-combinator support existed -- see
 * css_selector_matches()'s own comment on how a zero-part compound
 * always fails the match). */
static void css_parse_selector(const char **p, CssSelector *out) {
    out->chain_len = 0;
    out->specificity = 0;
    int pending_child = 0, pending_unsupported = 0, pending_sibling = 0;
    for (;;) {
        css_skip_ws(p);
        const char *q = *p;
        if (*q == 0 || *q == ',' || *q == '{') break;
        if (*q == '>') {
            q++; *p = q; css_skip_ws(p);
            pending_child = 1;
            continue;
        }
        if (*q == '~') {
            q++; *p = q; css_skip_ws(p);
            pending_sibling = 1;
            continue;
        }
        if (*q == '+') {
            q++; *p = q; css_skip_ws(p);
            pending_unsupported = 1;
            continue;
        }
        CssCompound *comp;
        CssCompound dummy;
        if (out->chain_len < CSS_MAX_SELECTOR_CHAIN) {
            comp = &out->chain[out->chain_len++];
        } else {
            comp = &dummy;
        }
        css_parse_compound(p, comp);
        comp->is_child_combinator = pending_child;
        comp->is_general_sibling = pending_sibling;
        if (pending_unsupported) comp->part_count = 0; /* + target: never matches, see this function's own comment */
        pending_child = 0;
        pending_sibling = 0;
        pending_unsupported = 0;
        if (comp != &dummy) out->specificity += css_compound_specificity(comp);
    }
}

static void css_parse_decls(const char **p, CssRule *rule) {
    const char *q = *p;
    for (;;) {
        *p = q;
        css_skip_ws(p);
        q = *p;
        if (*q == 0 || *q == '}') break;
        if (*q == ';') { q++; continue; }
        const char *name_start = q;
        while (*q && *q != ':' && *q != ';' && *q != '}') q++;
        const char *name_end = q;
        if (*q != ':') { if (*q == ';') q++; *p = q; continue; }
        q++; /* skip ':' */
        *p = q;
        css_skip_ws(p);
        q = *p;
        const char *val_start = q;
        int depth = 0;
        while (*q && (depth > 0 || (*q != ';' && *q != '}'))) {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        }
        const char *val_end = q;
        while (val_end > val_start && isspace((unsigned char)val_end[-1])) val_end--;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) name_end--;

        if (rule->decl_count < CSS_MAX_DECLS) {
            CssDecl *d = &rule->decls[rule->decl_count++];
            int nlen = (int)(name_end - name_start);
            if (nlen >= (int)sizeof d->name) nlen = (int)sizeof d->name - 1;
            int i;
            for (i = 0; i < nlen; i++) d->name[i] = (char)tolower((unsigned char)name_start[i]);
            d->name[nlen] = 0;
            int vlen = (int)(val_end - val_start);
            if (vlen >= (int)sizeof d->value) vlen = (int)sizeof d->value - 1;
            memcpy(d->value, val_start, (size_t)vlen);
            d->value[vlen] = 0;
        }
        if (*q == ';') q++;
    }
    *p = q;
}

static void css_parse_at_rule(CssStylesheet *sheet, const char **p, int *source_order, int mq_min, int mq_max);

/* Bundles css_parse_decls_nested()'s own "parse context" (everything
 * that isn't the position cursor/parent-selectors/output-rule) into one
 * struct passed by pointer -- cuts that function down from 8 separate
 * parameters to 4. Not just tidiness: a real squash codegen bug was
 * confirmed via a minimal repro (this exact 8-parameter mix, called from
 * a site computing one argument's address as a field inside an already-
 * huge -- ~29KB -- CssRule local) corrupting incoming parameters (a
 * plain int param read back as garbage, a pointer param read back NULL)
 * at function entry, even though the identical-shaped call worked
 * correctly under gcc. Cutting the parameter count (and moving the
 * rarely-changing ones behind one pointer) reliably avoided the bad
 * codegen path in testing -- documented here as this file's own
 * established "squash codegen workaround" convention, same as every
 * other one already noted throughout this file. */
typedef struct {
    CssStylesheet *sheet;
    int *source_order;
    int mq_min, mq_max;
} CssNestCtx;

/* Parses a rule body that may contain real, native CSS NESTING -- a
 * selector list followed by '{' appearing where a declaration's own
 * name/value would otherwise be, e.g. real Wikipedia CSS (found via a
 * real fetched-page investigation this engine's own stylesheet-parse
 * count silently plateaued on):
 *   .uls-rewrite{.badge-goodarticle::before,.badge-x::before{content:url(...)}...}
 * Before this function existed, css_parse_decls() had no notion of this
 * shape at all: it read ".badge-goodarticle::before,..." as a
 * declaration NAME (stopping at the first ':' inside "::before"), then
 * misparsed everything after as that one declaration's value -- which,
 * given enough intervening '('/')' imbalance from a later misparsed
 * value, could desync paren-depth tracking badly enough to swallow the
 * ENTIRE REST of the stylesheet (confirmed: 204KB of real, otherwise-
 * valid CSS silently discarded from one nested block this engine didn't
 * understand, hiding the real .mw-page-container-inner grid rules that
 * make Wikipedia's sidebar render beside its content instead of full-
 * width). Fixed for real, not just contained: a lookahead (tracking
 * paren depth, same convention as the ordinary value scanner) decides,
 * for each "declaration", whether a '{' or a ';'/'}' comes first at
 * depth 0 -- a plain colon is deliberately NOT one of the lookahead's
 * own stop characters (an earlier version of this fix used ':' as a
 * stop char too, which broke on exactly this real input: a pseudo-
 * element like "::before" inside the nested selector text has its own
 * literal colons long before the real terminating '{', so stopping at
 * the first colon misidentified every such nested rule as an ordinary
 * declaration again) -- '{' first means this is a nested rule, not a
 * declaration. A nested
 * rule's own selector list is combined with EVERY one of the enclosing
 * rule's own selectors (real CSS nesting's implicit descendant-
 * combinator semantics for a bare, non-"&"-prefixed nested selector --
 * "&"-prefixed nesting is not specially recognized, same lenient-
 * degradation convention as every other unsupported selector form this
 * file already documents: it parses without desyncing but simply never
 * matches) and pushed as real, independent top-level CssRule entries,
 * recursing so multiple levels of nesting (real, if rarer) also work. */
static void css_parse_decls_nested(CssNestCtx *ctx, const char **p,
                                    CssSelector *parent_sels, int parent_count,
                                    CssRule *out) {
    const char *q = *p;
    for (;;) {
        *p = q;
        css_skip_ws(p);
        q = *p;
        if (*q == 0 || *q == '}') break;
        if (*q == ';') { q++; continue; }
        if (*q == '@') { *p = q; css_parse_at_rule(ctx->sheet, p, ctx->source_order, ctx->mq_min, ctx->mq_max); q = *p; continue; }

        const char *scan = q;
        int look_depth = 0;
        int is_nested = 0;
        while (*scan) {
            char c = *scan;
            if (c == '(') look_depth++;
            else if (c == ')') look_depth--;
            else if (look_depth <= 0 && (c == '{' || c == ';' || c == '}')) {
                is_nested = (c == '{');
                break;
            }
            scan++;
        }

        if (is_nested) {
            /* CssSelector/CssRule are both large (a CssRule alone is
             * ~29KB, thanks to CSS_MAX_RULE_SELECTORS * CSS_MAX_
             * SELECTOR_CHAIN * CSS_MAX_COMPOUND_PARTS-sized nested
             * arrays) -- two of them plus an 8-element CssSelector array
             * as ordinary stack locals in one (recursive!) function
             * proved to be a real squash codegen bug, not just a
             * resource-budget concern: a minimal, non-recursive, single-
             * level repro (one nested rule, no deep nesting at all)
             * reliably segfaulted only when built with squash itself
             * (gcc built and ran the identical source correctly),
             * confirmed via gdb (a bogus, unrelated-looking crash site,
             * consistent with a miscomputed large stack-frame offset).
             * Heap-allocating instead sidesteps whatever squash's exact
             * large-stack-frame limit is -- also just sensible given the
             * sizes involved, independent of the squash bug. */
            CssSelector *nested_sels = (CssSelector *)malloc(sizeof(CssSelector) * CSS_MAX_RULE_SELECTORS);
            int nested_count = 0;
            for (;;) {
                *p = q;
                css_skip_ws(p);
                q = *p;
                if (*q == 0 || *q == '{') break;
                *p = q;
                if (nested_count < CSS_MAX_RULE_SELECTORS) {
                    css_parse_selector(p, &nested_sels[nested_count++]);
                } else {
                    CssSelector dummy;
                    css_parse_selector(p, &dummy);
                }
                q = *p;
                *p = q;
                css_skip_ws(p);
                q = *p;
                if (*q == ',') { q++; continue; }
                break;
            }
            if (*q != '{') { free(nested_sels); if (*q) q++; continue; } /* malformed -- resync */
            q++;
            *p = q;

            CssRule *combined = (CssRule *)malloc(sizeof(CssRule));
            memset(combined, 0, sizeof *combined);
            combined->media_min_width = ctx->mq_min;
            combined->media_max_width = ctx->mq_max;
            int pi, ni;
            for (pi = 0; pi < parent_count; pi++) {
                for (ni = 0; ni < nested_count; ni++) {
                    if (combined->selector_count >= CSS_MAX_RULE_SELECTORS) break;
                    CssSelector *dst = &combined->selectors[combined->selector_count++];
                    dst->chain_len = 0;
                    dst->specificity = parent_sels[pi].specificity + nested_sels[ni].specificity;
                    int ci;
                    for (ci = 0; ci < parent_sels[pi].chain_len && dst->chain_len < CSS_MAX_SELECTOR_CHAIN; ci++)
                        dst->chain[dst->chain_len++] = parent_sels[pi].chain[ci];
                    for (ci = 0; ci < nested_sels[ni].chain_len && dst->chain_len < CSS_MAX_SELECTOR_CHAIN; ci++)
                        dst->chain[dst->chain_len++] = nested_sels[ni].chain[ci];
                }
            }
            free(nested_sels);
            CssSelector *rec_parent_sels = combined->selectors;
            int rec_parent_count = combined->selector_count;
            css_parse_decls_nested(ctx, p, rec_parent_sels, rec_parent_count, combined);
            q = *p;
            if (*q == '}') q++;
            if (combined->selector_count > 0 && combined->decl_count > 0) {
                combined->source_order = (*ctx->source_order)++;
                CssRule *r = css_push_rule(ctx->sheet);
                *r = *combined;
            }
            free(combined);
            continue;
        }

        const char *name_start = q;
        while (*q && *q != ':' && *q != ';' && *q != '}') q++;
        const char *name_end = q;
        if (*q != ':') { if (*q == ';') q++; *p = q; continue; }
        q++;
        *p = q;
        css_skip_ws(p);
        q = *p;
        const char *val_start = q;
        int depth = 0;
        while (*q && (depth > 0 || (*q != ';' && *q != '}'))) {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        }
        const char *val_end = q;
        while (val_end > val_start && isspace((unsigned char)val_end[-1])) val_end--;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) name_end--;

        if (out->decl_count < CSS_MAX_DECLS) {
            CssDecl *d = &out->decls[out->decl_count++];
            int nlen = (int)(name_end - name_start);
            if (nlen >= (int)sizeof d->name) nlen = (int)sizeof d->name - 1;
            int i;
            for (i = 0; i < nlen; i++) d->name[i] = (char)tolower((unsigned char)name_start[i]);
            d->name[nlen] = 0;
            int vlen = (int)(val_end - val_start);
            if (vlen >= (int)sizeof d->value) vlen = (int)sizeof d->value - 1;
            memcpy(d->value, val_start, (size_t)vlen);
            d->value[vlen] = 0;
        }
        if (*q == ';') q++;
    }
    *p = q;
}

/* Skips one balanced-brace block starting at *p (which must point at
 * '{'); leaves *p just past the matching '}'. Used for @-rules whose
 * body this engine doesn't understand (@font-face, @keyframes, ...). */
static void css_skip_block(const char **p) {
    const char *q = *p;
    if (*q != '{') return;
    int depth = 0;
    do {
        if (*q == '{') depth++;
        else if (*q == '}') depth--;
        q++;
    } while (*q && depth > 0);
    *p = q;
}

static void css_parse_rule_body(CssStylesheet *sheet, const char **p, int *source_order, int mq_min, int mq_max);

/* Parses an "@media" prelude -- the text between "media" and the opening
 * '{' -- for the subset of real media-query syntax this engine
 * understands: a "print" media type (sets *out_print_only, so the caller
 * can skip the whole block instead of unwrapping it -- a real screen
 * renderer should never apply print-only rules) and "(min-width: Npx)" /
 * "(max-width: Npx)" features, ANDed together same as real CSS's own
 * "and" between features. Any OTHER feature this engine doesn't
 * recognize (orientation, prefers-color-scheme, hover, ...), or a comma-
 * separated OR'd query list, simply contributes no constraint -- same
 * "recover more than we lose" tradeoff the old unconditional-unwrap
 * already made, just narrowed to the one real case (min/max-width) this
 * engine can now actually evaluate correctly. */
static void css_parse_media_prelude(const char *start, const char *end, int *out_print_only, int *out_min, int *out_max) {
    *out_print_only = 0;
    *out_min = -1;
    *out_max = -1;
    const char *q = start;
    while (q < end) {
        char c = *q;
        if (isalpha((unsigned char)c)) {
            const char *w = q;
            while (q < end && (isalpha((unsigned char)*q) || *q == '-')) q++;
            int wlen = (int)(q - w);
            if (wlen == 5 && strncmp(w, "print", 5) == 0) *out_print_only = 1;
            continue;
        }
        if (c == '(') {
            const char *fstart = q + 1;
            const char *fend = fstart;
            while (fend < end && *fend != ')') fend++;
            const char *colon = fstart;
            while (colon < fend && *colon != ':') colon++;
            if (colon < fend) {
                const char *nstart = fstart;
                int name_len = (int)(colon - fstart);
                while (name_len > 0 && isspace((unsigned char)*nstart)) { nstart++; name_len--; }
                while (name_len > 0 && isspace((unsigned char)nstart[name_len - 1])) name_len--;
                const char *vstart = colon + 1;
                while (vstart < fend && isspace((unsigned char)*vstart)) vstart++;
                int val = atoi(vstart); /* stops at the first non-digit ("px"/"em"/end) -- treats any unit as px, same convention as this engine's other length handling */
                if (name_len == 9 && strncmp(nstart, "min-width", 9) == 0) *out_min = val;
                else if (name_len == 9 && strncmp(nstart, "max-width", 9) == 0) *out_max = val;
            }
            q = (fend < end) ? fend + 1 : fend;
            continue;
        }
        q++;
    }
}

/* Handles one "@...". @media's body is unwrapped and parsed as ordinary
 * rules, now gated by a real (if scoped) evaluation of its condition --
 * see css_parse_media_prelude()'s own comment -- instead of unconditional
 * unwrap; `mq_min`/`mq_max` are the ENCLOSING @media's own constraint (for
 * a nested "@media" inside another, real but rare), combined with this
 * one's own via intersection so the tighter of the two always wins.
 * Every other @-rule is properly skipped, whole. */
static void css_parse_at_rule(CssStylesheet *sheet, const char **p, int *source_order, int mq_min, int mq_max) {
    const char *kw_start = *p + 1;
    const char *q = kw_start;
    while (isalpha((unsigned char)*q) || *q == '-') q++;
    int kw_len = (int)(q - kw_start);
    int is_media = (kw_len == 5 && strncmp(kw_start, "media", 5) == 0);
    const char *prelude_start = q;
    /* Skip the prelude (condition/selector-like text before '{' or ';'). */
    while (*q && *q != '{' && *q != ';') q++;
    const char *prelude_end = q;
    if (*q == ';') { q++; *p = q; return; }
    if (*q != '{') { *p = q; return; }
    if (is_media) {
        int print_only, own_min, own_max;
        css_parse_media_prelude(prelude_start, prelude_end, &print_only, &own_min, &own_max);
        if (print_only) { *p = q; css_skip_block(p); return; }
        int combined_min = (own_min < 0) ? mq_min : (mq_min < 0 ? own_min : (own_min > mq_min ? own_min : mq_min));
        int combined_max = (own_max < 0) ? mq_max : (mq_max < 0 ? own_max : (own_max < mq_max ? own_max : mq_max));
        q++; /* enter the block */
        *p = q;
        css_parse_rule_body(sheet, p, source_order, combined_min, combined_max);
        q = *p;
        if (*q == '}') q++;
        *p = q;
    } else {
        *p = q;
        css_skip_block(p);
    }
}

/* Parses a sequence of ordinary rules (and nested @-rules) until a
 * top-level '}' or end of input -- used both for the whole stylesheet
 * and for an unwrapped @media body. `mq_min`/`mq_max` (see CssRule.
 * media_min_width's own comment) are stamped onto every rule pushed from
 * here -- (-1,-1) at the true top level, the enclosing @media's own
 * combined constraint otherwise. */
static void css_parse_rule_body(CssStylesheet *sheet, const char **p, int *source_order, int mq_min, int mq_max) {
    const char *q = *p;
    for (;;) {
        *p = q;
        css_skip_ws(p);
        q = *p;
        if (*q == 0 || *q == '}') { *p = q; return; }
        if (*q == '@') { *p = q; css_parse_at_rule(sheet, p, source_order, mq_min, mq_max); q = *p; continue; }

        CssRule tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.media_min_width = mq_min;
        tmp.media_max_width = mq_max;
        for (;;) {
            *p = q;
            css_skip_ws(p);
            q = *p;
            if (*q == 0 || *q == '{') break;
            *p = q;
            if (tmp.selector_count < CSS_MAX_RULE_SELECTORS) {
                css_parse_selector(p, &tmp.selectors[tmp.selector_count++]);
            } else {
                CssSelector dummy;
                css_parse_selector(p, &dummy);
            }
            q = *p;
            *p = q;
            css_skip_ws(p);
            q = *p;
            if (*q == ',') { q++; continue; }
            break;
        }
        if (*q != '{') { if (*q) q++; continue; } /* malformed -- resync */
        q++;
        *p = q;
        CssNestCtx nest_ctx;
        nest_ctx.sheet = sheet; nest_ctx.source_order = source_order;
        nest_ctx.mq_min = mq_min; nest_ctx.mq_max = mq_max;
        /* Each argument hoisted into its own plain local FIRST, then
         * passed -- see CssNestCtx's own comment on the real squash
         * codegen bug this avoids: computing several of a call's own
         * arguments as field accesses into the SAME largeish struct
         * (tmp.selectors/tmp.selector_count/&tmp all being fields of
         * this same ~29KB CssRule local) corrupted earlier-assigned
         * argument registers, confirmed via a minimal repro and fixed by
         * this exact hoisting pattern -- the established workaround
         * convention this whole file already uses for the same bug
         * class elsewhere (see css_skip_ws()'s own top comment). */
        CssSelector *nest_parent_sels = tmp.selectors;
        int nest_parent_count = tmp.selector_count;
        CssRule *nest_out = &tmp;
        css_parse_decls_nested(&nest_ctx, p, nest_parent_sels, nest_parent_count, nest_out);
        q = *p;
        if (*q == '}') q++;
        if (tmp.selector_count > 0 && tmp.decl_count > 0) {
            tmp.source_order = (*source_order)++;
            CssRule *r = css_push_rule(sheet);
            /* memcpy(), NOT "*r = tmp;" -- a real, confirmed squash/ARM64
             * codegen bug: a whole-struct assignment through a pointer
             * silently drops/corrupts fields on a struct this large
             * (CssRule is ~29KB), reproduced live on device as every
             * pushed rule's selector_count reading back 0 even though it
             * was set correctly in `tmp` right above. Same bug class, same
             * fix, as every other "whole-struct-assignment" workaround
             * already documented throughout this codebase (e.g. this
             * file's own top comment, php_mini.c's). */
            memcpy(r, &tmp, sizeof(CssRule));
        }
    }
}

void css_parse_into(CssStylesheet *sheet, const char *text) {
    const char *p = text;
    int source_order = sheet->count; /* continue numbering across multiple <style> tags */
    css_parse_rule_body(sheet, &p, &source_order, -1, -1);
}

/* ---- selector matching ---- */

static int css_compound_matches(const CssCompound *c, const DomNode *el) {
    int i;
    for (i = 0; i < c->part_count; i++) {
        const CssSimpleSel *s = &c->parts[i];
        if (s->kind == CSS_SEL_UNIVERSAL) continue;
        if (s->kind == CSS_SEL_TAG) {
            if (strcmp(el->tag, s->name) != 0) return 0;
        } else if (s->kind == CSS_SEL_ID) {
            const char *id = dom_get_attr(el, "id");
            if (!id || strcmp(id, s->name) != 0) return 0;
        } else if (s->kind == CSS_SEL_CLASS) {
            const char *cls = dom_get_attr(el, "class");
            if (!cls) return 0;
            /* Real "class" attribute matching: `cls` is a whitespace-
             * separated list, `s->name` must match one whole token, not
             * just appear as a substring. */
            const char *p = cls;
            int found = 0;
            while (*p && !found) {
                while (*p && isspace((unsigned char)*p)) p++;
                const char *tok = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                int tlen = (int)(p - tok);
                if (tlen == (int)strlen(s->name) && strncmp(tok, s->name, (size_t)tlen) == 0) found = 1;
            }
            if (!found) return 0;
        } else if (s->kind == CSS_SEL_ATTR) {
            const char *av = dom_get_attr(el, s->name);
            if (!av) return 0;
            if (s->value_set && strcmp(av, s->value) != 0) return 0;
        } else if (s->kind == CSS_SEL_HOVER) {
            if (!el->hover) return 0;
        } else if (s->kind == CSS_SEL_CHECKED) {
            if (!el->form_checked) return 0;
        }
    }
    /* An empty (zero-part, unsupported-combinator-target) compound is
     * rejected by css_selector_matches()'s own callers BEFORE this
     * function is ever called on one -- see that function's own comment
     * -- so reaching here with part_count==0 (nothing to check) is a
     * real, if vacuous, match: every check above was trivially satisfied
     * (there were none). */
    return 1;
}

/* The element immediately before `node` among its own parent's ELEMENT
 * children (text nodes skipped, since no compound ever matches one) --
 * real CSS's own "previous sibling" notion, used by the general-sibling
 * ("~") combinator match below. A plain linear scan (find `node`'s own
 * index, then walk backward) -- this file has no cached "index in
 * parent"/"previous sibling" pointer anywhere, and doesn't need one
 * badly enough to add one just for this: css_apply_element() is called
 * once per element per real cascade pass, not in any tighter loop. */
static const DomNode *css_prev_sibling(const DomNode *node) {
    if (!node->parent) return 0;
    const DomNode *parent = node->parent;
    int i, found_idx = -1;
    for (i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) { found_idx = i; break; }
    }
    if (found_idx < 0) return 0;
    for (i = found_idx - 1; i >= 0; i--) {
        if (!dom_is_text(parent->children[i])) return parent->children[i];
    }
    return 0;
}

/* Descendant/child/general-sibling chain match: chain[last] must match
 * `el` itself; each earlier chain[i] relates to whatever chain[i+1]
 * already matched according to chain[i+1]'s OWN combinator flags (see
 * CssCompound's own comments) -- by DEFAULT (plain whitespace) chain[i]
 * must match some ANCESTOR (any ancestor, real CSS's descendant
 * combinator); when chain[i+1].is_child_combinator ("> "), that ancestor
 * must be EXACTLY the immediate parent, not some further-up one; when
 * chain[i+1].is_general_sibling ("~ "), chain[i] must instead match some
 * EARLIER SIBLING (any earlier one, real CSS "~" semantics -- walking
 * css_prev_sibling() repeatedly rather than ->parent). A zero-part
 * compound (the target of the still-unsupported adjacent-sibling "+"
 * combinator, or beyond CSS_MAX_SELECTOR_CHAIN) never matches, failing
 * the whole selector immediately -- same "parses cleanly, degrades to
 * never-matching" convention as everywhere else in this file. */
static int css_selector_matches(const CssSelector *sel, const DomNode *el) {
    if (sel->chain_len == 0) return 0;
    const CssCompound *last = &sel->chain[sel->chain_len - 1];
    if (last->part_count == 0) return 0;
    if (!css_compound_matches(last, el)) return 0;
    if (sel->chain_len == 1) return 1;

    int ci = sel->chain_len - 2;
    const DomNode *matched = el;
    while (ci >= 0) {
        const CssCompound *c = &sel->chain[ci];
        if (c->part_count == 0) return 0;
        if (sel->chain[ci + 1].is_general_sibling) {
            const DomNode *cand = css_prev_sibling(matched);
            int found = 0;
            while (cand) {
                if (css_compound_matches(c, cand)) { found = 1; matched = cand; break; }
                cand = css_prev_sibling(cand);
            }
            if (!found) return 0;
        } else {
            int must_be_immediate = sel->chain[ci + 1].is_child_combinator;
            const DomNode *anc = matched->parent;
            int found = 0;
            while (anc) {
                if (css_compound_matches(c, anc)) { found = 1; matched = anc; break; }
                if (must_be_immediate) break;
                anc = anc->parent;
            }
            if (!found) return 0;
        }
        ci--;
    }
    return 1;
}

/* ---- value parsing ---- */

/* If `v` (after leading whitespace) is a "var(--name, fallback)" /
 * "var(--name)" reference, copies the FALLBACK text (trimmed) into
 * `out` and returns 1; returns 0 (leaving `out` untouched) for anything
 * else, INCLUDING a var() with no fallback at all (real CSS then falls
 * back to the property's own initial value, which this engine has no
 * per-property table for -- "not specified" is the correct, safe
 * degradation already used everywhere else in this file for an
 * unparseable value, so it's left alone rather than guessed at).
 *
 * This engine doesn't implement real CSS custom properties (no
 * *:root { --x: ... }` declaration tracking, no cascade/inheritance of
 * custom property VALUES) -- but real-world CSS, Wikipedia's own very
 * much included, uses "var(--name, fallback)" constantly for colors,
 * sizes, and more (confirmed: "background-color:var(--background-color-
 * base,#fff)", "font-size:var(--font-size-small,0.875rem)", ...), and
 * the fallback is what a real browser renders when (as here) the custom
 * property was never actually set to anything else -- so resolving to
 * the DECLARED FALLBACK is a real, high-value approximation of the
 * common case, not a guess: it's the actual, correct rendered value for
 * any element that isn't inside a scope overriding that custom
 * property. Before this fix, EVERY var()-based value on the page (an
 * enormous fraction of Wikipedia's own real CSS) silently failed to
 * parse at all, leaving color/background/font-size/etc completely
 * unset and falling back to this engine's own per-tag PLACEHOLDER
 * colors instead -- confirmed as the actual cause of the page's header/
 * toolbar area rendering as a solid, wrong, placeholder blue block
 * instead of its real (white/off-white) background.
 *
 * Nesting-aware (a fallback can itself contain a function call with
 * commas, e.g. "var(--a, rgb(1, 2, 3))" -- the split has to happen on
 * the FIRST top-level comma, not the first comma anywhere) and
 * recursive callers (css_parse_color/css_parse_len) re-parse the
 * extracted fallback text the normal way, so "var(--x, var(--y, red))"
 * (a real, if less common, pattern) also resolves correctly by
 * unwrapping one var() at a time. */
static int css_resolve_var_fallback(const char *v, char *out, int outsz) {
    while (*v && isspace((unsigned char)*v)) v++;
    if (strncmp(v, "var(", 4) != 0) return 0;
    const char *p = v + 4;
    int depth = 1;
    const char *comma = 0;
    while (*p && depth > 0) {
        if (*p == '(') depth++;
        else if (*p == ')') { depth--; if (depth == 0) break; }
        else if (*p == ',' && depth == 1 && !comma) comma = p;
        p++;
    }
    if (*p != ')' || !comma) return 0; /* malformed, or no fallback given */
    const char *fb = comma + 1;
    while (*fb && isspace((unsigned char)*fb)) fb++;
    const char *fb_end = p;
    while (fb_end > fb && isspace((unsigned char)fb_end[-1])) fb_end--;
    int len = (int)(fb_end - fb);
    if (len >= outsz) len = outsz - 1;
    if (len < 0) len = 0;
    memcpy(out, fb, (size_t)len);
    out[len] = 0;
    return 1;
}

static int css_parse_len(const char *v, float *out) {
    while (*v && isspace((unsigned char)*v)) v++;
    if (strncmp(v, "auto", 4) == 0) return 0;
    {
        char fb[192];
        if (css_resolve_var_fallback(v, fb, (int)sizeof fb)) {
            return css_parse_len(fb, out);
        }
    }
    char *end;
    double d = strtod(v, &end);
    if (end == v) return 0;
    /* px and unitless both taken as px; "rem" is resolved against a fixed
     * 16px root font-size (real CSS's own default, and this engine has no
     * notion of a page-configurable root font-size to resolve it against
     * more precisely) -- a real, useful unit to support: confirmed via
     * real Wikipedia CSS that its own grid track sizes (the sidebar-
     * beside-content layout) are specified in rem ("12.25rem
     * minmax(0,1fr)"), so leaving rem unresolved meant that whole layout
     * silently fell back to "not specified" everywhere it mattered. "em"
     * is deliberately NOT resolved the same way: it's relative to the
     * CURRENT element's own font-size, not a fixed root value, and this
     * function has no access to that context -- resolving it as if it
     * were rem would silently produce a wrong size instead of correctly
     * falling back to "not specified", so it stays unsupported. Anything
     * else (%, vw, vh, ...) is unchanged -- still "not specified". */
    while (*end && isspace((unsigned char)*end)) end++;
    if (strncmp(end, "px", 2) == 0) { *out = (float)d; return 1; }
    if (strncmp(end, "rem", 3) == 0) { *out = (float)d * 16.0f; return 1; }
    if (*end != 0) return 0;
    *out = (float)d;
    return 1;
}

static int css_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

typedef struct { const char *name; unsigned char r, g, b; } CssNamedColor;
/* Expanded from the original 16-color table (basically just the CSS
 * Level 1 set) after confirming, via real testing, that plenty of
 * ordinary CSS -- including this project's own newly-added test pages --
 * uses real CSS3/SVG extended color keywords ("crimson", "goldenrod", ...)
 * that weren't recognized at all, silently leaving css_has_bg/color unset
 * (the same safe-degrade "unrecognized value simply doesn't apply"
 * convention as everywhere else in this engine, but for names this common
 * it was costing real, visible correctness). Kept in sync with
 * svg_render.c's own g_svg_named_colors table (SVG uses the identical
 * CSS3 color keyword set) -- not shared code, just a consistent color
 * list independently duplicated in both files, matching how this
 * codebase already treats its 2D-affine-matrix convention.
 *
 * Populated at runtime by init_css_named_colors() below, NOT a static
 * brace initializer -- a real, confirmed squash bug found via direct
 * testing while expanding this table from 16 to ~60 entries: colors that
 * worked fine in the small table (e.g. "teal", "orange") started silently
 * failing to match at all once the array grew this large, even though
 * squash compiled the file with no error or warning. Matches this
 * project's own already-established workaround for the identical class of
 * bug (see SQW/*_spirv.h's own top comment: "squash's C parser chokes on
 * huge static array initializers" -- apparently including one this much
 * smaller than a compiled SPIR-V array, just with pointer-typed (string
 * literal) members instead of plain uint32_t words). */
#define CSS_NAMED_COLORS_COUNT 70
static CssNamedColor CSS_NAMED_COLORS[CSS_NAMED_COLORS_COUNT];
static int g_css_named_colors_inited = 0;

static void init_css_named_colors(void) {
    int i = 0;
    CSS_NAMED_COLORS[i].name = "black"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "white"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "red"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "green"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "blue"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "yellow"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "cyan"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "magenta"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "gray"; CSS_NAMED_COLORS[i].r=128; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=128; i++;
    CSS_NAMED_COLORS[i].name = "grey"; CSS_NAMED_COLORS[i].r=128; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=128; i++;
    CSS_NAMED_COLORS[i].name = "silver"; CSS_NAMED_COLORS[i].r=192; CSS_NAMED_COLORS[i].g=192; CSS_NAMED_COLORS[i].b=192; i++;
    CSS_NAMED_COLORS[i].name = "maroon"; CSS_NAMED_COLORS[i].r=128; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "olive"; CSS_NAMED_COLORS[i].r=128; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "lime"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "aqua"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "teal"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=128; i++;
    CSS_NAMED_COLORS[i].name = "navy"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=128; i++;
    CSS_NAMED_COLORS[i].name = "fuchsia"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "purple"; CSS_NAMED_COLORS[i].r=128; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=128; i++;
    CSS_NAMED_COLORS[i].name = "orange"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=165; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "brown"; CSS_NAMED_COLORS[i].r=165; CSS_NAMED_COLORS[i].g=42; CSS_NAMED_COLORS[i].b=42; i++;
    CSS_NAMED_COLORS[i].name = "pink"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=192; CSS_NAMED_COLORS[i].b=203; i++;
    CSS_NAMED_COLORS[i].name = "gold"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=215; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "indigo"; CSS_NAMED_COLORS[i].r=75; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=130; i++;
    CSS_NAMED_COLORS[i].name = "violet"; CSS_NAMED_COLORS[i].r=238; CSS_NAMED_COLORS[i].g=130; CSS_NAMED_COLORS[i].b=238; i++;
    CSS_NAMED_COLORS[i].name = "coral"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=127; CSS_NAMED_COLORS[i].b=80; i++;
    CSS_NAMED_COLORS[i].name = "salmon"; CSS_NAMED_COLORS[i].r=250; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=114; i++;
    CSS_NAMED_COLORS[i].name = "khaki"; CSS_NAMED_COLORS[i].r=240; CSS_NAMED_COLORS[i].g=230; CSS_NAMED_COLORS[i].b=140; i++;
    CSS_NAMED_COLORS[i].name = "plum"; CSS_NAMED_COLORS[i].r=221; CSS_NAMED_COLORS[i].g=160; CSS_NAMED_COLORS[i].b=221; i++;
    CSS_NAMED_COLORS[i].name = "orchid"; CSS_NAMED_COLORS[i].r=218; CSS_NAMED_COLORS[i].g=112; CSS_NAMED_COLORS[i].b=214; i++;
    CSS_NAMED_COLORS[i].name = "tan"; CSS_NAMED_COLORS[i].r=210; CSS_NAMED_COLORS[i].g=180; CSS_NAMED_COLORS[i].b=140; i++;
    CSS_NAMED_COLORS[i].name = "beige"; CSS_NAMED_COLORS[i].r=245; CSS_NAMED_COLORS[i].g=245; CSS_NAMED_COLORS[i].b=220; i++;
    CSS_NAMED_COLORS[i].name = "ivory"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=240; i++;
    CSS_NAMED_COLORS[i].name = "lavender"; CSS_NAMED_COLORS[i].r=230; CSS_NAMED_COLORS[i].g=230; CSS_NAMED_COLORS[i].b=250; i++;
    CSS_NAMED_COLORS[i].name = "crimson"; CSS_NAMED_COLORS[i].r=220; CSS_NAMED_COLORS[i].g=20; CSS_NAMED_COLORS[i].b=60; i++;
    CSS_NAMED_COLORS[i].name = "chocolate"; CSS_NAMED_COLORS[i].r=210; CSS_NAMED_COLORS[i].g=105; CSS_NAMED_COLORS[i].b=30; i++;
    CSS_NAMED_COLORS[i].name = "darkgray"; CSS_NAMED_COLORS[i].r=169; CSS_NAMED_COLORS[i].g=169; CSS_NAMED_COLORS[i].b=169; i++;
    CSS_NAMED_COLORS[i].name = "darkgrey"; CSS_NAMED_COLORS[i].r=169; CSS_NAMED_COLORS[i].g=169; CSS_NAMED_COLORS[i].b=169; i++;
    CSS_NAMED_COLORS[i].name = "lightgray"; CSS_NAMED_COLORS[i].r=211; CSS_NAMED_COLORS[i].g=211; CSS_NAMED_COLORS[i].b=211; i++;
    CSS_NAMED_COLORS[i].name = "lightgrey"; CSS_NAMED_COLORS[i].r=211; CSS_NAMED_COLORS[i].g=211; CSS_NAMED_COLORS[i].b=211; i++;
    CSS_NAMED_COLORS[i].name = "darkred"; CSS_NAMED_COLORS[i].r=139; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "darkgreen"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=100; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "darkblue"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=0; CSS_NAMED_COLORS[i].b=139; i++;
    CSS_NAMED_COLORS[i].name = "lightblue"; CSS_NAMED_COLORS[i].r=173; CSS_NAMED_COLORS[i].g=216; CSS_NAMED_COLORS[i].b=230; i++;
    CSS_NAMED_COLORS[i].name = "lightgreen"; CSS_NAMED_COLORS[i].r=144; CSS_NAMED_COLORS[i].g=238; CSS_NAMED_COLORS[i].b=144; i++;
    CSS_NAMED_COLORS[i].name = "steelblue"; CSS_NAMED_COLORS[i].r=70; CSS_NAMED_COLORS[i].g=130; CSS_NAMED_COLORS[i].b=180; i++;
    CSS_NAMED_COLORS[i].name = "skyblue"; CSS_NAMED_COLORS[i].r=135; CSS_NAMED_COLORS[i].g=206; CSS_NAMED_COLORS[i].b=235; i++;
    CSS_NAMED_COLORS[i].name = "dimgray"; CSS_NAMED_COLORS[i].r=105; CSS_NAMED_COLORS[i].g=105; CSS_NAMED_COLORS[i].b=105; i++;
    CSS_NAMED_COLORS[i].name = "dimgrey"; CSS_NAMED_COLORS[i].r=105; CSS_NAMED_COLORS[i].g=105; CSS_NAMED_COLORS[i].b=105; i++;
    CSS_NAMED_COLORS[i].name = "slategray"; CSS_NAMED_COLORS[i].r=112; CSS_NAMED_COLORS[i].g=128; CSS_NAMED_COLORS[i].b=144; i++;
    CSS_NAMED_COLORS[i].name = "whitesmoke"; CSS_NAMED_COLORS[i].r=245; CSS_NAMED_COLORS[i].g=245; CSS_NAMED_COLORS[i].b=245; i++;
    CSS_NAMED_COLORS[i].name = "gainsboro"; CSS_NAMED_COLORS[i].r=220; CSS_NAMED_COLORS[i].g=220; CSS_NAMED_COLORS[i].b=220; i++;
    CSS_NAMED_COLORS[i].name = "lightyellow"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=224; i++;
    CSS_NAMED_COLORS[i].name = "deepskyblue"; CSS_NAMED_COLORS[i].r=0; CSS_NAMED_COLORS[i].g=191; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "forestgreen"; CSS_NAMED_COLORS[i].r=34; CSS_NAMED_COLORS[i].g=139; CSS_NAMED_COLORS[i].b=34; i++;
    CSS_NAMED_COLORS[i].name = "firebrick"; CSS_NAMED_COLORS[i].r=178; CSS_NAMED_COLORS[i].g=34; CSS_NAMED_COLORS[i].b=34; i++;
    CSS_NAMED_COLORS[i].name = "royalblue"; CSS_NAMED_COLORS[i].r=65; CSS_NAMED_COLORS[i].g=105; CSS_NAMED_COLORS[i].b=225; i++;
    CSS_NAMED_COLORS[i].name = "midnightblue"; CSS_NAMED_COLORS[i].r=25; CSS_NAMED_COLORS[i].g=25; CSS_NAMED_COLORS[i].b=112; i++;
    CSS_NAMED_COLORS[i].name = "turquoise"; CSS_NAMED_COLORS[i].r=64; CSS_NAMED_COLORS[i].g=224; CSS_NAMED_COLORS[i].b=208; i++;
    CSS_NAMED_COLORS[i].name = "goldenrod"; CSS_NAMED_COLORS[i].r=218; CSS_NAMED_COLORS[i].g=165; CSS_NAMED_COLORS[i].b=32; i++;
    CSS_NAMED_COLORS[i].name = "seagreen"; CSS_NAMED_COLORS[i].r=46; CSS_NAMED_COLORS[i].g=139; CSS_NAMED_COLORS[i].b=87; i++;
    CSS_NAMED_COLORS[i].name = "darkorange"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=140; CSS_NAMED_COLORS[i].b=0; i++;
    CSS_NAMED_COLORS[i].name = "transparent"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=255; i++;
    CSS_NAMED_COLORS[i].name = "none"; CSS_NAMED_COLORS[i].r=255; CSS_NAMED_COLORS[i].g=255; CSS_NAMED_COLORS[i].b=255; i++;
    /* i now equals CSS_NAMED_COLORS_COUNT-1 -- one slot deliberately left
     * as a NULL-name sentinel (matching the old brace-initializer's own
     * trailing {0,0,0,0}), since the matching loop below scans until it
     * sees name==0, not a separately-tracked count. */
    CSS_NAMED_COLORS[i].name = 0;
    g_css_named_colors_inited = 1;
}

static int css_parse_color(const char *v, float *rgb) {
    if (!g_css_named_colors_inited) init_css_named_colors();
    while (*v && isspace((unsigned char)*v)) v++;
    {
        char fb[192];
        if (css_resolve_var_fallback(v, fb, (int)sizeof fb)) {
            return css_parse_color(fb, rgb);
        }
    }
    if (*v == '#') {
        v++;
        int len = 0; const char *p = v;
        while (isxdigit((unsigned char)*p)) { p++; len++; }
        if (len == 3) {
            int r = css_hex_digit(v[0]), g = css_hex_digit(v[1]), b = css_hex_digit(v[2]);
            rgb[0] = (float)(r * 17) / 255.0f; rgb[1] = (float)(g * 17) / 255.0f; rgb[2] = (float)(b * 17) / 255.0f;
            return 1;
        } else if (len >= 6) {
            int r = css_hex_digit(v[0]) * 16 + css_hex_digit(v[1]);
            int g = css_hex_digit(v[2]) * 16 + css_hex_digit(v[3]);
            int b = css_hex_digit(v[4]) * 16 + css_hex_digit(v[5]);
            rgb[0] = (float)r / 255.0f; rgb[1] = (float)g / 255.0f; rgb[2] = (float)b / 255.0f;
            return 1;
        }
        return 0;
    }
    if (strncmp(v, "rgb", 3) == 0) {
        const char *paren = strchr(v, '(');
        if (!paren) return 0;
        int r = 0, g = 0, b = 0;
        if (sscanf(paren + 1, "%d , %d , %d", &r, &g, &b) == 3 ||
            sscanf(paren + 1, "%d,%d,%d", &r, &g, &b) == 3) {
            rgb[0] = (float)r / 255.0f; rgb[1] = (float)g / 255.0f; rgb[2] = (float)b / 255.0f;
            return 1;
        }
        return 0;
    }
    {
        int i;
        for (i = 0; CSS_NAMED_COLORS[i].name; i++) {
            int j; int match = 1;
            for (j = 0; CSS_NAMED_COLORS[i].name[j]; j++) {
                char a = (char)tolower((unsigned char)v[j]);
                if (a != CSS_NAMED_COLORS[i].name[j]) { match = 0; break; }
            }
            if (match && !isalpha((unsigned char)v[j])) {
                rgb[0] = (float)CSS_NAMED_COLORS[i].r / 255.0f;
                rgb[1] = (float)CSS_NAMED_COLORS[i].g / 255.0f;
                rgb[2] = (float)CSS_NAMED_COLORS[i].b / 255.0f;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- computed-style resolution ---- */

static void css_set_default_style(DomNode *el) {
    const char *t = el->tag;
    int block =
        strcmp(t,"html")==0 || strcmp(t,"body")==0 || strcmp(t,"div")==0 || strcmp(t,"p")==0 ||
        strcmp(t,"center")==0 || strcmp(t,"pre")==0 || strcmp(t,"header")==0 || strcmp(t,"footer")==0 ||
        strcmp(t,"nav")==0 || strcmp(t,"section")==0 || strcmp(t,"article")==0 || strcmp(t,"aside")==0 ||
        strcmp(t,"main")==0 || strcmp(t,"ul")==0 || strcmp(t,"ol")==0 || strcmp(t,"li")==0 ||
        strcmp(t,"h1")==0 || strcmp(t,"h2")==0 || strcmp(t,"h3")==0 || strcmp(t,"h4")==0 ||
        strcmp(t,"h5")==0 || strcmp(t,"h6")==0 || strcmp(t,"blockquote")==0 || strcmp(t,"figure")==0 ||
        strcmp(t,"figcaption")==0 || strcmp(t,"table")==0 || strcmp(t,"tr")==0 || strcmp(t,"form")==0 ||
        /* Real, confirmed bug fix (found on a real Wikipedia page):
         * "td"/"th"/"tbody"/"thead"/"tfoot" were missing from this list
         * entirely, defaulting to css_display=INLINE (this function's
         * own fallback for anything not explicitly listed as block).
         * layout.c's per-child dispatch has no case for a display:INLINE
         * element that's also not one of the recognized "atomic inline"
         * tags (span/a/button/...) and not <img> -- its own comment
         * documents this exactly: "any other tag ... is skipped
         * entirely -- not visited, not descended into." A <td>/<th>
         * matched NONE of layout.c's cases, so every table cell's own
         * content -- on a real page, an infobox's image and key-facts
         * rows, or any "wikitable" data table -- was silently dropped
         * from layout altogether (confirmed: every real <table> box
         * measured at essentially zero height). This engine has no real
         * table-layout algorithm (rows/columns/cell alignment) at all,
         * so treating a cell as an ordinary block (stacked top-to-
         * bottom, like everything else this simplified model handles)
         * is a real, documented simplification -- cells within one row
         * render as separate stacked blocks rather than side-by-side
         * columns -- but is enormously better than the cell's entire
         * content being invisible, which is what happened before this
         * fix on literally every table on every page. */
        strcmp(t,"td")==0 || strcmp(t,"th")==0 || strcmp(t,"tbody")==0 ||
        strcmp(t,"thead")==0 || strcmp(t,"tfoot")==0 || strcmp(t,"caption")==0;
    el->css_display = block ? CSS_DISPLAY_BLOCK : CSS_DISPLAY_INLINE;
    el->css_has_width = 0; el->css_has_height = 0;
    el->css_width_is_percent = 0; el->css_height_is_percent = 0;
    /* Deliberately 4 separate statements, not one chained
     * "a=b=c=d=0.0f;" -- a real, confirmed squash codegen bug (a minimal
     * standalone repro: "s.w[0]=s.w[1]=s.w[2]=s.w[3]=99.0f;" on a struct
     * float array field only assigns index 0 under squash, gcc-compiled
     * control assigns all 4) made every one of this file's own multi-
     * target chained assignments for css_margin/css_padding -- including
     * the single-value and 2-value shorthand forms below, e.g. a plain
     * "margin: 10px;" -- silently only ever set the FIRST array slot,
     * leaving the rest at whatever they already were. A fix for the
     * compiler bug itself may land separately in codegen.c; every
     * chained assignment in this function is rewritten to explicit
     * separate statements regardless, since that's correct either way. */
    el->css_margin[0] = 0.0f; el->css_margin[1] = 0.0f; el->css_margin[2] = 0.0f; el->css_margin[3] = 0.0f;
    el->css_padding[0] = 0.0f; el->css_padding[1] = 0.0f; el->css_padding[2] = 0.0f; el->css_padding[3] = 0.0f;
    el->css_border_width[0] = 0.0f; el->css_border_width[1] = 0.0f; el->css_border_width[2] = 0.0f; el->css_border_width[3] = 0.0f;
    el->css_has_color = 0; el->css_has_bg = 0; el->css_has_border_color = 0;
    el->css_flex_direction = CSS_FLEX_ROW;
    el->css_flex_wrap = 0;
    el->css_justify = CSS_JUSTIFY_START;
    el->css_align = CSS_ALIGN_START;
    el->css_gap = 0.0f;
    el->css_flex_grow = 0.0f;
    el->css_grid_template_columns[0] = 0;
    el->css_grid_template_areas[0] = 0;
    el->css_grid_area[0] = 0;
    el->css_position_absolute = 0;
    el->css_has_clip = 0;
    el->css_text_decoration = 0;
    el->css_has_z_index = 0; el->css_z_index = 0;
    el->css_overflow_hidden = 0;
    el->css_bg_image_url[0] = 0;
    el->css_border_radius = 0.0f;
    el->css_has_box_shadow = 0;
    el->css_has_transform = 0;

    /* Real CSS inheritance: seed from the parent's ALREADY-RESOLVED
     * computed style (guaranteed resolved first -- css_apply()'s own walk
     * is pre-order, parent before any child) rather than a fixed default,
     * for the handful of properties real CSS actually inherits. Every
     * other property above (display, margin/padding/border, background,
     * flex/grid, position) is correctly NOT inherited -- those keep their
     * own fixed per-tag/zero defaults regardless of the parent, matching
     * real CSS. color is deliberately left out here even though it IS a
     * real inherited property -- sqw_main.c's own draw pass already walks
     * up to the nearest css_has_color ancestor itself (see its own
     * comment), so resolving it a second time here would be redundant,
     * not wrong, but is skipped to avoid two sources of truth. opacity is
     * real CSS's one common NOT-inherited property (a child's opacity is
     * independent of its parent's), so it always resets to fully opaque
     * here regardless of the parent. */
    DomNode *p = el->parent;
    if (p) {
        /* Read through the LOCAL "p", never "el->parent->field" directly
         * -- a real, confirmed squash codegen bug (found this session):
         * chaining two "->" hops in one expression to read a field of the
         * struct a struct-POINTER-FIELD points to reads back garbage,
         * even though "p->field" through an ordinary local pointer
         * variable holding the exact same address works correctly. Same
         * underlying bug class as (but a different specific shape than)
         * php_mini.c's own documented "struct-field-via-arrow comparison"
         * bug and this file's own "**p" double-dereference bug -- see
         * this file's top comment. */
        el->css_font_size = p->css_font_size;
        el->css_text_align = p->css_text_align;
        el->css_line_height = p->css_line_height;
        el->css_font_weight_bold = p->css_font_weight_bold;
    } else {
        el->css_font_size = 16.0f;   /* real CSS root default */
        el->css_text_align = 0;      /* left */
        el->css_line_height = 0.0f;  /* "normal" */
        el->css_font_weight_bold = 0;
    }
    /* Real UA-stylesheet defaults (applied AFTER inheritance, exactly
     * like a real browser's own user-agent stylesheet -- a page's own
     * CSS, applied later in css_apply_element(), still overrides this
     * normally): <b>/<strong> render bold, and so does <th> -- real
     * HTML5 gives table headers a bold, centered default even with zero
     * page CSS. Found missing while building this engine's first real
     * table test page: css_font_weight_bold was ONLY ever settable via
     * an explicit "font-weight" CSS declaration, so a bare "<b>bold</b>"
     * or "<th>Header</th>" with no matching CSS rule rendered as
     * perfectly plain text, unlike any real browser. (":italic" has the
     * same kind of gap for <i>/<em> but is a separate, pre-existing,
     * documented limitation -- this engine's single baked font atlas has
     * no slanted glyph variant to switch to, so there's no real fix
     * available at this layer the way bold's separate glyph weight
     * allows.) */
    if (strcmp(t,"b")==0 || strcmp(t,"strong")==0 || strcmp(t,"th")==0) el->css_font_weight_bold = 1;
    if (strcmp(t,"th")==0) el->css_text_align = 1; /* center */
    el->css_opacity = 1.0f;
}

/* Extracts the raw text inside "url(...)" (optionally single/double
 * quoted, both real forms) into `out` -- used for background-image/
 * background's own url() component. `u` must point at the "url(" itself
 * (i.e. the caller already found it via strstr). Leaves `out` empty if
 * nothing sensible is found (e.g. a bare "url()"), same safe-degrade
 * convention as every other parser in this file. */
static void css_extract_url(const char *u, char *out, int outsz) {
    out[0] = 0;
    u += 4; /* skip "url(" */
    while (*u == ' ' || *u == '\t') u++;
    if (*u == '"' || *u == '\'') u++;
    int i = 0;
    while (*u && *u != ')' && *u != '"' && *u != '\'' && i < outsz - 1) out[i++] = *u++;
    out[i] = 0;
}

static void css_apply_decl(DomNode *el, const char *name, const char *value) {
    float f;
    float rgb[3];
    if (strcmp(name, "display") == 0) {
        if (strstr(value, "none")) el->css_display = CSS_DISPLAY_NONE;
        else if (strstr(value, "inline-block")) el->css_display = CSS_DISPLAY_INLINE_BLOCK;
        else if (strstr(value, "flex")) el->css_display = CSS_DISPLAY_FLEX;
        else if (strstr(value, "grid")) el->css_display = CSS_DISPLAY_GRID;
        else if (strstr(value, "inline")) el->css_display = CSS_DISPLAY_INLINE;
        else if (strstr(value, "block")) el->css_display = CSS_DISPLAY_BLOCK;
    } else if (strcmp(name, "width") == 0) {
        if (css_parse_len(value, &f)) { el->css_width = f; el->css_has_width = 1; el->css_width_is_percent = 0; }
        /* Real, confirmed bug fix: "width:auto"/"height:auto" (checked
         * separately just below) previously did NOTHING at all --
         * css_parse_len() correctly recognizes "auto" as a real CSS
         * keyword (its very first check) but returns failure for it
         * (0, no length to report), and this call site's own "only
         * assign on success" pattern then just left css_has_width/
         * css_has_height whatever they ALREADY were. That's silently
         * WRONG whenever a LATER, higher-priority rule sets "auto" to
         * explicitly override an EARLIER rule's real numeric length --
         * exactly the shape of a real, confirmed bug found while
         * testing this engine's new ":checked" + general-sibling-
         * combinator support (CSS_SEL_CHECKED, this file's own top-of-
         * struct comment in css.h): a real dropdown's default,
         * collapsed rule sets "height:0" (correctly triggering this
         * file's own position:absolute+height<=0 "treat as collapsed"
         * heuristic just below in css_apply_element()), and the
         * ":checked ~ .content" rule that's supposed to REVEAL it on
         * click sets "height:auto" specifically to countermand that --
         * but since "auto" was a silent no-op, css_has_height/
         * css_height kept remembering the EARLIER "0" value forever,
         * so the collapse heuristic kept firing even after a real
         * click made :checked genuinely match. Real CSS "auto" means
         * "revert to content-driven sizing", which in this engine's own
         * terms is exactly what css_has_width/css_has_height==0 (no
         * explicit size at all) already means -- so "auto" now
         * explicitly CLEARS the flag instead of leaving it untouched. */
        else if (strcmp(value, "auto") == 0) { el->css_has_width = 0; el->css_width_is_percent = 0; }
        else {
            /* Real percentage width support -- see DomNode's own
             * css_width_is_percent comment (dom.h) for why the percent
             * value is stored RAW here (not resolved into a real px
             * width yet -- the containing block's own width isn't known
             * during this top-down CSS cascade pass) and re-resolved by
             * layout.c once it is. css_has_width is still set (matching
             * every other successful-parse branch here) so any existing
             * "does this element have an explicit width" check
             * elsewhere keeps working unchanged. */
            char *end; double pct = strtod(value, &end);
            if (end != value && *end == '%') {
                el->css_width_percent = (float)pct; el->css_width_is_percent = 1; el->css_has_width = 1;
            }
        }
    } else if (strcmp(name, "height") == 0) {
        if (css_parse_len(value, &f)) { el->css_height = f; el->css_has_height = 1; el->css_height_is_percent = 0; }
        else if (strcmp(value, "auto") == 0) { el->css_has_height = 0; el->css_height_is_percent = 0; }
        else {
            char *end; double pct = strtod(value, &end);
            if (end != value && *end == '%') {
                el->css_height_percent = (float)pct; el->css_height_is_percent = 1; el->css_has_height = 1;
            }
        }
    } else if (strcmp(name, "margin") == 0) {
        float v[4]; int n = 0;
        char buf[192]; strncpy(buf, value, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        char *tok = strtok(buf, " \t");
        while (tok && n < 4) { if (css_parse_len(tok, &v[n])) n++; else v[n++] = 0; tok = strtok(0, " \t"); }
        /* Explicit separate assignments, not chained -- see this
         * function's own opening comment on the real squash codegen bug
         * chained multi-target assignment hits here. */
        if (n == 1) { el->css_margin[0]=v[0]; el->css_margin[1]=v[0]; el->css_margin[2]=v[0]; el->css_margin[3]=v[0]; }
        else if (n == 2) { el->css_margin[0]=v[0]; el->css_margin[2]=v[0]; el->css_margin[1]=v[1]; el->css_margin[3]=v[1]; }
        else if (n == 3) { el->css_margin[0]=v[0]; el->css_margin[1]=v[1]; el->css_margin[3]=v[1]; el->css_margin[2]=v[2]; }
        else if (n == 4) { el->css_margin[0]=v[0]; el->css_margin[1]=v[1]; el->css_margin[2]=v[2]; el->css_margin[3]=v[3]; }
    } else if (strcmp(name, "margin-top") == 0) { if (css_parse_len(value, &f)) el->css_margin[0] = f; }
    else if (strcmp(name, "margin-right") == 0) { if (css_parse_len(value, &f)) el->css_margin[1] = f; }
    else if (strcmp(name, "margin-bottom") == 0) { if (css_parse_len(value, &f)) el->css_margin[2] = f; }
    else if (strcmp(name, "margin-left") == 0) { if (css_parse_len(value, &f)) el->css_margin[3] = f; }
    else if (strcmp(name, "padding") == 0) {
        float v[4]; int n = 0;
        char buf[192]; strncpy(buf, value, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        char *tok = strtok(buf, " \t");
        while (tok && n < 4) { if (css_parse_len(tok, &v[n])) n++; else v[n++] = 0; tok = strtok(0, " \t"); }
        if (n == 1) { el->css_padding[0]=v[0]; el->css_padding[1]=v[0]; el->css_padding[2]=v[0]; el->css_padding[3]=v[0]; }
        else if (n == 2) { el->css_padding[0]=v[0]; el->css_padding[2]=v[0]; el->css_padding[1]=v[1]; el->css_padding[3]=v[1]; }
        else if (n == 3) { el->css_padding[0]=v[0]; el->css_padding[1]=v[1]; el->css_padding[3]=v[1]; el->css_padding[2]=v[2]; }
        else if (n == 4) { el->css_padding[0]=v[0]; el->css_padding[1]=v[1]; el->css_padding[2]=v[2]; el->css_padding[3]=v[3]; }
    } else if (strcmp(name, "padding-top") == 0) { if (css_parse_len(value, &f)) el->css_padding[0] = f; }
    else if (strcmp(name, "padding-right") == 0) { if (css_parse_len(value, &f)) el->css_padding[1] = f; }
    else if (strcmp(name, "padding-bottom") == 0) { if (css_parse_len(value, &f)) el->css_padding[2] = f; }
    else if (strcmp(name, "padding-left") == 0) { if (css_parse_len(value, &f)) el->css_padding[3] = f; }
    else if (strcmp(name, "border-width") == 0) {
        float v[4]; int n = 0;
        char buf[192]; strncpy(buf, value, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        char *tok = strtok(buf, " \t");
        while (tok && n < 4) { if (css_parse_len(tok, &v[n])) n++; else v[n++] = 0; tok = strtok(0, " \t"); }
        if (n == 1) { el->css_border_width[0]=v[0]; el->css_border_width[1]=v[0]; el->css_border_width[2]=v[0]; el->css_border_width[3]=v[0]; }
        else if (n == 2) { el->css_border_width[0]=v[0]; el->css_border_width[2]=v[0]; el->css_border_width[1]=v[1]; el->css_border_width[3]=v[1]; }
        else if (n == 3) { el->css_border_width[0]=v[0]; el->css_border_width[1]=v[1]; el->css_border_width[3]=v[1]; el->css_border_width[2]=v[2]; }
        else if (n == 4) { el->css_border_width[0]=v[0]; el->css_border_width[1]=v[1]; el->css_border_width[2]=v[2]; el->css_border_width[3]=v[3]; }
    } else if (strcmp(name, "border-top-width") == 0) { if (css_parse_len(value, &f)) el->css_border_width[0] = f; }
    else if (strcmp(name, "border-right-width") == 0) { if (css_parse_len(value, &f)) el->css_border_width[1] = f; }
    else if (strcmp(name, "border-bottom-width") == 0) { if (css_parse_len(value, &f)) el->css_border_width[2] = f; }
    else if (strcmp(name, "border-left-width") == 0) { if (css_parse_len(value, &f)) el->css_border_width[3] = f; }
    else if (strcmp(name, "border-color") == 0) {
        if (css_parse_color(value, rgb)) { el->css_border_color[0]=rgb[0]; el->css_border_color[1]=rgb[1]; el->css_border_color[2]=rgb[2]; el->css_has_border_color = 1; }
    } else if (strcmp(name, "border-style") == 0) {
        /* No visual line-style distinction is drawn yet (see
         * css_border_width's own dom.h comment), but "none"/"hidden"
         * still needs to zero the width -- real CSS: a border-style of
         * none/hidden suppresses the border entirely regardless of
         * whatever width was set, and layout.c uses width alone to
         * decide how much box-model space to reserve. */
        if (strstr(value, "none") || strstr(value, "hidden")) {
            el->css_border_width[0] = 0; el->css_border_width[1] = 0; el->css_border_width[2] = 0; el->css_border_width[3] = 0;
        }
    } else if (strcmp(name, "border") == 0 ||
               strcmp(name, "border-top") == 0 || strcmp(name, "border-right") == 0 ||
               strcmp(name, "border-bottom") == 0 || strcmp(name, "border-left") == 0) {
        /* Shorthand: any order/subset of "<width> <style> <color>", e.g.
         * "border: 1px solid #ccc;" or "border-top: 2px dashed red;" --
         * each whitespace-separated token is classified independently
         * (a length -> width, a recognized style keyword -> style, else
         * try as a color) rather than requiring a fixed order, the same
         * approach flex-flow's own parsing above already uses. */
        float w = -1.0f; float border_rgb[3]; int has_color = 0; int is_none = 0;
        char buf[192]; strncpy(buf, value, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        char *tok = strtok(buf, " \t");
        while (tok) {
            float lf;
            if (strcmp(tok, "none") == 0 || strcmp(tok, "hidden") == 0) {
                is_none = 1;
            } else if (strcmp(tok, "solid") == 0 || strcmp(tok, "dashed") == 0 || strcmp(tok, "dotted") == 0 ||
                       strcmp(tok, "double") == 0 || strcmp(tok, "groove") == 0 || strcmp(tok, "ridge") == 0 ||
                       strcmp(tok, "inset") == 0 || strcmp(tok, "outset") == 0) {
                /* recognized style keyword, no visual effect yet -- consumed so it isn't mistaken for a color below */
            } else if (css_parse_len(tok, &lf)) {
                w = lf;
            } else if (css_parse_color(tok, border_rgb)) {
                has_color = 1;
            }
            tok = strtok(0, " \t");
        }
        if (is_none) w = 0.0f;
        if (w >= 0.0f) {
            if (strcmp(name, "border") == 0) { el->css_border_width[0]=w; el->css_border_width[1]=w; el->css_border_width[2]=w; el->css_border_width[3]=w; }
            else if (strcmp(name, "border-top") == 0) el->css_border_width[0] = w;
            else if (strcmp(name, "border-right") == 0) el->css_border_width[1] = w;
            else if (strcmp(name, "border-bottom") == 0) el->css_border_width[2] = w;
            else if (strcmp(name, "border-left") == 0) el->css_border_width[3] = w;
        }
        if (has_color) { el->css_border_color[0]=border_rgb[0]; el->css_border_color[1]=border_rgb[1]; el->css_border_color[2]=border_rgb[2]; el->css_has_border_color = 1; }
    }
    else if (strcmp(name, "color") == 0) { if (css_parse_color(value, rgb)) { el->css_color[0]=rgb[0]; el->css_color[1]=rgb[1]; el->css_color[2]=rgb[2]; el->css_has_color = 1; } }
    else if (strcmp(name, "background-color") == 0 || strcmp(name, "background") == 0) {
        if (css_parse_color(value, rgb)) { el->css_bg[0]=rgb[0]; el->css_bg[1]=rgb[1]; el->css_bg[2]=rgb[2]; el->css_has_bg = 1; }
        /* "background" is also a real shorthand for background-image
         * (among other components this engine doesn't model -- position/
         * repeat/size/attachment) -- a bare "background-color" never
         * contains "url(", so this is safe to run unconditionally for
         * both branches of this "else if". */
        const char *u = strstr(value, "url(");
        if (u) css_extract_url(u, el->css_bg_image_url, (int)sizeof el->css_bg_image_url);
    } else if (strcmp(name, "background-image") == 0) {
        const char *u = strstr(value, "url(");
        if (u) css_extract_url(u, el->css_bg_image_url, (int)sizeof el->css_bg_image_url);
    } else if (strcmp(name, "text-decoration") == 0 || strcmp(name, "text-decoration-line") == 0) {
        /* Checked in this order (not "none" first) since a value can list
         * multiple lines ("underline line-through") -- line-through wins
         * when both are present, an arbitrary but harmless tie-break
         * (this engine only ever draws one line per element anyway). */
        if (strstr(value, "line-through")) el->css_text_decoration = 3;
        else if (strstr(value, "underline")) el->css_text_decoration = 2;
        else if (strstr(value, "none")) el->css_text_decoration = 1;
    } else if (strcmp(name, "z-index") == 0) {
        char *end;
        long z = strtol(value, &end, 10);
        if (end != value) { el->css_has_z_index = 1; el->css_z_index = (int)z; }
    } else if (strcmp(name, "overflow") == 0 || strcmp(name, "overflow-x") == 0 || strcmp(name, "overflow-y") == 0) {
        if (strstr(value, "hidden")) el->css_overflow_hidden = 1;
    } else if (strcmp(name, "border-radius") == 0) {
        /* Real CSS allows 1-4 values (all corners / TL-BR + TR-BL / all
         * four independently) and per-corner longhands -- this engine
         * only models one uniform radius (see dom.h's own comment on this
         * whole feature's scope), so only the FIRST value is read; a
         * multi-value "border-radius: 4px 8px" still parses (no desync)
         * but only the 4px applies to every corner alike. */
        char *end;
        double d = strtod(value, &end);
        if (end != value) el->css_border_radius = (float)d;
    } else if (strcmp(name, "box-shadow") == 0) {
        /* "box-shadow: [inset] <dx> <dy> [<blur>] [<spread>] <color>" --
         * real CSS allows a comma-separated LIST of shadows; only the
         * FIRST is modeled (dom.h's own comment). "inset" is recognized
         * and skipped (inset shadows aren't modeled -- drawn as a normal
         * outer shadow instead of not at all, matching this engine's own
         * "recover more than we lose" convention). Numbers are read in
         * strict dx/dy/blur/spread order, stopping at the first token
         * that ISN'T a valid leading number (assumed to be where the
         * color starts) -- correctly handles the two real common forms
         * ("dx dy color" and "dx dy blur spread color") without needing
         * to look ahead for how many numeric tokens exist. */
        if (strstr(value, "none") == value) {
            el->css_has_box_shadow = 0;
        } else {
            const char *p = value;
            while (*p == ' ') p++;
            if (!strncmp(p, "inset", 5)) p += 5;
            float nums[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int nn = 0;
            for (;;) {
                while (*p == ' ' || *p == '\t') p++;
                char *end;
                double d = strtod(p, &end);
                if (end == p || nn >= 4) break;
                nums[nn++] = (float)d;
                p = end;
                /* Skip a unit suffix ("px", or bare/unitless) up to the
                 * next space -- without this, strtod's own stop-at-"p"
                 * behavior on "10px 10px ..." leaves `p` sitting mid-
                 * token ("px 10px...") for the NEXT iteration, which then
                 * fails to parse as a number at all and silently ends the
                 * whole loop after just ONE value (confirmed as a real
                 * bug this way: every box-shadow with px units failed to
                 * parse past dx, so the color parse below ran on leftover
                 * unit-suffix text instead of the real color and always
                 * failed, meaning css_has_box_shadow was NEVER set for
                 * any real "Npx Npx ..." box-shadow value). */
                while (*p && *p != ' ' && *p != '\t') p++;
            }
            while (*p == ' ') p++;
            if (css_parse_color(p, rgb)) {
                el->css_has_box_shadow = 1;
                el->css_shadow_dx = nums[0];
                el->css_shadow_dy = nums[1];
                el->css_shadow_blur = nn >= 3 ? nums[2] : 0.0f;
                el->css_shadow_spread = nn >= 4 ? nums[3] : 0.0f;
                el->css_shadow_r = rgb[0]; el->css_shadow_g = rgb[1]; el->css_shadow_b = rgb[2];
                /* css_parse_color() itself only ever extracts rgb (every
                 * one of its own callers historically only needed a solid
                 * color) -- rgba()'s own alpha component matters a lot
                 * more here (a box-shadow with no transparency looks like
                 * a hard drop-shadow silhouette, not the soft translucent
                 * shadow real CSS almost always uses it for), so it's
                 * re-extracted here directly rather than widening
                 * css_parse_color()'s own signature for every caller. */
                el->css_shadow_a = 1.0f;
                if (!strncmp(p, "rgba", 4)) {
                    const char *paren = strchr(p, '(');
                    if (paren) {
                        int rr, gg, bb; double aa;
                        if (sscanf(paren + 1, "%d , %d , %d , %lf", &rr, &gg, &bb, &aa) == 4 ||
                            sscanf(paren + 1, "%d,%d,%d,%lf", &rr, &gg, &bb, &aa) == 4) {
                            el->css_shadow_a = (float)aa;
                        }
                    }
                }
            }
        }
    } else if (strcmp(name, "transform") == 0) {
        /* Same function-call grammar as SVG's own "transform" attribute
         * (translate/scale/rotate/skewX/skewY/matrix, space-separated
         * chain, each composed left-to-right) -- see svg_render.c's
         * svg_parse_transform() for the identical algorithm; not shared
         * code (different file, different value units: CSS angles carry
         * an explicit "deg" suffix stripped here via atof's own "stop at
         * first non-numeric character" behavior, whereas SVG angles are
         * bare numbers already in degrees). "none" clears any transform
         * (real CSS meaning). */
        if (strstr(value, "none") == value) {
            el->css_has_transform = 0;
        } else {
            float m[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}; /* a,b,c,d,e,f identity */
            const char *p = value;
            int any = 0;
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == ',') p++;
                if (!*p) break;
                char fname[16]; int fi = 0;
                while (*p && isalpha((unsigned char)*p) && fi < 15) fname[fi++] = *p++;
                fname[fi] = 0;
                while (*p == ' ') p++;
                if (*p != '(') break;
                p++;
                float args[6] = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
                int na = 0;
                while (*p && *p != ')' && na < 6) {
                    while (*p == ' ' || *p == ',') p++;
                    char *end;
                    double d = strtod(p, &end);
                    if (end == p) { p++; continue; }
                    args[na++] = (float)d;
                    p = end;
                    while (*p == ' ') p++;
                    /* skip a unit suffix ("deg"/"px"/"%") up to the next
                     * comma/paren/space -- strtod already stopped at it. */
                    while (*p && *p != ',' && *p != ')' && *p != ' ') p++;
                }
                if (*p == ')') p++;
                float la = 1.0f, lb = 0.0f, lc = 0.0f, ld = 1.0f, le = 0.0f, lf = 0.0f;
                if (!strcmp(fname, "translate") || !strcmp(fname, "translateX")) {
                    le = args[0]; lf = (na > 1) ? args[1] : 0.0f;
                } else if (!strcmp(fname, "translateY")) {
                    lf = args[0];
                } else if (!strcmp(fname, "scale")) {
                    la = args[0]; ld = (na > 1) ? args[1] : args[0];
                } else if (!strcmp(fname, "scaleX")) {
                    la = args[0];
                } else if (!strcmp(fname, "scaleY")) {
                    ld = args[0];
                } else if (!strcmp(fname, "rotate")) {
                    double ang = (double)args[0] * 3.14159265358979 / 180.0;
                    la = (float)cos(ang); lb = (float)sin(ang); lc = -(float)sin(ang); ld = (float)cos(ang);
                } else if (!strcmp(fname, "skewX")) {
                    lc = (float)tan((double)args[0] * 3.14159265358979 / 180.0);
                } else if (!strcmp(fname, "skewY")) {
                    lb = (float)tan((double)args[0] * 3.14159265358979 / 180.0);
                } else if (!strcmp(fname, "matrix") && na >= 6) {
                    la = args[0]; lb = args[1]; lc = args[2]; ld = args[3]; le = args[4]; lf = args[5];
                } else {
                    continue; /* unrecognized function -- skip, keep composing the rest */
                }
                /* Compose: m = m * local (apply local AFTER everything
                 * already composed, same left-to-right chain order as
                 * svg_parse_transform()). */
                float ra = m[0]*la + m[2]*lb;
                float rb = m[1]*la + m[3]*lb;
                float rc = m[0]*lc + m[2]*ld;
                float rd = m[1]*lc + m[3]*ld;
                float re = m[0]*le + m[2]*lf + m[4];
                float rf = m[1]*le + m[3]*lf + m[5];
                m[0]=ra; m[1]=rb; m[2]=rc; m[3]=rd; m[4]=re; m[5]=rf;
                any = 1;
            }
            if (any) {
                el->css_has_transform = 1;
                el->css_transform[0]=m[0]; el->css_transform[1]=m[1]; el->css_transform[2]=m[2];
                el->css_transform[3]=m[3]; el->css_transform[4]=m[4]; el->css_transform[5]=m[5];
            }
        }
    } else if (strcmp(name, "flex-direction") == 0) {
        el->css_flex_direction = strstr(value, "column") ? CSS_FLEX_COLUMN : CSS_FLEX_ROW;
    } else if (strcmp(name, "flex-wrap") == 0) {
        el->css_flex_wrap = (strstr(value, "nowrap") == 0 && strstr(value, "wrap") != 0) ? 1 : 0;
    } else if (strcmp(name, "flex-flow") == 0) {
        /* Shorthand for flex-direction + flex-wrap in one value, e.g.
         * "row wrap" -- real CSS lets either order/either-alone too;
         * this covers the common "<direction> <wrap>" and
         * "<wrap> <direction>" forms via independent substring checks
         * rather than requiring a specific order. */
        el->css_flex_direction = strstr(value, "column") ? CSS_FLEX_COLUMN : CSS_FLEX_ROW;
        el->css_flex_wrap = (strstr(value, "nowrap") == 0 && strstr(value, "wrap") != 0) ? 1 : 0;
    } else if (strcmp(name, "flex-grow") == 0) {
        el->css_flex_grow = (float)atof(value);
    } else if (strcmp(name, "flex") == 0) {
        /* "flex: <grow> [<shrink>] [<basis>];" shorthand, or one of the
         * real CSS keyword forms ("flex: none" == "0 0 auto", "flex:
         * auto" == "1 1 auto", "flex: 1" == "1 1 0%" -- real CSS's own
         * single-number-means-grow-only special case). Only the GROW
         * component is modeled (see css_flex_grow's own comment on why
         * shrink/basis aren't) -- extracted as simply "the first token
         * that parses as a plain number", which correctly handles the
         * extremely common real-world "flex: 1;" / "flex: 1 1 auto;" /
         * "flex: 1 0 0%;" shapes without needing to fully parse the
         * shorthand's real 1-to-3-value grammar. */
        if (strstr(value, "none")) el->css_flex_grow = 0.0f;
        else if (strstr(value, "auto") && !isdigit((unsigned char)value[0])) el->css_flex_grow = 1.0f;
        else {
            char buf[192];
            strncpy(buf, value, sizeof buf - 1); buf[sizeof buf - 1] = 0;
            char *tok = strtok(buf, " \t");
            if (tok) el->css_flex_grow = (float)atof(tok);
        }
    } else if (strcmp(name, "position") == 0) {
        el->css_position_absolute = (strstr(value, "absolute") || strstr(value, "fixed")) ? 1 : 0;
    } else if (strcmp(name, "clip") == 0) {
        el->css_has_clip = (strstr(value, "auto") == 0) ? 1 : 0;
    } else if (strcmp(name, "justify-content") == 0) {
        if (strstr(value, "center")) el->css_justify = CSS_JUSTIFY_CENTER;
        else if (strstr(value, "space-between")) el->css_justify = CSS_JUSTIFY_BETWEEN;
        else if (strstr(value, "space-around")) el->css_justify = CSS_JUSTIFY_AROUND;
        else if (strstr(value, "end")) el->css_justify = CSS_JUSTIFY_END;
        else el->css_justify = CSS_JUSTIFY_START;
    } else if (strcmp(name, "align-items") == 0) {
        if (strstr(value, "center")) el->css_align = CSS_ALIGN_CENTER;
        else if (strstr(value, "end")) el->css_align = CSS_ALIGN_END;
        else if (strstr(value, "stretch")) el->css_align = CSS_ALIGN_STRETCH;
        else el->css_align = CSS_ALIGN_START;
    } else if (strcmp(name, "gap") == 0 || strcmp(name, "column-gap") == 0 || strcmp(name, "row-gap") == 0) {
        if (css_parse_len(value, &f)) el->css_gap = f;
    } else if (strcmp(name, "grid-template-columns") == 0) {
        strncpy(el->css_grid_template_columns, value, sizeof el->css_grid_template_columns - 1);
        el->css_grid_template_columns[sizeof el->css_grid_template_columns - 1] = 0;
    } else if (strcmp(name, "grid-template-areas") == 0) {
        strncpy(el->css_grid_template_areas, value, sizeof el->css_grid_template_areas - 1);
        el->css_grid_template_areas[sizeof el->css_grid_template_areas - 1] = 0;
    } else if (strcmp(name, "grid-area") == 0) {
        /* Only the plain "grid-area: <name>" form (an area name referring
         * to grid-template-areas) is modeled -- real CSS also allows the
         * numeric "row-start / col-start / row-end / col-end" shorthand
         * there, not supported here (a rule using it still parses, this
         * value just won't match any named area at layout time, the same
         * safe-degradation convention as every other unsupported CSS form
         * in this engine). */
        strncpy(el->css_grid_area, value, sizeof el->css_grid_area - 1);
        el->css_grid_area[sizeof el->css_grid_area - 1] = 0;
    } else if (strcmp(name, "grid-template") == 0) {
        /* "grid-template: <row-tracks> / <col-tracks>" shorthand -- real
         * Wikipedia CSS uses exactly this form for its own sidebar grid
         * ("min-content 1fr min-content / 12.25rem minmax(0,1fr)"). Only
         * the COLUMN half (after "/") is kept, feeding the same
         * css_grid_template_columns field/parser "grid-template-columns"
         * itself uses -- row track sizes aren't modeled at all (this
         * engine's grid rows are always content-driven/auto-height, see
         * layout.c's own row-height comment), so the row half is simply
         * discarded rather than parsed for no effect. A value containing
         * a quoted-string area-template form of this shorthand (real CSS
         * also allows embedding grid-template-areas rows directly inside
         * "grid-template") is NOT handled -- falls through to the
         * else-discard below, same as any other unrecognized shape. */
        const char *slash = strchr(value, '/');
        if (slash && !strchr(value, '"') && !strchr(value, '\'')) {
            const char *cols = slash + 1;
            while (*cols == ' ') cols++;
            strncpy(el->css_grid_template_columns, cols, sizeof el->css_grid_template_columns - 1);
            el->css_grid_template_columns[sizeof el->css_grid_template_columns - 1] = 0;
        }
    } else if (strcmp(name, "font-size") == 0) {
        if (css_parse_len(value, &f) && f > 0.0f) el->css_font_size = f;
    } else if (strcmp(name, "font-weight") == 0) {
        /* Real CSS: "bold"/"bolder", or a numeric weight >= 600 (700 is
         * the real "bold" keyword's own numeric equivalent; 600/"semibold"
         * is treated as bold too here since this project's faux-bold
         * double-draw -- see sqw_main.c's draw_layout_text() -- is a
         * binary effect with no room for a real weight gradient anyway).
         * "normal"/400 and anything else/unrecognized leaves it unbold. */
        if (strstr(value, "bold")) el->css_font_weight_bold = 1;
        else { char *end; long w = strtol(value, &end, 10); if (end != value && w >= 600) el->css_font_weight_bold = 1; else if (end != value) el->css_font_weight_bold = 0; }
    } else if (strcmp(name, "text-align") == 0) {
        if (strstr(value, "center")) el->css_text_align = 1;
        else if (strstr(value, "right")) el->css_text_align = 2;
        else el->css_text_align = 0; /* left, start, justify (no real justify support) */
    } else if (strcmp(name, "line-height") == 0) {
        /* A bare number (no unit) is a real-CSS MULTIPLIER of the
         * element's own font-size, not a px length -- e.g. "line-height:
         * 1.5;" on 16px text means 24px, not 1.5px. css_parse_len() only
         * ever returns a px value, so a unitless line-height is resolved
         * against el->css_font_size right here instead (which is already
         * correctly inherited/set by the time this declaration runs, same
         * as every other property in this same cascade pass). A value
         * WITH a unit (line-height: 24px;) is a real px length as normal. */
        char *end; double d = strtod(value, &end);
        while (*end && isspace((unsigned char)*end)) end++;
        if (end != value && *end == 0) el->css_line_height = (float)(d * el->css_font_size);
        else if (css_parse_len(value, &f)) el->css_line_height = f;
    } else if (strcmp(name, "opacity") == 0) {
        char *end; double d = strtod(value, &end);
        if (end != value) { if (d < 0.0) d = 0.0; if (d > 1.0) d = 1.0; el->css_opacity = (float)d; }
    }
    /* Any other property: parsed into a CssDecl by css_parse_decls()
     * already (so the declaration-list parse stays in sync), just not
     * acted on here -- see this file's top comment. */
}

typedef struct {
    CssRule *rule;
    int selector_specificity;
} CssMatchedRule;

static int css_match_cmp(const void *a, const void *b) {
    const CssMatchedRule *ma = (const CssMatchedRule *)a, *mb = (const CssMatchedRule *)b;
    if (ma->selector_specificity != mb->selector_specificity) return ma->selector_specificity - mb->selector_specificity;
    return ma->rule->source_order - mb->rule->source_order;
}

/* Was 128 -- confirmed, via direct testing against a real fetched
 * Wikipedia stylesheet (~970 parsed rules from its own ~200KB external
 * CSS bundle, see sqw_apply_css()'s own comment on why external <link
 * rel="stylesheet"> is fetched at all now), to be far too small: a
 * generic element matched by enough broad, common rules (any selector
 * touching a widely-used class/tag) hit this cap well before the parser
 * even reached specific, LATE-in-source-order rules that mattered a
 * great deal -- confirmed as the exact reason ".mw-page-container-inner"'s
 * own real "display:grid; grid-template-areas:...' rule (what actually
 * places Wikipedia's sidebar beside its article content) never took
 * effect at all: not a selector-matching or parsing bug (an isolated
 * standalone parse of that exact rule text worked perfectly), just this
 * cap silently dropping it before its turn. 2048 costs a few KB more
 * stack per css_apply_element() call (CssMatchedRule is small: a pointer
 * + an int) and comfortably covers even a large real-world stylesheet. */
#define CSS_MAX_MATCHES 2048

static void css_apply_element(DomNode *el, CssStylesheet *sheet, float viewport_w) {
    css_set_default_style(el);

    CssMatchedRule matches[CSS_MAX_MATCHES];
    int nmatch = 0;
    int ri;
    for (ri = 0; ri < sheet->count && nmatch < CSS_MAX_MATCHES; ri++) {
        CssRule *rule = &sheet->rules[ri];
        /* @media (min-width/max-width) gate -- see CssRule.media_min_width's
         * own comment. A rule whose enclosing @media doesn't match the
         * current viewport width is skipped entirely here, same as if it
         * had no matching selector -- so a mobile-only rule stops
         * overriding a desktop rule the instant the viewport widens past
         * its breakpoint (as long as css_apply()/css_apply_one() is
         * re-run with the new width -- see css_apply()'s own comment). */
        if (rule->media_min_width >= 0 && viewport_w < (float)rule->media_min_width) continue;
        if (rule->media_max_width >= 0 && viewport_w > (float)rule->media_max_width) continue;
        int si, best_spec = -1, any = 0;
        for (si = 0; si < rule->selector_count; si++) {
            if (css_selector_matches(&rule->selectors[si], el)) {
                any = 1;
                if (rule->selectors[si].specificity > best_spec) best_spec = rule->selectors[si].specificity;
            }
        }
        if (any) { matches[nmatch].rule = rule; matches[nmatch].selector_specificity = best_spec; nmatch++; }
    }
    qsort(matches, (size_t)nmatch, sizeof(CssMatchedRule), css_match_cmp);

    int mi;
    for (mi = 0; mi < nmatch; mi++) {
        CssRule *rule = matches[mi].rule;
        int di;
        for (di = 0; di < rule->decl_count; di++) {
            css_apply_decl(el, rule->decls[di].name, rule->decls[di].value);
        }
    }

    const char *inline_style = dom_get_attr(el, "style");
    if (inline_style) {
        CssRule tmp;
        memset(&tmp, 0, sizeof tmp);
        const char *p = inline_style;
        css_parse_decls(&p, &tmp);
        int di;
        for (di = 0; di < tmp.decl_count; di++) {
            css_apply_decl(el, tmp.decls[di].name, tmp.decls[di].value);
        }
    }

    /* Heuristic: the extremely common real-world "visually hidden,
     * screen-reader-only" CSS pattern -- position:absolute combined with
     * either a real clip rect or a ~1x1px box (the two most common ways
     * real sites accessibly hide duplicate/decorative text without
     * display:none, which screen readers themselves treat as "not
     * present" and therefore don't use). This project doesn't implement
     * real position:absolute (out-of-flow placement via top/left/right/
     * bottom) at all, so left alone such an element would render, wrong,
     * as normal in-flow content, typically overlapping whatever it was
     * meant to sit invisibly on top of -- confirmed as the real cause of
     * a genuine visual bug (a page's own logo heading text overlapping
     * its own tagline) hit while testing this engine against real
     * Wikipedia markup. Checked once, after every declaration (matched
     * rules + inline style) has been applied, so it sees the final
     * computed position/clip/width/height regardless of which
     * declaration set which property.
     *
     * Also covers a second, equally common real pattern found the same
     * way (a real Wikipedia article page's language-picker dropdown
     * rendering as a giant, full-height block of text instead of a
     * small collapsed control): position:absolute + explicit height:0 --
     * the standard "collapsed dropdown/menu panel, expanded only via a
     * ':checked ~ sibling' rule this engine correctly never matches (no
     * :checked/general-sibling-combinator support), so the base,
     * unconditional height:0 rule is the one left in effect" idiom.
     * Without out-of-flow placement, an in-flow position:absolute
     * element with height:0 would otherwise still lay out its full
     * (often huge -- here, a 119-entry language list) content height,
     * since only ITS OWN box would be zero-height while its children
     * still take up real space beneath it. */
    /* The "el->css_height <= 0.0f" checks below deliberately exclude a
     * PERCENTAGE height (css_height_is_percent) -- a real, confirmed
     * regression found while adding real percentage width/height
     * support (see DomNode's own css_width_is_percent comment, dom.h):
     * a percentage height is stored raw and only resolved into a real
     * px value later, at LAYOUT time, once the real containing-block
     * height is known (this CSS-cascade pass runs earlier and has no
     * such reference) -- so el->css_height is always still its
     * placeholder 0 here, even for a real, honest "height:100%"
     * (confirmed on Wikipedia's own dropdown-toggle checkboxes, which
     * declare exactly that so their real clickable area covers their
     * whole visible button). Without this exclusion, EVERY percentage-
     * height, position:absolute element -- not just the genuinely
     * collapsed "height:0" dropdown-content case this heuristic was
     * built for -- got incorrectly treated as collapsed and removed
     * from layout entirely, checkbox included. */
    if (el->css_position_absolute &&
        (el->css_has_clip ||
         (el->css_has_width && el->css_width <= 2.0f && el->css_has_height && el->css_height <= 2.0f && !el->css_width_is_percent && !el->css_height_is_percent) ||
         (el->css_has_height && el->css_height <= 0.0f && !el->css_height_is_percent))) {
        el->css_display = CSS_DISPLAY_NONE;
    }
}

void css_apply_one(DomNode *el, CssStylesheet *sheet, float viewport_w) {
    if (!el || dom_is_text(el)) return;
    css_apply_element(el, sheet, viewport_w);
}

void css_apply(DomNode *root, CssStylesheet *sheet, float viewport_w) {
    /* The synthetic "#document" root itself never goes through
     * css_apply_element() below (the walk only calls it on root's
     * CHILDREN onward), so its own inheritable fields would otherwise sit
     * at whatever dom_node_new()'s calloc left them (all-zero) --
     * css_set_default_style()'s own "if (el->parent) inherit else use the
     * real root defaults" branch relies on THIS being those real root
     * defaults, since root's children see root as their parent and
     * "el->parent" is non-NULL for them (it's the #document node, not
     * NULL) even though root has no CSS parent of its own. Seeded here,
     * once, rather than special-casing "tag == #document" inside
     * css_set_default_style() itself. */
    root->css_font_size = 16.0f;
    root->css_text_align = 0;
    root->css_line_height = 0.0f;
    root->css_font_weight_bold = 0;

    /* Iterative pre-order walk (see dom_walk.c's own identical pattern --
     * not reused directly so this stays a single self-contained pass
     * with no extra callback-context allocation). */
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;

    while (top > 0) {
        DomNode *n = stack[top - 1];
        if (next_child[top - 1] >= n->child_count) { top--; continue; }
        DomNode *child = n->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            css_apply_element(child, sheet, viewport_w);
            if (top >= cap) {
                cap *= 2;
                stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *));
                next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int));
            }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack);
    free(next_child);
}
