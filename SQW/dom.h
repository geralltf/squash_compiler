#ifndef SQW_DOM_H
#define SQW_DOM_H
#include "html_lexer.h"

/* Shared with sqw_main.c's sqw_resolve_image_urls() -- squash's C frontend
 * doesn't accept "sizeof node->img_url" as an array-size expression, so a
 * plain named constant is used for both DomNode::img_url's own size and
 * that function's local resolve buffer, instead. */
#define SQW_IMG_URL_MAX 384

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
    float css_border_width[4]; /* top, right, bottom, left -- px, 0 if
                                   none (real CSS default). Sits between
                                   margin and padding in the box model, so
                                   layout.c accounts for it the same way
                                   it already does padding -- shifting the
                                   content box in and adding to the final
                                   box height/width. NOT currently drawn
                                   by the renderer (renderer_vk.c has no
                                   per-side border-line draw pass yet,
                                   only a flat background fill) -- this is
                                   a real, deliberate, honestly-scoped gap
                                   rather than a silent one: getting box
                                   SIZING right (so later content doesn't
                                   overlap/misalign) is the more
                                   impactful, independently-useful half of
                                   "support border", and was tractable to
                                   verify (via SQW/tests/test_layout) in
                                   an environment with no GPU display to
                                   actually see a drawn border in;
                                   wiring an actual visible border line
                                   into the renderer is future work. */
    int css_has_border_color; float css_border_color[3];
    int css_has_color; float css_color[3];
    int css_has_bg;    float css_bg[3];
    int css_flex_direction;   /* CssFlexDirection */
    int css_flex_wrap;         /* 0 = nowrap (CSS default), 1 = wrap */
    int css_justify;          /* CssJustify */
    int css_align;             /* CssAlign */
    float css_gap;
    /* Heuristic detection of the extremely common real-world "visually
     * hidden, screen-reader-only" CSS pattern (position:absolute +
     * clip:rect(...) or a ~1x1px box) -- see css.c's own comment on why
     * this exists and exactly what it triggers on. Real position:
     * absolute/fixed (taking an element OUT of normal flow and placing
     * it via top/left/right/bottom) is NOT implemented in general; this
     * is only ever used to detect-and-hide that one specific pattern. */
    int css_position_absolute;
    int css_has_clip;
    /* Raw, unparsed "grid-template-columns" value (e.g. "repeat(4, 1fr)"
     * or "200px 1fr 200px") -- layout.c's own grid code parses this at
     * layout time, not here, since it's the one place track sizes get
     * resolved against a real available width. Empty string if unset. */
    char css_grid_template_columns[128];

    /* Live form-control state -- meaningful only on <input>/<textarea>
     * (and read by their enclosing <form> at submit time, see
     * sqw_main.c's own form-submission code). Lives directly on the node
     * for the same reason the interactive-state fields above do: the DOM
     * already lives for the whole page's lifetime, so there's no need for
     * a side-table keyed by node pointer. form_value is the field's
     * CURRENT text (seeded from the "value" attribute for <input>, or
     * from <textarea>'s own initial text-node content, then mutated
     * live by typing -- see sqw_main.c's own text-input handling);
     * form_checked is a checkbox/radio's current checked state (seeded
     * from a real "checked" attribute); form_focused marks the ONE
     * input/textarea (if any) currently receiving typed keyboard input,
     * mutually exclusive with the URL bar's own focus. */
    char form_value[512];
    int form_checked;
    int form_focused;

    /* <img>'s "src" attribute resolved to an absolute URL (empty if unset
     * or the node isn't an <img>) -- filled in once per page load by
     * sqw_resolve_image_urls() (sqw_main.c), the same "walk once right
     * after dom_parse(), cache the resolved value directly on the node"
     * pattern css_apply() already uses for computed style, so layout.c and
     * the draw pass can look an image up in SQW/image_cache.h's cache by
     * URL without needing the page's current_dir/current_base_url passed
     * down through every call. Also reused for CSS "background-image:
     * url(...)" (css_bg_image_url below) on any element, not just <img>. */
    char img_url[SQW_IMG_URL_MAX];
    char css_bg_image_url[SQW_IMG_URL_MAX];
} DomNode;

DomNode *dom_parse(const char *html);
void dom_free(DomNode *root);
const char *dom_get_attr(const DomNode *node, const char *name);
int dom_is_text(const DomNode *node);

#endif /* SQW_DOM_H */
