#include "dom_walk.h"
#include "dom.c"
#include <stdlib.h>

typedef struct {
    DomNode *node;
    int next_child;
    int depth;
} DomWalkFrame;

void dom_walk(DomNode *root, DomVisitFn visit, void *ctx) {
    if (!root) return;

    int cap = 64;
    int top = 0; /* number of live frames; stack[top-1] is current */
    DomWalkFrame *stack = (DomWalkFrame *)malloc(cap * sizeof(DomWalkFrame));

    stack[top].node = root;
    stack[top].next_child = 0;
    stack[top].depth = -1; /* root's children are depth 0 */
    top++;

    while (top > 0) {
        DomWalkFrame *f = &stack[top - 1];
        DomNode *n = f->node;

        if (f->next_child >= n->child_count) {
            top--;
            continue;
        }

        DomNode *child = n->children[f->next_child];
        int child_depth = f->depth + 1;
        f->next_child++;

        visit(child, child_depth, ctx);

        if (child->child_count > 0) {
            if (top >= cap) {
                cap *= 2;
                stack = (DomWalkFrame *)realloc(stack, cap * sizeof(DomWalkFrame));
                f = &stack[top - 1]; /* realloc may have moved the array */
            }
            stack[top].node = child;
            stack[top].next_child = 0;
            stack[top].depth = child_depth;
            top++;
        }
    }

    free(stack);
}
