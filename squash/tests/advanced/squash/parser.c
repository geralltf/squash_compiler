/*
 * parser.c  –  Recursive descent parser
 *
 * Operator precedence (low → high):
 *   =          (right-associative assignment)
 *   ||
 *   &&
 *   |
 *   ^
 *   &
 *   == !=
 *   < > <= >=
 *   << >>
 *   + -
 *   * / %
 *   unary: - ! ~
 *   postfix: () []
 *   primary: number, identifier, ( expr )
 */
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */
static Token cur(Parser *p)              { return lexer_peek(p->lex); }
static Token advance(Parser *p)          { return lexer_next(p->lex); }
static int   check(Parser *p, TokenKind k) { return lexer_check(p->lex, k); }
static Token expect(Parser *p, TokenKind k) { return lexer_expect(p->lex, k); }

static int is_type_keyword(TokenKind k) {
    return k==TOK_INT || k==TOK_VOID || k==TOK_CHAR;
}

static const char *type_name_str(TokenKind k) {
    switch(k){
        case TOK_INT:  return "int";
        case TOK_VOID: return "void";
        case TOK_CHAR: return "char";
        default:       return "int";
    }
}

/* =========================================================================
 * parser_init
 * ========================================================================= */
void parser_init(Parser *p, Lexer *l, SymTable *sym) {
    p->lex = l;
    p->sym = sym;
}

/* =========================================================================
 * ParseNumber() – parse a numeric literal from the token stream
 * ========================================================================= */
ASTNode *ParseNumber(Parser *p) {
    Token t = expect(p, TOK_NUMBER);
    return ast_number(t.ival, t.line);
}

/* =========================================================================
 * ParseIdentifier() – parse an identifier, distinguishing variable vs call
 * ========================================================================= */
ASTNode *ParseIdentifier(Parser *p) {
    Token t = expect(p, TOK_IDENT);
    char name[256];
    snprintf(name, sizeof name, "%.*s", t.len, t.start);

    /* Is it a function call? */
    if (check(p, TOK_LPAREN)) {
        advance(p); /* consume '(' */
        /* collect arguments */
        ASTNode *args[64]; int argc = 0;
        if (!check(p, TOK_RPAREN)) {
            do {
                args[argc++] = ParseExpression(p);
            } while (check(p, TOK_COMMA) && advance(p).kind != TOK_EOF);
        }
        expect(p, TOK_RPAREN);

        /* Mark the function as used (for import tracking) */
        Symbol *sym = symtable_lookup(p->sym, name);
        if (sym && sym->kind==SYM_IMPORT) {
            char key[256];
            snprintf(key, sizeof key, "%s:%s", sym->dll, name);
            symtable_add_import(p->sym, key);
        }
        return ast_call(name, args, argc, t.line);
    }

    /* Otherwise it's a variable reference */
    return ast_var(name, t.line);
}

/* =========================================================================
 * ParseFunction() – parse a complete function definition
 *   type name ( params ) { body }
 * ========================================================================= */
ASTNode *ParseFunction(Parser *p) {
    int line = cur(p).line;
    Token type_tok = cur(p);
    const char *ret_type = type_name_str(type_tok.kind);
    advance(p); /* consume type */

    Token name_tok = expect(p, TOK_IDENT);
    char name[256];
    snprintf(name, sizeof name, "%.*s", name_tok.len, name_tok.start);

    /* Register in symbol table at global scope */
    symtable_define_func(p->sym, name, ret_type, 0 /* updated below */);

    expect(p, TOK_LPAREN);

    /* Parameters */
    ASTNode *params[32]; int paramc = 0;
    symtable_push_scope(p->sym);
    symtable_reset_locals(p->sym);

    if (!check(p, TOK_RPAREN)) {
        do {
            int pline = cur(p).line;
            Token ptype = cur(p);
            const char *pt = type_name_str(ptype.kind);
            advance(p);
            Token pname = expect(p, TOK_IDENT);
            char pn[256];
            snprintf(pn, sizeof pn, "%.*s", pname.len, pname.start);

            symtable_define_param(p->sym, pn, pt, paramc);
            params[paramc++] = ast_param(pt, pn, pline);
        } while (check(p, TOK_COMMA) && advance(p).kind != TOK_EOF);
    }
    expect(p, TOK_RPAREN);

    /* Function body */
    ASTNode *body = ParseStatement(p);

    symtable_pop_scope(p->sym);

    return ast_func_decl(ret_type, name, params, paramc, body, line);
}

/* =========================================================================
 * ParseVariable() – parse a variable declaration statement
 *   type name [= expr] ;
 * ========================================================================= */
ASTNode *ParseVariable(Parser *p) {
    int line = cur(p).line;
    Token type_tok = cur(p);
    const char *type = type_name_str(type_tok.kind);
    advance(p);

    Token name_tok = expect(p, TOK_IDENT);
    char name[256];
    snprintf(name, sizeof name, "%.*s", name_tok.len, name_tok.start);

    ASTNode *init = NULL;
    if (check(p, TOK_ASSIGN)) {
        advance(p);
        init = ParseExpression(p);
    }
    expect(p, TOK_SEMICOLON);

    symtable_define_var(p->sym, name, type);
    return ast_var_decl(type, name, init, line);
}

/* =========================================================================
 * ParseStatement() – parse any statement
 * ========================================================================= */
ASTNode *ParseStatement(Parser *p) {
    int line = cur(p).line;

    /* Block: { stmt* } */
    if (check(p, TOK_LBRACE)) {
        advance(p);
        ASTNode *stmts[256]; int count = 0;
        symtable_push_scope(p->sym);
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF))
            stmts[count++] = ParseStatement(p);
        symtable_pop_scope(p->sym);
        expect(p, TOK_RBRACE);
        return ast_block(stmts, count, line);
    }

    /* if ( cond ) then [else else_] */
    if (check(p, TOK_IF)) {
        advance(p);
        expect(p, TOK_LPAREN);
        ASTNode *cond = ParseExpression(p);
        expect(p, TOK_RPAREN);
        ASTNode *then_ = ParseStatement(p);
        ASTNode *else_ = NULL;
        if (check(p, TOK_ELSE)) {
            advance(p);
            else_ = ParseStatement(p);
        }
        return ast_if(cond, then_, else_, line);
    }

    /* while ( cond ) body */
    if (check(p, TOK_WHILE)) {
        advance(p);
        expect(p, TOK_LPAREN);
        ASTNode *cond = ParseExpression(p);
        expect(p, TOK_RPAREN);
        ASTNode *body = ParseStatement(p);
        return ast_while(cond, body, line);
    }

    /* return [expr] ; */
    if (check(p, TOK_RETURN)) {
        advance(p);
        ASTNode *expr = NULL;
        if (!check(p, TOK_SEMICOLON))
            expr = ParseExpression(p);
        expect(p, TOK_SEMICOLON);
        return ast_return(expr, line);
    }

    /* Variable declaration */
    if (is_type_keyword(cur(p).kind)) {
        return ParseVariable(p);
    }

    /* Expression statement */
    ASTNode *expr = ParseExpression(p);
    expect(p, TOK_SEMICOLON);
    return ast_expr_stmt(expr, line);
}

/* =========================================================================
 * Expression parsers — precedence climbing (recursive descent)
 * ========================================================================= */

/* ParseExpression: entry point */
ASTNode *ParseExpression(Parser *p) { return ParseAssignment(p); }

/* Assignment (right-associative): a = b = c  parses as  a = (b = c) */
ASTNode *ParseAssignment(Parser *p) {
    ASTNode *lhs = ParseLogicalOr(p);
    if (check(p, TOK_ASSIGN)) {
        int line = cur(p).line;
        advance(p);
        ASTNode *rhs = ParseAssignment(p);  /* right-recursive */
        return ast_assign(lhs, rhs, line);
    }
    return lhs;
}

/* || */
ASTNode *ParseLogicalOr(Parser *p) {
    ASTNode *lhs = ParseLogicalAnd(p);
    while (check(p, TOK_OR)) {
        int line = cur(p).line; advance(p);
        lhs = ast_binary("||", lhs, ParseLogicalAnd(p), line);
    }
    return lhs;
}

/* && */
ASTNode *ParseLogicalAnd(Parser *p) {
    ASTNode *lhs = ParseBitOr(p);
    while (check(p, TOK_AND)) {
        int line = cur(p).line; advance(p);
        lhs = ast_binary("&&", lhs, ParseBitOr(p), line);
    }
    return lhs;
}

/* | */
ASTNode *ParseBitOr(Parser *p) {
    ASTNode *lhs = ParseBitXor(p);
    while (check(p, TOK_PIPE)) {
        int line = cur(p).line; advance(p);
        lhs = ast_binary("|", lhs, ParseBitXor(p), line);
    }
    return lhs;
}

/* ^ */
ASTNode *ParseBitXor(Parser *p) {
    ASTNode *lhs = ParseBitAnd(p);
    while (check(p, TOK_CARET)) {
        int line = cur(p).line; advance(p);
        lhs = ast_binary("^", lhs, ParseBitAnd(p), line);
    }
    return lhs;
}

/* & */
ASTNode *ParseBitAnd(Parser *p) {
    ASTNode *lhs = ParseEquality(p);
    while (check(p, TOK_AMP)) {
        int line = cur(p).line; advance(p);
        lhs = ast_binary("&", lhs, ParseEquality(p), line);
    }
    return lhs;
}

/* == != */
ASTNode *ParseEquality(Parser *p) {
    ASTNode *lhs = ParseRelational(p);
    for (;;) {
        int line = cur(p).line;
        if (check(p, TOK_EQ))  { advance(p); lhs=ast_binary("==",lhs,ParseRelational(p),line); }
        else if (check(p, TOK_NEQ)){ advance(p); lhs=ast_binary("!=",lhs,ParseRelational(p),line); }
        else break;
    }
    return lhs;
}

/* < > <= >= */
ASTNode *ParseRelational(Parser *p) {
    ASTNode *lhs = ParseShift(p);
    for (;;) {
        int line = cur(p).line;
        if      (check(p, TOK_LT)) { advance(p); lhs=ast_binary("<", lhs,ParseShift(p),line); }
        else if (check(p, TOK_GT)) { advance(p); lhs=ast_binary(">", lhs,ParseShift(p),line); }
        else if (check(p, TOK_LE)) { advance(p); lhs=ast_binary("<=",lhs,ParseShift(p),line); }
        else if (check(p, TOK_GE)) { advance(p); lhs=ast_binary(">=",lhs,ParseShift(p),line); }
        else break;
    }
    return lhs;
}

/* << >> */
ASTNode *ParseShift(Parser *p) {
    ASTNode *lhs = ParseAddSub(p);
    for (;;) {
        int line = cur(p).line;
        if      (check(p, TOK_LSHIFT)){ advance(p); lhs=ast_binary("<<",lhs,ParseAddSub(p),line); }
        else if (check(p, TOK_RSHIFT)){ advance(p); lhs=ast_binary(">>",lhs,ParseAddSub(p),line); }
        else break;
    }
    return lhs;
}

/* + - */
ASTNode *ParseAddSub(Parser *p) {
    ASTNode *lhs = ParseMulDiv(p);
    for (;;) {
        int line = cur(p).line;
        if      (check(p, TOK_PLUS)) { advance(p); lhs=ast_binary("+",lhs,ParseMulDiv(p),line); }
        else if (check(p, TOK_MINUS)){ advance(p); lhs=ast_binary("-",lhs,ParseMulDiv(p),line); }
        else break;
    }
    return lhs;
}

/* * / % */
ASTNode *ParseMulDiv(Parser *p) {
    ASTNode *lhs = ParseUnary(p);
    for (;;) {
        int line = cur(p).line;
        if      (check(p, TOK_STAR))   { advance(p); lhs=ast_binary("*",lhs,ParseUnary(p),line); }
        else if (check(p, TOK_SLASH))  { advance(p); lhs=ast_binary("/",lhs,ParseUnary(p),line); }
        else if (check(p, TOK_PERCENT)){ advance(p); lhs=ast_binary("%",lhs,ParseUnary(p),line); }
        else break;
    }
    return lhs;
}

/* Unary prefix: - ! ~ */
ASTNode *ParseUnary(Parser *p) {
    int line = cur(p).line;
    if (check(p, TOK_MINUS)) { advance(p); return ast_unary('-', ParseUnary(p), line); }
    if (check(p, TOK_BANG))  { advance(p); return ast_unary('!', ParseUnary(p), line); }
    if (check(p, TOK_TILDE)) { advance(p); return ast_unary('~', ParseUnary(p), line); }
    return ParsePostfix(p);
}

/* Postfix — currently just wraps primary (placeholder for future [] ++ --) */
ASTNode *ParsePostfix(Parser *p) {
    return ParsePrimary(p);
}

/* Primary: number | string | identifier | ( expr ) */
ASTNode *ParsePrimary(Parser *p) {
    int line = cur(p).line;

    if (check(p, TOK_NUMBER))
        return ParseNumber(p);

    if (check(p, TOK_STRING)) {
        Token t = cur(p); advance(p);
        return ast_string(t.sval ? t.sval : "", line);
    }

    if (check(p, TOK_IDENT))
        return ParseIdentifier(p);

    if (check(p, TOK_LPAREN)) {
        advance(p);
        ASTNode *e = ParseExpression(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    Token bad_tok = cur(p);
    fprintf(stderr, "Parse error: unexpected token %s at line %d\n", token_kind_name(bad_tok.kind), line);
    exit(1);
}

/* =========================================================================
 * parse_program – top-level: sequence of function or global var declarations
 * ========================================================================= */
ASTNode *parse_program(Parser *p) {
    ASTNode *decls[256]; int count = 0;
    while (!check(p, TOK_EOF)) {
        if (is_type_keyword(cur(p).kind))
            decls[count++] = ParseFunction(p);
        else {
            fprintf(stderr, "Parse error: expected type keyword at line %d\n",
                    cur(p).line);
            exit(1);
        }
    }
    return ast_program(decls, count, 1);
}
