#ifndef SQW_LAYOUT_H
#define SQW_LAYOUT_H
#include "dom.h"

/* Shared with sqw_main.c's draw pass, which must render each SQW_BOX_TEXT
 * run (and the A/SPAN/BUTTON label text drawn separately at draw time --
 * see layout.c's kind_for_tag comment) at the exact same scale this file
 * measured/wrapped it at, or wrapped text would visually overflow the
 * boxes it was wrapped into. */
#define SQW_TEXT_SCALE 0.45f

/* Placeholder colors are picked per-tag by the renderer (see renderer_vk.c);
 * this enum just labels which box is which so the renderer doesn't need to
 * re-inspect node->tag. */
typedef enum {
    SQW_BOX_DIV = 0,
    SQW_BOX_CENTER,
    SQW_BOX_P,
    SQW_BOX_SPAN,
    SQW_BOX_A,
    SQW_BOX_IMG,
    SQW_BOX_BUTTON,
    SQW_BOX_PRE,
    SQW_BOX_TEXT,    /* one word (normal flow) or one source line (inside <pre>) */
    /* <input type="text"/"password"/"email"/"search"/"number"/... (any
     * type not specifically listed below falls back to this -- a plain
     * single-line editable text box), and <textarea> (a multi-line one,
     * distinguished from SQW_BOX_INPUT_TEXT purely by node->tag at draw
     * time, both editable the same way). */
    SQW_BOX_INPUT_TEXT,
    SQW_BOX_TEXTAREA,
    /* <input type="checkbox"> / type="radio"> -- distinguished by
     * node's own "type" attribute at draw time (a checkbox draws a
     * square check mark, a radio a filled dot), both toggle
     * node->form_checked the same way. */
    SQW_BOX_INPUT_CHECK,
    SQW_BOX_OTHER
} SqwBoxKind;

typedef struct {
    DomNode *node;
    SqwBoxKind kind;
    float x, y, w, h;
    /* SQW_BOX_TEXT only: the run this box renders is
     * node->text[text_start .. text_start+text_len), NOT necessarily the
     * whole text node -- word-wrap and <pre> line-splitting both carve one
     * DOM text node into several of these. Unused (0) for every other
     * kind. */
    int text_start, text_len;
} LayoutBox;

typedef struct {
    LayoutBox *boxes;
    int count;
    int cap;
    /* Total laid-out content size, independent of viewport_w/h -- lets
     * sqw_main.c size scrollbars (content taller/wider than the viewport)
     * without re-walking every box. */
    float content_w, content_h;
} LayoutList;

/* Trivial top-down block layout, no CSS: div/p/center/html/body/pre are
 * full (parent) width and stack vertically; span/a/button/img/text are
 * inline, flowing left-to-right; center additionally centers each of its
 * own direct inline lines horizontally. Normal text is split into real
 * words and wrapped at the available width using the baked font's actual
 * glyph-advance metrics (text_metrics.h) -- <pre> text is instead split
 * only at source newlines, each line becoming one unwrapped (possibly
 * overflowing) SQW_BOX_TEXT box, HTML5's natural non-wrapping vehicle.
 * viewport_h is not a hard content cap (content can be taller -- see
 * out->content_h -- the caller applies scroll offset separately), only
 * the wrap width (viewport_w) matters here. Populates *out (caller must
 * layout_list_free() it). */
void layout_compute(DomNode *root, float viewport_w, float viewport_h, LayoutList *out);
void layout_list_free(LayoutList *list);

/* Optional hook for real <img> sizing: set once by sqw_main.c (to
 * SQW/image_cache.h's sqw_image_cache_get_size) right after the image
 * cache is initialized. `fn(url, &w, &h)` returns 1 and fills w/h with the
 * DECODED pixel size if known, 0 otherwise. Deliberately a plain function
 * pointer here rather than layout.c #include-ing image_cache.h directly:
 * that header pulls in vk_context.h (real Vulkan headers), and the
 * "test-layout" Makefile target builds layout.c standalone with no
 * Vulkan/X11 linkage at all -- leaving this NULL (its default) makes
 * layout_compute() fall back to the SQW_IMG_SIZE placeholder for every
 * <img>, exactly what it already did before real image support existed,
 * so that test target needs no changes. */
void layout_set_image_size_lookup(int (*fn)(const char *url, float *w, float *h));

/* Linear point-in-box hit test against the most recent layout_compute()
 * output, in CONTENT space (i.e. the caller must first add the current
 * scroll offset to the raw screen-space mouse position -- this function
 * knows nothing about scrolling). Returns the index of the topmost
 * (highest box_index, i.e. most-recently-pushed / most specific) box
 * whose rect contains (x,y), or -1 if none. Small linear scan -- fine at
 * this project's box-count scale (SQW_RENDERER_MAX_BOXES elsewhere caps
 * it well under a thousand), no spatial index needed. */
int layout_hit_test(const LayoutList *list, float x, float y);

#endif /* SQW_LAYOUT_H */
