#ifndef SQW_LAYOUT_H
#define SQW_LAYOUT_H
#include "dom.h"

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
    SQW_BOX_OTHER
} SqwBoxKind;

typedef struct {
    DomNode *node;
    SqwBoxKind kind;
    float x, y, w, h;
} LayoutBox;

typedef struct {
    LayoutBox *boxes;
    int count;
    int cap;
} LayoutList;

/* Trivial top-down block layout, no CSS: div/p/center/html/body are full
 * (parent) width and stack vertically; span/a/img/text are inline, flowing
 * left-to-right and wrapping to a new line when they'd overflow the
 * available width; center additionally centers each of its own direct
 * inline lines horizontally. Text nodes get a box sized proportionally to
 * character count (real glyph rendering is a later phase -- see project
 * plan). Populates *out (caller must layout_list_free() it). */
void layout_compute(DomNode *root, float viewport_w, float viewport_h, LayoutList *out);
void layout_list_free(LayoutList *list);

#endif /* SQW_LAYOUT_H */
