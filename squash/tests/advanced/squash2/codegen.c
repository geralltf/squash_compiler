#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strdup _strdup
#endif

/* =========================================================================
 * Internal Win32 function shims — malloc/free/memcpy etc backed by
 * HeapAlloc / HeapFree / kernel routines
 * ========================================================================= */
static const char *INTERNAL_SHIMS[] = {
    "malloc","calloc","realloc","free",
    "memcpy","memset","memcmp","memmove",
    "strlen","strcpy","strncpy","strcmp",
    "strncmp","strcat","strncat","strchr","strstr",
    "atoi","atol","sprintf","printf","puts","putchar","abort",
    NULL
};
static int is_internal_shim(const char *name) {
    for (int i=0;INTERNAL_SHIMS[i];i++)
        if (strcmp(INTERNAL_SHIMS[i],name)==0) return 1;
    return 0;
}

/* =========================================================================
 * Init
 * ========================================================================= */
void codegen_init(CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit) {
    memset(cg,0,sizeof *cg);
    cg->asm_=a; cg->sym=sym; cg->is_64bit=is_64bit;
    cg->loop_end_label=-1; cg->loop_top_label=-1; cg->switch_end_label=-1;
    cg->string_cap=32; cg->strings=malloc(cg->string_cap*sizeof(StringEntry));
    cg->func_cap=32;   cg->funcs  =malloc(cg->func_cap  *sizeof(FuncRecord));
}

/* =========================================================================
 * String pool
 * ========================================================================= */
static const char *intern_string(CodeGen *cg, const char *value) {
    for (int i=0;i<cg->string_count;i++)
        if (strcmp(cg->strings[i].value,value)==0)
            return cg->strings[i].label;
    if (cg->string_count==cg->string_cap) {
        cg->string_cap*=2;
        cg->strings=realloc(cg->strings,cg->string_cap*sizeof(StringEntry));
    }
    StringEntry *se=&cg->strings[cg->string_count++];
    se->value  = strdup(value);
    se->len    = (int)strlen(value)+1;
    se->offset = cg->string_pool_size;
    cg->string_pool_size += se->len;
    char lbl[32]; snprintf(lbl,sizeof lbl,"str%d",cg->string_count-1);
    se->label  = strdup(lbl);
    return se->label;
}

static uint8_t *build_rdata(CodeGen *cg, int *out_len) {
    if (!cg->string_pool_size) { *out_len=0; return NULL; }
    uint8_t *buf=calloc(cg->string_pool_size,1);
    for (int i=0;i<cg->string_count;i++)
        memcpy(buf+cg->strings[i].offset,cg->strings[i].value,cg->strings[i].len);
    *out_len=cg->string_pool_size;
    return buf;
}

/* =========================================================================
 * Function label management
 * ========================================================================= */
static int get_func_label(CodeGen *cg, const char *name) {
    for (int i=0;i<cg->func_count;i++)
        if (strcmp(cg->funcs[i].name,name)==0) return cg->funcs[i].label_id;
    if (cg->func_count==cg->func_cap) {
        cg->func_cap*=2;
        cg->funcs=realloc(cg->funcs,cg->func_cap*sizeof(FuncRecord));
    }
    int id=asm_new_label(cg->asm_,name);
    cg->funcs[cg->func_count].name     =strdup(name);
    cg->funcs[cg->func_count].label_id =id;
    cg->func_count++;
    return id;
}

/* =========================================================================
 * sizeof helper
 * ========================================================================= */
static int sizeof_type(TypeInfo *ti, int is_64) {
    return typeinfo_size(ti, is_64);
}

/* =========================================================================
 * emit_write_stdout  — write a string literal to stdout
 * Used by printf/puts shim: writes the format string verbatim.
 * ========================================================================= */
static void emit_write_stdout(CodeGen *cg, const char *str_lbl, int str_len) {
    Assembler *a=cg->asm_;
    symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
    symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");

    if (cg->is_64bit) {
        /* GetStdHandle(-11) */
        asm_mov_reg_imm(a,REG_RCX,(long long)-11);
        asm_sub_rsp(a,40);
        asm_call_import(a,"GetStdHandle");
        asm_add_rsp(a,40);
        asm_mov_reg_reg(a,REG_RBX,REG_RAX);

        /* WriteFile */
        asm_sub_rsp(a,56);
        /* zero slots */
        asm_mov_reg_imm(a,REG_RAX,0);
        asm_mov_mem_reg(a,REG_RSP,32,REG_RAX);
        asm_mov_mem_reg(a,REG_RSP,40,REG_RAX);
        asm_mov_reg_reg(a,REG_RCX,REG_RBX);
        asm_lea_rip_data(a,REG_RDX,str_lbl);
        asm_mov_reg_imm(a,REG_R8,(long long)str_len);
        /* lea r9,[rsp+40] */
        asm_emit4(a,0x4C,0x8D,0x4C,0x24); asm_emit1(a,40);
        asm_call_import(a,"WriteFile");
        asm_add_rsp(a,56);
    } else {
        asm_push_imm32(a,-11);
        asm_call_import32(a,"GetStdHandle");
        asm_mov_reg_reg(a,REG_EBX,REG_EAX);
        asm_sub_rsp(a,4);
        asm_mov_reg_imm(a,REG_EAX,0);
        asm_mov_mem_reg(a,REG_ESP,0,REG_EAX);
        asm_emit2(a,0x89,0xE1); /* mov ecx,esp */
        asm_push_imm32(a,0);
        asm_emit1(a,0x51); /* push ecx */
        asm_push_imm32(a,(int32_t)str_len);
        asm_emit1(a,0x68); asm_reloc_data(a,str_lbl);
        asm_emit1(a,0x53);
        asm_call_import32(a,"WriteFile");
        asm_add_rsp(a,4);
    }
}

/* =========================================================================
 * Internal shim dispatch
 * ========================================================================= */
static void emit_internal_call(CodeGen *cg, const char *name, ASTNode **args, int argc) {
    Assembler *a=cg->asm_;

    /* printf / puts — write first string arg verbatim */
    if (strcmp(name,"printf")==0||strcmp(name,"puts")==0) {
        if (argc>=1 && args[0]->kind==AST_STRING) {
            const char *lbl=intern_string(cg, args[0]->str.value);
            int sl=(int)strlen(args[0]->str.value);
            emit_write_stdout(cg, lbl, sl);
        }
        return;
    }
    if (strcmp(name,"putchar")==0) {
        /* single char — store in 1-byte buffer and write */
        char buf[2]={0,0};
        if (argc>=1 && args[0]->kind==AST_NUMBER) buf[0]=(char)args[0]->num.value;
        else buf[0]='?';
        const char *lbl=intern_string(cg,buf);
        emit_write_stdout(cg,lbl,1);
        return;
    }

    /* malloc — HeapAlloc(GetProcessHeap(), 0, size) */
    if (strcmp(name,"malloc")==0||strcmp(name,"calloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        ASTNode *sz_arg = (argc>0) ? args[0] : NULL;
        ASTNode *cnt_arg = (argc>1) ? args[0] : NULL; /* calloc: count*size */
        ASTNode *esz_arg = (argc>1) ? args[1] : sz_arg;

        if (cg->is_64bit) {
            /* GetProcessHeap() */
            asm_sub_rsp(a,40); asm_call_import(a,"GetProcessHeap"); asm_add_rsp(a,40);
            asm_push_reg(a,REG_RAX); /* save heap handle */
            /* size arg */
            if (esz_arg) { codegen_expr(cg,esz_arg); }
            else asm_mov_reg_imm(a,REG_RAX,0);
            if (argc>1 && cnt_arg) {
                /* calloc: multiply count * elemsize */
                asm_push_reg(a,REG_RAX);
                codegen_expr(cg,cnt_arg);
                asm_pop_reg(a,REG_RBX);
                asm_imul_reg_reg(a,REG_RAX,REG_RBX);
            }
            asm_mov_reg_reg(a,REG_R8,REG_RAX); /* size → r8 */
            asm_pop_reg(a,REG_RCX);             /* heap → rcx */
            asm_mov_reg_imm(a,REG_RDX,8);       /* HEAP_ZERO_MEMORY */
            asm_sub_rsp(a,40); asm_call_import(a,"HeapAlloc"); asm_add_rsp(a,40);
        } else {
            asm_push_imm32(a,0); asm_call_import32(a,"GetProcessHeap");
            asm_push_reg(a,REG_EAX);
            if (sz_arg) { codegen_expr(cg,sz_arg); asm_push_reg(a,REG_EAX); }
            else asm_push_imm32(a,0);
            asm_push_imm32(a,8); /* HEAP_ZERO_MEMORY */
            /* GetProcessHeap already on stack, need to re-push handle */
            /* simplified: just push 0 for heap handle */
            asm_add_rsp(a,4);
            asm_push_imm32(a,0); asm_push_imm32(a,8);
            if (sz_arg) { codegen_expr(cg,sz_arg); asm_push_reg(a,REG_EAX); }
            else asm_push_imm32(a,4);
            asm_call_import32(a,"HeapAlloc");
        }
        return;
    }

    if (strcmp(name,"free")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapFree");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        if (cg->is_64bit) {
            if (argc>0) { codegen_expr(cg,args[0]); asm_push_reg(a,REG_RAX); }
            asm_sub_rsp(a,40); asm_call_import(a,"GetProcessHeap"); asm_add_rsp(a,40);
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_mov_reg_imm(a,REG_RDX,0);
            if (argc>0) asm_pop_reg(a,REG_R8);
            else asm_mov_reg_imm(a,REG_R8,0);
            asm_sub_rsp(a,40); asm_call_import(a,"HeapFree"); asm_add_rsp(a,40);
        } else {
            if (argc>0) { codegen_expr(cg,args[0]); asm_push_reg(a,REG_EAX); }
            else asm_push_imm32(a,0);
            asm_push_imm32(a,0);
            asm_push_imm32(a,0); /* heap handle placeholder */
            asm_call_import32(a,"HeapFree");
        }
        return;
    }

    if (strcmp(name,"realloc")==0) {
        /* Use LocalAlloc / LocalFree or just VirtualAlloc for simplicity */
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* simplified: alloc new, caller must memcpy — emit HeapAlloc */
        if (argc>=2) {
            codegen_expr(cg,args[1]); /* new size */
            if (cg->is_64bit) {
                asm_mov_reg_reg(a,REG_R8,REG_RAX);
                asm_sub_rsp(a,40); asm_call_import(a,"GetProcessHeap"); asm_add_rsp(a,40);
                asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                asm_mov_reg_imm(a,REG_RDX,8);
                asm_sub_rsp(a,40); asm_call_import(a,"HeapAlloc"); asm_add_rsp(a,40);
            } else {
                asm_push_reg(a,REG_EAX); asm_push_imm32(a,8); asm_push_imm32(a,0);
                asm_call_import32(a,"HeapAlloc");
            }
        } else asm_mov_reg_imm(a,REG_RAX,0);
        return;
    }

    /* memset — stub: just emit a loop */
    if (strcmp(name,"memset")==0) {
        if (argc>=3) {
            /* dst in arg0, val in arg1, size in arg2 */
            codegen_expr(cg,args[0]); asm_push_reg(a,REG_RAX); /* dst */
            codegen_expr(cg,args[2]); asm_push_reg(a,REG_RAX); /* size */
            codegen_expr(cg,args[1]); /* val in RAX/EAX */
            if (cg->is_64bit) {
                asm_pop_reg(a,REG_RCX);  /* size */
                asm_pop_reg(a,REG_RDI);  /* dst */
                /* rep stosb: AL=val, RDI=dst, RCX=count */
                asm_emit1(a,0xF3); asm_emit1(a,0xAA);
                asm_mov_reg_reg(a,REG_RAX,REG_RDI);
            } else {
                asm_pop_reg(a,REG_ECX); asm_pop_reg(a,REG_EDI);
                asm_emit1(a,0xF3); asm_emit1(a,0xAA);
                asm_mov_reg_reg(a,REG_EAX,REG_EDI);
            }
        } else asm_mov_reg_imm(a,REG_RAX,0);
        return;
    }

    /* memcpy — stub loop */
    if (strcmp(name,"memcpy")==0||strcmp(name,"memmove")==0) {
        if (argc>=3) {
            codegen_expr(cg,args[0]); asm_push_reg(a,REG_RAX);
            codegen_expr(cg,args[1]); asm_push_reg(a,REG_RAX);
            codegen_expr(cg,args[2]);
            if (cg->is_64bit) {
                asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                asm_pop_reg(a,REG_RSI); asm_pop_reg(a,REG_RDI);
                asm_emit1(a,0xF3); asm_emit1(a,0xA4); /* rep movsb */
                /* restore rdi to result */
                asm_sub_rsp(a,16); asm_add_rsp(a,16); /* clobber avoidance */
                asm_mov_reg_imm(a,REG_RAX,0);
            } else {
                asm_mov_reg_reg(a,REG_ECX,REG_EAX);
                asm_pop_reg(a,REG_ESI); asm_pop_reg(a,REG_EDI);
                asm_emit1(a,0xF3); asm_emit1(a,0xA4);
                asm_mov_reg_imm(a,REG_EAX,0);
            }
        } else asm_mov_reg_imm(a,REG_RAX,0);
        return;
    }

    /* strlen — count bytes until NUL */
    if (strcmp(name,"strlen")==0) {
        if (argc>=1) {
            codegen_expr(cg,args[0]); /* ptr in RAX */
            /* xor ecx,ecx; loop: cmp [rax+rcx],0; je done; inc rcx; jmp loop */
            if (cg->is_64bit) {
                asm_emit3(a,0x48,0x31,0xC9); /* xor rcx,rcx */
                int lp=asm_new_label(a,"strlen_lp"); asm_def_label(a,lp);
                /* cmp byte [rax+rcx], 0 */
                asm_emit4(a,0x80,0x3C,0x08,0x00);
                int dn=asm_new_label(a,"strlen_dn"); asm_jcc_label(a,CC_E,dn);
                asm_emit3(a,0x48,0xFF,0xC1); /* inc rcx */
                asm_jmp_label(a,lp);
                asm_def_label(a,dn);
                asm_mov_reg_reg(a,REG_RAX,REG_RCX);
            } else {
                asm_emit2(a,0x31,0xC9);
                int lp=asm_new_label(a,"strlen_lp"); asm_def_label(a,lp);
                asm_emit3(a,0x80,0x3C,0x08); asm_emit1(a,0x00);
                int dn=asm_new_label(a,"strlen_dn"); asm_jcc_label(a,CC_E,dn);
                asm_emit1(a,0x41); /* inc ecx */
                asm_jmp_label(a,lp);
                asm_def_label(a,dn);
                asm_mov_reg_reg(a,REG_EAX,REG_ECX);
            }
        } else asm_mov_reg_imm(a,REG_RAX,0);
        return;
    }

    /* For other internals (strcmp, strcpy, atoi etc) — emit 0 as placeholder */
    asm_mov_reg_imm(a,REG_RAX,0);
}

/* =========================================================================
 * codegen_lvalue — compute address of lvalue into RAX/EAX
 * ========================================================================= */
void codegen_lvalue(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) return;
    switch (n->kind) {
    case AST_VAR: {
        Symbol *s=symtable_lookup(cg->sym,n->var.name);
        if (!s) { fprintf(stderr,"codegen_lvalue: undefined '%s'\n",n->var.name); asm_mov_reg_imm(a,REG_RAX,0); return; }
        if (s->kind==SYM_VAR||s->kind==SYM_PARAM)
            asm_lea_rbp_disp(a,REG_RAX,s->offset);
        else if (s->kind==SYM_GLOBAL) {
            /* global variable address — use data reloc */
            if (cg->is_64bit) asm_lea_rip_data(a,REG_RAX,n->var.name);
            else { asm_emit1(a,0xB8); asm_reloc_data(a,n->var.name); }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
        }
        break;
    }
    case AST_INDEX: {
        /* &array[idx] = base + idx*elem_size */
        codegen_lvalue(cg,n->index.array);
        asm_push_reg(a,REG_RAX);
        codegen_expr(cg,n->index.index);
        /* multiply by element size (assume 8 for 64-bit, 4 for 32-bit) */
        int esz = cg->is_64bit ? 8 : 4;
        if (esz>1) {
            if (cg->is_64bit) { asm_mov_reg_imm(a,REG_RBX,(long long)esz); asm_imul_reg_reg(a,REG_RAX,REG_RBX); }
            else { asm_mov_reg_imm(a,REG_EBX,(long long)esz); asm_imul_reg_reg(a,REG_EAX,REG_EBX); }
        }
        asm_pop_reg(a,REG_RBX);
        asm_add_reg_reg(a,REG_RAX,REG_RBX);
        break;
    }
    case AST_DEREF:
        codegen_expr(cg,n->deref.operand);
        break;
    case AST_MEMBER: {
        /* compute base address, add field offset */
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        /* field offset: simplified — assume 0 for now */
        /* A proper compiler would look up struct layout */
        break;
    }
    default:
        codegen_expr(cg,n);
        break;
    }
}

/* =========================================================================
 * codegen_expr — evaluate expr, result in RAX/EAX
 * ========================================================================= */
void codegen_expr(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) { asm_mov_reg_imm(a,REG_RAX,0); return; }

    switch (n->kind) {
    case AST_NUMBER:   asm_mov_reg_imm(a,REG_RAX,n->num.value); break;
    case AST_FLOAT:    asm_mov_reg_imm(a,REG_RAX,(long long)n->fnum.value); break; /* int approx */
    case AST_CHAR_LIT: asm_mov_reg_imm(a,REG_RAX,n->char_lit.value); break;

    case AST_STRING: {
        const char *lbl=intern_string(cg,n->str.value);
        if (cg->is_64bit) asm_lea_rip_data(a,REG_RAX,lbl);
        else { asm_emit1(a,0xB8); asm_reloc_data(a,lbl); }
        break;
    }

    case AST_VAR: {
        Symbol *s=symtable_lookup(cg->sym,n->var.name);
        if (!s) { fprintf(stderr,"codegen: undefined '%s' at line %d\n",n->var.name,n->line); asm_mov_reg_imm(a,REG_RAX,0); return; }
        if (s->kind==SYM_ENUM_VAL) { asm_mov_reg_imm(a,REG_RAX,s->enum_value); return; }
        if (s->kind==SYM_VAR||s->kind==SYM_PARAM)
            asm_mov_reg_mem(a,REG_RAX,REG_RBP,s->offset);
        else if (s->kind==SYM_GLOBAL) {
            if (cg->is_64bit) asm_lea_rip_data(a,REG_RAX,n->var.name);
            else { asm_emit1(a,0xB8); asm_reloc_data(a,n->var.name); }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
        }
        break;
    }

    case AST_SIZEOF_TYPE:
        asm_mov_reg_imm(a,REG_RAX,(long long)sizeof_type(n->sizeof_type.type,cg->is_64bit));
        break;
    case AST_SIZEOF_EXPR:
        /* emit 4 for non-pointer, 8 for pointer (simplified) */
        asm_mov_reg_imm(a,REG_RAX,cg->is_64bit?8:4);
        break;

    case AST_CAST:
        codegen_expr(cg,n->cast.expr);
        /* Numeric truncation/sign-extension happens automatically in registers */
        break;

    case AST_ADDR:
        codegen_lvalue(cg,n->addr.operand);
        break;

    case AST_DEREF:
        codegen_expr(cg,n->deref.operand);
        /* dereference: load [RAX] */
        if (cg->is_64bit) {
            asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax] */
        } else {
            asm_emit2(a,0x8B,0x00); /* mov eax,[eax] */
        }
        break;

    case AST_INDEX: {
        codegen_lvalue(cg,n);
        /* load from address */
        if (cg->is_64bit) asm_emit3(a,0x48,0x8B,0x00);
        else asm_emit2(a,0x8B,0x00);
        break;
    }

    case AST_MEMBER: {
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        /* load field — simplified: load first field (offset 0) */
        if (cg->is_64bit) asm_emit3(a,0x48,0x8B,0x00);
        else asm_emit2(a,0x8B,0x00);
        break;
    }

    case AST_ASSIGN: {
        const char *op=n->assign.op;
        codegen_expr(cg,n->assign.rhs);
        /* Compound assignment: combine with lhs */
        if (strcmp(op,"=")!=0) {
            asm_push_reg(a,REG_RAX);
            codegen_expr(cg,n->assign.lhs);
            asm_pop_reg(a,REG_RBX);
            if      (strcmp(op,"+=")==0)  asm_add_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"-=")==0)  asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"*=")==0)  asm_imul_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"/=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_idiv_reg(a,REG_RCX); }
            else if (strcmp(op,"%=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_idiv_reg(a,REG_RCX); asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
            else if (strcmp(op,"&=")==0)  asm_and_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"|=")==0)  asm_or_reg_reg (a,REG_RAX,REG_RBX);
            else if (strcmp(op,"^=")==0)  asm_xor_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"<<=")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_shl_reg_cl(a,REG_RAX); }
            else if (strcmp(op,">>=")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_shr_reg_cl(a,REG_RAX); }
        }
        /* store result back */
        if (n->assign.lhs->kind==AST_VAR) {
            Symbol *s=symtable_lookup(cg->sym,n->assign.lhs->var.name);
            if (s&&(s->kind==SYM_VAR||s->kind==SYM_PARAM||s->kind==SYM_GLOBAL))
                asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
        } else {
            asm_push_reg(a,REG_RAX);
            codegen_lvalue(cg,n->assign.lhs);
            asm_pop_reg(a,REG_RBX);
            if (cg->is_64bit) { asm_emit3(a,0x48,0x89,0x18); } /* mov [rax],rbx */
            else              { asm_emit2(a,0x89,0x18); }       /* mov [eax],ebx */
            asm_mov_reg_reg(a,REG_RAX,REG_RBX);
        }
        break;
    }

    case AST_UNARY: {
        const char *op=n->unary.op;
        if (n->unary.post) {
            /* post ++ / -- */
            codegen_expr(cg,n->unary.operand);
            asm_push_reg(a,REG_RAX); /* save original */
            if (strcmp(op,"++")==0) {
                asm_mov_reg_imm(a,REG_RBX,1);
                asm_add_reg_reg(a,REG_RAX,REG_RBX);
            } else {
                asm_mov_reg_imm(a,REG_RBX,1);
                asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            }
            if (n->unary.operand->kind==AST_VAR) {
                Symbol *s=symtable_lookup(cg->sym,n->unary.operand->var.name);
                if (s) asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
            }
            asm_pop_reg(a,REG_RAX); /* return original value */
        } else if (strcmp(op,"++")==0||strcmp(op,"--")==0) {
            codegen_expr(cg,n->unary.operand);
            asm_mov_reg_imm(a,REG_RBX,1);
            if (strcmp(op,"++")==0) asm_add_reg_reg(a,REG_RAX,REG_RBX);
            else asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            if (n->unary.operand->kind==AST_VAR) {
                Symbol *s=symtable_lookup(cg->sym,n->unary.operand->var.name);
                if (s) asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
            }
        } else {
            codegen_expr(cg,n->unary.operand);
            if      (strcmp(op,"-")==0) asm_neg_reg(a,REG_RAX);
            else if (strcmp(op,"~")==0) asm_not_reg(a,REG_RAX);
            else if (strcmp(op,"!")==0) {
                asm_test_reg_reg(a,REG_RAX,REG_RAX);
                asm_setcc_al(a,CC_E); asm_movzx_rax_al(a);
            }
        }
        break;
    }

    case AST_BINARY: {
        const char *op=n->binary.op;
        /* short-circuit */
        if (strcmp(op,"&&")==0) {
            int end=asm_new_label(a,"sc_and");
            codegen_expr(cg,n->binary.left);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_E,end);
            codegen_expr(cg,n->binary.right);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_setcc_al(a,CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a,end); break;
        }
        if (strcmp(op,"||")==0) {
            int end=asm_new_label(a,"sc_or");
            codegen_expr(cg,n->binary.left);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_NE,end);
            codegen_expr(cg,n->binary.right);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_setcc_al(a,CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a,end); break;
        }
        /* comma: eval both, result is right */
        if (strcmp(op,",")==0) {
            codegen_expr(cg,n->binary.left);
            codegen_expr(cg,n->binary.right);
            break;
        }
        codegen_expr(cg,n->binary.left);  asm_push_reg(a,REG_RAX);
        codegen_expr(cg,n->binary.right); asm_pop_reg(a,REG_RBX);
        /* RBX=left, RAX=right */
        if      (strcmp(op,"+")==0)  { asm_add_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"-")==0)  { asm_sub_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"*")==0)  { asm_imul_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"/")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_idiv_reg(a,REG_RCX); }
        else if (strcmp(op,"%")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_idiv_reg(a,REG_RCX); asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
        else if (strcmp(op,"&")==0)  { asm_and_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"|")==0)  { asm_or_reg_reg (a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"^")==0)  { asm_xor_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"<<")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shl_reg_cl(a,REG_RAX); }
        else if (strcmp(op,">>")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shr_reg_cl(a,REG_RAX); }
        else {
            asm_cmp_reg_reg(a,REG_RBX,REG_RAX);
            CondCode cc;
            if      (strcmp(op,"==")==0) cc=CC_E;
            else if (strcmp(op,"!=")==0) cc=CC_NE;
            else if (strcmp(op,"<" )==0) cc=CC_L;
            else if (strcmp(op,">" )==0) cc=CC_G;
            else if (strcmp(op,"<=")==0) cc=CC_LE;
            else                          cc=CC_GE;
            asm_setcc_al(a,cc); asm_movzx_rax_al(a);
        }
        break;
    }

    case AST_TERNARY: {
        int else_lbl=asm_new_label(a,"tern_else"), end_lbl=asm_new_label(a,"tern_end");
        codegen_expr(cg,n->ternary.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_E,else_lbl);
        codegen_expr(cg,n->ternary.then_);
        asm_jmp_label(a,end_lbl);
        asm_def_label(a,else_lbl);
        codegen_expr(cg,n->ternary.else_);
        asm_def_label(a,end_lbl);
        break;
    }

    case AST_CALL: {
        const char *name=n->call.name;
        Symbol *sym=symtable_lookup(cg->sym,name);

        if (is_internal_shim(name)) {
            emit_internal_call(cg,name,n->call.args,n->call.argc);
            break;
        }

        int argc=n->call.argc;
        if (cg->is_64bit) {
            int extra=(argc>4)?(argc-4)*8:0;
            int frame=32+extra;
            if ((frame&8)==0) frame+=8;
            asm_sub_rsp(a,frame);
            for (int i=0;i<argc;i++) {
                codegen_expr(cg,n->call.args[i]);
                if      (i==0) asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                else if (i==1) asm_mov_reg_reg(a,REG_RDX,REG_RAX);
                else if (i==2) asm_mov_reg_reg(a,REG_R8, REG_RAX);
                else if (i==3) asm_mov_reg_reg(a,REG_R9, REG_RAX);
                else           asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);
            }
            if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import(a,name);
            } else asm_call_direct(a,get_func_label(cg,name));
            asm_add_rsp(a,frame);
        } else {
            for (int i=argc-1;i>=0;i--) {
                codegen_expr(cg,n->call.args[i]);
                asm_push_reg(a,REG_EAX);
            }
            if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import32(a,name);
            } else {
                asm_call_direct(a,get_func_label(cg,name));
                if (argc>0) asm_add_rsp(a,argc*4);
            }
        }
        break;
    }

    case AST_FUNC_PTR_CALL: {
        /* Evaluate function pointer expression into RAX, then call */
        codegen_expr(cg,n->fp_call.func_expr);
        if (cg->is_64bit) {
            /* call rax */
            asm_sub_rsp(a,40);
            /* for simplicity: pass no args — full arg support would need saving rax */
            asm_emit2(a,0xFF,0xD0); /* call rax */
            asm_add_rsp(a,40);
        } else {
            asm_emit2(a,0xFF,0xD0); /* call eax */
        }
        break;
    }

    default:
        fprintf(stderr,"codegen_expr: unhandled node kind %d\n",n->kind);
        asm_mov_reg_imm(a,REG_RAX,0);
    }
}

/* =========================================================================
 * codegen_stmt
 * ========================================================================= */
void codegen_stmt(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) return;

    switch (n->kind) {
    case AST_BLOCK:
        symtable_push_scope(cg->sym);
        for (int i=0;i<n->block.count;i++) codegen_stmt(cg,n->block.stmts[i]);
        symtable_pop_scope(cg->sym);
        break;

    case AST_EXPR_STMT:
        codegen_expr(cg,n->expr_stmt.expr);
        break;

    case AST_VAR_DECL: {
        Symbol *sym=symtable_define_var(cg->sym,n->var_decl.name,n->var_decl.type);
        int arr=n->var_decl.array_size;
        if (n->var_decl.init) {
            if (n->var_decl.init->kind==AST_BLOCK && arr>0) {
                /* array initialiser: store each element */
                for (int i=0;i<n->var_decl.init->block.count&&i<arr;i++) {
                    codegen_expr(cg,n->var_decl.init->block.stmts[i]);
                    int esz=cg->is_64bit?8:4;
                    asm_mov_mem_reg(a,REG_RBP,sym->offset+i*esz,REG_RAX);
                }
            } else {
                codegen_expr(cg,n->var_decl.init);
                asm_mov_mem_reg(a,REG_RBP,sym->offset,REG_RAX);
            }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
            asm_mov_mem_reg(a,REG_RBP,sym->offset,REG_RAX);
        }
        break;
    }

    case AST_ASSIGN:
        codegen_expr(cg,n);
        break;

    case AST_IF: {
        int else_lbl=asm_new_label(a,"if_else");
        int end_lbl =asm_new_label(a,"if_end");
        codegen_expr(cg,n->if_.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_E,else_lbl);
        codegen_stmt(cg,n->if_.then_);
        asm_jmp_label(a,end_lbl);
        asm_def_label(a,else_lbl);
        if (n->if_.else_) codegen_stmt(cg,n->if_.else_);
        asm_def_label(a,end_lbl);
        break;
    }

    case AST_WHILE: {
        int top=asm_new_label(a,"whl_top"), end=asm_new_label(a,"whl_end");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=top;
        asm_def_label(a,top);
        codegen_expr(cg,n->while_.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_E,end);
        codegen_stmt(cg,n->while_.body);
        asm_jmp_label(a,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_DO_WHILE: {
        int top=asm_new_label(a,"do_top"), end=asm_new_label(a,"do_end");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=top;
        asm_def_label(a,top);
        codegen_stmt(cg,n->do_while.body);
        codegen_expr(cg,n->do_while.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_NE,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_FOR: {
        int top=asm_new_label(a,"for_top"), end=asm_new_label(a,"for_end"), step=asm_new_label(a,"for_step");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=step;
        if (n->for_.init) codegen_stmt(cg,n->for_.init);
        asm_def_label(a,top);
        if (n->for_.cond) {
            codegen_expr(cg,n->for_.cond);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_E,end);
        }
        codegen_stmt(cg,n->for_.body);
        asm_def_label(a,step);
        if (n->for_.step) codegen_expr(cg,n->for_.step);
        asm_jmp_label(a,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_SWITCH: {
        int end=asm_new_label(a,"sw_end");
        int sv_sw=cg->switch_end_label, sv_end=cg->loop_end_label;
        cg->switch_end_label=end; cg->loop_end_label=end;
        codegen_expr(cg,n->switch_.expr);
        asm_push_reg(a,REG_RAX); /* save switch value */
        /* Generate jump table: compare and jump to each case */
        int *case_labels=malloc(n->switch_.nc*sizeof(int));
        int default_lbl=-1;
        for (int i=0;i<n->switch_.nc;i++) {
            case_labels[i]=asm_new_label(a,"sw_case");
            if (n->switch_.cases[i]->kind==AST_DEFAULT) {
                default_lbl=case_labels[i];
            }
        }
        /* emit comparisons */
        for (int i=0;i<n->switch_.nc;i++) {
            if (n->switch_.cases[i]->kind==AST_CASE) {
                /* cmp [stack top], case_val */
                asm_mov_reg_mem(a,REG_RBX,REG_RSP,0); /* load switch value */
                asm_mov_reg_imm(a,REG_RAX,n->switch_.cases[i]->case_.value);
                asm_cmp_reg_reg(a,REG_RBX,REG_RAX);
                asm_jcc_label(a,CC_E,case_labels[i]);
            }
        }
        if (default_lbl>=0) asm_jmp_label(a,default_lbl);
        else asm_jmp_label(a,end);
        /* emit case bodies */
        for (int i=0;i<n->switch_.nc;i++) {
            asm_def_label(a,case_labels[i]);
            ASTNode *c=n->switch_.cases[i];
            ASTNode **body = c->kind==AST_CASE ? c->case_.body : c->default_.body;
            int nb        = c->kind==AST_CASE ? c->case_.nb   : c->default_.nb;
            for (int j=0;j<nb;j++) codegen_stmt(cg,body[j]);
        }
        free(case_labels);
        asm_def_label(a,end);
        asm_pop_reg(a,REG_RAX); /* pop saved switch value */
        cg->switch_end_label=sv_sw; cg->loop_end_label=sv_end;
        break;
    }

    case AST_BREAK:
        if (cg->loop_end_label>=0) asm_jmp_label(a,cg->loop_end_label);
        else fprintf(stderr,"codegen: break outside loop/switch\n");
        break;

    case AST_CONTINUE:
        if (cg->loop_top_label>=0) asm_jmp_label(a,cg->loop_top_label);
        else fprintf(stderr,"codegen: continue outside loop\n");
        break;

    case AST_GOTO: {
        /* Forward jump to a named label — allocate label if needed */
        char key[300]; snprintf(key,sizeof key,"lbl_%s",n->goto_.label);
        int lid=get_func_label(cg,key);
        asm_jmp_label(a,lid);
        break;
    }

    case AST_LABEL: {
        char key[300]; snprintf(key,sizeof key,"lbl_%s",n->label.name);
        int lid=get_func_label(cg,key);
        asm_def_label(a,lid);
        if (n->label.stmt) codegen_stmt(cg,n->label.stmt);
        break;
    }

    case AST_RETURN:
        if (n->ret.expr) codegen_expr(cg,n->ret.expr);
        else asm_mov_reg_imm(a,REG_RAX,0);
        asm_leave(a); asm_ret(a);
        break;

    default:
        fprintf(stderr,"codegen_stmt: unhandled node %d\n",n->kind);
    }
}

/* =========================================================================
 * codegen_func
 * ========================================================================= */
void codegen_func(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n||n->kind!=AST_FUNC_DECL) return;
    if (!n->func.body) return; /* forward declaration */

    int lid=get_func_label(cg,n->func.name);
    asm_def_label(a,lid);

    symtable_push_scope(cg->sym);
    symtable_reset_locals(cg->sym);
    cg->sym->next_offset=0;

    /* Register params */
    for (int i=0;i<n->func.paramc;i++) {
        ASTNode *pr=n->func.params[i];
        if (pr->param.name)
            symtable_define_param(cg->sym,pr->param.name,pr->param.type,i);
    }

    /* Reserve generous frame */
    int local_reserve=256;
    asm_enter(a,local_reserve);

    /* Spill reg params in 64-bit */
    if (cg->is_64bit) {
        static const Reg pr4[4]={REG_RCX,REG_RDX,REG_R8,REG_R9};
        for (int i=0;i<n->func.paramc&&i<4;i++)
            asm_mov_mem_reg(a,REG_RBP,16+i*8,pr4[i]);
    }

    codegen_stmt(cg,n->func.body);

    /* Default return 0 */
    asm_mov_reg_imm(a,REG_RAX,0);
    asm_leave(a); asm_ret(a);

    symtable_pop_scope(cg->sym);
}

/* =========================================================================
 * codegen_program
 * ========================================================================= */
void codegen_program(CodeGen *cg, ASTNode *prog) {
    if (!prog||prog->kind!=AST_PROGRAM) return;
    /* Pass 1: allocate labels */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (d->kind==AST_FUNC_DECL) get_func_label(cg,d->func.name);
    }
    /* Pass 2: generate */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (d->kind==AST_FUNC_DECL) codegen_func(cg,d);
    }
    asm_resolve(cg->asm_);
}

uint8_t    *codegen_get_text  (CodeGen *cg, int *len) { *len=cg->asm_->code_len; return cg->asm_->code; }
uint8_t    *codegen_get_rdata (CodeGen *cg, int *len) { return build_rdata(cg,len); }
Relocation *codegen_get_relocs(CodeGen *cg, int *cnt) { *cnt=cg->asm_->reloc_count; return cg->asm_->relocs; }
