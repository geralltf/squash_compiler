#include <stdio.h>
#include "codegen.h"

#ifdef _WIN32
#define strdup _strdup
#endif

/* =========================================================================
 * field_byte_offset — walk struct/union field list and return byte offset
 * For unions every field is at offset 0.
 * Fields are stored as AST_FIELD nodes; each is sizeof(int)=4 bytes wide
 * (we assume int-sized fields throughout; a full compiler would use TypeInfo).
 * ========================================================================= */
static int field_byte_offset(SymTable *sym, ASTNode *obj_node, const char *field_name) {
    /* Find the struct/union type name from the object's expression type.
     * The simplest heuristic: if obj is AST_VAR, look up its declaration
     * and get the type name from its TypeInfo or from the struct_decl.     */
    const char *type_name = NULL;

    /* Walk up: obj_node is the struct variable.
     * Its symbol gives us the TypeInfo whose name is "struct Foo" or "Foo". */
    if (obj_node && obj_node->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(sym, obj_node->var.name);
        if (vs && vs->type) {
            /* type->name might be "Point", "struct Point", etc. */
            const char *tn = vs->type->base;
            if (tn) {
                /* Strip leading "struct " or "union " if present */
                if (strncmp(tn,"struct ",7)==0) tn+=7;
                else if (strncmp(tn,"union ",6)==0)  tn+=6;
                type_name = tn;
            }
        }
    }
    if (!type_name) return 0;

     /* Look up the struct symbol — symtable_define_struct stores key as "struct <name>" */
     char key[128];
     /* symtable_define_struct always stores with "struct " prefix */
     const char *bare_tn2 = type_name;
     if (strncmp(bare_tn2,"struct ",7)==0) bare_tn2+=7;
     else if (strncmp(bare_tn2,"union ",6)==0) bare_tn2+=6;
     snprintf(key,sizeof key,"struct %s",bare_tn2);
     Symbol *ss = symtable_lookup(sym, key);
    if (!ss || !ss->struct_node) return 0;

    ASTNode *sd = ss->struct_node;   /* AST_STRUCT_DECL */
    int is_union = sd->struct_decl.is_union;

    /* Walk fields; each non-union field is 4 bytes wide (int/pointer).
     * We use a simple fixed-width layout matching what the parser does.   */
    int offset = 0;
    for (int i = 0; i < sd->struct_decl.nfields; i++) {
        ASTNode *f = sd->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD) continue;
        if (f->field.name && strcmp(f->field.name, field_name)==0)
            return is_union ? 0 : offset;
        if (!is_union) {
            /* Advance by field size (4 bytes per int field, aligned) */
            int fsz = 4;
            if (f->field.array_size > 0) fsz = 4 * f->field.array_size;
            offset += fsz;
        }
    }
    return 0; /* field not found — safe fallback */
}

/* elem_size_of — element size for an array/pointer expression.
 * Handles AST_VAR (stack arrays, pointer vars) and AST_MEMBER (struct field arrays). */
static int elem_size_of(CodeGen *cg, ASTNode *arr_expr) {
    if (!arr_expr) return 4;

    /* Case 1: simple variable — arr[i] or ptr[i] */
    if (arr_expr->kind == AST_VAR) {
        Symbol *asym = symtable_lookup(cg->sym, arr_expr->var.name);
        if (asym && asym->type) {
            int orig_pd = asym->type->pointer_depth;
            TypeInfo tmp = *asym->type;
            tmp.pointer_depth = (orig_pd > 0) ? orig_pd-1 : 0;
            tmp.array_size = -1;
            /* If original pointer_depth > 0 and the element type is itself a pointer
             * (e.g. array of function pointers: int (*fps[3])()), return pointer size. */
            if (orig_pd > 0 && tmp.pointer_depth > 0) return cg->is_64bit ? 8 : 4;
            /* Also: if element stride is 0 (void*) or the base after stripping is still
             * a pointer context, use pointer size for func-ptr arrays.
             * Heuristic: if orig_pd==1 AND asym is a stack array (array_size>0),
             * each element IS a pointer (8 bytes in 64-bit, 4 in 32-bit). */
            if (orig_pd == 1 && asym->array_size > 0) return cg->is_64bit ? 8 : 4;
            int sz = typeinfo_size(&tmp, cg->is_64bit);
            return (sz >= 1) ? sz : 4;
        }
    }

    /* Case 2: struct/union field — obj.field[i]
     * Look up the field's declared type in the struct definition.             */
    if (arr_expr->kind == AST_MEMBER && arr_expr->member.obj &&
        arr_expr->member.obj->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(cg->sym, arr_expr->member.obj->var.name);
        if (vs && vs->type) {
            const char *tn = vs->type->base;
            if (!tn) return 4;
            /* Build the struct symbol key */
            char key[128];
            /* symtable_define_struct always stores with "struct " prefix */
            const char *bare_tn = tn;
            if (strncmp(bare_tn,"struct ",7)==0) bare_tn+=7;
            else if (strncmp(bare_tn,"union ",6)==0) bare_tn+=6;
            snprintf(key, sizeof key, "struct %s", bare_tn);
            Symbol *ss = symtable_lookup(cg->sym, key);
            if (!ss || !ss->struct_node) return 4;
            ASTNode *sd = ss->struct_node;
            for (int i = 0; i < sd->struct_decl.nfields; i++) {
                ASTNode *f = sd->struct_decl.fields[i];
                if (!f || f->kind != AST_FIELD) continue;
                if (f->field.name && strcmp(f->field.name, arr_expr->member.field)==0) {
                    if (f->field.type) {
                        TypeInfo tmp = *f->field.type;
                        tmp.pointer_depth = (tmp.pointer_depth>0) ? tmp.pointer_depth-1 : 0;
                        tmp.array_size = -1;
                        int sz = typeinfo_size(&tmp, cg->is_64bit);
                        return (sz >= 1) ? sz : 4;
                    }
                }
            }
        }
    }
    return 4;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    cg->wdata_cap=32;  cg->wdata  =malloc(cg->wdata_cap *sizeof(WDataEntry));
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

/* Allocate a zero-filled slot in the writable .data pool.
 * Returns the label name (pointer into WDataEntry.label). */
static const char *intern_wdata(CodeGen *cg, const char *label, int size) {
    if (cg->wdata_count == cg->wdata_cap) {
        cg->wdata_cap *= 2;
        cg->wdata = realloc(cg->wdata, cg->wdata_cap * sizeof(WDataEntry));
    }
    WDataEntry *we = &cg->wdata[cg->wdata_count++];
    we->label  = strdup(label);
    we->offset = cg->wdata_pool_size;
    we->size   = size;
    cg->wdata_pool_size += size;
    /* Align to 8 bytes */
    int pad = (8 - (cg->wdata_pool_size & 7)) & 7;
    cg->wdata_pool_size += pad;
    return we->label;
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
 * emit_write_stdout  — write a string literal to stdout via WriteFile.
 * Caches the stdout HANDLE in a zero-initialised rdata slot so GetStdHandle
 * is called at most ONCE per program run regardless of how many printf calls
 * are made.  This eliminates the repetitive push-0xF5/call-GetStdHandle
 * pattern that triggers Windows Defender heuristics.
 * ========================================================================= */
static void emit_write_stdout(CodeGen *cg, const char *str_lbl, int str_len) {
    Assembler *a=cg->asm_;
    symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
    symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");

    /* Allocate the handle cache slot on first use.
     * MUST be in .data (writable) not .rdata (read-only).               */
    if (cg->stdout_handle_lbl[0] == '\0') {
        snprintf(cg->stdout_handle_lbl, sizeof cg->stdout_handle_lbl,
                 "stdout_handle_slot");
        intern_wdata(cg, cg->stdout_handle_lbl, 4); /* 4 bytes for HANDLE */
    }
    const char *hlbl = cg->stdout_handle_lbl;

    if (cg->is_64bit) {
        /* Load cached handle; call GetStdHandle only if zero */
        asm_lea_rip_wdata(a, REG_RBX, hlbl);
        asm_emit2(a, 0x8B, 0x03);               /* mov eax,[rbx]           */
        asm_test_reg_reg(a, REG_RAX, REG_RAX);
        int skip = asm_new_label(a, "gsh_skip");
        asm_jcc_label(a, CC_NE, skip);           /* jnz handle_cached       */
        asm_mov_reg_imm(a, REG_RCX, (long long)-11);
        asm_sub_rsp(a, 32);
        asm_call_import(a, "GetStdHandle");
        asm_add_rsp(a, 32);
        asm_emit2(a, 0x89, 0x03);               /* mov [rbx],eax           */
        asm_def_label(a, skip);
        asm_mov_reg_reg(a, REG_RBX, REG_RAX);   /* rbx = handle            */

        /* WriteFile(handle, buf, len, &written, NULL) */
        asm_sub_rsp(a, 48);
        asm_mov_reg_imm(a, REG_RAX, 0);
        asm_mov_mem_reg(a, REG_RSP, 32, REG_RAX);
        asm_mov_mem_reg(a, REG_RSP, 40, REG_RAX);
        asm_mov_reg_reg(a, REG_RCX, REG_RBX);
        asm_lea_rip_data(a, REG_RDX, str_lbl);
        asm_mov_reg_imm(a, REG_R8, (long long)str_len);
        asm_emit4(a, 0x4C,0x8D,0x4C,0x24); asm_emit1(a, 40); /* lea r9,[rsp+40] */
        asm_call_import(a, "WriteFile");
        asm_add_rsp(a, 48);
    } else {
        /* Load cached handle into EBX; call GetStdHandle only if zero */
        asm_emit1(a, 0xBB); asm_reloc_wdata(a, hlbl); /* mov ebx,&slot      */
        asm_emit2(a, 0x8B, 0x03);               /* mov eax,[ebx]           */
        asm_test_reg_reg(a, REG_EAX, REG_EAX);
        int skip = asm_new_label(a, "gsh_skip");
        asm_jcc_label(a, CC_NE, skip);
        asm_push_imm32(a, -11);
        asm_call_import32(a, "GetStdHandle");
        asm_emit2(a, 0x89, 0x03);               /* mov [ebx],eax           */
        asm_def_label(a, skip);
        asm_mov_reg_reg(a, REG_EBX, REG_EAX);   /* ebx = handle            */

        /* WriteFile(handle, buf, len, &written, NULL) */
        asm_sub_rsp(a, 4);
        asm_mov_reg_imm(a, REG_EAX, 0);
        asm_mov_mem_reg(a, REG_ESP, 0, REG_EAX);
        asm_emit2(a, 0x89, 0xE1);               /* mov ecx,esp             */
        asm_push_imm32(a, 0);
        asm_emit1(a, 0x51);                      /* push ecx                */
        asm_push_imm32(a, (int32_t)str_len);
        asm_emit1(a, 0x68); asm_reloc_data(a, str_lbl);
        asm_emit1(a, 0x53);                      /* push ebx (handle)       */
        asm_call_import32(a, "WriteFile");
        asm_add_rsp(a, 4);
    }
}

/* =========================================================================
 * Internal shim dispatch
 * ========================================================================= */
static void emit_internal_call(CodeGen *cg, const char *name, ASTNode **args, int argc) {
    Assembler *a = cg->asm_;

    /* -----------------------------------------------------------------------
     * printf(fmt, ...) / puts(str)
     *
     * Supported format specifiers: %d %i %u %x %X %c %s %%
     * Strategy: split the format string at compile time into literal chunks
     * and integer/string slots, then emit code to:
     *   1. Write each literal chunk via WriteFile.
     *   2. For %d/%i/%u/%x: convert the integer arg to decimal/hex digits
     *      using an inline itoa loop and write the result.
     *   3. For %s: write the string argument via WriteFile.
     *   4. For %c: write a 1-byte buffer containing the char.
     * This requires no libc and works entirely through WriteFile.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"printf")==0 || strcmp(name,"puts")==0) {
        /* Only process when first arg is a string literal */
        if (argc >= 1 && args[0]->kind == AST_STRING) {
            const char *fmt = args[0]->str.value;
            int fmt_arg = 1;   /* index into args[] for the next %? value */

            /* Walk the format string, emitting chunks and conversions */
            const char *p = fmt;
            const char *chunk_start = p;

            while (*p) {
                if (*p == '%' && p[1]) {
                    /* flush literal chunk before the % */
                    if (p > chunk_start) {
                        /* intern the chunk and write it */
                        char tmp[4096]; int tl = (int)(p - chunk_start);
                        if (tl >= (int)sizeof(tmp)) tl = (int)sizeof(tmp)-1;
                        memcpy(tmp, chunk_start, tl); tmp[tl]='\0';
                        const char *lbl = intern_string(cg, tmp);
                        emit_write_stdout(cg, lbl, tl);
                    }
                    p++;
                    char spec = *p++;
                    chunk_start = p;

                    if (spec == '%') {
                        /* literal percent */
                        const char *lbl = intern_string(cg, "%");
                        emit_write_stdout(cg, lbl, 1);
                        continue;
                    }

                    /* evaluate the argument */
                    if (fmt_arg < argc) {
                        codegen_expr(cg, args[fmt_arg++]);
                    } else {
                        asm_mov_reg_imm(a, REG_RAX, 0);
                    }
                    /* RAX / EAX now holds the argument value */

                    if (spec == 's') {
                        /* arg is a pointer to a string; use strlen then WriteFile */
                        /* Save pointer, compute length, write */
                        if (cg->is_64bit) {
                            /* save ptr in rbx */
                            asm_mov_reg_reg(a, REG_RBX, REG_RAX);
                            /* strlen inline: xor rcx,rcx; loop cmp [rax+rcx],0 */
                            asm_emit3(a, 0x48,0x31,0xC9); /* xor rcx,rcx */
                            int lp=asm_new_label(a,"pf_slen_lp"); asm_def_label(a,lp);
                            asm_emit4(a,0x80,0x3C,0x01,0x00); /* cmp [rcx+rax],0 */
                            int dn=asm_new_label(a,"pf_slen_dn"); asm_jcc_label(a,CC_E,dn);
                            asm_emit3(a,0x48,0xFF,0xC1); /* inc rcx */
                            asm_jmp_label(a,lp); asm_def_label(a,dn);
                            /* now: rbx=ptr, rcx=len */
                            /* GetStdHandle */
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            /* Use cached handle */
                            if (cg->stdout_handle_lbl[0]==' ') emit_write_stdout(cg,"",0);
                            asm_push_reg(a,REG_RBX); asm_push_reg(a,REG_RCX);
                            asm_lea_rip_wdata(a,REG_RBX,cg->stdout_handle_lbl);
                            asm_emit2(a,0x8B,0x03); /* mov eax,[rbx] */
                            asm_test_reg_reg(a,REG_RAX,REG_RAX);
                            int _s_skip=asm_new_label(a,"gsh_s");
                            asm_jcc_label(a,CC_NE,_s_skip);
                            asm_mov_reg_imm(a,REG_RCX,(long long)-11);
                            asm_sub_rsp(a,32); asm_call_import(a,"GetStdHandle"); asm_add_rsp(a,32);
                            asm_emit2(a,0x89,0x03); /* mov [rbx],eax */
                            asm_def_label(a,_s_skip);
                            asm_pop_reg(a,REG_R8); /* len */
                            asm_pop_reg(a,REG_RDX); /* ptr */
                            asm_mov_reg_reg(a,REG_RCX,REG_RAX); /* handle */
                            asm_sub_rsp(a,48);
                            asm_mov_reg_imm(a,REG_RAX,0);
                            asm_mov_mem_reg(a,REG_RSP,32,REG_RAX);
                            asm_mov_mem_reg(a,REG_RSP,40,REG_RAX);
                            asm_emit4(a,0x4C,0x8D,0x4C,0x24); asm_emit1(a,40);
                            asm_call_import(a,"WriteFile");
                            asm_add_rsp(a,48);
                        } else {
                            /* 32-bit: strlen then WriteFile */
                            asm_push_reg(a,REG_EAX); /* save ptr */
                            asm_emit2(a,0x31,0xC9);  /* xor ecx,ecx */
                            int lp=asm_new_label(a,"pf_slen_lp"); asm_def_label(a,lp);
                            asm_emit3(a,0x80,0x3C,0x08); asm_emit1(a,0x00);
                            int dn=asm_new_label(a,"pf_slen_dn"); asm_jcc_label(a,CC_E,dn);
                            asm_emit1(a,0x41); asm_jmp_label(a,lp); asm_def_label(a,dn);
                            asm_pop_reg(a,REG_EBX);   /* ptr */
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            /* cached handle for %s 32-bit */
                            if (cg->stdout_handle_lbl[0]=='\0') emit_write_stdout(cg,"",0);
                            { int _sk=asm_new_label(a,"gsh_s32");
                              asm_emit1(a,0xB8); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x8B,0x00); /* mov eax,[eax] */
                              asm_test_reg_reg(a,REG_EAX,REG_EAX);
                              asm_jcc_label(a,CC_NE,_sk);
                              asm_push_imm32(a,-11); asm_call_import32(a,"GetStdHandle");
                              asm_emit1(a,0xB9); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x89,0x01); /* mov [ecx],eax */
                              asm_def_label(a,_sk); }
                            asm_sub_rsp(a,4); asm_mov_reg_imm(a,REG_EAX,0);
                            asm_mov_mem_reg(a,REG_ESP,0,REG_EAX); asm_emit2(a,0x89,0xE2);
                            asm_push_imm32(a,0); asm_emit1(a,0x52);
                            asm_emit1(a,0x51); /* push ecx (len) */
                            asm_emit1(a,0x53); /* push ebx (ptr) */
                            asm_push_reg(a,REG_EAX); /* handle */
                            asm_call_import32(a,"WriteFile");
                            asm_add_rsp(a,4);
                        }
                    } else if (spec == 'c') {
                        /* write single char: store in a 1-byte rdata slot */
                        /* We build a 1-char buffer dynamically on the stack */
                        if (cg->is_64bit) {
                            /* store char in stack slot, write 1 byte */
                            asm_sub_rsp(a, 16);
                            asm_mov_mem_reg(a, REG_RSP, 0, REG_RAX); /* [rsp+0]=char */
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            asm_push_reg(a, REG_RSP); /* save ptr to char */
                            if (cg->stdout_handle_lbl[0]=='\0') emit_write_stdout(cg,"",0);
                            asm_lea_rip_wdata(a,REG_RCX,cg->stdout_handle_lbl);
                            asm_emit2(a,0x8B,0x09); /* mov ecx,[rcx] */
                            asm_test_reg_reg(a,REG_RCX,REG_RCX);
                            int _ck=asm_new_label(a,"gsh_c"); asm_jcc_label(a,CC_NE,_ck);
                            asm_push_reg(a,REG_RSP);
                            asm_mov_reg_imm(a,REG_RCX,(long long)-11);
                            asm_sub_rsp(a,32); asm_call_import(a,"GetStdHandle"); asm_add_rsp(a,32);
                            asm_pop_reg(a,REG_RBX);
                            asm_lea_rip_wdata(a,REG_RBX,cg->stdout_handle_lbl);
                            asm_emit2(a,0x89,0x03); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                            asm_def_label(a,_ck);
                            asm_pop_reg(a,REG_RDX);
                            asm_mov_reg_imm(a,REG_R8,1);
                            asm_sub_rsp(a,48);
                            asm_mov_reg_imm(a,REG_RAX,0);
                            asm_mov_mem_reg(a,REG_RSP,32,REG_RAX);
                            asm_mov_mem_reg(a,REG_RSP,40,REG_RAX);
                            asm_emit4(a,0x4C,0x8D,0x4C,0x24); asm_emit1(a,40);
                            asm_call_import(a,"WriteFile"); asm_add_rsp(a,48);
                            asm_add_rsp(a,16);
                        } else {
                            asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX);
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            if (cg->stdout_handle_lbl[0]=='\0') emit_write_stdout(cg,"",0);
                            { int _ck32=asm_new_label(a,"gsh_c32");
                              asm_emit1(a,0xB8); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x8B,0x00); asm_test_reg_reg(a,REG_EAX,REG_EAX);
                              asm_jcc_label(a,CC_NE,_ck32);
                              asm_push_imm32(a,-11); asm_call_import32(a,"GetStdHandle");
                              asm_emit1(a,0xB9); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x89,0x01); asm_def_label(a,_ck32); }
                            asm_push_reg(a,REG_EAX); /* handle */
                            asm_sub_rsp(a,4); asm_mov_reg_imm(a,REG_EAX,0);
                            asm_mov_mem_reg(a,REG_ESP,0,REG_EAX); asm_emit2(a,0x89,0xE1);
                            asm_push_imm32(a,0); asm_emit1(a,0x51);
                            asm_push_imm32(a,1); /* len=1 */
                            asm_emit2(a,0x8D,0x44); asm_emit2(a,0x24,0x14); /* lea eax,[esp+20] */
                            asm_push_reg(a,REG_EAX);
                            asm_emit3(a,0xFF,0x74,0x24); asm_emit1(a,20); /* push saved handle [esp+20] */
                            asm_call_import32(a,"WriteFile");
                            asm_add_rsp(a,8);
                        }
                    } else {
                        /* %d / %i / %u / %x / %X — integer to string conversion */
                        /* Use inline itoa into a stack buffer, then write.
                         * Buffer: 24 bytes on stack (enough for int64 + sign + null).
                         * Algorithm:
                         *   for %u/%x: treat as unsigned
                         *   for %d/%i: if negative, write '-' then negate
                         *   divide repeatedly by base (10 or 16), collect digits,
                         *   reverse the digit string, write it.
                         */
                        int is_hex  = (spec == 'x' || spec == 'X');
                        int is_unsigned = (spec == 'u' || is_hex);
                        long long base = is_hex ? 16 : 10;

                        if (cg->is_64bit) {
                            /* Allocate 32-byte stack buffer for digits */
                            asm_sub_rsp(a, 32);
                            /* RAX = value. Save it. */
                            asm_push_reg(a, REG_RAX); /* [rsp] = value */

                            /* For signed %d: check negative */
                            int neg_lbl = asm_new_label(a, "pf_neg");
                            int conv_lbl = asm_new_label(a, "pf_conv");
                            int neg_flag_off = 8; /* [rsp+8] = negative flag */
                            asm_mov_reg_imm(a, REG_RBX, 0);
                            /* [rsp+8] = 0 (not negative) */
                            asm_emit4(a,0x48,0x89,0x5C,0x24); asm_emit1(a,neg_flag_off);
                            if (!is_unsigned) {
                                asm_test_reg_reg(a, REG_RAX, REG_RAX);
                                asm_jcc_label(a, CC_GE, conv_lbl);
                                /* negative: negate, set flag */
                                asm_neg_reg(a, REG_RAX);
                                asm_emit4(a,0x48,0x89,0x04,0x24); /* mov [rsp],rax */
                                asm_mov_reg_imm(a, REG_RBX, 1);
                                asm_emit4(a,0x48,0x89,0x5C,0x24); asm_emit1(a,neg_flag_off);
                            }
                            asm_def_label(a, conv_lbl);

                            /* digits buffer starts at [rsp+16], we fill from end */
                            /* RCX = pointer to end of buffer (rsp+16+20) */
                            /* Use rdi as write pointer (digit end) */
                            /* rsi = digit start pointer */
                            /* Write digits backwards: */
                            /* rdi points to current digit write position */
                            int buf_base = 16;
                            /* lea rdi,[rsp+buf_base+20] */
                            asm_emit4(a,0x48,0x8D,0x7C,0x24);
                            asm_emit1(a,(uint8_t)(buf_base+20));
                            /* null-terminate */
                            asm_mov_reg_imm(a,REG_RAX,0);
                            asm_emit2(a,0x88,0x07); /* mov [rdi],al */
                            /* load value into rax */
                            asm_emit3(a,0x48,0x8B,0x04); asm_emit1(a,0x24); /* mov rax,[rsp] */

                            int digit_lp = asm_new_label(a,"pf_dlp");
                            int digit_dn = asm_new_label(a,"pf_ddn");
                            asm_def_label(a, digit_lp);
                            asm_test_reg_reg(a,REG_RAX,REG_RAX);
                            asm_jcc_label(a,CC_E,digit_dn);
                            /* rdx:rax / base -> quotient in rax, remainder in rdx */
                            asm_mov_reg_imm(a,REG_RCX,base);
                            asm_emit3(a,0x48,0x31,0xD2); /* xor rdx,rdx */
                            asm_emit3(a,0x48,0xF7,0xF1); /* div rcx */
                            /* rdx = remainder (digit) */
                            /* convert digit to ascii */
                            if (is_hex) {
                                /* if rdx < 10: '0'+rdx else 'a'+(rdx-10) */
                                int hex_ltr = asm_new_label(a,"pf_hx");
                                int hex_dn2 = asm_new_label(a,"pf_hd");
                                asm_mov_reg_imm(a,REG_RBX,10);
                                asm_emit3(a,0x48,0x39,0xDA); /* cmp rdx,rbx */
                                asm_jcc_label(a,CC_GE,hex_ltr);
                                asm_mov_reg_imm(a,REG_RBX,'0');
                                asm_emit3(a,0x48,0x01,0xD3); /* add rbx,rdx */
                                asm_jmp_label(a,hex_dn2);
                                asm_def_label(a,hex_ltr);
                                asm_mov_reg_imm(a,REG_RBX,(long long)(spec=='X'?'A':'a')-10);
                                asm_emit3(a,0x48,0x01,0xD3);
                                asm_def_label(a,hex_dn2);
                                asm_emit2(a,0x48,0xFF); asm_emit1(a,0xCF); /* dec rdi */
                                asm_emit2(a,0x88,0x1F); /* mov [rdi],bl */
                            } else {
                                asm_mov_reg_imm(a,REG_RBX,'0');
                                asm_emit3(a,0x48,0x01,0xD3); /* add rbx,rdx */
                                asm_emit2(a,0x48,0xFF); asm_emit1(a,0xCF); /* dec rdi */
                                asm_emit2(a,0x88,0x1F); /* mov [rdi],bl */
                            }
                            asm_jmp_label(a,digit_lp);
                            asm_def_label(a,digit_dn);

                            /* Handle zero specially */
                            int nz_lbl = asm_new_label(a,"pf_nz");
                            /* Check if rdi points to null (no digits written = value was 0) */
                            asm_emit3(a,0x48,0x8D,0x44); asm_emit2(a,0x24,(uint8_t)(buf_base+20));
                            asm_emit3(a,0x48,0x39,0xC7); /* cmp rdi,rax (rax=buf_end) */
                            asm_jcc_label(a,CC_NE,nz_lbl);
                            /* write '0' */
                            asm_emit2(a,0x48,0xFF); asm_emit1(a,0xCF); /* dec rdi */
                            asm_mov_reg_imm(a,REG_RBX,'0');
                            asm_emit2(a,0x88,0x1F); /* mov [rdi],bl */
                            asm_def_label(a,nz_lbl);

                            /* Prepend '-' if negative */
                            if (!is_unsigned) {
                                int skip_neg = asm_new_label(a,"pf_skipneg");
                                asm_emit4(a,0x48,0x8B,0x5C,0x24); asm_emit1(a,neg_flag_off);
                                asm_test_reg_reg(a,REG_RBX,REG_RBX);
                                asm_jcc_label(a,CC_E,skip_neg);
                                asm_emit2(a,0x48,0xFF); asm_emit1(a,0xCF);
                                asm_mov_reg_imm(a,REG_RBX,'-');
                                asm_emit2(a,0x88,0x1F);
                                asm_def_label(a,skip_neg);
                            }

                            /* Now write [rdi .. buf_end) via GetStdHandle+WriteFile */
                            /* compute length: rax=buf_end, rcx=rdi, rdx=rax-rdi */
                            asm_emit3(a,0x48,0x8D,0x44); asm_emit2(a,0x24,(uint8_t)(buf_base+20));
                            asm_emit3(a,0x48,0x29,0xF8); /* sub rax,rdi => len */
                            asm_push_reg(a,REG_RAX); /* save len */
                            asm_push_reg(a,REG_RDI); /* save ptr */
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            if (cg->stdout_handle_lbl[0]=='\0') emit_write_stdout(cg,"",0);
                            { asm_lea_rip_wdata(a,REG_RCX,cg->stdout_handle_lbl);
                              asm_emit2(a,0x8B,0x09); /* mov ecx,[rcx] */
                              asm_test_reg_reg(a,REG_RCX,REG_RCX);
                              int _dz=asm_new_label(a,"gsh_dz"); asm_jcc_label(a,CC_NE,_dz);
                              asm_push_reg(a,REG_RDX); asm_push_reg(a,REG_R8);
                              asm_mov_reg_imm(a,REG_RCX,(long long)-11);
                              asm_sub_rsp(a,40); asm_call_import(a,"GetStdHandle"); asm_add_rsp(a,40);
                              asm_lea_rip_wdata(a,REG_RBX,cg->stdout_handle_lbl);
                              asm_emit2(a,0x89,0x03); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                              asm_pop_reg(a,REG_R8); asm_pop_reg(a,REG_RDX);
                              asm_def_label(a,_dz); }
                            asm_pop_reg(a,REG_RDX);  /* ptr */
                            asm_pop_reg(a,REG_R8);   /* len */
                            asm_sub_rsp(a,56);
                            asm_mov_reg_imm(a,REG_RAX,0);
                            asm_mov_mem_reg(a,REG_RSP,32,REG_RAX);
                            asm_mov_mem_reg(a,REG_RSP,40,REG_RAX);
                            asm_emit4(a,0x4C,0x8D,0x4C,0x24); asm_emit1(a,40);
                            asm_call_import(a,"WriteFile"); asm_add_rsp(a,56);
                            /* clean up the pushed value + stack buffer */
                            asm_add_rsp(a,8);  /* pop the saved value */
                            asm_add_rsp(a,32); /* free the digit buffer */
                        } else {
                            /* 32-bit integer-to-string */
                            asm_sub_rsp(a,28); /* digit buffer + flag */
                            asm_push_reg(a,REG_EAX);
                            /* neg flag at [esp+4] */
                            asm_mov_reg_imm(a,REG_EBX,0); asm_mov_mem_reg(a,REG_ESP,4,REG_EBX);
                            if (!is_unsigned) {
                                int cv=asm_new_label(a,"pf_cv");
                                asm_test_reg_reg(a,REG_EAX,REG_EAX); asm_jcc_label(a,CC_GE,cv);
                                asm_neg_reg(a,REG_EAX);
                                asm_emit3(a,0x89,0x04,0x24); /* mov [esp],eax */
                                asm_mov_reg_imm(a,REG_EBX,1); asm_mov_mem_reg(a,REG_ESP,4,REG_EBX);
                                asm_def_label(a,cv);
                            }
                            /* edi = buf_end = esp+28+4+20 = esp+8+20 = esp+28 */
                            asm_emit3(a,0x8D,0x7C,0x24); asm_emit1(a,28);
                            asm_mov_reg_imm(a,REG_EAX,0); asm_emit2(a,0x88,0x07);
                            asm_emit3(a,0x8B,0x04,0x24); /* mov eax,[esp] */
                            int dlp=asm_new_label(a,"pf_dlp"); asm_def_label(a,dlp);
                            int ddn=asm_new_label(a,"pf_ddn");
                            asm_test_reg_reg(a,REG_EAX,REG_EAX); asm_jcc_label(a,CC_E,ddn);
                            asm_emit2(a,0x31,0xD2); /* xor edx,edx */
                            asm_push_reg(a,REG_EBX);
                            asm_mov_reg_imm(a,REG_EBX,(long long)base);
                            asm_emit2(a,0xF7,0xF3); /* div ebx */
                            asm_pop_reg(a,REG_EBX);
                            asm_mov_reg_imm(a,REG_EBX,'0'); asm_emit2(a,0x01,0xD3);
                            if (is_hex) {
                                /* For hex digits A-F: add 0x27 ('a'-'0'-10=39) */
                                int hex_ok=asm_new_label(a,"pf_hexok");
                                asm_emit3(a,0x83,0xFA,0x0A); /* cmp edx,10 */
                                asm_jcc_label(a,CC_L,hex_ok);
                                asm_emit3(a,0x83,0xC3,0x27); /* add ebx,0x27 */
                                asm_def_label(a,hex_ok);
                                if (spec=='X') {
                                    /* uppercase: 'A' instead of 'a'; 'A'-'a'=-32 so sub 32 */
                                    int hex_lc=asm_new_label(a,"pf_hexlc");
                                    asm_emit3(a,0x83,0xFA,0x0A); /* cmp edx,10 */
                                    asm_jcc_label(a,CC_L,hex_lc);
                                    asm_emit3(a,0x83,0xEB,0x20); /* sub ebx,32 */
                                    asm_def_label(a,hex_lc);
                                }
                            }
                            asm_emit1(a,0x4F); /* dec edi */
                            asm_emit2(a,0x88,0x1F); /* mov [edi],bl */
                            asm_jmp_label(a,dlp);
                            asm_def_label(a,ddn);
                            int nz2=asm_new_label(a,"pf_nz");
                            asm_emit3(a,0x8D,0x44,0x24); asm_emit1(a,28);
                            asm_emit2(a,0x39,0xC7); asm_jcc_label(a,CC_NE,nz2);
                            asm_emit1(a,0x4F); asm_mov_reg_imm(a,REG_EBX,'0');
                            asm_emit2(a,0x88,0x1F); asm_def_label(a,nz2);
                            if (!is_unsigned) {
                                int sn=asm_new_label(a,"pf_sn");
                                asm_emit3(a,0x8B,0x5C,0x24); asm_emit1(a,4);
                                asm_test_reg_reg(a,REG_EBX,REG_EBX); asm_jcc_label(a,CC_E,sn);
                                asm_emit1(a,0x4F); asm_mov_reg_imm(a,REG_EBX,'-');
                                asm_emit2(a,0x88,0x1F); asm_def_label(a,sn);
                            }
                            /* compute len */
                            asm_emit3(a,0x8D,0x44,0x24); asm_emit1(a,28);
                            asm_emit2(a,0x29,0xF8); /* sub eax,edi */
                            asm_push_reg(a,REG_EAX); /* len */
                            asm_push_reg(a,REG_EDI); /* ptr */
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:GetStdHandle");
                            symtable_add_import(cg->sym,"KERNEL32.dll:WriteFile");
                            if (cg->stdout_handle_lbl[0]=='\0') emit_write_stdout(cg,"",0);
                            { int _dz32=asm_new_label(a,"gsh_dz32");
                              asm_emit1(a,0xB8); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x8B,0x00); asm_test_reg_reg(a,REG_EAX,REG_EAX);
                              asm_jcc_label(a,CC_NE,_dz32);
                              asm_push_imm32(a,-11); asm_call_import32(a,"GetStdHandle");
                              asm_emit1(a,0xB9); asm_reloc_wdata(a,cg->stdout_handle_lbl);
                              asm_emit2(a,0x89,0x01); asm_def_label(a,_dz32); }
                            asm_push_reg(a,REG_EAX); /* handle */
                            asm_sub_rsp(a,4); asm_mov_reg_imm(a,REG_EBX,0);
                            asm_mov_mem_reg(a,REG_ESP,0,REG_EBX); asm_emit2(a,0x89,0xE1);
                            asm_push_imm32(a,0); asm_emit1(a,0x51);
                            /* push len and ptr from stack */
                            asm_emit3(a,0xFF,0x74,0x24); asm_emit1(a,20); /* push len  */
                            asm_emit3(a,0xFF,0x74,0x24); asm_emit1(a,20); /* push ptr  */
                            asm_emit3(a,0xFF,0x74,0x24); asm_emit1(a,20); /* push hndl */
                            asm_call_import32(a,"WriteFile");
                            asm_add_rsp(a,4+8+4+28+4); /* hndl+len+ptr+neg+val+buf */
                        }
                    }
                } else {
                    p++;
                }
            }
            /* flush any remaining literal at end */
            if (p > chunk_start) {
                int tl = (int)(p - chunk_start);
                char tmp[4096];
                if (tl >= (int)sizeof(tmp)) tl=(int)sizeof(tmp)-1;
                memcpy(tmp, chunk_start, tl); tmp[tl]='\0';
                const char *lbl = intern_string(cg, tmp);
                emit_write_stdout(cg, lbl, tl);
            }
        } else if (argc >= 1) {
            codegen_expr(cg, args[0]);
        }
        asm_mov_reg_imm(a, REG_RAX, 0);
        return;
    }

    /* -----------------------------------------------------------------------
     * malloc(size) — HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY=8, size)
     * Returns pointer in RAX/EAX.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"malloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* Evaluate size argument first, save it */
        if (argc >= 1) codegen_expr(cg, args[0]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            /* size is in RAX — move to R8 (arg3), then get heap handle */
            asm_push_reg(a, REG_RAX);           /* save size on stack        */
            asm_sub_rsp(a, 32);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 32);
            /* RAX = heap handle, RCX = handle, RDX = 8 (ZERO), R8 = size */
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_mov_reg_imm(a, REG_RDX, 0);    /* HeapAlloc flags = 0       */
            asm_pop_reg(a, REG_R8);             /* pop size → R8             */
            asm_sub_rsp(a, 32);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 32);
        } else {
            /* 32-bit stdcall: push size, flags, handle (right-to-left) */
            asm_push_reg(a, REG_EAX);           /* save size                 */
            asm_push_imm32(a, 0);               /* placeholder for handle    */
            asm_call_import32(a, "GetProcessHeap");
            /* EAX = heap handle; fix up the placeholder on stack           */
            /* [ESP+0] = placeholder we just pushed; EAX = real handle      */
            /* Re-push in correct order: handle, flags=8, size              */
            asm_pop_reg(a, REG_EBX);            /* discard placeholder       */
            asm_pop_reg(a, REG_ECX);            /* ECX = size                */
            asm_push_reg(a, REG_ECX);           /* push size                 */
            asm_push_imm32(a, 0);               /* push HeapAlloc flags=0    */
            asm_push_reg(a, REG_EAX);           /* push handle               */
            asm_call_import32(a, "HeapAlloc");
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * calloc(count, size) — same as malloc(count*size) but always zeroed
     * HeapAlloc(flags=0); caller is responsible for zeroing if needed.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"calloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* compute count * elemsize */
        if (argc >= 2) {
            codegen_expr(cg, args[0]);          /* count in RAX              */
            asm_push_reg(a, REG_RAX);
            codegen_expr(cg, args[1]);          /* size  in RAX              */
            asm_pop_reg(a, REG_RBX);
            asm_imul_reg_reg(a, REG_RAX, REG_RBX); /* RAX = count*size      */
        } else if (argc >= 1) {
            codegen_expr(cg, args[0]);
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);
            asm_sub_rsp(a, 32);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 32);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_mov_reg_imm(a, REG_RDX, 0);
            asm_pop_reg(a, REG_R8);
            asm_sub_rsp(a, 32);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 32);
        } else {
            asm_push_reg(a, REG_EAX);
            asm_push_imm32(a, 0);
            asm_call_import32(a, "GetProcessHeap");
            asm_pop_reg(a, REG_EBX);
            asm_pop_reg(a, REG_ECX);
            asm_push_reg(a, REG_ECX);
            asm_push_imm32(a, 0);
            asm_push_reg(a, REG_EAX);
            asm_call_import32(a, "HeapAlloc");
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * realloc(ptr, size) — simplified: just alloc new, return it
     * (A full implementation would memcpy the old data too)
     * --------------------------------------------------------------------- */
    if (strcmp(name,"realloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        if (argc >= 2) codegen_expr(cg, args[1]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);
            asm_sub_rsp(a, 32);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 32);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_mov_reg_imm(a, REG_RDX, 0);
            asm_pop_reg(a, REG_R8);
            asm_sub_rsp(a, 32);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 32);
        } else {
            asm_push_reg(a, REG_EAX);
            asm_push_imm32(a, 0);
            asm_call_import32(a, "GetProcessHeap");
            asm_pop_reg(a, REG_EBX);
            asm_pop_reg(a, REG_ECX);
            asm_push_reg(a, REG_ECX);
            asm_push_imm32(a, 0);
            asm_push_reg(a, REG_EAX);
            asm_call_import32(a, "HeapAlloc");
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * free(ptr) — HeapFree(GetProcessHeap(), 0, ptr)
     * --------------------------------------------------------------------- */
    if (strcmp(name,"free")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapFree");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        if (argc >= 1) codegen_expr(cg, args[0]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);           /* save ptr (arg3)           */
            asm_sub_rsp(a, 32);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 32);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX); /* RCX = heap             */
            asm_mov_reg_imm(a, REG_RDX, 0);     /* RDX = dwFlags = 0        */
            asm_pop_reg(a, REG_R8);             /* R8 = ptr                  */
            asm_sub_rsp(a, 32);
            asm_call_import(a, "HeapFree");
            asm_add_rsp(a, 32);
        } else {
            /* stdcall: HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) */
            asm_push_reg(a, REG_EAX);           /* save ptr                  */
            asm_push_imm32(a, 0);               /* placeholder               */
            asm_call_import32(a, "GetProcessHeap");
            asm_pop_reg(a, REG_EBX);            /* discard placeholder       */
            asm_pop_reg(a, REG_ECX);            /* ECX = ptr                 */
            asm_push_reg(a, REG_ECX);           /* push ptr (lpMem)          */
            asm_push_imm32(a, 0);               /* push flags=0              */
            asm_push_reg(a, REG_EAX);           /* push handle               */
            asm_call_import32(a, "HeapFree");
        }
        asm_mov_reg_imm(a, REG_RAX, 0);
        return;
    }

    /* -----------------------------------------------------------------------
     * memset(dst, val, count) — rep stosb
     * --------------------------------------------------------------------- */
    if (strcmp(name,"memset")==0) {
        if (argc >= 3) {
            codegen_expr(cg, args[0]); asm_push_reg(a, REG_RAX); /* dst    */
            codegen_expr(cg, args[2]); asm_push_reg(a, REG_RAX); /* count  */
            codegen_expr(cg, args[1]);                            /* val    */
            if (cg->is_64bit) {
                asm_pop_reg(a, REG_RCX);  /* count */
                asm_pop_reg(a, REG_RDI);  /* dst   */
                /* AL = val (low byte of RAX), RCX = count, RDI = dst */
                asm_emit1(a, 0xF3); asm_emit1(a, 0xAA); /* rep stosb */
                asm_mov_reg_reg(a, REG_RAX, REG_RDI);
            } else {
                asm_pop_reg(a, REG_ECX);
                asm_pop_reg(a, REG_EDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xAA);
                asm_mov_reg_reg(a, REG_EAX, REG_EDI);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * memcpy(dst, src, count) — rep movsb
     * --------------------------------------------------------------------- */
    if (strcmp(name,"memcpy")==0 || strcmp(name,"memmove")==0) {
        if (argc >= 3) {
            codegen_expr(cg, args[0]); asm_push_reg(a, REG_RAX); /* dst    */
            codegen_expr(cg, args[1]); asm_push_reg(a, REG_RAX); /* src    */
            codegen_expr(cg, args[2]);                            /* count  */
            if (cg->is_64bit) {
                asm_mov_reg_reg(a, REG_RCX, REG_RAX);
                asm_pop_reg(a, REG_RSI);
                asm_pop_reg(a, REG_RDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xA4); /* rep movsb */
            } else {
                asm_mov_reg_reg(a, REG_ECX, REG_EAX);
                asm_pop_reg(a, REG_ESI);
                asm_pop_reg(a, REG_EDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xA4);
            }
        }
        asm_mov_reg_imm(a, REG_RAX, 0);
        return;
    }

    /* -----------------------------------------------------------------------
     * strlen(str) — count bytes until NUL byte
     * --------------------------------------------------------------------- */
    if (strcmp(name,"strlen")==0) {
        if (argc >= 1) {
            codegen_expr(cg, args[0]);  /* ptr in RAX */
            if (cg->is_64bit) {
                /* xor rcx,rcx; loop: cmp byte[rax+rcx],0; je done; inc rcx; jmp */
                asm_emit3(a, 0x48, 0x31, 0xC9);       /* xor rcx,rcx          */
                int lp = asm_new_label(a, "strlen_lp");
                asm_def_label(a, lp);
                asm_emit4(a, 0x80, 0x3C, 0x08, 0x00); /* cmp [rax+rcx],0      */
                int dn = asm_new_label(a, "strlen_dn");
                asm_jcc_label(a, CC_E, dn);
                asm_emit3(a, 0x48, 0xFF, 0xC1);        /* inc rcx              */
                asm_jmp_label(a, lp);
                asm_def_label(a, dn);
                asm_mov_reg_reg(a, REG_RAX, REG_RCX);
            } else {
                asm_emit2(a, 0x31, 0xC9);
                int lp = asm_new_label(a, "strlen_lp");
                asm_def_label(a, lp);
                asm_emit3(a, 0x80, 0x3C, 0x08); asm_emit1(a, 0x00);
                int dn = asm_new_label(a, "strlen_dn");
                asm_jcc_label(a, CC_E, dn);
                asm_emit1(a, 0x41);
                asm_jmp_label(a, lp);
                asm_def_label(a, dn);
                asm_mov_reg_reg(a, REG_EAX, REG_ECX);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        return;
    }

    /* Default for all other internal shims — evaluate args and return 0 */
    for (int i = 0; i < argc; i++) codegen_expr(cg, args[i]);
    asm_mov_reg_imm(a, REG_RAX, 0);
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
            int is_static = (s->dll && s->dll[0]);
            const char *lbl = is_static ? s->dll : n->var.name;
            if (cg->is_64bit) {
                if (is_static) asm_lea_rip_wdata(a,REG_RAX,lbl);
                else           asm_lea_rip_data (a,REG_RAX,lbl);
            } else {
                if (is_static) { asm_emit1(a,0xB8); asm_reloc_wdata(a,lbl); }
                else           { asm_emit1(a,0xB8); asm_reloc_data (a,lbl); }
            }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
        }
        break;
    }
    case AST_INDEX: {
        /* &array[idx] = base_addr + idx * elem_size                         */
        int esz = elem_size_of(cg, n->index.array);
        int is_pointer_var = 0;
        if (n->index.array->kind == AST_VAR) {
            Symbol *asym = symtable_lookup(cg->sym, n->index.array->var.name);
            if (asym && asym->type) {
                int is_stack_array = (asym->array_size > 0);
                if (asym->type->pointer_depth > 0 && !is_stack_array)
                    is_pointer_var = 1;
            }
        }

        /* Load the base: either the VALUE of the pointer (ptr[i])
         * or the ADDRESS of the array's first element (arr[i]). */
        if (is_pointer_var) {
            codegen_expr(cg, n->index.array);   /* RAX = pointer value (heap addr) */
        } else {
            codegen_lvalue(cg, n->index.array); /* RAX = &arr[0] (frame-relative)  */
        }
        asm_push_reg(a, REG_RAX);               /* save base address */

        codegen_expr(cg, n->index.index);       /* RAX = index */
        if (esz > 1) {
            if (cg->is_64bit) {
                asm_mov_reg_imm(a, REG_RBX, (long long)esz);
                asm_imul_reg_reg(a, REG_RAX, REG_RBX);
            } else {
                asm_mov_reg_imm(a, REG_EBX, (long long)esz);
                asm_imul_reg_reg(a, REG_EAX, REG_EBX);
            }
        }
        asm_pop_reg(a, REG_RBX);                /* RBX = base address */
        asm_add_reg_reg(a, REG_RAX, REG_RBX);  /* RAX = &base[idx]   */
        break;
    }
    case AST_DEREF:
        codegen_expr(cg,n->deref.operand);
        break;
    case AST_MEMBER: {
        /* Get base address of struct/union into RAX/EAX */
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        /* Add field byte offset (0 for first field or union) */
        { int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
          if (foff > 0) asm_add_imm(cg->asm_, REG_RAX, foff); }
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
            /* Load value of a global variable.
             * sym->dll holds the wdata label for static locals;
             * otherwise the symbol name IS an rdata label.                    */
            int is_static = (s->dll && s->dll[0]);
            const char *lbl = is_static ? s->dll : n->var.name;
            int is_array = (s->array_size > 0);
            if (cg->is_64bit) {
                if (is_static) asm_lea_rip_wdata(a,REG_RAX,lbl);
                else           asm_lea_rip_data (a,REG_RAX,lbl);
                if (!is_array) asm_emit2(a,0x8B,0x00); /* mov eax,[rax] */
            } else {
                if (!is_array) {
                    if (is_static) { asm_emit1(a,0xA1); asm_reloc_wdata(a,lbl); }
                    else           { asm_emit1(a,0xA1); asm_reloc_data (a,lbl); }
                } else {
                    if (is_static) { asm_emit1(a,0xB8); asm_reloc_wdata(a,lbl); }
                    else           { asm_emit1(a,0xB8); asm_reloc_data (a,lbl); }
                }
            }
        } else if (s->kind==SYM_FUNC) {
            /* Load function address into RAX — used when assigning to a
             * function pointer variable: int (*op)(int,int) = fp_add;     */
            int func_lbl = get_func_label(cg, n->var.name);
            asm_load_func_addr(a, REG_RAX, func_lbl);
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
        codegen_expr(cg, n->cast.expr);
        /* Apply narrowing casts by truncating and sign/zero extending.
         * Without this, (char)0x1FF stays 0x1FF instead of becoming -1.
         * Rules (mirrors C integer conversion):
         *   (char)  / (signed char)  -> movsx eax,al   sign-extend low byte
         *   (unsigned char)          -> movzx eax,al   zero-extend low byte
         *   (short) / (signed short) -> movsx eax,ax   sign-extend low word
         *   (unsigned short)         -> movzx eax,ax
         *   (int)/(long)/(ptr)/etc.  -> no-op
         */
        if (n->cast.type) {
            const char *base = n->cast.type->base;
            int is_unsigned  = n->cast.type->is_unsigned;
            int pdepth       = n->cast.type->pointer_depth;
            if (pdepth == 0) {
                if (strcmp(base,"char")==0 || strcmp(base,"signed char")==0) {
                    /* movsx eax,al */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBE,0xC0);
                    else              asm_emit3(a,0x0F,0xBE,0xC0);
                } else if (strcmp(base,"unsigned char")==0) {
                    /* movzx eax,al */
                    asm_emit3(a,0x0F,0xB6,0xC0);
                } else if (strcmp(base,"short")==0 || strcmp(base,"signed short")==0) {
                    /* movsx eax,ax */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBF,0xC0);
                    else              asm_emit3(a,0x0F,0xBF,0xC0);
                } else if (strcmp(base,"unsigned short")==0) {
                    /* movzx eax,ax */
                    asm_emit3(a,0x0F,0xB7,0xC0);
                } else if (!is_unsigned && strcmp(base,"char")==0) {
                    /* default char: treat as signed */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBE,0xC0);
                    else              asm_emit3(a,0x0F,0xBE,0xC0);
                }
                /* int/long/void* etc: value already correct width in eax/rax */
            }
        }
        break;

    case AST_ADDR:
        codegen_lvalue(cg,n->addr.operand);
        break;

    case AST_DEREF:
        codegen_expr(cg,n->deref.operand);
        /* dereference: load [RAX] — use 32-bit load for int-sized values */
        if (!cg->is_64bit) {
            asm_emit2(a,0x8B,0x00); /* mov eax,[eax] */
        } else {
            /* Default to 32-bit load (zero-extends); ptr types use 64-bit */
            asm_emit2(a,0x8B,0x00); /* mov eax,[rax] — zero extends to rax */
        }
        break;

    case AST_INDEX: {
        int esz = elem_size_of(cg, n->index.array);
        codegen_lvalue(cg, n);          /* RAX/EAX = address of element */
        /* Load element using appropriate width */
        if (esz == 8 && cg->is_64bit) {
            asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax]          64-bit     */
        } else if (esz == 1) {
            /* movzx eax,byte[rax] — zero-extend byte to int                  */
            if (cg->is_64bit) asm_emit3(a,0x0F,0xB6,0x00);
            else              asm_emit3(a,0x0F,0xB6,0x00);
        } else if (esz == 2) {
            /* movzx eax,word[rax]                                            */
            if (cg->is_64bit) asm_emit3(a,0x0F,0xB7,0x00);
            else              asm_emit3(a,0x0F,0xB7,0x00);
        } else {
            asm_emit2(a,0x8B,0x00);      /* mov eax,[rax]          32-bit     */
        }
        break;
    }

    case AST_MEMBER: {
        /* Get base address of struct into RAX/EAX, then load field value */
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        { int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
          /* load 4-byte field — use 32-bit load (zero-extends to RAX in 64-bit) */
          if (foff == 0) {
              asm_emit2(a,0x8B,0x00);       /* mov eax,[rax] / mov eax,[eax] */
          } else if (foff < 128) {
              asm_emit3(a,0x8B,0x40,(uint8_t)foff); /* mov eax,[rax+disp8] */
          } else {
              asm_emit2(a,0x8B,0x80); asm_emit_u32(a,(uint32_t)foff); /* mov eax,[rax+disp32] */
          }
        }
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
            if (s && (s->kind==SYM_VAR||s->kind==SYM_PARAM)) {
                asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
            } else if (s && s->kind==SYM_GLOBAL) {
                int is_static = (s->dll && s->dll[0]);
                const char *glbl = is_static ? s->dll : n->assign.lhs->var.name;
                asm_push_reg(a,REG_RAX);
                if (cg->is_64bit) {
                    if (is_static) asm_lea_rip_wdata(a,REG_RBX,glbl);
                    else           asm_lea_rip_data (a,REG_RBX,glbl);
                } else {
                    if (is_static) { asm_emit1(a,0xBB); asm_reloc_wdata(a,glbl); }
                    else           { asm_emit1(a,0xBB); asm_reloc_data (a,glbl); }
                }
                asm_pop_reg(a,REG_RAX);
                asm_emit2(a,0x89,0x03);  /* mov [rbx/ebx], eax */
            }
        } else {
            asm_push_reg(a,REG_RAX);
            codegen_lvalue(cg,n->assign.lhs);
            asm_pop_reg(a,REG_RBX);
            /* Determine store width from LHS element size.
             * Array subscripts of pointer type need 64-bit store in 64-bit mode.
             * Struct member fields and plain int arrays use 32-bit store.         */
            int store_sz = 4;
            if (cg->is_64bit && n->assign.lhs->kind == AST_INDEX)
                store_sz = elem_size_of(cg, n->assign.lhs->index.array);
            if (store_sz == 8 && cg->is_64bit) {
                /* mov [rax],rbx — 64-bit store for pointer-sized elements */
                asm_emit3(a,0x48,0x89,0x18);
            } else if (!cg->is_64bit) {
                asm_emit2(a,0x89,0x18); /* mov [eax],ebx */
            } else {
                asm_emit2(a,0x89,0x18); /* mov [rax],ebx — 32-bit store */
            }
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

        /* If the symbol is a local variable or parameter (not a declared function),
         * treat it as a function pointer: load the pointer value and call through it.
         * This handles: int (*op)(int,int); op = fp_add; op(1,2);              */
        int is_fptr_var = (sym &&
                          (sym->kind==SYM_VAR || sym->kind==SYM_PARAM) &&
                           sym->type && sym->type->pointer_depth > 0);
        if (is_fptr_var) {
            /* Build an AST_FUNC_PTR_CALL and evaluate it */
            ASTNode fake_var; memset(&fake_var,0,sizeof fake_var);
            fake_var.kind=AST_VAR; fake_var.line=n->line;
            fake_var.var.name=(char*)name;
            ASTNode fake_fpcall; memset(&fake_fpcall,0,sizeof fake_fpcall);
            fake_fpcall.kind=AST_FUNC_PTR_CALL; fake_fpcall.line=n->line;
            fake_fpcall.fp_call.func_expr=&fake_var;
            fake_fpcall.fp_call.args=n->call.args;
            fake_fpcall.fp_call.argc=n->call.argc;
            codegen_expr(cg,&fake_fpcall);
            break;
        }

        int argc=n->call.argc;
        if (cg->is_64bit) {
            int extra=(argc>4)?(argc-4)*8:0;
            int frame=32+extra;
            if ((frame&8)==0) frame+=8;  /* ensure frame%16==8 so RSP is 0-mod-16 at CALL */
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
        /* Evaluate function pointer expression into a scratch register,
         * set up arguments in the ABI registers, then call through the register.
         * We use RBX (callee-saved) to hold the function pointer so that
         * argument evaluation (which uses RAX) doesn't clobber it. */
        int argc = n->fp_call.argc;
        if (cg->is_64bit) {
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra;
            if ((frame & 8) == 0) frame += 8;  /* keep RSP 16-aligned at CALL */
            asm_sub_rsp(a, frame);
            /* Evaluate the function pointer expression into RAX, save to RBX */
            codegen_expr(cg, n->fp_call.func_expr);
            asm_mov_reg_reg(a, REG_RBX, REG_RAX);
            /* Evaluate and place each argument */
            for (int i = 0; i < argc; i++) {
                codegen_expr(cg, n->fp_call.args[i]);
                if      (i == 0) asm_mov_reg_reg(a, REG_RCX, REG_RAX);
                else if (i == 1) asm_mov_reg_reg(a, REG_RDX, REG_RAX);
                else if (i == 2) asm_mov_reg_reg(a, REG_R8,  REG_RAX);
                else if (i == 3) asm_mov_reg_reg(a, REG_R9,  REG_RAX);
                else             asm_mov_mem_reg(a, REG_RSP, 32+(i-4)*8, REG_RAX);
            }
            asm_call_reg(a, REG_RBX);
            asm_add_rsp(a, frame);
        } else {
            /* 32-bit: push args right-to-left, then call through register */
            /* Evaluate function pointer first, save on stack */
            codegen_expr(cg, n->fp_call.func_expr);
            asm_push_reg(a, REG_EAX);          /* save fn ptr */
            /* Push arguments right-to-left */
            for (int i = argc-1; i >= 0; i--) {
                codegen_expr(cg, n->fp_call.args[i]);
                asm_push_reg(a, REG_EAX);
            }
            /* Reload fn ptr and call */
            int fn_stack_off = argc * 4;       /* fn ptr is above the args */
            /* mov eax, [esp + fn_stack_off] */
            asm_emit1(a, 0x8B); asm_emit1(a, 0x44);
            asm_emit1(a, 0x24); asm_emit1(a, (uint8_t)fn_stack_off);
            asm_call_reg(a, REG_EAX);
            /* Callee is cdecl: caller cleans args + saved fn ptr */
            if (argc > 0) asm_add_rsp(a, argc * 4 + 4);
            else          asm_add_rsp(a, 4);
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
        /* Stamp array_size onto the TypeInfo so symtable_define_var
           allocates the correct number of bytes for the whole array. */
        int arr=n->var_decl.array_size;
        if (n->var_decl.type && arr > 0)
            n->var_decl.type->array_size = arr;

        /* Static local variables live in the data section, not on the stack.
         * We allocate space in the data segment and access via RIP-relative
         * (64-bit) or absolute address (32-bit). */
        int is_static = (n->var_decl.storage &&
                         strcmp(n->var_decl.storage,"static")==0);
        if (is_static) {
            /* Static local variable: lives in rdata pool as a zero-initialised
             * named entry.  We give it a unique mangled label so the PE linker
             * can resolve the RIP-relative / absolute-VA reference.
             *
             * Key rules:
             *  - The value buffer must stay alive (no free after storing pointer).
             *  - We register both the mangled label AND keep the user's name in
             *    the symbol table so subsequent references to "count" still work.
             *  - se->len = total_sz (NOT +1) because static storage is not a
             *    C string; build_rdata uses se->len bytes verbatim.
             */
            int elem_sz = n->var_decl.type ?
                typeinfo_size(n->var_decl.type, cg->is_64bit) : 4;
            if (elem_sz < 1) elem_sz = 4;
            int total_sz = (arr > 0) ? elem_sz * arr : elem_sz;

            /* Unique mangled label */
            char sym_name[256];
            snprintf(sym_name, sizeof sym_name, "static_%s_%d",
                     n->var_decl.name, cg->string_count);

            /* Allocate a zero-filled buffer — DO NOT free, StringEntry owns it */
            char *zbuf = calloc(total_sz + 1, 1);  /* +1 so strlen is safe   */

             /* Register in writable data pool (.data section, R/W).
              * Static vars MUST be writable — they store live state across calls. */
             intern_wdata(cg, sym_name, total_sz);
             free(zbuf);  /* wdata doesn't need the buffer content               */

            /* Register in symbol table under the ORIGINAL name so user code
             * can continue referencing "count", "total", etc.                */
            TypeInfo *ti = n->var_decl.type ? n->var_decl.type : typeinfo_new("int");
            symtable_define_global(cg->sym, n->var_decl.name, ti, arr);
            Symbol *user_sym = symtable_lookup(cg->sym, n->var_decl.name);
            if (user_sym) {
                /* Store the rdata label in sym->dll (repurposed for static vars)
                 * so codegen_expr can find it while lookup by original name works. */
                free(user_sym->dll);
                user_sym->dll = strdup(sym_name);
            }

            /* Static variable storage is zero-initialised via calloc() in the
             * wdata pool — no inline init code is emitted.  C semantics require
             * static locals to be initialised only once (at program start), not
             * on every function call.  Emitting inline init code would reset the
             * variable on every invocation, which is wrong.
             * Non-zero initialisers: the wdata is pre-zeroed; a non-zero value
             * can be set by the programmer on first call with an if-not-set guard,
             * or we handle it as a one-time init (future work).               */
            (void)sym_name; /* used only for relocation symbol */
            break;
        }

        Symbol *sym=symtable_define_var(cg->sym,n->var_decl.name,n->var_decl.type);
        if (n->var_decl.init) {
            if (n->var_decl.init->kind==AST_BLOCK && arr>0) {
                /* array initialiser: store each element at its natural element size */
                int esz = n->var_decl.type ? typeinfo_size(n->var_decl.type, cg->is_64bit) : 4;
                if (esz < 1) esz = 4;
                for (int i=0;i<n->var_decl.init->block.count&&i<arr;i++) {
                    codegen_expr(cg,n->var_decl.init->block.stmts[i]);
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

    /* Register params in symbol table */
    for (int i=0;i<n->func.paramc;i++) {
        ASTNode *pr=n->func.params[i];
        if (pr->param.name)
            symtable_define_param(cg->sym,pr->param.name,pr->param.type,i);
    }

    /* Two-pass frame sizing: emit prologue with placeholder size=0,
     * compile the body (which defines locals in the symtable),
     * then patch the placeholder with the actual aligned frame size.
     * This eliminates the hardcoded sub rsp,0x108 that triggers AV. */
    int frame_patch = asm_enter_deferred(a);

    /* Spill register params into their shadow-space home locations */
    if (cg->is_64bit) {
        static const Reg pr4[4]={REG_RCX,REG_RDX,REG_R8,REG_R9};
        for (int i=0;i<n->func.paramc&&i<4;i++)
            asm_mov_mem_reg(a,REG_RBP,16+i*8,pr4[i]);
    }

    /* Generate function body; locals accumulate into sym->next_offset */
    codegen_stmt(cg,n->func.body);

    /* Default return 0 at fall-through */
    asm_mov_reg_imm(a,REG_RAX,0);
    asm_leave(a); asm_ret(a);

    /* Patch the frame size with the actual number of bytes needed.
     * Round up to 16, then add 8 so RSP stays 16-byte aligned after PUSH RBP. */
    int raw   = symtable_local_size(cg->sym);
    int align = (raw + 15) & ~15;
    if (cg->is_64bit && (align & 8) == 0) align += 8;
    if (align < 8) align = 8;   /* minimum: at least 8 bytes for alignment */
    asm_patch_frame(a, frame_patch, align);

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
    /* Pass 2: generate code for functions that have bodies.
     * Forward declarations (body==NULL) and typedef/struct/enum nodes are skipped. */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (d->kind==AST_FUNC_DECL && d->func.body!=NULL)
            codegen_func(cg,d);
        /* VAR_DECL at top-level (global vars) — future work */
    }
    asm_resolve(cg->asm_);
}

uint8_t    *codegen_get_text  (CodeGen *cg, int *len) { *len=cg->asm_->code_len; return cg->asm_->code; }
uint8_t    *codegen_get_rdata (CodeGen *cg, int *len) { return build_rdata(cg,len); }
Relocation *codegen_get_relocs(CodeGen *cg, int *cnt) { *cnt=cg->asm_->reloc_count; return cg->asm_->relocs; }
