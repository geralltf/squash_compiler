#include "symtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* Avoid global array initializers (squash codegen doesn't support pointer-field init).
 * Use explicit per-entry definitions in symtable_init and find_dll instead. */
#define SI(name,dll) symtable_define_import(st,(name),(dll))
#define SF(name) do { TypeInfo *_t=typeinfo_new("void");_t->pointer_depth=0; symtable_define_func(st,(name),_t,-1,NULL); } while(0)

static const char *find_dll(const char *name) {
    if (!name) return NULL;
    if (strcmp(name,"GetStdHandle")==0||strcmp(name,"WriteFile")==0||
        strcmp(name,"WriteConsoleA")==0||strcmp(name,"ExitProcess")==0||
        strcmp(name,"GetLastError")==0||strcmp(name,"CloseHandle")==0||
        strcmp(name,"CreateFileA")==0||strcmp(name,"ReadFile")==0||
        strcmp(name,"Sleep")==0||strcmp(name,"GetTickCount")==0||
        strcmp(name,"VirtualAlloc")==0||strcmp(name,"VirtualFree")==0||
        strcmp(name,"HeapAlloc")==0||strcmp(name,"HeapFree")==0||
        strcmp(name,"GetProcessHeap")==0||strcmp(name,"HeapReAlloc")==0||
        strcmp(name,"LocalAlloc")==0||strcmp(name,"LocalFree")==0||
        strcmp(name,"SetConsoleTextAttribute")==0||
        strcmp(name,"GetConsoleScreenBufferInfo")==0||
        strcmp(name,"SetConsoleCursorPosition")==0||
        strcmp(name,"GetCommandLineA")==0||
        strcmp(name,"GetSystemTimeAsFileTime")==0||
        strcmp(name,"GetCurrentProcessId")==0||
        strcmp(name,"GetCurrentThreadId")==0||
        strcmp(name,"InitializeSListHead")==0||
        strcmp(name,"GetSystemTime")==0||
        strcmp(name,"GetLocalTime")==0) return "KERNEL32.dll";
    return NULL;
}

void symtable_init(SymTable *st, int is_64bit) {
    memset(st,0,sizeof *st);
    st->is_64bit = is_64bit;
    symtable_push_scope(st);
    /* Windows API imports */
    SI("GetStdHandle","KERNEL32.dll"); SI("WriteFile","KERNEL32.dll");
    SI("WriteConsoleA","KERNEL32.dll"); SI("ExitProcess","KERNEL32.dll");
    SI("GetLastError","KERNEL32.dll"); SI("CloseHandle","KERNEL32.dll");
    SI("CreateFileA","KERNEL32.dll"); SI("ReadFile","KERNEL32.dll");
    SI("Sleep","KERNEL32.dll"); SI("GetTickCount","KERNEL32.dll");
    SI("VirtualAlloc","KERNEL32.dll"); SI("VirtualFree","KERNEL32.dll");
    SI("HeapAlloc","KERNEL32.dll"); SI("HeapFree","KERNEL32.dll");
    SI("HeapReAlloc","KERNEL32.dll"); SI("GetProcessHeap","KERNEL32.dll");
    SI("LocalAlloc","KERNEL32.dll"); SI("LocalFree","KERNEL32.dll");
    SI("SetConsoleTextAttribute","KERNEL32.dll");
    SI("GetConsoleScreenBufferInfo","KERNEL32.dll");
    SI("SetConsoleCursorPosition","KERNEL32.dll");
    SI("GetCommandLineA","KERNEL32.dll");
    SI("GetSystemTimeAsFileTime","KERNEL32.dll");
    SI("GetCurrentProcessId","KERNEL32.dll");
    SI("GetCurrentThreadId","KERNEL32.dll");
    SI("InitializeSListHead","KERNEL32.dll");
    SI("GetSystemTime","KERNEL32.dll");
    SI("GetLocalTime","KERNEL32.dll");
    /* Internal shims (handled by codegen directly) */
    SF("malloc"); SF("calloc"); SF("realloc"); SF("free");
    SF("memcpy"); SF("memset"); SF("memcmp"); SF("memmove");
    SF("strlen"); SF("strcpy"); SF("strncpy"); SF("strcmp");
    SF("strncmp"); SF("strcat"); SF("strncat"); SF("strchr");
    SF("strstr"); SF("atoi"); SF("atol"); SF("itoa");
    SF("sprintf"); SF("printf"); SF("puts"); SF("putchar");
    SF("abort"); SF("exit");
    SF("fprintf"); SF("fflush"); SF("fwrite"); SF("fread");
    SF("fopen"); SF("fclose"); SF("fseek"); SF("ftell"); SF("rewind");
    SF("fgets"); SF("fputs"); SF("feof"); SF("fgetc"); SF("fputc");
    SF("ungetc"); SF("remove"); SF("rename"); SF("ferror"); SF("clearerr");
    SF("vfprintf"); SF("vprintf"); SF("snprintf"); SF("vsnprintf");
    SF("perror"); SF("atof"); SF("strrchr"); SF("_snprintf");
    SF("puts"); SF("putchar");
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
    s->name = my_strdup(name);
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
    s->name=my_strdup(name);
    s->type=type;
    s->kind=kind;
    s->is_64bit=st->is_64bit;
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
                /* Check if it's a typedef for a struct */
                Symbol *td = symtable_lookup(st, b);
                if (td && td->kind == SYM_TYPEDEF && td->type) {
                    /* Recurse with the resolved typedef type */
                    TypeInfo *rt = td->type;
                    if (rt->pointer_depth > 0) {
                        fsz = st->is_64bit ? 8 : 4;
                    } else {
                        const char *rb = rt->base; const char *rbare = rb;
                        if (strncmp(rbare,"struct ",7)==0) rbare+=7;
                        else if (strncmp(rbare,"union ",6)==0) rbare+=6;
                        if (rbare != rb) {
                            char rkey[256]; snprintf(rkey,sizeof rkey,"struct %s",rbare);
                            Symbol *rss = symtable_lookup(st, rkey);
                            if (rss && rss->struct_node)
                                fsz = symtable_compute_struct_size(st, rss->struct_node);
                            else if (rss && rss->struct_size > 0)
                                fsz = rss->struct_size;
                            else fsz = 4;
                        } else {
                            fsz = typeinfo_size(rt, st->is_64bit);
                            if (fsz < 1) fsz = 4;
                        }
                    }
                } else if (td && td->kind == SYM_STRUCT) {
                    if (td->struct_node)
                        fsz = symtable_compute_struct_size(st, td->struct_node);
                    else if (td->struct_size > 0)
                        fsz = td->struct_size;
                    else fsz = 4;
                } else {
                    /* Primitive type */
                    if (strcmp(b,"char")==0||strcmp(b,"int8_t")==0||strcmp(b,"uint8_t")==0||strcmp(b,"_Bool")==0||strcmp(b,"bool")==0) fsz=1;
                    else if (strcmp(b,"short")==0||strcmp(b,"int16_t")==0||strcmp(b,"uint16_t")==0) fsz=2;
                    else if (strcmp(b,"float")==0) fsz=4;
                    else if (strcmp(b,"long")==0) fsz = st->is_64bit ? 8 : 4;
                    else if (strcmp(b,"double")==0||strcmp(b,"long long")==0||strcmp(b,"int64_t")==0||strcmp(b,"uint64_t")==0) fsz=8;
                    else fsz=4; /* int, unsigned int, etc */
                }
            }
        }
        int elem_fsz = (f->field.array_size > 0) ? fsz : fsz; /* element size */
        if (f->field.array_size > 0) fsz *= f->field.array_size;
        if (fsz < 1) fsz = 1;
        if (is_union) {
            if (fsz > total) total = fsz;
        } else {
            /* Align field: use element size (not total array size) for alignment */
            int align = elem_fsz < 8 ? elem_fsz : 8;
            if (align > 1) total = (total + align - 1) & ~(align - 1);
            total += fsz;
        }
    }
    if (total < 1) total = 1;
    /* Add trailing padding to align the struct size to its own alignment
     * (max natural alignment of any field, capped at 8). */
    if (!is_union) {
        int max_align = 1;
        for (int i = 0; i < struct_node->struct_decl.nfields; i++) {
            ASTNode *f = struct_node->struct_decl.fields[i];
            if (!f || f->kind != AST_FIELD || !f->field.type) continue;
            int fa = 4; /* default alignment */
            if (f->field.type->pointer_depth > 0)
                fa = st->is_64bit ? 8 : 4;
            else {
                const char *b = f->field.type->base;
                if (!b) { fa = 4; }
                else if (strcmp(b,"char")==0||strcmp(b,"int8_t")==0||strcmp(b,"uint8_t")==0) fa=1;
                else if (strcmp(b,"short")==0||strcmp(b,"int16_t")==0||strcmp(b,"uint16_t")==0) fa=2;
                else if (strcmp(b,"double")==0||strcmp(b,"long long")==0||strcmp(b,"int64_t")==0||strcmp(b,"uint64_t")==0) fa=8;
                else if (strcmp(b,"long")==0) fa = st->is_64bit ? 8 : 4;
                else fa = 4;
                /* For struct-type fields, max alignment is 8 */
                const char *bare = b;
                if (strncmp(bare,"struct ",7)==0||strncmp(bare,"union ",6)==0) fa = 8;
            }
            if (fa > max_align) max_align = fa;
        }
        if (max_align > 8) max_align = 8;
        if (max_align > 1) total = (total + max_align - 1) & ~(max_align - 1);
    }
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
        /* For struct/union element types, look up actual size in symtable */
        if (type->base && type->pointer_depth == 0) {
            const char *b = type->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) {
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss = symtable_lookup(st, key);
                if (ss && ss->struct_size > 0) elem_size = ss->struct_size;
                else if (ss && ss->struct_node) elem_size = symtable_compute_struct_size(st, ss->struct_node);
            } else {
                /* Try as typedef */
                Symbol *td = symtable_lookup(st, b);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0) {
                    const char *tb = td->type->base;
                    const char *tbare = tb;
                    if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                    else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                    if (tbare != tb) {
                        char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                        Symbol *tss = symtable_lookup(st, tkey);
                        if (tss && tss->struct_size > 0) elem_size = tss->struct_size;
                        else if (tss && tss->struct_node) elem_size = symtable_compute_struct_size(st, tss->struct_node);
                    }
                }
            }
        }
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
        /* Override with recursively-computed size for nested structs or typedefs */
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
            } else {
                /* Try as typedef — e.g. PPState *st = ..., where PPState is a typedef for a struct */
                Symbol *td = symtable_lookup(st, b2);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0) {
                    const char *tb = td->type->base;
                    const char *tbare = tb;
                    if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                    else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                    if (tbare != tb) {
                        char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                        Symbol *tss = symtable_lookup(st, tkey);
                        if (tss && tss->struct_node) {
                            int real_sz = symtable_compute_struct_size(st, tss->struct_node);
                            if (real_sz > slot) slot = real_sz;
                        } else if (tss && tss->struct_size > 0 && tss->struct_size > slot) {
                            slot = tss->struct_size;
                        }
                    }
                } else if (td && td->kind == SYM_STRUCT) {
                    if (td->struct_size > slot) slot = td->struct_size;
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
    s->dll = my_strdup(dll); s->paramc = -1;
    return s;
}

Symbol *symtable_define_enum_val(SymTable *st, const char *name, long long val) {
    TypeInfo *ti = typeinfo_new("int");
    Symbol *s = alloc_global_sym(st, name, ti, SYM_ENUM_VAL);
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
    s->struct_node = node;
    /* Compute accurate struct size using recursive field traversal */
    int real_sz = symtable_compute_struct_size(st, node);
    s->struct_size = (real_sz > sz) ? real_sz : sz;
    return s;
}

int symtable_sizeof_struct(SymTable *st, ASTNode *struct_node) {
    return symtable_compute_struct_size(st, struct_node);
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    Scope *sc = st->current;
    while (sc) {
        Symbol *s = sc->head;
        while (s) {
            if (strcmp(s->name,name)==0) return s;
            s=s->next;
        }
        sc=sc->parent;
    }
    return NULL;
}

/* Debug helper: count global scope symbols */
int symtable_global_count(SymTable *st) {
    Scope *g = st->current;
    while (g->parent) g=g->parent;
    int c=0; Symbol *s=g->head; while(s){c++;s=s->next;} return c;
}
/* Debug: print last N in global chain */
void symtable_check_global(SymTable *st, const char *tag) {
    Scope *g = st->current;
    while (g->parent) g=g->parent;
    int c=0; Symbol *s=g->head;
    Symbol *prev=NULL;
    while(s){c++;prev=s;s=s->next;}
    printf("[SCG] %s count=%d last=%p\n", tag, c, (void*)prev); fflush(0);
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
    st->imports[st->import_count++] = my_strdup(dll_func);
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
