/*
 * codegen_arm64.c — AArch64 code generator for the squash compiler.
 *
 * Calling convention: AAPCS64 (used by both Linux and Windows ARM64 for
 * the first 8 integer args, which is all we need here).
 *   x0-x7 : args / return value in x0
 *   x9-x15 : caller-saved scratch
 *   x19-x28: callee-saved (we save x19 as an extra scratch register)
 *   x29 (FP), x30 (LR): saved in prologue via STP
 *
 * Frame layout (after prologue):
 *   [x29]    = saved x29 (old FP)
 *   [x29+8]  = saved x30 (LR)
 *   [x29+16] = local var 0  (= slot for param 0 or first local)
 *   [x29+24] = local var 1
 *   ...
 *   [x29+16+N] = temp expression stack slots
 *
 * Local variable sym->offset is always negative (symtable convention).
 * ARM64 frame offset = -sym->offset  (e.g. offset=-16 → [x29+16]).
 */

#include "codegen_arm64.h"
#include "symtable.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* True for the fixed set of CRT-shaped names (malloc, printf, memcpy, ...)
 * codegen.c's x86-64 path always resolves specially — see codegen.h's own
 * comment. Used by a64_emit_linux_extern_call to avoid ever routing one of
 * these through the new .sqo/.a cross-object resolution path: ARM64 has no
 * inline-shim reimplementation the way x86-64 does, so every one of these
 * names reaches ordinary call codegen and must keep resolving via the
 * always-assumed "libc.so.6" GOT import that already correctly handles
 * them today. */
int is_internal_shim(const char *name);

/* Mirrors codegen.c's identical x86-64 helper — see codegen.h's
 * CodeGen.sqo_export_names comment for why this check exists. */
static int a64_is_sqo_export(CodeGenA64 *cg, const char *name) {
    for (int i=0;i<cg->sqo_export_count;i++)
        if (strcmp(cg->sqo_export_names[i], name)==0) return 1;
    return 0;
}

/* Forward declaration from assembler.c */
char *my_strdup(const char *s);

/* Helper: size of a type in bytes for load/store */
static int typeinfo_size_a64(TypeInfo *t, int is64) {
    if (!t) return 8;
    if (t->pointer_depth > 0) return is64 ? 8 : 4;
    if (t->array_size > 0)    return is64 ? 8 : 4; /* pointer to array */
    if (strcmp(t->base, "char")  == 0) return 1;
    if (strcmp(t->base, "short") == 0) return 2;
    if (strcmp(t->base, "int")   == 0) return 4;
    if (strcmp(t->base, "long")  == 0) return is64 ? 8 : 4;
    if (strcmp(t->base, "long long") == 0) return 8;
    if (strcmp(t->base, "float")  == 0) return 4;
    if (strcmp(t->base, "double") == 0) return 8;
    if (strcmp(t->base, "void") == 0)   return 0;
    return is64 ? 8 : 4;
}

/* Size of a type, resolving struct/typedef names through the symbol table
 * (needed since squash's own struct-size machinery is the only reliable
 * source of truth for struct sizes — codegen can't recompute them from
 * TypeInfo alone). ptr_decrement lets callers ask "what does this pointer
 * point AT" without mutating/copying the TypeInfo (squash's own codegen
 * can't correctly copy a struct by value via pointer dereference, so this
 * avoids ever doing "TypeInfo tmp = *t; tmp.pointer_depth--;").
 * ignore_array skips the array-size multiplication, for callers that want
 * a single element's size. */
static int a64_sizeof_type_sym_ex(SymTable *sym, TypeInfo *t, int is64,
                                   int ptr_decrement, int ignore_array) {
    if (!t) return is64 ? 8 : 4;
    int pdepth = t->pointer_depth - ptr_decrement;
    if (pdepth < 0) pdepth = 0;
    if (!t->base && pdepth == 0) return 4;
    if (!ignore_array && t->array_size > 0) {
        int esz = a64_sizeof_type_sym_ex(sym, t, is64, ptr_decrement, 1);
        int total = esz * t->array_size;
        if (t->array_size2 > 0) total *= t->array_size2;
        return total;
    }
    if (pdepth > 0) return is64 ? 8 : 4;
    const char *b = t->base;
    int basic;
    if (!b) basic = 4;
    else if (strcmp(b,"char")==0) basic = 1;
    else if (strcmp(b,"short")==0) basic = 2;
    else if (strcmp(b,"int")==0) basic = 4;
    else if (strcmp(b,"long")==0) basic = is64 ? 8 : 4;
    else if (strcmp(b,"long long")==0) basic = 8;
    else if (strcmp(b,"float")==0) basic = 4;
    else if (strcmp(b,"double")==0) basic = 8;
    else if (strcmp(b,"void")==0) basic = 0;
    /* Anything else (a struct/union/typedef/enum name, e.g. "VkFormat")
     * is unresolved at this point — 4, not 8, is the correct sentinel for
     * "keep looking" below, matching codegen.c's identical x86-64 helper
     * (sizeof_type_sym(): "int basic = typeinfo_size(...); if (basic != 4)
     * return basic;"). This matters beyond just being consistent: a real
     * C `enum` (4 bytes, no separate tag registration to resolve further)
     * hits the self-referential-typedef guard just below for the
     * extremely common "typedef enum VkFormat {...} VkFormat;" idiom
     * (used throughout Vulkan/Windows headers) and falls through to
     * `return basic` a few lines down WITHOUT ever finding a "struct
     * VkFormat" entry — so `basic` itself has to already be the right
     * answer by then. Defaulting it to pointer-sized 8 here (the previous
     * behavior) silently returned 8 for every such enum field, which
     * corrupted every struct field layout after the first one on ARM64
     * (reproduced directly: VkSwapchainCreateInfoKHR's imageFormat/
     * imageColorSpace/imageSharingMode fields — all enums — each added a
     * spurious 4-byte gap, so every field from imageFormat onward read
     * from 8-40 bytes past its real offset, and vkCreateSwapchainKHR
     * received garbage extent/format/transform values despite every
     * printed diagnostic during development happening to look plausible
     * for other reasons). Struct/union names are unaffected: they still
     * resolve to their real size via the symbol-table lookups below this
     * point either way. */
    else basic = 4;
    if (basic != 4) return basic;
    if (sym && b) {
        const char *bare = b;
        if (strncmp(bare,"struct ",7)==0) bare+=7;
        else if (strncmp(bare,"union ",6)==0) bare+=6;
        if (bare != b) {
            char key[256]; snprintf(key,sizeof key,"struct %s",bare);
            Symbol *ss = symtable_lookup(sym, key);
            if (ss && ss->struct_size > 0) return ss->struct_size;
        }
        Symbol *td = symtable_lookup(sym, b);
        if (td && td->kind==SYM_TYPEDEF && td->type) {
            /* Guard against "typedef enum Foo { ... } Foo;" (see codegen.c's
             * sizeof_type_sym() -- the identical, already-fixed x86-64
             * guard for this exact bug -- for the full rationale): when the
             * typedef's own type->base is textually identical to the name
             * just looked up, recursing calls this function with an
             * unchanged argument forever, a genuine infinite recursion
             * (confirmed directly: a real stack-overflow crash compiling
             * triangle_vulkan.c for ARM64, which declares several such
             * "typedef enum Foo {...} Foo;" Vulkan enums). Enums are
             * int-sized with no separate tag registration to resolve
             * further, so stop here and fall through to `basic` instead. */
            if (!(td->type->base && strcmp(td->type->base, b) == 0))
                return a64_sizeof_type_sym_ex(sym, td->type, is64, 0, 0);
        }
        char skey[256]; snprintf(skey,sizeof skey,"struct %s",b);
        Symbol *ss2 = symtable_lookup(sym, skey);
        if (ss2 && ss2->struct_size > 0) return ss2->struct_size;
    }
    return basic;
}
static int a64_sizeof_type_sym(SymTable *sym, TypeInfo *t, int is64) {
    return a64_sizeof_type_sym_ex(sym, t, is64, 0, 0);
}

static int a64_is_float_type(SymTable *sym, TypeInfo *t) {
    if (!t) return 0;
    if (t->pointer_depth > 0) return 0;
    if (strcmp(t->base, "double") == 0 || strcmp(t->base, "float") == 0 || t->is_float)
        return 1;
    /* Resolve typedefs (e.g. "typedef double myfloat;") — t->base holding a
     * typedef name doesn't match "double"/"float" directly above, and
     * t->is_float is only set on the underlying type, not the alias. */
    Symbol *td = symtable_lookup(sym, t->base);
    if (td && td->kind == SYM_TYPEDEF && td->type)
        return strcmp(td->type->base,"double")==0 ||
               strcmp(td->type->base,"float")==0 || td->type->is_float;
    return 0;
}

/* =========================================================================
 * Struct/union field resolution.
 * Only handles the common case exercised by squash programs: obj is a plain
 * local/param/global variable of struct/union type, or a chain of such
 * plain member accesses (obj.field1.field2). Arrow chains through pointers
 * and array-of-struct indexing are not resolved (fall back to offset 0),
 * mirroring the scope of what codegen.c's field_byte_offset covers for the
 * common cases while staying much simpler.
 * ========================================================================= */
static ASTNode *a64_find_struct_def(CodeGenA64 *cg, const char *type_name) {
    if (!type_name) return NULL;
    const char *bare = type_name;
    if (strncmp(bare, "struct ", 7) == 0) bare += 7;
    else if (strncmp(bare, "union ", 6) == 0) bare += 6;
    char key[256];
    snprintf(key, sizeof key, "struct %s", bare);
    Symbol *ss = symtable_lookup(cg->sym, key);
    if (ss && ss->struct_node) return ss->struct_node;
    /* typedef indirection, e.g. "typedef struct {...} Point;" — but guard
     * against "typedef enum VkFormat {...} VkFormat;" (extremely common
     * throughout Vulkan/Windows headers, see a64_sizeof_type_sym_ex's
     * identical guard for the full rationale): when the typedef's own
     * type->base is textually identical to the name just looked up,
     * recursing calls this function with an unchanged argument forever.
     * An enum has no struct_node to find either way, so just report "not
     * a struct" instead of recursing. */
    Symbol *td = symtable_lookup(cg->sym, type_name);
    if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base &&
        strcmp(td->type->base, type_name) != 0)
        return a64_find_struct_def(cg, td->type->base);
    return NULL;
}

/* Resolves field_name within a struct/union AST_STRUCT_DECL node, recursing
 * into anonymous struct/union members (parser_new4.c gives these synthesized
 * "__anon_N" field names) since a member declared inside one is accessed as
 * if it were a direct member of the enclosing struct. */
static ASTNode *a64_resolve_field_in_def(CodeGenA64 *cg, ASTNode *sd,
                                          const char *field_name, int *out_offset) {
    int is_union = sd->struct_decl.is_union;
    int offset = 0;
    int i;
    for (i = 0; i < sd->struct_decl.nfields; i++) {
        ASTNode *f = sd->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD) continue;

        /* This field's own alignment must be applied to `offset` BEFORE
         * using it as this field's address — not just when accumulating
         * past it to find the NEXT field's position. The previous version
         * aligned offset only in the size-accumulation branch below, so a
         * field needing wider alignment than its predecessor (e.g. an
         * 8-byte pointer field directly after a 4-byte int field, as in
         * Token's "kind; start;") returned the predecessor's unaligned
         * end offset instead of its own correctly-padded one — off by
         * exactly the padding gap (4 instead of 8 for that example),
         * silently aliasing onto the padding bytes ahead of the real field. */
        int elem_sz = 4, total_sz = 4, align = 4;
        if (!is_union) {
            elem_sz = f->field.type ? a64_sizeof_type_sym(cg->sym, f->field.type, 1) : 4;
            if (elem_sz < 1) elem_sz = 4;
            total_sz = elem_sz;
            if (f->field.array_size > 0) {
                total_sz *= f->field.array_size;
                if (f->field.array_size2 > 0) total_sz *= f->field.array_size2; /* T x[N][M] */
            }
            /* A struct/union-typed field's real alignment is the max
             * alignment of ITS OWN members, not its total byte size —
             * capping align at min(elem_sz,8) conflates the two, which is
             * wrong whenever a struct's size doesn't match its natural
             * alignment (e.g. Vulkan's VkExtent2D: two uint32_t members,
             * size 8 but alignment 4). That silently inserted a spurious
             * 4-byte pad in front of any such field, and cascaded into
             * every field after it (reproduced directly: VkSwapchainCreateInfoKHR's
             * "VkExtent2D imageExtent" mid-struct pushed imageArrayLayers/
             * imageUsage/imageSharingMode/etc. 4 bytes past their real
             * offsets). Falls back to the elem_sz-capped heuristic for
             * plain scalar fields, matching prior behavior exactly. */
            /* pointer_depth==0 guard: a field that's a POINTER TO a struct
             * (e.g. "const VkSubpassDependency *pDependencies") must align
             * like the pointer it is (8), not like its pointee's struct
             * alignment — a64_find_struct_def() resolves by base type name
             * alone and doesn't know about pointer_depth, so without this
             * guard a trailing "T *p" field right after a plain uint32_t
             * field only got padded to whatever T's own (possibly
             * 4-byte) alignment was instead of the required 8, corrupting
             * every field after it (reproduced directly: VkRenderPassCreateInfo's
             * trailing "const VkSubpassDependency *pDependencies" landed 4
             * bytes short of its real offset). */
            ASTNode *fsd = (f->field.type && f->field.type->pointer_depth == 0)
                ? a64_find_struct_def(cg, f->field.type->base) : NULL;
            if (fsd) {
                align = symtable_compute_struct_alignment(cg->sym, fsd);
                if (align < 1) align = elem_sz < 8 ? elem_sz : 8;
            } else {
                align = elem_sz < 8 ? elem_sz : 8;
            }
            if (align > 1) offset = (offset + align - 1) & ~(align - 1);
        }

        if (f->field.name && strcmp(f->field.name, field_name) == 0) {
            *out_offset = is_union ? 0 : offset;
            return f;
        }
        if (f->field.name && strncmp(f->field.name, "__anon_", 7) == 0 && f->field.type) {
            ASTNode *anon_sd = a64_find_struct_def(cg, f->field.type->base);
            if (anon_sd) {
                int inner_off = 0;
                ASTNode *found = a64_resolve_field_in_def(cg, anon_sd, field_name, &inner_off);
                if (found) {
                    *out_offset = (is_union ? 0 : offset) + inner_off;
                    return found;
                }
            }
        }
        if (!is_union) {
            offset += total_sz;
        }
    }
    return NULL;
}

/* Resolves field_name within obj_node's struct/union type. On success returns
 * the AST_FIELD node and sets *out_offset (0 for any union member). */
static ASTNode *a64_resolve_field(CodeGenA64 *cg, ASTNode *obj_node,
                                   const char *field_name, int *out_offset) {
    const char *type_name = NULL;
    if (obj_node && obj_node->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(cg->sym, obj_node->var.name);
        if (vs && vs->type) type_name = vs->type->base;
    } else if (obj_node && obj_node->kind == AST_MEMBER) {
        int inner_off;
        ASTNode *inner = a64_resolve_field(cg, obj_node->member.obj,
                                            obj_node->member.field, &inner_off);
        if (inner && inner->field.type) type_name = inner->field.type->base;
    } else if (obj_node && obj_node->kind == AST_INDEX) {
        /* obj_node is e.g. "st->macros[0]" (a struct-typed array element) —
         * field_name's type is looked up in the ARRAY's element type, i.e.
         * the array variable/field's own declared base type (for T x[N],
         * the field/symbol's type IS the element type T, with array_size
         * just recording the dimension). Without this, any field access
         * chained off an array index (arr[i].field, st->arr[i].field) fell
         * through with type_name still NULL, so a64_resolve_field_in_def
         * was never reached and every such field silently resolved to
         * offset 0 — aliasing every field in the struct onto the first one. */
        ASTNode *base = obj_node->index.array;
        if (base && base->kind == AST_VAR) {
            Symbol *vs = symtable_lookup(cg->sym, base->var.name);
            if (vs && vs->type) type_name = vs->type->base;
        } else if (base && base->kind == AST_MEMBER) {
            int inner_off;
            ASTNode *inner = a64_resolve_field(cg, base->member.obj,
                                                base->member.field, &inner_off);
            if (inner && inner->field.type) type_name = inner->field.type->base;
        }
    } else if (obj_node && obj_node->kind == AST_CAST && obj_node->cast.type && obj_node->cast.type->base) {
        /* ((T*)expr)->field / ((T*)expr).field — an explicit cast names the
         * pointee type directly, matching x64 codegen.c's identical fix
         * (see its comment there for the real-world bug this closes: a
         * COM-vtable-style "((Foo*)p)->lpVtbl->Method()" call silently
         * truncated an 8-byte function-pointer field to 4 bytes and
         * crashed calling through the corrupted address, because none of
         * the struct-type-resolution call sites handled a cast as the
         * chain's base — this was ARM64's twin of that gap). */
        type_name = obj_node->cast.type->base;
    }
    if (!type_name) return NULL;

    ASTNode *sd = a64_find_struct_def(cg, type_name);
    if (!sd) return NULL;
    return a64_resolve_field_in_def(cg, sd, field_name, out_offset);
}

static void a64_expr(CodeGenA64 *cg, ASTNode *n);
static void a64_lvalue(CodeGenA64 *cg, ASTNode *n);

/* struct_copy_size_of / struct_copy_addr_of — support whole-struct
 * assignment ("dst = src;" where both sides denote a struct/union VALUE,
 * not a scalar). Without this, AST_ASSIGN's normal path below only ever
 * moves a single register's worth of bytes (silently truncating any
 * multi-field struct copy to its first 4/8 bytes and leaving every field
 * after that zeroed/stale) — reproduced directly with a real Vulkan
 * pattern, "VkBufferMemoryBarrier bufBarriers[2]; ...; bufBarriers[1] =
 * bufBarriers[0];" (a common "copy then tweak one field" idiom): every
 * field after the first stayed at its memset(0) value, and the driver
 * later crashed dereferencing/using the resulting all-zero VkBuffer
 * handle/queue-family-index pair from the barrier. Mirrors codegen.c's
 * identical, already-working x86-64 struct_copy_size_of/addr_of pair
 * (that implementation covers AST_DEREF and AST_MEMBER too, along with a
 * genuinely optimized per-size copy loop; this covers the two shapes this
 * backend's own callers actually hit — AST_VAR and AST_INDEX — with a
 * simpler byte-at-a-time copy, since ARM64 struct copies here are small
 * and correctness matters far more than shaving a few instructions). */
static int a64_struct_copy_size_of(CodeGenA64 *cg, ASTNode *node) {
    if (!node) return 0;
    if (node->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, node->var.name);
        if (!s || !s->type || s->type->pointer_depth != 0 || s->array_size > 0) return 0;
        if (!a64_find_struct_def(cg, s->type->base)) return 0;
        int sz = a64_sizeof_type_sym(cg->sym, s->type, 1);
        return sz > 0 ? sz : 0;
    }
    if (node->kind == AST_INDEX) {
        /* arr[i] where arr is an array of struct/union. */
        ASTNode *base = node->index.array;
        const char *type_name = NULL;
        if (base && base->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, base->var.name);
            if (s && s->type) type_name = s->type->base;
        } else if (base && base->kind == AST_MEMBER) {
            int off;
            ASTNode *f = a64_resolve_field(cg, base->member.obj, base->member.field, &off);
            if (f && f->field.type) type_name = f->field.type->base;
        }
        if (!type_name || !a64_find_struct_def(cg, type_name)) return 0;
        TypeInfo tmp; memset(&tmp, 0, sizeof(tmp));
        tmp.base = (char *)type_name; tmp.array_size = -1;
        int sz = a64_sizeof_type_sym(cg->sym, &tmp, 1);
        return sz > 0 ? sz : 0;
    }
    return 0;
}
static void a64_struct_copy_addr_of(CodeGenA64 *cg, ASTNode *node) {
    if (node->kind == AST_DEREF) a64_expr(cg, node->deref.operand);
    else a64_lvalue(cg, node);
}

/* Element size for a field used as an array (e.g. the "1" in u.bytes[i]
 * for char bytes[4]). */
static int a64_field_elem_size(CodeGenA64 *cg, ASTNode *member_node) {
    if (!member_node || member_node->kind != AST_MEMBER) return 8;
    int off;
    ASTNode *f = a64_resolve_field(cg, member_node->member.obj, member_node->member.field, &off);
    if (f && f->field.type) {
        int base_elem = a64_sizeof_type_sym_ex(cg->sym, f->field.type, 1, 1, 1);
        if (base_elem < 1) base_elem = 1;
        if (f->field.array_size2 > 0) return base_elem * f->field.array_size2; /* T x[N][M] */
        return base_elem;
    }
    return 8;
}

/* =========================================================================
 * String / float constant pool management
 * ========================================================================= */
static const char *a64_intern_string(CodeGenA64 *cg, const char *val, int vlen) {
    int i;
    for (i = 0; i < cg->string_count; i++) {
        if (cg->strings[i].len == vlen &&
            memcmp(cg->strings[i].value, val, vlen) == 0)
            return cg->strings[i].label;
    }
    if (cg->string_count == cg->string_cap) {
        cg->string_cap = cg->string_cap ? cg->string_cap * 2 : 64;
        cg->strings = (A64StringEntry*)realloc(cg->strings,
                           cg->string_cap * sizeof(A64StringEntry));
    }
    int idx = cg->string_count++;
    char lbl[32];
    snprintf(lbl, sizeof lbl, "__a64str%d", idx);
    cg->strings[idx].value  = (char*)malloc(vlen + 1);
    memcpy(cg->strings[idx].value, val, vlen);
    cg->strings[idx].value[vlen] = '\0';
    cg->strings[idx].len    = vlen;
    cg->strings[idx].label  = my_strdup(lbl);
    cg->strings[idx].offset = cg->string_pool_size;
    /* Grow rdata buffer */
    if (cg->asm_->rdata_len + vlen + 1 > cg->asm_->rdata_cap) {
        while (cg->asm_->rdata_len + vlen + 1 > cg->asm_->rdata_cap)
            cg->asm_->rdata_cap *= 2;
        cg->asm_->rdata = (uint8_t*)realloc(cg->asm_->rdata, cg->asm_->rdata_cap);
    }
    memcpy(cg->asm_->rdata + cg->asm_->rdata_len, val, vlen + 1);
    cg->asm_->rdata_len   += vlen + 1;
    cg->string_pool_size  += vlen + 1;
    return cg->strings[idx].label;
}

static const char *a64_intern_float(CodeGenA64 *cg, double val) {
    uint8_t raw[8];
    int i;
    memcpy(raw, &val, 8);
    for (i = 0; i < cg->float_const_count; i++) {
        if (memcmp(cg->float_consts[i].value, raw, 8) == 0)
            return cg->float_consts[i].label;
    }
    if (cg->float_const_count == cg->float_const_cap) {
        cg->float_const_cap = cg->float_const_cap ? cg->float_const_cap * 2 : 16;
        cg->float_consts = (A64StringEntry*)realloc(cg->float_consts,
                                cg->float_const_cap * sizeof(A64StringEntry));
    }
    int idx = cg->float_const_count++;
    char lbl[32];
    snprintf(lbl, sizeof lbl, "__a64flt%d", idx);
    /* Align to 8 bytes */
    while (cg->asm_->rdata_len & 7) {
        if (cg->asm_->rdata_len >= cg->asm_->rdata_cap) {
            cg->asm_->rdata_cap *= 2;
            cg->asm_->rdata = (uint8_t*)realloc(cg->asm_->rdata, cg->asm_->rdata_cap);
        }
        cg->asm_->rdata[cg->asm_->rdata_len++] = 0;
        cg->string_pool_size++;
    }
    int pool_off = cg->string_pool_size;
    /* Grow for 8 bytes */
    if (cg->asm_->rdata_len + 8 > cg->asm_->rdata_cap) {
        cg->asm_->rdata_cap *= 2;
        cg->asm_->rdata = (uint8_t*)realloc(cg->asm_->rdata, cg->asm_->rdata_cap);
    }
    memcpy(cg->asm_->rdata + cg->asm_->rdata_len, raw, 8);
    cg->asm_->rdata_len   += 8;
    cg->string_pool_size  += 8;
    cg->float_consts[idx].value  = (char*)malloc(8);
    memcpy(cg->float_consts[idx].value, raw, 8);
    cg->float_consts[idx].len    = 8;
    cg->float_consts[idx].label  = my_strdup(lbl);
    cg->float_consts[idx].offset = pool_off;
    return cg->float_consts[idx].label;
}

/* =========================================================================
 * Writable data pool (.data)
 * ========================================================================= */
static const char *a64_alloc_wdata(CodeGenA64 *cg, int size, const char *hint) {
    if (cg->wdata_count == cg->wdata_cap) {
        cg->wdata_cap = cg->wdata_cap ? cg->wdata_cap * 2 : 16;
        cg->wdata = (A64WDataEntry*)realloc(cg->wdata,
                        cg->wdata_cap * sizeof(A64WDataEntry));
    }
    int idx = cg->wdata_count++;
    char lbl[64];
    if (hint && hint[0]) {
        snprintf(lbl, sizeof lbl, "%s", hint);
    } else {
        snprintf(lbl, sizeof lbl, "__a64wd%d", idx);
    }
    /* align to 8 bytes */
    int off = (cg->wdata_pool_size + 7) & ~7;
    cg->wdata[idx].label  = my_strdup(lbl);
    cg->wdata[idx].offset = off;
    cg->wdata[idx].size   = size;
    cg->wdata_pool_size   = off + size;
    /* ensure wdata buffer is large enough */
    while (cg->asm_->wdata_len < cg->wdata_pool_size) {
        if (cg->asm_->wdata_len >= cg->asm_->wdata_cap) {
            cg->asm_->wdata_cap *= 2;
            cg->asm_->wdata = (uint8_t*)realloc(cg->asm_->wdata, cg->asm_->wdata_cap);
        }
        cg->asm_->wdata[cg->asm_->wdata_len++] = 0;
    }
    return cg->wdata[idx].label;
}

/* =========================================================================
 * Load immediate value into register xd
 * ========================================================================= */
static void a64_load_imm(CodeGenA64 *cg, int xd, long long val) {
    Arm64Asm *a = cg->asm_;
    unsigned long long uval = (unsigned long long)val;
    uint16_t w0 = (uint16_t)(uval);
    uint16_t w1 = (uint16_t)(uval >> 16);
    uint16_t w2 = (uint16_t)(uval >> 32);
    uint16_t w3 = (uint16_t)(uval >> 48);
    int first = 1;

    if (val == 0) { a64_emit(a, a64_MOV(xd, A64_XZR)); return; }

    /* MOVZ for the first non-zero chunk, MOVK for the rest */
    if (w0) { a64_emit(a, a64_MOVZ(xd, w0,  0)); first = 0; }
    if (w1) {
        if (first) { a64_emit(a, a64_MOVZ(xd, w1, 16)); first = 0; }
        else         a64_emit(a, a64_MOVK(xd, w1, 16));
    }
    if (w2) {
        if (first) { a64_emit(a, a64_MOVZ(xd, w2, 32)); first = 0; }
        else         a64_emit(a, a64_MOVK(xd, w2, 32));
    }
    if (w3) {
        if (first) { a64_emit(a, a64_MOVZ(xd, w3, 48)); first = 0; }
        else         a64_emit(a, a64_MOVK(xd, w3, 48));
    }
    if (first) a64_emit(a, a64_MOV(xd, A64_XZR));  /* val == 0, already handled */
}

/* Adjust SP (or any register) by an arbitrary amount, splitting into a
 * LSL#12-shifted high part and a plain low part when the amount exceeds a
 * single ADD/SUB immediate's 12-bit field (4095). Needed for large stack
 * frames (e.g. a function with a big local array) whose extra SUB beyond
 * A64_FRAME_SPLIT can itself exceed 4095. */
static void a64_sp_adjust(Arm64Asm *a, int rd, int rn, int amount, int is_sub) {
    int hi = (amount >> 12) & 0xFFF;
    int lo = amount & 0xFFF;
    int cur = rn;
    if (hi) {
        a64_emit(a, is_sub ? a64_SUB_imm_sh12(rd, cur, hi) : a64_ADD_imm_sh12(rd, cur, hi));
        cur = rd;
    }
    if (lo || !hi) {
        a64_emit(a, is_sub ? a64_SUB_imm(rd, cur, lo) : a64_ADD_imm(rd, cur, lo));
    }
}

/* =========================================================================
 * Temp expression stack: push x0 / pop into x1
 * Temp slots live at [x29, #(cur_local_off + 16 + j*8)]
 * ========================================================================= */
static void push_temp(CodeGenA64 *cg) {
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_STR(A64_X0, A64_FP, off));
    cg->cur_temp_depth++;
    if (cg->cur_temp_depth > cg->max_temp_depth)
        cg->max_temp_depth = cg->cur_temp_depth;
}

static void pop_temp_into_x1(CodeGenA64 *cg) {
    cg->cur_temp_depth--;
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_LDR(A64_X1, A64_FP, off));
}

static void push_float_temp(CodeGenA64 *cg) {
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_FSTR(A64_D0, A64_FP, off));
    cg->cur_temp_depth++;
    if (cg->cur_temp_depth > cg->max_temp_depth)
        cg->max_temp_depth = cg->cur_temp_depth;
}

static void pop_float_temp_into_d1(CodeGenA64 *cg) {
    cg->cur_temp_depth--;
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_FLDR(A64_D1, A64_FP, off));
}

/* =========================================================================
 * Emit ADRP + ADD pair for a data label (string/wdata)
 * Emits two instructions; reloc pair records the patch offsets.
 * ========================================================================= */
static void emit_data_ref(CodeGenA64 *cg, int xd, const char *label,
                           RelocKind adrp_kind, RelocKind lo12_kind) {
    Arm64Asm *a = cg->asm_;
    int adrp_off = a->code_len;
    a64_emit(a, a64_ADRP(xd, 0));          /* placeholder: ADRP xd, 0 */
    int lo12_off = a->code_len;
    a64_emit(a, a64_ADD_imm(xd, xd, 0));   /* placeholder: ADD xd, xd, #0 */
    a64_add_reloc(a, adrp_off, adrp_kind, label, 0);
    a64_add_reloc(a, lo12_off, lo12_kind,  label, 0);
}

/* Emit ADRP + LDR pair for an IAT slot (import function pointer) */
static void emit_iat_ref(CodeGenA64 *cg, int xd, const char *func_name) {
    Arm64Asm *a = cg->asm_;
    int adrp_off = a->code_len;
    a64_emit(a, a64_ADRP(xd, 0));              /* ADRP xd, IAT_page */
    int ldr_off = a->code_len;
    a64_emit(a, a64_LDR(xd, xd, 0));           /* LDR xd, [xd, #lo12] */
    a64_add_reloc(a, adrp_off, RELOC_A64_IAT_ADRP, func_name, 0);
    a64_add_reloc(a, ldr_off,  RELOC_A64_IAT_LO12, func_name, 0);
}

/* =========================================================================
 * Load local/global variable address into x8, then load value into x0
 * sym->offset is negative; ARM64 FP off = -sym->offset
 * ========================================================================= */
static void a64_load_sym_addr(CodeGenA64 *cg, Symbol *sym);

/* Element size of an indexing expression's array/pointer base (e.g. the "4"
 * in arr[i] for int arr[8]).  The parser never populates the generic
 * ASTNode.type field (only declaration-specific .type fields are set), so
 * this must resolve through the symbol table, mirroring codegen.c's
 * elem_size_of() for the x86 backend. Used for both address computation
 * (index * esz) and load/store instruction width — getting either wrong
 * either miscomputes the address or overruns adjacent stack slots. */
static int a64_index_elem_size(CodeGenA64 *cg, ASTNode *arr_expr) {
    /* Nested index — arr[i][j]: arr_expr here is the OUTER index node
     * (arr[i]) itself, meaning we're sizing the INNER/second-dimension
     * access. If the outer index's own base is a 2D array (T x[N][M]),
     * the outer index already consumed the row dimension, so this inner
     * access needs the plain scalar element size, not the row stride. */
    if (arr_expr && arr_expr->kind == AST_INDEX) {
        ASTNode *base = arr_expr->index.array;
        if (base && base->kind == AST_VAR) {
            Symbol *bsym = symtable_lookup(cg->sym, base->var.name);
            if (bsym && bsym->array_size2 > 0 && bsym->type) {
                int sz = a64_sizeof_type_sym(cg->sym, bsym->type, 1);
                return sz < 1 ? 1 : sz;
            }
        } else if (base && base->kind == AST_MEMBER) {
            int off;
            ASTNode *f = a64_resolve_field(cg, base->member.obj, base->member.field, &off);
            if (f && f->field.array_size2 > 0 && f->field.type) {
                int sz = a64_sizeof_type_sym(cg->sym, f->field.type, 1);
                return sz < 1 ? 1 : sz;
            }
        }
    }
    if (arr_expr && arr_expr->kind == AST_VAR) {
        Symbol *asym = symtable_lookup(cg->sym, arr_expr->var.name);
        if (asym && asym->type) {
            if (asym->type->pointer_depth > 0 && asym->array_size > 0)
                return 8; /* array of pointers: each element is a pointer */
            if (asym->array_size2 > 0) {
                /* T x[N][M]: x[i] yields row i — needs the row stride
                 * (sizeof(T) * M), NOT the whole array's total size. Must
                 * call the _ex form with ignore_array=1: asym->type here
                 * can itself carry the same array_size/array_size2 as the
                 * symbol (mirrored when the symbol was defined), so the
                 * plain a64_sizeof_type_sym() wrapper (ignore_array=0)
                 * returned the FULL array's total byte size (sizeof(T) *
                 * N * M) as "the element size" — then this multiplied
                 * that by M *again*, producing a wildly inflated stride
                 * (reproduced directly: "static char g_bufs[8][32]; ...
                 * g_bufs[idx]" strode by 8192 bytes instead of 32 per
                 * row, corrupting whatever else lived in the following
                 * ~24KB of global/static memory after only a few calls). */
                int base_elem = a64_sizeof_type_sym_ex(cg->sym, asym->type, 1, 0, 1);
                if (base_elem < 1) base_elem = 1;
                return base_elem * asym->array_size2;
            }
            return a64_sizeof_type_sym_ex(cg->sym, asym->type, 1, 1, 1);
        }
    } else if (arr_expr && arr_expr->kind == AST_MEMBER) {
        return a64_field_elem_size(cg, arr_expr);
    } else if (arr_expr && arr_expr->type && arr_expr->type->pointed_to) {
        return a64_sizeof_type_sym(cg->sym, arr_expr->type->pointed_to, 1);
    }
    return 8;
}

/* Resolves is_unsigned through typedef chains (up to 8 deep, matching real
 * typedef nesting like VkFlags -> uint32_t -> unsigned int): a typedef's
 * own TypeInfo doesn't inherit is_unsigned from what it aliases — only
 * the FINAL underlying type (e.g. the "unsigned int" in "typedef unsigned
 * int uint32_t;") actually has it set — so checking a field/element's
 * declared type directly (e.g. "uint32_t", "VkFlags") missed it entirely.
 * This mattered beyond just being imprecise: every Vulkan struct field
 * declared as VkFlags/uint32_t holding one of Vulkan's own ~0u sentinels
 * (VK_QUEUE_FAMILY_IGNORED, VK_SUBPASS_EXTERNAL) read back sign-extended
 * into a huge garbage 64-bit value via the still-unresolved is_unsigned
 * check below. */
static int a64_type_is_unsigned(SymTable *sym, TypeInfo *t) {
    int depth = 0;
    while (t && depth++ < 8) {
        if (t->pointer_depth > 0) return 0;
        if (t->is_unsigned) return 1;
        if (!t->base) return 0;
        Symbol *td = symtable_lookup(sym, t->base);
        if (td && td->kind == SYM_TYPEDEF && td->type) { t = td->type; continue; }
        return 0;
    }
    return 0;
}

/* Is a[i]'s ELEMENT type unsigned? Companion to a64_index_elem_size, used
 * so a 4-byte array-element/pointer-dereference LOAD picks a zero-extending
 * instruction (LDR into a W register) for unsigned element types instead
 * of always sign-extending (LDRSW) — the AST_MEMBER read path already made
 * this same distinction (see its own fld_unsigned check), but AST_INDEX
 * never did, so any unsigned 32-bit array element/pointee whose top bit is
 * set (e.g. "unsigned int arr[N]; arr[i] = 0xFFFFFFFFu;", or any of
 * Vulkan's own ~0u sentinels like VK_QUEUE_FAMILY_IGNORED/
 * VK_SUBPASS_EXTERNAL stored through a pointer/array) read back
 * sign-extended into a huge garbage 64-bit value the instant it was used
 * again (reproduced directly: "unsigned int raw[8]; raw[2]=0xFFFFFFFFu;"
 * printed garbage via %x on the very next read). Only handles the plain
 * AST_VAR base case (covers what matters here); anything else defaults to
 * signed (0), preserving the prior LDRSW behavior exactly. */
static int a64_index_elem_is_unsigned(CodeGenA64 *cg, ASTNode *arr_expr) {
    if (arr_expr && arr_expr->kind == AST_VAR) {
        Symbol *asym = symtable_lookup(cg->sym, arr_expr->var.name);
        if (asym && asym->type && asym->array_size2 <= 0)
            return a64_type_is_unsigned(cg->sym, asym->type);
    }
    return 0;
}

/* Frame offset (from x29, extending upward) for a local variable or param.
 *
 * symtable_define_var's allocation (shared with the x86 backend) is only
 * non-overlapping when addressed as [FP + negative_offset] extending
 * DOWNWARD, which is how x86 uses it directly. ARM64 instead negates the
 * offset and adds it as a positive displacement extending UPWARD from x29 —
 * under that scheme, two consecutively-declared variables are only
 * guaranteed non-overlapping when they have the SAME aligned slot size
 * (which happens to hold for sequences of same-sized scalars, masking the
 * bug, but breaks e.g. for an array followed by a smaller scalar: the next
 * variable's slot only leaves room for its OWN size, not the array's).
 * Correcting by "+16 - this variable's own aligned slot size" exactly
 * reproduces x86's non-overlapping layout, just measured from x29 instead
 * of from the stack pointer. SYM_PARAM uses its own fixed (i+1)*16 scheme
 * (assigned directly in a64_codegen_func) and needs no correction. */
static int a64_sym_fp_off(Symbol *sym) {
    int fp_off = -sym->offset;
    if (sym->kind == SYM_VAR) fp_off = 16 + fp_off - sym->slot_size;
    return fp_off;
}

static void a64_load_sym_value(CodeGenA64 *cg, Symbol *sym) {
    Arm64Asm *a = cg->asm_;
    if (!sym) { a64_emit(a, a64_MOV(A64_X0, A64_XZR)); return; }

    if (sym->array_size > 0 && (sym->kind == SYM_VAR || sym->kind == SYM_PARAM)) {
        /* C array-to-pointer decay: a bare array name used as a value is the
         * address of its first element, not a load from that address. */
        a64_load_sym_addr(cg, sym);
        return;
    }

    if (sym->kind == SYM_FUNC) {
        /* A bare function name used as a value (e.g. assigning to a function
         * pointer: op = fp_add;) decays to the function's address. Uses ADR
         * (byte-level PC-relative, +-1MB range) resolved by the same
         * label/fixup mechanism as branch targets in a64_resolve(). */
        int lbl_id = a64_find_label(a, sym->name);
        if (lbl_id < 0) lbl_id = a64_new_label(a, sym->name);
        int patch = a->code_len;
        a64_emit(a, 0x10000000u | A64_X0); /* ADR x0, #0 (placeholder) */
        a64_add_fixup(a, patch, lbl_id);
        return;
    }

    if (sym->kind == SYM_VAR || sym->kind == SYM_PARAM) {
        int fp_off = a64_sym_fp_off(sym);
        TypeInfo *t = sym->type;
        /* a64_sizeof_type_sym, NOT typeinfo_size_a64 -- see the identical
         * fix at the external-call return-value fixup above for the full
         * story: an unrecognized typedef/enum (e.g. "VkResult") silently
         * defaulted to 8 bytes here too, so a variable's LOAD width could
         * disagree with its real size the same way its call-site fixup
         * did. */
        int sz = a64_sizeof_type_sym(cg->sym, t, 1);
        /* Each sized load instruction has its own encodable immediate range
         * (unsigned offset, scaled by the access size): LDR up to 32760,
         * LDR32 up to 16380, LDRH up to 8190, LDRB up to 4095 (unscaled).
         * Gating all of them on LDR's wider range let e.g. fp_off=4144 pass
         * an ">=0 && <=32760 && %8==0" check meant only for 8-byte access
         * and then get encoded as LDRB, whose 12-bit unscaled field
         * silently truncates 4144 to 4144&0xFFF=48 — reading whichever
         * unrelated local/array element happened to live at offset 48. */
        int in_range = (fp_off >= 0) &&
            ((sz >= 8 && fp_off <= 32760 && (fp_off & 7) == 0) ||
             (sz == 4 && fp_off <= 16380 && (fp_off & 3) == 0) ||
             (sz == 2 && fp_off <= 8190  && (fp_off & 1) == 0) ||
             (sz <= 1 && fp_off <= 4095));
        if (in_range) {
            if (sz >= 8)
                a64_emit(a, a64_LDR(A64_X0, A64_FP, fp_off));
            else if (sz == 4)
                a64_emit(a, a64_LDR32(A64_X0, A64_FP, fp_off));
            else if (sz == 2)
                a64_emit(a, a64_LDRH(A64_X0, A64_FP, fp_off));
            else
                a64_emit(a, a64_LDRB(A64_X0, A64_FP, fp_off));
        } else if (fp_off >= -256 && fp_off <= 255 && sz >= 8) {
            a64_emit(a, a64_LDUR(A64_X0, A64_FP, fp_off));
        } else {
            /* Large offset: compute address in x8 */
            a64_load_imm(cg, A64_X8, fp_off);
            a64_emit(a, a64_ADD(A64_X8, A64_FP, A64_X8));
            if (sz >= 8)      a64_emit(a, a64_LDR(A64_X0, A64_X8, 0));
            else if (sz == 4) a64_emit(a, a64_LDR32(A64_X0, A64_X8, 0));
            else if (sz == 2) a64_emit(a, a64_LDRH(A64_X0, A64_X8, 0));
            else              a64_emit(a, a64_LDRB(A64_X0, A64_X8, 0));
        }
        /* Sign-extend char/short */
        if (t && t->pointer_depth == 0) {
            if (sz == 1 && !t->is_unsigned) {
                /* SXTB X0, W0 */
                a64_emit(a, 0x93401C00u | A64_X0);
            } else if (sz == 2 && !t->is_unsigned) {
                /* SXTH X0, W0 */
                a64_emit(a, 0x93403C00u | A64_X0);
            } else if (sz == 4 && !t->is_unsigned &&
                       strcmp(t->base, "int") == 0) {
                a64_emit(a, a64_SXTW(A64_X0, A64_X0));
            }
        }
    } else if (sym->kind == SYM_GLOBAL) {
        /* ADRP x0, data_page; LDR x0, [x0, #lo12].
         * sym->dll holds the mangled wdata label for static locals (set by
         * AST_VAR_DECL); plain globals use the variable's own name. */
        const char *lbl = (sym->dll && sym->dll[0]) ? sym->dll : sym->name;
        emit_data_ref(cg, A64_X0, lbl, RELOC_A64_WDATA_ADRP, RELOC_A64_WDATA_LO12);
        if (sym->array_size > 0) {
            /* Array-to-pointer decay: address already in x0, no dereference. */
        } else if (!sym->type || sym->type->pointer_depth == 0) {
            /* Scalar: dereference, sized to the type — wdata entries are
             * packed tightly (8-byte aligned, not padded), so a wider load
             * than the type's size would read into the next global. */
            int sz = sym->type ? a64_sizeof_type_sym(cg->sym, sym->type, 1) : 4;
            int sym_unsigned = sym->type && sym->type->is_unsigned;
            if (sz >= 8)       a64_emit(a, a64_LDR(A64_X0, A64_X0, 0));
            else if (sz == 4)  a64_emit(a, sym_unsigned ? a64_LDR32(A64_X0, A64_X0, 0) : a64_LDRSW(A64_X0, A64_X0, 0));
            else if (sz == 2)  a64_emit(a, a64_LDRH(A64_X0, A64_X0, 0));
            else               a64_emit(a, a64_LDRB(A64_X0, A64_X0, 0));
        }
    } else if (sym->kind == SYM_ENUM_VAL) {
        a64_load_imm(cg, A64_X0, sym->enum_value);
    }
}

static void a64_load_sym_addr(CodeGenA64 *cg, Symbol *sym) {
    Arm64Asm *a = cg->asm_;
    if (!sym) return;
    if (sym->kind == SYM_VAR || sym->kind == SYM_PARAM) {
        int fp_off = a64_sym_fp_off(sym);
        if (fp_off >= 0 && fp_off <= 4095) {
            a64_emit(a, a64_ADD_imm(A64_X0, A64_FP, fp_off));
        } else {
            a64_load_imm(cg, A64_X8, fp_off);
            a64_emit(a, a64_ADD(A64_X0, A64_FP, A64_X8));
        }
    } else if (sym->kind == SYM_GLOBAL) {
        const char *lbl = (sym->dll && sym->dll[0]) ? sym->dll : sym->name;
        emit_data_ref(cg, A64_X0, lbl, RELOC_A64_WDATA_ADRP, RELOC_A64_WDATA_LO12);
    }
}

static void a64_store_to_sym(CodeGenA64 *cg, Symbol *sym, int val_reg) {
    Arm64Asm *a = cg->asm_;
    if (!sym) return;
    if (sym->kind == SYM_VAR || sym->kind == SYM_PARAM) {
        int fp_off = a64_sym_fp_off(sym);
        TypeInfo *t = sym->type;
        /* a64_sizeof_type_sym, NOT typeinfo_size_a64 -- see a64_load_sym_value's
         * identical fix; store width must agree with load width or a
         * round-tripped value picks up stale bits from whatever used to
         * occupy the rest of an over-wide slot. */
        int sz = a64_sizeof_type_sym(cg->sym, t, 1);
        /* See a64_load_sym_value for why the range check must be per-size:
         * STR up to 32760, STR32 up to 16380, STRH up to 8190, STRB up to
         * 4095 (unscaled) — gating all of them on STR's wider range let
         * large-but-still-"aligned" offsets silently truncate in STRB's
         * 12-bit unscaled field. */
        int in_range = (fp_off >= 0) &&
            ((sz >= 8 && fp_off <= 32760 && (fp_off & 7) == 0) ||
             (sz == 4 && fp_off <= 16380 && (fp_off & 3) == 0) ||
             (sz == 2 && fp_off <= 8190  && (fp_off & 1) == 0) ||
             (sz <= 1 && fp_off <= 4095));
        if (in_range) {
            if (sz >= 8)
                a64_emit(a, a64_STR(val_reg, A64_FP, fp_off));
            else if (sz == 4)
                a64_emit(a, a64_STR32(val_reg, A64_FP, fp_off));
            else if (sz == 2)
                a64_emit(a, a64_STRH(val_reg, A64_FP, fp_off));
            else
                a64_emit(a, a64_STRB(val_reg, A64_FP, fp_off));
        } else if (fp_off >= -256 && fp_off <= 255 && sz >= 8) {
            a64_emit(a, a64_STUR(val_reg, A64_FP, fp_off));
        } else {
            a64_load_imm(cg, A64_X8, fp_off);
            a64_emit(a, a64_ADD(A64_X8, A64_FP, A64_X8));
            if (sz >= 8)      a64_emit(a, a64_STR(val_reg, A64_X8, 0));
            else if (sz == 4) a64_emit(a, a64_STR32(val_reg, A64_X8, 0));
            else if (sz == 2) a64_emit(a, a64_STRH(val_reg, A64_X8, 0));
            else              a64_emit(a, a64_STRB(val_reg, A64_X8, 0));
        }
    } else if (sym->kind == SYM_GLOBAL) {
        const char *lbl = (sym->dll && sym->dll[0]) ? sym->dll : sym->name;
        emit_data_ref(cg, A64_X8, lbl, RELOC_A64_WDATA_ADRP, RELOC_A64_WDATA_LO12);
        int sz = sym->type ? a64_sizeof_type_sym(cg->sym, sym->type, 1) : 4;
        if (sz >= 8)       a64_emit(a, a64_STR(val_reg, A64_X8, 0));
        else if (sz == 4)  a64_emit(a, a64_STR32(val_reg, A64_X8, 0));
        else if (sz == 2)  a64_emit(a, a64_STRH(val_reg, A64_X8, 0));
        else               a64_emit(a, a64_STRB(val_reg, A64_X8, 0));
    }
}

/* =========================================================================
 * Forward declarations of expression/statement generators
 * ========================================================================= */
static void a64_expr(CodeGenA64 *cg, ASTNode *n);
static void a64_lvalue(CodeGenA64 *cg, ASTNode *n);
static int  a64_lvalue_size(CodeGenA64 *cg, ASTNode *n);
static void a64_stmt(CodeGenA64 *cg, ASTNode *n);
static int  a64_is_float(CodeGenA64 *cg, ASTNode *n);
static void a64_float_expr(CodeGenA64 *cg, ASTNode *n);
static void a64_emit_call(CodeGenA64 *cg, ASTNode *n);

/* =========================================================================
 * Determine if expression yields float/double
 * ========================================================================= */
static int a64_is_float(CodeGenA64 *cg, ASTNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_FLOAT: return 1;
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        /* A bare "float arr[N]" name used as a value (e.g. passed as a
         * function argument) decays to a POINTER, not a float — s->type
         * here is the array's ELEMENT type ("float"), so without this
         * check a float/double array argument was misclassified as
         * float-class and a64_emit_call put its address in a D-register
         * instead of an X-register. That doesn't just corrupt that one
         * argument: it also shifts the int-class register index for
         * every argument evaluated after it in the same call, since the
         * int/float register banks are tracked independently (reproduced
         * directly: "f(dev, phys, mod, floatArr1, floatArr2, structPtr)"
         * — passing two local float arrays — silently corrupted the
         * struct pointer argument two slots later into garbage, which
         * then crashed inside a memset() using it as the destination). */
        if (s && s->array_size > 0) return 0;
        return s && a64_is_float_type(cg->sym, s->type) ? 1 : 0;
    }
    case AST_BINARY:
        /* Comparison/logical operators always produce an int (0/1) result,
         * regardless of operand type — only arithmetic operators propagate
         * float-ness from their operands. Getting this wrong matters far
         * beyond expression codegen: a64_emit_call uses a64_is_float() to
         * decide which register bank (x0-x7 vs d0-d7) an argument goes in,
         * so e.g. check(pi > e, "...") would try to pass the comparison's
         * int result in a d-register if this returned true. */
        if (strcmp(n->binary.op,"<")==0 || strcmp(n->binary.op,">")==0 ||
            strcmp(n->binary.op,"<=")==0 || strcmp(n->binary.op,">=")==0 ||
            strcmp(n->binary.op,"==")==0 || strcmp(n->binary.op,"!=")==0 ||
            strcmp(n->binary.op,"&&")==0 || strcmp(n->binary.op,"||")==0)
            return 0;
        return a64_is_float(cg, n->binary.left) || a64_is_float(cg, n->binary.right);
    case AST_UNARY:
        return a64_is_float(cg, n->unary.operand);
    case AST_ASSIGN:
        return a64_is_float(cg, n->assign.lhs);
    case AST_CAST:
        return a64_is_float_type(cg->sym, n->cast.type);
    case AST_CALL: {
        Symbol *s = n->call.name ? symtable_lookup(cg->sym, n->call.name) : NULL;
        return s && s->func_node && a64_is_float_type(cg->sym, s->func_node->func.ret_type);
    }
    case AST_MEMBER: {
        int off;
        ASTNode *f = a64_resolve_field(cg, n->member.obj, n->member.field, &off);
        return f && a64_is_float_type(cg->sym, f->field.type);
    }
    case AST_INDEX: {
        if (n->index.array && n->index.array->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, n->index.array->var.name);
            return s && a64_is_float_type(cg->sym, s->type);
        }
        return 0;
    }
    default: return 0;
    }
}

/* =========================================================================
 * Shared call-emission: evaluates args into x0-x7/d0-d7 per AAPCS64 and
 * dispatches the call. Leaves the result in x0 (int) or d0 (float, if the
 * callee's declared return type is float/double) — used by both a64_expr
 * and a64_float_expr so a call returning double is only ever read from d0,
 * never garbage-converted from whatever happened to be left in x0.
 * ========================================================================= */

/* Resolve and emit a Linux call to `name`, a function with no body in this
 * translation unit. Tries, in order:
 *  1. A real dynamic symbol in one of the user's -l/-L shared libraries
 *     (cg->linker) — GOT-indirect call via the existing IAT/GOT mechanism.
 *  2. A static definition pulled from a -l/-L ".a" archive member
 *     (cg->linker) — a deferred RELOC_A64_STATIC_BL call (see
 *     a64_call_static and elf_builder.c's matching patch case).
 *  3. If this build is ALSO linking in one or more ".sqo" cross-object
 *     files (cg->prefer_static_calls, set from compiler.c's n_obj>0), ALSO
 *     try a deferred RELOC_A64_STATIC_BL call on the assumption the real
 *     definition lives in one of those objects — resolved later by
 *     objfile_merge() (which patches it directly and drops the
 *     relocation), or left as a soft "unresolved static symbol" warning at
 *     link time if genuinely undefined anywhere, exactly like every other
 *     unresolved cross-module reference already behaves.
 *  4. Otherwise, the historical default this function preserves exactly:
 *     assume libc.so.6 exports it and emit a GOT-indirect call. This is
 *     what makes squash's own stdio.h/stdlib.h-shimmed CRT calls (malloc,
 *     printf, memcpy, ...) work today — ARM64 has no is_internal_shim()-
 *     style inline CRT reimplementation the way codegen.c's x86-64 path
 *     does, so EVERY such call currently reaches this exact fallback, and
 *     changing its default behavior (e.g. preferring a static call
 *     whenever unresolved) would silently break them. Only steps 1-3 above
 *     are new; step 4 is intentionally untouched. */
static void a64_emit_linux_extern_call(CodeGenA64 *cg, const char *name) {
    Arm64Asm *a = cg->asm_;
    if (cg->linker) {
        const char *soname = linker_lookup_dynamic(cg->linker, name);
        if (soname) {
            char lkey[512]; snprintf(lkey,sizeof lkey,"%s:%s",soname,name);
            symtable_add_import(cg->sym, lkey);
            emit_iat_ref(cg, A64_X16, name);
            a64_emit(a, a64_BLR(A64_X16));
            return;
        }
        if (linker_link_static(cg->linker, name)) {
            a64_call_static(a, name);
            return;
        }
    }
    /* codegen_is_sqo_export() checks the actual export tables of the
     * ".sqo" files being linked (compiler.c populates cg->sqo_export_names
     * before codegen runs) — is_internal_shim() alone is an incomplete,
     * hand-maintained denylist (it never listed "write", for instance) and
     * prefer_static_calls alone just means "some .sqo is present", neither
     * of which tells you whether THIS name is really one of their exports.
     * Guessing wrong here breaks a genuine external libc call the moment
     * any .sqo is linked in at all — confirmed via squash self-hosting
     * itself for -macos (write(), reached through stdio.h's fprintf shim,
     * misrouted as an unresolvable cross-object call). See codegen.h's
     * matching x86-64 comment for the full story. */
    if (cg->prefer_static_calls && !is_internal_shim(name) && a64_is_sqo_export(cg,name)) {
        a64_call_static(a, name);
        return;
    }
    char lkey2[512]; snprintf(lkey2,sizeof lkey2,"%s:%s",g_squash_libc_soname,name);
    symtable_add_import(cg->sym, lkey2);
    emit_iat_ref(cg, A64_X16, name);
    a64_emit(a, a64_BLR(A64_X16));
}

static void a64_emit_call(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;

    /* If the identifier is a local variable/param holding a function pointer
     * (not a declared function), treat it as a function-pointer call:
     * int (*op)(int,int); op = fp_add; op(1,2);
     * Mirrors codegen.c's x86 handling of the same ambiguous call syntax. */
    {
        Symbol *csym = n->call.name ? symtable_lookup(cg->sym, n->call.name) : NULL;
        int is_fptr_var = (csym &&
                          (csym->kind==SYM_VAR || csym->kind==SYM_PARAM) &&
                           csym->type && csym->type->pointer_depth > 0);
        if (is_fptr_var) {
            ASTNode fake_var; memset(&fake_var,0,sizeof fake_var);
            fake_var.kind=AST_VAR; fake_var.line=n->line;
            fake_var.var.name=(char*)n->call.name;
            ASTNode fake_fpcall; memset(&fake_fpcall,0,sizeof fake_fpcall);
            fake_fpcall.kind=AST_FUNC_PTR_CALL; fake_fpcall.line=n->line;
            fake_fpcall.fp_call.func_expr=&fake_var;
            fake_fpcall.fp_call.args=n->call.args;
            fake_fpcall.fp_call.argc=n->call.argc;
            a64_expr(cg, &fake_fpcall);
            return;
        }
    }
    /* Evaluate arguments and place in x0-x7 / d0-d7, with any overflow
     * (9th+ argument in either bank) passed on the stack per AAPCS64.
     * AAPCS64 uses independent register banks for integer and float
     * args (e.g. f(int,double,int) passes them x0,d0,x1 — NOT x0,x1,x2)
     * so each argument's own float-ness determines its bank and index,
     * tracked with separate counters, mirroring the prologue's handling
     * of declared parameter types.
     *
     * Real-world calls needing this are common with real Vulkan/Win32
     * APIs (e.g. vkCmdPipelineBarrier takes 10 arguments) — the previous
     * flat "cap at 8, silently drop the rest" here left the 9th/10th
     * argument NEVER EVALUATED OR PASSED AT ALL: the callee (real
     * external code, compiled against the real ABI) still read ITS
     * stack-argument slots as always, but squash never wrote anything
     * there, so it read whatever garbage happened to be on the stack —
     * reproduced directly: vkCmdPipelineBarrier's 9th/10th args
     * (imageMemoryBarrierCount/pImageMemoryBarriers, both meant to be 0/
     * NULL) came through as garbage, and the driver's attempt to walk
     * "pImageMemoryBarriers[i]" for a garbage count crashed with a wild
     * jump into unmapped memory. Overflow args, regardless of int/float
     * type, are pushed in ORIGINAL ARGUMENT ORDER at consecutive 8-byte
     * stack slots (AAPCS64's NSAA is a single counter shared across
     * types) — capped defensively at 16 total arguments, matching the
     * temp-slot budget (A64_MAX_TEMP_DEPTH=24) with headroom for nested
     * expression temps within the argument expressions themselves. */
    int i;
    int nargs = n->call.argc < 16 ? n->call.argc : 16;
    int is_float_arg[16];
    int reg_idx[16];
    int on_stack[16];
    int stack_slot[16];
    int stack_bytes;
    {
        int ireg = 0, freg = 0, stackn = 0;
        for (i = 0; i < nargs; i++) {
            is_float_arg[i] = a64_is_float(cg, n->call.args[i]);
            reg_idx[i] = is_float_arg[i] ? freg++ : ireg++;
            if (reg_idx[i] >= 8) { on_stack[i] = 1; stack_slot[i] = stackn++; }
            else                 { on_stack[i] = 0; stack_slot[i] = 0; }
        }
        stack_bytes = (stackn * 8 + 15) & ~15;
    }
    /* Save args in temp slots (they might overwrite each other) */
    for (i = 0; i < nargs; i++) {
        if (is_float_arg[i]) { a64_float_expr(cg, n->call.args[i]); push_float_temp(cg); }
        else                 { a64_expr(cg, n->call.args[i]);       push_temp(cg); }
    }
    /* Allocate the overflow-argument stack area (if any) before popping,
     * so the SP-relative stores below land at their final address — the
     * callee expects to find its stack arguments at [sp+0], [sp+8], ...
     * at the moment of the call. */
    if (stack_bytes > 0) a64_sp_adjust(a, A64_SP, A64_SP, stack_bytes, 1);
    /* Pop args in REVERSE order to load into registers (or the stack).
     * Popping via a plain integer LDR/STR regardless of is_float_arg is
     * safe for the stack case: push_temp/push_float_temp both write the
     * same 8-byte-strided temp slot format (see their own bodies), so an
     * integer load just moves the raw bit pattern through — exactly what
     * a stack-argument copy needs, no float reinterpretation required. */
    for (i = nargs - 1; i >= 0; i--) {
        cg->cur_temp_depth--;
        int off = cg->temp_base + cg->cur_temp_depth * 8;
        if (on_stack[i]) {
            a64_emit(a, a64_LDR(A64_X8, A64_FP, off));
            a64_emit(a, a64_STR(A64_X8, A64_SP, stack_slot[i] * 8));
        } else if (is_float_arg[i]) {
            if (reg_idx[i] < 8) a64_emit(a, a64_FLDR(reg_idx[i], A64_FP, off));
        } else {
            if (reg_idx[i] < 8) a64_emit(a, a64_LDR(reg_idx[i], A64_FP, off));
        }
    }

    /* Emit call */
    Symbol *fsym = n->call.name ? symtable_lookup(cg->sym, n->call.name) : NULL;
    if (cg->is_linux && n->call.name && strcmp(n->call.name, "ExitProcess") == 0) {
        /* ExitProcess(code) -> libc exit(code) on Linux.  ExitProcess is
         * unconditionally pre-registered as a KERNEL32.dll SYM_IMPORT
         * (symtable.c), which the ELF builder skips (.dll libs are
         * Windows-only), so it must be redirected here instead of falling
         * into the generic SYM_IMPORT branch below. Mirrors codegen.c. */
        char ekey[128]; snprintf(ekey,sizeof ekey,"%s:exit",g_squash_libc_soname);
        symtable_add_import(cg->sym, ekey);
        emit_iat_ref(cg, A64_X16, "exit");
        a64_emit(a, a64_BLR(A64_X16));
    } else if (fsym && fsym->kind == SYM_IMPORT) {
        /* IAT indirect call: ADRP + LDR + BLR.
         * dll=="extern" is a sentinel the parser uses for plain
         * `extern <decl>;` declarations (as opposed to a real Windows DLL
         * name) — meaningless as an ELF/PE import-DLL name, so it gets
         * dropped (elf_grp_add on Linux; pe_builder.c's ".dll"-suffix
         * filter on Windows), leaving the call's IAT/GOT slot never
         * created and the call unresolved ("no IAT entry for 'x'"). On
         * Linux, route through a64_emit_linux_extern_call (dynamic .so /
         * static .a / cross-".sqo" object / libc.so.6 default, in that
         * order — see its own comment). On Windows: unlike x64's
         * codegen.c (which must fall back to a genuine CROSS-OBJECT call
         * when the name isn't a real DLL export, because x64 supports
         * linking separately-compiled ".sqo" objects), Windows/ARM64 has
         * no cross-object linking support (only Linux/ARM64 gained it, see
         * a64_emit_linux_extern_call) — so "extern" here can only ever
         * mean "a real Windows API function declared via a bodyless
         * extern/EXTERN_C prototype" (EXTERN_C is literally "#define
         * EXTERN_C extern" in plain C, an extremely common idiom
         * throughout the Windows SDK — see project memory for the
         * identical x64 bug this mirrors). Check the known-name table
         * (symtable_find_dll, now backed by real .lib import-library
         * parsing, see implib.c) and default to msvcrt.dll otherwise,
         * matching x64's own final unknown-call fallback. */
        if (fsym->dll && strcmp(fsym->dll, "extern") == 0 && cg->is_linux) {
            a64_emit_linux_extern_call(cg, fsym->name);
        } else {
            const char *dll = fsym->dll;
            if (fsym->dll && strcmp(fsym->dll, "extern") == 0) {
                dll = symtable_find_dll(cg->sym, fsym->name);
                if (!dll) dll = "msvcrt.dll";
            }
            char lkey[512]; snprintf(lkey,sizeof lkey,"%s:%s",dll,fsym->name);
            symtable_add_import(cg->sym, lkey);
            emit_iat_ref(cg, A64_X16, fsym->name);
            a64_emit(a, a64_BLR(A64_X16));
        }
    } else if (fsym && fsym->kind == SYM_FUNC && fsym->func_node && fsym->func_node->func.body) {
        /* Locally-defined function: direct call via BL with a fixup */
        int lbl_id = a64_find_label(a, n->call.name);
        if (lbl_id < 0) lbl_id = a64_new_label(a, n->call.name);
        int patch = a->code_len;
        a64_emit(a, a64_BL(0));
        a64_add_fixup(a, patch, lbl_id);
    } else if (n->call.name) {
        /* Forward-declared with no body (e.g. libc/Win32 prototypes from
         * stdio.h/windows.h — real code overwhelmingly reaches this via a
         * PLAIN (non-"extern"-keyword) bodyless declaration, e.g. squash's
         * own "int printf(const char *fmt, ...);" shim, which registers as
         * SYM_FUNC, not SYM_IMPORT, so it lands here rather than in the
         * branch above) or entirely unknown. Linux: route through
         * a64_emit_linux_extern_call (dynamic .so / static .a / cross-
         * ".sqo" object / libc.so.6 default, in that order — see its own
         * comment; the libc.so.6 default preserves this branch's original,
         * unconditional behavior when none of the new resolution paths
         * apply). Windows: this used to ALSO hardcode libc.so.6
         * unconditionally — a Linux-only concept that doesn't exist on
         * Windows at all, so EVERY such call (originally: any libc-shaped
         * function declared through squash's own stdio.h/stdlib.h shim,
         * e.g. printf) silently produced an import spec pe_builder.c's
         * ".dll"-suffix filter then dropped entirely, leaving the call's
         * IAT slot never created ("no IAT entry for 'printf'") and the
         * call unresolved at runtime with no compile-time error. Check
         * the known-name table first (same as the SYM_IMPORT branch
         * above), defaulting to msvcrt.dll — matching x64's own
         * unknown-call fallback exactly. */
        if (cg->is_linux) {
            a64_emit_linux_extern_call(cg, n->call.name);
        } else {
            const char *dll = symtable_find_dll(cg->sym, n->call.name);
            if (!dll) dll = "msvcrt.dll";
            char lkey[512]; snprintf(lkey,sizeof lkey,"%s:%s",dll,n->call.name);
            symtable_add_import(cg->sym, lkey);
            emit_iat_ref(cg, A64_X16, n->call.name);
            a64_emit(a, a64_BLR(A64_X16));
        }
    } else {
        a64_emit(a, a64_NOP());   /* unknown call */
    }
    /* Restore SP after a call that used the overflow-argument stack area
     * above — must happen before the return value is used/interpreted by
     * anything below (though X0 itself is untouched by this). */
    if (stack_bytes > 0) a64_sp_adjust(a, A64_SP, A64_SP, stack_bytes, 0);

    /* External functions (real, natively-compiled libc/OS code called
     * through the GOT) only guarantee their declared return width is
     * valid — e.g. a function declared to return `int` may leave garbage
     * in the upper 32 bits of x0, since the C ABI doesn't require callees
     * to sign/zero-extend narrower-than-register return values. Squash's
     * own codegen keeps values consistently extended across the full 64
     * bits throughout, so this only needs correcting right after a call
     * to code outside squash's control. Locally-defined squash functions
     * don't need this: their own `return` already produces a consistent
     * 64-bit value. */
    if (fsym && fsym->func_node && !(fsym->kind == SYM_FUNC && fsym->func_node->func.body)) {
        TypeInfo *rt = fsym->func_node->func.ret_type;
        if (rt && rt->pointer_depth == 0) {
            /* a64_sizeof_type_sym, NOT typeinfo_size_a64 -- the latter has
             * no symbol-table access and silently defaults an unrecognized
             * typedef/enum return type (e.g. Vulkan's "VkResult", a
             * typedef'd enum) to 8 bytes instead of its real 4, which
             * skipped this fixup entirely for exactly the case it exists
             * to handle. Reproduced directly: "VkResult vr =
             * vkCreateInstance(...); if (vr == VK_SUCCESS)" took the wrong
             * branch even though (int)vr and (int)VK_SUCCESS were both 0,
             * because vr's stored/loaded value still carried whatever
             * garbage the real driver call left in x0's upper 32 bits --
             * this fixup never ran to clear it. */
            int sz = a64_sizeof_type_sym(cg->sym, rt, 1);
            if (sz == 4)      a64_emit(a, rt->is_unsigned ? a64_UXTW(A64_X0, A64_X0) : a64_SXTW(A64_X0, A64_X0));
            else if (sz == 2) a64_emit(a, rt->is_unsigned ? 0xD3403C00u|A64_X0 : 0x93403C00u|A64_X0);
            else if (sz == 1) a64_emit(a, rt->is_unsigned ? 0xD3401C00u|A64_X0 : 0x93401C00u|A64_X0);
        }
    }
}

/* =========================================================================
 * Float expression evaluation — result in d0
 * ========================================================================= */
static void a64_float_expr(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    if (!n) return;
    switch (n->kind) {
    case AST_FLOAT: {
        const char *lbl = a64_intern_float(cg, n->fnum.value);
        emit_data_ref(cg, A64_X8, lbl, RELOC_A64_DATA_ADRP, RELOC_A64_DATA_LO12);
        a64_emit(a, a64_FLDR(A64_D0, A64_X8, 0));
        break;
    }
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (s && (s->kind == SYM_VAR || s->kind == SYM_PARAM)) {
            int fp_off = a64_sym_fp_off(s);
            a64_emit(a, a64_FLDR(A64_D0, A64_FP, fp_off));
        }
        break;
    }
    case AST_BINARY: {
        const char *op = n->binary.op;
        a64_float_expr(cg, n->binary.right);
        push_float_temp(cg);
        a64_float_expr(cg, n->binary.left);
        pop_float_temp_into_d1(cg);
        /* now: left=d0, right=d1 */
        if (strcmp(op,"+") == 0)      a64_emit(a, a64_FADD(A64_D0, A64_D0, A64_D1));
        else if (strcmp(op,"-") == 0) a64_emit(a, a64_FSUB(A64_D0, A64_D0, A64_D1));
        else if (strcmp(op,"*") == 0) a64_emit(a, a64_FMUL(A64_D0, A64_D0, A64_D1));
        else if (strcmp(op,"/") == 0) a64_emit(a, a64_FDIV(A64_D0, A64_D0, A64_D1));
        else if (strcmp(op,"<") == 0 || strcmp(op,">")==0 ||
                 strcmp(op,"<=") == 0|| strcmp(op,">=")==0 ||
                 strcmp(op,"==") == 0|| strcmp(op,"!=")==0) {
            a64_emit(a, a64_FCMP(A64_D0, A64_D1));
            int cond = A64_EQ;
            if (strcmp(op,"<") == 0)       cond = A64_MI;
            else if (strcmp(op,">")==0)    cond = A64_GT;
            else if (strcmp(op,"<=")==0)   cond = A64_LS;
            else if (strcmp(op,">=")==0)   cond = A64_GE;
            else if (strcmp(op,"!=")==0)   cond = A64_NE;
            a64_emit(a, a64_CSET(A64_X0, cond));
            a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
        }
        break;
    }
    case AST_UNARY:
        if (strcmp(n->unary.op,"-") == 0) {
            a64_float_expr(cg, n->unary.operand);
            a64_emit(a, a64_FNEG(A64_D0, A64_D0));
        } else {
            a64_float_expr(cg, n->unary.operand);
        }
        break;
    case AST_CAST:
        if (a64_is_float_type(cg->sym, n->cast.type)) {
            if (a64_is_float(cg, n->cast.expr)) {
                a64_float_expr(cg, n->cast.expr);
            } else {
                a64_expr(cg, n->cast.expr);
                a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
            }
        }
        break;
    case AST_CALL:
        a64_emit_call(cg, n);
        if (!a64_is_float(cg, n)) {
            /* Callee returns int (or its return type is unknown, e.g. an
             * external libc function with no recorded signature): result
             * is in x0, convert up to d0. If the callee genuinely returns
             * float/double, a64_emit_call already left it in d0. */
            a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
        }
        break;
    case AST_MEMBER:
    case AST_INDEX:
        /* A double-typed struct field or array element: the default case
         * below would read the raw IEEE-754 bit pattern as an INTEGER via
         * a64_expr() and numerically convert it with SCVTF, producing a
         * wildly wrong value instead of the actual stored double. Compute
         * the lvalue's address and load it as a double directly. */
        a64_lvalue(cg, n);
        /* A real 4-byte `float` field/element (not `double`) was only ever
         * written as 4 bytes at this exact offset (see the mirroring fix
         * in AST_ASSIGN's float-lvalue store) — loading 8 bytes here would
         * read half of the NEXT field/element too. See a64_FLDR_S's own
         * comment for the full story. */
        if (a64_lvalue_size(cg, n) == 4) {
            a64_emit(a, a64_FLDR_S(A64_D0, A64_X0, 0));
            a64_emit(a, a64_FCVT_S_TO_D(A64_D0, A64_D0));
        } else {
            a64_emit(a, a64_FLDR(A64_D0, A64_X0, 0));
        }
        break;
    default:
        /* fall back to int expr then convert */
        a64_expr(cg, n);
        a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
        break;
    }
}

/* Storage size of an lvalue expression (for ++/-- load/store width).
 * Using a fixed 64-bit LDR/STR regardless of the operand's real size is
 * wrong whenever that size is narrower than 8 bytes: a 4-byte "int" slot
 * is only ever written via a 32-bit STR (a64_store_to_sym), leaving the
 * top 4 bytes of its 8-byte-aligned stack slot as leftover garbage from
 * whatever was there before — a subsequent 64-bit LDR (as ++/-- used to
 * do unconditionally) reads that garbage into the high bits, corrupting
 * any address arithmetic done with the result (e.g. "buf[wi++]"). */
static int a64_lvalue_size(CodeGenA64 *cg, ASTNode *n) {
    if (!n) return 8;
    switch (n->kind) {
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (s && s->type) return a64_sizeof_type_sym(cg->sym, s->type, 1);
        return 8;
    }
    case AST_MEMBER: {
        int off;
        ASTNode *f = a64_resolve_field(cg, n->member.obj, n->member.field, &off);
        if (f && f->field.type) return a64_sizeof_type_sym(cg->sym, f->field.type, 1);
        return 8;
    }
    case AST_INDEX:
        return a64_index_elem_size(cg, n->index.array);
    case AST_DEREF:
        return a64_index_elem_size(cg, n->deref.operand);
    default:
        return 8;
    }
}

/* Sized load/store pair for ++/-- — sign-extends narrower-than-64-bit
 * loads (via LDRSW for 4-byte, since that's the only signed variant the
 * assembler exposes) so the 1-added/subtracted 64-bit arithmetic and any
 * later use of the result as a full expression value stay correct; stores
 * always narrow back down so adjacent stack slots aren't clobbered. */
static void a64_emit_sized_load(Arm64Asm *a, int rt, int rn, int sz) {
    if (sz >= 8)       a64_emit(a, a64_LDR(rt, rn, 0));
    else if (sz == 4)  a64_emit(a, a64_LDRSW(rt, rn, 0));
    else if (sz == 2)  a64_emit(a, a64_LDRH(rt, rn, 0));
    else               a64_emit(a, a64_LDRB(rt, rn, 0));
}
static void a64_emit_sized_store(Arm64Asm *a, int rt, int rn, int sz) {
    if (sz >= 8)       a64_emit(a, a64_STR(rt, rn, 0));
    else if (sz == 4)  a64_emit(a, a64_STR32(rt, rn, 0));
    else if (sz == 2)  a64_emit(a, a64_STRH(rt, rn, 0));
    else               a64_emit(a, a64_STRB(rt, rn, 0));
}

/* =========================================================================
 * lvalue: compute address of l-value, leave in x0
 * ========================================================================= */
static void a64_lvalue(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    if (!n) return;
    switch (n->kind) {
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        a64_load_sym_addr(cg, s);
        break;
    }
    case AST_DEREF:
        a64_expr(cg, n->deref.operand);
        break;
    case AST_INDEX: {
        /* base + index * element_size */
        a64_expr(cg, n->index.index);
        push_temp(cg);
        a64_expr(cg, n->index.array);  /* base address */
        pop_temp_into_x1(cg);
        int esz = a64_index_elem_size(cg, n->index.array);
        if (esz > 1) {
            a64_load_imm(cg, A64_X8, esz);
            a64_emit(a, a64_MUL(A64_X1, A64_X1, A64_X8));
        }
        a64_emit(a, a64_ADD(A64_X0, A64_X0, A64_X1));
        break;
    }
    case AST_MEMBER: {
        /* Compute struct base, add field offset */
        if (n->member.arrow) {
            a64_expr(cg, n->member.obj);   /* pointer in x0 */
        } else {
            a64_lvalue(cg, n->member.obj); /* address in x0 */
        }
        /* look up field offset in struct/union */
        int foff = 0;
        a64_resolve_field(cg, n->member.obj, n->member.field, &foff);
        if (foff != 0) {
            if (foff > 0 && foff <= 4095)
                a64_emit(a, a64_ADD_imm(A64_X0, A64_X0, foff));
            else {
                a64_load_imm(cg, A64_X8, foff);
                a64_emit(a, a64_ADD(A64_X0, A64_X0, A64_X8));
            }
        }
        break;
    }
    default:
        a64_expr(cg, n);
        break;
    }
}

/* =========================================================================
 * Store x0 to the lvalue described by lhs_node
 * ========================================================================= */
static void a64_store_to_lvalue(CodeGenA64 *cg, ASTNode *lhs) {
    Arm64Asm *a = cg->asm_;
    if (!lhs) return;
    /* Save value in x19 (callee-saved scratch) */
    a64_emit(a, a64_MOV(A64_X19, A64_X0));
    a64_lvalue(cg, lhs);   /* address in x0 */
    /* Determine store size (lhs->type is never populated by the parser) */
    int sz = 8;
    if (lhs->kind == AST_INDEX) {
        sz = a64_index_elem_size(cg, lhs->index.array);
    } else if (lhs->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, lhs->var.name);
        /* a64_sizeof_type_sym (not typeinfo_size_a64, which has no
         * SymTable access and so can't resolve typedefs at all) — a
         * typedef'd 4-byte type like "VkFlags"/"uint32_t" fell through
         * typeinfo_size_a64's fixed name list straight to its pointer-
         * sized (8) default, so a store to a VkFlags-typed struct field
         * (e.g. "bufBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;")
         * wrote a full 8 bytes instead of 4 — clobbering the ADJACENT
         * field right after it with zero (the store value's upper 32
         * bits) every time (reproduced directly: writing a real Vulkan
         * VkBufferMemoryBarrier's "dstAccessMask" field silently zeroed
         * the "srcQueueFamilyIndex" field declared immediately after it,
         * corrupting a barrier the GPU driver later used — this was the
         * real cause of a crash deep in driver code that looked totally
         * unrelated to the actual bug). */
        if (s && s->type) sz = a64_sizeof_type_sym(cg->sym, s->type, 1);
    } else if (lhs->kind == AST_MEMBER) {
        int foff;
        ASTNode *fld = a64_resolve_field(cg, lhs->member.obj, lhs->member.field, &foff);
        if (fld && fld->field.type) sz = a64_sizeof_type_sym(cg->sym, fld->field.type, 1);
    } else if (lhs->kind == AST_DEREF) {
        sz = a64_index_elem_size(cg, lhs->deref.operand);
    } else if (lhs->type) {
        sz = a64_sizeof_type_sym(cg->sym, lhs->type, 1);
    }
    if (sz >= 8)
        a64_emit(a, a64_STR(A64_X19, A64_X0, 0));
    else if (sz == 4)
        a64_emit(a, a64_STR32(A64_X19, A64_X0, 0));
    else if (sz == 2)
        a64_emit(a, a64_STRH(A64_X19, A64_X0, 0));
    else
        a64_emit(a, a64_STRB(A64_X19, A64_X0, 0));
}

/* =========================================================================
 * Main integer expression evaluator — result in x0
 * ========================================================================= */
static void a64_expr(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    if (!n) return;

    switch (n->kind) {
    /* ---------------------------------------------------------------------- */
    case AST_NUMBER:
        a64_load_imm(cg, A64_X0, n->num.value);
        break;

    case AST_CHAR_LIT:
        a64_load_imm(cg, A64_X0, n->char_lit.value);
        break;

    case AST_FLOAT:
        a64_float_expr(cg, n);
        a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
        break;

    /* ---------------------------------------------------------------------- */
    case AST_STRING: {
        const char *label = a64_intern_string(cg, n->str.value,
                                              (int)strlen(n->str.value));
        emit_data_ref(cg, A64_X0, label, RELOC_A64_DATA_ADRP, RELOC_A64_DATA_LO12);
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (!s) { a64_emit(a, a64_MOV(A64_X0, A64_XZR)); break; }
        if (s->kind == SYM_ENUM_VAL) { a64_load_imm(cg, A64_X0, s->enum_value); break; }
        /* Array-decay (a bare "float arr[4]" used as a value, e.g. as the
         * base of arr[i] in a64_lvalue's AST_INDEX case, or passed to a
         * function expecting a pointer) must be checked BEFORE the
         * float-type check right below: s->type here is the ARRAY
         * ELEMENT's type ("float"), so a plain float array symbol used to
         * incorrectly take the scalar-float branch — loading and
         * int-truncating whatever raw bits happened to be at the array's
         * own FP offset instead of yielding its address — leaving x0 as
         * garbage (usually 0). Every caller doing arr[i]=... then wrote
         * through a NULL/garbage base address (reproduced directly: "float
         * arr[4]; arr[0]=1.5f;" segfaulted with x0=0 at the store).
         * a64_load_sym_value (below) already gets this ordering right;
         * this call site just needs the same array_size check first. */
        if (s->array_size > 0 && (s->kind == SYM_VAR || s->kind == SYM_PARAM)) {
            a64_load_sym_addr(cg, s);
            break;
        }
        if (a64_is_float_type(cg->sym, s->type)) {
            int fp_off = a64_sym_fp_off(s);
            a64_emit(a, a64_FLDR(A64_D0, A64_FP, fp_off));
            a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            break;
        }
        a64_load_sym_value(cg, s);
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_ADDR:
        a64_lvalue(cg, n->addr.operand);
        break;

    case AST_DEREF: {
        a64_expr(cg, n->deref.operand);
        /* Dereference: load from [x0], sized to the pointee type.
         * n->type is never populated by the parser — *ptr needs exactly the
         * same "pointee size" resolution as ptr[0], so reuse that helper.
         * Defaulting to 8 (as before) silently over-reads a smaller pointee
         * (e.g. int*) by 4 garbage stack bytes, corrupting comparisons
         * whenever those bytes happen to be non-zero. */
        int sz = a64_index_elem_size(cg, n->deref.operand);
        int is_uns = a64_index_elem_is_unsigned(cg, n->deref.operand);
        if (sz >= 8)       a64_emit(a, a64_LDR(A64_X0, A64_X0, 0));
        else if (sz == 4)  a64_emit(a, is_uns ? a64_LDR32(A64_X0, A64_X0, 0) : a64_LDRSW(A64_X0, A64_X0, 0));
        else if (sz == 2)  a64_emit(a, a64_LDRH(A64_X0, A64_X0, 0));
        else               a64_emit(a, a64_LDRB(A64_X0, A64_X0, 0));
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_SIZEOF_TYPE: {
        /* typeinfo_size_a64 has no symbol-table access, so any struct or
         * typedef name it doesn't recognize as a builtin falls through to
         * the generic "is64?8:4" default — silently treating e.g.
         * "sizeof(PPState)" (a struct containing a 1024-element array of
         * another struct) as if PPState were an 8-byte pointer. That undersized
         * value then flows straight into calloc()/malloc() call sites,
         * allocating far too little memory and corrupting the heap on the
         * first out-of-bounds field write. Resolve through the symbol table
         * instead, mirroring every other sizeof-like site in this file. */
        TypeInfo *t = n->sizeof_type.type;
        int sz = a64_sizeof_type_sym(cg->sym, t, 1);
        a64_load_imm(cg, A64_X0, sz);
        break;
    }
    case AST_SIZEOF_EXPR: {
        /* n->sizeof_expr.expr->type is never populated by the parser (only
         * declaration-specific .type fields are set), so resolve through
         * the symbol table instead — mirroring codegen.c's x86 handling.
         * Critically must account for array_size: sizeof(char buf[64])
         * needs the full 64 bytes, not just the element/pointer size. */
        ASTNode *se = n->sizeof_expr.expr;
        int sz = 8; /* default: pointer size */
        if (se && se->kind == AST_VAR) {
            Symbol *esym = symtable_lookup(cg->sym, se->var.name);
            if (esym && esym->type) {
                sz = a64_sizeof_type_sym(cg->sym, esym->type, 1);
            }
        } else if (se && se->kind == AST_DEREF && se->deref.operand &&
                   se->deref.operand->kind == AST_VAR) {
            /* sizeof(*ptr) — size of the pointed-to type */
            Symbol *esym = symtable_lookup(cg->sym, se->deref.operand->var.name);
            if (esym && esym->type && esym->type->pointer_depth > 0) {
                sz = a64_sizeof_type_sym_ex(cg->sym, esym->type, 1, 1, 1);
            }
        } else if (se && se->type) {
            sz = a64_sizeof_type_sym(cg->sym, se->type, 1);
        }
        a64_load_imm(cg, A64_X0, sz);
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_CAST: {
        TypeInfo *ct = n->cast.type;
        if (a64_is_float_type(cg->sym, ct)) {
            a64_float_expr(cg, n);
            a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
        } else if (a64_is_float(cg, n->cast.expr)) {
            a64_float_expr(cg, n->cast.expr);
            a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
        } else {
            a64_expr(cg, n->cast.expr);
            /* Truncate/sign-extend based on target type */
            if (ct && ct->pointer_depth == 0) {
                int sz = a64_sizeof_type_sym(cg->sym, ct, 1);
                if (sz == 1 && !ct->is_unsigned)
                    a64_emit(a, 0x93401C00u | A64_X0);   /* SXTB */
                else if (sz == 1 && ct->is_unsigned)
                    a64_emit(a, 0xD3401C00u | A64_X0);   /* UXTB */
                else if (sz == 2 && !ct->is_unsigned)
                    a64_emit(a, 0x93403C00u | A64_X0);   /* SXTH */
                else if (sz == 4 && !ct->is_unsigned)
                    a64_emit(a, a64_SXTW(A64_X0, A64_X0));
            }
        }
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_INDEX: {
        a64_lvalue(cg, n);
        /* T x[N][M]: x[i] yields row i, itself an array — decays to a
         * pointer (address already in x0 from a64_lvalue above), matching
         * plain array-to-pointer decay. Only x[i][j] (a further AST_INDEX
         * wrapping this one) actually loads a scalar. */
        int base_is_2d = 0;
        if (n->index.array->kind == AST_VAR) {
            Symbol *bsym = symtable_lookup(cg->sym, n->index.array->var.name);
            base_is_2d = (bsym && bsym->array_size2 > 0);
        } else if (n->index.array->kind == AST_MEMBER) {
            int off;
            ASTNode *f = a64_resolve_field(cg, n->index.array->member.obj, n->index.array->member.field, &off);
            base_is_2d = (f && f->field.array_size2 > 0);
        }
        if (base_is_2d) break;
        /* load from address, sized to the element (n->type is never
         * populated by the parser — see a64_index_elem_size) */
        int sz = a64_index_elem_size(cg, n->index.array);
        int is_uns = a64_index_elem_is_unsigned(cg, n->index.array);
        if (sz >= 8)       a64_emit(a, a64_LDR(A64_X0, A64_X0, 0));
        else if (sz == 4)  a64_emit(a, is_uns ? a64_LDR32(A64_X0, A64_X0, 0) : a64_LDRSW(A64_X0, A64_X0, 0));
        else if (sz == 2)  a64_emit(a, a64_LDRH(A64_X0, A64_X0, 0));
        else               a64_emit(a, a64_LDRB(A64_X0, A64_X0, 0));
        break;
    }

    case AST_MEMBER: {
        a64_lvalue(cg, n);
        int foff;
        ASTNode *fld = a64_resolve_field(cg, n->member.obj, n->member.field, &foff);
        if (fld && fld->field.array_size > 0) {
            /* Array-typed field used as a value (e.g. u.bytes in u.bytes[i]):
             * decays to its address, matching C array-to-pointer decay —
             * a64_lvalue above already left that address in x0. */
            break;
        }
        int sz = 8;
        if (fld && fld->field.type) sz = a64_sizeof_type_sym(cg->sym, fld->field.type, 1);
        int fld_unsigned = fld && fld->field.type && a64_type_is_unsigned(cg->sym, fld->field.type);
        if (sz >= 8)       a64_emit(a, a64_LDR(A64_X0, A64_X0, 0));
        else if (sz == 4)  a64_emit(a, fld_unsigned ? a64_LDR32(A64_X0, A64_X0, 0) : a64_LDRSW(A64_X0, A64_X0, 0));
        else if (sz == 2)  a64_emit(a, a64_LDRH(A64_X0, A64_X0, 0));
        else               a64_emit(a, a64_LDRB(A64_X0, A64_X0, 0));
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_UNARY: {
        const char *op = n->unary.op;
        if (strcmp(op, "!") == 0) {
            a64_expr(cg, n->unary.operand);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            a64_emit(a, a64_CSET(A64_X0, A64_EQ));
        } else if (strcmp(op, "-") == 0) {
            a64_expr(cg, n->unary.operand);
            a64_emit(a, a64_NEG(A64_X0, A64_X0));
        } else if (strcmp(op, "~") == 0) {
            a64_expr(cg, n->unary.operand);
            a64_emit(a, a64_MVN(A64_X0, A64_X0));
        } else if (strcmp(op, "++") == 0 && !n->unary.post) {
            /* pre-increment: ++v */
            int sz = a64_lvalue_size(cg, n->unary.operand);
            a64_lvalue(cg, n->unary.operand);
            a64_emit(a, a64_MOV(A64_X8, A64_X0));   /* addr in x8 */
            a64_emit_sized_load(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_ADD_imm(A64_X0, A64_X0, 1));
            a64_emit_sized_store(a, A64_X0, A64_X8, sz);
        } else if (strcmp(op, "--") == 0 && !n->unary.post) {
            int sz = a64_lvalue_size(cg, n->unary.operand);
            a64_lvalue(cg, n->unary.operand);
            a64_emit(a, a64_MOV(A64_X8, A64_X0));
            a64_emit_sized_load(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_SUB_imm(A64_X0, A64_X0, 1));
            a64_emit_sized_store(a, A64_X0, A64_X8, sz);
        } else if (strcmp(op, "++") == 0 && n->unary.post) {
            /* post-increment: v++ */
            int sz = a64_lvalue_size(cg, n->unary.operand);
            a64_lvalue(cg, n->unary.operand);
            a64_emit(a, a64_MOV(A64_X8, A64_X0));
            a64_emit_sized_load(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_MOV(A64_X9, A64_X0));   /* save old value */
            a64_emit(a, a64_ADD_imm(A64_X0, A64_X0, 1));
            a64_emit_sized_store(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_MOV(A64_X0, A64_X9));   /* return old */
        } else if (strcmp(op, "--") == 0 && n->unary.post) {
            int sz = a64_lvalue_size(cg, n->unary.operand);
            a64_lvalue(cg, n->unary.operand);
            a64_emit(a, a64_MOV(A64_X8, A64_X0));
            a64_emit_sized_load(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_MOV(A64_X9, A64_X0));
            a64_emit(a, a64_SUB_imm(A64_X0, A64_X0, 1));
            a64_emit_sized_store(a, A64_X0, A64_X8, sz);
            a64_emit(a, a64_MOV(A64_X0, A64_X9));
        } else if (strcmp(op, "*") == 0) {
            /* dereference — same as AST_DEREF */
            a64_expr(cg, n->unary.operand);
            a64_emit(a, a64_LDR(A64_X0, A64_X0, 0));
        } else if (strcmp(op, "&") == 0) {
            a64_lvalue(cg, n->unary.operand);
        } else {
            a64_expr(cg, n->unary.operand);
        }
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_BINARY: {
        const char *op = n->binary.op;

        /* Comma operator: evaluate left (for side effects only), then
         * right — the expression's value is the right operand's. Falling
         * through to the generic integer-binary path below would be wrong
         * two ways at once: that path evaluates .right before .left (side
         * effects run out of order), and since no operator branch matches
         * ",", it leaves whatever a64_expr(.left) last wrote in x0 as the
         * "result" — the left operand's value instead of the right's. */
        if (strcmp(op, ",") == 0) {
            a64_expr(cg, n->binary.left);
            a64_expr(cg, n->binary.right);
            break;
        }

        /* Short-circuit logical operators */
        if (strcmp(op, "&&") == 0) {
            int end_lbl = a64_new_label(a, "");
            a64_expr(cg, n->binary.left);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            int patch = a->code_len;
            a64_emit(a, a64_Bcond(A64_EQ, 0));   /* branch to end if false */
            a64_add_fixup(a, patch, end_lbl);
            a64_expr(cg, n->binary.right);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            a64_emit(a, a64_CSET(A64_X0, A64_NE));
            a64_def_label(a, end_lbl);
            /* NOTE: deliberately no a64_resolve() here — see the removal
             * comment on AST_IF's own a64_resolve() call below for why an
             * intermediate resolve mid-expression is actively wrong, not
             * just redundant, whenever this operator is nested inside a
             * still-open enclosing construct (e.g. "if (a || b) { x ? y :
             * z; }" — a real, confirmed bug this exact shape reproduced:
             * a spurious "unresolved label" print for the if-statement's
             * OWN not-yet-defined end/else label, since a64_resolve()
             * just iterates every outstanding fixup unconditionally, not
             * only the ones this operator itself just created). Resolving
             * fixups is a whole-function-body operation (see the single
             * authoritative a64_resolve() call after the function body is
             * fully generated); doing it here bought nothing (this
             * function's own end_lbl fixup would already be current by
             * function-end too) and actively broke on nesting. */
            break;
        }
        if (strcmp(op, "||") == 0) {
            int end_lbl = a64_new_label(a, "");
            a64_expr(cg, n->binary.left);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            int patch = a->code_len;
            a64_emit(a, a64_Bcond(A64_NE, 0));   /* branch to end if true */
            a64_add_fixup(a, patch, end_lbl);
            a64_expr(cg, n->binary.right);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            a64_emit(a, a64_CSET(A64_X0, A64_NE));
            a64_def_label(a, end_lbl);
            /* See "&&" above — same reasoning, same fix. */
            break;
        }

        /* Float binary */
        if (a64_is_float(cg, n->binary.left) || a64_is_float(cg, n->binary.right)) {
            a64_float_expr(cg, n);
            if (strcmp(op,"==")==0||strcmp(op,"!=")==0||strcmp(op,"<")==0||
                strcmp(op,">")==0||strcmp(op,"<=")==0||strcmp(op,">=")==0)
                /* already an int in x0 from float_expr comparison path */;
            else
                a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            break;
        }

        /* Integer binary: eval right first, push, eval left, pop */
        a64_expr(cg, n->binary.right);
        push_temp(cg);
        a64_expr(cg, n->binary.left);
        pop_temp_into_x1(cg);   /* left=x0, right=x1 */

        if (strcmp(op,"+") == 0)
            a64_emit(a, a64_ADD(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"-") == 0)
            a64_emit(a, a64_SUB(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"*") == 0)
            a64_emit(a, a64_MUL(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"/") == 0)
            a64_emit(a, a64_SDIV(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"%") == 0) {
            /* x0 = x0 - (x0/x1)*x1 */
            a64_emit(a, a64_MOV(A64_X9, A64_X0));
            a64_emit(a, a64_SDIV(A64_X8, A64_X0, A64_X1));
            a64_emit(a, a64_MUL(A64_X8, A64_X8, A64_X1));
            a64_emit(a, a64_SUB(A64_X0, A64_X9, A64_X8));
        }
        else if (strcmp(op,"&") == 0)
            a64_emit(a, a64_AND(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"|") == 0)
            a64_emit(a, a64_ORR(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"^") == 0)
            a64_emit(a, a64_EOR(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,"<<") == 0)
            a64_emit(a, a64_LSL(A64_X0, A64_X0, A64_X1));
        else if (strcmp(op,">>") == 0) {
            /* signed or unsigned shift */
            int unsign = n->binary.left->type && n->binary.left->type->is_unsigned;
            if (unsign)
                a64_emit(a, a64_LSR(A64_X0, A64_X0, A64_X1));
            else
                a64_emit(a, a64_ASR(A64_X0, A64_X0, A64_X1));
        }
        else if (strcmp(op,"==") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_EQ)); }
        else if (strcmp(op,"!=") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_NE)); }
        else if (strcmp(op,"<") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_LT)); }
        else if (strcmp(op,"<=") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_LE)); }
        else if (strcmp(op,">") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_GT)); }
        else if (strcmp(op,">=") == 0)
            { a64_emit(a, a64_CMP(A64_X0, A64_X1)); a64_emit(a, a64_CSET(A64_X0, A64_GE)); }
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_ASSIGN: {
        const char *op = n->assign.op;
        /* Whole-struct assignment ("dst = src;" where both sides are a
         * struct/union VALUE) must be checked before anything else here —
         * see a64_struct_copy_size_of's own comment for why the normal
         * assignment path below is wrong for this shape. Address-to-
         * address byte copy, matching x86-64's identical fast path. */
        if (strcmp(op,"=") == 0) {
            int lsz = a64_struct_copy_size_of(cg, n->assign.lhs);
            int rsz = a64_struct_copy_size_of(cg, n->assign.rhs);
            if (lsz > 0 && lsz == rsz) {
                a64_struct_copy_addr_of(cg, n->assign.rhs);
                push_temp(cg);
                a64_struct_copy_addr_of(cg, n->assign.lhs);
                a64_emit(a, a64_MOV(A64_X2, A64_X0));   /* x2 = dst addr */
                pop_temp_into_x1(cg);                    /* x1 = src addr */
                int off = 0;
                for (; off + 8 <= lsz; off += 8) {
                    a64_emit(a, a64_LDR(A64_X8, A64_X1, off));
                    a64_emit(a, a64_STR(A64_X8, A64_X2, off));
                }
                for (; off + 4 <= lsz; off += 4) {
                    a64_emit(a, a64_LDR32(A64_X8, A64_X1, off));
                    a64_emit(a, a64_STR32(A64_X8, A64_X2, off));
                }
                for (; off < lsz; off++) {
                    a64_emit(a, a64_LDRB(A64_X8, A64_X1, off));
                    a64_emit(a, a64_STRB(A64_X8, A64_X2, off));
                }
                a64_emit(a, a64_MOV(A64_X0, A64_X2));
                break;
            }
        }
        /* Compound assignments: load current, combine, store */
        if (strcmp(op,"=") != 0) {
            /* Load current value */
            a64_expr(cg, n->assign.lhs);
            push_temp(cg);
            if (a64_is_float(cg, n->assign.rhs)) {
                a64_float_expr(cg, n->assign.rhs);
                a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            } else {
                a64_expr(cg, n->assign.rhs);
            }
            pop_temp_into_x1(cg);  /* lhs=x1, rhs=x0 */
            if (strcmp(op,"+=") == 0)       a64_emit(a, a64_ADD(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"-=") == 0)  a64_emit(a, a64_SUB(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"*=") == 0)  a64_emit(a, a64_MUL(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"/=") == 0)  a64_emit(a, a64_SDIV(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"%=") == 0) {
                a64_emit(a, a64_MOV(A64_X9, A64_X1));
                a64_emit(a, a64_SDIV(A64_X8, A64_X1, A64_X0));
                a64_emit(a, a64_MUL(A64_X8, A64_X8, A64_X0));
                a64_emit(a, a64_SUB(A64_X0, A64_X9, A64_X8));
            }
            else if (strcmp(op,"&=") == 0)  a64_emit(a, a64_AND(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"|=") == 0)  a64_emit(a, a64_ORR(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"^=") == 0)  a64_emit(a, a64_EOR(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,"<<=") == 0) a64_emit(a, a64_LSL(A64_X0, A64_X1, A64_X0));
            else if (strcmp(op,">>=") == 0) a64_emit(a, a64_ASR(A64_X0, A64_X1, A64_X0));
        } else if (a64_is_float(cg, n->assign.lhs)) {
            /* Float-typed lvalue (e.g. a plain "double" var/param or a
             * double-typed struct field/array element): a64_store_to_lvalue
             * only knows how to store an integer/pointer value through
             * x-registers — routing a double through it (as the code below
             * does for everything else) truncates it via FCVTZS and stores
             * the resulting integer, silently destroying the value's bit
             * pattern. Compute the RHS as a float and store it directly. */
            if (a64_is_float(cg, n->assign.rhs)) {
                a64_float_expr(cg, n->assign.rhs);
            } else {
                a64_expr(cg, n->assign.rhs);
                a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
            }
            a64_lvalue(cg, n->assign.lhs);   /* address in x0; doesn't touch d0 */
            /* Real 4-byte `float` lvalue (struct field/array element, not
             * `double`): narrow before storing, or the 8-byte store below
             * overwrites the next field/element's first 4 bytes too — see
             * a64_FLDR_S's comment. */
            if (a64_lvalue_size(cg, n->assign.lhs) == 4) {
                a64_emit(a, a64_FCVT_D_TO_S(A64_D0, A64_D0));
                a64_emit(a, a64_FSTR_S(A64_D0, A64_X0, 0));
                a64_emit(a, a64_FCVT_S_TO_D(A64_D0, A64_D0));
            } else {
                a64_emit(a, a64_FSTR(A64_D0, A64_X0, 0));
            }
            /* Expression value: int-truncated, matching the (imperfect but
             * pre-existing) convention that a64_expr's AST_ASSIGN always
             * yields an int in x0 regardless of the assignment's own type. */
            a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            break;
        } else {
            /* Simple assignment */
            if (a64_is_float(cg, n->assign.rhs)) {
                a64_float_expr(cg, n->assign.rhs);
                a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            } else {
                a64_expr(cg, n->assign.rhs);
            }
        }
        a64_store_to_lvalue(cg, n->assign.lhs);
        /* The assignment expression's value is whatever was just stored,
         * which a64_store_to_lvalue leaves in x19. Re-evaluating the lvalue
         * here (as this used to do) would re-run any side effects it
         * contains — e.g. "*wp++ = c" would increment wp a second time,
         * since a64_lvalue on a AST_DEREF evaluates its operand expression,
         * including any post/pre-increment inside it. */
        a64_emit(a, a64_MOV(A64_X0, A64_X19));
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_TERNARY: {
        int else_lbl = a64_new_label(a, "");
        int end_lbl  = a64_new_label(a, "");
        a64_expr(cg, n->ternary.cond);
        a64_emit(a, a64_CMP_imm(A64_X0, 0));
        int p1 = a->code_len;
        a64_emit(a, a64_Bcond(A64_EQ, 0));
        a64_add_fixup(a, p1, else_lbl);
        a64_expr(cg, n->ternary.then_);
        int p2 = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p2, end_lbl);
        a64_def_label(a, else_lbl);
        a64_expr(cg, n->ternary.else_);
        a64_def_label(a, end_lbl);
        /* No a64_resolve() here — see AST_IF's removal comment: premature
         * mid-function resolution, wrong whenever nested inside a
         * still-open enclosing construct. */
        break;
    }

    /* ---------------------------------------------------------------------- */
    case AST_CALL: {
        a64_emit_call(cg, n);
        if (a64_is_float(cg, n)) {
            /* Callee's declared return type is float/double: real result is
             * in d0 (a64_emit_call already placed it there), but a64_expr
             * must produce an int result in x0 — truncate it down. */
            a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
        }
        /* Result is in x0 */
        break;
    }

    case AST_FUNC_PTR_CALL: {
        int i, nargs = n->fp_call.argc < 8 ? n->fp_call.argc : 8;
        /* Evaluate the function pointer first and spill it to a temp slot.
         * It must survive arbitrary argument evaluation below (which may
         * itself use any caller-saved scratch register internally), so a
         * stack spill is used rather than holding it in a register across
         * the argument-evaluation loop — holding it in X0 (as before) would
         * get clobbered by loading arg0 into X0 for the call. */
        a64_expr(cg, n->fp_call.func_expr);
        push_temp(cg);
        for (i = 0; i < nargs; i++) {
            a64_expr(cg, n->fp_call.args[i]);
            push_temp(cg);
        }
        for (i = nargs - 1; i >= 0; i--) {
            cg->cur_temp_depth--;
            int off = cg->temp_base + cg->cur_temp_depth * 8;
            a64_emit(a, a64_LDR(i, A64_FP, off));
        }
        cg->cur_temp_depth--;
        {
            int fp_off = cg->temp_base + cg->cur_temp_depth * 8;
            a64_emit(a, a64_LDR(A64_X9, A64_FP, fp_off));
        }
        a64_emit(a, a64_BLR(A64_X9));
        break;
    }

    default:
        /* Unhandled — emit zero */
        a64_emit(a, a64_MOV(A64_X0, A64_XZR));
        break;
    }
}

/* =========================================================================
 * Inline assembly (GCC extended asm) — AArch64 codegen
 * =========================================================================
 * Same practical, deliberately-scoped subset as the x86 side (codegen.c):
 * %N-numbered constraint operands (register class 'r', memory 'm',
 * digit-tied operands), raw physical register references (bare "x9"/"w9",
 * or the "%%x9" style some templates use), local numeric jump labels
 * (1:/1b/1f), and a fixed set of common AArch64 mnemonics (data movement,
 * arithmetic, the LDXR/STXR exclusive-access atomics, barriers, NOP/BRK).
 * AArch64 syntax differs from x86 in several ways this has to account for:
 * operand order is "dst, src1, src2" (not AT&T's src-then-dst), memory
 * operands use square brackets "[Xn, #imm]" (not parens), and a register
 * operand can be explicitly narrowed to its 32-bit W-form via a "%wN"
 * (rather than "%xN"/bare "%N") template reference. */
#define A64_ASM_MAX_OPS 16

typedef struct {
    int      is_output;
    int      is_readwrite;
    int      is_memory;
    int      tied_to;
    ASTNode *expr;
    int      reg;     /* assigned X register number (holds value, or address if is_memory) */
    int      width;   /* 4 or 8 — byte width of the C operand's type */
} AsmOpA64;

typedef struct {
    int kind;        /* 0=unrecognized, 1=operand ref (%N/%wN/%xN), 2=raw register, 3=immediate, 4=local label ref */
    int op_index;     /* kind==1 */
    int op_is_w;       /* kind==1: referenced via %wN (32-bit) rather than %N/%xN (64-bit) */
    int reg_id;        /* kind==2 */
    int reg_is_w;       /* kind==2: register named in its 32-bit "wN" form */
    int is_deref;      /* wrapped in [...] -> memory access */
    long long disp;
    long long imm;     /* kind==3 */
    int label_num;     /* kind==4 */
} AsmTokA64;

static int a64_asm_regname_to_id(const char *name, int *out_is_w) {
    *out_is_w = 0;
    if ((name[0]=='x'||name[0]=='w') && isdigit((unsigned char)name[1])) {
        int n2 = atoi(name+1);
        if (n2>=0 && n2<=30) { *out_is_w = (name[0]=='w'); return n2; }
    }
    if (strcmp(name,"sp")==0) return A64_SP;
    if (strcmp(name,"xzr")==0) return A64_XZR;
    if (strcmp(name,"wzr")==0) { *out_is_w=1; return A64_XZR; }
    return -1;
}

static int asm_a64_operand_width(CodeGenA64 *cg, ASTNode *expr) {
    if (!expr) return 8;
    if (expr->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, expr->var.name);
        if (s && s->type) {
            if (s->type->pointer_depth > 0) return 8;
            int sz = a64_sizeof_type_sym(cg->sym, s->type, 1);
            if (sz==4||sz==8) return sz;
        }
    } else if (expr->kind == AST_DEREF && expr->deref.operand && expr->deref.operand->kind==AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, expr->deref.operand->var.name);
        if (s && s->type && s->type->pointer_depth >= 1) {
            if (s->type->pointer_depth >= 2) return 8;
            TypeInfo tmp; memset(&tmp,0,sizeof tmp);
            tmp.base=s->type->base; tmp.pointer_depth=0; tmp.array_size=-1;
            tmp.is_unsigned=s->type->is_unsigned;
            int sz = a64_sizeof_type_sym(cg->sym, &tmp, 1);
            if (sz==4||sz==8) return sz;
        }
    }
    return 8;
}

static void asm_a64_push_reg(CodeGenA64 *cg, int reg) {
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_STR(reg, A64_FP, off));
    cg->cur_temp_depth++;
    if (cg->cur_temp_depth > cg->max_temp_depth) cg->max_temp_depth = cg->cur_temp_depth;
}
static void asm_a64_pop_reg(CodeGenA64 *cg, int reg) {
    cg->cur_temp_depth--;
    int off = cg->temp_base + cg->cur_temp_depth * 8;
    a64_emit(cg->asm_, a64_LDR(reg, A64_FP, off));
}
/* Evaluate expr's VALUE into X0, then move to dst. */
static void asm_a64_load_value_into(CodeGenA64 *cg, ASTNode *expr, int dst) {
    a64_expr(cg, expr);
    if (dst != A64_X0) a64_emit(cg->asm_, a64_MOV(dst, A64_X0));
}
/* Evaluate the ADDRESS a memory ('m') operand refers to into dst. */
static void asm_a64_load_address_into(CodeGenA64 *cg, ASTNode *expr, int dst) {
    if (expr && expr->kind == AST_DEREF) a64_expr(cg, expr->deref.operand);
    else a64_lvalue(cg, expr);
    if (dst != A64_X0) a64_emit(cg->asm_, a64_MOV(dst, A64_X0));
}

/* Classify one raw template operand token (whitespace already trimmed). */
static int asm_a64_classify_token(const char *tok_in, AsmTokA64 *out) {
    memset(out,0,sizeof *out);
    char tok[80]; snprintf(tok,sizeof tok,"%s",tok_in);
    char *p = tok;
    char *brack = strchr(p,'[');
    if (brack) {
        char *close = strchr(brack,']');
        int len = close ? (int)(close-brack-1) : (int)strlen(brack+1);
        if (len>63) len=63;
        char inner[64]; memcpy(inner,brack+1,len); inner[len]=0;
        /* "[Xn, #imm]" — split off an optional ", #imm" suffix inside the brackets */
        char *comma = strchr(inner,',');
        long long disp=0;
        if (comma) {
            *comma=0;
            char *dp = comma+1; while (*dp==' '||*dp=='\t') dp++;
            if (*dp=='#') dp++;
            disp = strtoll(dp,NULL,0);
        }
        out->is_deref = 1; out->disp = disp;
        p = inner;
    }
    if (p[0]=='%' && p[1]=='%') { p+=2; } /* tolerate GCC's universal %% escaping, rarely used on ARM templates */
    if (p[0]=='%' && (p[1]=='w'||p[1]=='x') && isdigit((unsigned char)p[2])) {
        out->kind=1; out->op_index=atoi(p+2); out->op_is_w=(p[1]=='w'); return 1;
    }
    if (p[0]=='%' && isdigit((unsigned char)p[1])) { out->kind=1; out->op_index=atoi(p+1); return 1; }
    {
        int is_w=0; int rid = a64_asm_regname_to_id(p,&is_w);
        if (rid>=0) { out->kind=2; out->reg_id=rid; out->reg_is_w=is_w; return 1; }
    }
    if (p[0]=='#') { out->kind=3; out->imm=strtoll(p+1,NULL,0); return 1; }
    if (isdigit((unsigned char)p[0]) && (p[1]=='f'||p[1]=='b') && p[2]=='\0') {
        out->kind=4; out->label_num=p[0]-'0'; return 1;
    }
    { char *endp; long long v=strtoll(p,&endp,0); if (endp!=p && *endp=='\0') { out->kind=3; out->imm=v; return 1; } }
    return 0;
}

/* True if this token refers to a 32-bit (W-register) value: either the
 * template used a bare "wN" register name, or a "%wN" operand reference,
 * or the referenced C operand's own type is 4 bytes wide. */
static int asm_a64_tok_is_w(AsmTokA64 *tok, AsmOpA64 *ops, int n_ops) {
    if (tok->kind==2) return tok->reg_is_w;
    if (tok->kind==1) return tok->op_is_w || (tok->op_index>=0 && tok->op_index<n_ops && ops[tok->op_index].width==4);
    return 0;
}

static int asm_a64_resolve_operand(AsmOpA64 *ops, int n_ops, AsmTokA64 *tok, int *out_reg, int *out_is_mem, long long *out_disp) {
    *out_is_mem = tok->is_deref; *out_disp = tok->disp;
    if (tok->kind==2) { *out_reg=tok->reg_id; return 1; }
    if (tok->kind==1) {
        if (tok->op_index<0||tok->op_index>=n_ops) return 0;
        *out_reg = ops[tok->op_index].reg;
        if (ops[tok->op_index].is_memory) *out_is_mem = 1;
        return 1;
    }
    return 0;
}

/* Encode one parsed instruction. AArch64 operand order: tok[0]=dst,
 * tok[1..]=src(s) — the opposite convention from x86 AT&T syntax. */
static void asm_a64_encode_insn(CodeGenA64 *cg, const char *mnemonic_in, AsmTokA64 *toks, int ntok,
                                 AsmOpA64 *ops, int n_ops, int local_labels[10]) {
    Arm64Asm *a = cg->asm_;
    char mnem[16]; snprintf(mnem,sizeof mnem,"%s",mnemonic_in);
    for (char *q=mnem; *q; q++) *q=(char)tolower((unsigned char)*q);

    int r0=A64_X0,r1=A64_X0,r2=A64_X0; int mem0=0,mem1=0,mem2=0; long long disp0=0,disp1=0,disp2=0;
    long long imm=0; int is_imm2=0;
    if (ntok>=1) asm_a64_resolve_operand(ops,n_ops,&toks[0],&r0,&mem0,&disp0);
    if (ntok>=2) { if (toks[1].kind==3) { imm=toks[1].imm; is_imm2=1; } else asm_a64_resolve_operand(ops,n_ops,&toks[1],&r1,&mem1,&disp1); }
    if (ntok>=3) { if (toks[2].kind==3) { imm=toks[2].imm; is_imm2=1; } else asm_a64_resolve_operand(ops,n_ops,&toks[2],&r2,&mem2,&disp2); }

    if (strcmp(mnem,"mov")==0 && ntok==2) {
        if (is_imm2) a64_load_imm(cg, r0, imm);
        else {
            int w = asm_a64_tok_is_w(&toks[0],ops,n_ops) || asm_a64_tok_is_w(&toks[1],ops,n_ops);
            a64_emit(a, w ? a64_MOV32(r0,r1) : a64_MOV(r0,r1));
        }
    }
    else if (strcmp(mnem,"ldr")==0 && ntok==2 && mem1) {
        a64_emit(a, a64_LDR(r0,r1,(int)disp1));
    }
    else if (strcmp(mnem,"str")==0 && ntok==2 && mem1) {
        a64_emit(a, a64_STR(r0,r1,(int)disp1));
    }
    else if (strcmp(mnem,"ldxr")==0 && ntok==2 && mem1) {
        int w = asm_a64_tok_is_w(&toks[0],ops,n_ops);
        a64_emit(a, w ? a64_LDXR32(r0,r1) : a64_LDXR(r0,r1));
    }
    else if (strcmp(mnem,"ldaxr")==0 && ntok==2 && mem1) {
        a64_emit(a, a64_LDAXR(r0,r1));
    }
    else if (strcmp(mnem,"stxr")==0 && ntok==3 && mem2) {
        int w = asm_a64_tok_is_w(&toks[1],ops,n_ops);
        a64_emit(a, w ? a64_STXR32(r0,r1,r2) : a64_STXR(r0,r1,r2));
    }
    else if (strcmp(mnem,"stlxr")==0 && ntok==3 && mem2) {
        a64_emit(a, a64_STLXR(r0,r1,r2));
    }
    else if ((strcmp(mnem,"add")==0||strcmp(mnem,"sub")==0||strcmp(mnem,"and")==0||strcmp(mnem,"orr")==0||strcmp(mnem,"eor")==0) && ntok==3) {
        int w = asm_a64_tok_is_w(&toks[0],ops,n_ops) || asm_a64_tok_is_w(&toks[1],ops,n_ops) || asm_a64_tok_is_w(&toks[2],ops,n_ops);
        uint32_t (*fn)(int,int,int) = NULL;
        if (strcmp(mnem,"add")==0) fn = w ? a64_ADD32 : a64_ADD;
        else if (strcmp(mnem,"sub")==0) fn = w ? a64_SUB32 : a64_SUB;
        else if (strcmp(mnem,"and")==0) fn = w ? a64_AND32 : a64_AND;
        else if (strcmp(mnem,"orr")==0) fn = w ? a64_ORR32 : a64_ORR;
        else if (strcmp(mnem,"eor")==0) fn = w ? a64_EOR32 : a64_EOR;
        if (is_imm2 && (strcmp(mnem,"add")==0||strcmp(mnem,"sub")==0)) {
            int is_add = strcmp(mnem,"add")==0;
            if (w) a64_emit(a, is_add ? a64_ADD_imm32(r0,r1,(int)imm) : a64_SUB_imm32(r0,r1,(int)imm));
            else   a64_emit(a, is_add ? a64_ADD_imm(r0,r1,(int)imm)   : a64_SUB_imm(r0,r1,(int)imm));
        } else if (fn) {
            a64_emit(a, fn(r0,r1,r2));
        }
    }
    else if (strcmp(mnem,"cmp")==0 && ntok==2) {
        int w = asm_a64_tok_is_w(&toks[0],ops,n_ops) || asm_a64_tok_is_w(&toks[1],ops,n_ops);
        if (is_imm2) a64_emit(a, w ? a64_CMP_imm32(r0,(int)imm) : a64_CMP_imm(r0,(int)imm));
        else a64_emit(a, w ? a64_CMP32(r0,r1) : a64_CMP(r0,r1));
    }
    else if (strcmp(mnem,"nop")==0) a64_emit(a, a64_NOP());
    else if (strcmp(mnem,"dmb")==0) a64_emit(a, a64_DMB_ISH());
    else if (strcmp(mnem,"dsb")==0) a64_emit(a, a64_DSB_SY());
    else if (strcmp(mnem,"isb")==0) a64_emit(a, a64_ISB());
    else if (strcmp(mnem,"wfe")==0) a64_emit(a, a64_WFE());
    else if (strcmp(mnem,"sev")==0) a64_emit(a, a64_SEV());
    else if (strcmp(mnem,"clrex")==0) a64_emit(a, a64_CLREX());
    else if (strcmp(mnem,"brk")==0 && ntok==1) a64_emit(a, a64_BRK((int)(toks[0].kind==3?toks[0].imm:0)));
    else if (ntok==1 && toks[0].kind==4 &&
             (strcmp(mnem,"b")==0||strcmp(mnem,"cbz")==0||strcmp(mnem,"cbnz")==0)) {
        /* plain "b 1b" with no register operand — rare; skipped, needs a
         * register for cbz/cbnz which this arm doesn't reach */
    }
    else if (ntok==2 && toks[1].kind==4 && (strcmp(mnem,"cbz")==0||strcmp(mnem,"cbnz")==0)) {
        int ln = toks[1].label_num;
        int w = asm_a64_tok_is_w(&toks[0],ops,n_ops);
        int is_z = strcmp(mnem,"cbz")==0;
        if (local_labels[ln] < 0) local_labels[ln] = a64_new_label(a,"asmlbl");
        int p = a->code_len;
        a64_emit(a, is_z ? (w ? a64_CBZ32(r0,0) : a64_CBZ(r0,0))
                          : (w ? a64_CBNZ32(r0,0) : a64_CBNZ(r0,0)));
        a64_add_fixup(a, p, local_labels[ln]);
    }
    else if (ntok==1 && toks[0].kind==4 && strcmp(mnem,"b")==0) {
        int ln = toks[0].label_num;
        if (local_labels[ln] < 0) local_labels[ln] = a64_new_label(a,"asmlbl");
        int p = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p, local_labels[ln]);
    }
    /* unrecognized mnemonic/operand shape: skipped (best-effort subset) */
}

static void codegen_asm_a64(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    AsmOpA64 ops[A64_ASM_MAX_OPS]; int n_ops=0;
    int n_outputs = n->asm_stmt.n_outputs;

    for (int i=0;i<n->asm_stmt.n_outputs && n_ops<A64_ASM_MAX_OPS;i++) {
        const char *c = n->asm_stmt.outputs[i].constraint;
        AsmOpA64 *op = &ops[n_ops++];
        memset(op,0,sizeof *op);
        op->is_output=1; op->tied_to=-1; op->expr=n->asm_stmt.outputs[i].expr;
        int ci=0;
        while (c[ci]=='='||c[ci]=='+'||c[ci]=='&') { if(c[ci]=='+') op->is_readwrite=1; ci++; }
        char cls[16]; int cl=0; while (c[ci] && c[ci]!=',' && cl<15) cls[cl++]=c[ci++]; cls[cl]=0;
        if (cl>0) { if (isdigit((unsigned char)cls[0])) op->tied_to=atoi(cls); else if (cls[0]=='m') op->is_memory=1; }
        op->width = asm_a64_operand_width(cg, op->expr);
    }
    for (int i=0;i<n->asm_stmt.n_inputs && n_ops<A64_ASM_MAX_OPS;i++) {
        const char *c = n->asm_stmt.inputs[i].constraint;
        AsmOpA64 *op = &ops[n_ops++];
        memset(op,0,sizeof *op);
        op->is_output=0; op->tied_to=-1; op->expr=n->asm_stmt.inputs[i].expr;
        int ci=0;
        while (c[ci]=='&') ci++;
        char cls[16]; int cl=0; while (c[ci] && c[ci]!=',' && cl<15) cls[cl++]=c[ci++]; cls[cl]=0;
        if (cl>0) { if (isdigit((unsigned char)cls[0])) op->tied_to=atoi(cls); else if (cls[0]=='m') op->is_memory=1; }
        op->width = asm_a64_operand_width(cg, op->expr);
    }

    /* Evaluate all operand expressions before touching the register pool —
     * same reasoning as the x86 side. */
    int has_slot[A64_ASM_MAX_OPS];
    for (int i=0;i<n_ops;i++) {
        int need_init = ops[i].is_memory || !(ops[i].is_output && !ops[i].is_readwrite && ops[i].tied_to<0);
        has_slot[i]=need_init;
        if (!need_init) continue;
        if (ops[i].is_memory) asm_a64_load_address_into(cg, ops[i].expr, A64_X0);
        else asm_a64_load_value_into(cg, ops[i].expr, A64_X0);
        asm_a64_push_reg(cg, A64_X0);
    }

    /* Register allocation: a fixed scratch pool (X9-X15) deliberately
     * disjoint from every register squash's OWN codegen uses internally
     * (X0/X1/X8 as expression scratch, X19 as store-back scratch, X16-X18/
     * X29/X30/SP reserved) — no letter-constrained "hard" registers exist
     * in standard AArch64 GCC inline asm the way x86 has a/b/c/d/S/D, so
     * this is simpler than the x86 allocator. */
    static const int pool[] = {A64_X9,A64_X10,A64_X11,A64_X12,A64_X13,A64_X14,A64_X15};
    int used[32]; for (int i=0;i<32;i++) used[i]=0;
    int pi=0;
    for (int i=0;i<n_ops;i++) {
        if (ops[i].tied_to>=0) continue;
        while (pi<7 && used[pool[pi]]) pi++;
        ops[i].reg = (pi<7) ? pool[pi++] : A64_X9;
        used[ops[i].reg]=1;
    }
    for (int pass=0; pass<3; pass++)
        for (int i=0;i<n_ops;i++)
            if (ops[i].tied_to>=0 && ops[i].tied_to<n_ops) ops[i].reg = ops[ops[i].tied_to].reg;

    /* Pop stashed values directly into their final registers, in reverse
     * push order (see the x86 side for why this must be a direct pop, not
     * staged through one shared temp register). */
    for (int i=n_ops-1;i>=0;i--) if (has_slot[i]) asm_a64_pop_reg(cg, ops[i].reg);

    /* Parse and encode the template, statement by statement. */
    int local_labels[10]; for (int i=0;i<10;i++) local_labels[i]=-1;
    char buf[4096]; snprintf(buf,sizeof buf,"%s",n->asm_stmt.template_str);
    { /* strip '#'-to-end-of-line GAS comments */
        char *ln = buf;
        while (*ln) {
            char *h = strchr(ln,'#'); char *nl = strchr(ln,'\n');
            /* AArch64 immediates ALSO use '#' (e.g. "#imm") — only treat it
             * as a comment if preceded by whitespace or start-of-line and
             * NOT immediately followed by a digit (a real immediate). */
            if (h && (h==ln || h[-1]==' ' || h[-1]=='\t') && !isdigit((unsigned char)h[1]) && h[1]!='-') {
                if (!nl || h<nl) { char *e = nl?nl:(ln+strlen(ln)); memmove(h,e,strlen(e)+1); }
            }
            if (!nl) break;
            ln = nl+1;
        }
    }
    {
        char *p = buf;
        while (*p) {
            while (*p==';'||*p=='\n'||*p=='\t'||*p==' '||*p=='\r') p++;
            if (!*p) break;
            char *start=p;
            while (*p && *p!=';' && *p!='\n') p++;
            int len=(int)(p-start); if (len>511) len=511;
            char stmt[512]; memcpy(stmt,start,len); stmt[len]=0;
            int sl=(int)strlen(stmt);
            while (sl>0 && (stmt[sl-1]==' '||stmt[sl-1]=='\t'||stmt[sl-1]=='\r')) stmt[--sl]=0;
            char *s2=stmt; while (*s2==' '||*s2=='\t') s2++;
            if (*s2) {
                char *colon = strchr(s2,':');
                int label_ok=0;
                if (colon && colon[1]=='\0' && colon!=s2) {
                    label_ok=1;
                    for (char *d=s2; d<colon; d++) if (!isdigit((unsigned char)*d)) { label_ok=0; break; }
                }
                if (label_ok) {
                    int ln2 = s2[0]-'0';
                    if (local_labels[ln2]<0) local_labels[ln2]=a64_new_label(a,"asmlbl");
                    a64_def_label(a, local_labels[ln2]);
                } else {
                    char mnem[16]; int mi=0; char *s3=s2;
                    while (*s3 && *s3!=' ' && *s3!='\t' && mi<15) mnem[mi++]=*s3++;
                    mnem[mi]=0;
                    while (*s3==' '||*s3=='\t') s3++;
                    AsmTokA64 toks[4]; int ntok=0;
                    /* Manually split on ',' — NOT bracket-aware if done via
                     * strtok, since an AArch64 memory operand like
                     * "[x0, #8]" itself contains a comma. Track bracket
                     * depth so that comma is treated as part of the token,
                     * not a separator. */
                    while (*s3 && ntok<4) {
                        while (*s3==' '||*s3=='\t') s3++;
                        if (!*s3) break;
                        char *tstart=s3; int depth=0;
                        while (*s3 && !(*s3==',' && depth==0)) {
                            if (*s3=='[') depth++; else if (*s3==']') depth--;
                            s3++;
                        }
                        int tlen=(int)(s3-tstart); if (tlen>63) tlen=63;
                        char tokbuf[64]; memcpy(tokbuf,tstart,tlen); tokbuf[tlen]=0;
                        int te=(int)strlen(tokbuf); while (te>0 && (tokbuf[te-1]==' '||tokbuf[te-1]=='\t')) tokbuf[--te]=0;
                        asm_a64_classify_token(tokbuf,&toks[ntok]);
                        ntok++;
                        if (*s3==',') s3++;
                    }
                    asm_a64_encode_insn(cg, mnem, toks, ntok, ops, n_ops, local_labels);
                }
            }
        }
    }
    a64_resolve(a);

    /* Write outputs back, protecting all values on the stack first (see
     * the x86 side for why: an earlier output's address computation could
     * otherwise clobber a later output still sitting in its own register). */
    for (int i=0;i<n_outputs;i++) if (!ops[i].is_memory) asm_a64_push_reg(cg, ops[i].reg);
    for (int i=n_outputs-1;i>=0;i--) {
        if (ops[i].is_memory) continue;
        a64_lvalue(cg, ops[i].expr);           /* x0 = &expr */
        a64_emit(a, a64_MOV(A64_X19, A64_X0));
        asm_a64_pop_reg(cg, A64_X0);           /* x0 = this output's real value */
        if (ops[i].width>=8) a64_emit(a, a64_STR(A64_X0, A64_X19, 0));
        else a64_emit(a, a64_STR32(A64_X0, A64_X19, 0));
    }
}

/* =========================================================================
 * Statement generator
 * ========================================================================= */
static void a64_stmt(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    if (!n) return;

    switch (n->kind) {
    case AST_EXPR_STMT:
        a64_expr(cg, n->expr_stmt.expr);
        break;

    case AST_RETURN: {
        if (n->ret.expr) {
            if (cg->cur_func_is_float_ret) {
                /* Function declared to return float/double: result must
                 * stay in d0 (AAPCS64), converting int expressions up
                 * rather than truncating float expressions down. */
                if (a64_is_float(cg, n->ret.expr)) {
                    a64_float_expr(cg, n->ret.expr);
                } else {
                    a64_expr(cg, n->ret.expr);
                    a64_emit(a, a64_SCVTF(A64_D0, A64_X0));
                }
            } else if (a64_is_float(cg, n->ret.expr)) {
                a64_float_expr(cg, n->ret.expr);
                a64_emit(a, a64_FCVTZS(A64_X0, A64_D0));
            } else {
                a64_expr(cg, n->ret.expr);
            }
        } else {
            a64_emit(a, a64_MOV(A64_X0, A64_XZR));
        }
        cg->cur_func_has_return = 1;
        /* Epilogue: restore x19, then FP/LR, return.
         * Must reuse the prologue's fixed x19_slot/frame_size — cur_local_off
         * only reflects locals declared before this return in program order,
         * so recomputing from it here would restore x19 from the wrong slot
         * (and mis-size the frame) for any early return. */
        a64_emit(a, a64_LDR(A64_X19, A64_FP, cg->x19_slot));
        int frame = cg->frame_size;
        if (frame <= A64_FRAME_SPLIT) {
            a64_emit(a, a64_LDP_post(A64_FP, A64_LR, A64_SP, frame));
        } else {
            a64_emit(a, a64_LDP_post(A64_FP, A64_LR, A64_SP, A64_FRAME_SPLIT));
            a64_sp_adjust(a, A64_SP, A64_SP, frame - A64_FRAME_SPLIT, 0);
        }
        a64_emit(a, a64_RET());
        break;
    }

    case AST_VAR_DECL: {
        ASTNode *nd = n;
        int is_static = (nd->var_decl.storage && strcmp(nd->var_decl.storage, "static") == 0);
        if (is_static) {
            /* Static locals live in .data, not on the stack, and must keep
             * their value across calls. Mirrors codegen.c's x86 handling:
             * allocate a uniquely-named zero-filled wdata slot, register a
             * SYM_GLOBAL under the variable's own name so later references
             * resolve normally, and skip inline init — C requires a static
             * local's initializer to run once at program start, not on
             * every call, and the wdata pool is already zero-initialised
             * (matches "= 0", which is the only case squash programs use). */
            int arr = nd->var_decl.array_size;
            /* a64_sizeof_type_sym_ex, NOT typeinfo_size_a64 -- same fix as
             * elsewhere in this file. ignore_array=1 since `arr` here
             * already tracks the element count separately (multiplied in
             * below) -- using the plain a64_sizeof_type_sym wrapper
             * (ignore_array=0) would double-count it if the type itself
             * also carried a nonzero array_size. */
            int elem_sz = nd->var_decl.type ? a64_sizeof_type_sym_ex(cg->sym, nd->var_decl.type, 1, 0, 1) : 4;
            if (elem_sz < 1) elem_sz = 4;
            int total_sz = (arr > 0) ? elem_sz * arr : elem_sz;

            char lbl[256];
            snprintf(lbl, sizeof lbl, "static_%s_%d", nd->var_decl.name, cg->static_var_counter++);
            a64_alloc_wdata(cg, total_sz, lbl);

            TypeInfo *ti = nd->var_decl.type ? nd->var_decl.type : typeinfo_new("int");
            symtable_define_global(cg->sym, nd->var_decl.name, ti, arr);
            Symbol *s = symtable_lookup(cg->sym, nd->var_decl.name);
            if (s) { free(s->dll); s->dll = my_strdup(lbl); }

            /* ANY non-zero initializer (not just a non-constant one like a
             * string-literal pointer -- this ARM64 backend, unlike the x86
             * one just above build_static_local_init_bytes(), never bakes
             * compile-time-constant initializer bytes into the wdata slot
             * either) was previously silently discarded here -- the
             * comment above admits it outright ("matches '= 0', which is
             * the only case squash programs use"). Confirmed as the exact
             * same real bug class as x86's (a minimal "static const char
             * *s = "hi";" reads back NULL under this backend too, for the
             * identical underlying reason: nothing ever emits code to
             * store the initializer anywhere). Fixed the same way x86's
             * "is_static" branch now is: synthesize a real "if (!guard) {
             * guard = 1; <assignment(s)> }" AST and run it through this
             * backend's own ordinary if/assignment codegen (a64_stmt) --
             * see codegen.c's identical fix for the full reasoning
             * (guard flag because a local static's declaration statement
             * genuinely re-executes every call, brace-list expansion to
             * one assignment per flat element since C doesn't allow
             * assigning a whole array by name). No struct-of-arrays/
             * array-of-struct expansion here either, same scope limit. */
            if (nd->var_decl.init) {
                char guard_lbl[300];
                snprintf(guard_lbl, sizeof guard_lbl, "%s_init_done", lbl);
                a64_alloc_wdata(cg, 4, guard_lbl);
                TypeInfo *guard_ti = typeinfo_new("int");
                symtable_define_global(cg->sym, guard_lbl, guard_ti, -1);
                Symbol *guard_sym = symtable_lookup(cg->sym, guard_lbl);
                if (guard_sym) { free(guard_sym->dll); guard_sym->dll = my_strdup(guard_lbl); }

                ASTNode *init_node = nd->var_decl.init;
                nd->var_decl.init = NULL; /* ownership moves to the synthesized assignment(s) below */

                ASTNode *then_stmts[257];
                int n_then = 0;
                then_stmts[n_then++] = ast_expr_stmt(
                    ast_assign("=", ast_var(guard_lbl, nd->line), ast_number(1, nd->line), nd->line), nd->line);
                if (init_node->kind == AST_BLOCK && init_node->block.count > 0 && init_node->block.count <= 256) {
                    for (int ei = 0; ei < init_node->block.count; ei++) {
                        ASTNode *elem = init_node->block.stmts[ei];
                        if (!elem || elem->kind == AST_BLOCK) continue;
                        ASTNode *elhs = ast_index(ast_var(nd->var_decl.name, nd->line), ast_number(ei, nd->line), nd->line);
                        then_stmts[n_then++] = ast_expr_stmt(ast_assign("=", elhs, elem, nd->line), nd->line);
                        init_node->block.stmts[ei] = NULL;
                    }
                    ast_free(init_node);
                } else if (init_node->kind == AST_STRING && arr > 0) {
                    /* "char x[] = \"abc\";" -- see codegen.c's identical
                     * fix for the full reasoning (a real array target
                     * can't be assigned a whole string via "="). */
                    const char *sv = init_node->str.value ? init_node->str.value : "";
                    int slen = (int)strlen(sv);
                    int ncopy = slen + 1;
                    if (ncopy > arr) ncopy = arr;
                    if (ncopy > 256) ncopy = 256;
                    for (int ci = 0; ci < ncopy; ci++) {
                        ASTNode *clhs = ast_index(ast_var(nd->var_decl.name, nd->line), ast_number(ci, nd->line), nd->line);
                        ASTNode *chval = ast_char_lit(ci < slen ? (unsigned char)sv[ci] : 0, nd->line);
                        then_stmts[n_then++] = ast_expr_stmt(ast_assign("=", clhs, chval, nd->line), nd->line);
                    }
                    ast_free(init_node);
                } else {
                    ASTNode *slhs = ast_var(nd->var_decl.name, nd->line);
                    then_stmts[n_then++] = ast_expr_stmt(ast_assign("=", slhs, init_node, nd->line), nd->line);
                }
                ASTNode *then_block = ast_block(then_stmts, n_then, nd->line);
                ASTNode *cond = ast_unary("!", ast_var(guard_lbl, nd->line), 0, nd->line);
                ASTNode *guarded_if = ast_if(cond, then_block, NULL, nd->line);
                a64_stmt(cg, guarded_if);
                /* Deliberately not ast_free()'d -- see codegen.c's
                 * identical fix for why (a64_stmt() only reads the tree
                 * to emit code, never retains it; a small one-time
                 * compile-time leak is the safe choice over risking a
                 * use-after-free in a freshly written synthesis path). */
            }
            break;
        }
        /* Allocate local — symtable tracks next_offset negatively */
        if (!nd->var_decl.storage || strcmp(nd->var_decl.storage, "extern") != 0) {
            Symbol *s = symtable_define_var(cg->sym, nd->var_decl.name,
                                            nd->var_decl.type);
            /* After definition, next_offset has decreased.
             * Update cur_local_off to reflect total local usage.
             * sym->offset is negative; local_off = -sym->offset accounts for it. */
            int used = -cg->sym->next_offset;
            if (used > cg->cur_local_off) cg->cur_local_off = used;

            /* Initializer */
            if (nd->var_decl.init) {
                int arr = nd->var_decl.array_size;
                if (nd->var_decl.init->kind == AST_BLOCK && arr > 0) {
                    /* Local array brace-initializer (e.g. "const char
                     * *exts[2] = {\"a\",\"b\"};" or a Vulkan-style
                     * "Vertex verts[3] = {{...},{...},{...}};") — this
                     * codegen previously had NO handling for this at all:
                     * it fell into the plain-scalar branch below, which
                     * evaluated the whole AST_BLOCK as one expression
                     * (yielding garbage/0 in x0) and stored that single
                     * value into just the array's first slot, leaving
                     * every element uninitialized (reproduced directly: a
                     * real Vulkan triangle demo's ppEnabledExtensionNames
                     * pointer array read back as NULL and the loader
                     * segfaulted on strlen(NULL)). Desugar into ordinary
                     * per-element assignment statements instead — the
                     * fastest way to piggyback on a64_stmt/a64_expr's
                     * already-correct AST_ASSIGN/AST_MEMBER/AST_INDEX
                     * codegen (including the float/double width fix
                     * above) rather than re-deriving raw store widths
                     * here. Mirrors codegen.c's identical x86-64 fix for
                     * the same construct (see its own comment for why the
                     * struct-array case must be detected FIRST: the
                     * parser flattens nested per-element "{...}" braces
                     * into one flat scalar list, so block.count is
                     * nfields*N, not N, for an array of structs). */
                    const char *base = nd->var_decl.type ? nd->var_decl.type->base : NULL;
                    ASTNode *esd = base ? a64_find_struct_def(cg, base) : NULL;
                    ASTNode *initblk = nd->var_decl.init;
                    if (esd && esd->struct_decl.nfields > 0) {
                        int nf = esd->struct_decl.nfields;
                        int per_elem_flat = 0;
                        for (int fi = 0; fi < nf; fi++) {
                            ASTNode *ff0 = esd->struct_decl.fields[fi];
                            per_elem_flat += (ff0 && ff0->kind == AST_FIELD && ff0->field.array_size > 0) ? ff0->field.array_size : 1;
                        }
                        if (per_elem_flat < 1) per_elem_flat = nf;
                        int true_n = initblk->block.count / per_elem_flat;
                        if (true_n > arr) true_n = arr;
                        int flat = 0;
                        for (int idx = 0; idx < true_n && flat < initblk->block.count; idx++) {
                            for (int fi = 0; fi < nf && flat < initblk->block.count; fi++) {
                                ASTNode *ff = esd->struct_decl.fields[fi];
                                if (!ff || ff->kind != AST_FIELD || !ff->field.name) { flat++; continue; }
                                if (ff->field.array_size > 0) {
                                    for (int ei = 0; ei < ff->field.array_size && flat < initblk->block.count; ei++, flat++) {
                                        ASTNode *sub = initblk->block.stmts[flat];
                                        if (!sub || sub->kind == AST_BLOCK) continue;
                                        ASTNode *lhs = ast_index(
                                            ast_member(ast_index(ast_var(nd->var_decl.name, n->line), ast_number(idx, n->line), n->line),
                                                       ff->field.name, 0, n->line),
                                            ast_number(ei, n->line), n->line);
                                        a64_stmt(cg, ast_expr_stmt(ast_assign("=", lhs, sub, n->line), n->line));
                                    }
                                    continue;
                                }
                                ASTNode *sub = initblk->block.stmts[flat];
                                if (!sub || sub->kind == AST_BLOCK) { flat++; continue; }
                                ASTNode *lhs = ast_member(
                                    ast_index(ast_var(nd->var_decl.name, n->line), ast_number(idx, n->line), n->line),
                                    ff->field.name, 0, n->line);
                                a64_stmt(cg, ast_expr_stmt(ast_assign("=", lhs, sub, n->line), n->line));
                                flat++;
                            }
                        }
                    } else {
                        for (int i = 0; i < initblk->block.count && i < arr; i++) {
                            ASTNode *sub = initblk->block.stmts[i];
                            if (!sub) continue;
                            ASTNode *lhs = ast_index(ast_var(nd->var_decl.name, n->line), ast_number(i, n->line), n->line);
                            a64_stmt(cg, ast_expr_stmt(ast_assign("=", lhs, sub, n->line), n->line));
                        }
                    }
                } else if (a64_is_float(cg, nd->var_decl.init)) {
                    a64_float_expr(cg, nd->var_decl.init);
                    int fp_off = a64_sym_fp_off(s);
                    a64_emit(a, a64_FSTR(A64_D0, A64_FP, fp_off));
                } else {
                    a64_expr(cg, nd->var_decl.init);
                    a64_store_to_sym(cg, s, A64_X0);
                }
            }
        }
        break;
    }

    case AST_BLOCK: {
        int i;
        /* Multi-declarator statements like "int a=1, *b=0;" are parsed as an
         * AST_BLOCK of AST_VAR_DECL children purely to group them — they are
         * NOT a nested lexical scope, so pushing/popping one here would free
         * their symbols the instant this "statement" finishes, making them
         * unreachable for the rest of the enclosing block. Matches codegen.c's
         * x86 handling of the same parser output. */
        int all_var_decl = (n->block.count > 0);
        for (i = 0; i < n->block.count && all_var_decl; i++)
            if (!n->block.stmts[i] || n->block.stmts[i]->kind != AST_VAR_DECL)
                all_var_decl = 0;
        if (all_var_decl) {
            for (i = 0; i < n->block.count; i++) a64_stmt(cg, n->block.stmts[i]);
        } else {
            symtable_push_scope(cg->sym);
            for (i = 0; i < n->block.count; i++)
                a64_stmt(cg, n->block.stmts[i]);
            symtable_pop_scope(cg->sym);
        }
        break;
    }

    case AST_IF: {
        int else_lbl = a64_new_label(a, "");
        int end_lbl  = a64_new_label(a, "");
        a64_expr(cg, n->if_.cond);
        a64_emit(a, a64_CMP_imm(A64_X0, 0));
        int p1 = a->code_len;
        a64_emit(a, a64_Bcond(A64_EQ, 0));
        a64_add_fixup(a, p1, else_lbl);
        a64_stmt(cg, n->if_.then_);
        int p2 = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p2, end_lbl);
        a64_def_label(a, else_lbl);
        if (n->if_.else_) a64_stmt(cg, n->if_.else_);
        a64_def_label(a, end_lbl);
        /* Deliberately NOT calling a64_resolve() here (removed — was
         * present in every control-flow case in this switch, plus "&&"/
         * "||"/ternary above). a64_resolve() iterates and attempts to
         * patch EVERY outstanding fixup in the whole function so far, not
         * just the ones this specific construct just created — calling it
         * before the function body is fully generated is only safe by
         * coincidence (it works whenever nothing else in the function
         * still has an undefined label at this point) and actively wrong
         * otherwise. Confirmed via a minimal repro: "if (a || b) { x ? y :
         * z; }" — the nested "||"'s own (then-present) a64_resolve() call
         * ran while THIS if-statement's own else/end labels were still
         * undefined (we're still inside its then-branch), printing a
         * spurious "arm64: unresolved label id N" for them. The fixup
         * itself was never lost (a64_resolve doesn't clear/consume the
         * fixup array), so the actual generated code was still correct
         * once the SINGLE authoritative a64_resolve() call after the
         * whole function body (see a64_gen_function) ran — this was pure
         * cosmetic noise, but noise that could mask a genuine unresolved-
         * label bug and needlessly alarmed anyone reading build output.
         * Every control-flow case's own label is guaranteed defined by
         * the time that one real end-of-function call runs, so none of
         * these intermediate calls were ever necessary in the first
         * place. */
        break;
    }

    case AST_WHILE: {
        int top_lbl = a64_new_label(a, "");
        int end_lbl = a64_new_label(a, "");
        a64_def_label(a, top_lbl);
        a64_expr(cg, n->while_.cond);
        a64_emit(a, a64_CMP_imm(A64_X0, 0));
        int p = a->code_len;
        a64_emit(a, a64_Bcond(A64_EQ, 0));
        a64_add_fixup(a, p, end_lbl);

        int saved_end  = cg->loop_end_label;
        int saved_top  = cg->loop_top_label;
        cg->loop_end_label = end_lbl;
        cg->loop_top_label = top_lbl;
        a64_stmt(cg, n->while_.body);
        cg->loop_end_label = saved_end;
        cg->loop_top_label = saved_top;

        int p2 = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p2, top_lbl);
        a64_def_label(a, end_lbl);
        /* No a64_resolve() here — see AST_IF's removal comment. */
        break;
    }

    case AST_DO_WHILE: {
        int top_lbl = a64_new_label(a, "");
        int end_lbl = a64_new_label(a, "");
        a64_def_label(a, top_lbl);

        int saved_end = cg->loop_end_label;
        int saved_top = cg->loop_top_label;
        cg->loop_end_label = end_lbl;
        cg->loop_top_label = top_lbl;
        a64_stmt(cg, n->do_while.body);
        cg->loop_end_label = saved_end;
        cg->loop_top_label = saved_top;

        a64_expr(cg, n->do_while.cond);
        a64_emit(a, a64_CMP_imm(A64_X0, 0));
        int p = a->code_len;
        a64_emit(a, a64_Bcond(A64_NE, 0));
        a64_add_fixup(a, p, top_lbl);
        a64_def_label(a, end_lbl);
        /* No a64_resolve() here — see AST_IF's removal comment. */
        break;
    }

    case AST_FOR: {
        int top_lbl  = a64_new_label(a, "");
        int cont_lbl = a64_new_label(a, "");
        int end_lbl  = a64_new_label(a, "");
        symtable_push_scope(cg->sym);
        if (n->for_.init) a64_stmt(cg, n->for_.init);
        a64_def_label(a, top_lbl);
        if (n->for_.cond) {
            a64_expr(cg, n->for_.cond);
            a64_emit(a, a64_CMP_imm(A64_X0, 0));
            int p = a->code_len;
            a64_emit(a, a64_Bcond(A64_EQ, 0));
            a64_add_fixup(a, p, end_lbl);
        }
        int saved_end  = cg->loop_end_label;
        int saved_top  = cg->loop_top_label;
        cg->loop_end_label = end_lbl;
        cg->loop_top_label = cont_lbl;
        a64_stmt(cg, n->for_.body);
        cg->loop_end_label = saved_end;
        cg->loop_top_label = saved_top;

        a64_def_label(a, cont_lbl);
        if (n->for_.step) a64_expr(cg, n->for_.step);
        int p2 = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p2, top_lbl);
        a64_def_label(a, end_lbl);
        /* No a64_resolve() here — see AST_IF's removal comment. */
        symtable_pop_scope(cg->sym);
        break;
    }

    case AST_BREAK:
        if (cg->loop_end_label >= 0) {
            int p = a->code_len;
            a64_emit(a, a64_B(0));
            a64_add_fixup(a, p, cg->loop_end_label);
        }
        break;

    case AST_CONTINUE:
        if (cg->loop_top_label >= 0) {
            int p = a->code_len;
            a64_emit(a, a64_B(0));
            a64_add_fixup(a, p, cg->loop_top_label);
        }
        break;

    case AST_SWITCH: {
        /* Previous version emitted each case's Bcond target label immediately
         * after the branch itself, i.e. the very next instruction — meaning
         * the comparison was a no-op (branch taken or not, control landed at
         * the same place) and every case body ran unconditionally in source
         * order, with `break` a no-op outside a loop. Correct approach
         * (mirrors codegen.c's x86 backend): emit ALL comparisons first,
         * each jumping to its own case label; only then emit the bodies
         * sequentially with natural C fall-through between cases. */
        int end_lbl = a64_new_label(a, "");
        int saved_switch   = cg->switch_end_label;
        int saved_loop_end = cg->loop_end_label;
        cg->switch_end_label = end_lbl;
        cg->loop_end_label   = end_lbl; /* break targets the switch, like x86 */

        a64_expr(cg, n->switch_.expr);
        a64_emit(a, a64_MOV(A64_X19, A64_X0));   /* save switch value in x19 */

        int nc = n->switch_.nc;
        int *case_labels = (int*)malloc(nc * sizeof(int));
        int default_idx = -1;
        int i;
        for (i = 0; i < nc; i++) {
            case_labels[i] = a64_new_label(a, "");
            ASTNode *c = n->switch_.cases[i];
            if (c && c->kind == AST_DEFAULT) default_idx = i;
        }
        for (i = 0; i < nc; i++) {
            ASTNode *c = n->switch_.cases[i];
            if (c && c->kind == AST_CASE) {
                a64_load_imm(cg, A64_X0, c->case_.value);
                a64_emit(a, a64_CMP(A64_X19, A64_X0));
                int p = a->code_len;
                a64_emit(a, a64_Bcond(A64_EQ, 0));
                a64_add_fixup(a, p, case_labels[i]);
            }
        }
        {
            int p = a->code_len;
            a64_emit(a, a64_B(0));
            a64_add_fixup(a, p, default_idx >= 0 ? case_labels[default_idx] : end_lbl);
        }
        for (i = 0; i < nc; i++) {
            a64_def_label(a, case_labels[i]);
            ASTNode *c = n->switch_.cases[i];
            if (!c) continue;
            int j;
            if (c->kind == AST_DEFAULT) {
                for (j = 0; j < c->default_.nb; j++) a64_stmt(cg, c->default_.body[j]);
            } else {
                for (j = 0; j < c->case_.nb; j++) a64_stmt(cg, c->case_.body[j]);
            }
        }
        free(case_labels);
        a64_def_label(a, end_lbl);
        /* No a64_resolve() here — see AST_IF's removal comment. */
        cg->switch_end_label = saved_switch;
        cg->loop_end_label   = saved_loop_end;
        break;
    }

    case AST_LABEL: {
        int lbl_id = a64_find_label(a, n->label.name);
        if (lbl_id < 0) lbl_id = a64_new_label(a, n->label.name);
        a64_def_label(a, lbl_id);
        if (n->label.stmt) a64_stmt(cg, n->label.stmt);
        /* No a64_resolve() here — see AST_IF's removal comment. A `goto`
         * to this label from EARLIER in the function (a backward jump)
         * already has a valid target the moment a64_def_label() above
         * runs; a `goto` from LATER (forward jump, not yet emitted) can't
         * be resolved yet regardless — either way, the single end-of-
         * function a64_resolve() call correctly covers both. */
        break;
    }

    case AST_GOTO: {
        int lbl_id = a64_find_label(a, n->goto_.label);
        if (lbl_id < 0) lbl_id = a64_new_label(a, n->goto_.label);
        int p = a->code_len;
        a64_emit(a, a64_B(0));
        a64_add_fixup(a, p, lbl_id);
        break;
    }

    case AST_FUNC_DECL:
        /* nested function declarations: skip */
        break;

    case AST_ASM_STMT:
        codegen_asm_a64(cg, n);
        break;

    default:
        break;
    }
}

/* =========================================================================
 * Pre-scan function body to estimate total local bytes needed
 * (so the prologue can be emitted with the right frame size)
 * ========================================================================= */
static int scan_local_bytes(SymTable *sym, ASTNode *n) {
    if (!n) return 0;
    int total = 0;
    if (n->kind == AST_VAR_DECL) {
        if (!n->var_decl.storage || strcmp(n->var_decl.storage, "extern") != 0) {
            if (!n->var_decl.storage || strcmp(n->var_decl.storage, "static") != 0) {
                int sz = 8; /* minimum 8 bytes per slot (16-byte aligned) */
                if (n->var_decl.type) {
                    int base_sz = a64_sizeof_type_sym(sym, n->var_decl.type, 1);
                    sz = (base_sz + 15) & ~15;
                    if (sz < 16) sz = 16;
                }
                total += sz;
            }
        }
    }
    /* Recurse into children */
    int i;
    switch (n->kind) {
    case AST_BLOCK:
        for (i = 0; i < n->block.count; i++) total += scan_local_bytes(sym, n->block.stmts[i]);
        break;
    case AST_IF:
        total += scan_local_bytes(sym, n->if_.then_);
        total += scan_local_bytes(sym, n->if_.else_);
        break;
    case AST_WHILE:  total += scan_local_bytes(sym, n->while_.body); break;
    case AST_DO_WHILE: total += scan_local_bytes(sym, n->do_while.body); break;
    case AST_FOR:
        total += scan_local_bytes(sym, n->for_.init);
        total += scan_local_bytes(sym, n->for_.body);
        break;
    case AST_SWITCH: {
        for (i = 0; i < n->switch_.nc; i++) total += scan_local_bytes(sym, n->switch_.cases[i]);
        break;
    }
    case AST_CASE: {
        for (i = 0; i < n->case_.nb; i++) total += scan_local_bytes(sym, n->case_.body[i]);
        break;
    }
    case AST_LABEL: total += scan_local_bytes(sym, n->label.stmt); break;
    default: break;
    }
    return total;
}

/* =========================================================================
 * Function code generator
 * ========================================================================= */
static void a64_codegen_func(CodeGenA64 *cg, ASTNode *n) {
    Arm64Asm *a = cg->asm_;
    if (!n || n->kind != AST_FUNC_DECL || !n->func.body) return;

    /* Define function label */
    int lbl_id = a64_find_label(a, n->func.name);
    if (lbl_id < 0) lbl_id = a64_new_label(a, n->func.name);
    a64_def_label(a, lbl_id);

    cg->cur_func_is_float_ret = a64_is_float_type(cg->sym, n->func.ret_type);

    /* Set up symbol table scope */
    symtable_push_scope(cg->sym);
    symtable_reset_locals(cg->sym);
    cg->sym->next_offset = 0;

    /* Pre-scan to estimate locals (for prologue frame sizing).
     * Variadic functions (e.g. this project's own include/stdio.h
     * __squash_fprintf_impl) need every not-otherwise-named incoming
     * integer register argument (up to AAPCS64's x0-x7) spilled to the
     * stack too, tightly 8-byte-packed and contiguous with the named
     * params, so stdarg.h's va_start/va_arg — which just walk memory
     * downward from &lastNamedParam in fixed 8-byte steps, with no idea
     * where any particular register landed — read real spilled values
     * instead of stale stack contents. The normal 16-byte-per-param
     * stride used below for non-variadic functions leaves only 8 bytes of
     * unused padding after each param, nowhere near enough contiguous
     * room for that walk, so variadic functions use a dedicated 8-byte
     * stride for ALL of their params (named + spilled-extra) instead —
     * see the offset assignment right below and the extra-register spill
     * after the param loop. */
    int va_extra_regs = 0;
    if (n->func.is_variadic) {
        int va_ireg_named = 0;
        for (int pi = 0; pi < n->func.paramc; pi++) {
            ASTNode *pr = n->func.params[pi];
            if (!a64_is_float_type(cg->sym, pr->param.type)) va_ireg_named++;
        }
        if (va_ireg_named < 8) va_extra_regs = 8 - va_ireg_named;
    }
    /* +16 below: offsets [x29+0, x29+16) are the STP-pre save slot for the
     * caller's FP/LR (see the STP-pre emitted further down) — off limits
     * to any param/local, so the variadic 8-byte-packed region must start
     * at x29+16 just like the non-variadic 16-byte-strided one already
     * implicitly does by using (i+1)*16 (i.e. never emitting an offset
     * below 16 either). Skipping this reservation here previously let the
     * lowest 1-2 spilled variadic registers land ON TOP OF the saved
     * return address, corrupting it (reproduced directly: a variadic
     * function's fallthrough epilogue's RET jumped to a near-zero garbage
     * address). */
    int param_slots = n->func.is_variadic
        ? 16 + (n->func.paramc + va_extra_regs) * 8
        : n->func.paramc * 16;  /* 16 bytes per param (aligned) */
    int body_locals = scan_local_bytes(cg->sym, n->func.body);
    /* +16 for x19 save slot + 8 alignment */
    int total_locals = param_slots + body_locals + 16;

    /* Frame size = 16 (FP/LR) + total_locals + temp_space, aligned to 16 */
    int temp_space = A64_MAX_TEMP_DEPTH * 8;
    int frame = (16 + total_locals + temp_space + 15) & ~15;
    if (frame < 32) frame = 32;

    /* Clamp to a chunk representable by both STP-pre and LDP-post in one
     * instruction (see A64_FRAME_SPLIT) */
    int stp_frame = frame <= A64_FRAME_SPLIT ? frame : A64_FRAME_SPLIT;

    /* Emit prologue. The extra SUB (for frame > A64_FRAME_SPLIT) must happen
     * BEFORE the STP-pre / FP setup: locals are addressed as [FP + positive
     * offset], so FP must end up pointing at the true bottom of the whole
     * frame. Doing the extra SUB first, then STP-pre (which lowers SP by
     * stp_frame and stores old FP/LR at the new, final bottom), then
     * FP=SP achieves that. Emitting the extra SUB after STP instead (as SP
     * for the FP/LR save, then a further reduction) would leave FP pointing
     * partway up the frame, so any local at offset > stp_frame would
     * resolve outside the allocated region. */
    if (frame > A64_FRAME_SPLIT) {
        cg->sub_patch_off = a->code_len;
        a64_sp_adjust(a, A64_SP, A64_SP, frame - A64_FRAME_SPLIT, 1);
    } else {
        cg->sub_patch_off = -1;
    }
    cg->stp_patch_off = a->code_len;
    a64_emit(a, a64_STP_pre(A64_FP, A64_LR, A64_SP, -stp_frame));
    a64_emit(a, a64_ADD_imm(A64_FP, A64_SP, 0));   /* x29 = sp (MOV register form can't source SP) */
    /* Save x19 (callee-saved scratch used for temp store operations) */
    /* Store at [x29 + 16 + total_locals - 16] — last slot before temps */
    int x19_slot = 16 + total_locals - 16;  /* offset from x29 */
    if (x19_slot < 16) x19_slot = 16;
    a64_emit(a, a64_STR(A64_X19, A64_FP, x19_slot));
    /* Fixed once here; every epilogue (early return or fallthrough) must
     * reuse these exact values rather than recompute them from cur_local_off,
     * which only reflects locals declared before that point in the function. */
    cg->x19_slot   = x19_slot;
    cg->frame_size = frame;

    /* Initialize context */
    cg->cur_local_off   = 0;
    cg->cur_temp_depth  = 0;
    cg->max_temp_depth  = 0;
    cg->cur_func_has_return = 0;
    cg->loop_end_label  = -1;
    cg->loop_top_label  = -1;
    cg->switch_end_label = -1;

    /* Define parameters in symtable with ARM64 offsets.
     * AAPCS64 uses two independent register banks: integer/pointer args in
     * x0-x7, float/double args in d0-d7 — e.g. f(int a, double b, int c)
     * passes a in x0, b in d0, c in x1 (NOT x1 for b). Each parameter's
     * OWN declared type determines which bank and index it comes from,
     * tracked with separate counters. */
    int i;
    int ireg = 0, freg = 0;
    for (i = 0; i < n->func.paramc; i++) {
        ASTNode *pr = n->func.params[i];
        if (!pr->param.name) continue;
        Symbol *s = symtable_define_param(cg->sym, pr->param.name,
                                          pr->param.type, i, 0);
        /* Non-variadic: param i at [x29 + (i+1)*16] (16-byte stride, matches
         * every other param/local offset in this file). Variadic: [x29 +
         * param_slots - (i+1)*8] instead — packed tightly at 8-byte stride
         * with no gaps, so the extra-register spill emitted right after
         * this loop (and stdarg.h's va_start/va_arg, which just walk
         * memory downward from &lastNamedParam in fixed 8-byte steps) land
         * on real contiguous slots instead of the 8-byte padding gap a
         * 16-byte stride would otherwise leave after every param — see
         * this function's param_slots/va_extra_regs comment above. */
        int pos_off = n->func.is_variadic ? (param_slots - (i + 1) * 8) : (i + 1) * 16;
        s->offset = -pos_off;
        if (a64_is_float_type(cg->sym, pr->param.type)) {
            if (freg < 8) a64_emit(a, a64_FSTR(freg, A64_FP, pos_off));
            freg++;
        } else {
            if (ireg < 8) a64_emit(a, a64_STR(ireg, A64_FP, pos_off));
            ireg++;
        }
    }
    /* Variadic: spill every remaining incoming integer argument register
     * (up to AAPCS64's x0-x7) not already spilled above as a named
     * parameter, continuing the exact same 8-byte-descending sequence the
     * named params above just used — see this function's param_slots
     * comment for the full rationale. Only integer/pointer varargs are
     * supported (no floating-point varargs), matching codegen.c's
     * identical x86-64 SysV limitation and this project's own actual
     * variadic call sites (printf-style %d/%p/%s diagnostics). */
    if (n->func.is_variadic) {
        for (int k = 0; k < va_extra_regs; k++) {
            int r = ireg + k;
            if (r >= 8) break;
            int pos_off = param_slots - (n->func.paramc + k + 1) * 8;
            a64_emit(a, a64_STR(r, A64_FP, pos_off));
        }
    }
    /* Ensure next_offset starts past param slots */
    if (param_slots > 0)
        cg->sym->next_offset = -param_slots;
    cg->cur_local_off = param_slots;

    /* Temp base is at [x29 + 16 + total_locals] */
    cg->temp_base = 16 + total_locals;

    /* Emit function body */
    a64_stmt(cg, n->func.body);

    /* Deliberately NOT calling a64_resolve() here (removed). A call to a
     * function defined LATER in the file (e.g. this project's own test
     * suite calling "fwd_is_even"/"fwd_sum_range" long before their
     * definitions) creates a fixup whose target label genuinely isn't
     * defined yet at this point — every function's a64_labels/a64_fixups
     * arrays are shared across the WHOLE file (one Arm64Asm context, see
     * a64_codegen_init), but a64_resolve() patches machine code
     * immediately based on whatever the label's offset is AT THE TIME OF
     * THE CALL and never revisits a fixup later — so calling it here,
     * once per function, printed a spurious (but for locally-resolvable
     * labels, harmless) "unresolved label" for every forward reference,
     * and — worse — for a TRUE cross-function forward reference, this was
     * the ONLY resolve call that would ever see that particular fixup
     * before the whole-program-scope one, permanently leaving the branch
     * target as whatever placeholder bytes were originally emitted. A
     * SINGLE authoritative a64_resolve() call now runs once, in
     * a64_codegen_program(), after every function in the file has been
     * compiled — by then every label (including forward-referenced ones)
     * is guaranteed defined. */

    /* Always emit a fallthrough epilogue at the end of the function body,
     * regardless of whether some earlier statement already contained a
     * `return`. cur_func_has_return only records that a return statement
     * was SEEN somewhere while walking the body — it says nothing about
     * whether that return is on every path. A `return` nested inside an
     * `if` (with no matching `else`) leaves the fallthrough path (the
     * condition being false) reachable and still needing an epilogue;
     * skipping it here left that path falling off the end of this
     * function's machine code with no LDP/RET at all, straight into
     * whatever function happens to follow in .text (observed as calls
     * mysteriously "restarting" an unrelated, later-defined function).
     * Emitting it unconditionally is always safe: when every path really
     * does return explicitly, this block is simply unreachable dead code. */
    a64_emit(a, a64_MOV(A64_X0, A64_XZR));
    a64_emit(a, a64_LDR(A64_X19, A64_FP, x19_slot));
    if (frame <= A64_FRAME_SPLIT) {
        a64_emit(a, a64_LDP_post(A64_FP, A64_LR, A64_SP, stp_frame));
    } else {
        a64_emit(a, a64_LDP_post(A64_FP, A64_LR, A64_SP, A64_FRAME_SPLIT));
        a64_sp_adjust(a, A64_SP, A64_SP, frame - A64_FRAME_SPLIT, 0);
    }
    a64_emit(a, a64_RET());

    symtable_pop_scope(cg->sym);
}

/* =========================================================================
 * Global variable handler
 * ========================================================================= */
static void a64_codegen_global(CodeGenA64 *cg, ASTNode *n) {
    if (!n || n->kind != AST_VAR_DECL) return;
    /* Allocate in wdata pool */
    TypeInfo *t = n->var_decl.type;
    /* a64_sizeof_type_sym, NOT typeinfo_size_a64 -- the latter has no
     * symbol-table access (see its own doc comment at AST_SIZEOF_TYPE
     * above) and silently falls back to a generic "is64?8:4" default for
     * any struct/typedef name it doesn't recognize as a builtin. For a
     * global struct variable that meant reserving only 8 bytes of wdata
     * space for the WHOLE struct regardless of its real size, so the next
     * global declared after it landed PARTWAY INSIDE the first one's true
     * memory footprint -- reproduced directly: two globals, "struct
     * AppInfo g_appInfo;" (real size 48) followed by "struct CreateInfo
     * g_createInfo;", placed g_createInfo starting at g_appInfo+8, i.e.
     * exactly overlapping g_appInfo's own second field (pNext) -- so
     * g_createInfo.sType=1 silently overwrote g_appInfo.pNext with 1,
     * corrupting a value that had already been explicitly set to 0/NULL
     * several statements earlier with no visible relation to the actual
     * bug. Exactly the same bug class already fixed at AST_SIZEOF_TYPE
     * above and documented at the 2D-array (array_size2) case just below
     * -- this call site was simply never updated to match. */
    /* a64_sizeof_type_sym_ex (which the 2-arg wrapper below calls with
     * ignore_array=0) already multiplies by array_size and array_size2
     * itself -- see its own array-handling block -- so no additional
     * multiplication belongs here. (An earlier version of this function
     * had to do that multiplication by hand, one dimension at a time, and
     * had a bug where 2D arrays' second dimension was silently dropped —
     * see git history/this file's other comments for that incident — but
     * routing through the shared, already-correct helper below makes
     * that entire class of bug impossible to reintroduce here.) */
    int sz = a64_sizeof_type_sym(cg->sym, t, 1);
    if (sz < 1) sz = 1;
    a64_alloc_wdata(cg, sz, n->var_decl.name);
    /* Define in symtable as global */
    symtable_define_global(cg->sym, n->var_decl.name, t, t ? t->array_size : 0);
}

/* =========================================================================
 * Program code generator
 * ========================================================================= */
void a64_codegen_program(CodeGenA64 *cg, ASTNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;
    int i;
    /* Pass 0: globals */
    for (i = 0; i < prog->program.count; i++) {
        ASTNode *d = prog->program.decls[i];
        if (!d) continue;
        if (d->kind == AST_VAR_DECL)
            a64_codegen_global(cg, d);
    }
    /* Pass 1: functions */
    for (i = 0; i < prog->program.count; i++) {
        ASTNode *d = prog->program.decls[i];
        if (!d) continue;
        if (d->kind == AST_FUNC_DECL && d->func.body)
            a64_codegen_func(cg, d);
    }
    /* Final, whole-PROGRAM fixup resolve. `cg->asm_`'s labels/fixups
     * arrays are shared across every function (one Arm64Asm context for
     * the whole file, per a64_codegen_init), so a call to a function
     * defined LATER in the file (a real, common pattern — e.g. this
     * project's own test suite calls "fwd_is_even"/"fwd_sum_range" well
     * before their definitions) creates a fixup that's still unresolved
     * when the CALLING function's own end-of-function a64_resolve() call
     * runs (that function is compiled before the callee, so the callee's
     * label doesn't exist yet). a64_resolve() patches machine code
     * in-place immediately based on whatever the label's offset is AT
     * THE TIME OF THE CALL — it never revisits a fixup later once the
     * label eventually becomes valid, so without this final pass such a
     * call's branch/BL target was silently left as whatever placeholder
     * bytes were emitted originally (0), a genuine wrong-target bug, not
     * just the cosmetic "unresolved label" print output. This call is
     * purely additive/idempotent for every fixup ALREADY correctly
     * patched by its own function's resolve() call (same target offset,
     * re-written to the same bytes) — only fixups that were genuinely
     * still-pending at their own function's resolve time get a chance to
     * be correctly patched here. */
    a64_resolve(cg->asm_);
}

/* =========================================================================
 * Init / output accessors
 * ========================================================================= */
void a64_codegen_init(CodeGenA64 *cg, Arm64Asm *a, SymTable *sym, int is_linux, LinkerContext *linker, int prefer_static_calls) {
    memset(cg, 0, sizeof *cg);
    cg->asm_      = a;
    cg->sym       = sym;
    cg->is_linux  = is_linux;
    cg->linker    = linker;
    cg->prefer_static_calls = prefer_static_calls;
    cg->loop_end_label   = -1;
    cg->loop_top_label   = -1;
    cg->switch_end_label = -1;
    cg->string_cap = 64;
    cg->strings    = (A64StringEntry*)malloc(cg->string_cap * sizeof(A64StringEntry));
    cg->float_const_cap = 16;
    cg->float_consts = (A64StringEntry*)malloc(cg->float_const_cap * sizeof(A64StringEntry));
    cg->wdata_cap  = 16;
    cg->wdata      = (A64WDataEntry*)malloc(cg->wdata_cap * sizeof(A64WDataEntry));
}

uint8_t *a64_codegen_get_text(CodeGenA64 *cg, int *len) {
    *len = cg->asm_->code_len;
    return cg->asm_->code;
}

uint8_t *a64_codegen_get_rdata(CodeGenA64 *cg, int *len) {
    *len = cg->asm_->rdata_len;
    return cg->asm_->rdata;
}

Relocation *a64_codegen_get_relocs(CodeGenA64 *cg, int *count) {
    *count = cg->asm_->reloc_count;
    return cg->asm_->relocs;
}
