#include "layout.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define SQW_LINE_H 22.0f
#define SQW_CHAR_W 8.0f
#define SQW_IMG_SIZE 64.0f
#define SQW_PAD 6.0f
#define SQW_INLINE_GAP 6.0f
#define SQW_MIN_TEXT_W 8.0f

typedef struct {
    DomNode *node;
    int next_child;
    float x, y;
    float avail_w;
    float cursor_x, cursor_y;
    float line_h;
    int box_index;   /* index into out->boxes for this frame's own box, -1 for the synthetic doc root */
    int is_center;
    int *line_boxes;
    int line_box_count, line_box_cap;
} LayoutFrame;

static int is_block_tag(const char *tag) {
    return strcmp(tag, "html") == 0 || strcmp(tag, "body") == 0 ||
           strcmp(tag, "div") == 0 || strcmp(tag, "p") == 0 || strcmp(tag, "center") == 0;
}

static SqwBoxKind kind_for_tag(const char *tag) {
    if (strcmp(tag, "div") == 0) return SQW_BOX_DIV;
    if (strcmp(tag, "center") == 0) return SQW_BOX_CENTER;
    if (strcmp(tag, "p") == 0) return SQW_BOX_P;
    if (strcmp(tag, "span") == 0) return SQW_BOX_SPAN;
    if (strcmp(tag, "a") == 0) return SQW_BOX_A;
    if (strcmp(tag, "img") == 0) return SQW_BOX_IMG;
    return SQW_BOX_OTHER;
}

/* Sum of the character counts of child's DIRECT text-node children (no
 * further descent -- real glyph layout is a later phase, see layout.h). */
static int direct_text_len(const DomNode *node) {
    int i, total = 0;
    for (i = 0; i < node->child_count; i++) {
        if (dom_is_text(node->children[i])) total += (int)strlen(node->children[i]->text);
    }
    return total;
}

static void layout_list_push(LayoutList *out, DomNode *node, SqwBoxKind kind, float x, float y, float w, float h) {
    if (out->count >= out->cap) {
        out->cap = out->cap ? out->cap * 2 : 32;
        out->boxes = (LayoutBox *)realloc(out->boxes, out->cap * sizeof(LayoutBox));
    }
    LayoutBox *b = &out->boxes[out->count];
    b->node = node; b->kind = kind; b->x = x; b->y = y; b->w = w; b->h = h;
    out->count++;
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
        float used_w = f->cursor_x - SQW_INLINE_GAP; /* drop the trailing gap */
        float shift = (f->avail_w - used_w) / 2.0f;
        if (shift > 0) {
            int i;
            for (i = 0; i < f->line_box_count; i++) out->boxes[f->line_boxes[i]].x += shift;
        }
    }
    f->line_box_count = 0;
}

static void place_inline(LayoutFrame *f, LayoutList *out, DomNode *node, SqwBoxKind kind, float w, float h) {
    /* NOTE: deliberately split out of a single
     * "if (f->cursor_x > 0 && f->cursor_x + w > f->avail_w)" condition --
     * that compound form (a float add nested inside a float comparison,
     * both inline in one if/&&) reliably mis-evaluated to true even when
     * the arithmetic was well within bounds (e.g. 54+40=94 vs. avail_w=964
     * still "wrapped") -- a real, currently-unresolved squash codegen bug
     * distinct from the float-struct-field-via-arrow bug fixed in
     * codegen.c's codegen_is_float_expr(). Splitting into separate float
     * locals and simple two-operand comparisons before the `if` sidesteps
     * it and is confirmed correct. */
    float sum = f->cursor_x + w;
    int has_content_on_line = f->cursor_x > 0;
    int would_overflow = sum > f->avail_w;
    if (has_content_on_line && would_overflow) {
        finalize_line(f, out);
        f->cursor_y += f->line_h;
        f->cursor_x = 0;
        f->line_h = 0;
    }
    layout_list_push(out, node, kind, f->x + f->cursor_x, f->y + f->cursor_y, w, h);
    frame_track_line_box(f, out->count - 1);
    f->cursor_x += w + SQW_INLINE_GAP;
    if (h > f->line_h) f->line_h = h;
}

void layout_compute(DomNode *root, float viewport_w, float viewport_h, LayoutList *out) {
    out->boxes = 0; out->count = 0; out->cap = 0;
    (void)viewport_h;

    int cap = 32;
    int top = 0;
    LayoutFrame *stack = (LayoutFrame *)malloc(cap * sizeof(LayoutFrame));

    stack[top].node = root;
    stack[top].next_child = 0;
    stack[top].x = 0; stack[top].y = 0; stack[top].avail_w = viewport_w;
    stack[top].cursor_x = 0; stack[top].cursor_y = 0; stack[top].line_h = 0;
    stack[top].box_index = -1;
    stack[top].is_center = 0;
    stack[top].line_boxes = 0; stack[top].line_box_count = 0; stack[top].line_box_cap = 0;
    top++;

    while (top > 0) {
        LayoutFrame *f = &stack[top - 1];
        DomNode *node = f->node;

        if (f->next_child >= node->child_count) {
            finalize_line(f, out);
            float content_h = f->cursor_y + f->line_h;
            if (f->box_index >= 0) out->boxes[f->box_index].h = content_h;
            if (f->line_boxes) free(f->line_boxes);
            top--;
            if (top > 0) {
                LayoutFrame *parent = &stack[top - 1];
                parent->cursor_y += content_h;
            }
            continue;
        }

        DomNode *child = node->children[f->next_child];
        f->next_child++;

        if (dom_is_text(child)) {
            int len = (int)strlen(child->text);
            /* Whitespace-only text nodes (formatting between tags) take no
             * visual space -- otherwise every "\n    " indent in the source
             * HTML would render as its own box. */
            int all_ws = 1, i;
            for (i = 0; i < len; i++) if (!isspace((unsigned char)child->text[i])) { all_ws = 0; break; }
            if (all_ws) continue;
            float w = len * SQW_CHAR_W;
            if (w < SQW_MIN_TEXT_W) w = SQW_MIN_TEXT_W;
            place_inline(f, out, child, SQW_BOX_OTHER, w, SQW_LINE_H);
        } else if (strcmp(child->tag, "img") == 0) {
            place_inline(f, out, child, SQW_BOX_IMG, SQW_IMG_SIZE, SQW_IMG_SIZE);
        } else if (strcmp(child->tag, "span") == 0 || strcmp(child->tag, "a") == 0) {
            int len = direct_text_len(child);
            float w = (len > 0 ? len : 4) * SQW_CHAR_W;
            place_inline(f, out, child, kind_for_tag(child->tag), w, SQW_LINE_H);
        } else if (is_block_tag(child->tag)) {
            finalize_line(f, out);
            if (f->line_h > 0) { f->cursor_y += f->line_h; f->cursor_x = 0; f->line_h = 0; }

            float bx = f->x + SQW_PAD;
            float by = f->y + f->cursor_y;
            float bw = f->avail_w - 2 * SQW_PAD;
            if (bw < 0) bw = 0;

            layout_list_push(out, child, kind_for_tag(child->tag), bx, by, bw, 0);
            int idx = out->count - 1;

            if (top >= cap) {
                cap *= 2;
                stack = (LayoutFrame *)realloc(stack, cap * sizeof(LayoutFrame));
                f = &stack[top - 1];
            }
            LayoutFrame *nf = &stack[top];
            nf->node = child; nf->next_child = 0;
            nf->x = bx; nf->y = by; nf->avail_w = bw;
            nf->cursor_x = 0; nf->cursor_y = 0; nf->line_h = 0;
            nf->box_index = idx;
            nf->is_center = (strcmp(child->tag, "center") == 0);
            nf->line_boxes = 0; nf->line_box_count = 0; nf->line_box_cap = 0;
            top++;
        }
        /* Any other tag (head, title, script, ...) is skipped entirely --
         * not visited, not descended into. */
    }

    free(stack);
}

void layout_list_free(LayoutList *list) {
    if (list->boxes) free(list->boxes);
    list->boxes = 0; list->count = 0; list->cap = 0;
}
