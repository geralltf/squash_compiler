#ifndef SQW_CSS_H
#define SQW_CSS_H
#include "dom.h"

/* A real (if deliberately scoped) CSS engine: tokenizes/parses actual CSS
 * syntax (rules, selectors, declarations, comments, @-rules), matches
 * selectors against the real DOM tree with real specificity-based
 * cascade + source order, and resolves one final ComputedStyle per
 * element -- not a fake/hardcoded per-tag color table. See css.c's own
 * top comment for the exact, documented scope limits (what selector
 * forms and properties are supported). */

typedef enum {
    CSS_DISPLAY_BLOCK = 0,
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_INLINE_BLOCK,
    CSS_DISPLAY_NONE,
    CSS_DISPLAY_FLEX,
    CSS_DISPLAY_GRID
} CssDisplay;

typedef enum { CSS_FLEX_ROW = 0, CSS_FLEX_COLUMN } CssFlexDirection;
typedef enum { CSS_JUSTIFY_START = 0, CSS_JUSTIFY_CENTER, CSS_JUSTIFY_END, CSS_JUSTIFY_BETWEEN, CSS_JUSTIFY_AROUND } CssJustify;
typedef enum { CSS_ALIGN_START = 0, CSS_ALIGN_CENTER, CSS_ALIGN_END, CSS_ALIGN_STRETCH } CssAlign;

#define CSS_MAX_SELECTOR_CHAIN 6
#define CSS_MAX_COMPOUND_PARTS 4
#define CSS_MAX_RULE_SELECTORS 8
#define CSS_MAX_DECLS 24

typedef enum {
    CSS_SEL_TAG, CSS_SEL_CLASS, CSS_SEL_ID, CSS_SEL_UNIVERSAL,
    /* [name] / [name="value"] -- name holds the attribute name; when a
     * value is also given, value_set is 1 and value holds it (an exact
     * match, real CSS's own "[x=y]" form -- ~=, |=, ^=, $=, *= are not
     * supported, same safe-degradation convention as everything else this
     * engine doesn't implement: a compound using one still parses, the
     * attribute-name check just never finds that suffixed operator so the
     * whole bracket is consumed as an attribute-presence check instead of
     * failing outright -- see css_parse_compound()'s own comment). */
    CSS_SEL_ATTR,
    /* :hover -- matched against the DomNode's own runtime `hover` flag
     * (already tracked by sqw_main.c's mouse-move handling for the
     * existing hardcoded a:hover/button:hover color logic -- see css.c's
     * own comment on how this plugs into that). No other pseudo-class/
     * -element is supported; one still parses (css_parse_compound already
     * consumes ":anything(...)" generically) but never matches, same
     * convention as child/sibling combinators below. */
    CSS_SEL_HOVER
} CssSelKind;

typedef struct {
    CssSelKind kind;
    char name[48];
    /* CSS_SEL_ATTR only. */
    int value_set;
    char value[64];
} CssSimpleSel;

/* One compound selector (e.g. "div.foo#bar" -- everything with no space
 * between it) = up to CSS_MAX_COMPOUND_PARTS simple selectors ANDed
 * together. */
typedef struct {
    CssSimpleSel parts[CSS_MAX_COMPOUND_PARTS];
    int part_count;
} CssCompound;

/* A full selector: a whitespace-separated chain of compounds, matched as
 * a DESCENDANT combinator only (see css.c's top comment -- child ">",
 * sibling "+"/"~", and attribute/pseudo selectors are not supported;
 * a selector using one of those is parsed leniently but will simply
 * never match anything, the same safe-degradation this project's HTML
 * parser already uses elsewhere for unsupported input). */
typedef struct {
    CssCompound chain[CSS_MAX_SELECTOR_CHAIN];
    int chain_len;
    int specificity; /* id*100 + class*10 + tag*1, summed across the chain */
} CssSelector;

typedef struct {
    char name[40];
    char value[192];
} CssDecl;

typedef struct {
    CssSelector selectors[CSS_MAX_RULE_SELECTORS];
    int selector_count;
    CssDecl decls[CSS_MAX_DECLS];
    int decl_count;
    int source_order; /* later rules win a specificity tie -- real cascade order */
} CssRule;

typedef struct {
    CssRule *rules;
    int count, cap;
} CssStylesheet;

void css_stylesheet_init(CssStylesheet *sheet);
void css_stylesheet_free(CssStylesheet *sheet);

/* Parses `text` (a <style> tag's body, or any other CSS source) and
 * APPENDS its rules to `sheet` (source_order continuing from whatever's
 * already in it -- multiple <style> tags combine in document order,
 * matching real cascade behavior). */
void css_parse_into(CssStylesheet *sheet, const char *text);

/* Walks the whole tree, computing every element's final style (from
 * `sheet` plus that element's own inline style="..." attribute, which
 * always wins regardless of specificity -- real CSS's own rule) directly
 * into its DomNode fields (see dom.h's own comment on those). Call once,
 * after dom_parse() and after every <style> tag's text has been folded
 * into `sheet`, before layout_compute() ever runs. */
void css_apply(DomNode *root, CssStylesheet *sheet);

/* Re-resolves ONE element's computed style in place (same resolution
 * css_apply()'s own walk already does per-element -- default style, then
 * matched rules, then inline style="...") WITHOUT touching the rest of
 * the tree or re-running layout. Call this on the old and new hover_node
 * whenever it changes (sqw_main.c's mouse-move handling) so a ":hover"
 * rule's color/background/etc takes effect live -- see css.c's own top
 * comment on ":hover". Deliberately doesn't reflow: if a ":hover" rule
 * changes something layout affects (width, display, ...) the visual
 * result won't repaint correctly until the next real relayout (e.g. the
 * next navigation or resize) -- a real, honestly-scoped limitation, not a
 * bug, matching how rare "layout-affecting :hover" actually is in real
 * CSS (almost all real :hover rules only touch color/background/border). */
void css_apply_one(DomNode *el, CssStylesheet *sheet);

#endif /* SQW_CSS_H */
