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
} DomNode;

DomNode *dom_parse(const char *html);
void dom_free(DomNode *root);
const char *dom_get_attr(const DomNode *node, const char *name);
int dom_is_text(const DomNode *node);

#endif /* SQW_DOM_H */
