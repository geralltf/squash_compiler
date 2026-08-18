#include "html_lexer.h"
#include <string.h>
#include <ctype.h>

static const char *RAW_TEXT_TAGS[] = { "script", "style", 0 };
static const char *VOID_TAGS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", 0
};

static void lower_copy(char *dst, const char *src, int n, int dstcap) {
    int i;
    if (n > dstcap - 1) n = dstcap - 1;
    for (i = 0; i < n; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static int tag_in_list(const char *tag, const char **list) {
    int i;
    for (i = 0; list[i]; i++) {
        if (strcmp(tag, list[i]) == 0) return 1;
    }
    return 0;
}

int html_tag_is_raw_text(const char *tag) { return tag_in_list(tag, RAW_TEXT_TAGS); }
int html_tag_is_void(const char *tag) { return tag_in_list(tag, VOID_TAGS); }

void html_lexer_init(HtmlLexer *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lx->len = (int)strlen(src);
    lx->in_raw_text = 0;
    lx->raw_text_tag[0] = 0;
}

static int at_end(HtmlLexer *lx) { return lx->pos >= lx->len; }
static char peek(HtmlLexer *lx) { return at_end(lx) ? 0 : lx->src[lx->pos]; }
static char peek_at(HtmlLexer *lx, int off) {
    int p = lx->pos + off;
    return (p < lx->len) ? lx->src[p] : 0;
}

static void skip_ws(HtmlLexer *lx) {
    while (!at_end(lx) && isspace((unsigned char)peek(lx))) lx->pos++;
}

/* Skips <!-- ... --> and <!DOCTYPE ...>; returns 1 if it consumed one. */
static int try_skip_markup_declaration(HtmlLexer *lx) {
    if (peek(lx) != '<' || peek_at(lx, 1) != '!') return 0;
    if (peek_at(lx, 2) == '-' && peek_at(lx, 3) == '-') {
        lx->pos += 4;
        while (!at_end(lx) && !(peek(lx) == '-' && peek_at(lx, 1) == '-' && peek_at(lx, 2) == '>')) lx->pos++;
        if (!at_end(lx)) lx->pos += 3;
        return 1;
    }
    /* <!DOCTYPE ...> or any other <! ...> markup declaration */
    lx->pos += 2;
    while (!at_end(lx) && peek(lx) != '>') lx->pos++;
    if (!at_end(lx)) lx->pos++;
    return 1;
}

static void read_attr_value(HtmlLexer *lx, char *out, int outcap) {
    char q = peek(lx);
    int start;
    int n;
    if (q == '"' || q == '\'') {
        lx->pos++;
        start = lx->pos;
        while (!at_end(lx) && peek(lx) != q) lx->pos++;
        n = lx->pos - start;
        if (n > outcap - 1) n = outcap - 1;
        memcpy(out, lx->src + start, n);
        out[n] = 0;
        if (!at_end(lx)) lx->pos++;
    } else {
        start = lx->pos;
        while (!at_end(lx) && !isspace((unsigned char)peek(lx)) && peek(lx) != '>') lx->pos++;
        n = lx->pos - start;
        if (n > outcap - 1) n = outcap - 1;
        memcpy(out, lx->src + start, n);
        out[n] = 0;
    }
}

static void read_attrs(HtmlLexer *lx, HtmlToken *out) {
    out->attr_count = 0;
    for (;;) {
        skip_ws(lx);
        char c = peek(lx);
        if (c == 0 || c == '>' || (c == '/' && peek_at(lx, 1) == '>')) break;
        int start = lx->pos;
        while (!at_end(lx) && !isspace((unsigned char)peek(lx)) && peek(lx) != '=' && peek(lx) != '>' && peek(lx) != '/') lx->pos++;
        int n = lx->pos - start;
        if (n <= 0) { lx->pos++; continue; } /* stray char, avoid infinite loop */
        HtmlAttr *a = (out->attr_count < HTML_MAX_ATTRS) ? &out->attrs[out->attr_count] : 0;
        char namebuf[HTML_MAX_ATTR_LEN];
        lower_copy(namebuf, lx->src + start, n, HTML_MAX_ATTR_LEN);
        if (a) { strncpy(a->name, namebuf, HTML_MAX_ATTR_LEN - 1); a->name[HTML_MAX_ATTR_LEN - 1] = 0; a->value[0] = 0; }
        skip_ws(lx);
        if (peek(lx) == '=') {
            lx->pos++;
            skip_ws(lx);
            char valbuf[HTML_MAX_ATTR_LEN];
            read_attr_value(lx, valbuf, HTML_MAX_ATTR_LEN);
            if (a) { strncpy(a->value, valbuf, HTML_MAX_ATTR_LEN - 1); a->value[HTML_MAX_ATTR_LEN - 1] = 0; }
        }
        if (a) out->attr_count++;
    }
}

int html_lex_next(HtmlLexer *lx, HtmlToken *out) {
    out->kind = HTML_TOK_EOF;
    out->tag[0] = 0;
    out->self_closing = 0;
    out->attr_count = 0;
    out->text = 0;
    out->text_len = 0;

    if (lx->in_raw_text) {
        char closer[HTML_MAX_TAG_LEN + 3];
        int clen;
        closer[0] = '<'; closer[1] = '/';
        strncpy(closer + 2, lx->raw_text_tag, HTML_MAX_TAG_LEN);
        closer[HTML_MAX_TAG_LEN + 1] = 0;
        clen = (int)strlen(closer);
        int start = lx->pos;
        while (!at_end(lx)) {
            if (lx->src[lx->pos] == '<' && lx->pos + clen <= lx->len && _strnicmp(lx->src + lx->pos, closer, clen) == 0) break;
            lx->pos++;
        }
        if (lx->pos > start) {
            out->kind = HTML_TOK_TEXT;
            out->text = lx->src + start;
            out->text_len = lx->pos - start;
            return 1;
        }
        lx->in_raw_text = 0;
        /* fall through: next call sees the "</tag" as a normal close tag */
    }

    for (;;) {
        if (at_end(lx)) return 0;
        if (peek(lx) == '<' && peek_at(lx, 1) == '!') {
            if (try_skip_markup_declaration(lx)) continue;
        }
        break;
    }

    if (peek(lx) != '<') {
        int start = lx->pos;
        while (!at_end(lx) && peek(lx) != '<') lx->pos++;
        out->kind = HTML_TOK_TEXT;
        out->text = lx->src + start;
        out->text_len = lx->pos - start;
        return 1;
    }

    if (peek_at(lx, 1) == '/') {
        lx->pos += 2;
        int start = lx->pos;
        while (!at_end(lx) && peek(lx) != '>' && !isspace((unsigned char)peek(lx))) lx->pos++;
        lower_copy(out->tag, lx->src + start, lx->pos - start, HTML_MAX_TAG_LEN);
        while (!at_end(lx) && peek(lx) != '>') lx->pos++;
        if (!at_end(lx)) lx->pos++;
        out->kind = HTML_TOK_TAG_CLOSE;
        return 1;
    }

    /* Opening tag */
    lx->pos++;
    int start = lx->pos;
    while (!at_end(lx) && !isspace((unsigned char)peek(lx)) && peek(lx) != '>' && peek(lx) != '/') lx->pos++;
    lower_copy(out->tag, lx->src + start, lx->pos - start, HTML_MAX_TAG_LEN);
    read_attrs(lx, out);
    skip_ws(lx);
    if (peek(lx) == '/' && peek_at(lx, 1) == '>') {
        out->self_closing = 1;
        lx->pos += 2;
    } else {
        if (!at_end(lx) && peek(lx) == '>') lx->pos++;
        if (html_tag_is_void(out->tag)) out->self_closing = 1;
    }
    out->kind = HTML_TOK_TAG_OPEN;

    if (!out->self_closing && html_tag_is_raw_text(out->tag)) {
        lx->in_raw_text = 1;
        strncpy(lx->raw_text_tag, out->tag, HTML_MAX_TAG_LEN - 1);
        lx->raw_text_tag[HTML_MAX_TAG_LEN - 1] = 0;
    }
    return 1;
}
