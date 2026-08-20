#ifndef SQW_DOM_H
#define SQW_DOM_H
#include "html_lexer.h"

typedef struct {
    char name[HTML_MAX_ATTR_LEN];
    char value[HTML_MAX_ATTR_LEN];
} DomAttr;

/* A DomNode is either an element (tag[0] != 0, text == NULL) or a text node
 * (tag[0] == 0, text != NULL). The synthetic root returned by dom_parse()
 * has tag "#document". */
typedef struct DomNode {
    char tag[HTML_MAX_TAG_LEN];
    char *text;

    DomAttr attrs[HTML_MAX_ATTRS];
    int attr_count;

    struct DomNode **children;
    int child_count;
    int child_cap;

    struct DomNode *parent;

    /* Interactive state, checked/set by sqw_main.c's hit-testing and
     * click handling and read by the renderer for per-state color (e.g.
     * an <a> goes blue->purple once visited) -- lives directly on the
     * node rather than a side-table since the DOM already lives for the
     * whole process/page lifetime. Meaningful only for <a> (visited,
     * hover) and <button> (hover, active/pressed) nodes; left 0 for
     * everything else. */
    int visited;
    int hover;
    int active;

    /* Computed CSS style -- resolved ONCE per element by css_apply()
     * (SQW/css.c), right after dom_parse() and after every <style> tag's
     * text has been folded into a CssStylesheet, before layout_compute()
     * ever runs. Every element gets real values here (css_apply() fills
     * in each tag's normal browser-default display even with zero
     * matching CSS rules), so layout.c never needs to guess. Lives
     * directly on the node for the same reason the interactive-state
     * fields above do. Meaningless (left at whatever css_apply() didn't
     * touch) on text nodes and the synthetic #document root. */
    int css_display;          /* CssDisplay, see css.h */
    int css_has_width;  float css_width;
    int css_has_height; float css_height;
    float css_margin[4];       /* top, right, bottom, left -- px */
    float css_padding[4];      /* top, right, bottom, left -- px */
    int css_has_color; float css_color[3];
    int css_has_bg;    float css_bg[3];
    int css_flex_direction;   /* CssFlexDirection */
    int css_justify;          /* CssJustify */
    int css_align;             /* CssAlign */
    float css_gap;
    /* Raw, unparsed "grid-template-columns" value (e.g. "repeat(4, 1fr)"
     * or "200px 1fr 200px") -- layout.c's own grid code parses this at
     * layout time, not here, since it's the one place track sizes get
     * resolved against a real available width. Empty string if unset. */
    char css_grid_template_columns[128];
} DomNode;

DomNode *dom_parse(const char *html);
void dom_free(DomNode *root);
const char *dom_get_attr(const DomNode *node, const char *name);
int dom_is_text(const DomNode *node);

#endif /* SQW_DOM_H */
