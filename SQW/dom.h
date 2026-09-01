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
    /* "flex-grow: N;" (also the first number of the "flex: N [S [B]];"
     * shorthand) -- how much of a flex ROW's own leftover space (after
     * every item's natural/shrink-to-fit width, see
     * compute_flex_row_positions()'s own comment) this item should
     * absorb, proportional to every OTHER item in the same row that also
     * has a non-zero flex-grow, real CSS's own algorithm (simplified: no
     * flex-shrink/flex-basis modeling, no min/max-width clamping during
     * growth -- see layout.c's own comment on where this is applied).
     * Real CSS default is 0 (an item doesn't grow unless told to) --
     * matches this field's own zero-initialized default, so a flex
     * container with no flex-grow anywhere behaves exactly as it did
     * before this field existed (falls through to justify-content's own
     * existing space distribution, unchanged). */
    float css_flex_grow;
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

    /* This element's own JS click handler (a JSObject* -- js_engine.h's
     * own function-object type -- cast through an opaque void* so this
     * header doesn't need to know anything about js_engine.h/JSObject;
     * NULL if none). Set by either a real "onclick" HTML attribute
     * (compiled into an implicit tiny function once, when its owning
     * page's <script>s run -- see js_engine.c's own comment) or a script
     * doing "el.onclick = fn"/"el.addEventListener('click', fn)". Lives
     * directly on the node, same rationale as every other per-element
     * interactive-state field above: a JS wrapper object returned by
     * document.getElementById() is a fresh, disposable JSObject each
     * call (this engine doesn't cache/intern one wrapper per DomNode),
     * so a handler assigned through it would otherwise be lost the
     * instant that wrapper is garbage -- storing it here instead makes
     * it survive for the whole page's lifetime, exactly like this
     * project's own click-driven navigation/hover state already does. */
    void *js_onclick;
    /* Same rationale/lifetime as js_onclick above, one slot per other
     * DOM event this project wires up: 'input' (fires on every keystroke
     * that changes a text input/textarea's own form_value -- see
     * sqw_main.c's own text-input handling), 'change' (fires once a
     * checkbox/radio is toggled, or once a text input/textarea loses
     * focus -- real HTML5's own distinction between the two events), and
     * 'keydown' (fires for a key press while this element has keyboard
     * focus). Sqw_main.c's own event handling is what actually calls
     * js_dispatch_input()/js_dispatch_change()/js_dispatch_keydown() at
     * the right moments -- these fields only hold WHICH function (if
     * any) to call. */
    void *js_oninput;
    void *js_onchange;
    void *js_onkeydown;

    /* Phase 7: this element's own C# click handler -- a REAL, raw C-ABI
     * function pointer (a loaded C# script's static method, Phase 6d's
     * "static method as a function pointer value" mechanism), NOT a
     * JSObject* like js_onclick above -- there's no interpreter/function-
     * object layer in between here, the pointer is directly callable.
     * Set via the "SqwRegisterClickHandler(string elementId, IntPtr fn)"
     * host symbol (SQW/sqo_host_syms.c), dispatched by sqw_main.c's own
     * click-handling code alongside js_onclick's own ancestor-walk. NULL
     * (the default) if none. Signature: "void (*)(void)". */
    void *native_onclick;

    /* Text styling -- font_size/text_align/line_height/font_weight_bold
     * are real CSS-inherited properties (unlike most fields above, which
     * only ever come from a rule/inline-style directly targeting the
     * element itself): css_apply_element() seeds each of these from the
     * element's OWN PARENT before applying this element's own matched
     * rules/inline style on top, same as real CSS's inheritance model,
     * see css.c's own comment on where that seeding happens. Root default
     * is 16px/left/normal(1.2x)/not-bold, the real CSS initial values.
     * Deliberately appended at the very END of DomNode, not grouped in
     * with the other css_* fields further up -- a real, confirmed squash
     * codegen bug (struct-field read-back garbage for a float field
     * inserted mid-struct on a struct this large/field-heavy, confirmed
     * via a standalone repro this session) made every read of
     * css_font_size return garbage when these fields lived between
     * css_has_bg and css_flex_direction; appending them here instead,
     * after every other field, avoids it entirely (gcc-compiled DomNode
     * usage is unaffected either way -- this is purely a squash-codegen
     * struct-layout issue, not a real C bug). */
    float css_font_size;         /* px */
    int css_text_align;          /* 0=left, 1=center, 2=right */
    float css_line_height;       /* px; <=0 means "normal" (1.2 * font_size) */
    int css_font_weight_bold;    /* 0/1 */
    /* opacity is NOT an inherited property in real CSS (a child's own
     * opacity is independent of its parent's) -- defaults to 1.0. */
    float css_opacity;
} DomNode;

DomNode *dom_parse(const char *html);
void dom_free(DomNode *root);
const char *dom_get_attr(const DomNode *node, const char *name);
/* Sets (overwriting an existing one, or appending a new one, capped at
 * HTML_MAX_ATTRS same as a real parsed tag) an attribute's value --
 * needed by js_engine.c's setAttribute()/className/id builtins, which
 * mutate a real live DOM attribute the same way real JS does, not a
 * side-channel. */
void dom_set_attr(DomNode *node, const char *name, const char *value);
int dom_is_text(const DomNode *node);

#endif /* SQW_DOM_H */
