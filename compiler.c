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

static char *read_file(const char *path) {
    FILE *fp=fopen(path,"rb");
    if (!fp) { printf("error: cannot open %s\n",path); exit(1); }
    fseek(fp,0,SEEK_END); long sz=ftell(fp); rewind(fp);
    char *buf=malloc(sz+1); fread(buf,1,sz,fp); buf[sz]='\0';
    fclose(fp); return buf;
}

static int find_func_offset(Assembler *a, const char *name) {
    for (int i=0;i<a->label_count;i++)
        if (strcmp(a->labels[i].name,name)==0 && a->labels[i].offset>=0)
            return a->labels[i].offset;
    return -1;
}

static void inject_entry_reloc(Assembler *a, const char *entry) {
    int off=find_func_offset(a,entry);
    if (off<0) off=find_func_offset(a,"main");
    if (off<0) off=0;
    if (a->reloc_count==a->reloc_cap) {
        a->reloc_cap*=2; a->relocs=realloc(a->relocs,a->reloc_cap*sizeof(Relocation));
    }
    Relocation *r=&a->relocs[a->reloc_count++];
    r->offset=0; r->kind=RELOC_ABS32; r->symbol=my_strdup("__entry__"); r->addend=off;
}

int main(int argc, char **argv) {
    int    is_64bit=1;
    char  *src_path=NULL, *out_path=NULL;
    int    dump=0;
    const char *include_dirs[32]; int n_inc=0;

    for (int i=1;i<argc;i++) {
        if      (strcmp(argv[i],"-32")==0)    is_64bit=0;
        else if (strcmp(argv[i],"-64")==0)    is_64bit=1;
        else if (strcmp(argv[i],"-dump")==0)  dump=1;
        else if (strcmp(argv[i],"-o")==0 && i+1<argc) out_path=argv[++i];
        else if (strncmp(argv[i],"-I",2)==0)  {
            const char *d=argv[i]+2;
            if (!d[0]&&i+1<argc) d=argv[++i];
            if (n_inc<32) include_dirs[n_inc++]=d;
        }
        else if (!src_path) src_path=argv[i];
        else printf("unknown argument: %s\n",argv[i]);
    }
    if (!src_path) {
        printf("Usage: compiler [-32|-64] [-dump] [-I dir] <source.c> [-o output.exe]\n");
        return 1;
    }
    char out_buf[512];
    if (!out_path) {
        strncpy(out_buf,src_path,sizeof out_buf-5);
        char *dot=strrchr(out_buf,'.');
        if (dot) *dot='\0';
        strcat(out_buf,".exe");
        out_path=out_buf;
    }

    printf("Compiling: %s -> %s (%s) [v92]\n",src_path,out_path,is_64bit?"64-bit":"32-bit");

    /* Stage 1: Read + preprocess */
    char *raw=read_file(src_path);
    include_dirs[n_inc]=NULL;
    char *src=preprocess(raw,src_path,include_dirs,n_inc);
    free(raw);

    /* Stage 1b: Optionally dump preprocessed output */
    if (dump) {
        printf("=== Preprocessed output ===\n%s\n=== End PP ===\n", src);
    }

    /* Stage 2: Lex */
    Lexer lex;
    lexer_init(&lex,src,src_path);

    /* Stage 3: Symbol table */
    SymTable sym;
    symtable_init(&sym,is_64bit);

    /* Stage 4: Parse */
    Parser parser;
    parser_init(&parser,&lex,&sym,src_path);
    ASTNode *prog=parse_program(&parser);

    if (parser.error_count>0) {
        printf("%d compile error(s). Aborting.\n",parser.error_count);
        return 1;
    }

    if (dump) {
        printf("\n=== AST ===\n"); ast_print(prog,0);
        printf("\n"); symtable_print(&sym);
    }

    /* Stage 5: Code generation */
    Assembler as;
    asm_init(&as,is_64bit);
    CodeGen cg;
    codegen_init(&cg,&as,&sym,is_64bit);
    codegen_program(&cg,prog);

    if (dump) {
        printf("=== .text (%d bytes) ===\n",as.code_len);
        for (int i=0;i<as.code_len;i++) {
            printf("%02X",as.code[i]);
            if ((i+1)%16==0) printf("\n"); else if ((i+1)%4==0) printf(" ");
        }
        printf("\n");
    }

    inject_entry_reloc(&as,"main");

    /* Stage 6: Gather string labels */
    int rdata_len=0;
    uint8_t *rdata_data=codegen_get_rdata(&cg,&rdata_len);
    char **str_labels=malloc(cg.string_count*sizeof(char*));
    int  *str_offsets=malloc(cg.string_count*sizeof(int));
    for (int i=0;i<cg.string_count;i++) {
        str_labels[i]=cg.strings[i].label;
        str_offsets[i]=cg.strings[i].offset;
    }

    /* Stage 7: PE build + link */
    int text_len=0;
    uint8_t *text=codegen_get_text(&cg,&text_len);
    int reloc_count=0;
    Relocation *relocs=codegen_get_relocs(&cg,&reloc_count);

    /* Resolve RELOC_TEXT_ABS32 (function pointer addresses in 32-bit mode) */
    uint32_t text_rva_val  = 0x1000;
    uint64_t image_base_val = is_64bit ? 0x140000000ULL : 0x00400000ULL;
    asm_resolve_text_relocs(&as, text_rva_val, image_base_val);

    /* Build wdata label/offset arrays for the PE builder */
    char **wdata_labels  = malloc(cg.wdata_count * sizeof(char*));
    int  *wdata_offsets  = malloc(cg.wdata_count * sizeof(int));
    for (int i=0; i<cg.wdata_count; i++) {
        wdata_labels[i]  = cg.wdata[i].label;
        wdata_offsets[i] = cg.wdata[i].offset;
    }

    PEBuildInput pbi; memset(&pbi,0,sizeof pbi);
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
    pbi.wdata_bytes       = calloc(cg.wdata_pool_size + 1, 1); /* zero-init wdata */
    pbi.wdata_len         = cg.wdata_pool_size;
    pbi.wdata_labels      = wdata_labels;
    pbi.wdata_offsets     = wdata_offsets;
    pbi.wdata_count       = cg.wdata_count;
    pbi.import_specs      = sym.imports;
    pbi.import_count      = sym.import_count;
    pbi.entry_func        = "main";
    pbi.output_path       = out_path;

    int rc=pe_link_and_write(&pbi);

    free(src); free(str_labels); free(str_offsets);
    free(wdata_labels); free(wdata_offsets);
    if (pbi.wdata_bytes) free(pbi.wdata_bytes);
    if (rdata_data) free(rdata_data);
    ast_free(prog); asm_free(&as);
    return rc;
}
