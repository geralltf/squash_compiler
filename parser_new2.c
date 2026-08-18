#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* =========================================================================
 * Helpers
 * ========================================================================= */
/* cur: macro for direct access — avoids large struct return ABI issue */
#define cur(p) ((p)->lex->cur)
static void adv(Parser *p) { lexer_next(p->lex); }
static int  adv_kind(Parser *p) { adv(p); return (int)p->lex->cur.kind; }
static int    chk(Parser *p, TokenKind k) { return lexer_check(p->lex, k); }
static void eat(Parser *p, TokenKind k) { lexer_expect(p->lex, k); }

void parse_error(Parser *p, const char *msg) {
    printf("%s:%d:%d: error: %s\n", p->filename?p->filename:"?", cur(p).line, cur(p).col, msg);
    p->error_count++;
    if (p->error_count > 20) { printf("Too many errors, aborting.\n"); exit(1); }
}

void parse_warn(Parser *p, const char *msg) {
    printf("%s:%d:%d: warning: %s\n", p->filename?p->filename:"?", cur(p).line, cur(p).col, msg);
}

static char *tok_ident(Token *t) {
    static char buf[256];
    snprintf(buf,sizeof buf,"%.*s",t->len,t->start);
    return buf;
}

/* =========================================================================
 * parser_init
 * ========================================================================= */
void parser_init(Parser *p, Lexer *l, SymTable *sym, const char *filename) {
    p->lex=l; p->sym=sym; p->filename=filename; p->error_count=0; p->anon_counter=0;
}

/* =========================================================================
 * Type parsing
 * ========================================================================= */

/* is current token a type keyword or typedef name? */
static int is_type_start(Parser *p) {
    TokenKind k = cur(p).kind;
    if (tok_is_type(k)) return 1;
    /* check if it's a typedef'd name */
    if (k==TOK_IDENT) {
        Symbol *s = symtable_lookup(p->sym, tok_ident(&cur(p)));
        if (s && s->kind==SYM_TYPEDEF) return 1;
    }
    return 0;
}

TypeInfo *ParseTypeSpecifier(Parser *p) {
    TypeInfo *ti = calloc(1, sizeof(TypeInfo));
    ti->array_size = -1;

    /* storage-class qualifiers: eaten, not stored in TypeInfo */
    while (chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)||chk(p,TOK_SIGNED)||chk(p,TOK_UNSIGNED)) {
        if (chk(p,TOK_UNSIGNED)) ti->is_unsigned=1;
        adv(p);
    }

    TokenKind k = cur(p).kind;

    if (k==TOK_STRUCT || k==TOK_UNION) {
        int is_union = (k==TOK_UNION);
        adv(p);
        char sname[256]=""; int has_name=0;
        if (chk(p,TOK_IDENT)) {
            strncpy(sname, tok_ident(&cur(p)), sizeof sname-1);
            has_name=1; adv(p);
        }
        if (chk(p,TOK_LBRACE)) {
            /* struct definition inline */
            adv(p);
            ASTNode *fields[64]; int nf=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                TypeInfo *ft = ParseFullType(p);
                if (!ft) { parse_error(p,"expected field type"); break; }
                do {
                    /* field might have pointer stars */
                    TypeInfo *ft2 = ft;
                    while (chk(p,TOK_STAR)) { adv(p); ft2=typeinfo_ptr(ft2); }
                    char fn[64];
                    if (!chk(p,TOK_IDENT)) {
                        /* Anonymous struct/union member (C11): no field name.
                         * Give it a hidden name "__anon_N" so struct is valid. */
                        if (ft && ft->base &&
                            (strncmp(ft->base,"struct ",7)==0 ||
                             strncmp(ft->base,"union " ,6)==0)) {
                            snprintf(fn, sizeof fn, "__anon_%d", p->anon_counter++);
                        } else {
                            parse_error(p,"expected field name"); break;
                        }
                    } else {
                        strncpy(fn, tok_ident(&cur(p)), sizeof fn-1);
                        fn[sizeof fn-1]='\0';
                        adv(p);
                    }
                    int arr=-1;
                    if (chk(p,TOK_LBRACKET)) {
                        adv(p);
                        if (chk(p,TOK_NUMBER)) { arr=(int)cur(p).ival; adv(p); }
                        else arr=0;
                        eat(p,TOK_RBRACKET);
                    }
                    fields[nf++]=ast_field(ft2,fn,arr,cur(p).line);
                } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
                eat(p,TOK_SEMICOLON);
            }
            eat(p,TOK_RBRACE);
            char key[280]; snprintf(key,sizeof key,"%s %s",is_union?"union":"struct",sname);
            ASTNode *sd=ast_struct_decl(sname,is_union,fields,nf,cur(p).line);
            /* compute rough size */
            int sz=0;
            for (int i=0;i<nf;i++) {
                int fs=typeinfo_size(fields[i]->field.type,p->sym->is_64bit);
                if (fields[i]->field.array_size>0) fs*=fields[i]->field.array_size;
                if (is_union) sz = fs>sz?fs:sz;
                else sz+=fs;
            }
            if (!has_name) {
                /* Generate unique name for anonymous struct/union so it can be found
                 * by field_byte_offset after typedef resolution. */
                snprintf(sname, sizeof sname, "$anon%d", p->anon_counter++);
                has_name = 1;
                snprintf(key, sizeof key, "%s %s", is_union?"union":"struct", sname);
            }
            if (has_name) symtable_define_struct(p->sym, sname, sd, sz);
            ti->base = my_strdup(key);
        } else {
            char key[280]; snprintf(key,sizeof key,"%s %s",is_union?"union":"struct",sname);
            ti->base = my_strdup(key);
        }
        return ti;
    }

    if (k==TOK_ENUM) {
        adv(p);
        char ename[256]="";
        if (chk(p,TOK_IDENT)) { strncpy(ename,tok_ident(&cur(p)),sizeof ename-1); adv(p); }
        if (chk(p,TOK_LBRACE)) {
            adv(p);
            long long next_val=0;
            ASTNode *vals[256]; int nv=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                if (!chk(p,TOK_IDENT)) { parse_error(p,"expected enum name"); break; }
                char vn[256]; strncpy(vn,tok_ident(&cur(p)),sizeof vn-1); adv(p);
                long long val=next_val;
                if (chk(p,TOK_ASSIGN)) {
                    adv(p); /* skip '=' */
                    /* Handle negative literals, e.g. STR_LESS = -1 */
                    int neg = 0;
                    if (chk(p,TOK_MINUS)) { neg=1; adv(p); }
                    if (chk(p,TOK_NUMBER)) {
                        val = neg ? -(cur(p).ival) : cur(p).ival;
                        adv(p);
                    } else if (chk(p,TOK_IDENT)) {
                        /* Enum referring to another enum constant */
                        Symbol *rs = symtable_lookup(p->sym, tok_ident(&cur(p)));
                        val = (rs && rs->kind==SYM_ENUM_VAL) ? rs->enum_value : 0;
                        if (neg) val = -val;
                        adv(p);
                    } else if (chk(p,TOK_CHAR_LIT)) {
                        val = neg ? -(cur(p).ival) : cur(p).ival;
                        adv(p);
                    }
                }
                next_val=val+1;
                symtable_define_enum_val(p->sym, vn, val);
                vals[nv++]=ast_enum_val(vn,val,1,cur(p).line);
                if (!chk(p,TOK_COMMA)) break; adv(p);
            }
            eat(p,TOK_RBRACE);
        }
        ti->base = my_strdup(ename[0] ? ename : "int");
        return ti;
    }

    /* Check for typedef name */
    if (k==TOK_IDENT) {
        Symbol *s = symtable_lookup(p->sym, tok_ident(&cur(p)));
        if (s && s->kind==SYM_TYPEDEF) {
            ti->base = my_strdup(tok_ident(&cur(p))); adv(p);
            return ti;
        }
    }

    /* Primitive types — accumulate qualifiers */
    char base[64]=""; int has_long=0, has_short=0;
    while (1) {
        k=cur(p).kind;
        if (k==TOK_UNSIGNED) { ti->is_unsigned=1; adv(p); continue; }
        if (k==TOK_SIGNED)   { adv(p); continue; }
        if (k==TOK_LONG)     { has_long++; adv(p); continue; }
        if (k==TOK_SHORT)    { has_short=1; adv(p); continue; }
        if (k==TOK_INT)      { strncpy(base,"int",sizeof base-1); adv(p); break; }
        if (k==TOK_CHAR)     { strncpy(base,"char",sizeof base-1); adv(p); break; }
        if (k==TOK_VOID)     { strncpy(base,"void",sizeof base-1); adv(p); break; }
        if (k==TOK_DOUBLE)   { strncpy(base,"double",sizeof base-1); adv(p); break; }
        if (k==TOK_FLOAT_KW) { strncpy(base,"float",sizeof base-1); adv(p); break; }
        break;
    }
    if (!base[0]) {
        if (has_long) strncpy(base,"long",sizeof base-1);
        else if (has_short) strncpy(base,"short",sizeof base-1);
        else strncpy(base,"int",sizeof base-1);
    } else if (has_long==2) strncpy(base,"long long",sizeof base-1);
    else if (has_long==1 && strcmp(base,"int")==0) strncpy(base,"long",sizeof base-1);
    else if (has_short && strcmp(base,"int")==0) strncpy(base,"short",sizeof base-1);

    ti->base = my_strdup(base);
    return ti;
}

TypeInfo *ParseFullType(Parser *p) {
    if (!is_type_start(p)) return NULL;
    /* eat storage class separately */
    while (chk(p,TOK_STATIC)||chk(p,TOK_EXTERN)||chk(p,TOK_AUTO)||
           chk(p,TOK_REGISTER)) adv(p);
    TypeInfo *ti = ParseTypeSpecifier(p);
    if (!ti) return NULL;
    while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
    return ti;
}

/* =========================================================================
 * ParseNumber
 * ========================================================================= */
ASTNode *ParseNumber(Parser *p) {
    Token t = cur(p);
    if (t.kind==TOK_FLOAT) { adv(p); return ast_float(t.fval, t.line); }
    eat(p,TOK_NUMBER);
    return ast_number(t.ival, t.line);
}

/* =========================================================================
 * ParseIdentifier
 * ========================================================================= */
ASTNode *ParseIdentifier(Parser *p) {
    Token t = eat(p, TOK_IDENT);
    char name[256]; snprintf(name,sizeof name,"%.*s",t.len,t.start);

    /* function call? */
    if (chk(p,TOK_LPAREN)) {
        adv(p);
        ASTNode *args[128]; int argc=0;
        if (!chk(p,TOK_RPAREN)) {
            do {
                if (argc<128) args[argc++]=ParseAssignment(p);
                else { parse_error(p,"too many arguments"); ParseAssignment(p); }
            } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
        }
        eat(p,TOK_RPAREN);
        /* Track Windows API imports */
        Symbol *sym = symtable_lookup(p->sym, name);
        if (sym && sym->kind==SYM_IMPORT && sym->dll) {
            char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
            symtable_add_import(p->sym, key);
        }
        return ast_call(name, args, argc, t.line);
    }
    return ast_var(name, t.line);
}

/* =========================================================================
 * ParsePrimary
 * ========================================================================= */
ASTNode *ParsePrimary(Parser *p) {
    int line = cur(p).line;

    if (chk(p,TOK_NUMBER)||chk(p,TOK_FLOAT)) return ParseNumber(p);

    if (chk(p,TOK_CHAR_LIT)) {
        Token t=cur(p); adv(p);
        return ast_char_lit(t.ival, t.line);
    }

    if (chk(p,TOK_STRING)) {
        Token t=cur(p); adv(p);
        /* Adjacent string concatenation */
        char buf[4096]; snprintf(buf,sizeof buf,"%s",t.sval?t.sval:"");
        while (chk(p,TOK_STRING)) {
            t=cur(p); adv(p);
            if (t.sval) strncat(buf,t.sval,sizeof buf-strlen(buf)-1);
        }
        return ast_string(buf, line);
    }

    if (chk(p,TOK_LPAREN)) {
        adv(p);
        /* Check for cast: (type)expr */
        if (is_type_start(p)) {
            TypeInfo *ti = ParseFullType(p);
            if (ti && chk(p,TOK_RPAREN)) {
                adv(p);
                ASTNode *expr = ParseUnary(p);
                return ast_cast(ti, expr, line);
            }
            /* Not a cast — parenthesised expression, put type back somehow */
            /* This is a parse ambiguity we resolve by just continuing as expr */
            typeinfo_free(ti);
        }
        ASTNode *e = ParseExpression(p);
        eat(p,TOK_RPAREN);
        return e;
    }

    if (chk(p,TOK_SIZEOF)) {
        adv(p);
        /* sizeof supports both sizeof(x) and sizeof x */
        if (chk(p, TOK_LPAREN)) {
            adv(p); /* consume '(' */
            if (is_type_start(p)) {
                TypeInfo *ti = ParseFullType(p);
                eat(p,TOK_RPAREN);
                return ast_sizeof_type(ti, line);
            }
            ASTNode *e = ParseUnary(p);
            eat(p,TOK_RPAREN);
            return ast_sizeof_expr(e, line);
        } else {
            /* sizeof without parens: sizeof varname */
            ASTNode *eu = ParseUnary(p);
            return ast_sizeof_expr(eu, line);
        }
    }

    if (chk(p,TOK_IDENT)) return ParseIdentifier(p);

    { char _em[128]; snprintf(_em,sizeof _em,"unexpected token '%s'",token_kind_name(cur(p).kind)); parse_error(p,_em); }
    adv(p);
    return ast_number(0, line); /* error recovery */
}

/* =========================================================================
 * ParsePostfix — a[i], a.b, a->b, f(args), a++, a--
 * ========================================================================= */
ASTNode *ParsePostfix(Parser *p) {
    ASTNode *n = ParsePrimary(p);
    for (;;) {
        int line = cur(p).line;
        if (chk(p,TOK_LBRACKET)) {
            adv(p);
            ASTNode *idx = ParseExpression(p);
            eat(p,TOK_RBRACKET);
            n = ast_index(n, idx, line);
        } else if (chk(p,TOK_DOT)) {
            adv(p);
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected field name"); break; }
            char fn[256]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p);
            n = ast_member(n, fn, 0, line);
        } else if (chk(p,TOK_ARROW)) {
            adv(p);
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected field name"); break; }
            char fn[256]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p);
            n = ast_member(n, fn, 1, line);
        } else if (chk(p,TOK_INC)) {
            adv(p); n = ast_unary("++", n, 1, line);
        } else if (chk(p,TOK_DEC)) {
            adv(p); n = ast_unary("--", n, 1, line);
        } else if (chk(p,TOK_LPAREN)) {
            /* Function-pointer call: expr(args) */
            adv(p);
            ASTNode *args[64]; int argc=0;
            if (!chk(p,TOK_RPAREN)) {
                do {
                    if (argc<64) args[argc++]=ParseAssignment(p);
                    else { parse_error(p,"too many args"); ParseAssignment(p); }
                } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
            }
            eat(p,TOK_RPAREN);
            n = ast_fp_call(n, args, argc, line);
        } else break;
    }
    return n;
}

/* =========================================================================
 * ParseUnary — prefix: !, ~, -, +, &, *, ++, --, (cast)
 * ========================================================================= */
ASTNode *ParseUnary(Parser *p) {
    int line = cur(p).line;
    if (chk(p,TOK_BANG))  { adv(p); return ast_unary("!", ParseUnary(p), 0, line); }
    if (chk(p,TOK_TILDE)) { adv(p); return ast_unary("~", ParseUnary(p), 0, line); }
    if (chk(p,TOK_MINUS)) { adv(p); return ast_unary("-", ParseUnary(p), 0, line); }
    if (chk(p,TOK_PLUS))  { adv(p); return ParseUnary(p); }
    if (chk(p,TOK_INC))   { adv(p); return ast_unary("++",ParseUnary(p), 0, line); }
    if (chk(p,TOK_DEC))   { adv(p); return ast_unary("--",ParseUnary(p), 0, line); }
    if (chk(p,TOK_AMP))   { adv(p); return ast_addr(ParseUnary(p), line); }
    if (chk(p,TOK_STAR))  { adv(p); return ast_deref(ParseUnary(p), line); }
    return ParsePostfix(p);
}

/* =========================================================================
 * Binary expression parsers — full precedence table
 * ========================================================================= */
ASTNode *ParseMulDiv(Parser *p) {
    ASTNode *lhs = ParseUnary(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_STAR))    op="*";
        else if (chk(p,TOK_SLASH))   op="/";
        else if (chk(p,TOK_PERCENT)) op="%";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseUnary(p), line);
    }
    return lhs;
}
ASTNode *ParseAddSub(Parser *p) {
    ASTNode *lhs = ParseMulDiv(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_PLUS))  op="+";
        else if (chk(p,TOK_MINUS)) op="-";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseMulDiv(p), line);
    }
    return lhs;
}
ASTNode *ParseShift(Parser *p) {
    ASTNode *lhs = ParseAddSub(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_LSHIFT)) op="<<";
        else if (chk(p,TOK_RSHIFT)) op=">>";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseAddSub(p), line);
    }
    return lhs;
}
ASTNode *ParseRelational(Parser *p) {
    ASTNode *lhs = ParseShift(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if      (chk(p,TOK_LT)) op="<";
        else if (chk(p,TOK_GT)) op=">";
        else if (chk(p,TOK_LE)) op="<=";
        else if (chk(p,TOK_GE)) op=">=";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseShift(p), line);
    }
    return lhs;
}
ASTNode *ParseEquality(Parser *p) {
    ASTNode *lhs = ParseRelational(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_EQ)) op="=="; else if (chk(p,TOK_NEQ)) op="!="; else break;
        adv(p); lhs = ast_binary(op, lhs, ParseRelational(p), line);
    }
    return lhs;
}
ASTNode *ParseBitAnd(Parser *p) {
    ASTNode *lhs = ParseEquality(p);
    while (chk(p,TOK_AMP)) { int l=cur(p).line; adv(p); lhs=ast_binary("&",lhs,ParseEquality(p),l); }
    return lhs;
}
ASTNode *ParseBitXor(Parser *p) {
    ASTNode *lhs = ParseBitAnd(p);
    while (chk(p,TOK_CARET)) { int l=cur(p).line; adv(p); lhs=ast_binary("^",lhs,ParseBitAnd(p),l); }
    return lhs;
}
ASTNode *ParseBitOr(Parser *p) {
    ASTNode *lhs = ParseBitXor(p);
    while (chk(p,TOK_PIPE)) { int l=cur(p).line; adv(p); lhs=ast_binary("|",lhs,ParseBitXor(p),l); }
    return lhs;
}
ASTNode *ParseLogicalAnd(Parser *p) {
    ASTNode *lhs = ParseBitOr(p);
    while (chk(p,TOK_AND)) { int l=cur(p).line; adv(p); lhs=ast_binary("&&",lhs,ParseBitOr(p),l); }
    return lhs;
}
ASTNode *ParseLogicalOr(Parser *p) {
    ASTNode *lhs = ParseLogicalAnd(p);
    while (chk(p,TOK_OR)) { int l=cur(p).line; adv(p); lhs=ast_binary("||",lhs,ParseLogicalAnd(p),l); }
    return lhs;
}
ASTNode *ParseTernary(Parser *p) {
    ASTNode *cond = ParseLogicalOr(p);
    if (chk(p,TOK_QUESTION)) {
        int line=cur(p).line; adv(p);
        ASTNode *then_ = ParseExpression(p);
        eat(p,TOK_COLON);
        ASTNode *else_ = ParseTernary(p);
        return ast_ternary(cond, then_, else_, line);
    }
    return cond;
}

ASTNode *ParseAssignment(Parser *p) {
    ASTNode *lhs = ParseTernary(p);
    int line = cur(p).line;
    const char *op = NULL;
    switch (cur(p).kind) {
        case TOK_ASSIGN:     op="=";   break;
        case TOK_PLUS_EQ:    op="+=";  break;
        case TOK_MINUS_EQ:   op="-=";  break;
        case TOK_STAR_EQ:    op="*=";  break;
        case TOK_SLASH_EQ:   op="/=";  break;
        case TOK_PERCENT_EQ: op="%=";  break;
        case TOK_AMP_EQ:     op="&=";  break;
        case TOK_PIPE_EQ:    op="|=";  break;
        case TOK_CARET_EQ:   op="^=";  break;
        case TOK_LSHIFT_EQ:  op="<<="; break;
        case TOK_RSHIFT_EQ:  op=">>="; break;
        default: return lhs;
    }
    adv(p);
    ASTNode *rhs = ParseAssignment(p); /* right-associative */
    return ast_assign(op, lhs, rhs, line);
}

ASTNode *ParseExpression(Parser *p) {
    ASTNode *e = ParseAssignment(p);
    /* comma operator */
    while (chk(p,TOK_COMMA)) {
        int line=cur(p).line; adv(p);
        ASTNode *r = ParseAssignment(p);
        e = ast_binary(",", e, r, line);
    }
    return e;
}

/* =========================================================================
 * ParseStatement
 * ========================================================================= */
ASTNode *ParseBlock(Parser *p) {
    int line=cur(p).line;
    eat(p,TOK_LBRACE);
    ASTNode *stmts[1024]; int count=0;
    symtable_push_scope(p->sym);
    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
        if (count<1024) stmts[count++]=ParseStatement(p);
        else { parse_error(p,"block too large"); break; }
    symtable_pop_scope(p->sym);
    eat(p,TOK_RBRACE);
    return ast_block(stmts, count, line);
}

ASTNode *ParseStatement(Parser *p) {
    int line = cur(p).line;
    /* Null statement: just a semicolon */
    if (chk(p,TOK_SEMICOLON)) { adv(p); return ast_number(0,line); }

    /* Local typedef inside a function body */
    if (chk(p,TOK_TYPEDEF)) {
        adv(p);
        TypeInfo *ti = ParseTypeSpecifier(p);
        while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
        if (chk(p,TOK_IDENT)) {
            char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
            eat(p,TOK_SEMICOLON);
            symtable_define_typedef(p->sym, alias, ti);
            return ast_typedef_decl(ti, alias, line);
        }
        eat(p,TOK_SEMICOLON);
        return ast_number(0,line);
    }

    if (chk(p,TOK_LBRACE)) return ParseBlock(p);

    if (chk(p,TOK_IF)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *cond = ParseExpression(p); eat(p,TOK_RPAREN);
        ASTNode *then_ = ParseStatement(p);
        ASTNode *else_ = NULL;
        if (chk(p,TOK_ELSE)) { adv(p); else_=ParseStatement(p); }
        return ast_if(cond, then_, else_, line);
    }

    if (chk(p,TOK_WHILE)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *cond=ParseExpression(p); eat(p,TOK_RPAREN);
        ASTNode *body=ParseStatement(p);
        return ast_while(cond, body, line);
    }

    if (chk(p,TOK_DO)) {
        adv(p);
        ASTNode *body=ParseStatement(p);
        eat(p,TOK_WHILE); eat(p,TOK_LPAREN);
        ASTNode *cond=ParseExpression(p); eat(p,TOK_RPAREN); eat(p,TOK_SEMICOLON);
        return ast_do_while(body, cond, line);
    }

    if (chk(p,TOK_FOR)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *init=NULL, *cond=NULL, *step=NULL;
        symtable_push_scope(p->sym);
        if (!chk(p,TOK_SEMICOLON)) {
            if (is_type_start(p)) init = ParseVariable(p); /* var decl — eats semicolon */
            else { init=ast_expr_stmt(ParseExpression(p),line); eat(p,TOK_SEMICOLON); }
        } else eat(p,TOK_SEMICOLON);
        if (!chk(p,TOK_SEMICOLON)) cond=ParseExpression(p);
        eat(p,TOK_SEMICOLON);
        if (!chk(p,TOK_RPAREN)) step=ParseExpression(p);
        eat(p,TOK_RPAREN);
        ASTNode *body=ParseStatement(p);
        symtable_pop_scope(p->sym);
        return ast_for(init, cond, step, body, line);
    }

    if (chk(p,TOK_SWITCH)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *expr=ParseExpression(p); eat(p,TOK_RPAREN);
        eat(p,TOK_LBRACE);
        ASTNode *cases[256]; int nc=0;
        while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
            if (chk(p,TOK_CASE)) {
                adv(p);
                long long val=0;
                /* check for enum value or number */
                if (chk(p,TOK_NUMBER)) { val=cur(p).ival; adv(p); }
                else if (chk(p,TOK_IDENT)) {
                    Symbol *s=symtable_lookup(p->sym,tok_ident(&cur(p)));
                    if (s&&s->kind==SYM_ENUM_VAL) val=s->enum_value;
                    else parse_error(p,"case value must be constant");
                    adv(p);
                } else if (chk(p,TOK_CHAR_LIT)) { val=cur(p).ival; adv(p); }
                else { parse_error(p,"expected case constant"); }
                eat(p,TOK_COLON);
                ASTNode *body[256]; int nb=0;
                while (!chk(p,TOK_CASE)&&!chk(p,TOK_DEFAULT)&&!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
                    body[nb++]=ParseStatement(p);
                if (nc<256) cases[nc++]=ast_case(val,body,nb,line);
            } else if (chk(p,TOK_DEFAULT)) {
                adv(p); eat(p,TOK_COLON);
                ASTNode *body[256]; int nb=0;
                while (!chk(p,TOK_CASE)&&!chk(p,TOK_DEFAULT)&&!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
                    body[nb++]=ParseStatement(p);
                if (nc<256) cases[nc++]=ast_default(body,nb,line);
            } else {
                parse_error(p,"expected 'case' or 'default' in switch");
                adv(p);
            }
        }
        eat(p,TOK_RBRACE);
        return ast_switch(expr, cases, nc, line);
    }

    if (chk(p,TOK_RETURN)) {
        adv(p);
        ASTNode *expr=NULL;
        if (!chk(p,TOK_SEMICOLON)) expr=ParseExpression(p);
        eat(p,TOK_SEMICOLON);
        return ast_return(expr, line);
    }

    if (chk(p,TOK_BREAK))    { adv(p); eat(p,TOK_SEMICOLON); return ast_break(line); }
    if (chk(p,TOK_CONTINUE)) { adv(p); eat(p,TOK_SEMICOLON); return ast_continue(line); }

    if (chk(p,TOK_GOTO)) {
        adv(p);
        if (!chk(p,TOK_IDENT)) { parse_error(p,"expected label after goto"); }
        char lbl[256]; strncpy(lbl,tok_ident(&cur(p)),sizeof lbl-1); adv(p);
        eat(p,TOK_SEMICOLON);
        return ast_goto(lbl, line);
    }

    /* Label: ident ':' statement  OR  typedef-name variable declaration */
    if (chk(p,TOK_IDENT)) {
        /* If this ident is a typedef name, treat as variable declaration */
        { Symbol *ts = symtable_lookup(p->sym, tok_ident(&cur(p)));
          if (ts && ts->kind==SYM_TYPEDEF) return ParseVariable(p); }
        /* peek one ahead to see if it's a label */
        Token saved = cur(p);
        char nm[256]; strncpy(nm,tok_ident(saved),sizeof nm-1);
        adv(p);
        if (chk(p,TOK_COLON)) {
            adv(p);
            ASTNode *stmt = ParseStatement(p);
            return ast_label(nm, stmt, line);
        }
        /* Not a label — reconstruct expression starting with the identifier */
        /* We consumed the ident, need to rebuild. Use ast_var and continue parsing */
        ASTNode *base = ast_var(nm, line);
        /* Finish any postfix */
        ASTNode *e = base;
        /* re-enter postfix with already-parsed base */
        for (;;) {
            int l2=cur(p).line;
            if (chk(p,TOK_LBRACKET)) { adv(p); ASTNode *idx=ParseExpression(p); eat(p,TOK_RBRACKET); e=ast_index(e,idx,l2); }
            else if (chk(p,TOK_DOT)) { adv(p); char fn[64]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p); e=ast_member(e,fn,0,l2); }
            else if (chk(p,TOK_ARROW)) { adv(p); char fn[64]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p); e=ast_member(e,fn,1,l2); }
            else if (chk(p,TOK_INC)) { adv(p); e=ast_unary("++",e,1,l2); }
            else if (chk(p,TOK_DEC)) { adv(p); e=ast_unary("--",e,1,l2); }
            else if (chk(p,TOK_LPAREN)) {
                adv(p); ASTNode *args[64]; int argc=0;
                if (!chk(p,TOK_RPAREN)) {
                    do {
                        if (argc<64) args[argc++]=ParseAssignment(p);
                    } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
                }
                eat(p,TOK_RPAREN);
                if (e->kind==AST_VAR) {
                    Symbol *sym=symtable_lookup(p->sym,e->var.name);
                    if (sym&&sym->kind==SYM_IMPORT&&sym->dll) {
                        char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,e->var.name);
                        symtable_add_import(p->sym,key);
                    }
                    e=ast_call(e->var.name,args,argc,l2);
                } else e=ast_fp_call(e,args,argc,l2);
            }
            else break;
        }
        /* Continue with binary/assign operators */
        /* Temporarily create a fake parser state by injecting 'e' as the lhs */
        /* We do this by calling a helper that takes a pre-parsed LHS */
        /* Actually: re-enter the assignment chain */
        int l2=cur(p).line;
        const char *aop=NULL;
        switch(cur(p).kind){
            case TOK_ASSIGN: aop="="; break; case TOK_PLUS_EQ: aop="+="; break;
            case TOK_MINUS_EQ: aop="-="; break; case TOK_STAR_EQ: aop="*="; break;
            case TOK_SLASH_EQ: aop="/="; break; case TOK_PERCENT_EQ: aop="%="; break;
            case TOK_AMP_EQ: aop="&="; break; case TOK_PIPE_EQ: aop="|="; break;
            case TOK_CARET_EQ: aop="^="; break;
            case TOK_LSHIFT_EQ: aop="<<="; break; case TOK_RSHIFT_EQ: aop=">>="; break;
            default: break;
        }
        if (aop) { adv(p); ASTNode *rhs=ParseAssignment(p); e=ast_assign(aop,e,rhs,l2); }
        eat(p,TOK_SEMICOLON);
        return ast_expr_stmt(e, line);
    }

    /* Variable declaration */
    if (is_type_start(p)) return ParseVariable(p);

    /* Expression statement */
    ASTNode *expr = ParseExpression(p);
    eat(p,TOK_SEMICOLON);
    return ast_expr_stmt(expr, line);
}

/* =========================================================================
 * ParseVariable — [storage] type name [[N]] [= init] ;
 * ========================================================================= */
ASTNode *ParseVariable(Parser *p) {
    int line = cur(p).line;
    char storage[32]="";
    if (chk(p,TOK_STATIC))   { strncpy(storage,"static",sizeof storage-1);   adv(p); }
    else if (chk(p,TOK_EXTERN))  { strncpy(storage,"extern",sizeof storage-1);   adv(p); }
    else if (chk(p,TOK_AUTO))    { strncpy(storage,"auto",sizeof storage-1);     adv(p); }
    else if (chk(p,TOK_REGISTER)){ strncpy(storage,"register",sizeof storage-1); adv(p); }
    else if (chk(p,TOK_CONST))   { strncpy(storage,"const",sizeof storage-1);    adv(p); }
    /* consume inline/volatile silently */
    while (chk(p,TOK_INLINE)||chk(p,TOK_VOLATILE)) adv(p);
    if (chk(p,TOK_STATIC)&&!storage[0]){strncpy(storage,"static",sizeof storage-1);adv(p);}
    while (chk(p,TOK_INLINE)||chk(p,TOK_VOLATILE)) adv(p);

    TypeInfo *type = ParseTypeSpecifier(p);
    if (!type) { parse_error(p,"expected type specifier"); return ast_number(0,line); }
    int base_ptr_depth = type->pointer_depth; /* save before first declarator's stars */

    /* Pointer stars directly after type */
    while (chk(p,TOK_STAR)) { adv(p); type->pointer_depth++; }

    /* Function pointer declaration: type (*name)(...) or type (*name[N])(...)
     * After the base type we see '(' '*' name ')' '(' params ')'
     * We parse this as a plain pointer variable of the return type.        */
    if (chk(p,TOK_LPAREN)) {
        adv(p); /* consume '(' */
        while (chk(p,TOK_STAR)) { adv(p); type->pointer_depth++; }
        /* optional name */
        char name[256]; name[0]='\0';
        if (chk(p,TOK_IDENT)) {
            strncpy(name, tok_ident(&cur(p)), sizeof name-1);
            adv(p);
        }
        /* optional array dimension: (*ops[3]) */
        int array_size = -1;
        if (chk(p,TOK_LBRACKET)) {
            adv(p);
            if (chk(p,TOK_NUMBER)) { array_size=(int)cur(p).ival; adv(p); }
            else array_size = 0;
            eat(p,TOK_RBRACKET);
        }
        eat(p,TOK_RPAREN);
        /* skip parameter list: (...) */
        if (chk(p,TOK_LPAREN)) {
            adv(p);
            int depth=1;
            while (!chk(p,TOK_EOF) && depth>0) {
                if (chk(p,TOK_LPAREN)) depth++;
                else if (chk(p,TOK_RPAREN)) depth--;
                adv(p);
            }
        }
        /* optional initialiser */
        ASTNode *init=NULL;
        if (chk(p,TOK_ASSIGN)) { adv(p); init=ParseAssignment(p); }
        eat(p,TOK_SEMICOLON);
        if (name[0]) {
            symtable_define_var(p->sym, name, type);
            return ast_var_decl(storage[0]?storage:NULL, type, name,
                                init, array_size, line);
        }
        return ast_number(0, line); /* anonymous — skip */
    }

    if (!chk(p,TOK_IDENT)) {
        parse_error(p,"expected variable name after type");
        eat(p,TOK_SEMICOLON);
        return ast_number(0,line);
    }
    char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);

    /* Array dimension */
    int array_size = -1;
    if (chk(p,TOK_LBRACKET)) {
        adv(p);
        if (chk(p,TOK_RBRACKET)) { array_size=0; }
        else { array_size=(int)cur(p).ival; eat(p,TOK_NUMBER); }
        eat(p,TOK_RBRACKET);
    }

    ASTNode *init = NULL;
    if (chk(p,TOK_ASSIGN)) {
        adv(p);
        if (chk(p,TOK_LBRACE)) {
            /* Array initialiser {a, b, c} */
            adv(p);
            ASTNode *elems[256]; int ne=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                elems[ne++]=ParseAssignment(p);
                if (!chk(p,TOK_COMMA)) break; adv(p);
            }
            eat(p,TOK_RBRACE);
            init = ast_block(elems, ne, line); /* reuse AST_BLOCK as initialiser list */
            /* If array size was unspecified (char x[] = {...}), infer from element count */
            if (array_size <= 0) array_size = ne;
        } else {
            init = ParseAssignment(p);
        }
    }
    /* Multi-declarator: "int a=1, *b=NULL, c[4];" — collect all into a block */
    if (chk(p, TOK_COMMA)) {
        /* Build a list of VarDecl nodes, one per declarator */
        ASTNode *decls[64]; int nd=0;
        symtable_define_var(p->sym, name, type);
        decls[nd++] = ast_var_decl(storage[0]?storage:NULL, type, name, init, array_size, line);
        while (chk(p, TOK_COMMA) && nd<63) {
            adv(p); /* consume ',' */
            /* Each additional declarator gets a fresh copy of the BASE type */
            if (!type) { parse_error(p,"internal: type is null before copy"); break; }
            TypeInfo *t2 = (TypeInfo*)calloc(1, sizeof(TypeInfo));
            if (!t2) { parse_error(p,"internal: calloc returned null"); break; }
            *t2 = *type;
            if (type->base) t2->base = my_strdup(type->base);
            t2->pointer_depth = base_ptr_depth;  /* reset to base, not first declarator's depth */
            while (chk(p, TOK_STAR)) { adv(p); t2->pointer_depth++; }
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected declarator name"); break; }
            char n2[256]; strncpy(n2,tok_ident(&cur(p)),sizeof n2-1); n2[sizeof n2-1]='\0'; adv(p);
            int arr2=-1;
            if (chk(p,TOK_LBRACKET)) {
                adv(p);
                if (chk(p,TOK_NUMBER)) { arr2=(int)cur(p).ival; adv(p); } else arr2=0;
                eat(p,TOK_RBRACKET);
            }
            ASTNode *init2=NULL;
            if (chk(p,TOK_ASSIGN)) { adv(p); init2=ParseAssignment(p); }
            symtable_define_var(p->sym, n2, t2);
            decls[nd++] = ast_var_decl(storage[0]?storage:NULL, t2, n2, init2, arr2, line);
        }
        eat(p,TOK_SEMICOLON);
        if (nd==1) return decls[0];
        return ast_block(decls, nd, line);
    }

    eat(p,TOK_SEMICOLON);

    symtable_define_var(p->sym, name, type);
    return ast_var_decl(storage[0]?storage:NULL, type, name, init, array_size, line);
}

/* =========================================================================
 * ParseFunction — type name ( params ) { body } or ;
 * ========================================================================= */
ASTNode *ParseFunction(Parser *p, const char *storage, TypeInfo *ret, const char *name) {
    int line=cur(p).line;
    eat(p,TOK_LPAREN);
    ASTNode *params[32]; int paramc=0; int variadic=0;
    symtable_push_scope(p->sym);
    symtable_reset_locals(p->sym);
    if (!chk(p,TOK_RPAREN)) {
        int param_byte_off=0; /* cumulative 32-bit param stack offset */
        do {
            if (chk(p,TOK_ELLIPSIS)) { variadic=1; adv(p); break; }
            if (!is_type_start(p)) { parse_error(p,"expected parameter type"); break; }
            TypeInfo *pt = ParseTypeSpecifier(p);
            /* "void f(void)" — single void param with no name = zero params in C */
            if (pt->base && strcmp(pt->base,"void")==0 && pt->pointer_depth==0
                && !chk(p,TOK_IDENT) && !chk(p,TOK_STAR) && chk(p,TOK_RPAREN)) {
                break; /* treat as empty param list */
            }
            /* check for function pointer param: type (*name)(params) */
            while (chk(p,TOK_STAR)) { adv(p); pt->pointer_depth++; }
            char pname[256]="";
            if (chk(p,TOK_IDENT)) { strncpy(pname,tok_ident(&cur(p)),sizeof pname-1); adv(p); }
            /* array param: name[] */
            if (chk(p,TOK_LBRACKET)) { adv(p); if (chk(p,TOK_RBRACKET)) adv(p); pt->pointer_depth++; }
            symtable_define_param(p->sym, pname, pt, paramc, param_byte_off);
            /* Advance cumulative offset by actual param size (doubles=8, rest=4) */
            { int psz = typeinfo_size(pt, p->sym->is_64bit); param_byte_off += (psz > 4 ? psz : 4); }
            params[paramc++]=ast_param(pt, pname[0]?pname:NULL, 0, cur(p).line);
        } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
    }
    eat(p,TOK_RPAREN);

    /* Register function in symbol table.
     * If already defined (e.g. forward declaration), don't re-add.
     * For a full definition, update func_node to point to the body node. */
    Symbol *existing_func = symtable_lookup(p->sym, name);
    if (!existing_func || existing_func->kind != SYM_FUNC) {
        symtable_define_func(p->sym, name, ret, paramc, NULL);
    }

    ASTNode *body=NULL;
    if (chk(p,TOK_LBRACE)) {
        body = ParseBlock(p);
    } else {
        eat(p,TOK_SEMICOLON); /* forward declaration */
        /* extern/forward declaration: register as import so calls use IAT */
        if (storage && strcmp(storage,"extern")==0) {
            /* Only register if not already a known Win API import */
            Symbol *existing = symtable_lookup(p->sym, name);
            if (!existing || existing->kind != SYM_IMPORT) {
                symtable_define_import(p->sym, name, "extern");
            }
        }
    }
    symtable_pop_scope(p->sym);
    return ast_func_decl(storage, ret, name, params, paramc, variadic, body, line);
}

/* =========================================================================
 * parse_program — top-level declarations
 * ========================================================================= */
ASTNode *parse_program(Parser *p) {
    ASTNode *decls[1024]; int count=0;
    while (!chk(p,TOK_EOF)) {
        int line=cur(p).line;

        /* struct / union definition at top level */
        if (chk(p,TOK_STRUCT)||chk(p,TOK_UNION)) {
            TypeInfo *ti = ParseTypeSpecifier(p);
            if (chk(p,TOK_SEMICOLON)) { adv(p); typeinfo_free(ti); continue; }
            /* struct Foo var; or struct Foo fn(...) */
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected identifier"); continue; }
            char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);
            if (chk(p,TOK_LPAREN)) decls[count++]=ParseFunction(p,NULL,ti,name);
            else {
                /* global var */
                int arr=-1;
                if (chk(p,TOK_LBRACKET)) { adv(p); if (chk(p,TOK_NUMBER)){ arr=(int)cur(p).ival; adv(p); } eat(p,TOK_RBRACKET); }
                ASTNode *init=NULL;
                if (chk(p,TOK_ASSIGN)){
                    adv(p);
                    if (chk(p,TOK_LBRACE)) {
                        /* Brace-enclosed initializer: { val, val, ... } or { {v,v}, ... } */
                        adv(p); /* consume { */
                        ASTNode *elems[1024]; int ne=0;
                        while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne<1023) {
                            if (chk(p,TOK_LBRACE)) {
                                /* nested brace: struct literal - collect until } */
                                adv(p);
                                while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne<1023) {
                                    elems[ne++]=ParseAssignment(p);
                                    if (!chk(p,TOK_COMMA)) break; adv(p);
                                }
                                eat(p,TOK_RBRACE);
                            } else {
                                elems[ne++]=ParseAssignment(p);
                            }
                            if (!chk(p,TOK_COMMA)) break; adv(p);
                        }
                        eat(p,TOK_RBRACE);
                        init = ast_block(elems, ne, line);
                    } else {
                        init=ParseAssignment(p);
                    }
                }
                eat(p,TOK_SEMICOLON);
                symtable_define_global(p->sym,name,ti,arr);
                decls[count++]=ast_var_decl(NULL,ti,name,init,arr,line);
            }
            continue;
        }

        /* enum */
        if (chk(p,TOK_ENUM)) {
            TypeInfo *ti = ParseTypeSpecifier(p); /* handles enum body */
            if (chk(p,TOK_SEMICOLON)) { adv(p); typeinfo_free(ti); continue; }
            /* enum Foo var; */
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            if (chk(p,TOK_IDENT)) {
                char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);
                ASTNode *init=NULL;
                if (chk(p,TOK_ASSIGN)){adv(p); init=ParseAssignment(p);}
                eat(p,TOK_SEMICOLON);
                symtable_define_global(p->sym,name,ti,-1);
                decls[count++]=ast_var_decl(NULL,ti,name,init,-1,line);
            } else { eat(p,TOK_SEMICOLON); typeinfo_free(ti); }
            continue;
        }

        /* typedef */
        if (chk(p,TOK_TYPEDEF)) {
            adv(p);
            TypeInfo *ti = ParseTypeSpecifier(p);
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected typedef name"); eat(p,TOK_SEMICOLON); continue; }
            char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
            eat(p,TOK_SEMICOLON);
            symtable_define_typedef(p->sym, alias, ti);
            decls[count++]=ast_typedef_decl(ti, alias, line);
            continue;
        }

        /* storage class */
        char storage[32]="";
        if (chk(p,TOK_STATIC))  { strncpy(storage,"static",sizeof storage-1);  adv(p); }
        else if (chk(p,TOK_EXTERN)) { strncpy(storage,"extern",sizeof storage-1); adv(p); }
        /* consume inline/const/volatile qualifiers silently */
        while (chk(p,TOK_INLINE)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) adv(p);
        /* also handle 'static inline' or 'inline static' */
        if (chk(p,TOK_STATIC)&&!storage[0]) { strncpy(storage,"static",sizeof storage-1); adv(p); }
        while (chk(p,TOK_INLINE)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) adv(p);

        if (!is_type_start(p)) {
            { char _em[128]; snprintf(_em,sizeof _em,"expected declaration at top level, got '%s'",token_kind_name(cur(p).kind)); parse_error(p,_em); }
            adv(p); continue;
        }

        TypeInfo *ret = ParseTypeSpecifier(p);
        while (chk(p,TOK_STAR)) { adv(p); ret->pointer_depth++; }

        if (!chk(p,TOK_IDENT)) {
            parse_error(p,"expected identifier");
            eat(p,TOK_SEMICOLON); continue;
        }
        char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);

        if (chk(p,TOK_LPAREN)) {
            decls[count++] = ParseFunction(p, storage[0]?storage:NULL, ret, name);
        } else {
            /* global variable */
            int arr=-1;
            if (chk(p,TOK_LBRACKET)) {
                adv(p);
                if (chk(p,TOK_NUMBER)) { arr=(int)cur(p).ival; adv(p); } else arr=0;
                eat(p,TOK_RBRACKET);
            }
            ASTNode *init=NULL;
            if (chk(p,TOK_ASSIGN)) {
                adv(p);
                if (chk(p,TOK_LBRACE)) {
                    /* Braced global initializer: arr = {a,b} or = {{a,b},{c,d}} */
                    adv(p);
                    ASTNode *elems[1024]; int ne=0;
                    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne<1023) {
                        if (chk(p,TOK_LBRACE)) {
                            adv(p);
                            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne<1023) {
                                elems[ne++]=ParseAssignment(p);
                                if (!chk(p,TOK_COMMA)) break; adv(p);
                            }
                            eat(p,TOK_RBRACE);
                        } else {
                            elems[ne++]=ParseAssignment(p);
                        }
                        if (!chk(p,TOK_COMMA)) break; adv(p);
                    }
                    eat(p,TOK_RBRACE);
                    init = ast_block(elems, ne, line);
                    /* Infer array size from initializer if unspecified */
                    if (arr <= 0) arr = ne;
                } else {
                    init=ParseAssignment(p);
                }
            }
            eat(p,TOK_SEMICOLON);
            if (arr > 0) ret->array_size = arr;
            symtable_define_global(p->sym, name, ret, arr);
            decls[count++]=ast_var_decl(storage[0]?storage:NULL, ret, name, init, arr, line);
        }
    }
    return ast_program(decls, count, 1);
}
