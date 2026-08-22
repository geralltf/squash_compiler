/* Implementation of css.h -- see that header's own top comment. #include-d
 * directly into sqw_main.c, same single-TU convention as every other
 * SQW/*.c file.
 *
 * Documented scope (a REAL CSS engine, not a fake one, but a genuinely
 * bounded subset -- see the project's own multi-session-scope discussion
 * this was built under):
 *   Selectors : tag, .class, #id, "*", and any AND-combination of those
 *               in one compound (div.foo#bar), chained with the
 *               DESCENDANT combinator (space) only. NOT supported:
 *               child ">", sibling "+"/"~", attribute "[x=y]", and
 *               pseudo-class/-element ":hover"/"::before" selectors --
 *               a rule using one parses without error but its selector
 *               simply never matches anything (same safe-degradation
 *               convention as the rest of this project's parsers).
 *   At-rules  : "@media { ... }" has its condition ignored and its BODY
 *               parsed as ordinary rules (ignoring the query means this
 *               is not actually responsive, but nearly all of a real
 *               site's real layout-affecting CSS lives inside @media
 *               blocks, so unwrapping unconditionally recovers far more
 *               real style than skipping them entirely would). Every
 *               other @-rule (@import/@font-face/@keyframes/@supports/
 *               @charset/@page/...) is properly skipped as a whole
 *               (balanced-brace block, or up to the next top-level ";"
 *               for a statement-only at-rule like @import) -- never
 *               misparsed as a normal rule.
 *   Properties: display, width, height, margin(-top/right/bottom/left),
 *               padding(-top/right/bottom/left), color, background /
 *               background-color, flex-direction, justify-content,
 *               align-items, gap / column-gap / row-gap,
 *               grid-template-columns (stored raw, parsed by layout.c at
 *               layout time). Any other property is parsed (so it
 *               doesn't desync the declaration-list parse) and then
 *               silently ignored.
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
 * at *p, stopping at whitespace/','/'{'/'>'/'+'/'~'/end. Unsupported
 * combinators (>,+,~) and attribute/pseudo forms ([...], :...) are
 * consumed (so the overall parse stays in sync) but contribute no simple
 * selector, so a compound built from one is empty and never matches --
 * see this file's own top comment. */
static void css_parse_compound(const char **p, CssCompound *out) {
    out->part_count = 0;
    const char *q = *p;
    for (;;) {
        char c = *q;
        if (c == 0 || isspace((unsigned char)c) || c == ',' || c == '{') break;
        if (c == '>' || c == '+' || c == '~') { q++; continue; }
        if (c == '[') { while (*q && *q != ']') q++; if (*q) q++; continue; }
        if (c == ':') {
            q++;
            if (*q == ':') q++;
            while (isalnum((unsigned char)*q) || *q == '-') q++;
            if (*q == '(') { int depth = 1; q++; while (*q && depth > 0) { if (*q == '(') depth++; else if (*q == ')') depth--; q++; } }
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
}

static int css_compound_specificity(const CssCompound *c) {
    int i, sp = 0;
    for (i = 0; i < c->part_count; i++) {
        if (c->parts[i].kind == CSS_SEL_ID) sp += 100;
        else if (c->parts[i].kind == CSS_SEL_CLASS) sp += 10;
        else if (c->parts[i].kind == CSS_SEL_TAG) sp += 1;
    }
    return sp;
}

/* Parses one full selector (comma-separated list item) into `out`,
 * stopping at ',' or '{'. */
static void css_parse_selector(const char **p, CssSelector *out) {
    out->chain_len = 0;
    out->specificity = 0;
    for (;;) {
        css_skip_ws(p);
        const char *q = *p;
        if (*q == 0 || *q == ',' || *q == '{') break;
        if (out->chain_len < CSS_MAX_SELECTOR_CHAIN) {
            CssCompound *comp = &out->chain[out->chain_len++];
            css_parse_compound(p, comp);
            out->specificity += css_compound_specificity(comp);
        } else {
            CssCompound dummy;
            css_parse_compound(p, &dummy);
        }
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

static void css_parse_rule_body(CssStylesheet *sheet, const char **p, int *source_order);

/* Handles one "@...". @media's body is unwrapped and parsed as ordinary
 * rules (see this file's top comment on why); every other @-rule is
 * properly skipped, whole. */
static void css_parse_at_rule(CssStylesheet *sheet, const char **p, int *source_order) {
    const char *kw_start = *p + 1;
    const char *q = kw_start;
    while (isalpha((unsigned char)*q) || *q == '-') q++;
    int kw_len = (int)(q - kw_start);
    int is_media = (kw_len == 5 && strncmp(kw_start, "media", 5) == 0);
    /* Skip the prelude (condition/selector-like text before '{' or ';'). */
    while (*q && *q != '{' && *q != ';') q++;
    if (*q == ';') { q++; *p = q; return; }
    if (*q != '{') { *p = q; return; }
    if (is_media) {
        q++; /* enter the block */
        *p = q;
        css_parse_rule_body(sheet, p, source_order);
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
 * and for an unwrapped @media body. */
static void css_parse_rule_body(CssStylesheet *sheet, const char **p, int *source_order) {
    const char *q = *p;
    for (;;) {
        *p = q;
        css_skip_ws(p);
        q = *p;
        if (*q == 0 || *q == '}') { *p = q; return; }
        if (*q == '@') { *p = q; css_parse_at_rule(sheet, p, source_order); q = *p; continue; }

        CssRule tmp;
        memset(&tmp, 0, sizeof tmp);
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
        css_parse_decls(p, &tmp);
        q = *p;
        if (*q == '}') q++;
        if (tmp.selector_count > 0 && tmp.decl_count > 0) {
            tmp.source_order = (*source_order)++;
            CssRule *r = css_push_rule(sheet);
            *r = tmp;
        }
    }
}

void css_parse_into(CssStylesheet *sheet, const char *text) {
    const char *p = text;
    int source_order = sheet->count; /* continue numbering across multiple <style> tags */
    css_parse_rule_body(sheet, &p, &source_order);
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
        }
    }
    return c->part_count > 0 || 1; /* an empty (unsupported-combinator) compound never matches -- see below */
}

/* Descendant-combinator chain match: chain[last] must match `el` itself;
 * each earlier chain[i] must match SOME ancestor, in order (walking up
 * from `el`, each successive required compound found further up than
 * the last one matched) -- the real definition of the CSS descendant
 * combinator. */
static int css_selector_matches(const CssSelector *sel, const DomNode *el) {
    if (sel->chain_len == 0) return 0;
    const CssCompound *last = &sel->chain[sel->chain_len - 1];
    if (last->part_count == 0) return 0; /* unsupported-combinator compound: never matches */
    if (!css_compound_matches(last, el)) return 0;
    if (sel->chain_len == 1) return 1;

    int ci = sel->chain_len - 2;
    const DomNode *anc = el->parent;
    while (ci >= 0 && anc) {
        const CssCompound *c = &sel->chain[ci];
        if (c->part_count > 0 && css_compound_matches(c, anc)) ci--;
        anc = anc->parent;
    }
    return ci < 0;
}

/* ---- value parsing ---- */

static int css_parse_len(const char *v, float *out) {
    while (*v && isspace((unsigned char)*v)) v++;
    if (strncmp(v, "auto", 4) == 0) return 0;
    char *end;
    double d = strtod(v, &end);
    if (end == v) return 0;
    /* px and unitless both taken as px; anything else (%, em, rem, vw,
     * vh, ...) isn't resolved against a real reference size here, so
     * it's treated as "not specified" -- see this file's top comment. */
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != 0 && strncmp(end, "px", 2) != 0) return 0;
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
static const CssNamedColor CSS_NAMED_COLORS[] = {
    {"black",0,0,0}, {"white",255,255,255}, {"red",255,0,0}, {"green",0,128,0},
    {"blue",0,0,255}, {"yellow",255,255,0}, {"gray",128,128,128}, {"grey",128,128,128},
    {"silver",192,192,192}, {"orange",255,165,0}, {"purple",128,0,128}, {"navy",0,0,128},
    {"teal",0,128,128}, {"maroon",128,0,0}, {"lime",0,255,0}, {"olive",128,128,0},
    {"transparent",255,255,255}, {"none",255,255,255},
    {0,0,0,0}
};

static int css_parse_color(const char *v, float *rgb) {
    while (*v && isspace((unsigned char)*v)) v++;
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
        strcmp(t,"figcaption")==0 || strcmp(t,"table")==0 || strcmp(t,"tr")==0 || strcmp(t,"form")==0;
    el->css_display = block ? CSS_DISPLAY_BLOCK : CSS_DISPLAY_INLINE;
    el->css_has_width = 0; el->css_has_height = 0;
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
    el->css_grid_template_columns[0] = 0;
    el->css_position_absolute = 0;
    el->css_has_clip = 0;
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
        if (css_parse_len(value, &f)) { el->css_width = f; el->css_has_width = 1; }
    } else if (strcmp(name, "height") == 0) {
        if (css_parse_len(value, &f)) { el->css_height = f; el->css_has_height = 1; }
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

#define CSS_MAX_MATCHES 128

static void css_apply_element(DomNode *el, CssStylesheet *sheet) {
    css_set_default_style(el);

    CssMatchedRule matches[CSS_MAX_MATCHES];
    int nmatch = 0;
    int ri;
    for (ri = 0; ri < sheet->count && nmatch < CSS_MAX_MATCHES; ri++) {
        CssRule *rule = &sheet->rules[ri];
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
     * declaration set which property. */
    if (el->css_position_absolute &&
        (el->css_has_clip || (el->css_has_width && el->css_width <= 2.0f && el->css_has_height && el->css_height <= 2.0f))) {
        el->css_display = CSS_DISPLAY_NONE;
    }
}

void css_apply(DomNode *root, CssStylesheet *sheet) {
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
            css_apply_element(child, sheet);
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
