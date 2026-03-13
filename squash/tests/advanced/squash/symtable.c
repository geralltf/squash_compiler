#include "symtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strdup _strdup
#endif

/* =========================================================================
 * Predefined Windows API imports: name -> dll
 * ========================================================================= */
typedef struct { const char *name; const char *dll; } WinAPI;
static const WinAPI WIN_APIS[] = {
    /* kernel32 */
    {"GetStdHandle",           "KERNEL32.dll"},
    {"WriteFile",              "KERNEL32.dll"},
    {"WriteConsoleA",          "KERNEL32.dll"},
    {"ExitProcess",            "KERNEL32.dll"},
    {"GetLastError",           "KERNEL32.dll"},
    {"CloseHandle",            "KERNEL32.dll"},
    {"CreateFileA",            "KERNEL32.dll"},
    {"ReadFile",               "KERNEL32.dll"},
    {"GetProcessHeap",         "KERNEL32.dll"},
    {"HeapAlloc",              "KERNEL32.dll"},
    {"HeapFree",               "KERNEL32.dll"},
    {"LocalAlloc",             "KERNEL32.dll"},
    {"LocalFree",              "KERNEL32.dll"},
    {"Sleep",                  "KERNEL32.dll"},
    {"GetTickCount",           "KERNEL32.dll"},
    {"VirtualAlloc",           "KERNEL32.dll"},
    {"VirtualFree",            "KERNEL32.dll"},
    {"SetConsoleTitle",        "KERNEL32.dll"},
    {"GetConsoleWindow",       "KERNEL32.dll"},
    /* msvcrt / ucrtbase shims we support directly */
    {"printf",                 "__internal__"},
    {"puts",                   "__internal__"},
    {NULL, NULL}
};

static const char *find_dll(const char *name) {
    for (int i = 0; WIN_APIS[i].name; i++)
        if (strcmp(WIN_APIS[i].name, name)==0)
            return WIN_APIS[i].dll;
    return NULL;
}

/* =========================================================================
 * symtable_init
 * ========================================================================= */
void symtable_init(SymTable *st, int is_64bit) {
    memset(st, 0, sizeof *st);
    st->is_64bit    = is_64bit;
    st->next_offset = 0;
    /* push global scope */
    symtable_push_scope(st);

    /* Pre-define all known Windows API functions as imports */
    for (int i = 0; WIN_APIS[i].name; i++) {
        if (strcmp(WIN_APIS[i].dll, "__internal__") == 0) {
            /* handled internally; define as func with variadic hint */
            symtable_define_func(st, WIN_APIS[i].name, "int", -1);
        } else {
            symtable_define_import(st, WIN_APIS[i].name, WIN_APIS[i].dll);
        }
    }
}

/* =========================================================================
 * Scope management
 * ========================================================================= */
void symtable_push_scope(SymTable *st) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent  = st->current;
    st->current = s;
}

void symtable_pop_scope(SymTable *st) {
    if (!st->current) return;
    Scope *dead = st->current;
    st->current = dead->parent;
    /* free symbols in this scope */
    Symbol *sym = dead->head;
    while (sym) {
        Symbol *next = sym->next;
        free(sym->name);
        free(sym->type_name);
        free(sym->dll);
        free(sym);
        sym = next;
    }
    free(dead);
}

/* =========================================================================
 * Alloc a symbol in the current scope
 * ========================================================================= */
static Symbol *alloc_sym(SymTable *st, const char *name, const char *type, SymKind kind) {
    Symbol *s = calloc(1, sizeof(Symbol));
    s->name      = strdup(name);
    s->type_name = type ? strdup(type) : strdup("int");
    s->kind      = kind;
    s->is_64bit  = st->is_64bit;
    /* prepend to current scope */
    s->next = st->current->head;
    st->current->head = s;
    return s;
}

/* =========================================================================
 * Define a local variable — allocated on the stack
 * 64-bit: 8-byte slots; 32-bit: 4-byte slots
 * ========================================================================= */
Symbol *symtable_define_var(SymTable *st, const char *name, const char *type) {
    int slot = st->is_64bit ? 8 : 4;
    st->next_offset -= slot;
    Symbol *s  = alloc_sym(st, name, type, SYM_VAR);
    s->offset  = st->next_offset;
    return s;
}

/* =========================================================================
 * Define a function parameter
 * x64 ABI: first 4 params in RCX/RDX/R8/R9, rest on stack.
 * x86 ABI: all params pushed on stack (offset above saved EBP).
 * For simplicity we track by index; codegen resolves the actual register/offset.
 * ========================================================================= */
Symbol *symtable_define_param(SymTable *st, const char *name, const char *type, int idx) {
    Symbol *s     = alloc_sym(st, name, type, SYM_PARAM);
    s->param_index = idx;
    /* compute stack offset for parameter (caller-passed, above RBP) */
    if (st->is_64bit) {
        /* shadow space: [RBP+16], [RBP+24], [RBP+32], [RBP+40] for reg-passed,
         * then [RBP+48]... for stack-passed.
         * We spill all into shadow space for simplicity. */
        s->offset = 16 + idx * 8;
    } else {
        /* 32-bit: first param at [EBP+8], then [EBP+12], ... */
        s->offset = 8 + idx * 4;
    }
    return s;
}

/* =========================================================================
 * Define a function (in global scope)
 * ========================================================================= */
Symbol *symtable_define_func(SymTable *st, const char *name, const char *ret_type, int paramc) {
    /* Use global scope (bottom of stack) */
    Scope *cur  = st->current;
    Scope *glob = cur;
    while (glob->parent) glob = glob->parent;

    Symbol *s = calloc(1, sizeof(Symbol));
    s->name      = strdup(name);
    s->type_name = strdup(ret_type ? ret_type : "int");
    s->kind      = SYM_FUNC;
    s->paramc    = paramc;
    s->is_64bit  = st->is_64bit;
    s->next      = glob->head;
    glob->head   = s;
    return s;
}

/* =========================================================================
 * Define an import
 * ========================================================================= */
Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll) {
    Scope *glob = st->current;
    while (glob->parent) glob = glob->parent;

    Symbol *s = calloc(1, sizeof(Symbol));
    s->name      = strdup(name);
    s->type_name = strdup("int");
    s->kind      = SYM_IMPORT;
    s->dll       = strdup(dll);
    s->is_64bit  = st->is_64bit;
    s->paramc    = -1;
    s->next      = glob->head;
    glob->head   = s;
    return s;
}

/* =========================================================================
 * Lookup — walks scope chain from innermost
 * ========================================================================= */
Symbol *symtable_lookup(SymTable *st, const char *name) {
    Scope *s = st->current;
    while (s) {
        Symbol *sym = s->head;
        while (sym) {
            if (strcmp(sym->name, name)==0) return sym;
            sym = sym->next;
        }
        s = s->parent;
    }
    return NULL;
}

/* =========================================================================
 * Reset locals between functions
 * ========================================================================= */
void symtable_reset_locals(SymTable *st) {
    st->next_offset = 0;
}

int symtable_local_size(SymTable *st) {
    int sz = -st->next_offset;
    /* align to 16 bytes */
    return (sz + 15) & ~15;
}

/* =========================================================================
 * Track which Windows API imports are actually used (for PE import table)
 * ========================================================================= */
void symtable_add_import(SymTable *st, const char *dll_func) {
    /* Avoid duplicates */
    for (int i=0; i<st->import_count; i++)
        if (strcmp(st->imports[i], dll_func)==0) return;

    if (st->import_count == st->import_cap) {
        st->import_cap = st->import_cap ? st->import_cap*2 : 16;
        st->imports = realloc(st->imports, st->import_cap * sizeof(char*));
    }
    st->imports[st->import_count++] = strdup(dll_func);
}

void symtable_print(const SymTable *st) {
    printf("=== Symbol Table ===\n");
    Scope *s = st->current;
    int depth = 0;
    while (s) {
        printf("  Scope[%d]:\n", depth++);
        Symbol *sym = s->head;
        while (sym) {
            printf("    %s %s",
                   sym->kind==SYM_VAR    ? "var"    :
                   sym->kind==SYM_PARAM  ? "param"  :
                   sym->kind==SYM_FUNC   ? "func"   :
                   sym->kind==SYM_IMPORT ? "import" : "?",
                   sym->name);
            if (sym->kind==SYM_VAR||sym->kind==SYM_PARAM)
                printf(" [%+d]", sym->offset);
            if (sym->dll) printf(" (%s)", sym->dll);
            printf("\n");
            sym = sym->next;
        }
        s = s->parent;
    }
    printf("Tracked imports (%d):\n", st->import_count);
    for (int i=0;i<st->import_count;i++)
        printf("  %s\n", st->imports[i]);
}
