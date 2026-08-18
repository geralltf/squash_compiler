#ifndef SQW_HTML_LEXER_H
#define SQW_HTML_LEXER_H
/* Minimal HTML5-ish tokenizer: tags/attributes/text/comments, enough for
 * well-formed-ish real pages (self-closing awareness for <img>, no
 * full spec error-recovery state machine). Pull-style: call
 * html_lex_next() repeatedly until it returns HTML_TOK_EOF. */

#define HTML_MAX_ATTRS 16
#define HTML_MAX_TAG_LEN 32
#define HTML_MAX_ATTR_LEN 64

typedef enum {
    HTML_TOK_EOF = 0,
    HTML_TOK_TAG_OPEN,
    HTML_TOK_TAG_CLOSE,
    HTML_TOK_TEXT
} HtmlTokKind;

typedef struct {
    char name[HTML_MAX_ATTR_LEN];
    char value[HTML_MAX_ATTR_LEN];
} HtmlAttr;

typedef struct {
    HtmlTokKind kind;
    char tag[HTML_MAX_TAG_LEN];     /* TAG_OPEN / TAG_CLOSE */
    int self_closing;               /* TAG_OPEN only, e.g. <img .../> or <br> */
    HtmlAttr attrs[HTML_MAX_ATTRS];
    int attr_count;
    const char *text;               /* TEXT: pointer into the source buffer */
    int text_len;                   /* TEXT: byte length (not NUL-terminated) */
} HtmlToken;

typedef struct {
    const char *src;
    int pos;
    int len;
    int in_raw_text;                    /* inside <script>/<style> body */
    char raw_text_tag[HTML_MAX_TAG_LEN]; /* which one, for the matching close */
} HtmlLexer;

void html_lexer_init(HtmlLexer *lx, const char *src);
/* Returns 0 once EOF has been reported (out->kind == HTML_TOK_EOF), 1 otherwise. */
int html_lex_next(HtmlLexer *lx, HtmlToken *out);

/* Tags whose content is raw text (no nested tag parsing) until the matching
 * close tag -- <script>/<style>. */
int html_tag_is_raw_text(const char *tag);
/* Void elements that never have a close tag / children (<img>, <br>, ...). */
int html_tag_is_void(const char *tag);

#endif /* SQW_HTML_LEXER_H */
