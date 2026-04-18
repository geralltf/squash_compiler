#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* my_strdup(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char* dest = malloc(len);
    if (dest == NULL) return NULL;
    memcpy(dest, src, len);
    return dest;
}


/* =========================================================================
 * TypeInfo
 * ========================================================================= */
TypeInfo *typeinfo_new(const char *base) {
    TypeInfo *t = calloc(1, sizeof(TypeInfo));
    t->base = my_strdup(base ? base : "int");
    t->array_size = -1;
    return t;
}
TypeInfo *typeinfo_ptr(TypeInfo *base) {
    TypeInfo *t = calloc(1, sizeof(TypeInfo));
    if (base) {
        t->base          = my_strdup(base->base);
        t->pointer_depth = base->pointer_depth + 1;
        t->is_const      = base->is_const;
        t->is_unsigned   = base->is_unsigned;
    } else {
        t->base = my_strdup("void");
        t->pointer_depth = 1;
    }
    t->array_size = -1;
    return t;
}
void typeinfo_free(TypeInfo *t) {
    if (!t) return;
    free(t->base);
    typeinfo_free(t->pointed_to);
    free(t);
}
char *typeinfo_str(const TypeInfo *t) {
    if (!t) return my_strdup("?");
    char buf[256];
    snprintf(buf, sizeof buf, "%s%s",
             t->is_unsigned?"unsigned ":"", t->base);
    for (int i=0;i<t->pointer_depth;i++) strncat(buf,"*",sizeof buf-strlen(buf)-1);
    return my_strdup(buf);
}
int typeinfo_size(const TypeInfo *t, int is_64bit) {
    if (!t) return 4;
    if (t->pointer_depth > 0) return is_64bit ? 8 : 4;
    const char *b = t->base;
    if (!b) return 4;
    /* Strip leading unsigned/signed qualifiers for size computation */
    if (strncmp(b,"unsigned ",9)==0) b+=9;
    else if (strncmp(b,"signed ",7)==0) b+=7;
    /* 1-byte types */
    if (strcmp(b,"char")==0) return 1;
    if (strcmp(b,"uint8_t")==0) return 1;
    if (strcmp(b,"int8_t")==0) return 1;
    if (strcmp(b,"uint_least8_t")==0) return 1;
    if (strcmp(b,"int_least8_t")==0) return 1;
    /* bool/_Bool: our stdbool.h defines bool as typedef int (4 bytes) */
    if (strcmp(b,"bool")==0) return 4;
    if (strcmp(b,"_Bool")==0) return 4;
    /* 2-byte types */
    if (strcmp(b,"short")==0) return 2;
    if (strcmp(b,"int16_t")==0) return 2;
    if (strcmp(b,"uint16_t")==0) return 2;
    if (strcmp(b,"uint_least16_t")==0) return 2;
    if (strcmp(b,"int_least16_t")==0) return 2;
    /* 4-byte types */
    if (strcmp(b,"int")==0) return 4;
    if (strcmp(b,"uint")==0) return 4;
    if (strcmp(b,"uint32_t")==0) return 4;
    if (strcmp(b,"int32_t")==0) return 4;
    if (strcmp(b,"uint_least32_t")==0) return 4;
    if (strcmp(b,"int_least32_t")==0) return 4;
    if (strcmp(b,"intptr_t")==0) return 4;
    if (strcmp(b,"uintptr_t")==0) return 4;
    if (strcmp(b,"float")==0) return 4;
    if (strcmp(b,"long")==0) return is_64bit ? 8 : 4;
    /* 8-byte types */
    if (strcmp(b,"long long")==0) return 8;
    if (strcmp(b,"int64_t")==0) return 8;
    if (strcmp(b,"uint64_t")==0) return 8;
    if (strcmp(b,"ulong")==0) return 8;
    if (strcmp(b,"uint_least64_t")==0) return 8;
    if (strcmp(b,"int_least64_t")==0) return 8;
    if (strcmp(b,"intmax_t")==0) return 8;
    if (strcmp(b,"uintmax_t")==0) return 8;
    if (strcmp(b,"time_t")==0) return 8;
    if (strcmp(b,"clock_t")==0) return 8;
    if (strcmp(b,"double")==0) return 8;
    if (strcmp(b,"long double")==0) return 8;
    if (strcmp(b,"void")==0) return 0;
    return 4; /* default for structs etc */
}

/* Returns 1 if the type is a floating-point type */
TypeInfo *typeinfo_copy(const TypeInfo *t) {
    if (!t) return NULL;
    TypeInfo *c = calloc(1, sizeof(TypeInfo));
    *c = *t;  /* shallow copy fields */
    if (t->base) c->base = my_strdup(t->base);
    /* pointed_t is shared (not deep-copied) which is fine for our use */
    return c;
}

int typeinfo_is_float(const TypeInfo *t) {
    if (!t || !t->base || t->pointer_depth > 0) return 0;
    const char *b = t->base;
    return strcmp(b,"float")==0 || strcmp(b,"double")==0 ||
           strcmp(b,"long double")==0;
}

/* Returns 1 if the type is unsigned */
int typeinfo_is_unsigned(const TypeInfo *t) {
    if (!t || !t->base) return 0;
    const char *b = t->base;
    return strncmp(b,"unsigned ",9)==0 || strcmp(b,"uint")==0 ||
           strcmp(b,"ulong")==0 || strcmp(b,"bool")==0 ||
           strcmp(b,"_Bool")==0 || strcmp(b,"uint8_t")==0 ||
           strcmp(b,"uint16_t")==0 || strcmp(b,"uint32_t")==0 ||
           strcmp(b,"uint64_t")==0;
}

/* =========================================================================
 * Allocator
 * ========================================================================= */
static ASTNode *an(ASTKind k, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (!n) { perror("calloc"); exit(1); }
    n->kind = k; n->line = line;
    return n;
}

/* =========================================================================
 * Constructors
 * ========================================================================= */
ASTNode *ast_number    (long long v, int line) { ASTNode *n=an(AST_NUMBER,line); n->num.value=v; return n; }
ASTNode *ast_float     (double v, int line)    { ASTNode *n=an(AST_FLOAT,line);  n->fnum.value=v; return n; }
ASTNode *ast_string    (const char *v, int line){ ASTNode *n=an(AST_STRING,line); n->str.value=my_strdup(v?v:""); return n; }
ASTNode *ast_char_lit  (long long v, int line)  { ASTNode *n=an(AST_CHAR_LIT,line); n->char_lit.value=v; return n; }
ASTNode *ast_var       (const char *name, int line){ ASTNode *n=an(AST_VAR,line); n->var.name=my_strdup(name); return n; }

ASTNode *ast_unary(const char *op, ASTNode *operand, int post, int line) {
    ASTNode *n=an(AST_UNARY,line);
    strncpy(n->unary.op,op,3); n->unary.operand=operand; n->unary.post=post; return n;
}
ASTNode *ast_binary(const char *op, ASTNode *left, ASTNode *right, int line) {
    ASTNode *n=an(AST_BINARY,line);
    strncpy(n->binary.op,op,3); n->binary.left=left; n->binary.right=right; return n;
}
ASTNode *ast_assign(const char *op, ASTNode *lhs, ASTNode *rhs, int line) {
    ASTNode *n=an(AST_ASSIGN,line);
    strncpy(n->assign.op,op,3); n->assign.lhs=lhs; n->assign.rhs=rhs; return n;
}
ASTNode *ast_ternary(ASTNode *cond, ASTNode *then_, ASTNode *else_, int line) {
    ASTNode *n=an(AST_TERNARY,line); n->ternary.cond=cond; n->ternary.then_=then_; n->ternary.else_=else_; return n;
}
ASTNode *ast_cast(TypeInfo *type, ASTNode *expr, int line) {
    ASTNode *n=an(AST_CAST,line); n->cast.type=type; n->cast.expr=expr; return n;
}
ASTNode *ast_sizeof_expr(ASTNode *expr, int line) {
    ASTNode *n=an(AST_SIZEOF_EXPR,line); n->sizeof_expr.expr=expr; return n;
}
ASTNode *ast_sizeof_type(TypeInfo *type, int line) {
    ASTNode *n=an(AST_SIZEOF_TYPE,line); n->sizeof_type.type=type; return n;
}
ASTNode *ast_index(ASTNode *array, ASTNode *index, int line) {
    ASTNode *n=an(AST_INDEX,line); n->index.array=array; n->index.index=index; return n;
}
ASTNode *ast_member(ASTNode *obj, const char *field, int arrow, int line) {
    ASTNode *n=an(AST_MEMBER,line); n->member.obj=obj; n->member.field=my_strdup(field); n->member.arrow=arrow; return n;
}
ASTNode *ast_addr(ASTNode *operand, int line) {
    ASTNode *n=an(AST_ADDR,line); n->addr.operand=operand; return n;
}
ASTNode *ast_deref(ASTNode *operand, int line) {
    ASTNode *n=an(AST_DEREF,line); n->deref.operand=operand; return n;
}
ASTNode *ast_fp_call(ASTNode *func_expr, ASTNode **args, int argc, int line) {
    ASTNode *n=an(AST_FUNC_PTR_CALL,line);
    n->fp_call.func_expr=func_expr; n->fp_call.argc=argc;
    if (argc>0) { n->fp_call.args=malloc(argc*sizeof(ASTNode*)); memcpy(n->fp_call.args,args,argc*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_call(const char *name, ASTNode **args, int argc, int line) {
    ASTNode *n=an(AST_CALL,line);
    n->call.name=my_strdup(name); n->call.argc=argc;
    if (argc>0) { n->call.args=malloc(argc*sizeof(ASTNode*)); memcpy(n->call.args,args,argc*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_if(ASTNode *cond, ASTNode *then_, ASTNode *else_, int line) {
    ASTNode *n=an(AST_IF,line); n->if_.cond=cond; n->if_.then_=then_; n->if_.else_=else_; return n;
}
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line) {
    ASTNode *n=an(AST_WHILE,line); n->while_.cond=cond; n->while_.body=body; return n;
}
ASTNode *ast_do_while(ASTNode *body, ASTNode *cond, int line) {
    ASTNode *n=an(AST_DO_WHILE,line); n->do_while.body=body; n->do_while.cond=cond; return n;
}
ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *step, ASTNode *body, int line) {
    ASTNode *n=an(AST_FOR,line); n->for_.init=init; n->for_.cond=cond; n->for_.step=step; n->for_.body=body; return n;
}
ASTNode *ast_switch(ASTNode *expr, ASTNode **cases, int nc, int line) {
    ASTNode *n=an(AST_SWITCH,line); n->switch_.expr=expr; n->switch_.nc=nc;
    if (nc>0) { n->switch_.cases=malloc(nc*sizeof(ASTNode*)); memcpy(n->switch_.cases,cases,nc*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_case(long long value, ASTNode **body, int nb, int line) {
    ASTNode *n=an(AST_CASE,line); n->case_.value=value; n->case_.is_default=0; n->case_.nb=nb;
    if (nb>0) { n->case_.body=malloc(nb*sizeof(ASTNode*)); memcpy(n->case_.body,body,nb*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_default(ASTNode **body, int nb, int line) {
    ASTNode *n=an(AST_DEFAULT,line); n->default_.nb=nb;
    if (nb>0) { n->default_.body=malloc(nb*sizeof(ASTNode*)); memcpy(n->default_.body,body,nb*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_return   (ASTNode *expr, int line) { ASTNode *n=an(AST_RETURN,line); n->ret.expr=expr; return n; }
ASTNode *ast_break    (int line) { return an(AST_BREAK,line); }
ASTNode *ast_continue (int line) { return an(AST_CONTINUE,line); }
ASTNode *ast_goto     (const char *label, int line) { ASTNode *n=an(AST_GOTO,line); n->goto_.label=my_strdup(label); return n; }
ASTNode *ast_label    (const char *name, ASTNode *stmt, int line) {
    ASTNode *n=an(AST_LABEL,line); n->label.name=my_strdup(name); n->label.stmt=stmt; return n;
}
ASTNode *ast_expr_stmt(ASTNode *expr, int line) { ASTNode *n=an(AST_EXPR_STMT,line); n->expr_stmt.expr=expr; return n; }
ASTNode *ast_var_decl(const char *storage, TypeInfo *type, const char *name,
                      ASTNode *init, int array_size, int line) {
    ASTNode *n=an(AST_VAR_DECL,line);
    n->var_decl.storage=storage?my_strdup(storage):NULL;
    n->var_decl.type=type; n->var_decl.name=my_strdup(name);
    n->var_decl.init=init; n->var_decl.array_size=array_size;
    return n;
}
ASTNode *ast_block(ASTNode **stmts, int count, int line) {
    ASTNode *n=an(AST_BLOCK,line); n->block.count=count;
    if (count>0) { n->block.stmts=malloc(count*sizeof(ASTNode*)); memcpy(n->block.stmts,stmts,count*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_func_decl(const char *storage, TypeInfo *ret_type, const char *name,
                        ASTNode **params, int paramc, int variadic,
                        ASTNode *body, int line) {
    ASTNode *n=an(AST_FUNC_DECL,line);
    n->func.storage=storage?my_strdup(storage):NULL;
    n->func.ret_type=ret_type; n->func.name=my_strdup(name);
    n->func.paramc=paramc; n->func.is_variadic=variadic; n->func.body=body;
    if (paramc>0) { n->func.params=malloc(paramc*sizeof(ASTNode*)); memcpy(n->func.params,params,paramc*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_param(TypeInfo *type, const char *name, int variadic, int line) {
    ASTNode *n=an(AST_PARAM,line);
    n->param.type=type; n->param.name=name?my_strdup(name):NULL; n->param.is_variadic=variadic;
    return n;
}
ASTNode *ast_struct_decl(const char *name, int is_union, ASTNode **fields, int nf, int line) {
    ASTNode *n=an(AST_STRUCT_DECL,line);
    n->struct_decl.name=name?my_strdup(name):NULL; n->struct_decl.is_union=is_union;
    n->struct_decl.nfields=nf;
    if (nf>0) { n->struct_decl.fields=malloc(nf*sizeof(ASTNode*)); memcpy(n->struct_decl.fields,fields,nf*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_field(TypeInfo *type, const char *name, int array_size, int line) {
    ASTNode *n=an(AST_FIELD,line);
    n->field.type=type; n->field.name=my_strdup(name); n->field.array_size=array_size;
    return n;
}
ASTNode *ast_enum_decl(const char *name, ASTNode **vals, int nv, int line) {
    ASTNode *n=an(AST_ENUM_DECL,line);
    n->enum_decl.name=name?my_strdup(name):NULL; n->enum_decl.nvals=nv;
    if (nv>0) { n->enum_decl.vals=malloc(nv*sizeof(ASTNode*)); memcpy(n->enum_decl.vals,vals,nv*sizeof(ASTNode*)); }
    return n;
}
ASTNode *ast_enum_val(const char *name, long long value, int has_value, int line) {
    ASTNode *n=an(AST_ENUM_VAL,line);
    n->enum_val.name=my_strdup(name); n->enum_val.value=value; n->enum_val.has_value=has_value;
    return n;
}
ASTNode *ast_typedef_decl(TypeInfo *type, const char *name, int line) {
    ASTNode *n=an(AST_TYPEDEF_DECL,line); n->typedef_decl.type=type; n->typedef_decl.name=my_strdup(name); return n;
}
ASTNode *ast_program(ASTNode **decls, int count, int line) {
    ASTNode *n=an(AST_PROGRAM,line); n->program.count=count;
    if (count>0) { n->program.decls=malloc(count*sizeof(ASTNode*)); memcpy(n->program.decls,decls,count*sizeof(ASTNode*)); }
    return n;
}

/* =========================================================================
 * Pretty printer
 * ========================================================================= */
static void ind(int d) { for(int i=0;i<d;i++) printf("  "); }

void ast_print(const ASTNode *n, int depth) {
    if (!n) { ind(depth); printf("(null)\n"); return; }
    ind(depth);
    switch (n->kind) {
    case AST_NUMBER:    printf("Number(%lld)\n",n->num.value); break;
    case AST_FLOAT:     printf("Float(%g)\n",n->fnum.value); break;
    case AST_STRING:    printf("String(\"%s\")\n",n->str.value); break;
    case AST_CHAR_LIT:  printf("Char('%c')\n",(char)n->char_lit.value); break;
    case AST_VAR:       printf("Var(%s)\n",n->var.name); break;
    case AST_UNARY:     printf("Unary(%s%s)\n",n->unary.post?"post":"",n->unary.op); ast_print(n->unary.operand,depth+1); break;
    case AST_BINARY:    printf("Binary(%s)\n",n->binary.op); ast_print(n->binary.left,depth+1); ast_print(n->binary.right,depth+1); break;
    case AST_ASSIGN:    printf("Assign(%s)\n",n->assign.op); ast_print(n->assign.lhs,depth+1); ast_print(n->assign.rhs,depth+1); break;
    case AST_TERNARY:   printf("Ternary\n"); ast_print(n->ternary.cond,depth+1); ast_print(n->ternary.then_,depth+1); ast_print(n->ternary.else_,depth+1); break;
    case AST_CAST:      { char *ts=typeinfo_str(n->cast.type); printf("Cast(%s)\n",ts); free(ts); ast_print(n->cast.expr,depth+1); break; }
    case AST_SIZEOF_EXPR: printf("SizeofExpr\n"); ast_print(n->sizeof_expr.expr,depth+1); break;
    case AST_SIZEOF_TYPE: { char *ts=typeinfo_str(n->sizeof_type.type); printf("SizeofType(%s)\n",ts); free(ts); break; }
    case AST_INDEX:     printf("Index\n"); ast_print(n->index.array,depth+1); ast_print(n->index.index,depth+1); break;
    case AST_MEMBER:    printf("Member(%s%s)\n",n->member.arrow?"->":".",n->member.field); ast_print(n->member.obj,depth+1); break;
    case AST_ADDR:      printf("Addr\n"); ast_print(n->addr.operand,depth+1); break;
    case AST_DEREF:     printf("Deref\n"); ast_print(n->deref.operand,depth+1); break;
    case AST_CALL:      printf("Call(%s,%d)\n",n->call.name,n->call.argc); for(int i=0;i<n->call.argc;i++) ast_print(n->call.args[i],depth+1); break;
    case AST_FUNC_PTR_CALL: printf("FPCall(%d args)\n",n->fp_call.argc); ast_print(n->fp_call.func_expr,depth+1); for(int i=0;i<n->fp_call.argc;i++) ast_print(n->fp_call.args[i],depth+1); break;
    case AST_IF:        printf("If\n"); ast_print(n->if_.cond,depth+1); ast_print(n->if_.then_,depth+1); if(n->if_.else_) ast_print(n->if_.else_,depth+1); break;
    case AST_WHILE:     printf("While\n"); ast_print(n->while_.cond,depth+1); ast_print(n->while_.body,depth+1); break;
    case AST_DO_WHILE:  printf("DoWhile\n"); ast_print(n->do_while.body,depth+1); ast_print(n->do_while.cond,depth+1); break;
    case AST_FOR:       printf("For\n"); ast_print(n->for_.init,depth+1); ast_print(n->for_.cond,depth+1); ast_print(n->for_.step,depth+1); ast_print(n->for_.body,depth+1); break;
    case AST_SWITCH:    printf("Switch(%d cases)\n",n->switch_.nc); ast_print(n->switch_.expr,depth+1); for(int i=0;i<n->switch_.nc;i++) ast_print(n->switch_.cases[i],depth+1); break;
    case AST_CASE:      printf("Case(%lld, %d stmts)\n",n->case_.value,n->case_.nb); for(int i=0;i<n->case_.nb;i++) ast_print(n->case_.body[i],depth+1); break;
    case AST_DEFAULT:   printf("Default(%d stmts)\n",n->default_.nb); for(int i=0;i<n->default_.nb;i++) ast_print(n->default_.body[i],depth+1); break;
    case AST_RETURN:    printf("Return\n"); if(n->ret.expr) ast_print(n->ret.expr,depth+1); break;
    case AST_BREAK:     printf("Break\n"); break;
    case AST_CONTINUE:  printf("Continue\n"); break;
    case AST_GOTO:      printf("Goto(%s)\n",n->goto_.label); break;
    case AST_LABEL:     printf("Label(%s)\n",n->label.name); ast_print(n->label.stmt,depth+1); break;
    case AST_EXPR_STMT: printf("ExprStmt\n"); ast_print(n->expr_stmt.expr,depth+1); break;
    case AST_VAR_DECL:  { char *ts=typeinfo_str(n->var_decl.type); printf("VarDecl(%s %s arr=%d)\n",ts,n->var_decl.name,n->var_decl.array_size); free(ts); if(n->var_decl.init) ast_print(n->var_decl.init,depth+1); break; }
    case AST_BLOCK:     printf("Block(%d)\n",n->block.count); for(int i=0;i<n->block.count;i++) ast_print(n->block.stmts[i],depth+1); break;
    case AST_FUNC_DECL: { char *ts=typeinfo_str(n->func.ret_type); printf("FuncDecl(%s %s, %d params%s)\n",ts,n->func.name,n->func.paramc,n->func.is_variadic?", ...":""); free(ts); for(int i=0;i<n->func.paramc;i++) ast_print(n->func.params[i],depth+1); if(n->func.body) ast_print(n->func.body,depth+1); break; }
    case AST_PARAM:     { char *ts=typeinfo_str(n->param.type); printf("Param(%s %s)\n",ts,n->param.name?n->param.name:"?"); free(ts); break; }
    case AST_STRUCT_DECL: printf("Struct(%s,%d fields)\n",n->struct_decl.name?n->struct_decl.name:"anon",n->struct_decl.nfields); for(int i=0;i<n->struct_decl.nfields;i++) ast_print(n->struct_decl.fields[i],depth+1); break;
    case AST_FIELD:     { char *ts=typeinfo_str(n->field.type); printf("Field(%s %s)\n",ts,n->field.name); free(ts); break; }
    case AST_ENUM_DECL: printf("Enum(%s,%d)\n",n->enum_decl.name?n->enum_decl.name:"anon",n->enum_decl.nvals); for(int i=0;i<n->enum_decl.nvals;i++) ast_print(n->enum_decl.vals[i],depth+1); break;
    case AST_ENUM_VAL:  printf("EnumVal(%s=%lld)\n",n->enum_val.name,n->enum_val.value); break;
    case AST_TYPEDEF_DECL: { char *ts=typeinfo_str(n->typedef_decl.type); printf("Typedef(%s = %s)\n",n->typedef_decl.name,ts); free(ts); break; }
    case AST_PROGRAM:   printf("Program(%d)\n",n->program.count); for(int i=0;i<n->program.count;i++) ast_print(n->program.decls[i],depth+1); break;
    default: printf("??(%d)\n",n->kind);
    }
}

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
    case AST_STRING:    free(n->str.value); break;
    case AST_VAR:       free(n->var.name); break;
    case AST_UNARY:     ast_free(n->unary.operand); break;
    case AST_BINARY:    ast_free(n->binary.left); ast_free(n->binary.right); break;
    case AST_ASSIGN:    ast_free(n->assign.lhs); ast_free(n->assign.rhs); break;
    case AST_TERNARY:   ast_free(n->ternary.cond); ast_free(n->ternary.then_); ast_free(n->ternary.else_); break;
    case AST_CAST:      typeinfo_free(n->cast.type); ast_free(n->cast.expr); break;
    case AST_SIZEOF_EXPR: ast_free(n->sizeof_expr.expr); break;
    case AST_SIZEOF_TYPE: typeinfo_free(n->sizeof_type.type); break;
    case AST_INDEX:     ast_free(n->index.array); ast_free(n->index.index); break;
    case AST_MEMBER:    ast_free(n->member.obj); free(n->member.field); break;
    case AST_ADDR:      ast_free(n->addr.operand); break;
    case AST_DEREF:     ast_free(n->deref.operand); break;
    case AST_FUNC_PTR_CALL: ast_free(n->fp_call.func_expr); for(int i=0;i<n->fp_call.argc;i++) ast_free(n->fp_call.args[i]); free(n->fp_call.args); break;
    case AST_CALL:      free(n->call.name); for(int i=0;i<n->call.argc;i++) ast_free(n->call.args[i]); free(n->call.args); break;
    case AST_IF:        ast_free(n->if_.cond); ast_free(n->if_.then_); ast_free(n->if_.else_); break;
    case AST_WHILE:     ast_free(n->while_.cond); ast_free(n->while_.body); break;
    case AST_DO_WHILE:  ast_free(n->do_while.body); ast_free(n->do_while.cond); break;
    case AST_FOR:       ast_free(n->for_.init); ast_free(n->for_.cond); ast_free(n->for_.step); ast_free(n->for_.body); break;
    case AST_SWITCH:    ast_free(n->switch_.expr); for(int i=0;i<n->switch_.nc;i++) ast_free(n->switch_.cases[i]); free(n->switch_.cases); break;
    case AST_CASE:      for(int i=0;i<n->case_.nb;i++) ast_free(n->case_.body[i]); free(n->case_.body); break;
    case AST_DEFAULT:   for(int i=0;i<n->default_.nb;i++) ast_free(n->default_.body[i]); free(n->default_.body); break;
    case AST_RETURN:    ast_free(n->ret.expr); break;
    case AST_GOTO:      free(n->goto_.label); break;
    case AST_LABEL:     free(n->label.name); ast_free(n->label.stmt); break;
    case AST_EXPR_STMT: ast_free(n->expr_stmt.expr); break;
    case AST_VAR_DECL:  free(n->var_decl.storage); typeinfo_free(n->var_decl.type); free(n->var_decl.name); ast_free(n->var_decl.init); break;
    case AST_BLOCK:     for(int i=0;i<n->block.count;i++) ast_free(n->block.stmts[i]); free(n->block.stmts); break;
    case AST_FUNC_DECL: free(n->func.storage); typeinfo_free(n->func.ret_type); free(n->func.name); for(int i=0;i<n->func.paramc;i++) ast_free(n->func.params[i]); free(n->func.params); ast_free(n->func.body); break;
    case AST_PARAM:     typeinfo_free(n->param.type); free(n->param.name); break;
    case AST_STRUCT_DECL: free(n->struct_decl.name); for(int i=0;i<n->struct_decl.nfields;i++) ast_free(n->struct_decl.fields[i]); free(n->struct_decl.fields); break;
    case AST_FIELD:     typeinfo_free(n->field.type); free(n->field.name); break;
    case AST_ENUM_DECL: free(n->enum_decl.name); for(int i=0;i<n->enum_decl.nvals;i++) ast_free(n->enum_decl.vals[i]); free(n->enum_decl.vals); break;
    case AST_ENUM_VAL:  free(n->enum_val.name); break;
    case AST_TYPEDEF_DECL: typeinfo_free(n->typedef_decl.type); free(n->typedef_decl.name); break;
    case AST_PROGRAM:   for(int i=0;i<n->program.count;i++) ast_free(n->program.decls[i]); free(n->program.decls); break;
    default: break;
    }
    free(n);
}
