#include "dom.h"
#include "html_lexer.c"
#include <stdlib.h>
#include <string.h>

#define DOM_MAX_STACK 128
#define DOM_CHILD_INIT_CAP 4

static DomNode *dom_node_new(const char *tag) {
    DomNode *n = (DomNode *)malloc(sizeof(DomNode));
    memset(n, 0, sizeof(DomNode));
    if (tag) {
        strncpy(n->tag, tag, HTML_MAX_TAG_LEN - 1);
        n->tag[HTML_MAX_TAG_LEN - 1] = 0;
    }
    return n;
}

static void dom_node_add_child(DomNode *parent, DomNode *child) {
    if (parent->child_count >= parent->child_cap) {
        int newcap = parent->child_cap ? parent->child_cap * 2 : DOM_CHILD_INIT_CAP;
        parent->children = (DomNode **)realloc(parent->children, newcap * sizeof(DomNode *));
        parent->child_cap = newcap;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

DomNode *dom_parse(const char *html) {
    DomNode *root = dom_node_new("#document");

    /* stack[] and *tok are heap-allocated, not stack locals: HtmlToken
     * alone (16 attrs x 2x64-byte name/value buffers) is ~2KB, and squash's
     * codegen has had known issues with large stack-resident locals in the
     * past (see project memory on pp_expand's char[8192] locals, and
     * SQW/vk_context.c's own heap-allocated SqwVkContext) -- sidestepped
     * here the same way. */
    DomNode **stack = (DomNode **)malloc(DOM_MAX_STACK * sizeof(DomNode *));
    int depth = 0;
    stack[depth++] = root;

    HtmlLexer lx;
    HtmlToken *tok = (HtmlToken *)malloc(sizeof(HtmlToken));
    html_lexer_init(&lx, html);

    while (html_lex_next(&lx, tok)) {
        DomNode *top = stack[depth - 1];

        if (tok->kind == HTML_TOK_TEXT) {
            if (tok->text_len <= 0) continue;
            DomNode *tn = dom_node_new(0);
            tn->text = (char *)malloc(tok->text_len + 1);
            memcpy(tn->text, tok->text, tok->text_len);
            tn->text[tok->text_len] = 0;
            dom_node_add_child(top, tn);
        } else if (tok->kind == HTML_TOK_TAG_OPEN) {
            DomNode *el = dom_node_new(tok->tag);
            int i;
            el->attr_count = tok->attr_count;
            for (i = 0; i < tok->attr_count; i++) {
                strncpy(el->attrs[i].name, tok->attrs[i].name, HTML_MAX_ATTR_LEN - 1);
                el->attrs[i].name[HTML_MAX_ATTR_LEN - 1] = 0;
                strncpy(el->attrs[i].value, tok->attrs[i].value, HTML_MAX_ATTR_LEN - 1);
                el->attrs[i].value[HTML_MAX_ATTR_LEN - 1] = 0;
            }
            dom_node_add_child(top, el);
            if (!tok->self_closing && depth < DOM_MAX_STACK) {
                stack[depth++] = el;
            }
        } else if (tok->kind == HTML_TOK_TAG_CLOSE) {
            /* Leniently pop up to (and including) the matching open element,
             * skipping unmatched close tags entirely -- real-world HTML is
             * rarely perfectly well-formed. */
            int i;
            int match = -1;
            for (i = depth - 1; i >= 1; i--) {
                if (strcmp(stack[i]->tag, tok->tag) == 0) { match = i; break; }
            }
            if (match >= 1) depth = match;
        }
    }

    free(tok);
    free(stack);
    return root;
}

static void dom_free_rec_stack_push(DomNode ***stack, int *count, int *cap, DomNode *n) {
    if (*count >= *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *stack = (DomNode **)realloc(*stack, (*cap) * sizeof(DomNode *));
    }
    (*stack)[(*count)++] = n;
}

void dom_free(DomNode *root) {
    if (!root) return;
    DomNode **stack = 0;
    int count = 0, cap = 0;
    dom_free_rec_stack_push(&stack, &count, &cap, root);
    /* Collect every node first (BFS/DFS order doesn't matter for freeing),
     * then free bottom-up via a second pass over the same list. */
    DomNode **all = 0;
    int allcount = 0, allcap = 0;
    while (count > 0) {
        DomNode *n = stack[--count];
        dom_free_rec_stack_push(&all, &allcount, &allcap, n);
        int i;
        for (i = 0; i < n->child_count; i++) {
            /* Local temp, not "n->children[i]" passed inline as the call
             * argument: a real squash codegen bug (reproduced independent
             * of this file -- struct-array-indexed expression evaluated
             * directly as a call argument corrupts caller state, e.g. the
             * loop's own "n"/"i") crashes this traversal otherwise, on the
             * second push of any node with 2+ children. dom_walk.c/
             * layout.c's own DomNode traversals already use this same
             * local-temp pattern for the identical child-array access. */
            DomNode *child = n->children[i];
            dom_free_rec_stack_push(&stack, &count, &cap, child);
        }
    }
    int i;
    for (i = 0; i < allcount; i++) {
        DomNode *n = all[i];
        if (n->text) free(n->text);
        if (n->children) free(n->children);
        free(n);
    }
    free(all);
    free(stack);
}

const char *dom_get_attr(const DomNode *node, const char *name) {
    int i;
    for (i = 0; i < node->attr_count; i++) {
        if (strcmp(node->attrs[i].name, name) == 0) return node->attrs[i].value;
    }
    return 0;
}

int dom_is_text(const DomNode *node) {
    return node->text != 0;
}
