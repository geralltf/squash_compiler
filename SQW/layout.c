#include "layout.h"
#include "text_metrics.h"
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

typedef struct {
    DomNode *node;
    int next_child;
    float x, y;
    float avail_w;
    float cursor_x, cursor_y;
    float line_h;
    int box_index;   /* index into out->boxes for this frame's own box, -1 for the synthetic doc root */
    int is_center;
    int is_pre;
    int *line_boxes;
    int line_box_count, line_box_cap;
} LayoutFrame;

static int is_block_tag(const char *tag) {
    return strcmp(tag, "html") == 0 || strcmp(tag, "body") == 0 ||
           strcmp(tag, "div") == 0 || strcmp(tag, "p") == 0 || strcmp(tag, "center") == 0 ||
           strcmp(tag, "pre") == 0;
}

static SqwBoxKind kind_for_tag(const char *tag) {
    if (strcmp(tag, "div") == 0) return SQW_BOX_DIV;
    if (strcmp(tag, "center") == 0) return SQW_BOX_CENTER;
    if (strcmp(tag, "p") == 0) return SQW_BOX_P;
    if (strcmp(tag, "span") == 0) return SQW_BOX_SPAN;
    if (strcmp(tag, "a") == 0) return SQW_BOX_A;
    if (strcmp(tag, "img") == 0) return SQW_BOX_IMG;
    if (strcmp(tag, "button") == 0) return SQW_BOX_BUTTON;
    if (strcmp(tag, "pre") == 0) return SQW_BOX_PRE;
    return SQW_BOX_OTHER;
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
static void init_block_frame(LayoutFrame *nf, DomNode *child, float bx, float by, float bw, int idx) {
    nf->node = child; nf->next_child = 0;
    nf->x = bx; nf->y = by; nf->avail_w = bw;
    nf->cursor_x = 0; nf->cursor_y = 0; nf->line_h = 0;
    nf->box_index = idx;
    nf->is_center = (strcmp(child->tag, "center") == 0);
    nf->is_pre = (strcmp(child->tag, "pre") == 0);
    nf->line_boxes = 0; nf->line_box_count = 0; nf->line_box_cap = 0;
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
 * passing them as separate scalar parameters -- that function used to
 * take 9 plain arguments (f, out, node, kind, w, h, text_start, text_len,
 * gap) and, called in a tight per-word loop from place_text_node(),
 * produced silently wrong cursor_x accumulation for a subset of words
 * (confirmed by dumping real box positions: widths came out correct but
 * x advanced by the wrong amount starting a few words in, reproducing
 * only under squash, never under gcc on the identical source) -- the
 * same general "too many live scalars/parameters confuses squash's
 * codegen" class of bug already worked around once this session (see
 * init_block_frame's own comment in layout_compute). Passing one struct
 * pointer instead of 5 trailing scalars avoids it. */
typedef struct {
    SqwBoxKind kind;
    float w, h;
    int text_start, text_len;
    float gap;
} PlaceSpec;

/* Places one inline unit (element box or single word) at the current
 * cursor, wrapping to a new line first if it wouldn't fit -- spec->gap is
 * the horizontal space reserved AFTER this box before the next one on the
 * same line (SQW_INLINE_GAP between elements, a real space-glyph advance
 * between words so wrapped body text doesn't get extra-wide gaps). */
static void place_inline_run(LayoutFrame *f, LayoutList *out, DomNode *node, const PlaceSpec *spec) {
    /* NOTE: deliberately split out of a single
     * "if (f->cursor_x > 0 && f->cursor_x + w > f->avail_w)" condition --
     * that compound form (a float add nested inside a float comparison,
     * both inline in one if/&&) reliably mis-evaluated to true even when
     * the arithmetic was well within bounds -- a real squash codegen bug,
     * distinct from (and found before) the calling-convention/global-
     * assignment float bugs fixed in codegen.c this same session.
     * Splitting into separate float locals and simple two-operand
     * comparisons before the `if` sidesteps it and is confirmed correct. */
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

/* Splits one DOM text node into words at whitespace and places each as
 * its own SQW_BOX_TEXT run, wrapping normally -- real word-wrap, using
 * the baked font's actual glyph-advance metrics (text_metrics.h) rather
 * than the old flat "strlen*8px" guess. Consecutive whitespace collapses
 * to a single real space-glyph gap between words, matching normal HTML
 * text-flow whitespace handling. */
#define SQW_MAX_WORDS_PER_TEXT_NODE 512

/* Splits `s` (length len) into up to SQW_MAX_WORDS_PER_TEXT_NODE words at
 * whitespace, filling parallel out_start[]/out_len[] arrays. Pure,
 * side-effect-free (no LayoutFrame/LayoutList access at all) -- kept
 * strictly separate from placement (place_text_node_words() below) as a
 * workaround for a squash codegen bug: interleaving word-splitting,
 * sqw_text_measure() calls, and layout_list_push_full()/frame_track_line_
 * box() calls all in one loop produced silently wrong cursor_x
 * accumulation for a subset of words (box WIDTHS came out right, X
 * positions didn't, confirmed via a direct position dump; the identical
 * source under gcc is exactly correct) -- splitting into a pure
 * measurement pass and a separate, simpler placement-only pass (both
 * still hand-verified correct C, see place_text_node_words()'s own
 * comment) avoids whatever specific interleaving triggered it. Returns
 * the number of words found. */
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

/* <pre>: split only at source newlines (no word-wrap splitting), each
 * line becoming one box that is allowed to overflow the available width
 * -- the natural HTML5 vehicle for non-wrapping text (contrasted with
 * place_text_node()'s normal wrapping). Every line forces a real line
 * break after it, matching <pre>'s whitespace-preserving semantics
 * (source newlines are real line breaks, not collapsible whitespace). */
static void place_pre_text_node(LayoutFrame *f, LayoutList *out, DomNode *node) {
    const char *s = node->text;
    int len = (int)strlen(s);
    int i = 0;
    /* A single leading newline right after "<pre>" is conventionally
     * suppressed (matches every real browser) -- otherwise every <pre>
     * whose opening tag is followed by a real newline in the source (the
     * overwhelmingly common style) renders one extra blank line up top. */
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
        i++; /* skip the newline itself */
    }
    f->line_h = 0;
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
    stack[stack_top].is_center = 0;
    stack[stack_top].is_pre = 0;
    stack[stack_top].line_boxes = 0; stack[stack_top].line_box_count = 0; stack[stack_top].line_box_cap = 0;
    stack_top++;

    while (stack_top > 0) {
        LayoutFrame *f = &stack[stack_top - 1];
        DomNode *node = f->node;

        if (f->next_child >= node->child_count) {
            finalize_line(f, out);
            float content_h = f->cursor_y + f->line_h;
            if (f->box_index >= 0) {
                out->boxes[f->box_index].h = content_h;
                float bottom = out->boxes[f->box_index].y + content_h;
                if (bottom > out->content_h) out->content_h = bottom;
            }
            if (f->line_boxes) free(f->line_boxes);
            stack_top--;
            if (stack_top > 0) {
                LayoutFrame *parent = &stack[stack_top - 1];
                parent->cursor_y = parent->cursor_y + content_h;
            }
            continue;
        }

        DomNode *child = node->children[f->next_child];
        f->next_child++;

        if (dom_is_text(child)) {
            /* Whitespace-only text nodes (formatting between tags) take no
             * visual space -- otherwise every "\n    " indent in the source
             * HTML would render as its own box. Real (non-whitespace-only)
             * text inside <pre> keeps its whitespace verbatim, so this
             * all-whitespace short-circuit only applies outside <pre>. */
            if (!f->is_pre) {
                int len = (int)strlen(child->text), i, all_ws = 1;
                for (i = 0; i < len; i++) if (!isspace((unsigned char)child->text[i])) { all_ws = 0; break; }
                if (all_ws) continue;
                place_text_node(f, out, child);
            } else {
                place_pre_text_node(f, out, child);
            }
        } else if (strcmp(child->tag, "img") == 0) {
            PlaceSpec spec;
            spec.kind = SQW_BOX_IMG; spec.w = SQW_IMG_SIZE; spec.h = SQW_IMG_SIZE;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            place_inline_run(f, out, child, &spec);
        } else if (strcmp(child->tag, "span") == 0 || strcmp(child->tag, "a") == 0 || strcmp(child->tag, "button") == 0) {
            float w = direct_text_width(child);
            if (w < 4.0f) w = 4.0f;
            PlaceSpec spec;
            spec.kind = kind_for_tag(child->tag); spec.w = w; spec.h = SQW_LINE_H;
            spec.text_start = 0; spec.text_len = 0; spec.gap = SQW_INLINE_GAP;
            place_inline_run(f, out, child, &spec);
        } else if (is_block_tag(child->tag)) {
            finalize_line(f, out);
            if (f->line_h > 0) { f->cursor_y = f->cursor_y + f->line_h; f->cursor_x = 0; f->line_h = 0; }

            float bx = f->x + SQW_PAD;
            float by = f->y + f->cursor_y;
            float bw = f->avail_w - 2 * SQW_PAD;
            if (bw < 0) bw = 0;

            layout_list_push(out, child, kind_for_tag(child->tag), bx, by, bw, 0);
            int idx = out->count - 1;

            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (LayoutFrame *)realloc(stack, stack_cap * sizeof(LayoutFrame));
                f = &stack[stack_top - 1];
            }
            init_block_frame(&stack[stack_top], child, bx, by, bw, idx);
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
