/* Implementation of svg_render.h -- see that header's own top comment for
 * scope. #include-d directly from sqw_main.c after the other image
 * decoders, same single-TU convention as every other SQW file.
 *
 * NOTE on a real squash compiler quirk this file works around throughout:
 * squash's codegen can misclassify an argument passed to a float/double
 * parameter when the argument is a bare int literal or an int-typed
 * variable (confirmed via a direct repro this session) -- every call site
 * below that passes a value to a double parameter uses an explicit
 * (double) cast, and every double literal is written with a decimal point,
 * to stay on the known-good path. */
#include "svg_render.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define SVG_PI 3.14159265358979323846
/* Matches SQW_SVG_MAX_DIM referenced in svg_render.h's own top comment --
 * kept as a plain internal constant since nothing outside this file needs
 * to know the exact cap. */
#define SVG_MAX_DIM 2048
#define SVG_BEZIER_SEGS 12
#define SVG_ARC_SEGS 24
#define SVG_MAX_DEPTH 64
#define SVG_MAX_GRADIENTS 64
#define SVG_GRAD_ID_MAX 64

/* ---------------- tokenizer ---------------- */

typedef struct {
    const char *data;
    long len;
    long pos;
} SvgLexer;

typedef struct {
    char name[64];
    int self_closing;
    int is_close;
    char *attr_names[32];
    char *attr_values[32];
    int n_attrs;
} SvgTok;

static void svg_lexer_init(SvgLexer *lx, const char *data, long len) {
    lx->data = data; lx->len = len; lx->pos = 0;
}

static void svg_skip_ws(SvgLexer *lx) {
    while (lx->pos < lx->len && isspace((unsigned char)lx->data[lx->pos])) lx->pos++;
}

static void svg_tok_free(SvgTok *tok) {
    int i;
    for (i = 0; i < tok->n_attrs; i++) { free(tok->attr_names[i]); free(tok->attr_values[i]); }
    tok->n_attrs = 0;
}

static int svg_lexer_next(SvgLexer *lx, SvgTok *tok) {
    memset(tok, 0, sizeof *tok);
    for (;;) {
        while (lx->pos < lx->len && lx->data[lx->pos] != '<') lx->pos++;
        if (lx->pos >= lx->len) return 0;
        if (lx->pos + 3 < lx->len && lx->data[lx->pos+1]=='!' && lx->data[lx->pos+2]=='-' && lx->data[lx->pos+3]=='-') {
            lx->pos += 4;
            while (lx->pos + 2 < lx->len && !(lx->data[lx->pos]=='-' && lx->data[lx->pos+1]=='-' && lx->data[lx->pos+2]=='>')) lx->pos++;
            lx->pos += 3;
            if (lx->pos > lx->len) lx->pos = lx->len;
            continue;
        }
        if (lx->pos + 1 < lx->len && lx->data[lx->pos+1] == '?') {
            lx->pos += 2;
            while (lx->pos + 1 < lx->len && !(lx->data[lx->pos]=='?' && lx->data[lx->pos+1]=='>')) lx->pos++;
            lx->pos += 2;
            continue;
        }
        if (lx->pos + 1 < lx->len && lx->data[lx->pos+1] == '!') {
            lx->pos += 2;
            while (lx->pos < lx->len && lx->data[lx->pos] != '>') lx->pos++;
            lx->pos++;
            continue;
        }
        break;
    }
    lx->pos++;
    if (lx->pos < lx->len && lx->data[lx->pos] == '/') {
        tok->is_close = 1;
        lx->pos++;
        svg_skip_ws(lx);
        int ni = 0;
        while (lx->pos < lx->len && (isalnum((unsigned char)lx->data[lx->pos]) || lx->data[lx->pos]=='_' || lx->data[lx->pos]==':' || lx->data[lx->pos]=='-') && ni < 63) tok->name[ni++] = lx->data[lx->pos++];
        tok->name[ni] = 0;
        while (lx->pos < lx->len && lx->data[lx->pos] != '>') lx->pos++;
        if (lx->pos < lx->len) lx->pos++;
        return 1;
    }
    int ni = 0;
    while (lx->pos < lx->len && (isalnum((unsigned char)lx->data[lx->pos]) || lx->data[lx->pos]=='_' || lx->data[lx->pos]==':' || lx->data[lx->pos]=='-') && ni < 63) tok->name[ni++] = lx->data[lx->pos++];
    tok->name[ni] = 0;
    for (;;) {
        svg_skip_ws(lx);
        if (lx->pos >= lx->len) break;
        if (lx->data[lx->pos] == '/') {
            tok->self_closing = 1; lx->pos++;
            svg_skip_ws(lx);
            if (lx->pos < lx->len && lx->data[lx->pos] == '>') lx->pos++;
            break;
        }
        if (lx->data[lx->pos] == '>') { lx->pos++; break; }
        char namebuf[128];
        int an = 0;
        while (lx->pos < lx->len && (isalnum((unsigned char)lx->data[lx->pos]) || lx->data[lx->pos]=='_' || lx->data[lx->pos]==':' || lx->data[lx->pos]=='-') && an < 127) namebuf[an++] = lx->data[lx->pos++];
        namebuf[an] = 0;
        if (an == 0) { lx->pos++; continue; }
        svg_skip_ws(lx);
        char *val = NULL;
        if (lx->pos < lx->len && lx->data[lx->pos] == '=') {
            lx->pos++;
            svg_skip_ws(lx);
            if (lx->pos < lx->len && (lx->data[lx->pos]=='"' || lx->data[lx->pos]=='\'')) {
                char q = lx->data[lx->pos]; lx->pos++;
                long start = lx->pos;
                while (lx->pos < lx->len && lx->data[lx->pos] != q) lx->pos++;
                long vlen = lx->pos - start;
                val = (char *)malloc((size_t)vlen + 1);
                memcpy(val, lx->data + start, (size_t)vlen);
                val[vlen] = 0;
                if (lx->pos < lx->len) lx->pos++;
            }
        }
        if (tok->n_attrs < 32) {
            tok->attr_names[tok->n_attrs] = strdup(namebuf);
            tok->attr_values[tok->n_attrs] = val ? val : strdup("");
            tok->n_attrs++;
        } else if (val) {
            free(val);
        }
    }
    return 1;
}

static const char *svg_attr(SvgTok *tok, const char *name) {
    int i;
    for (i = 0; i < tok->n_attrs; i++) if (!strcmp(tok->attr_names[i], name)) return tok->attr_values[i];
    return NULL;
}

static double svg_read_num(const char **pp) {
    const char *p = *pp;
    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    char *end;
    double v = strtod(p, &end);
    if (end == p) { if (*p) p++; *pp = p; return 0.0; }
    *pp = end;
    return v;
}

static double svg_attr_num(SvgTok *tok, const char *name, double defval) {
    const char *v = svg_attr(tok, name);
    if (!v) return defval;
    const char *p = v;
    return svg_read_num(&p);
}

/* ---------------- matrix ---------------- */

typedef struct { double a, b, c, d, e, f; } SvgMat;

/* NOTE: these two return their SvgMat/SvgStyle result via an out-parameter
 * rather than a normal C return -- confirmed via a direct repro this
 * session that squash's codegen corrupts a struct RETURNED by value once
 * it exceeds 16 bytes (both SvgMat at 48 bytes and SvgStyle are well over
 * that), even though struct-by-value ARGUMENTS work correctly. Matches
 * this project's own established workaround for the identical bug
 * elsewhere (see cs_parser.c's ckpt_save, which takes an out-parameter for
 * the same reason). */
static void svg_mat_identity(SvgMat *out) {
    out->a = 1.0; out->b = 0.0; out->c = 0.0; out->d = 1.0; out->e = 0.0; out->f = 0.0;
}

static void svg_mat_mul(SvgMat *out, SvgMat p, SvgMat q) {
    SvgMat r;
    r.a = p.a*q.a + p.c*q.b;
    r.b = p.b*q.a + p.d*q.b;
    r.c = p.a*q.c + p.c*q.d;
    r.d = p.b*q.c + p.d*q.d;
    r.e = p.a*q.e + p.c*q.f + p.e;
    r.f = p.b*q.e + p.d*q.f + p.f;
    *out = r;
}

static void svg_mat_apply(SvgMat m, double x, double y, double *ox, double *oy) {
    *ox = m.a*x + m.c*y + m.e;
    *oy = m.b*x + m.d*y + m.f;
}

static double svg_mat_scale_factor(SvgMat m) {
    double sx = sqrt(m.a*m.a + m.b*m.b);
    double sy = sqrt(m.c*m.c + m.d*m.d);
    return (sx + sy) / 2.0;
}

static void svg_parse_transform(SvgMat *out, const char *s) {
    svg_mat_identity(out);
    if (!s) return;
    const char *p = s;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        char name[24]; int ni = 0;
        while (*p && isalpha((unsigned char)*p) && ni < 23) name[ni++] = *p++;
        name[ni] = 0;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '(') break;
        p++;
        double args[6] = {0.0,0.0,0.0,0.0,0.0,0.0};
        int na = 0;
        while (*p && *p != ')' && na < 6) {
            double v = svg_read_num(&p);
            args[na++] = v;
            while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        }
        if (*p == ')') p++;
        SvgMat local; svg_mat_identity(&local);
        if (!strcmp(name, "translate")) {
            local.e = args[0]; local.f = (na > 1) ? args[1] : 0.0;
        } else if (!strcmp(name, "scale")) {
            local.a = args[0]; local.d = (na > 1) ? args[1] : args[0];
        } else if (!strcmp(name, "rotate")) {
            double ang = args[0] * SVG_PI / 180.0;
            double ca = cos(ang), sa = sin(ang);
            if (na >= 3) {
                double cx = args[1], cy = args[2];
                SvgMat t1; svg_mat_identity(&t1); t1.e = cx; t1.f = cy;
                SvgMat r; svg_mat_identity(&r); r.a = ca; r.b = sa; r.c = -sa; r.d = ca;
                SvgMat t2; svg_mat_identity(&t2); t2.e = -cx; t2.f = -cy;
                SvgMat tmp;
                svg_mat_mul(&tmp, t1, r);
                svg_mat_mul(&local, tmp, t2);
            } else {
                local.a = ca; local.b = sa; local.c = -sa; local.d = ca;
            }
        } else if (!strcmp(name, "skewX")) {
            local.c = tan(args[0] * SVG_PI / 180.0);
        } else if (!strcmp(name, "skewY")) {
            local.b = tan(args[0] * SVG_PI / 180.0);
        } else if (!strcmp(name, "matrix") && na >= 6) {
            local.a = args[0]; local.b = args[1]; local.c = args[2];
            local.d = args[3]; local.e = args[4]; local.f = args[5];
        }
        SvgMat newresult;
        svg_mat_mul(&newresult, *out, local);
        *out = newresult;
    }
}

/* ---------------- color ---------------- */

typedef struct { const char *name; unsigned char r, g, b; } SvgNamedColor;

static const SvgNamedColor g_svg_named_colors[] = {
    {"black",0,0,0}, {"white",255,255,255}, {"red",255,0,0}, {"green",0,128,0},
    {"blue",0,0,255}, {"yellow",255,255,0}, {"cyan",0,255,255}, {"magenta",255,0,255},
    {"gray",128,128,128}, {"grey",128,128,128}, {"silver",192,192,192},
    {"maroon",128,0,0}, {"olive",128,128,0}, {"lime",0,255,0}, {"aqua",0,255,255},
    {"teal",0,128,128}, {"navy",0,0,128}, {"fuchsia",255,0,255}, {"purple",128,0,128},
    {"orange",255,165,0}, {"brown",165,42,42}, {"pink",255,192,203}, {"gold",255,215,0},
    {"indigo",75,0,130}, {"violet",238,130,238}, {"coral",255,127,80}, {"salmon",250,128,114},
    {"khaki",240,230,140}, {"plum",221,160,221}, {"orchid",218,112,214}, {"tan",210,180,140},
    {"beige",245,245,220}, {"ivory",255,255,240}, {"lavender",230,230,250}, {"crimson",220,20,60},
    {"chocolate",210,105,30}, {"darkgray",169,169,169}, {"darkgrey",169,169,169},
    {"lightgray",211,211,211}, {"lightgrey",211,211,211}, {"darkred",139,0,0},
    {"darkgreen",0,100,0}, {"darkblue",0,0,139}, {"lightblue",173,216,230},
    {"lightgreen",144,238,144}, {"steelblue",70,130,180}, {"skyblue",135,206,235},
    {"dimgray",105,105,105}, {"dimgrey",105,105,105}, {"slategray",112,128,144},
    {"whitesmoke",245,245,245}, {"gainsboro",220,220,220}, {"lightyellow",255,255,224},
    {"deepskyblue",0,191,255}, {"forestgreen",34,139,34}, {"firebrick",178,34,34},
    {"royalblue",65,105,225}, {"midnightblue",25,25,112}, {"turquoise",64,224,208},
    { NULL, 0, 0, 0 }
};

static int svg_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int svg_clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Returns 1 on a resolved color, 0 for "none"/unrecognized (caller decides
 * what "unrecognized" means for its own attribute -- e.g. fill="none"
 * clears the fill, but an unrecognized fill value is just left alone). */
static int svg_parse_color(const char *s, unsigned char *r, unsigned char *g, unsigned char *b) {
    if (!s) return 0;
    while (isspace((unsigned char)*s)) s++;
    if (!strncmp(s, "none", 4)) return 0;
    if (!strncmp(s, "transparent", 11)) { *r = 0; *g = 0; *b = 0; return 1; }
    if (!strncmp(s, "currentColor", 12)) { *r = 0; *g = 0; *b = 0; return 1; }
    if (s[0] == '#') {
        s++;
        int len = 0;
        while (s[len] && isxdigit((unsigned char)s[len])) len++;
        if (len == 3) {
            *r = (unsigned char)(svg_hexval(s[0]) * 17);
            *g = (unsigned char)(svg_hexval(s[1]) * 17);
            *b = (unsigned char)(svg_hexval(s[2]) * 17);
            return 1;
        } else if (len >= 6) {
            *r = (unsigned char)(svg_hexval(s[0]) * 16 + svg_hexval(s[1]));
            *g = (unsigned char)(svg_hexval(s[2]) * 16 + svg_hexval(s[3]));
            *b = (unsigned char)(svg_hexval(s[4]) * 16 + svg_hexval(s[5]));
            return 1;
        }
        return 0;
    }
    if (!strncmp(s, "rgb(", 4) || !strncmp(s, "rgba(", 5)) {
        const char *p = strchr(s, '(');
        if (!p) return 0;
        p++;
        double v[4] = {0.0,0.0,0.0,0.0};
        int n = 0;
        while (*p && *p != ')' && n < 4) {
            while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
            char *end;
            double val = strtod(p, &end);
            if (end == p) break;
            p = end;
            if (*p == '%') { val = val * 255.0 / 100.0; p++; }
            v[n++] = val;
        }
        if (n >= 3) {
            *r = (unsigned char)svg_clampi((int)(v[0] + 0.5), 0, 255);
            *g = (unsigned char)svg_clampi((int)(v[1] + 0.5), 0, 255);
            *b = (unsigned char)svg_clampi((int)(v[2] + 0.5), 0, 255);
            return 1;
        }
        return 0;
    }
    int i;
    for (i = 0; g_svg_named_colors[i].name; i++) {
        size_t nl = strlen(g_svg_named_colors[i].name);
        if (!strncmp(s, g_svg_named_colors[i].name, nl) && !isalnum((unsigned char)s[nl])) {
            *r = g_svg_named_colors[i].r; *g = g_svg_named_colors[i].g; *b = g_svg_named_colors[i].b;
            return 1;
        }
    }
    return 0;
}

static void svg_extract_style_prop(const char *style, const char *prop, char *out, int outsz) {
    out[0] = 0;
    const char *p = style;
    size_t plen = strlen(prop);
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ';')) p++;
        if (!*p) break;
        const char *key_start = p;
        while (*p && *p != ':' && *p != ';') p++;
        size_t klen = (size_t)(p - key_start);
        while (klen > 0 && isspace((unsigned char)key_start[klen-1])) klen--;
        const char *val_start = NULL; size_t vlen = 0;
        if (*p == ':') {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            val_start = p;
            while (*p && *p != ';') p++;
            vlen = (size_t)(p - val_start);
            while (vlen > 0 && isspace((unsigned char)val_start[vlen-1])) vlen--;
        }
        if (klen == plen && !strncmp(key_start, prop, plen) && val_start) {
            size_t cl = vlen < (size_t)(outsz - 1) ? vlen : (size_t)(outsz - 1);
            memcpy(out, val_start, cl);
            out[cl] = 0;
            return;
        }
    }
}

/* ---------------- gradients (flat averaged color) ---------------- */

typedef struct { char id[SVG_GRAD_ID_MAX]; double r, g, b; int has; } SvgGradEntry;
typedef struct { SvgGradEntry entries[SVG_MAX_GRADIENTS]; int count; } SvgGradientTable;

static void svg_collect_gradients(const unsigned char *data, long len, SvgGradientTable *table) {
    memset(table, 0, sizeof *table);
    SvgLexer lex; svg_lexer_init(&lex, (const char *)data, len);
    SvgTok tok;
    char cur_id[SVG_GRAD_ID_MAX]; cur_id[0] = 0;
    double sumr = 0.0, sumg = 0.0, sumb = 0.0;
    int nstops = 0;
    int active = 0;
    int enter_depth = 0;
    int depth = 0;
    for (;;) {
        if (!svg_lexer_next(&lex, &tok)) break;
        if (!tok.is_close) {
            if (!active && (!strcmp(tok.name, "linearGradient") || !strcmp(tok.name, "radialGradient"))) {
                const char *id = svg_attr(&tok, "id");
                if (id) {
                    strncpy(cur_id, id, sizeof cur_id - 1);
                    cur_id[sizeof cur_id - 1] = 0;
                    active = 1; sumr = 0.0; sumg = 0.0; sumb = 0.0; nstops = 0;
                    enter_depth = depth;
                }
            } else if (active && !strcmp(tok.name, "stop")) {
                const char *sc = svg_attr(&tok, "stop-color");
                char colorbuf[64];
                if (!sc) {
                    const char *style = svg_attr(&tok, "style");
                    if (style) {
                        svg_extract_style_prop(style, "stop-color", colorbuf, (int)sizeof colorbuf);
                        if (colorbuf[0]) sc = colorbuf;
                    }
                }
                if (sc) {
                    unsigned char r, g, b;
                    if (svg_parse_color(sc, &r, &g, &b)) {
                        sumr += (double)r; sumg += (double)g; sumb += (double)b;
                        nstops++;
                    }
                }
            }
            if (!tok.self_closing) depth++;
            svg_tok_free(&tok);
        } else {
            if (depth > 0) depth--;
            if (active && depth == enter_depth && (!strcmp(tok.name, "linearGradient") || !strcmp(tok.name, "radialGradient"))) {
                if (nstops > 0 && table->count < SVG_MAX_GRADIENTS) {
                    SvgGradEntry *e = &table->entries[table->count++];
                    strncpy(e->id, cur_id, sizeof e->id - 1);
                    e->id[sizeof e->id - 1] = 0;
                    e->r = sumr / (double)nstops;
                    e->g = sumg / (double)nstops;
                    e->b = sumb / (double)nstops;
                    e->has = 1;
                }
                active = 0;
            }
            svg_tok_free(&tok);
        }
    }
}

static int svg_lookup_gradient_color(SvgGradientTable *table, const char *fill_value, unsigned char *r, unsigned char *g, unsigned char *b) {
    const char *hash = strchr(fill_value, '#');
    if (!hash) return 0;
    hash++;
    char id[SVG_GRAD_ID_MAX]; int i = 0;
    while (hash[i] && hash[i] != ')' && i < SVG_GRAD_ID_MAX - 1) { id[i] = hash[i]; i++; }
    id[i] = 0;
    int k;
    for (k = 0; k < table->count; k++) {
        if (!strcmp(table->entries[k].id, id)) {
            *r = (unsigned char)svg_clampi((int)(table->entries[k].r + 0.5), 0, 255);
            *g = (unsigned char)svg_clampi((int)(table->entries[k].g + 0.5), 0, 255);
            *b = (unsigned char)svg_clampi((int)(table->entries[k].b + 0.5), 0, 255);
            return 1;
        }
    }
    return 0;
}

/* ---------------- path/point buffer ---------------- */

typedef struct {
    double *x, *y;
    int n, cap;
    int *subpath_start;
    int *subpath_closed;
    int subpath_count, subpath_cap;
} SvgPath;

static void svg_path_init(SvgPath *p) { memset(p, 0, sizeof *p); }

static void svg_path_free(SvgPath *p) {
    free(p->x); free(p->y); free(p->subpath_start); free(p->subpath_closed);
    memset(p, 0, sizeof *p);
}

static void svg_path_grow(SvgPath *p) {
    if (p->n >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->x = (double *)realloc(p->x, (size_t)p->cap * sizeof(double));
        p->y = (double *)realloc(p->y, (size_t)p->cap * sizeof(double));
    }
}

static void svg_path_lineto(SvgPath *p, double x, double y) {
    svg_path_grow(p);
    p->x[p->n] = x; p->y[p->n] = y; p->n++;
}

static void svg_path_moveto(SvgPath *p, double x, double y) {
    if (p->subpath_count >= p->subpath_cap) {
        p->subpath_cap = p->subpath_cap ? p->subpath_cap * 2 : 8;
        p->subpath_start = (int *)realloc(p->subpath_start, (size_t)p->subpath_cap * sizeof(int));
        p->subpath_closed = (int *)realloc(p->subpath_closed, (size_t)p->subpath_cap * sizeof(int));
    }
    p->subpath_start[p->subpath_count] = p->n;
    p->subpath_closed[p->subpath_count] = 0;
    p->subpath_count++;
    svg_path_lineto(p, x, y);
}

static void svg_path_close_subpath(SvgPath *p) {
    if (p->subpath_count > 0) p->subpath_closed[p->subpath_count - 1] = 1;
}

static void svg_path_transform(SvgPath *p, SvgMat m) {
    int i;
    for (i = 0; i < p->n; i++) {
        double ox, oy;
        svg_mat_apply(m, p->x[i], p->y[i], &ox, &oy);
        p->x[i] = ox; p->y[i] = oy;
    }
}

/* ---------------- bezier / arc flattening ---------------- */

static void svg_flatten_cubic(SvgPath *path, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3) {
    int i;
    for (i = 1; i <= SVG_BEZIER_SEGS; i++) {
        double t = (double)i / (double)SVG_BEZIER_SEGS;
        double mt = 1.0 - t;
        double x = mt*mt*mt*x0 + 3.0*mt*mt*t*x1 + 3.0*mt*t*t*x2 + t*t*t*x3;
        double y = mt*mt*mt*y0 + 3.0*mt*mt*t*y1 + 3.0*mt*t*t*y2 + t*t*t*y3;
        svg_path_lineto(path, x, y);
    }
}

static void svg_flatten_quad(SvgPath *path, double x0, double y0, double x1, double y1, double x2, double y2) {
    int i;
    for (i = 1; i <= SVG_BEZIER_SEGS; i++) {
        double t = (double)i / (double)SVG_BEZIER_SEGS;
        double mt = 1.0 - t;
        double x = mt*mt*x0 + 2.0*mt*t*x1 + t*t*x2;
        double y = mt*mt*y0 + 2.0*mt*t*y1 + t*t*y2;
        svg_path_lineto(path, x, y);
    }
}

static double svg_arc_angle(double ux, double uy, double vx, double vy) {
    double dot = ux*vx + uy*vy;
    double len = sqrt((ux*ux + uy*uy) * (vx*vx + vy*vy));
    double cosv = len > 0.0 ? dot / len : 1.0;
    if (cosv > 1.0) cosv = 1.0;
    if (cosv < -1.0) cosv = -1.0;
    double ang = acos(cosv);
    if (ux*vy - uy*vx < 0.0) ang = -ang;
    return ang;
}

static void svg_flatten_arc(SvgPath *path, double x0, double y0, double rx, double ry, double xrot_deg, int large_arc, int sweep, double x, double y) {
    if (rx == 0.0 || ry == 0.0 || (x0 == x && y0 == y)) { svg_path_lineto(path, x, y); return; }
    rx = fabs(rx); ry = fabs(ry);
    double phi = xrot_deg * SVG_PI / 180.0;
    double cphi = cos(phi), sphi = sin(phi);
    double dx2 = (x0 - x) / 2.0, dy2 = (y0 - y) / 2.0;
    double x1p = cphi*dx2 + sphi*dy2;
    double y1p = -sphi*dx2 + cphi*dy2;
    double lambda = (x1p*x1p) / (rx*rx) + (y1p*y1p) / (ry*ry);
    if (lambda > 1.0) { double s = sqrt(lambda); rx *= s; ry *= s; }
    double sign = (large_arc != sweep) ? 1.0 : -1.0;
    double num = rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p;
    double den = rx*rx*y1p*y1p + ry*ry*x1p*x1p;
    double co = (den > 0.0 && num > 0.0) ? sign * sqrt(num / den) : 0.0;
    double cxp = co * (rx*y1p/ry);
    double cyp = co * -(ry*x1p/rx);
    double ccx = cphi*cxp - sphi*cyp + (x0 + x) / 2.0;
    double ccy = sphi*cxp + cphi*cyp + (y0 + y) / 2.0;
    double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
    double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    double t1 = svg_arc_angle(1.0, 0.0, ux, uy);
    double dtheta = svg_arc_angle(ux, uy, vx, vy);
    if (!sweep && dtheta > 0.0) dtheta -= 2.0 * SVG_PI;
    if (sweep && dtheta < 0.0) dtheta += 2.0 * SVG_PI;
    int i;
    for (i = 1; i <= SVG_ARC_SEGS; i++) {
        double t = t1 + dtheta * ((double)i / (double)SVG_ARC_SEGS);
        double ex = ccx + rx*cos(t)*cphi - ry*sin(t)*sphi;
        double ey = ccy + rx*cos(t)*sphi + ry*sin(t)*cphi;
        svg_path_lineto(path, ex, ey);
    }
}

/* ---------------- path 'd' parser ---------------- */

static void svg_parse_path_d(const char *d, SvgPath *path) {
    double cx = 0.0, cy = 0.0;
    double start_x = 0.0, start_y = 0.0;
    double prev_cx = 0.0, prev_cy = 0.0;
    int prev_was_cubic = 0, prev_was_quad = 0;
    const char *p = d;
    char cmd = 0;
    int have_cmd = 0;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        if (isalpha((unsigned char)*p)) { cmd = *p; p++; have_cmd = 1; }
        if (!have_cmd) break;
        int relative = islower((unsigned char)cmd);
        char C = (char)toupper((unsigned char)cmd);
        if (C == 'M') {
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x += cx; y += cy; }
            cx = x; cy = y; start_x = x; start_y = y;
            svg_path_moveto(path, x, y);
            cmd = relative ? 'l' : 'L';
            prev_was_cubic = 0; prev_was_quad = 0;
        } else if (C == 'L') {
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x += cx; y += cy; }
            svg_path_lineto(path, x, y); cx = x; cy = y;
            prev_was_cubic = 0; prev_was_quad = 0;
        } else if (C == 'H') {
            double x = svg_read_num(&p);
            if (relative) x += cx;
            svg_path_lineto(path, x, cy); cx = x;
            prev_was_cubic = 0; prev_was_quad = 0;
        } else if (C == 'V') {
            double y = svg_read_num(&p);
            if (relative) y += cy;
            svg_path_lineto(path, cx, y); cy = y;
            prev_was_cubic = 0; prev_was_quad = 0;
        } else if (C == 'C') {
            double x1 = svg_read_num(&p), y1 = svg_read_num(&p);
            double x2 = svg_read_num(&p), y2 = svg_read_num(&p);
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
            svg_flatten_cubic(path, cx, cy, x1, y1, x2, y2, x, y);
            prev_cx = x2; prev_cy = y2; cx = x; cy = y;
            prev_was_cubic = 1; prev_was_quad = 0;
        } else if (C == 'S') {
            double x2 = svg_read_num(&p), y2 = svg_read_num(&p);
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x2 += cx; y2 += cy; x += cx; y += cy; }
            double x1, y1;
            if (prev_was_cubic) { x1 = 2.0*cx - prev_cx; y1 = 2.0*cy - prev_cy; } else { x1 = cx; y1 = cy; }
            svg_flatten_cubic(path, cx, cy, x1, y1, x2, y2, x, y);
            prev_cx = x2; prev_cy = y2; cx = x; cy = y;
            prev_was_cubic = 1; prev_was_quad = 0;
        } else if (C == 'Q') {
            double x1 = svg_read_num(&p), y1 = svg_read_num(&p);
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x1 += cx; y1 += cy; x += cx; y += cy; }
            svg_flatten_quad(path, cx, cy, x1, y1, x, y);
            prev_cx = x1; prev_cy = y1; cx = x; cy = y;
            prev_was_quad = 1; prev_was_cubic = 0;
        } else if (C == 'T') {
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x += cx; y += cy; }
            double x1, y1;
            if (prev_was_quad) { x1 = 2.0*cx - prev_cx; y1 = 2.0*cy - prev_cy; } else { x1 = cx; y1 = cy; }
            svg_flatten_quad(path, cx, cy, x1, y1, x, y);
            prev_cx = x1; prev_cy = y1; cx = x; cy = y;
            prev_was_quad = 1; prev_was_cubic = 0;
        } else if (C == 'A') {
            double rx = svg_read_num(&p), ry = svg_read_num(&p);
            double xrot = svg_read_num(&p);
            int large_arc = (int)svg_read_num(&p);
            int sweep = (int)svg_read_num(&p);
            double x = svg_read_num(&p), y = svg_read_num(&p);
            if (relative) { x += cx; y += cy; }
            svg_flatten_arc(path, cx, cy, rx, ry, xrot, large_arc, sweep, x, y);
            cx = x; cy = y;
            prev_was_cubic = 0; prev_was_quad = 0;
        } else if (C == 'Z') {
            svg_path_close_subpath(path);
            cx = start_x; cy = start_y;
            prev_was_cubic = 0; prev_was_quad = 0;
        } else {
            break;
        }
    }
}

/* ---------------- shape generators ---------------- */

static void svg_arc_corner(SvgPath *path, double ccx, double ccy, double rx, double ry, double a0, double a1, int n) {
    int i;
    for (i = 0; i <= n; i++) {
        double t = a0 + (a1 - a0) * ((double)i / (double)n);
        svg_path_lineto(path, ccx + rx*cos(t), ccy + ry*sin(t));
    }
}

static void svg_shape_rect(SvgPath *path, double x, double y, double w, double h, double rx, double ry) {
    if (rx < 0.0) rx = 0.0;
    if (ry < 0.0) ry = 0.0;
    if (rx == 0.0 && ry == 0.0) {
        svg_path_moveto(path, x, y);
        svg_path_lineto(path, x+w, y);
        svg_path_lineto(path, x+w, y+h);
        svg_path_lineto(path, x, y+h);
        svg_path_close_subpath(path);
        return;
    }
    if (rx == 0.0) rx = ry;
    if (ry == 0.0) ry = rx;
    if (rx > w/2.0) rx = w/2.0;
    if (ry > h/2.0) ry = h/2.0;
    svg_path_moveto(path, x+rx, y);
    svg_path_lineto(path, x+w-rx, y);
    svg_arc_corner(path, x+w-rx, y+ry, rx, ry, -SVG_PI/2.0, 0.0, 6);
    svg_path_lineto(path, x+w, y+h-ry);
    svg_arc_corner(path, x+w-rx, y+h-ry, rx, ry, 0.0, SVG_PI/2.0, 6);
    svg_path_lineto(path, x+rx, y+h);
    svg_arc_corner(path, x+rx, y+h-ry, rx, ry, SVG_PI/2.0, SVG_PI, 6);
    svg_path_lineto(path, x, y+ry);
    svg_arc_corner(path, x+rx, y+ry, rx, ry, SVG_PI, 3.0*SVG_PI/2.0, 6);
    svg_path_close_subpath(path);
}

static void svg_shape_ellipse(SvgPath *path, double ccx, double ccy, double rx, double ry) {
    int i; int N = 48;
    for (i = 0; i < N; i++) {
        double t = 2.0 * SVG_PI * ((double)i / (double)N);
        double x = ccx + rx*cos(t), y = ccy + ry*sin(t);
        if (i == 0) svg_path_moveto(path, x, y); else svg_path_lineto(path, x, y);
    }
    svg_path_close_subpath(path);
}

static void svg_shape_points(SvgPath *path, const char *pts, int close) {
    const char *p = pts;
    int first = 1;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        double x = svg_read_num(&p);
        double y = svg_read_num(&p);
        if (first) { svg_path_moveto(path, x, y); first = 0; } else svg_path_lineto(path, x, y);
    }
    if (close && !first) svg_path_close_subpath(path);
}

/* ---------------- rasterization ---------------- */

static void svg_blend_pixel(unsigned char *rgba, int W, int H, int x, int y, unsigned char r, unsigned char g, unsigned char b, double alpha) {
    if (x < 0 || x >= W || y < 0 || y >= H || alpha <= 0.0) return;
    if (alpha > 1.0) alpha = 1.0;
    unsigned char *px = rgba + ((long)y * W + x) * 4;
    double dr = (double)px[0], dg = (double)px[1], db = (double)px[2], da = (double)px[3] / 255.0;
    double outa = alpha + da * (1.0 - alpha);
    double orr, ogg, obb;
    if (outa <= 0.0001) { orr = 0.0; ogg = 0.0; obb = 0.0; }
    else {
        orr = ((double)r*alpha + dr*da*(1.0-alpha)) / outa;
        ogg = ((double)g*alpha + dg*da*(1.0-alpha)) / outa;
        obb = ((double)b*alpha + db*da*(1.0-alpha)) / outa;
    }
    px[0] = (unsigned char)(orr + 0.5);
    px[1] = (unsigned char)(ogg + 0.5);
    px[2] = (unsigned char)(obb + 0.5);
    px[3] = (unsigned char)(outa * 255.0 + 0.5);
}

static void svg_blend_span(unsigned char *rgba, int W, int H, int y, double x0, double x1, unsigned char r, unsigned char g, unsigned char b, double alpha) {
    int ix0 = (int)floor(x0 + 0.5);
    int ix1 = (int)floor(x1 - 0.5);
    if (ix0 < 0) ix0 = 0;
    if (ix1 >= W) ix1 = W - 1;
    int x;
    for (x = ix0; x <= ix1; x++) svg_blend_pixel(rgba, W, H, x, y, r, g, b, alpha);
}

#define SVG_MAX_CROSSINGS 4096

static void svg_fill_subpaths(unsigned char *rgba, int W, int H, const SvgPath *path, unsigned char r, unsigned char g, unsigned char b, double alpha) {
    if (path->n < 2 || alpha <= 0.0) return;
    double miny = 1e18, maxy = -1e18;
    int i;
    for (i = 0; i < path->n; i++) {
        if (path->y[i] < miny) miny = path->y[i];
        if (path->y[i] > maxy) maxy = path->y[i];
    }
    int y0 = (int)floor(miny); if (y0 < 0) y0 = 0;
    int y1 = (int)ceil(maxy); if (y1 >= H) y1 = H - 1;
    double xs[SVG_MAX_CROSSINGS];
    int windings[SVG_MAX_CROSSINGS];
    int yy;
    for (yy = y0; yy <= y1; yy++) {
        double sy = (double)yy + 0.5;
        int ncross = 0;
        int sp;
        for (sp = 0; sp < path->subpath_count; sp++) {
            int s0 = path->subpath_start[sp];
            int s1 = (sp + 1 < path->subpath_count) ? path->subpath_start[sp+1] : path->n;
            int cnt = s1 - s0;
            if (cnt < 2) continue;
            int k;
            for (k = 0; k < cnt; k++) {
                int ia = s0 + k;
                int ib = s0 + ((k + 1) % cnt);
                double ax = path->x[ia], ay = path->y[ia];
                double bx = path->x[ib], by = path->y[ib];
                if (ay == by) continue;
                if ((sy >= ay && sy < by) || (sy >= by && sy < ay)) {
                    double t = (sy - ay) / (by - ay);
                    double ix = ax + t * (bx - ax);
                    if (ncross < SVG_MAX_CROSSINGS) {
                        xs[ncross] = ix;
                        windings[ncross] = (by > ay) ? 1 : -1;
                        ncross++;
                    }
                }
            }
        }
        int a, bidx;
        for (a = 1; a < ncross; a++) {
            double kx = xs[a]; int kw = windings[a];
            bidx = a - 1;
            while (bidx >= 0 && xs[bidx] > kx) { xs[bidx+1] = xs[bidx]; windings[bidx+1] = windings[bidx]; bidx--; }
            xs[bidx+1] = kx; windings[bidx+1] = kw;
        }
        int wind = 0;
        double span_start = 0.0;
        int in_span = 0;
        for (a = 0; a < ncross; a++) {
            int before = wind;
            wind += windings[a];
            if (before == 0 && wind != 0) { span_start = xs[a]; in_span = 1; }
            else if (before != 0 && wind == 0 && in_span) {
                svg_blend_span(rgba, W, H, yy, span_start, xs[a], r, g, b, alpha);
                in_span = 0;
            }
        }
    }
}

static void svg_stroke_subpaths(unsigned char *rgba, int W, int H, const SvgPath *path, unsigned char r, unsigned char g, unsigned char b, double alpha, double width_px) {
    if (width_px <= 0.0) width_px = 1.0;
    double hw = width_px / 2.0;
    int sp;
    for (sp = 0; sp < path->subpath_count; sp++) {
        int s0 = path->subpath_start[sp];
        int s1 = (sp + 1 < path->subpath_count) ? path->subpath_start[sp+1] : path->n;
        int cnt = s1 - s0;
        if (cnt < 2) continue;
        int closed = path->subpath_closed[sp];
        int kmax = closed ? cnt : (cnt - 1);
        int k;
        for (k = 0; k < kmax; k++) {
            int ia = s0 + k, ib = s0 + ((k + 1) % cnt);
            double ax = path->x[ia], ay = path->y[ia], bx = path->x[ib], by = path->y[ib];
            double dx = bx - ax, dy = by - ay;
            double len = sqrt(dx*dx + dy*dy);
            if (len < 1e-6) continue;
            double nx = -dy / len * hw, ny = dx / len * hw;
            SvgPath quad; svg_path_init(&quad);
            svg_path_moveto(&quad, ax+nx, ay+ny);
            svg_path_lineto(&quad, bx+nx, by+ny);
            svg_path_lineto(&quad, bx-nx, by-ny);
            svg_path_lineto(&quad, ax-nx, ay-ny);
            svg_path_close_subpath(&quad);
            svg_fill_subpaths(rgba, W, H, &quad, r, g, b, alpha);
            svg_path_free(&quad);
        }
    }
}

/* ---------------- style ---------------- */

typedef struct {
    int has_fill; unsigned char fr, fg, fb; double fill_opacity;
    int has_stroke; unsigned char sr, sg, sb; double stroke_width, stroke_opacity;
    double opacity;
    int invisible;
} SvgStyle;

/* Out-parameter, not a return value -- see svg_mat_identity()'s own comment
 * for why (SvgStyle is also well over 16 bytes). */
static void svg_default_style(SvgStyle *out) {
    out->has_fill = 1; out->fr = 0; out->fg = 0; out->fb = 0; out->fill_opacity = 1.0;
    out->has_stroke = 0; out->sr = 0; out->sg = 0; out->sb = 0; out->stroke_width = 1.0; out->stroke_opacity = 1.0;
    out->opacity = 1.0;
    out->invisible = 0;
}

static void svg_apply_fill_str(SvgStyle *st, const char *f, SvgGradientTable *grad) {
    if (!strncmp(f, "none", 4)) { st->has_fill = 0; return; }
    if (!strncmp(f, "url(", 4)) {
        unsigned char r, g, b;
        if (svg_lookup_gradient_color(grad, f, &r, &g, &b)) { st->has_fill = 1; st->fr = r; st->fg = g; st->fb = b; }
        return;
    }
    unsigned char r, g, b;
    if (svg_parse_color(f, &r, &g, &b)) { st->has_fill = 1; st->fr = r; st->fg = g; st->fb = b; }
}

static void svg_apply_stroke_str(SvgStyle *st, const char *s) {
    if (!strncmp(s, "none", 4)) { st->has_stroke = 0; return; }
    unsigned char r, g, b;
    if (svg_parse_color(s, &r, &g, &b)) { st->has_stroke = 1; st->sr = r; st->sg = g; st->sb = b; }
}

static void svg_parse_style_attr(const char *style_str, SvgStyle *st, SvgGradientTable *grad, int *fill_touched, int *stroke_touched) {
    char buf[256];
    svg_extract_style_prop(style_str, "fill", buf, (int)sizeof buf);
    if (buf[0]) { svg_apply_fill_str(st, buf, grad); *fill_touched = 1; }
    svg_extract_style_prop(style_str, "stroke", buf, (int)sizeof buf);
    if (buf[0]) { svg_apply_stroke_str(st, buf); *stroke_touched = 1; }
    svg_extract_style_prop(style_str, "opacity", buf, (int)sizeof buf);
    if (buf[0]) st->opacity *= strtod(buf, NULL);
    svg_extract_style_prop(style_str, "fill-opacity", buf, (int)sizeof buf);
    if (buf[0]) st->fill_opacity = strtod(buf, NULL);
    svg_extract_style_prop(style_str, "stroke-opacity", buf, (int)sizeof buf);
    if (buf[0]) st->stroke_opacity = strtod(buf, NULL);
    svg_extract_style_prop(style_str, "stroke-width", buf, (int)sizeof buf);
    if (buf[0]) st->stroke_width = strtod(buf, NULL);
}

static void svg_compute_style(SvgStyle *out, const SvgStyle *parent, SvgTok *t, SvgGradientTable *grad) {
    SvgStyle st = *parent;
    st.opacity = 1.0;
    int fill_touched = 0, stroke_touched = 0;
    const char *style_attr = svg_attr(t, "style");
    if (style_attr) svg_parse_style_attr(style_attr, &st, grad, &fill_touched, &stroke_touched);
    if (!fill_touched) {
        const char *f = svg_attr(t, "fill");
        if (f) svg_apply_fill_str(&st, f, grad);
    }
    const char *fo = svg_attr(t, "fill-opacity");
    if (fo) st.fill_opacity = strtod(fo, NULL);
    if (!stroke_touched) {
        const char *s = svg_attr(t, "stroke");
        if (s) svg_apply_stroke_str(&st, s);
    }
    const char *sw = svg_attr(t, "stroke-width");
    if (sw) st.stroke_width = strtod(sw, NULL);
    const char *so = svg_attr(t, "stroke-opacity");
    if (so) st.stroke_opacity = strtod(so, NULL);
    const char *op = svg_attr(t, "opacity");
    double own_opacity = op ? strtod(op, NULL) : st.opacity;
    st.opacity = parent->opacity * own_opacity;
    *out = st;
}

/* ---------------- natural size ---------------- */

static double svg_parse_len(const char *s, double defval) {
    if (!s) return defval;
    const char *p = s;
    return svg_read_num(&p);
}

static void svg_natural_size(const unsigned char *data, long len, int *out_w, int *out_h, double *vb_x, double *vb_y, double *vb_w, double *vb_h) {
    SvgLexer lex; svg_lexer_init(&lex, (const char *)data, len);
    SvgTok tok;
    *out_w = 0; *out_h = 0; *vb_x = 0.0; *vb_y = 0.0; *vb_w = 0.0; *vb_h = 0.0;
    for (;;) {
        if (!svg_lexer_next(&lex, &tok)) break;
        if (!tok.is_close && !strcmp(tok.name, "svg")) {
            const char *vb = svg_attr(&tok, "viewBox");
            if (vb) {
                const char *p = vb;
                *vb_x = svg_read_num(&p);
                *vb_y = svg_read_num(&p);
                *vb_w = svg_read_num(&p);
                *vb_h = svg_read_num(&p);
            }
            double w = svg_parse_len(svg_attr(&tok, "width"), 0.0);
            double h = svg_parse_len(svg_attr(&tok, "height"), 0.0);
            if (w > 0.0) *out_w = (int)(w + 0.5);
            if (h > 0.0) *out_h = (int)(h + 0.5);
            if (*out_w == 0 && *vb_w > 0.0) *out_w = (int)(*vb_w + 0.5);
            if (*out_h == 0 && *vb_h > 0.0) *out_h = (int)(*vb_h + 0.5);
            if (*vb_w == 0.0 && *out_w > 0) *vb_w = (double)*out_w;
            if (*vb_h == 0.0 && *out_h > 0) *vb_h = (double)*out_h;
            svg_tok_free(&tok);
            return;
        }
        svg_tok_free(&tok);
    }
}

/* ---------------- main tree walk ---------------- */

typedef struct { SvgMat matrix; SvgStyle style; } SvgStackFrame;

static int svg_tag_is_shape(const char *name) {
    return !strcmp(name, "rect") || !strcmp(name, "circle") || !strcmp(name, "ellipse") ||
           !strcmp(name, "line") || !strcmp(name, "polygon") || !strcmp(name, "polyline") ||
           !strcmp(name, "path");
}

static int svg_tag_is_group(const char *name) {
    return !strcmp(name, "g") || !strcmp(name, "a") || !strcmp(name, "svg");
}

static int svg_tag_is_nonrendering_container(const char *name) {
    return !strcmp(name, "defs") || !strcmp(name, "symbol") || !strcmp(name, "clipPath") || !strcmp(name, "mask");
}

static void svg_render_shape(unsigned char *rgba, int W, int H, SvgTok *tok, SvgMat cur_m, SvgStyle cur_style) {
    SvgPath shape; svg_path_init(&shape);
    int is_line = 0;
    if (!strcmp(tok->name, "rect")) {
        double x = svg_attr_num(tok, "x", 0.0), y = svg_attr_num(tok, "y", 0.0);
        double w = svg_attr_num(tok, "width", 0.0), h = svg_attr_num(tok, "height", 0.0);
        double rx = svg_attr_num(tok, "rx", -1.0), ry = svg_attr_num(tok, "ry", -1.0);
        if (rx < 0.0 && ry >= 0.0) rx = ry;
        if (ry < 0.0 && rx >= 0.0) ry = rx;
        if (rx < 0.0) rx = 0.0;
        if (ry < 0.0) ry = 0.0;
        if (w > 0.0 && h > 0.0) svg_shape_rect(&shape, x, y, w, h, rx, ry);
    } else if (!strcmp(tok->name, "circle")) {
        double ccx = svg_attr_num(tok, "cx", 0.0), ccy = svg_attr_num(tok, "cy", 0.0), rr = svg_attr_num(tok, "r", 0.0);
        if (rr > 0.0) svg_shape_ellipse(&shape, ccx, ccy, rr, rr);
    } else if (!strcmp(tok->name, "ellipse")) {
        double ccx = svg_attr_num(tok, "cx", 0.0), ccy = svg_attr_num(tok, "cy", 0.0);
        double rx = svg_attr_num(tok, "rx", 0.0), ry = svg_attr_num(tok, "ry", 0.0);
        if (rx > 0.0 && ry > 0.0) svg_shape_ellipse(&shape, ccx, ccy, rx, ry);
    } else if (!strcmp(tok->name, "line")) {
        double x1 = svg_attr_num(tok, "x1", 0.0), y1 = svg_attr_num(tok, "y1", 0.0);
        double x2 = svg_attr_num(tok, "x2", 0.0), y2 = svg_attr_num(tok, "y2", 0.0);
        svg_path_moveto(&shape, x1, y1);
        svg_path_lineto(&shape, x2, y2);
        is_line = 1;
    } else if (!strcmp(tok->name, "polygon")) {
        const char *pts = svg_attr(tok, "points");
        if (pts) svg_shape_points(&shape, pts, 1);
    } else if (!strcmp(tok->name, "polyline")) {
        const char *pts = svg_attr(tok, "points");
        if (pts) svg_shape_points(&shape, pts, 0);
    } else if (!strcmp(tok->name, "path")) {
        const char *dd = svg_attr(tok, "d");
        if (dd) svg_parse_path_d(dd, &shape);
    }
    svg_path_transform(&shape, cur_m);
    if (!is_line && cur_style.has_fill) {
        svg_fill_subpaths(rgba, W, H, &shape, cur_style.fr, cur_style.fg, cur_style.fb, cur_style.fill_opacity * cur_style.opacity);
    }
    if (cur_style.has_stroke && cur_style.stroke_width > 0.0) {
        double sw_px = cur_style.stroke_width * svg_mat_scale_factor(cur_m);
        svg_stroke_subpaths(rgba, W, H, &shape, cur_style.sr, cur_style.sg, cur_style.sb, cur_style.stroke_opacity * cur_style.opacity, sw_px);
    }
    svg_path_free(&shape);
}

static void svg_render_pass(const unsigned char *data, long len, unsigned char *rgba, int W, int H, SvgMat root_matrix, SvgGradientTable *grad) {
    SvgLexer lex; svg_lexer_init(&lex, (const char *)data, len);
    SvgTok tok;
    SvgStackFrame stack[SVG_MAX_DEPTH];
    int depth = 0;
    stack[0].matrix = root_matrix;
    svg_default_style(&stack[0].style);
    depth = 1;
    int found_root = 0;
    for (;;) {
        if (!svg_lexer_next(&lex, &tok)) break;
        if (!tok.is_close) {
            if (!found_root) {
                if (!strcmp(tok.name, "svg")) { found_root = 1; }
                svg_tok_free(&tok);
                continue;
            }
            SvgStackFrame parent = stack[depth - 1];
            SvgMat local_m; svg_parse_transform(&local_m, svg_attr(&tok, "transform"));
            SvgMat cur_m; svg_mat_mul(&cur_m, parent.matrix, local_m);
            SvgStyle cur_style; svg_compute_style(&cur_style, &parent.style, &tok, grad);

            if (svg_tag_is_group(tok.name)) {
                if (!tok.self_closing && depth < SVG_MAX_DEPTH) {
                    stack[depth].matrix = cur_m;
                    stack[depth].style = cur_style;
                    depth++;
                }
            } else if (svg_tag_is_nonrendering_container(tok.name)) {
                if (!tok.self_closing && depth < SVG_MAX_DEPTH) {
                    stack[depth].matrix = cur_m;
                    cur_style.invisible = 1;
                    stack[depth].style = cur_style;
                    depth++;
                }
            } else if (!parent.style.invisible && svg_tag_is_shape(tok.name)) {
                svg_render_shape(rgba, W, H, &tok, cur_m, cur_style);
                if (!tok.self_closing && depth < SVG_MAX_DEPTH) {
                    cur_style.invisible = 1;
                    stack[depth].matrix = cur_m;
                    stack[depth].style = cur_style;
                    depth++;
                }
            } else {
                if (!tok.self_closing && depth < SVG_MAX_DEPTH) {
                    cur_style.invisible = 1;
                    stack[depth].matrix = cur_m;
                    stack[depth].style = cur_style;
                    depth++;
                }
            }
            svg_tok_free(&tok);
        } else {
            if (depth > 1) depth--;
            svg_tok_free(&tok);
        }
    }
}

/* ---------------- public entry point ---------------- */

int sqw_svg_decode(const unsigned char *data, long len, unsigned char **out_rgba, int *out_w, int *out_h) {
    if (!data || len <= 0) return 0;
    /* quick sniff: real magic-byte formats (PNG/GIF/JPEG) are ruled out by
     * the caller before this is ever invoked; here just confirm there's an
     * <svg tag somewhere near the start (tolerating a leading BOM/XML decl/
     * comments, which the tokenizer itself skips during the real scan). */
    int w = 0, h = 0;
    double vbx = 0.0, vby = 0.0, vbw = 0.0, vbh = 0.0;
    svg_natural_size(data, len, &w, &h, &vbx, &vby, &vbw, &vbh);
    if (w <= 0 || h <= 0) return 0;
    if (w > SVG_MAX_DIM) w = SVG_MAX_DIM;
    if (h > SVG_MAX_DIM) h = SVG_MAX_DIM;

    unsigned char *rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (!rgba) return 0;
    memset(rgba, 0, (size_t)w * (size_t)h * 4);

    SvgGradientTable grad;
    svg_collect_gradients(data, len, &grad);

    SvgMat root_matrix;
    svg_mat_identity(&root_matrix);
    if (vbw > 0.0 && vbh > 0.0) {
        root_matrix.a = (double)w / vbw;
        root_matrix.d = (double)h / vbh;
        root_matrix.e = -vbx * root_matrix.a;
        root_matrix.f = -vby * root_matrix.d;
    }

    svg_render_pass(data, len, rgba, w, h, root_matrix, &grad);

    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 1;
}
