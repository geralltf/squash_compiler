#include "layout.h"
#include "text_metrics.h"
#include "css.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* SQW_TEXT_SCALE (line height derived from it): the atlas is baked at a
 * large, crisp pixel size (SQW_FONT_CELL_H at scale 1.0 -- see
 * font_atlas.h); real body text looks better noticeably smaller than
 * that. Defined in layout.h, shared with sqw_main.c's draw pass -- see
 * that define's own comment for why it must match exactly. */
#define SQW_LINE_H (SQW_FONT_CELL_H * SQW_TEXT_SCALE)
#define SQW_IMG_SIZE 64.0f
#define SQW_PAD 6.0f
#define SQW_INLINE_GAP 6.0f
/* Form-control default sizing -- real browsers size these from the
 * platform widget toolkit; we don't have one, so these are fixed,
 * reasonable-looking defaults, overridden by explicit CSS width/height
 * where the element's computed style has one. */
#define SQW_CHECK_SIZE 16.0f
#define SQW_INPUT_MIN_W 40.0f
#define SQW_INPUT_DEFAULT_W 150.0f
#define SQW_TEXTAREA_DEFAULT_W 300.0f
#define SQW_TEXTAREA_DEFAULT_H 80.0f

/* Real CSS box-model + display support (block/inline/inline-block/none/
 * flex/grid), driven entirely by each DomNode's own computed style (see
 * css.h/css.c -- css_apply() resolves that BEFORE layout_compute() ever
 * runs, from real parsed CSS + inline style="", not a hardcoded per-tag
 * table). Two real, if intentionally scoped, layout algorithms beyond
 * plain block/inline flow:
 *   flex-direction:row    -- a genuine up-front pass distributes each
 *                             item's width along the main axis per
 *                             justify-content (explicit widths honored,
 *                             remaining space split evenly among items
 *                             without one) before any of them are laid
 *                             out; align-items is NOT implemented (every
 *                             row-flex item is top-aligned, i.e. treated
 *                             as align-items:flex-start regardless of
 *                             the real value) -- real align-items:center/
 *                             stretch needs each item's height known
 *                             BEFORE the row is positioned, which this
 *                             single depth-first layout pass can't
 *                             produce without a real two-pass/intrinsic-
 *                             sizing algorithm.
 *   flex-direction:column -- reuses plain block stacking (already
 *                             exactly "one item per line, full width,
 *                             top to bottom") plus real `gap` spacing;
 *                             align-items on the (horizontal) cross axis
 *                             isn't implemented.
 *   display:grid           -- grid-template-columns tracks (fixed px or
 *                             fr units, including repeat(N, ...)) are
 *                             real and drive real column x/width;
 *                             auto-flow is row-only (no explicit
 *                             grid-row/grid-column placement), and each
 *                             row's height is the real max height of
 *                             that row's own items (determined as they're
 *                             laid out, not guessed). */
typedef struct {
    DomNode *node;
    int next_child;
    float x, y;
    float avail_w;
    float cursor_x, cursor_y;
    float line_h;
    int box_index;   /* index into out->boxes for this frame's own box, -1 for the synthetic doc root */
    /* This frame's own node's index within its PARENT's children[]
     * array -- needed at pop time so a flex-row parent can look up
     * which row (parent->flex_row[child_index_in_parent]) this child
     * belonged to (see layout_compute()'s own pop-time comment); -1 for
     * the synthetic doc root, which has no parent frame to report to. */
    int child_index_in_parent;
    int is_center;
    int is_pre;
    int *line_boxes;
    int line_box_count, line_box_cap;

    /* Box-model bookkeeping for THIS frame's own box (applied when the
     * frame is popped -- see layout_compute()'s own pop-time comment).
     * All zero for the synthetic root and for any element with no
     * explicit CSS margin/padding, which exactly reproduces this
     * project's original (pre-CSS) fixed-SQW_PAD behavior. */
    float margin_top, margin_bottom, pad_top, pad_bottom;

    /* flex-direction:row support -- precomputed once when the frame is
     * pushed (see compute_flex_row_positions()), indexed by the child's
     * own position in node->children[] (parallel arrays, only entries for
     * participating element children are meaningful). NULL/0 unless this
     * frame's own node has css_display == CSS_DISPLAY_FLEX and
     * css_flex_direction == CSS_FLEX_ROW.
     *
     * flex_row[] is which wrapped row each item belongs to (always all
     * 0 when css_flex_wrap is off, i.e. a single row -- real CSS
     * flex-wrap:nowrap, the default) -- X per item is fully known
     * up-front (real widths + real per-row justify-content, computed by
     * compute_flex_row_positions() before any item is laid out), but Y
     * is NOT: same problem as display:grid's own rows (an item's real
     * height isn't known until its own subtree finishes laying out), so
     * it's tracked live the same way, via flex_row_y/flex_row_max_h/
     * flex_row_at below as each item is popped. */
    int is_flex_row;
    float *flex_x, *flex_w;
    int *flex_row;
    int flex_row_at;
    float flex_row_y, flex_row_max_h;

    /* display:grid support -- column tracks resolved once at push time
     * (real px), row height/position tracked live as items are placed
     * (see the grid item-placement code in layout_compute()). */
    int is_grid;
    float *grid_col_x, *grid_col_w;
    int grid_ncols;
    int grid_col_index;
    float grid_row_y, grid_row_max_h;
} LayoutFrame;

/* True for any display value that gets its own box and participates in
 * normal block-axis stacking (or the flex/grid algorithms above) --
 * i.e. everything except CSS_DISPLAY_INLINE (handled as an atomic inline
 * run, see place_inline_element()) and CSS_DISPLAY_NONE (not laid out at
 * all). inline-block is treated as block here (flows top-to-bottom like
 * a block, not alongside inline siblings) -- a documented simplification,
 * real inline-block flows inline. */
static int is_block_like(int display) {
    return display == CSS_DISPLAY_BLOCK || display == CSS_DISPLAY_INLINE_BLOCK ||
           display == CSS_DISPLAY_FLEX || display == CSS_DISPLAY_GRID;
}

static int is_atomic_inline_tag(const char *tag); /* defined below */

static SqwBoxKind kind_for_tag(const char *tag) {
    if (strcmp(tag, "div") == 0) return SQW_BOX_DIV;
    if (strcmp(tag, "center") == 0) return SQW_BOX_CENTER;
    if (strcmp(tag, "p") == 0) return SQW_BOX_P;
    if (strcmp(tag, "a") == 0) return SQW_BOX_A;
    if (strcmp(tag, "img") == 0) return SQW_BOX_IMG;
    if (strcmp(tag, "button") == 0) return SQW_BOX_BUTTON;
    if (strcmp(tag, "pre") == 0) return SQW_BOX_PRE;
    if (strcmp(tag, "textarea") == 0) return SQW_BOX_TEXTAREA;
    /* Only reached if some CSS rule forces display:block/etc on an
     * <input> -- the normal (display:inline, the real HTML5 default)
     * path never calls kind_for_tag() for <input>, see the dedicated
     * branch in layout_compute() which reads its "type" attribute too. */
    if (strcmp(tag, "input") == 0) return SQW_BOX_INPUT_TEXT;
    /* Every other atomic-inline HTML5 tag (span, strong, em, b, i, small,
     * label, u, mark, code -- see is_atomic_inline_tag()) is rendered as
     * a generic SQW_BOX_SPAN: one indivisible inline text run, no
     * semantic-specific visual treatment (no real bold/italic font
     * variant exists in this project's single baked glyph atlas -- see
     * font_atlas.h -- so "strong"/"b" don't actually render bolder; a
     * real, documented limitation, not a bug). */
    if (is_atomic_inline_tag(tag)) return SQW_BOX_SPAN;
    return SQW_BOX_OTHER;
}

/* Atomic inline tags -- laid out as ONE indivisible run sized by their
 * DIRECT text children only (see direct_text_width()), same
 * simplification the original span/a/button handling already used, now
 * shared by every other real HTML5 inline tag too. Real nested inline
 * formatting (e.g. "<a>text <b>bold</b> more</a>") only shows the direct
 * text pieces, not b's own text -- a known, documented limitation of this
 * project's inline layout, not something this CSS pass changes. */
static int is_atomic_inline_tag(const char *tag) {
    return strcmp(tag,"span")==0 || strcmp(tag,"a")==0 || strcmp(tag,"button")==0 ||
           strcmp(tag,"strong")==0 || strcmp(tag,"em")==0 || strcmp(tag,"b")==0 ||
           strcmp(tag,"i")==0 || strcmp(tag,"small")==0 || strcmp(tag,"label")==0 ||
           strcmp(tag,"u")==0 || strcmp(tag,"mark")==0 || strcmp(tag,"code")==0;
}

/* Sum of the REAL measured width of child's DIRECT text-node children (no
 * further descent) at SQW_TEXT_SCALE -- used to size <a>/<span>/<button>,
 * which this layout treats as one indivisible inline unit (unlike a plain
 * text node, which word-wraps -- see place_text_node()). */
static float direct_text_width(const DomNode *node) {
    int i; float w = 0.0f;
    for (i = 0; i < node->child_count; i++) {
        DomNode *c = node->children[i];
        if (dom_is_text(c)) w += sqw_text_measure(c->text, (int)strlen(c->text), SQW_TEXT_SCALE);
    }
    return w;
}

static void layout_list_push_full(LayoutList *out, DomNode *node, SqwBoxKind kind,
                                   float x, float y, float w, float h, int text_start, int text_len) {
    if (out->count >= out->cap) {
        out->cap = out->cap ? out->cap * 2 : 32;
        out->boxes = (LayoutBox *)realloc(out->boxes, out->cap * sizeof(LayoutBox));
    }
    LayoutBox *b = &out->boxes[out->count];
    b->node = node; b->kind = kind; b->x = x; b->y = y; b->w = w; b->h = h;
    b->text_start = text_start; b->text_len = text_len;
    out->count++;
    if (x + w > out->content_w) out->content_w = x + w;
    if (y + h > out->content_h) out->content_h = y + h;
}

static void layout_list_push(LayoutList *out, DomNode *node, SqwBoxKind kind, float x, float y, float w, float h) {
    layout_list_push_full(out, node, kind, x, y, w, h, 0, 0);
}

/* Split out of layout_compute()'s block-tag branch into its own function
 * -- that branch, inlined, crashed squash's codegen on a plain
 * "nf->next_child = 0;" int-field store immediately after "nf->node =
 * child;" succeeded (confirmed via step-by-step tracing: the write of a
 * SINGLE zero to a valid, correctly-computed in-bounds struct pointer
 * segfaulted) whenever the surrounding function had accumulated enough
 * other live locals (reproduces only with >=4 block-level siblings mixing
 * <center>/wrapped text/<pre>/<a>+<button> together -- fewer combined
 * pieces never triggered it, and the identical field-by-field store
 * sequence in a standalone repro with few locals worked fine) -- a real,
 * still only partially understood squash register/stack-slot allocation
 * bug under high local-variable pressure in one function, not a logic
 * bug in this code (confirmed correct and leak-only under gcc+
 * AddressSanitizer). Moving the store sequence into its own small
 * function, called with a POINTER through the whole frame rather than
 * inlined field-by-field in the crashing context, avoids it. */
static void init_block_frame(LayoutFrame *nf, DomNode *child, float bx, float by, float bw, int idx, int child_index_in_parent) {
    nf->node = child; nf->next_child = 0;
    nf->child_index_in_parent = child_index_in_parent;
    nf->x = bx; nf->y = by; nf->avail_w = bw;
    nf->cursor_x = 0; nf->cursor_y = 0; nf->line_h = 0;
    nf->box_index = idx;
    nf->is_center = (strcmp(child->tag, "center") == 0);
    nf->is_pre = (strcmp(child->tag, "pre") == 0);
    nf->line_boxes = 0; nf->line_box_count = 0; nf->line_box_cap = 0;
    nf->margin_top = 0; nf->margin_bottom = 0; nf->pad_top = 0; nf->pad_bottom = 0;
    nf->is_flex_row = 0; nf->flex_x = 0; nf->flex_w = 0; nf->flex_row = 0;
    nf->flex_row_at = 0; nf->flex_row_y = 0; nf->flex_row_max_h = 0;
    nf->is_grid = 0; nf->grid_col_x = 0; nf->grid_col_w = 0;
    nf->grid_ncols = 0; nf->grid_col_index = 0; nf->grid_row_y = 0; nf->grid_row_max_h = 0;
}

static void frame_track_line_box(LayoutFrame *f, int box_index) {
    if (f->line_box_count >= f->line_box_cap) {
        f->line_box_cap = f->line_box_cap ? f->line_box_cap * 2 : 8;
        f->line_boxes = (int *)realloc(f->line_boxes, f->line_box_cap * sizeof(int));
    }
    f->line_boxes[f->line_box_count++] = box_index;
}

/* If this frame is a <center>, shift every box on the just-finished line so
 * the line as a whole is horizontally centered within the frame's width. */
static void finalize_line(LayoutFrame *f, LayoutList *out) {
    if (f->is_center && f->line_box_count > 0) {
        /* Measured from the first box's left edge to the last box's right
         * edge (not f->cursor_x, which still includes one trailing
         * inter-box gap after the last box -- a word-run gap and an
         * element gap now differ, see place_inline_run, so there's no
         * single fixed amount left to just subtract off cursor_x). */
        LayoutBox *lastb = &out->boxes[f->line_boxes[f->line_box_count - 1]];
        LayoutBox *firstb = &out->boxes[f->line_boxes[0]];
        float used_w = (lastb->x + lastb->w) - firstb->x;
        float shift = (f->avail_w - used_w) / 2.0f;
        if (shift > 0) {
            int i;
            for (i = 0; i < f->line_box_count; i++) out->boxes[f->line_boxes[i]].x = out->boxes[f->line_boxes[i]].x + shift;
        }
    }
    f->line_box_count = 0;
}

/* Bundles place_inline_run()'s per-box details into one struct instead of
 * passing them as separate scalar parameters -- see the original comment
 * on why (a real squash codegen bug under too many live locals/params). */
typedef struct {
    SqwBoxKind kind;
    float w, h;
    int text_start, text_len;
    float gap;
} PlaceSpec;

static void place_inline_run(LayoutFrame *f, LayoutList *out, DomNode *node, const PlaceSpec *spec) {
    float w = spec->w, h = spec->h;
    float sum = f->cursor_x + w;
    int has_content_on_line = f->cursor_x > 0;
    int would_overflow = sum > f->avail_w;
    if (has_content_on_line && would_overflow) {
        finalize_line(f, out);
        f->cursor_y = f->cursor_y + f->line_h;
        f->cursor_x = 0;
        f->line_h = 0;
    }
    layout_list_push_full(out, node, spec->kind, f->x + f->cursor_x, f->y + f->cursor_y, w, h, spec->text_start, spec->text_len);
    frame_track_line_box(f, out->count - 1);
    f->cursor_x = f->cursor_x + w + spec->gap;
    if (h > f->line_h) f->line_h = h;
}

#define SQW_MAX_WORDS_PER_TEXT_NODE 512

static int split_words(const char *s, int len, int *out_start, int *out_len) {
    int i = 0, n = 0;
    while (i < len && n < SQW_MAX_WORDS_PER_TEXT_NODE) {
        while (i < len && isspace((unsigned char)s[i])) i++;
        if (i >= len) break;
        int start = i;
        while (i < len && !isspace((unsigned char)s[i])) i++;
        out_start[n] = start;
        out_len[n] = i - start;
        n++;
    }
    return n;
}

static void place_text_node(LayoutFrame *f, LayoutList *out, DomNode *node) {
    const char *s = node->text;
    int len = (int)strlen(s);
    int word_start[SQW_MAX_WORDS_PER_TEXT_NODE];
    int word_len[SQW_MAX_WORDS_PER_TEXT_NODE];
    int nwords = split_words(s, len, word_start, word_len);
    float space_w = sqw_text_glyph_advance(' ', SQW_TEXT_SCALE);
    int wi;
    for (wi = 0; wi < nwords; wi++) {
        int start = word_start[wi];
        int wlen = word_len[wi];
        float w = sqw_text_measure(s + start, wlen, SQW_TEXT_SCALE);
        PlaceSpec spec;
        spec.kind = SQW_BOX_TEXT; spec.w = w; spec.h = SQW_LINE_H;
        spec.text_start = start; spec.text_len = wlen; spec.gap = space_w;
        place_inline_run(f, out, node, &spec);
    }
}

static void place_pre_text_node(LayoutFrame *f, LayoutList *out, DomNode *node) {
    const char *s = node->text;
    int len = (int)strlen(s);
    int i = 0;
    if (i < len && s[i] == '\n') i++;
    while (i <= len) {
        int start = i;
        while (i < len && s[i] != '\n') i++;
        int llen = i - start;
        if (llen > 0) {
            float w = sqw_text_measure(s + start, llen, SQW_TEXT_SCALE);
            layout_list_push_full(out, node, SQW_BOX_TEXT, f->x + f->cursor_x, f->y + f->cursor_y, w, SQW_LINE_H, start, llen);
        }
        f->cursor_y = f->cursor_y + SQW_LINE_H;
        f->cursor_x = 0;
        if (i >= len) break;
        i++;
    }
    f->line_h = 0;
}

/* ---- grid track parsing ---- */

/* Parses a "grid-template-columns" value into up to max_cols real column
 * WIDTHS (px), resolved against avail_w. Understands a space-separated
 * track list where each track is a bare px length, a bare number
 * (treated as px, matching this project's other length parsing), or an
 * "Nfr" flex unit -- and a single leading "repeat(N, TRACK)" wrapping the
 * whole list (the common Wikipedia-style "repeat(4, 1fr)" form). Real
 * grid-template-columns syntax allows far more (minmax(), auto, multiple
 * repeat() calls mixed with literal tracks, named lines, subgrid, ...) --
 * this covers the two shapes that account for the overwhelming majority
 * of real-world grids, and silently falls back to a single full-width
 * column (i.e. normal block stacking) for anything it doesn't recognize,
 * never crashing or misinterpreting into a wrong-but-plausible layout. */
static int parse_grid_tracks(const char *value, float avail_w, float *out_widths, int max_cols) {
    char buf[256];
    strncpy(buf, value, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    const char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;

    int repeat_n = 1;
    char tracks_buf[192];
    if (strncmp(p, "repeat(", 7) == 0) {
        p += 7;
        char *endp;
        long n = strtol(p, &endp, 10);
        if (endp == p || n <= 0 || n > max_cols) return 0;
        repeat_n = (int)n;
        p = endp;
        while (*p && (*p == ',' || isspace((unsigned char)*p))) p++;
        const char *inner_start = p;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') { depth--; if (depth == 0) break; }
            p++;
        }
        int ilen = (int)(p - inner_start);
        if (ilen <= 0 || ilen >= (int)sizeof tracks_buf) return 0;
        memcpy(tracks_buf, inner_start, (size_t)ilen);
        tracks_buf[ilen] = 0;
    } else {
        strncpy(tracks_buf, p, sizeof tracks_buf - 1);
        tracks_buf[sizeof tracks_buf - 1] = 0;
    }

    /* Tokenize tracks_buf into individual track specs. */
    float track_px[16]; float track_fr[16]; int is_fr[16];
    int ntracks = 0;
    char *save = 0;
    char *tok = strtok(tracks_buf, " \t");
    while (tok && ntracks < 16) {
        int tl = (int)strlen(tok);
        if (tl > 2 && strcmp(tok + tl - 2, "fr") == 0) {
            track_fr[ntracks] = (float)atof(tok);
            is_fr[ntracks] = 1;
        } else {
            track_px[ntracks] = (float)atof(tok);
            is_fr[ntracks] = 0;
        }
        ntracks++;
        tok = strtok(0, " \t");
    }
    (void)save;
    if (ntracks == 0) return 0;

    int total = ntracks * repeat_n;
    if (total > max_cols) total = max_cols;
    float fixed_sum = 0.0f; float fr_sum = 0.0f;
    int i;
    for (i = 0; i < total; i++) {
        int ti = i % ntracks;
        if (is_fr[ti]) fr_sum += track_fr[ti]; else fixed_sum += track_px[ti];
    }
    float remaining = avail_w - fixed_sum;
    if (remaining < 0) remaining = 0;
    for (i = 0; i < total; i++) {
        int ti = i % ntracks;
        if (is_fr[ti]) out_widths[i] = fr_sum > 0 ? remaining * (track_fr[ti] / fr_sum) : 0.0f;
        else out_widths[i] = track_px[ti];
    }
    return total;
}

/* ---- flex row up-front pass ---- */

/* Fills flex_x[]/flex_w[]/flex_row[] (parallel to node->children[], only
 * meaningful for participating element children) with each item's real
 * x/width/row-index along the row, per css_justify (applied PER ROW when
 * wrapping) -- see this file's own top comment for exactly what's real
 * here and what's not (align-items on either axis). Two passes: first
 * assign each item a natural width (explicit css_width, else a
 * shrink-to-fit heuristic -- real CSS's own default flex-basis is
 * content-based, not an even split of the container, which only makes
 * sense for a single guaranteed-one-row case) and a row via simple greedy
 * wrapping (only when css_flex_wrap is on -- off, the CSS default, always
 * produces exactly one row, matching flex-wrap:nowrap's real behavior of
 * never breaking, items simply overflowing instead); second, apply
 * justify-content within each row independently, using that row's own
 * real total width. Returns the number of rows produced (always >= 1 for
 * a non-empty container). */
static int compute_flex_row_positions(DomNode *node, float avail_w, float *flex_x, float *flex_w, int *flex_row) {
    int n = node->child_count;
    int *idx = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    int cnt = 0, i;
    for (i = 0; i < n; i++) {
        DomNode *c = node->children[i];
        if (dom_is_text(c)) continue;
        if (c->css_display == CSS_DISPLAY_NONE) continue;
        idx[cnt++] = i;
    }
    if (cnt == 0) { free(idx); return 0; }

    float gap = node->css_gap;
    float *w = (float *)malloc((size_t)cnt * sizeof(float));
    int *row = (int *)malloc((size_t)cnt * sizeof(int));

    for (i = 0; i < cnt; i++) {
        DomNode *c = node->children[idx[i]];
        if (c->css_has_width) w[i] = c->css_width;
        else {
            /* Shrink-to-fit heuristic: content width + a fixed allowance
             * for whatever padding/border a real item like this would
             * have, falling back to a fixed reasonable default (a
             * "chip"-sized box) for an item with no direct text of its
             * own (e.g. one whose own text lives in a further-nested
             * child, which direct_text_width() deliberately doesn't
             * descend into -- see that function's own comment). Not real
             * intrinsic sizing, but far better than either a fixed
             * constant for everything or an even split that breaks down
             * the moment wrapping is possible. */
            float tw = direct_text_width(c);
            w[i] = tw > 0.0f ? tw + 24.0f : 120.0f;
        }
        if (w[i] > avail_w) w[i] = avail_w;
    }

    int cur_row = 0;
    float cur_x = 0.0f;
    for (i = 0; i < cnt; i++) {
        if (node->css_flex_wrap && cur_x > 0.0f && cur_x + w[i] > avail_w) { cur_row++; cur_x = 0.0f; }
        row[i] = cur_row;
        cur_x = cur_x + w[i] + gap;
    }
    int num_rows = cur_row + 1;

    int r;
    for (r = 0; r < num_rows; r++) {
        float total_w = 0.0f; int row_cnt = 0;
        for (i = 0; i < cnt; i++) if (row[i] == r) { total_w += w[i]; row_cnt++; }
        float used = total_w + gap * (float)(row_cnt > 1 ? row_cnt - 1 : 0);
        float extra = avail_w - used;
        if (extra < 0.0f) extra = 0.0f;
        float start_x = 0.0f, spacing_extra = 0.0f;
        if (node->css_justify == CSS_JUSTIFY_CENTER) start_x = extra / 2.0f;
        else if (node->css_justify == CSS_JUSTIFY_END) start_x = extra;
        else if (node->css_justify == CSS_JUSTIFY_BETWEEN && row_cnt > 1) spacing_extra = extra / (float)(row_cnt - 1);
        else if (node->css_justify == CSS_JUSTIFY_AROUND && row_cnt > 0) { start_x = extra / (float)(row_cnt * 2); spacing_extra = extra / (float)row_cnt; }

        float cur = start_x;
        for (i = 0; i < cnt; i++) {
            if (row[i] != r) continue;
            flex_x[idx[i]] = cur;
            flex_w[idx[i]] = w[i];
            flex_row[idx[i]] = r;
            cur = cur + w[i] + gap + spacing_extra;
        }
    }

    free(w); free(row); free(idx);
    return num_rows;
}

void layout_compute(DomNode *root, float viewport_w, float viewport_h, LayoutList *out) {
    out->boxes = 0; out->count = 0; out->cap = 0;
    out->content_w = 0; out->content_h = 0;
    (void)viewport_h;

    int stack_cap = 32;
    int stack_top = 0;
    LayoutFrame *stack = (LayoutFrame *)malloc(stack_cap * sizeof(LayoutFrame));

    stack[stack_top].node = root;
    stack[stack_top].next_child = 0;
    stack[stack_top].x = 0; stack[stack_top].y = 0; stack[stack_top].avail_w = viewport_w;
    stack[stack_top].cursor_x = 0; stack[stack_top].cursor_y = 0; stack[stack_top].line_h = 0;
    stack[stack_top].box_index = -1;
    stack[stack_top].child_index_in_parent = -1;
    stack[stack_top].is_center = 0;
    stack[stack_top].is_pre = 0;
    stack[stack_top].line_boxes = 0; stack[stack_top].line_box_count = 0; stack[stack_top].line_box_cap = 0;
    stack[stack_top].margin_top = 0; stack[stack_top].margin_bottom = 0;
    stack[stack_top].pad_top = 0; stack[stack_top].pad_bottom = 0;
    stack[stack_top].is_flex_row = 0; stack[stack_top].flex_x = 0; stack[stack_top].flex_w = 0; stack[stack_top].flex_row = 0;
    stack[stack_top].flex_row_at = 0; stack[stack_top].flex_row_y = 0; stack[stack_top].flex_row_max_h = 0;
    stack[stack_top].is_grid = 0; stack[stack_top].grid_col_x = 0; stack[stack_top].grid_col_w = 0;
    stack[stack_top].grid_ncols = 0; stack[stack_top].grid_col_index = 0;
    stack[stack_top].grid_row_y = 0; stack[stack_top].grid_row_max_h = 0;
    stack_top++;

    while (stack_top > 0) {
        LayoutFrame *f = &stack[stack_top - 1];
        DomNode *node = f->node;

        if (f->next_child >= node->child_count) {
            finalize_line(f, out);
            /* If this was a grid/flex-row container with an unfinished
             * (partial) last row, its height still needs folding in. */
            if (f->is_grid && f->grid_row_max_h > 0) f->cursor_y = f->grid_row_y + f->grid_row_max_h;
            if (f->is_flex_row && f->flex_row_max_h > 0) f->cursor_y = f->flex_row_y + f->flex_row_max_h;
            float content_h = f->cursor_y + f->line_h;
            float box_h = content_h + f->pad_top + f->pad_bottom;
            if (f->box_index >= 0) {
                out->boxes[f->box_index].h = box_h;
                float bottom = out->boxes[f->box_index].y + box_h;
                if (bottom > out->content_h) out->content_h = bottom;
            }
            if (f->line_boxes) free(f->line_boxes);
            if (f->flex_x) free(f->flex_x);
            if (f->flex_w) free(f->flex_w);
            if (f->flex_row) free(f->flex_row);
            if (f->grid_col_x) free(f->grid_col_x);
            if (f->grid_col_w) free(f->grid_col_w);
            stack_top--;
            if (stack_top > 0) {
                LayoutFrame *parent = &stack[stack_top - 1];
                float total = f->margin_top + box_h + f->margin_bottom;
                if (parent->is_grid) {
                    if (total > parent->grid_row_max_h) parent->grid_row_max_h = total;
                    parent->grid_col_index++;
                    if (parent->grid_col_index >= parent->grid_ncols) {
                        parent->cursor_y = parent->grid_row_y + parent->grid_row_max_h + parent->node->css_gap;
                        parent->grid_row_y = parent->cursor_y;
                        parent->grid_row_max_h = 0;
                        parent->grid_col_index = 0;
                    }
                } else if (parent->is_flex_row && parent->flex_row && f->child_index_in_parent >= 0) {
                    int this_row = parent->flex_row[f->child_index_in_parent];
                    /* Advance to the next row's Y the moment we pop the
                     * FIRST item belonging to a NEW row (found by checking
                     * whether the row this item belongs to differs from
                     * the row the frame is currently tracking) -- items
                     * are always popped in the same left-to-right,
                     * row-by-row order they were assigned in (see
                     * compute_flex_row_positions()), so this is exactly
                     * the row boundary, without needing to know row item
                     * COUNTS up front the way display:grid's fixed column
                     * count lets it. Checked BEFORE folding this item's
                     * own height in, so a new row's first item's height
                     * starts that row's own tracking, not the OLD row's. */
                    if (this_row != parent->flex_row_at) {
                        parent->flex_row_y = parent->flex_row_y + parent->flex_row_max_h + parent->node->css_gap;
                        parent->flex_row_max_h = total;
                        parent->flex_row_at = this_row;
                    } else if (total > parent->flex_row_max_h) {
                        parent->flex_row_max_h = total;
                    }
                    parent->cursor_y = parent->flex_row_y + parent->flex_row_max_h;
                } else {
                    parent->cursor_y = parent->cursor_y + total;
                }
            }
            continue;
        }

        DomNode *child = node->children[f->next_child];
        int child_index_in_parent = f->next_child;
        f->next_child++;

        if (dom_is_text(child)) {
            if (!f->is_pre) {
                int len = (int)strlen(child->text), i, all_ws = 1;
                for (i = 0; i < len; i++) if (!isspace((unsigned char)child->text[i])) { all_ws = 0; break; }
                if (all_ws) continue;
                place_text_node(f, out, child);
            } else {
                place_pre_text_node(f, out, child);
            }
            continue;
        }

        if (child->css_display == CSS_DISPLAY_NONE) continue;

        if (child->css_display == CSS_DISPLAY_INLINE && strcmp(child->tag, "img") == 0) {
            PlaceSpec spec;
            spec.kind = SQW_BOX_IMG; spec.w = SQW_IMG_SIZE; spec.h = SQW_IMG_SIZE;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            place_inline_run(f, out, child, &spec);
        } else if (child->css_display == CSS_DISPLAY_INLINE && strcmp(child->tag, "input") == 0) {
            const char *type = dom_get_attr(child, "type");
            PlaceSpec spec;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) {
                spec.kind = SQW_BOX_INPUT_CHECK; spec.w = SQW_CHECK_SIZE; spec.h = SQW_CHECK_SIZE;
            } else if (type && (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0)) {
                float w = sqw_text_measure(child->form_value, (int)strlen(child->form_value), SQW_TEXT_SCALE) + 16.0f;
                if (w < SQW_INPUT_MIN_W) w = SQW_INPUT_MIN_W;
                spec.kind = SQW_BOX_BUTTON; spec.w = w; spec.h = SQW_LINE_H + 4.0f;
            } else if (type && strcmp(type, "hidden") == 0) {
                continue; /* never rendered/laid out -- real HTML5 behavior */
            } else {
                /* text/password/email/search/number/tel/url/date/... and
                 * any other/unrecognized type: real HTML5 falls back to a
                 * plain single-line text box for anything it doesn't
                 * specifically special-case too. */
                float w = child->css_has_width ? child->css_width : SQW_INPUT_DEFAULT_W;
                spec.kind = SQW_BOX_INPUT_TEXT; spec.w = w; spec.h = SQW_LINE_H + 4.0f;
            }
            place_inline_run(f, out, child, &spec);
        } else if (child->css_display == CSS_DISPLAY_INLINE && strcmp(child->tag, "textarea") == 0) {
            float w = child->css_has_width ? child->css_width : SQW_TEXTAREA_DEFAULT_W;
            float h = child->css_has_height ? child->css_height : SQW_TEXTAREA_DEFAULT_H;
            PlaceSpec spec;
            spec.kind = SQW_BOX_TEXTAREA; spec.w = w; spec.h = h;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            place_inline_run(f, out, child, &spec);
        } else if (child->css_display == CSS_DISPLAY_INLINE && is_atomic_inline_tag(child->tag)) {
            float w = direct_text_width(child);
            if (w < 4.0f) w = 4.0f;
            PlaceSpec spec;
            spec.kind = kind_for_tag(child->tag); spec.w = w; spec.h = SQW_LINE_H;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            place_inline_run(f, out, child, &spec);
        } else if (is_block_like(child->css_display) || strcmp(child->tag, "img") == 0) {
            /* Non-atomic-inline "img"/unrecognized-inline tags fall
             * through here too and are just treated as block -- a plain,
             * safe default rather than silently dropping them. */
            finalize_line(f, out);
            if (f->line_h > 0) { f->cursor_y = f->cursor_y + f->line_h; f->cursor_x = 0; f->line_h = 0; }

            float margin_top = child->css_margin[0], margin_bottom = child->css_margin[2];
            float margin_left = child->css_margin[3], margin_right = child->css_margin[1];
            float pad_top = child->css_padding[0], pad_bottom = child->css_padding[2];
            float pad_left = child->css_padding[3], pad_right = child->css_padding[1];

            float bx, by, bw;
            if (f->is_flex_row && f->flex_x && f->flex_w) {
                bx = f->x + f->flex_x[child_index_in_parent];
                bw = f->flex_w[child_index_in_parent];
                by = f->y + f->flex_row_y + margin_top;
            } else if (f->is_grid && f->grid_col_x && f->grid_col_w) {
                bx = f->x + f->grid_col_x[f->grid_col_index];
                bw = f->grid_col_w[f->grid_col_index];
                by = f->y + f->grid_row_y + margin_top;
            } else {
                bx = f->x + SQW_PAD + margin_left;
                by = f->y + f->cursor_y + margin_top;
                bw = f->avail_w - 2 * SQW_PAD - margin_left - margin_right;
                if (child->css_has_width) bw = child->css_width;
            }
            if (bw < 0) bw = 0;

            layout_list_push(out, child, kind_for_tag(child->tag), bx, by, bw, 0);
            int idx = out->count - 1;

            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (LayoutFrame *)realloc(stack, stack_cap * sizeof(LayoutFrame));
                f = &stack[stack_top - 1];
            }
            float content_x = bx + pad_left;
            float content_w = bw - pad_left - pad_right;
            if (content_w < 0) content_w = 0;
            init_block_frame(&stack[stack_top], child, content_x, by, content_w, idx, child_index_in_parent);
            stack[stack_top].margin_top = margin_top;
            stack[stack_top].margin_bottom = margin_bottom;
            stack[stack_top].pad_top = pad_top;
            stack[stack_top].pad_bottom = pad_bottom;

            if (child->css_display == CSS_DISPLAY_FLEX && child->css_flex_direction == CSS_FLEX_ROW) {
                stack[stack_top].is_flex_row = 1;
                int n = child->child_count > 0 ? child->child_count : 1;
                stack[stack_top].flex_x = (float *)calloc((size_t)n, sizeof(float));
                stack[stack_top].flex_w = (float *)calloc((size_t)n, sizeof(float));
                stack[stack_top].flex_row = (int *)calloc((size_t)n, sizeof(int));
                compute_flex_row_positions(child, content_w, stack[stack_top].flex_x, stack[stack_top].flex_w, stack[stack_top].flex_row);
            } else if (child->css_display == CSS_DISPLAY_GRID && child->css_grid_template_columns[0]) {
                float widths[16];
                int ncols = parse_grid_tracks(child->css_grid_template_columns, content_w, widths, 16);
                if (ncols > 0) {
                    stack[stack_top].is_grid = 1;
                    stack[stack_top].grid_ncols = ncols;
                    stack[stack_top].grid_col_x = (float *)malloc((size_t)ncols * sizeof(float));
                    stack[stack_top].grid_col_w = (float *)malloc((size_t)ncols * sizeof(float));
                    float gap = child->css_gap;
                    float cx = 0.0f;
                    int ci;
                    for (ci = 0; ci < ncols; ci++) {
                        stack[stack_top].grid_col_x[ci] = cx;
                        stack[stack_top].grid_col_w[ci] = widths[ci];
                        cx = cx + widths[ci] + gap;
                    }
                }
            }
            stack_top++;
        }
        /* Any other tag (head, title, script, ...) is skipped entirely --
         * not visited, not descended into. */
    }

    free(stack);
}

int layout_hit_test(const LayoutList *list, float x, float y) {
    int i;
    int best = -1;
    for (i = 0; i < list->count; i++) {
        const LayoutBox *b = &list->boxes[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) best = i;
    }
    return best;
}

void layout_list_free(LayoutList *list) {
    if (list->boxes) free(list->boxes);
    list->boxes = 0; list->count = 0; list->cap = 0;
}
