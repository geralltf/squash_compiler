/*
 * compiler.c  —  Main driver: C source → Windows PE executable
 *
 * Pipeline:
 *   1. Read source file
 *   2. Lex (tokenise)
 *   3. Parse → AST
 *   4. Codegen: walk AST → emit x64 or x86 machine code via Assembler API
 *   5. Link: patch relocations, build import table
 *   6. PE builder: write PE32 or PE32+ executable
 *
 * Usage:
 *   compiler [-32|-64] <source.c> [-o output.exe]
 *   Default: 64-bit, output = source basename + .exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "symtable.h"
#include "assembler.h"
#include "codegen.h"
#include "pe_builder.h"

/* =========================================================================
 * Read entire file into heap buffer
 * ========================================================================= */
static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    char *buf = malloc(sz + 1);
    if (!buf) { perror("malloc"); exit(1); }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    return buf;
}

/* =========================================================================
 * Find a function's code offset in the assembler label table.
 * The codegen registered every function with asm_new_label + asm_def_label.
 * We look up by name in a->labels[].
 * ========================================================================= */
static int find_func_offset(Assembler *a, const char *name) {
    for (int i=0;i<a->label_count;i++) {
        if (strcmp(a->labels[i].name, name)==0 && a->labels[i].offset >= 0)
            return a->labels[i].offset;
    }
    return -1;
}

/* =========================================================================
 * Build an "__entry__" pseudo-reloc so the PE builder knows the entry offset
 * ========================================================================= */
static void inject_entry_reloc(Assembler *a, const char *entry_name) {
    int off = find_func_offset(a, entry_name);
    if (off < 0) {
        /* Try "main" as fallback */
        off = find_func_offset(a, "main");
        if (off < 0) {
            fprintf(stderr, "Warning: entry function '%s' not found; using offset 0\n",
                    entry_name);
            off = 0;
        }
    }
    /* Add a special reloc with symbol "__entry__" and addend=offset */
    if (a->reloc_count == a->reloc_cap) {
        a->reloc_cap *= 2;
        a->relocs = realloc(a->relocs, a->reloc_cap * sizeof(Relocation));
    }
    Relocation *r    = &a->relocs[a->reloc_count++];
    r->offset        = 0;
    r->kind          = RELOC_ABS32;
    r->symbol        = strdup("__entry__");
    r->addend        = off;
}

/* =========================================================================
 * Print AST and symbol table for debugging
 * ========================================================================= */
static void dump_ast(ASTNode *prog) {
    printf("\n=== AST ===\n");
    ast_print(prog, 0);
    printf("\n");
}
static void dump_symtable(SymTable *sym) {
    printf("\n");
    symtable_print(sym);
    printf("\n");
}
static void dump_code(Assembler *a) {
    printf("=== .text (%d bytes) ===\n", a->code_len);
    for (int i=0;i<a->code_len;i++) {
        printf("%02X", a->code[i]);
        if ((i+1)%16==0) printf("\n");
        else if ((i+1)%4==0) printf(" ");
    }
    printf("\n");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(int argc, char **argv) {
    int    is_64bit   = 1;            /* default 64-bit */
    char  *src_path   = NULL;
    char  *out_path   = NULL;
    int    dump       = 0;

    /* Parse arguments */
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"-32")==0)       is_64bit = 0;
        else if (strcmp(argv[i],"-64")==0)  is_64bit = 1;
        else if (strcmp(argv[i],"-dump")==0) dump = 1;
        else if (strcmp(argv[i],"-o")==0 && i+1<argc) {
            out_path = argv[++i];
        } else if (!src_path) {
            src_path = argv[i];
        } else {
            fprintf(stderr,"Unknown argument: %s\n", argv[i]);
        }
    }

    if (!src_path) {
        fprintf(stderr,
            "Usage: compiler [-32|-64] [-dump] <source.c> [-o output.exe]\n");
        return 1;
    }

    /* Default output path: replace .c with .exe or append .exe */
    char out_buf[512];
    if (!out_path) {
        strncpy(out_buf, src_path, sizeof out_buf-5);
        char *dot = strrchr(out_buf, '.');
        if (dot) *dot = '\0';
        strcat(out_buf, ".exe");
        out_path = out_buf;
    }

    printf("Compiling: %s → %s (%s)\n", src_path, out_path,
           is_64bit ? "64-bit" : "32-bit");

    /* -----------------------------------------------------------------------
     * Stage 1: Lex
     * -------------------------------------------------------------------- */
    char  *src = read_file(src_path);
    Lexer  lex;
    lexer_init(&lex, src);

    /* -----------------------------------------------------------------------
     * Stage 2: Symbol table init
     * -------------------------------------------------------------------- */
    SymTable sym;
    symtable_init(&sym, is_64bit);

    /* -----------------------------------------------------------------------
     * Stage 3: Parse
     * -------------------------------------------------------------------- */
    Parser parser;
    parser_init(&parser, &lex, &sym);
    ASTNode *prog = parse_program(&parser);

    if (dump) dump_ast(prog);

    /* -----------------------------------------------------------------------
     * Stage 4: Code generation
     * -------------------------------------------------------------------- */
    Assembler asm_;
    asm_init(&asm_, is_64bit);

    CodeGen cg;
    codegen_init(&cg, &asm_, &sym, is_64bit);
    codegen_program(&cg, prog);

    if (dump) {
        dump_symtable(&sym);
        dump_code(&asm_);
    }

    /* Inject entry point reloc so PE builder can find main() */
    inject_entry_reloc(&asm_, "main");

    /* -----------------------------------------------------------------------
     * Stage 5: Gather string labels for PE builder
     * -------------------------------------------------------------------- */
    int      rdata_len  = 0;
    uint8_t *rdata_data = codegen_get_rdata(&cg, &rdata_len);

    char **str_labels   = malloc(cg.string_count * sizeof(char*));
    int  *str_offsets   = malloc(cg.string_count * sizeof(int));
    for (int i=0;i<cg.string_count;i++) {
        str_labels[i]  = cg.strings[i].label;
        str_offsets[i] = cg.strings[i].offset;
    }

    /* -----------------------------------------------------------------------
     * Stage 6: PE build + link + write
     * -------------------------------------------------------------------- */
    int   text_len  = 0;
    uint8_t *text   = codegen_get_text(&cg, &text_len);

    int         reloc_count = 0;
    Relocation *relocs      = codegen_get_relocs(&cg, &reloc_count);

    PEBuildInput pbi;
    memset(&pbi, 0, sizeof pbi);
    pbi.is_64bit          = is_64bit;
    pbi.text              = text;
    pbi.text_len          = text_len;
    pbi.rdata_strings     = rdata_data;
    pbi.rdata_strings_len = rdata_len;
    pbi.relocs            = relocs;
    pbi.reloc_count       = reloc_count;
    pbi.string_labels     = str_labels;
    pbi.string_offsets    = str_offsets;
    pbi.string_count      = cg.string_count;
    pbi.import_specs      = sym.imports;
    pbi.import_count      = sym.import_count;
    pbi.entry_func        = "main";
    pbi.output_path       = out_path;

    int rc = pe_link_and_write(&pbi);

    /* Cleanup */
    free(src);
    free(str_labels);
    free(str_offsets);
    if (rdata_data) free(rdata_data);
    ast_free(prog);
    asm_free(&asm_);

    return rc;
}
