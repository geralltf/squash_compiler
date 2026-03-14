/*
 * codegen.c  –  AST traversal for x64/x86 machine code generation
 *
 * Calling convention overview:
 *   64-bit (Microsoft x64):
 *     - Args: RCX, RDX, R8, R9, then [RSP+32], [RSP+40]...
 *     - Shadow space: 32 bytes always reserved by caller
 *     - Return value in RAX
 *     - Non-volatile: RBX, RBP, RDI, RSI, R12-R15
 *     - Stack aligned to 16 bytes at CALL
 *
 *   32-bit (stdcall):
 *     - Args pushed right-to-left on stack
 *     - Return in EAX
 *     - Callee cleans stack
 *
 * Value model: every expression leaves its result in RAX/EAX.
 * To handle binary ops, we push the left result, evaluate right into RAX,
 * then pop left into RBX/EBX and combine.
 */
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strdup _strdup
#endif

/* =========================================================================
 * Init
 * ========================================================================= */
void codegen_init(CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit) {
    memset(cg, 0, sizeof *cg);
    cg->asm_     = a;
    cg->sym      = sym;
    cg->is_64bit = is_64bit;
    cg->loop_end_label  = -1;
    cg->loop_top_label  = -1;
    cg->string_cap = 16;
    cg->strings    = malloc(cg->string_cap * sizeof(StringEntry));
    cg->func_cap   = 16;
    cg->funcs      = malloc(cg->func_cap * sizeof(FuncRecord));
}

/* =========================================================================
 * String pool management
 * ========================================================================= */
static const char *intern_string(CodeGen *cg, const char *value) {
    /* Return existing label if already interned */
    for (int i=0;i<cg->string_count;i++)
        if (strcmp(cg->strings[i].value, value)==0)
            return cg->strings[i].label;

    if (cg->string_count == cg->string_cap) {
        cg->string_cap *= 2;
        cg->strings = realloc(cg->strings, cg->string_cap*sizeof(StringEntry));
    }
    StringEntry *se = &cg->strings[cg->string_count++];
    se->value  = strdup(value);
    se->len    = (int)strlen(value)+1;
    se->offset = cg->string_pool_size;
    cg->string_pool_size += se->len;
    char lbl[32];
    snprintf(lbl, sizeof lbl, "str%d", cg->string_count-1);
    se->label  = strdup(lbl);
    return se->label;
}

/* Build the .rdata string pool bytes */
static uint8_t *build_rdata(CodeGen *cg, int *out_len) {
    int total = cg->string_pool_size;
    if (total == 0) { *out_len = 0; return NULL; }
    uint8_t *buf = calloc(total, 1);
    for (int i=0;i<cg->string_count;i++) {
        memcpy(buf + cg->strings[i].offset, cg->strings[i].value, cg->strings[i].len);
    }
    *out_len = total;
    return buf;
}

/* =========================================================================
 * Function record lookup / register
 * ========================================================================= */
static int get_func_label(CodeGen *cg, const char *name) {
    for (int i=0;i<cg->func_count;i++)
        if (strcmp(cg->funcs[i].name,name)==0)
            return cg->funcs[i].label_id;
    /* Allocate new label */
    if (cg->func_count==cg->func_cap) {
        cg->func_cap*=2;
        cg->funcs=realloc(cg->funcs,cg->func_cap*sizeof(FuncRecord));
    }
    int id = asm_new_label(cg->asm_, name);
    cg->funcs[cg->func_count].name     = strdup(name);
    cg->funcs[cg->func_count].label_id = id;
    cg->func_count++;
    return id;
}

/* =========================================================================
 * Emit a printf/puts shim built from WriteConsoleA / WriteFile
 * For our simple compiler, we implement printf as:
 *   write the format string to stdout via WriteFile (no format processing).
 * For a test we keep it simple and just output the string arg directly.
 * ========================================================================= */
static void emit_internal_printf(CodeGen *cg, ASTNode *call) {
    /* Only handle printf("literal") and printf("literal", int) */
    Assembler *a = cg->asm_;

    if (call->call.argc < 1) return;

    ASTNode *fmt_arg = call->call.args[0];
    if (fmt_arg->kind != AST_STRING) {
        /* fall back: evaluate and ignore */
        codegen_expr(cg, fmt_arg);
        return;
    }

    /* Build the output string by substituting %d with the int arg value.
     * For codegen simplicity we implement: write(stdout, str, len)
     * using GetStdHandle + WriteFile, same as our PE builder test.
     * Full printf formatting is out of scope — we handle the format string
     * as a raw write. */
    const char *fmt = fmt_arg->str.value;
    const char *lbl = intern_string(cg, fmt);
    int str_len = (int)strlen(fmt);

    /* Track as import */
    symtable_add_import(cg->sym, "KERNEL32.dll:GetStdHandle");
    symtable_add_import(cg->sym, "KERNEL32.dll:WriteFile");

    if (cg->is_64bit) {
        /* --- GetStdHandle(-11) --- */
        asm_mov_reg_imm(a, REG_RCX, -11); /* STD_OUTPUT_HANDLE */
        asm_sub_rsp(a,  40); /* shadow+slot */
        asm_call_import(a, "GetStdHandle");
        asm_add_rsp(a, 40);
        asm_mov_reg_reg(a, REG_RBX, REG_RAX); /* save handle */

        /* --- WriteFile(handle, str, len, &bw, NULL) --- */
        /* Allocate space for bytesWritten on stack */
        asm_sub_rsp(a, 48); /* shadow(32)+arg5(8)+bw(8) */
        /* Zero bytesWritten and lpOverlapped slots */
        asm_mov_reg_imm(a, REG_RAX, 0);
        asm_mov_mem_reg(a, REG_RSP, 32, REG_RAX);  /* lpOverlapped = NULL */
        asm_mov_mem_reg(a, REG_RSP, 40, REG_RAX);  /* bytesWritten = 0    */

        asm_mov_reg_reg(a, REG_RCX, REG_RBX);          /* arg1: handle */
        asm_lea_rip_data(a, REG_RDX, lbl);              /* arg2: buffer */
        asm_mov_reg_imm(a, REG_R8,  str_len);           /* arg3: len    */
        asm_lea_rbp_disp(a, REG_R9, -8);                /* arg4: &bw (use a local slot) */
        /* Actually use stack slot properly */
        asm_mov_reg_reg(a, REG_RAX, REG_RSP);
        /* R9 = RSP+40 (bytesWritten slot) */
        asm_emit4(a, 0x4C, 0x8D, 0x4C, 0x24); asm_emit1(a, 40); /* lea r9,[rsp+40] */
        asm_call_import(a, "WriteFile");
        asm_add_rsp(a, 48);
    } else {
        /* 32-bit: push args right-to-left, call WriteFile */
        /* GetStdHandle(-11) */
        asm_push_imm32(a, -11);
        asm_call_import32(a, "GetStdHandle");
        asm_mov_reg_reg(a, REG_EBX, REG_EAX);

        /* WriteFile: push 0(overlap), push &bw, push len, push str_va, push handle */
        asm_sub_rsp(a, 4);  /* space for bytesWritten local */
        asm_mov_reg_imm(a, REG_EAX, 0);
        asm_mov_mem_reg(a, REG_ESP, 0, REG_EAX);
        /* ecx = &bytesWritten = esp */
        asm_emit2(a, 0x89, 0xE1); /* mov ecx, esp */

        asm_push_imm32(a, 0);          /* lpOverlapped = NULL */
        /* push ecx */
        asm_emit1(a, 0x51);
        asm_push_imm32(a, str_len);
        /* push abs32 reloc for string */
        asm_emit1(a, 0x68); asm_reloc_data(a, lbl);
        /* push ebx (handle) */
        asm_emit1(a, 0x53);
        asm_call_import32(a, "WriteFile");
        asm_add_rsp(a, 4); /* clean bytesWritten local */
    }
}

/* =========================================================================
 * codegen_expr  –  evaluate expression, result in RAX/EAX
 * ========================================================================= */
void codegen_expr(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    if (!n) { asm_mov_reg_imm(a,REG_RAX,0); return; }

    switch (n->kind) {

    /* ---- Number literal ---- */
    case AST_NUMBER:
        asm_mov_reg_imm(a, REG_RAX, n->num.value);
        break;

    /* ---- String literal ---- */
    case AST_STRING: {
        const char *lbl = intern_string(cg, n->str.value);
        if (cg->is_64bit) {
            asm_lea_rip_data(a, REG_RAX, lbl);
        } else {
            asm_emit1(a,0xB8); asm_reloc_data(a, lbl);
        }
        break;
    }

    /* ---- Variable read ---- */
    case AST_VAR: {
        Symbol *sym = symtable_lookup(cg->sym, n->var.name);
        if (!sym) {
            fprintf(stderr,"codegen: undefined variable '%s'\n", n->var.name);
            exit(1);
        }
        asm_mov_reg_mem(a, REG_RAX, REG_RBP, sym->offset);
        break;
    }

    /* ---- Assignment ---- */
    case AST_ASSIGN: {
        codegen_expr(cg, n->assign.rhs);  /* RAX = rhs */
        if (n->assign.lhs->kind == AST_VAR) {
            Symbol *sym = symtable_lookup(cg->sym, n->assign.lhs->var.name);
            if (!sym) { fprintf(stderr,"codegen: undef var '%s'\n",n->assign.lhs->var.name); exit(1); }
            asm_mov_mem_reg(a, REG_RBP, sym->offset, REG_RAX);
        }
        break;
    }

    /* ---- Unary operator ---- */
    case AST_UNARY:
        codegen_expr(cg, n->unary.operand);
        switch (n->unary.op) {
            case '-': asm_neg_reg(a, REG_RAX); break;
            case '~': asm_not_reg(a, REG_RAX); break;
            case '!':
                asm_test_reg_reg(a, REG_RAX, REG_RAX);
                asm_setcc_al(a, CC_E);
                asm_movzx_rax_al(a);
                break;
        }
        break;

    /* ---- Binary operator ---- */
    case AST_BINARY: {
        const char *op = n->binary.op;
        /* Short-circuit && / || */
        if (strcmp(op,"&&")==0) {
            int end_id = asm_new_label(a, "sc_and_end");
            codegen_expr(cg, n->binary.left);
            asm_test_reg_reg(a, REG_RAX, REG_RAX);
            asm_jcc_label(a, CC_E, end_id);
            codegen_expr(cg, n->binary.right);
            asm_test_reg_reg(a, REG_RAX, REG_RAX);
            asm_setcc_al(a, CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a, end_id);
            break;
        }
        if (strcmp(op,"||")==0) {
            int end_id = asm_new_label(a,"sc_or_end");
            codegen_expr(cg, n->binary.left);
            asm_test_reg_reg(a, REG_RAX, REG_RAX);
            asm_jcc_label(a, CC_NE, end_id);
            codegen_expr(cg, n->binary.right);
            asm_test_reg_reg(a, REG_RAX, REG_RAX);
            asm_setcc_al(a, CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a, end_id);
            break;
        }

        /* General: eval left -> push; eval right -> RAX; pop left -> RBX */
        codegen_expr(cg, n->binary.left);
        asm_push_reg(a, REG_RAX);
        codegen_expr(cg, n->binary.right);
        /* RAX = right, stack top = left */
        asm_pop_reg(a, REG_RBX); /* left in RBX, right in RAX */

        if      (strcmp(op,"+")==0)  { asm_add_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"-")==0)  { asm_sub_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"*")==0)  { asm_imul_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"/")==0)  {
            asm_mov_reg_reg(a,REG_RCX,REG_RAX); /* divisor in RCX */
            asm_mov_reg_reg(a,REG_RAX,REG_RBX); /* dividend in RAX */
            asm_idiv_reg(a,REG_RCX);
        }
        else if (strcmp(op,"%")==0)  {
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            asm_idiv_reg(a,REG_RCX);
            asm_mov_reg_reg(a,REG_RAX,REG_RDX); /* remainder in RDX */
        }
        else if (strcmp(op,"&")==0)  { asm_and_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"|")==0)  { asm_or_reg_reg (a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"^")==0)  { asm_xor_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"<<")==0) {
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            asm_shl_reg_cl(a,REG_RAX);
        }
        else if (strcmp(op,">>")==0) {
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            asm_shr_reg_cl(a,REG_RAX);
        }
        else {
            /* Comparison operators: result 0 or 1 in RAX */
            asm_cmp_reg_reg(a, REG_RBX, REG_RAX);
            CondCode cc;
            if      (strcmp(op,"==")==0) cc=CC_E;
            else if (strcmp(op,"!=")==0) cc=CC_NE;
            else if (strcmp(op,"<" )==0) cc=CC_L;
            else if (strcmp(op,">" )==0) cc=CC_G;
            else if (strcmp(op,"<=")==0) cc=CC_LE;
            else if (strcmp(op,">=")==0) cc=CC_GE;
            else { fprintf(stderr,"codegen: unknown op '%s'\n",op); exit(1); }
            asm_setcc_al(a, cc);
            asm_movzx_rax_al(a);
        }
        break;
    }

    /* ---- Function call ---- */
    case AST_CALL: {
        const char *name = n->call.name;
        Symbol *sym = symtable_lookup(cg->sym, name);

        /* Internal printf/puts shim */
        if (strcmp(name,"printf")==0 || strcmp(name,"puts")==0) {
            /* Simple: call internal WriteFile-based shim */
            emit_internal_printf(cg, n);
            break;
        }

        int argc = n->call.argc;

        if (cg->is_64bit) {
            /* --- 64-bit Microsoft ABI --- */
            /* Evaluate args and store. For simplicity: eval each, store to
             * a temporary on stack, then load into regs/stack positions. */

            /* We need 32 (shadow) + 8*(argc>4 ? argc-4 : 0) bytes */
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra;
            /* Align to 16 */
            if ((frame & 8)==0) frame += 8;
            asm_sub_rsp(a, frame);

            /* Evaluate each argument and place it */
            for (int i=0;i<argc;i++) {
                codegen_expr(cg, n->call.args[i]); /* result in RAX */
                if (i==0)      asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                else if (i==1) asm_mov_reg_reg(a,REG_RDX,REG_RAX);
                else if (i==2) asm_mov_reg_reg(a,REG_R8, REG_RAX);
                else if (i==3) asm_mov_reg_reg(a,REG_R9, REG_RAX);
                else           asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);
            }

            if (sym && sym->kind==SYM_IMPORT) {
                char key[256];
                snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym, key);
                asm_call_import(a, name);
            } else {
                /* Direct call to a user-defined function */
                int lid = get_func_label(cg, name);
                asm_call_direct(a, lid);
            }
            asm_add_rsp(a, frame);

        } else {
            /* --- 32-bit stdcall: push args right-to-left --- */
            for (int i=argc-1;i>=0;i--) {
                codegen_expr(cg, n->call.args[i]);
                asm_push_reg(a, REG_EAX);
            }
            if (sym && sym->kind==SYM_IMPORT) {
                char key[256];
                snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym, key);
                asm_call_import32(a, name);
                /* stdcall: callee cleaned stack; nothing to do */
            } else {
                int lid = get_func_label(cg, name);
                asm_call_direct(a, lid);
                /* cdecl caller cleanup (user funcs are cdecl-like here) */
                if (argc > 0) asm_add_rsp(a, argc*4);
            }
        }
        break;
    }

    default:
        fprintf(stderr,"codegen_expr: unhandled node kind %d\n", n->kind);
        exit(1);
    }
}

/* =========================================================================
 * codegen_stmt  –  generate code for a statement (no value produced)
 * ========================================================================= */
void codegen_stmt(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    if (!n) return;

    switch (n->kind) {

    case AST_BLOCK:
        symtable_push_scope(cg->sym);
        for (int i=0;i<n->block.count;i++)
            codegen_stmt(cg, n->block.stmts[i]);
        symtable_pop_scope(cg->sym);
        break;

    case AST_EXPR_STMT:
        codegen_expr(cg, n->expr_stmt.expr);
        break;

    case AST_VAR_DECL: {
        /* Allocate stack slot */
        Symbol *sym = symtable_define_var(cg->sym, n->var_decl.name, n->var_decl.type_name);
        if (n->var_decl.init) {
            codegen_expr(cg, n->var_decl.init);
            asm_mov_mem_reg(a, REG_RBP, sym->offset, REG_RAX);
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
            asm_mov_mem_reg(a, REG_RBP, sym->offset, REG_RAX);
        }
        break;
    }

    case AST_ASSIGN:
        codegen_expr(cg, n);
        break;

    case AST_IF: {
        int else_lbl = asm_new_label(a, "if_else");
        int end_lbl  = asm_new_label(a, "if_end");

        codegen_expr(cg, n->if_.cond);
        asm_test_reg_reg(a, REG_RAX, REG_RAX);
        asm_jcc_label(a, CC_E, else_lbl);   /* jump if cond == 0 */

        codegen_stmt(cg, n->if_.then_);
        asm_jmp_label(a, end_lbl);

        asm_def_label(a, else_lbl);
        if (n->if_.else_) codegen_stmt(cg, n->if_.else_);

        asm_def_label(a, end_lbl);
        break;
    }

    case AST_WHILE: {
        int top_lbl = asm_new_label(a, "while_top");
        int end_lbl = asm_new_label(a, "while_end");

        int saved_end = cg->loop_end_label;
        int saved_top = cg->loop_top_label;
        cg->loop_end_label = end_lbl;
        cg->loop_top_label = top_lbl;

        asm_def_label(a, top_lbl);
        codegen_expr(cg, n->while_.cond);
        asm_test_reg_reg(a, REG_RAX, REG_RAX);
        asm_jcc_label(a, CC_E, end_lbl);

        codegen_stmt(cg, n->while_.body);
        asm_jmp_label(a, top_lbl);
        asm_def_label(a, end_lbl);

        cg->loop_end_label = saved_end;
        cg->loop_top_label = saved_top;
        break;
    }

    case AST_RETURN:
        if (n->ret.expr) codegen_expr(cg, n->ret.expr);
        else asm_mov_reg_imm(a, REG_RAX, 0);
        asm_leave(a);
        asm_ret(a);
        break;

    default:
        fprintf(stderr,"codegen_stmt: unhandled node kind %d\n", n->kind);
        exit(1);
    }
}

/* =========================================================================
 * codegen_func  –  generate a complete function
 * ========================================================================= */
void codegen_func(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    if (!n || n->kind != AST_FUNC_DECL) return;

    const char *fname = n->func.name;

    /* Define / get the function's label */
    int lid = get_func_label(cg, fname);
    asm_def_label(a, lid);

    /* Enter scope for parameters */
    symtable_push_scope(cg->sym);
    symtable_reset_locals(cg->sym);
    cg->sym->next_offset = 0;

    /* First pass: count locals by inspecting the body */
    /* For simplicity, we reserve a generous fixed frame. */
    /* A proper compiler would pre-scan for declarations. */
    int local_reserve = 128;  /* enough for typical test programs */

    /* Register parameters in symbol table */
    for (int i=0;i<n->func.paramc;i++) {
        ASTNode *p = n->func.params[i];
        symtable_define_param(cg->sym, p->param.name, p->param.type_name, i);
    }

    /* Prologue */
    asm_enter(a, local_reserve);

    /* In 64-bit: spill register parameters into their shadow home locations */
    if (cg->is_64bit) {
        static const Reg param_regs[4] = {REG_RCX, REG_RDX, REG_R8, REG_R9};
        for (int i=0;i<n->func.paramc&&i<4;i++) {
            (void)symtable_lookup(cg->sym, n->func.params[i]->param.name);
            /* shadow home is at [RBP+16+i*8] */
            asm_mov_mem_reg(a, REG_RBP, 16+i*8, param_regs[i]);
        }
    }

    /* Generate body */
    if (n->func.body) codegen_stmt(cg, n->func.body);

    /* Default return 0 if no explicit return */
    asm_mov_reg_imm(a, REG_RAX, 0);
    asm_leave(a);
    asm_ret(a);

    symtable_pop_scope(cg->sym);
}

/* =========================================================================
 * codegen_program  –  walk the program node
 * ========================================================================= */
void codegen_program(CodeGen *cg, ASTNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;

    /* Pass 1: pre-allocate all function labels so forward calls work */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *decl = prog->program.decls[i];
        if (decl->kind == AST_FUNC_DECL)
            get_func_label(cg, decl->func.name);
    }

    /* Pass 2: codegen each function */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *decl = prog->program.decls[i];
        if (decl->kind == AST_FUNC_DECL)
            codegen_func(cg, decl);
    }

    /* Resolve internal label fixups */
    asm_resolve(cg->asm_);
}

/* =========================================================================
 * Accessors for PE builder
 * ========================================================================= */
uint8_t *codegen_get_text(CodeGen *cg, int *len) {
    *len = cg->asm_->code_len;
    return cg->asm_->code;
}

uint8_t *codegen_get_rdata(CodeGen *cg, int *len) {
    return build_rdata(cg, len);
}

Relocation *codegen_get_relocs(CodeGen *cg, int *count) {
    *count = cg->asm_->reloc_count;
    return cg->asm_->relocs;
}
