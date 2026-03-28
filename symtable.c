#include "symtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strdup _strdup
#endif

typedef struct { const char *name; const char *dll; } WinAPI;
static const WinAPI WIN_APIS[] = {
    {"GetStdHandle","KERNEL32.dll"},{"WriteFile","KERNEL32.dll"},
    {"WriteConsoleA","KERNEL32.dll"},{"ExitProcess","KERNEL32.dll"},
    {"GetLastError","KERNEL32.dll"},{"CloseHandle","KERNEL32.dll"},
    {"CreateFileA","KERNEL32.dll"},{"ReadFile","KERNEL32.dll"},
    {"Sleep","KERNEL32.dll"},{"GetTickCount","KERNEL32.dll"},
    {"VirtualAlloc","KERNEL32.dll"},{"VirtualFree","KERNEL32.dll"},
    {"HeapAlloc","KERNEL32.dll"},{"HeapFree","KERNEL32.dll"},
    {"GetProcessHeap","KERNEL32.dll"},
    {"LocalAlloc","KERNEL32.dll"},{"LocalFree","KERNEL32.dll"},
    {"SetConsoleTextAttribute","KERNEL32.dll"},
    {"GetConsoleScreenBufferInfo","KERNEL32.dll"},
    {"SetConsoleCursorPosition","KERNEL32.dll"},
    /* UCRT/msvcrt memory */
    {"malloc","__internal__"},{"calloc","__internal__"},
    {"realloc","__internal__"},{"free","__internal__"},
    {"memcpy","__internal__"},{"memset","__internal__"},
    {"memcmp","__internal__"},{"memmove","__internal__"},
    {"strlen","__internal__"},{"strcpy","__internal__"},
    {"strncpy","__internal__"},{"strcmp","__internal__"},
    {"strncmp","__internal__"},{"strcat","__internal__"},
    {"strncat","__internal__"},{"strchr","__internal__"},
    {"strstr","__internal__"},{"atoi","__internal__"},
    {"atol","__internal__"},{"itoa","__internal__"},
    {"sprintf","__internal__"},{"printf","__internal__"},
    {"puts","__internal__"},{"putchar","__internal__"},
    {"abort","__internal__"},
    {"exit","__internal__"},
    {"GetSystemTimeAsFileTime","KERNEL32.dll"},
    {"GetCurrentProcessId","KERNEL32.dll"},
    {"GetCurrentThreadId","KERNEL32.dll"},
    {"InitializeSListHead","KERNEL32.dll"},
    {"GetSystemTime","KERNEL32.dll"},
    {"GetLocalTime","KERNEL32.dll"},
    {"GetTickCount","KERNEL32.dll"},
    {NULL,NULL}
};

static const char *find_dll(const char *name) {
    for (int i=0;WIN_APIS[i].name;i++)
        if (strcmp(WIN_APIS[i].name,name)==0) return WIN_APIS[i].dll;
    return NULL;
}

void symtable_init(SymTable *st, int is_64bit) {
    memset(st,0,sizeof *st);
    st->is_64bit = is_64bit;
    symtable_push_scope(st);
    /* Pre-define Windows API imports */
    for (int i=0;WIN_APIS[i].name;i++) {
        if (strcmp(WIN_APIS[i].dll,"__internal__")==0) {
            TypeInfo *t = typeinfo_new("void"); t->pointer_depth=0;
            symtable_define_func(st, WIN_APIS[i].name, t, -1, NULL);
        } else {
            symtable_define_import(st, WIN_APIS[i].name, WIN_APIS[i].dll);
        }
    }
}

void symtable_push_scope(SymTable *st) {
    Scope *s = calloc(1,sizeof(Scope));
    s->parent = st->current; st->current = s;
}

void symtable_pop_scope(SymTable *st) {
    if (!st->current) return;
    Scope *dead = st->current;
    st->current = dead->parent;
    Symbol *sym = dead->head;
    while (sym) {
        Symbol *nx=sym->next;
        free(sym->name); free(sym->dll);
        free(sym); sym=nx;
    }
    free(dead);
}

static Symbol *alloc_sym(SymTable *st, const char *name, TypeInfo *type, SymKind kind) {
    Symbol *s = calloc(1,sizeof(Symbol));
    s->name = strdup(name);
    s->type = type;
    s->kind = kind;
    s->is_64bit = st->is_64bit;
    s->next = st->current->head;
    st->current->head = s;
    return s;
}

static Symbol *alloc_global_sym(SymTable *st, const char *name, TypeInfo *type, SymKind kind) {
    /* Insert into global (bottom) scope */
    Scope *g = st->current;
    while (g->parent) g=g->parent;
    Symbol *s = calloc(1,sizeof(Symbol));
    s->name=strdup(name); s->type=type; s->kind=kind; s->is_64bit=st->is_64bit;
    s->next=g->head; g->head=s;
    return s;
}


/* Compute the actual size of a struct/union type, recursively resolving
 * nested struct fields. Returns size in bytes with proper field alignment. */
static int symtable_compute_struct_size(SymTable *st, ASTNode *struct_node) {
    if (!struct_node || struct_node->kind != AST_STRUCT_DECL) return 4;
    int is_union = struct_node->struct_decl.is_union;
    int total = 0;
    for (int i = 0; i < struct_node->struct_decl.nfields; i++) {
        ASTNode *f = struct_node->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD || !f->field.type) continue;
        TypeInfo *ft = f->field.type;
        int fsz;
        if (ft->pointer_depth > 0) {
            fsz = st->is_64bit ? 8 : 4;
        } else {
            /* Check if it's a struct/union type - look up recursively */
            const char *b = ft->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) {
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss2 = symtable_lookup(st, key);
                if (ss2 && ss2->struct_node)
                    fsz = symtable_compute_struct_size(st, ss2->struct_node);
                else if (ss2 && ss2->struct_size > 0)
                    fsz = ss2->struct_size;
                else
                    fsz = 4;
            } else {
                /* Primitive type */
                if (strcmp(b,"char")==0||strcmp(b,"int8_t")==0||strcmp(b,"uint8_t")==0||strcmp(b,"_Bool")==0||strcmp(b,"bool")==0) fsz=1;
                else if (strcmp(b,"short")==0||strcmp(b,"int16_t")==0||strcmp(b,"uint16_t")==0) fsz=2;
                else if (strcmp(b,"float")==0) fsz=4;
                else if (strcmp(b,"double")==0||strcmp(b,"long long")==0||strcmp(b,"int64_t")==0||strcmp(b,"uint64_t")==0) fsz=8;
                else fsz=4; /* int, long, etc */
            }
        }
        if (f->field.array_size > 0) fsz *= f->field.array_size;
        if (fsz < 1) fsz = 1;
        if (is_union) {
            if (fsz > total) total = fsz;
        } else {
            int align = fsz < 8 ? fsz : 8;
            if (align > 1) total = (total + align - 1) & ~(align - 1);
            total += fsz;
        }
    }
    if (total < 1) total = 1;
    return total;
}

Symbol *symtable_define_var(SymTable *st, const char *name, TypeInfo *type) {
    int ptr_size = st->is_64bit ? 8 : 4;
    int slot;
    if (type && type->array_size > 0) {
        /* Array: allocate elem_size * count bytes contiguously.
         * C arrays are stored element-by-element at their natural size.
         * Minimum slot is ptr_size so a zero-element array still has room. */
        int elem_size = typeinfo_size(type, st->is_64bit);
        if (elem_size < 1) elem_size = 4;
        slot = type->array_size * elem_size;
        if (slot < ptr_size) slot = ptr_size;
    } else {
        /* For struct/union types, look up the actual stored size */
        slot = typeinfo_size(type, st->is_64bit);
        if (slot <= 0) slot = ptr_size;
        /* For struct/union, use the struct_size from symtable if available */
        if (type && type->base && type->pointer_depth == 0) {
            const char *b = type->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) { /* it is a struct or union type */
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss=symtable_lookup(st,key);
                if (ss && ss->struct_size > 0) slot=ss->struct_size;
            }
        }
        if (slot < ptr_size) slot = ptr_size;
        /* Override with recursively-computed size for nested structs */
        if (type->base && type->pointer_depth == 0) {
            const char *b2 = type->base;
            const char *bare2 = b2;
            if (strncmp(bare2,"struct ",7)==0) bare2+=7;
            else if (strncmp(bare2,"union ",6)==0) bare2+=6;
            if (bare2 != b2) {
                char key2[256]; snprintf(key2,sizeof key2,"struct %s",bare2);
                Symbol *ss2 = symtable_lookup(st, key2);
                if (ss2 && ss2->struct_node) {
                    int real_sz = symtable_compute_struct_size(st, ss2->struct_node);
                    if (real_sz > slot) slot = real_sz;
                }
            }
        }
    }
    st->next_offset -= (slot + 15) & ~15; /* align to 16 for safety */
    Symbol *s = alloc_sym(st, name, type, SYM_VAR);
    s->offset = st->next_offset;
    s->array_size = type ? type->array_size : -1;
    return s;
}

Symbol *symtable_define_global(SymTable *st, const char *name, TypeInfo *type, int array_size) {
    Symbol *s = alloc_global_sym(st, name, type, SYM_GLOBAL);
    s->array_size = array_size;
    return s;
}

Symbol *symtable_define_param(SymTable *st, const char *name, TypeInfo *type, int idx, int byte_offset_32) {
    Symbol *s = alloc_sym(st, name, type, SYM_PARAM);
    s->param_index = idx;
    s->offset = st->is_64bit ? 16+idx*8 : 8+byte_offset_32;
    return s;
}

Symbol *symtable_define_func(SymTable *st, const char *name, TypeInfo *ret, int paramc, ASTNode *node) {
    Symbol *s = alloc_global_sym(st, name, ret, SYM_FUNC);
    s->paramc = paramc; s->func_node = node;
    return s;
}

Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll) {
    Symbol *s = alloc_global_sym(st, name, typeinfo_new("int"), SYM_IMPORT);
    s->dll = strdup(dll); s->paramc = -1;
    return s;
}

Symbol *symtable_define_enum_val(SymTable *st, const char *name, long long val) {
    Symbol *s = alloc_global_sym(st, name, typeinfo_new("int"), SYM_ENUM_VAL);
    s->enum_value = val;
    return s;
}

Symbol *symtable_define_typedef(SymTable *st, const char *name, TypeInfo *type) {
    Symbol *s = alloc_global_sym(st, name, type, SYM_TYPEDEF);
    return s;
}

Symbol *symtable_define_struct(SymTable *st, const char *name, ASTNode *node, int sz) {
    char key[256]; snprintf(key,sizeof key,"struct %s",name);
    Symbol *s = alloc_global_sym(st, key, typeinfo_new(key), SYM_STRUCT);
    s->struct_node = node; s->struct_size = sz;
    return s;
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    Scope *sc = st->current;
    while (sc) {
        Symbol *s = sc->head;
        while (s) { if (strcmp(s->name,name)==0) return s; s=s->next; }
        sc=sc->parent;
    }
    return NULL;
}

void symtable_reset_locals(SymTable *st) { st->next_offset=0; }
int  symtable_local_size  (SymTable *st) { int sz=-st->next_offset; return (sz+15)&~15; }

void symtable_add_import(SymTable *st, const char *dll_func) {
    for (int i=0;i<st->import_count;i++)
        if (strcmp(st->imports[i],dll_func)==0) return;
    if (st->import_count==st->import_cap) {
        st->import_cap = st->import_cap ? st->import_cap*2 : 16;
        st->imports = realloc(st->imports, st->import_cap*sizeof(char*));
    }
    st->imports[st->import_count++] = strdup(dll_func);
}

const char *symtable_find_dll(SymTable *st, const char *name) {
    (void)st;
    return find_dll(name);
}

void symtable_print(const SymTable *st) {
    printf("=== Symbol Table ===\n");
    Scope *sc=st->current; int d=0;
    while(sc) {
        printf(" Scope[%d]:\n",d++);
        for (Symbol *s=sc->head;s;s=s->next) {
            const char *kn[] = {"var","param","func","import","enum","typedef","struct","global"};
            printf("  %s %s",kn[s->kind],s->name);
            if (s->kind==SYM_VAR||s->kind==SYM_PARAM) printf("[%+d]",s->offset);
            if (s->dll) printf("(%s)",s->dll);
            printf("\n");
        }
        sc=sc->parent;
    }
    printf("Imports: %d\n",st->import_count);
    for (int i=0;i<st->import_count;i++) printf("  %s\n",st->imports[i]);
}
