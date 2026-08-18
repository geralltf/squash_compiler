#ifndef SQW_DOM_WALK_H
#define SQW_DOM_WALK_H
#include "dom.h"

typedef void (*DomVisitFn)(DomNode *node, int depth, void *ctx);

/* Pre-order depth-first traversal of root's descendants (root itself, the
 * synthetic "#document" node, is not visited). Iterative (explicit stack),
 * not recursive -- see project notes on squash's own stack-frame history. */
void dom_walk(DomNode *root, DomVisitFn visit, void *ctx);

#endif /* SQW_DOM_WALK_H */
