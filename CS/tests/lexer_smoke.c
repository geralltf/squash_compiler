#include "../cs_lexer.h"
#include "../cs_ast.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } } while (0)

int main(void) {
    {
        CsLexer lx;
        CsTok *toks[64]; int n = 0;
        cs_lexer_init(&lx, "public class Foo<T> { int x = 42; double y = 3.14; }");
        for (;;) {
            CsTok *t = cs_lexer_next(&lx);
            toks[n++] = t;
            if (t->kind == CS_TOK_EOF) break;
        }
        CHECK(n == 19, "basic tokenization: expected token count");
        CHECK(toks[0]->kind == CS_TOK_KEYWORD && strcmp(toks[0]->text, "public") == 0, "keyword 'public' recognized");
        CHECK(toks[2]->kind == CS_TOK_IDENT && strcmp(toks[2]->text, "Foo") == 0, "identifier 'Foo' recognized");
        CHECK(toks[3]->kind == CS_TOK_PUNCT && strcmp(toks[3]->text, "<") == 0, "punct '<' for generic open");
        {
            int i;
            for (i = 0; i < n; i++) cs_tok_free(toks[i]);
        }
    }

    {
        CsLexer lx; CsTok *t;
        cs_lexer_init(&lx, "42 3.14 0x1F \"hello\\nworld\" 'A' true false null");
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_INT_LIT && t->int_value == 42, "int literal 42"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_DOUBLE_LIT && t->double_value > 3.13 && t->double_value < 3.15, "double literal 3.14"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_INT_LIT && t->int_value == 0x1F, "hex literal 0x1F"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_STRING_LIT && strcmp(t->text, "hello\nworld") == 0, "string literal with \\n escape"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_CHAR_LIT && t->int_value == 'A', "char literal 'A'"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_KEYWORD && strcmp(t->text, "true") == 0, "keyword true"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_KEYWORD && strcmp(t->text, "false") == 0, "keyword false"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_KEYWORD && strcmp(t->text, "null") == 0, "keyword null"); cs_tok_free(t);
    }

    {
        CsLexer lx; CsTok *t;
        cs_lexer_init(&lx, "=> ?? ?. == != <= >= && || ++ --");
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "=>") == 0, "op =>"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "??") == 0, "op ??"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "?.") == 0, "op ?."); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "==") == 0, "op =="); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "!=") == 0, "op !="); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "<=") == 0, "op <="); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, ">=") == 0, "op >="); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "&&") == 0, "op &&"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "||") == 0, "op ||"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "++") == 0, "op ++"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(strcmp(t->text, "--") == 0, "op --"); cs_tok_free(t);
    }

    {
        CsLexer lx; CsTok *t;
        /* C# source: @"a""b" -- a verbatim string whose doubled "" decodes to one literal quote */
        cs_lexer_init(&lx, "@\"a\"\"b\"");
        t = cs_lexer_next(&lx);
        CHECK(t->kind == CS_TOK_STRING_LIT && strcmp(t->text, "a\"b") == 0, "verbatim string with doubled-quote escape");
        cs_tok_free(t);
    }
    {
        CsLexer lx; CsTok *t;
        /* C# source: @"C:\path\to\file" -- backslash is LITERAL in verbatim strings, no escaping */
        cs_lexer_init(&lx, "@\"C:\\path\\to\\file\"");
        t = cs_lexer_next(&lx);
        CHECK(t->kind == CS_TOK_STRING_LIT && strcmp(t->text, "C:\\path\\to\\file") == 0, "verbatim string treats backslash literally");
        cs_tok_free(t);
    }

    {
        CsLexer lx; CsTok *t;
        cs_lexer_init(&lx, "$\"Hello {name}, you are {age + 1} next year\"");
        t = cs_lexer_next(&lx);
        CHECK(t->kind == CS_TOK_INTERP_STRING_LIT &&
              strcmp(t->text, "Hello {name}, you are {age + 1} next year") == 0,
              "interpolated string raw body captured with braces intact");
        cs_tok_free(t);
    }

    {
        CsLexer lx; CsTok *t;
        /* nested call-with-string-arg inside an interpolation slot */
        cs_lexer_init(&lx, "$\"result={Foo(\"a}b\")}\"");
        t = cs_lexer_next(&lx);
        CHECK(t->kind == CS_TOK_INTERP_STRING_LIT &&
              strcmp(t->text, "result={Foo(\"a}b\")}") == 0,
              "interpolation slot with nested string containing a brace doesn't confuse depth tracking");
        cs_tok_free(t);
    }

    {
        CsLexer lx; CsTok *t;
        cs_lexer_init(&lx, "// comment\nint /* block */ x;");
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_KEYWORD && strcmp(t->text, "int") == 0, "comments skipped, keyword int found"); cs_tok_free(t);
        t = cs_lexer_next(&lx); CHECK(t->kind == CS_TOK_IDENT && strcmp(t->text, "x") == 0, "block comment skipped, ident x found"); cs_tok_free(t);
    }

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
