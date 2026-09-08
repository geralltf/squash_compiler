#include "layout.h"
#include "text_metrics.h"
#include "css.h"
#include "html_lexer.h"
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
/* Real CSS Grid "grid-template-areas" support -- generous, not tight,
 * caps: real Wikipedia's own sidebar grid is 3 rows x 2 cols, and no
 * other real page this project has been tested against comes close to
 * these limits. See LayoutFrame::area_grid's own comment. */
#define SQW_GRID_AREA_MAX_ROWS 8
#define SQW_GRID_AREA_MAX_COLS 8
#define SQW_GRID_AREA_NAME_LEN 32
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

/* See layout.h's own comment on why this is a plain function pointer
 * rather than an #include of image_cache.h. */
static int (*g_image_size_lookup)(const char *url, float *w, float *h) = NULL;

void layout_set_image_size_lookup(int (*fn)(const char *url, float *w, float *h)) {
    g_image_size_lookup = fn;
}

/* Real CSS box-model + display support (block/inline/inline-block/none/
 * flex/grid), driven entirely by each DomNode's own computed style (see
 * css.h/css.c -- css_apply() resolves that BEFORE layout_compute() ever
 * runs, from real parsed CSS + inline style="", not a hardcoded per-tag
 * table). Two real, if intentionally scoped, layout algorithms beyond
 * plain block/inline flow:
 *   flex-direction:row    -- a genuine up-front pass distributes each
 *                             item's width along the main axis: explicit
 *                             widths are honored; any item with a real
 *                             flex-grow (see DomNode.css_flex_grow's own
 *                             comment) absorbs the row's own leftover
 *                             space proportionally to its grow factor
 *                             BEFORE justify-content ever sees any of
 *                             that space, matching real CSS's own
 *                             priority; only once nothing in a row has
 *                             flex-grow does justify-content distribute
 *                             the leftover space as inter-item spacing
 *                             instead (its original, still-real
 *                             behavior). flex-shrink/flex-basis are NOT
 *                             modeled (an item never shrinks below its
 *                             own natural/shrink-to-fit width, and
 *                             "basis" is always treated as auto/content-
 *                             based, never a real flex-basis: Npx/%
 *                             value). align-items is NOT implemented (every
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

    /* Real per-element text metrics, resolved once at push time from this
     * frame's own node (css_font_size/css_text_align/css_line_height,
     * already resolved by css_apply() before layout ever runs) --
     * text_scale is what SQW_TEXT_SCALE used to be everywhere in this
     * file (a fixed constant); normal_line_h is what SQW_LINE_H used to
     * be. See LayoutBox::text_scale's own comment for why the exact same
     * value has to reach the draw pass too. */
    float text_scale;
    float normal_line_h;
    int text_align; /* CssDisplay-adjacent: 0=left, 1=center, 2=right (see dom.h) */

    /* Box-model bookkeeping for THIS frame's own box (applied when the
     * frame is popped -- see layout_compute()'s own pop-time comment).
     * All zero for the synthetic root and for any element with no
     * explicit CSS margin/padding, which exactly reproduces this
     * project's original (pre-CSS) fixed-SQW_PAD behavior. */
    float margin_top, margin_bottom, pad_top, pad_bottom, border_top, border_bottom;

    /* This frame's own effective z-order key -- see LayoutBox::z_key's
     * own comment. Inherited from the parent frame at push time unless
     * this frame's own node has an explicit CSS z-index, so a whole
     * positioned subtree shares one key. */
    int effective_z;

    /* This frame's own effective overflow:hidden clip rect (content-
     * space, same as every other x/y/w/h in this file) -- see
     * LayoutBox::has_clip's own comment. Inherited from the parent frame
     * unless this frame's own node has css_overflow_hidden, in which case
     * it becomes this frame's own box rect (so every descendant -- not
     * just this element's own box -- gets clipped to it). */
    int has_clip; float clip_x, clip_y, clip_w, clip_h;

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

    /* Real CSS Grid "grid-template-areas" support -- see dom.h's own
     * comment on css_grid_template_areas/css_grid_area for why this
     * exists (it's what actually places Wikipedia's own sidebar beside
     * its article content). Parsed once at push time (parse_grid_areas())
     * into area_grid, a [row][col] table of area-name strings; a child
     * whose own css_grid_area matches one of these names is positioned
     * directly at that name's row/column span instead of going through
     * the ordinary sequential auto-placement grid_col_index logic above
     * (both can coexist on the same grid container: a child with no
     * matching area name, or none at all, still falls back to sequential
     * placement -- see the per-child push-site comment). area_row_at
     * mirrors flex_row_at's own role (which named-area ROW the running
     * grid_row_y/grid_row_max_h are currently accumulating for), -1
     * meaning "no row started yet" so the very first item doesn't
     * spuriously advance grid_row_y before anything has been placed. */
    int has_named_areas;
    /* Flattened (squash's own C parser doesn't accept a true
     * multi-dimensional array field/parameter type -- confirmed via a
     * direct compile attempt) row-major [row][col][name char] buffer;
     * see area_cell() for the row/col -> flat-offset math every access
     * goes through instead of real [][] indexing. */
    char area_grid[SQW_GRID_AREA_MAX_ROWS * SQW_GRID_AREA_MAX_COLS * SQW_GRID_AREA_NAME_LEN];
    int area_nrows, area_ncols;
    int area_row_at;
    /* THIS frame's own named-area row (set once at push time from the
     * PARENT's area_grid lookup, read back by the PARENT at pop time to
     * decide whether to advance grid_row_y -- see area_row_at above).
     * -1 when this frame's own element didn't match a named area (either
     * it has no css_grid_area, the parent has no named areas at all, or
     * the name didn't match anything in area_grid), meaning this child
     * was placed via ordinary sequential grid auto-placement instead. */
    int area_row;
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
static float node_text_scale(const DomNode *node) {
    return SQW_TEXT_SCALE * (node->css_font_size / 16.0f);
}

static float node_normal_line_h(const DomNode *node, float scale) {
    /* Copy to a local before comparing -- see css.c's own top-of-file
     * comment (and php_mini.c's, which first documented it) on the real
     * squash codegen bug where comparing a struct field read through "->"
     * DIRECTLY is unreliable; only comparing a plain local is safe. */
    float lh = node->css_line_height;
    return lh > 0.0f ? lh : (SQW_FONT_CELL_H * scale);
}

static float direct_text_width(const DomNode *node, float scale) {
    int i; float w = 0.0f;
    for (i = 0; i < node->child_count; i++) {
        DomNode *c = node->children[i];
        if (dom_is_text(c)) w += sqw_text_measure(c->text, (int)strlen(c->text), scale);
        /* Recurse into a nested ELEMENT child too, not just direct text --
         * real-world markup very commonly wraps a link/button's actual
         * label in an inner <span> (confirmed via real Wikipedia HTML:
         * every sidebar/nav link is "<a><span>Main page</span></a>", not
         * a direct text child of <a> at all) -- without this, every such
         * label measured as width 0, and concat_direct_text() (sqw_main.c)
         * found no text either, so the link rendered as an empty box with
         * no visible label whatsoever. Excludes "script"/"style"/
         * "textarea" (html_tag_is_raw_text()) explicitly -- a real,
         * confirmed regression this exact recursion caused: real
         * Wikipedia markup wraps a templatestyles <style> block's raw CSS
         * text directly inside an otherwise-empty <span
         * class="mw-empty-elt">, and unconditional recursion happily
         * "measured" that CSS text as if it were the span's own visible
         * label, which then rendered as literal CSS text on the page (a
         * MUCH worse bug than the width-0 problem this recursion was
         * written to fix). Every other element child is still safe to
         * descend into unconditionally: real HTML rarely puts any other
         * kind of raw-text/genuine-block content inside an atomic-inline
         * tag. */
        else if (!html_tag_is_raw_text(c->tag)) w += direct_text_width(c, scale);
    }
    return w;
}

static void layout_list_push_full(LayoutList *out, DomNode *node, SqwBoxKind kind,
                                   float x, float y, float w, float h, int text_start, int text_len, int z,
                                   int has_clip, float clip_x, float clip_y, float clip_w, float clip_h) {
    if (out->count >= out->cap) {
        out->cap = out->cap ? out->cap * 2 : 32;
        out->boxes = (LayoutBox *)realloc(out->boxes, out->cap * sizeof(LayoutBox));
    }
    LayoutBox *b = &out->boxes[out->count];
    b->node = node; b->kind = kind; b->x = x; b->y = y; b->w = w; b->h = h;
    b->text_start = text_start; b->text_len = text_len;
    b->z_key = z;
    b->has_clip = has_clip; b->clip_x = clip_x; b->clip_y = clip_y; b->clip_w = clip_w; b->clip_h = clip_h;
    out->count++;
    if (x + w > out->content_w) out->content_w = x + w;
    if (y + h > out->content_h) out->content_h = y + h;
}

static void layout_list_push(LayoutList *out, DomNode *node, SqwBoxKind kind, float x, float y, float w, float h, int z,
                              int has_clip, float clip_x, float clip_y, float clip_w, float clip_h) {
    layout_list_push_full(out, node, kind, x, y, w, h, 0, 0, z, has_clip, clip_x, clip_y, clip_w, clip_h);
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
    nf->border_top = 0; nf->border_bottom = 0;
    nf->is_flex_row = 0; nf->flex_x = 0; nf->flex_w = 0; nf->flex_row = 0;
    nf->flex_row_at = 0; nf->flex_row_y = 0; nf->flex_row_max_h = 0;
    nf->is_grid = 0; nf->grid_col_x = 0; nf->grid_col_w = 0;
    nf->grid_ncols = 0; nf->grid_col_index = 0; nf->grid_row_y = 0; nf->grid_row_max_h = 0;
    nf->has_named_areas = 0; nf->area_nrows = 0; nf->area_ncols = 0; nf->area_row_at = -1;
    nf->area_row = -1; /* overwritten right after this call if the parent matched a named area for this child */
    nf->text_scale = node_text_scale(child);
    nf->normal_line_h = node_normal_line_h(child, nf->text_scale);
    nf->text_align = child->css_text_align;
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
    /* <center> forces centering regardless of text-align (real HTML5:
     * <center> behaves like "text-align: center" plus more, but centering
     * text is the one part this layout engine implements); otherwise the
     * frame's own (real, inherited) css_text_align decides. */
    int want_center = f->is_center || f->text_align == 1;
    int want_right = !f->is_center && f->text_align == 2;
    if ((want_center || want_right) && f->line_box_count > 0) {
        /* Measured from the first box's left edge to the last box's right
         * edge (not f->cursor_x, which still includes one trailing
         * inter-box gap after the last box -- a word-run gap and an
         * element gap now differ, see place_inline_run, so there's no
         * single fixed amount left to just subtract off cursor_x). */
        LayoutBox *lastb = &out->boxes[f->line_boxes[f->line_box_count - 1]];
        LayoutBox *firstb = &out->boxes[f->line_boxes[0]];
        float used_w = (lastb->x + lastb->w) - firstb->x;
        float shift = want_center ? (f->avail_w - used_w) / 2.0f : (f->avail_w - used_w);
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
    float text_scale; /* see LayoutBox::text_scale's own comment */
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
    layout_list_push_full(out, node, spec->kind, f->x + f->cursor_x, f->y + f->cursor_y, w, h, spec->text_start, spec->text_len, f->effective_z,
                           f->has_clip, f->clip_x, f->clip_y, f->clip_w, f->clip_h);
    out->boxes[out->count - 1].text_scale = spec->text_scale;
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
    float space_w = sqw_text_glyph_advance(' ', f->text_scale);
    int wi;
    for (wi = 0; wi < nwords; wi++) {
        int start = word_start[wi];
        int wlen = word_len[wi];
        float w = sqw_text_measure(s + start, wlen, f->text_scale);
        PlaceSpec spec;
        spec.kind = SQW_BOX_TEXT; spec.w = w; spec.h = f->normal_line_h;
        spec.text_start = start; spec.text_len = wlen; spec.gap = space_w;
        spec.text_scale = f->text_scale;
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
            float w = sqw_text_measure(s + start, llen, f->text_scale);
            layout_list_push_full(out, node, SQW_BOX_TEXT, f->x + f->cursor_x, f->y + f->cursor_y, w, f->normal_line_h, start, llen, f->effective_z,
                                   f->has_clip, f->clip_x, f->clip_y, f->clip_w, f->clip_h);
            out->boxes[out->count - 1].text_scale = f->text_scale;
        }
        f->cursor_y = f->cursor_y + f->normal_line_h;
        f->cursor_x = 0;
        if (i >= len) break;
        i++;
    }
    f->line_h = 0;
}

/* ---- grid track parsing ---- */

/* Row-major flat-offset math for LayoutFrame::area_grid -- see that
 * field's own comment for why it's a flattened 1D buffer instead of a
 * real char[][][] (squash's own C parser rejects a true multi-dimensional
 * array field/parameter type). */
static char *area_cell(char *grid, int row, int col) {
    return grid + (row * SQW_GRID_AREA_MAX_COLS + col) * SQW_GRID_AREA_NAME_LEN;
}

/* Parses a "grid-template-areas" value -- one or more quoted row strings,
 * e.g. "'siteNotice siteNotice' 'columnStart pageContent' 'footer
 * footer'" (real Wikipedia CSS, confirmed via its own fetched
 * stylesheet) -- into `grid` (see area_cell() for how it's indexed), a
 * [row][col] table of area-name tokens. Every row must have the same
 * column count in real CSS; this doesn't enforce that (a short/ragged row
 * just leaves its missing columns as empty strings, matched by nothing,
 * same safe-degrade convention as everywhere else) -- `*out_cols` is set
 * from the FIRST row's own token count. Returns the row count (0 if
 * `value` has no quoted rows at all, e.g. empty/unset). */
static int parse_grid_areas(const char *value, char *grid, int *out_cols) {
    int nrows = 0;
    int ncols = -1;
    const char *p = value;
    while (*p && nrows < SQW_GRID_AREA_MAX_ROWS) {
        while (*p && *p != '\'' && *p != '"') p++;
        if (!*p) break;
        char q = *p; p++;
        const char *row_start = p;
        while (*p && *p != q) p++;
        int row_len = (int)(p - row_start);
        if (*p == q) p++;
        char rowbuf[256];
        int rl = row_len < (int)sizeof(rowbuf) - 1 ? row_len : (int)sizeof(rowbuf) - 1;
        memcpy(rowbuf, row_start, (size_t)rl);
        rowbuf[rl] = 0;
        int col = 0;
        char *tok = strtok(rowbuf, " \t");
        while (tok && col < SQW_GRID_AREA_MAX_COLS) {
            char *cell = area_cell(grid, nrows, col);
            strncpy(cell, tok, SQW_GRID_AREA_NAME_LEN - 1);
            cell[SQW_GRID_AREA_NAME_LEN - 1] = 0;
            col++;
            tok = strtok(0, " \t");
        }
        while (col < SQW_GRID_AREA_MAX_COLS) { area_cell(grid, nrows, col)[0] = 0; col++; }
        if (ncols < 0) ncols = col;
        nrows++;
    }
    *out_cols = ncols > 0 ? ncols : 0;
    return nrows;
}

/* Looks up `name` in an already-parsed area_grid, returning its
 * bounding row/col span (a named area is always a contiguous rectangle
 * in valid CSS; this just takes the min/max row and col it appears at,
 * which is exactly that rectangle for well-formed input and a reasonable
 * best-effort for anything else). Returns 0 (r0 left at -1) if `name`
 * doesn't appear anywhere in the grid. */
static int find_grid_area(char *grid, int nrows, int ncols, const char *name,
                           int *r0, int *r1, int *c0, int *c1) {
    *r0 = -1; *r1 = -1; *c0 = -1; *c1 = -1;
    int ri, ci;
    for (ri = 0; ri < nrows; ri++) {
        for (ci = 0; ci < ncols; ci++) {
            char *cell = area_cell(grid, ri, ci);
            if (cell[0] && !strcmp(cell, name)) {
                if (*r0 < 0 || ri < *r0) *r0 = ri;
                if (ri > *r1) *r1 = ri;
                if (*c0 < 0 || ci < *c0) *c0 = ci;
                if (ci > *c1) *c1 = ci;
            }
        }
    }
    return *r0 >= 0;
}

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
        /* "minmax(min, max)" -- real CSS negotiates between the two
         * bounds against available space; this engine just takes the MAX
         * argument as the track's own spec (fr or px, resolved the same
         * as any other track below) and ignores the min, a real
         * simplification but the common real-world case ("minmax(0,
         * 1fr)", confirmed via real Wikipedia CSS, means "at least 0,
         * grow to share remaining space" -- taking just the 1fr already
         * gets that right). */
        char resolved[64];
        const char *use = tok;
        if (!strncmp(tok, "minmax(", 7)) {
            const char *comma = strchr(tok + 7, ',');
            if (comma) {
                comma++;
                while (*comma == ' ') comma++;
                const char *close = strchr(comma, ')');
                int len = close ? (int)(close - comma) : (int)strlen(comma);
                if (len > 0 && len < (int)sizeof resolved) {
                    memcpy(resolved, comma, (size_t)len);
                    resolved[len] = 0;
                    use = resolved;
                }
            }
        }
        int tl = (int)strlen(use);
        if (tl > 2 && strcmp(use + tl - 2, "fr") == 0) {
            track_fr[ntracks] = (float)atof(use);
            is_fr[ntracks] = 1;
        } else if (tl > 3 && strcmp(use + tl - 3, "rem") == 0) {
            /* Real Wikipedia CSS specifies its own sidebar column width
             * in rem ("12.25rem") -- see css_parse_len()'s own comment
             * on why rem (fixed 16px root) is resolved but em isn't. */
            track_px[ntracks] = (float)atof(use) * 16.0f;
            is_fr[ntracks] = 0;
        } else {
            track_px[ntracks] = (float)atof(use);
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
            float tw = direct_text_width(c, node_text_scale(c));
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
        float grow_sum = 0.0f;
        for (i = 0; i < cnt; i++) {
            if (row[i] != r) continue;
            total_w += w[i]; row_cnt++;
            DomNode *c = node->children[idx[i]];
            if (c->css_flex_grow > 0.0f) grow_sum += c->css_flex_grow;
        }
        float used = total_w + gap * (float)(row_cnt > 1 ? row_cnt - 1 : 0);
        float extra = avail_w - used;
        if (extra < 0.0f) extra = 0.0f;
        float start_x = 0.0f, spacing_extra = 0.0f;
        /* Real CSS flex-grow consumes the row's own leftover space FIRST,
         * before justify-content ever sees any -- an item that grows
         * fills the gap itself rather than leaving it to be distributed
         * as inter-item spacing. Only reached when at least one item in
         * THIS row actually has flex-grow > 0 (see grow_sum above); a
         * container with no flex-grow anywhere falls straight through to
         * the existing justify-content logic below, unchanged. */
        if (extra > 0.0f && grow_sum > 0.0f) {
            for (i = 0; i < cnt; i++) {
                if (row[i] != r) continue;
                DomNode *c = node->children[idx[i]];
                if (c->css_flex_grow > 0.0f) w[i] += extra * (c->css_flex_grow / grow_sum);
            }
        } else if (node->css_justify == CSS_JUSTIFY_CENTER) start_x = extra / 2.0f;
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
    stack[stack_top].border_top = 0; stack[stack_top].border_bottom = 0;
    stack[stack_top].is_flex_row = 0; stack[stack_top].flex_x = 0; stack[stack_top].flex_w = 0; stack[stack_top].flex_row = 0;
    stack[stack_top].flex_row_at = 0; stack[stack_top].flex_row_y = 0; stack[stack_top].flex_row_max_h = 0;
    stack[stack_top].is_grid = 0; stack[stack_top].grid_col_x = 0; stack[stack_top].grid_col_w = 0;
    stack[stack_top].grid_ncols = 0; stack[stack_top].grid_col_index = 0;
    stack[stack_top].grid_row_y = 0; stack[stack_top].grid_row_max_h = 0;
    stack[stack_top].has_named_areas = 0; stack[stack_top].area_nrows = 0; stack[stack_top].area_ncols = 0;
    stack[stack_top].area_row_at = -1; stack[stack_top].area_row = -1;
    stack[stack_top].text_scale = node_text_scale(root);
    stack[stack_top].normal_line_h = node_normal_line_h(root, stack[stack_top].text_scale);
    stack[stack_top].text_align = root->css_text_align;
    stack[stack_top].effective_z = root->css_has_z_index ? root->css_z_index : 0;
    stack[stack_top].has_clip = 0;
    stack[stack_top].clip_x = 0; stack[stack_top].clip_y = 0; stack[stack_top].clip_w = 0; stack[stack_top].clip_h = 0;
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
            /* An explicit CSS "height" was previously ignored entirely
             * here -- box_h was ALWAYS just however tall the children
             * happened to make it. Rescued ONLY for an effectively EMPTY
             * box (content_h under a few px -- no real text/child content
             * ever got laid out) rather than for every element with an
             * explicit height, e.g. a decorative spacer div, a fixed-size
             * icon/background-image box with no text content, or a
             * placeholder awaiting JS-inserted content -- all previously
             * collapsed to 0/near-0 height regardless of what CSS said.
             * Confirmed via a real live-Wikipedia regression (found during
             * this same round of testing) that unconditionally honoring
             * css_height for EVERY element is actively harmful: real
             * pages routinely give a big explicit height to an element
             * that's meant to be constrained by a DIFFERENT mechanism this
             * engine doesn't implement (a collapsible ":checked" sibling-
             * selector menu, a position:fixed/sticky sidebar reserving
             * viewport-relative space) -- honoring that height blindly
             * turned Wikipedia's collapsed-by-default sidebar menu into a
             * dominant, page-breaking block of blue, even though the fix
             * was "more spec-correct" in isolation for the narrower empty-
             * box case it was written for. Re-checking content_h alone
             * wasn't a safe enough gate either -- a CONTAINER (child_count
             * > 0) can still measure a near-zero content_h for reasons
             * that have nothing to do with "this box is meant to be
             * empty" (nested flex/grid rows, a real squash/layout quirk in
             * how a particular child's own height folds upward, etc), so
             * the actual Wikipedia sidebar still regressed even gated on
             * content_h < 4px. Narrowed to the exact case this fix was
             * written for and nothing broader: a genuinely LEAF element
             * (child_count == 0, no children of any kind, text or
             * element) with an explicit height -- a background-image/
             * decorative spacer div is always exactly this shape; a real
             * navigation container never is. Every container element, no
             * matter how little visible content it ends up with, keeps
             * its old content-driven height unchanged, exactly as before
             * this fix existed. */
            float content_h_eff = (node->css_has_height && node->child_count == 0) ? node->css_height : content_h;
            float box_h = content_h_eff + f->pad_top + f->pad_bottom + f->border_top + f->border_bottom;
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
                /* Hoisted -- see the push-site's own comment (a few
                 * hundred lines down) on why "parent->node->css_gap" as a
                 * direct chained-arrow expression is a real, confirmed
                 * squash codegen bug, not a style preference. */
                DomNode *parent_node = parent->node;
                float parent_gap = parent_node->css_gap;
                float total = f->margin_top + box_h + f->margin_bottom;
                if (parent->is_grid && parent->has_named_areas && f->area_row >= 0) {
                    /* Named-area placement -- the row TRANSITION itself
                     * is detected at PUSH time now (see the push-site's
                     * own comment on why: pop-time detection, like
                     * sequential grid/flex-row both correctly use below,
                     * is too late here since DOM order doesn't drive
                     * named-area row membership). This pop-time half just
                     * folds this child's own height into the running max
                     * for whichever row it belongs to -- grid_col_index is
                     * never touched in this branch at all (no sequential
                     * column-wrapping applies to named placement). */
                    if (total > parent->grid_row_max_h) parent->grid_row_max_h = total;
                    parent->cursor_y = parent->grid_row_y + parent->grid_row_max_h;
                } else if (parent->is_grid) {
                    if (total > parent->grid_row_max_h) parent->grid_row_max_h = total;
                    parent->grid_col_index++;
                    if (parent->grid_col_index >= parent->grid_ncols) {
                        parent->cursor_y = parent->grid_row_y + parent->grid_row_max_h + parent_gap;
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
                        parent->flex_row_y = parent->flex_row_y + parent->flex_row_max_h + parent_gap;
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
            float iw = SQW_IMG_SIZE, ih = SQW_IMG_SIZE;
            float real_w, real_h;
            /* Real decoded size once known (see g_image_size_lookup's own
             * comment); still just the placeholder square while
             * PENDING/FAILED/unset, same as before real image support
             * existed. Explicit CSS/attribute width/height (already
             * resolved onto the node by css_apply(), see dom.h) always
             * wins over either -- real browsers' own precedence. */
            if (g_image_size_lookup && g_image_size_lookup(child->img_url, &real_w, &real_h)) {
                iw = real_w; ih = real_h;
            }
            if (child->css_has_width) iw = child->css_width;
            if (child->css_has_height) ih = child->css_height;
            spec.kind = SQW_BOX_IMG; spec.w = iw; spec.h = ih;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP; spec.text_scale = SQW_TEXT_SCALE;
            place_inline_run(f, out, child, &spec);
        } else if (child->css_display == CSS_DISPLAY_INLINE && strcmp(child->tag, "input") == 0) {
            const char *type = dom_get_attr(child, "type");
            PlaceSpec spec;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP; spec.text_scale = SQW_TEXT_SCALE;
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
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP; spec.text_scale = SQW_TEXT_SCALE;
            place_inline_run(f, out, child, &spec);
        } else if (child->css_display == CSS_DISPLAY_INLINE && is_atomic_inline_tag(child->tag)) {
            float cscale = node_text_scale(child);
            float w = direct_text_width(child, cscale);
            if (w < 4.0f) w = 4.0f;
            PlaceSpec spec;
            spec.kind = kind_for_tag(child->tag); spec.w = w; spec.h = node_normal_line_h(child, cscale);
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP; spec.text_scale = cscale;
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
            float border_top = child->css_border_width[0], border_bottom = child->css_border_width[2];
            float border_left = child->css_border_width[3], border_right = child->css_border_width[1];

            float bx, by, bw;
            int child_area_row = -1;
            /* Computed unconditionally (cheap: a small nested loop over
             * at most SQW_GRID_AREA_MAX_ROWS x _COLS) so the branch below
             * can be a plain condition on `area_found` rather than
             * calling find_grid_area() a second time -- see
             * LayoutFrame::area_row's own comment for what this feeds. */
            int area_r0 = -1, area_r1 = -1, area_c0 = -1, area_c1 = -1;
            int area_found = f->is_grid && f->has_named_areas && child->css_grid_area[0] &&
                find_grid_area(f->area_grid, f->area_nrows, f->area_ncols, child->css_grid_area,
                                &area_r0, &area_r1, &area_c0, &area_c1);
            /* Hoisted through a local plain pointer/value, NOT used as
             * "f->node->css_gap" inline further down -- a real, confirmed
             * squash codegen bug (see css_set_default_style()'s own
             * comment, css.c, and draw_layout_text()'s "textnode"/"owner"
             * comment, sqw_main.c, for the same bug elsewhere in this
             * project): a chained "->node->css_gap" double-dereference
             * reads back garbage on a struct this size, even though the
             * exact same field read through an ordinary local pointer
             * works correctly. Found here via direct testing: grid_col_w[]
             * itself printed correctly moments before the very expression
             * that used this chain, and the resulting `bw`/`grid_row_y`
             * came out as obvious garbage (billions, not the small real
             * gap value) immediately after -- textbook match for this
             * exact, already-documented bug shape. */
            DomNode *container_node = f->node;
            float container_gap = container_node->css_gap;
            if (f->is_flex_row && f->flex_x && f->flex_w) {
                bx = f->x + f->flex_x[child_index_in_parent];
                bw = f->flex_w[child_index_in_parent];
                by = f->y + f->flex_row_y + margin_top;
            } else if (area_found) {
                child_area_row = area_r0;
                /* Row-transition detected HERE, at push time, not at pop
                 * time -- a real bug found via direct testing: pop-time
                 * detection (matching flex_row's own comment, which is
                 * safe there ONLY because compute_flex_row_positions()
                 * precomputes every item's position up front) is too
                 * late for NAMED-area placement, since DOM order doesn't
                 * drive the row-wrap decision the way a fixed column
                 * count does for sequential grid: by the time a row's
                 * LAST-in-DOM-order item is popped and detects "this
                 * finished a row", the FIRST item of the actual next row
                 * has may already have been pushed and positioned using
                 * the stale, not-yet-advanced grid_row_y. Detecting the
                 * transition here instead -- before THIS child is
                 * positioned, comparing against the PREVIOUS child's own
                 * row -- guarantees grid_row_y is already correct for
                 * every child in a new row, including the first. */
                if (f->has_named_areas && child_area_row != f->area_row_at) {
                    if (f->area_row_at >= 0) {
                        f->grid_row_y = f->grid_row_y + f->grid_row_max_h + container_gap;
                    }
                    f->grid_row_max_h = 0.0f;
                    f->area_row_at = child_area_row;
                }
                bx = f->x + f->grid_col_x[area_c0];
                bw = 0.0f;
                int cc;
                for (cc = area_c0; cc <= area_c1 && cc < f->grid_ncols; cc++) bw += f->grid_col_w[cc];
                if (area_c1 > area_c0) bw += (float)(area_c1 - area_c0) * container_gap;
                by = f->y + f->grid_row_y + margin_top;
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

            /* See LayoutBox::z_key's own comment: an explicit z-index on
             * this child starts a new effective key for its whole
             * subtree; otherwise it inherits the enclosing frame's own
             * key, so a positioned container's children paint together
             * with it, not scattered back into plain DOM order. */
            int child_z = child->css_has_z_index ? child->css_z_index : f->effective_z;
            /* See LayoutBox::has_clip's own comment: an overflow:hidden
             * child starts a new clip rect (its own box) for its whole
             * subtree; otherwise it inherits the enclosing frame's own
             * clip rect unchanged (nested overflow:hidden containers just
             * keep the INNERMOST one here -- real CSS would intersect
             * both, a real but minor scope cut for the common case of at
             * most one clipping ancestor actually mattering visually).
             * Height uses the child's own explicit css_height when set
             * (known immediately); otherwise a large sentinel, since a
             * content-driven height isn't resolvable until this child's
             * own subtree finishes laying out (see this loop's own "push
             * before height is known" comment just below). */
            int child_has_clip; float child_clip_x, child_clip_y, child_clip_w, child_clip_h;
            if (child->css_overflow_hidden) {
                child_has_clip = 1;
                child_clip_x = bx; child_clip_y = by; child_clip_w = bw;
                child_clip_h = child->css_has_height ? child->css_height : 1000000.0f;
            } else {
                child_has_clip = f->has_clip;
                child_clip_x = f->clip_x; child_clip_y = f->clip_y; child_clip_w = f->clip_w; child_clip_h = f->clip_h;
            }
            layout_list_push(out, child, kind_for_tag(child->tag), bx, by, bw, 0, child_z,
                              child_has_clip, child_clip_x, child_clip_y, child_clip_w, child_clip_h);
            int idx = out->count - 1;

            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (LayoutFrame *)realloc(stack, stack_cap * sizeof(LayoutFrame));
                f = &stack[stack_top - 1];
            }
            float content_x = bx + border_left + pad_left;
            float content_w = bw - border_left - border_right - pad_left - pad_right;
            if (content_w < 0) content_w = 0;
            init_block_frame(&stack[stack_top], child, content_x, by, content_w, idx, child_index_in_parent);
            stack[stack_top].area_row = child_area_row;
            stack[stack_top].effective_z = child_z;
            stack[stack_top].has_clip = child_has_clip;
            stack[stack_top].clip_x = child_clip_x; stack[stack_top].clip_y = child_clip_y;
            stack[stack_top].clip_w = child_clip_w; stack[stack_top].clip_h = child_clip_h;
            stack[stack_top].margin_top = margin_top;
            stack[stack_top].margin_bottom = margin_bottom;
            stack[stack_top].pad_top = pad_top;
            stack[stack_top].pad_bottom = pad_bottom;
            stack[stack_top].border_top = border_top;
            stack[stack_top].border_bottom = border_bottom;

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
                    if (child->css_grid_template_areas[0]) {
                        int area_ncols = 0;
                        int area_nrows = parse_grid_areas(child->css_grid_template_areas, stack[stack_top].area_grid, &area_ncols);
                        if (area_nrows > 0 && area_ncols > 0) {
                            stack[stack_top].has_named_areas = 1;
                            stack[stack_top].area_nrows = area_nrows;
                            /* Clamped to the real resolved column COUNT
                             * (grid-template-columns's own track count) --
                             * a template-areas string naming more columns
                             * than grid-template-columns actually resolves
                             * is invalid CSS, but this keeps column
                             * lookups (area_c0/c1 index into grid_col_x/w,
                             * sized `ncols`) safely in bounds regardless. */
                            stack[stack_top].area_ncols = area_ncols < ncols ? area_ncols : ncols;
                        }
                    }
                }
            }
            stack_top++;
        }
        /* Any other tag (head, title, script, ...) is skipped entirely --
         * not visited, not descended into. */
    }

    free(stack);

    /* Real CSS z-index paint-order approximation -- see LayoutBox::z_key's
     * own comment for exactly what this does and doesn't model. A plain
     * STABLE insertion sort (not qsort, which makes no stability
     * guarantee) so boxes sharing the same key -- the overwhelming
     * majority on a page with few or no explicit z-indexes -- keep their
     * original relative (DOM/paint) order; only moves an element past
     * ones with a strictly different key. O(n^2) worst case, but a no-op
     * fast pass (already sorted) for the common all-zero-key case, and
     * this project's own box-count scale (SQW_RENDERER_MAX_BOXES == 512)
     * keeps even the worst case cheap. */
    {
        int i, j;
        for (i = 1; i < out->count; i++) {
            LayoutBox key = out->boxes[i];
            j = i - 1;
            while (j >= 0 && out->boxes[j].z_key > key.z_key) {
                out->boxes[j + 1] = out->boxes[j];
                j--;
            }
            out->boxes[j + 1] = key;
        }
    }
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
