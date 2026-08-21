#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "symtable.h"
#include "assembler.h"
#include "codegen.h"
#include "arm64_asm.h"
#include "codegen_arm64.h"
#include "pe_builder.h"
#include "elf_builder.h"
#include "macho_builder.h"
#include "linker.h"
#include "winlinker.h"
#include "objfile.h"
#include "diag.h"
#if defined(__OpenBSD__)
#include <dirent.h>
#endif

/* Only meaningful when squash itself is running natively on an OpenBSD host
 * (self-hosted bootstrap, or a native OpenBSD build compiling OpenBSD
 * targets) — there is no way to know a *different* OpenBSD release's exact
 * libc.so.MAJOR.MINOR soname from a cross-compiling host, since OpenBSD
 * bumps it most releases and keeps no stable unversioned alias. Returns
 * NULL (letting the caller fall back to a placeholder + warning) unless
 * built for OpenBSD. */
static const char *openbsd_detect_libc_soname(void) {
#if defined(__OpenBSD__)
    DIR *d = opendir("/usr/lib");
    if (!d) return NULL;
    static char best[128];
    int best_maj = -1, best_min = -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int maj = -1, min = -1;
        if (sscanf(e->d_name, "libc.so.%d.%d", &maj, &min) == 2) {
            if (maj > best_maj || (maj == best_maj && min > best_min)) {
                best_maj = maj; best_min = min;
                snprintf(best, sizeof best, "%s", e->d_name);
            }
        }
    }
    closedir(d);
    return best_maj >= 0 ? best : NULL;
#else
    return NULL;
#endif
}

static char *read_file(const char *path) {
    FILE *fp=fopen(path,"rb");
    if (!fp) { diag_emit(DIAG_ERROR, -1, NULL, NULL, "cannot open %s", path); exit(1); }
    fseek(fp,0,SEEK_END); long sz=ftell(fp); rewind(fp);
    char *buf=malloc(sz+1); fread(buf,1,sz,fp); buf[sz]='\0';
    fclose(fp);
    /* normalize line endings: strip \r so CRLF->LF and bare CR->nothing */
    { int _ri=0, _wi=0; while (buf[_ri]) { if (buf[_ri]!='\r') buf[_wi++]=buf[_ri]; _ri++; } buf[_wi]='\0'; }
    return buf;
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
    if (off<0) {
        /* Silently defaulting to offset 0 (the very start of .text) makes
         * the OS execute whatever unrelated function the compiler happened
         * to emit first as the program's entry point — not a crash the
         * compiler itself can detect, but a real, confusing, hard-to-
         * diagnose runtime failure (e.g. SDL3's own <SDL3/SDL_main.h>
         * "#define main SDL_main" silently renames a real main() to
         * SDL_main, so a program built without SDL's own platform-specific
         * launcher wrapper loses its entry point this way with zero
         * compile-time errors or warnings). Surface it instead. */
        diag_emit(DIAG_WARNING, -1, NULL, NULL,
                  "no function named '%s' or 'main' found — entry point defaulting to the start of .text (offset 0), which is almost certainly NOT what you want. If a header '#define's your entry function to another name (e.g. SDL3's SDL_main.h does \"#define main SDL_main\"), #undef it before your real definition", entry);
        off=0;
    }
    if (a->reloc_count==a->reloc_cap) {
        a->reloc_cap*=2; a->relocs=realloc(a->relocs,a->reloc_cap*sizeof(Relocation));
    }
    Relocation *r=&a->relocs[a->reloc_count++];
    r->offset=0; r->kind=RELOC_ABS32; r->symbol=my_strdup("__entry__"); r->addend=off;
}

static int find_func_offset_a64(Arm64Asm *a, const char *name) {
    for (int i=0;i<a->label_count;i++)
        if (strcmp(a->labels[i].name,name)==0 && a->labels[i].offset>=0)
            return a->labels[i].offset;
    return -1;
}

/* elf_builder assumes main is at the start of .text unless it finds a
 * "__entry__" reloc (see inject_entry_reloc above) — without this, ARM64
 * binaries would call whatever function happens to be compiled first. */
static void inject_entry_reloc_a64(Arm64Asm *a, const char *entry) {
    int off=find_func_offset_a64(a,entry);
    if (off<0) off=find_func_offset_a64(a,"main");
    if (off<0) {
        /* See the identical warning in inject_entry_reloc() (x86-64 path)
         * above for why silently defaulting to offset 0 here is dangerous. */
        diag_emit(DIAG_WARNING, -1, NULL, NULL,
                  "no function named '%s' or 'main' found — entry point defaulting to the start of .text (offset 0), which is almost certainly NOT what you want. If a header '#define's your entry function to another name (e.g. SDL3's SDL_main.h does \"#define main SDL_main\"), #undef it before your real definition", entry);
        off=0;
    }
    if (a->reloc_count==a->reloc_cap) {
        a->reloc_cap*=2; a->relocs=realloc(a->relocs,a->reloc_cap*sizeof(Relocation));
    }
    Relocation *r=&a->relocs[a->reloc_count++];
    r->offset=0; r->kind=RELOC_ABS32; r->symbol=my_strdup("__entry__"); r->addend=off;
}

int main(int argc, char **argv) {
    int    is_64bit=1;
    int    is_linux=0;
    int    is_arm64=0;
    /* -macos: Mach-O output for Intel (x86-64) macOS. macOS uses the very
     * same System V AMD64 ABI and LP64 type model as Linux, so this rides
     * on the entire existing -linux code path (is_linux stays 1 below) and
     * only swaps the executable writer at the very end — see
     * macho_builder.h. It is NOT a third value of is_linux precisely so
     * that every ABI/type-size decision downstream keeps seeing "Unix"
     * without needing a macOS case of its own. */
    int    is_macos=0;
    /* -openbsd: native ELF output for OpenBSD, riding on the same SysV/LP64
     * -linux codegen path as -macos does (see is_macos comment above) —
     * only the ELF branding (EI_OSABI, .note.openbsd.ident, ld.so path) and
     * the libc soname differ; see g_squash_openbsd_target/g_squash_libc_soname
     * in ast.h. */
    int    is_openbsd=0;
    const char *openbsd_libc_override = NULL;
    char  *src_path=NULL, *out_path=NULL;
    int    dump=0;
    int    compile_only=0; /* -c: emit a .sqo object file instead of a linked executable */
    /* -nodebug: omit the ELF .symtab/.strtab/.shstrtab + section-header
     * table that elf_link_and_write() writes by default (see
     * elf_builder.h's strip_debug_sections comment) -- a smaller output
     * file with nothing for readelf/objdump/nm/gdb to read function names
     * off of. Off by default: section headers are real, useful debugging
     * information (this is what made gdb/valgrind/objdump able to make
     * sense of squash's own output at all — see elf_link_and_write's own
     * "Step 9.6" comment for the history) and costs nothing at runtime
     * (the extra bytes live past the end of the loaded program image), so
     * there's no reason to strip them unless the caller explicitly wants
     * to. Linux/ELF-only for now — Windows PE and macOS Mach-O output
     * never grew the equivalent debug metadata in the first place, so
     * this flag is simply a no-op for -windows/-macos builds. */
    int    nodebug=0;
    /* Tracks whether the user passed an explicit -linux/-windows/-macos/
     * -openbsd flag, so the default (see below, once arg parsing is done)
     * can fall back to whatever platform THIS squash binary itself was
     * built for/on, instead of always defaulting to Windows regardless of
     * host. */
    int    target_explicit=0;
    const char *include_dirs[32]; int n_inc=0;
    /* Library flags: collect -l and -L for Linux linking */
    const char *lib_flags[64];  int n_lib=0;
    const char *lib_paths[32];  int n_lpath=0;
    /* Previously-compiled squash object files (".sqo", see objfile.h) to
     * link against this compile — e.g. "squash main.c common.sqo -o a.exe"
     * recompiles only main.c and links it against a common.sqo built once
     * ahead of time via "squash -c big_shared.c -o common.sqo", instead of
     * recompiling the whole shared body from source every time. */
    const char *obj_flags[64]; int n_obj=0;

    for (int i=1;i<argc;i++) {
        if      (strcmp(argv[i],"-32")==0)      is_64bit=0;
        else if (strcmp(argv[i],"-64")==0)      is_64bit=1;
        else if (strcmp(argv[i],"-arm64")==0)   { is_arm64=1; is_64bit=1; }
        else if (strcmp(argv[i],"-linux")==0)   { is_linux=1; is_macos=0; is_openbsd=0; target_explicit=1; }
        else if (strcmp(argv[i],"-windows")==0) { is_linux=0; is_macos=0; is_openbsd=0; target_explicit=1; }
        else if (strcmp(argv[i],"-macos")==0)   { is_macos=1; is_linux=1; is_64bit=1; is_openbsd=0; target_explicit=1; }
        else if (strcmp(argv[i],"-openbsd")==0) { is_openbsd=1; is_linux=1; is_macos=0; target_explicit=1; }
        else if (strncmp(argv[i],"-openbsd-libc",13)==0) {
            const char *v=argv[i]+13;
            if (v[0]=='=') v++;
            else if (!v[0] && i+1<argc) v=argv[++i];
            openbsd_libc_override=v;
        }
        else if (strcmp(argv[i],"-dump")==0)    dump=1;
        else if (strcmp(argv[i],"-nodebug")==0) nodebug=1;
        else if (strcmp(argv[i],"-c")==0)       compile_only=1;
        else if (strcmp(argv[i],"-o")==0 && i+1<argc) out_path=argv[++i];
        else if (strncmp(argv[i],"-I",2)==0)  {
            const char *d=argv[i]+2;
            if (!d[0]&&i+1<argc) d=argv[++i];
            if (n_inc<32) include_dirs[n_inc++]=d;
        }
        else if (strncmp(argv[i],"-l",2)==0)  {
            const char *name=argv[i]+2;
            if (!name[0]&&i+1<argc) name=argv[++i];
            if (n_lib<64) lib_flags[n_lib++]=name;
        }
        else if (strncmp(argv[i],"-L",2)==0)  {
            const char *d=argv[i]+2;
            if (!d[0]&&i+1<argc) d=argv[++i];
            if (n_lpath<32) lib_paths[n_lpath++]=d;
        }
        /* A ".sqo" file — a previously-compiled squash object (see
         * objfile.h) — always goes to the link set, regardless of whether
         * src_path is already set, so multiple can be given. */
        else if (argv[i][0]!='-' && strstr(argv[i],".sqo")!=NULL) {
            if (n_obj<64) obj_flags[n_obj++]=argv[i];
        }
        /* Accept .so and .a files as direct library arguments */
        else if (!src_path && (
            (argv[i][0]!= '-') &&
            ( (strstr(argv[i],".so") != NULL) || (strstr(argv[i],".a") != NULL && argv[i][strlen(argv[i])-1]=='a') )
            )) {
            if (n_lib<64) lib_flags[n_lib++]=argv[i];
        }
        else if (!src_path) src_path=argv[i];
        else diag_emit(DIAG_WARNING, -1, NULL, NULL, "unknown argument: %s", argv[i]);
    }
    /* No explicit -linux/-windows/-macos/-openbsd: default to whatever
     * platform THIS squash binary was itself built for, rather than
     * always defaulting to Windows regardless of host. is_linux starts
     * at 0 (Windows) above, so only the two non-Windows cases need to
     * override it here. Deliberately checked via __linux__/__APPLE__
     * (real, target-specific predefines) rather than _WIN32, since
     * squash's own lexer always defines _WIN32 for any non-Linux/non-
     * macOS BUILD TARGET (see lexer.c's target-macro block) — checking
     * it here would make a self-hosted (squash-built-by-squash) compiler
     * built with -windows wrongly look "not Windows" to itself, but
     * checking it would ALSO be wrong the other way for a self-hosted
     * Linux build, since a squash binary built with -linux never defines
     * _WIN32 at all. __linux__/__APPLE__ are unambiguous either way:
     * whichever target flag built THIS binary is exactly the platform it
     * should now default to when invoked without one. */
    if (!target_explicit) {
#if defined(__linux__)
        is_linux = 1; is_macos = 0; is_openbsd = 0;
#elif defined(__APPLE__)
        is_macos = 1; is_linux = 1; is_64bit = 1; is_openbsd = 0;
#endif
        /* else: leave is_linux=0 (Windows), matching the pre-existing default. */
    }
    if (!src_path) {
        printf("Usage: compiler [-32|-64|-arm64] [-linux|-windows|-macos|-openbsd] [-openbsd-libc soname] [-c] [-dump] [-nodebug] [-I dir] [-l lib] [-L path] <source.c> [object.sqo ...] [-o output]\n");
        printf("  (default target platform: whatever this squash binary was itself built for)\n");
        return 1;
    }
    /* macho_builder.c targets Intel (x86-64) macOS only. 32-bit i386 macOS
     * no longer exists (Apple dropped it in 10.15), and arm64 macOS needs
     * mandatory code signing plus chained fixups, neither of which that
     * backend emits — so reject both here rather than writing out a file
     * the loader would refuse with a far less obvious error. */
    if (is_macos && !is_64bit) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL, "-macos is 64-bit only (there is no 32-bit macOS); drop -32");
        return 1;
    }
    if (is_macos && is_arm64) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL, "-macos targets Intel x86-64 only; -arm64 macOS output is not supported");
        return 1;
    }
    if (compile_only && n_obj>0) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL, "-c (compile only) and .sqo link inputs are mutually exclusive");
        return 1;
    }
    /* Windows is LLP64: `long` stays 4 bytes even at 64-bit, unlike Linux's
     * LP64 where it's 8. Must be set before any type-size computation. */
    g_squash_windows_target = !is_linux;
    g_squash_macos_target   = is_macos;
    g_squash_openbsd_target = is_openbsd;
    if (is_openbsd) {
        if (openbsd_libc_override && openbsd_libc_override[0]) {
            g_squash_libc_soname = openbsd_libc_override;
        } else {
            const char *detected = openbsd_detect_libc_soname();
            if (detected) {
                g_squash_libc_soname = detected;
            } else {
                /* Cross-compiling from a non-OpenBSD host: OpenBSD's libc.so
                 * is versioned per-release (e.g. "libc.so.99.1") with no
                 * stable unversioned alias ld.so will accept, so there is no
                 * safe default. Fall back to a placeholder and make the user
                 * supply the real one from the target machine's /usr/lib. */
                g_squash_libc_soname = "libc.so.99.1";
                diag_emit(DIAG_WARNING, -1, NULL, NULL,
                          "-openbsd: cannot detect the target's libc.so version from this host — "
                          "defaulting to a placeholder soname '%s'. Check /usr/lib/libc.so.*.* on "
                          "the actual OpenBSD target and pass the real one via -openbsd-libc <soname>, "
                          "or the resulting binary will fail to dynamically link.",
                          g_squash_libc_soname);
            }
        }
    }

    /* Build LinkerContext for Linux targets with -l flags */
    LinkerContext *linker = NULL;
    WinLinkerContext *winlinker = NULL;
    /* linker.c reads ELF shared objects and System V archives; macOS uses
     * neither. A -macos build resolves its libc through the Mach-O
     * backend's own dylib mapping instead (macho_builder.c's
     * macho_dylib_path), so -l here would silently do nothing — say so
     * rather than letting linker_new() fail to open a .so that isn't there. */
    if (is_macos && n_lib > 0) {
        diag_emit(DIAG_WARNING, -1, NULL, NULL,
                  "-l is ignored for -macos: libc/libm/pthread symbols already resolve through /usr/lib/libSystem.B.dylib, and linking other dylibs is not yet supported");
        n_lib = 0;
    }
    if (is_linux && n_lib > 0) {
        linker = linker_new(is_64bit, is_arm64);
        int li;
        for (li=0; li<n_lpath; li++) linker_add_search_path(linker, lib_paths[li]);
        for (li=0; li<n_lib;   li++) linker_add_library(linker, lib_flags[li]);
    } else if (!is_linux && n_lib > 0) {
        /* Windows linker: resolve .dll/.lib files */
        winlinker = winlinker_new(is_64bit);
        int li;
        for (li=0; li<n_lpath; li++) winlinker_add_search_path(winlinker, lib_paths[li]);
        for (li=0; li<n_lib;   li++) winlinker_add_library(winlinker, lib_flags[li]);
        winlinker_resolve(winlinker);
    }
    char out_buf[512];
    if (!out_path) {
        strncpy(out_buf,src_path,sizeof out_buf-5);
        char *dot=strrchr(out_buf,'.');
        if (dot) *dot='\0';
        if (compile_only) strcat(out_buf,".sqo");
        else if (!is_linux) strcat(out_buf,".exe");
        out_path=out_buf;
    }

    printf("Compiling: %s -> %s (%s, %s) [v96]\n",src_path,out_path,
           is_arm64?"arm64":(is_64bit?"64-bit":"32-bit"),
           is_macos?"macos":(is_openbsd?"openbsd":(is_linux?"linux":"windows")));

    /* Stage 1: Read + preprocess */
    char *raw=read_file(src_path);
    include_dirs[n_inc]=NULL;
    char *src=preprocess(raw,src_path,include_dirs,n_inc,is_linux);
    free(raw);

    if (dump) {
        printf("=== Preprocessed output ===\n%s\n=== End PP ===\n", src);
    }

    /* Stage 2: Lex */
    Lexer lex;
    lexer_init(&lex,src,src_path);

    /* Stage 3: Symbol table */
    SymTable sym;
    symtable_init(&sym,is_64bit);
    sym.is_linux = is_linux;

    /* Stage 4: Parse */
    Parser parser;
    parser_init(&parser,&lex,&sym,src_path);
    ASTNode *prog=parse_program(&parser);
    if (parser.error_count>0) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL,
                  "compilation failed: %d syntax error%s -- no %s written",
                  parser.error_count, parser.error_count==1?"":"s",
                  compile_only ? "object file" : "executable");
        return 1;
    }

    if (dump) {
        printf("\n=== AST ===\n"); ast_print(prog,0);
        printf("\n"); symtable_print(&sym);
    }

    /* Inject Windows linker imports into symtable BEFORE codegen */
    if (winlinker) winlinker_inject_imports(winlinker, &sym);

    /* Pre-scan every ".sqo" input's export-name table before codegen runs,
     * so a bodyless "extern"-declared call can be checked against what
     * these objects ACTUALLY define instead of guessing from n_obj>0 alone
     * (see codegen.h's CodeGen.sqo_export_names comment — n_obj>0 alone
     * caused a genuine external libc call, e.g. write(), to be misrouted
     * as an unresolvable cross-object reference the moment any ".sqo" was
     * being linked in at all, which is exactly what squash self-hosting
     * itself for -macos tripped over). This reads each file a second time
     * (the real objfile_merge() below re-reads them fully after codegen,
     * once their offsets into the merged text are known) — cheap relative
     * to a full compile, and far simpler than threading partially-loaded
     * ObjFiles through the codegen stage. Also validates -64-only and the
     * target tag here, before spending any codegen work on a build that's
     * going to fail anyway (previously only checked after codegen, further
     * below). */
    char **sqo_export_names = NULL; int sqo_export_count = 0;
    if (n_obj > 0) {
        if (!is_64bit) {
            diag_emit(DIAG_ERROR, -1, NULL, NULL, "linking .sqo object files is only supported for -64 builds");
            return 1;
        }
        int cap = 0;
        for (int oi=0; oi<n_obj; oi++) {
            ObjFile peek;
            if (!objfile_read(obj_flags[oi], &peek)) return 1;
            if (!objfile_target_matches(&peek, is_64bit, is_linux, is_arm64)) {
                diag_emit(DIAG_ERROR, -1, NULL, NULL,
                          "'%s' was compiled for a different target (is_64bit=%d is_linux=%d is_arm64=%d) than this build (is_64bit=%d is_linux=%d is_arm64=%d)",
                          obj_flags[oi], peek.is_64bit, peek.is_linux, peek.is_arm64,
                          is_64bit, is_linux, is_arm64);
                return 1;
            }
            if (peek.export_count > 0) {
                cap += peek.export_count;
                sqo_export_names = realloc(sqo_export_names, (size_t)cap * sizeof(char*));
                for (int k=0; k<peek.export_count; k++)
                    sqo_export_names[sqo_export_count++] = my_strdup(peek.export_names[k]);
            }
            objfile_free(&peek);
        }
    }

    /* Stage 5: Code generation */
    /* ARM64 path uses separate assembler and code generator */
    Arm64Asm   a64as;
    CodeGenA64 cg64;
    Assembler  as;
    CodeGen    cg;

    uint8_t *text        = NULL;
    int      text_len    = 0;
    uint8_t *rdata_data  = NULL;
    int      rdata_len   = 0;
    Relocation *relocs   = NULL;
    int      reloc_count = 0;
    char   **str_labels  = NULL;
    int    *str_offsets  = NULL;
    int      str_count   = 0;
    char   **wdata_labels = NULL;
    int    *wdata_offsets = NULL;
    int      wdata_count  = 0;
    int      wdata_pool_size = 0;

    if (is_arm64) {
        a64_asm_init(&a64as, is_linux);
        a64_codegen_init(&cg64, &a64as, &sym, is_linux, linker, n_obj > 0);
        cg64.sqo_export_names = sqo_export_names;
        cg64.sqo_export_count = sqo_export_count;
        a64_codegen_program(&cg64, prog);
        /* An object file (-c) has no single required entry point, exactly
         * like the x86-64 path's identical guard above — see its comment.
         * Pre-existing bug (this call used to be unconditional here), only
         * now actually exercised since ARM64 ".sqo" support (this Phase)
         * made "-c -arm64" on a library file with no main() a real,
         * meaningful case: the resulting spurious "__entry__" reloc was
         * blindly carried through by objfile_merge() (which has no way to
         * know it came from a non-main part) and could silently override
         * the real merged entry point, corrupting the final binary. */
        if (!compile_only) inject_entry_reloc_a64(&a64as, "main");

        text   = a64_codegen_get_text(&cg64, &text_len);
        rdata_data = a64_codegen_get_rdata(&cg64, &rdata_len);
        relocs = a64_codegen_get_relocs(&cg64, &reloc_count);

        /* Float constants live in their own pool (cg64.float_consts) but
         * share the same underlying rdata buffer/offsets as strings — merge
         * them into the same exported list so elf_link_and_write's
         * RELOC_A64_DATA_ADRP/LO12 label lookup (which only searches
         * ebi.string_labels) can find them too. Without this, every double
         * literal's ADRP+ADD stays an unpatched zero-offset placeholder. */
        str_count   = cg64.string_count + cg64.float_const_count;
        str_labels  = malloc(str_count * sizeof(char*));
        str_offsets = malloc(str_count * sizeof(int));
        for (int i=0; i<cg64.string_count; i++) {
            str_labels[i]  = cg64.strings[i].label;
            str_offsets[i] = cg64.strings[i].offset;
        }
        for (int i=0; i<cg64.float_const_count; i++) {
            str_labels[cg64.string_count + i]  = cg64.float_consts[i].label;
            str_offsets[cg64.string_count + i] = cg64.float_consts[i].offset;
        }
        wdata_count     = cg64.wdata_count;
        wdata_pool_size = cg64.wdata_pool_size;
        wdata_labels    = malloc(wdata_count * sizeof(char*));
        wdata_offsets   = malloc(wdata_count * sizeof(int));
        for (int i=0; i<wdata_count; i++) {
            wdata_labels[i]  = cg64.wdata[i].label;
            wdata_offsets[i] = cg64.wdata[i].offset;
        }
    } else {
        asm_init(&as, is_64bit);
        as.is_linux = is_linux;
        codegen_init(&cg, &as, &sym, is_64bit);
        cg.linker = linker;
        /* -c (compile_only) and .sqo linking (n_obj>0) are mutually
         * exclusive (checked earlier), so exactly one of these is true
         * whenever either is -- see codegen.h's sqo_precompile comment for
         * why a "-c" precompile ALSO needs prefer_static_calls: it's what
         * lets a plain bodyless SYM_FUNC prototype (my_strdup, not "extern"
         * -declared) defer to a sibling object not yet compiled, instead of
         * always guessing "libc" for it. */
        cg.prefer_static_calls = compile_only || (n_obj > 0);
        cg.sqo_precompile = compile_only;
        cg.sqo_export_names = sqo_export_names;
        cg.sqo_export_count = sqo_export_count;
        codegen_program(&cg, prog);

        /* An object file (-c) has no single required entry point — that's
         * only meaningful for the final linked executable, which comes from
         * whichever compile actually defines main(). Injecting one here
         * would either be a meaningless no-op (if this file happens not to
         * define main) or, worse, spuriously print the "no function named
         * main" warning for every library-only object compiled this way. */
        if (!compile_only) inject_entry_reloc(&as, "main");

        rdata_data  = codegen_get_rdata(&cg, &rdata_len);
        text        = codegen_get_text(&cg, &text_len);
        relocs      = codegen_get_relocs(&cg, &reloc_count);
        str_count   = cg.string_count;
        str_labels  = malloc(str_count * sizeof(char*));
        str_offsets = malloc(str_count * sizeof(int));
        for (int i=0; i<str_count; i++) {
            str_labels[i]  = cg.strings[i].label;
            str_offsets[i] = cg.strings[i].offset;
        }
        wdata_count     = cg.wdata_count;
        wdata_pool_size = cg.wdata_pool_size;
        wdata_labels    = malloc(wdata_count * sizeof(char*));
        wdata_offsets   = malloc(wdata_count * sizeof(int));
        for (int i=0; i<wdata_count; i++) {
            wdata_labels[i]  = cg.wdata[i].label;
            wdata_offsets[i] = cg.wdata[i].offset;
        }
    }

    if (dump) {
        printf("=== .text (%d bytes) ===\n", text_len);
        for (int i=0; i<text_len; i++) {
            printf("%02X", text[i]);
            if ((i+1)%16==0) printf("\n"); else if ((i+1)%4==0) printf(" ");
        }
        printf("\n");
    }

    /* Function export table: every function this TU actually defines (has a
     * real body), name -> byte offset in `text`. This is what a later
     * "-c"-produced object file's cross-object calls (see codegen.c's
     * asm_call_static comment, and codegen_arm64.c's a64_call_static/
     * RELOC_A64_STATIC_BL for the ARM64 equivalent) get resolved against by
     * objfile_merge(). */
    char **export_names = NULL; int *export_offsets = NULL; int export_count = 0;
    if (!is_arm64) {
        export_names   = malloc((size_t)(cg.func_count>0?cg.func_count:1) * sizeof(char*));
        export_offsets = malloc((size_t)(cg.func_count>0?cg.func_count:1) * sizeof(int));
        for (int i=0;i<cg.func_count;i++) {
            int lbl_off = as.labels[cg.funcs[i].label_id].offset;
            if (lbl_off >= 0) {
                export_names[export_count]   = cg.funcs[i].name;
                export_offsets[export_count] = lbl_off;
                export_count++;
            }
        }
    } else {
        /* ARM64 has no dedicated funcs[] tracking list (see
         * codegen_arm64.h) — derive the export table directly from the
         * AST's top-level function definitions and their resolved label
         * offsets, mirroring inject_entry_reloc_a64()'s own
         * find_func_offset_a64() lookup. */
        int cap = prog->program.count > 0 ? prog->program.count : 1;
        export_names   = malloc((size_t)cap * sizeof(char*));
        export_offsets = malloc((size_t)cap * sizeof(int));
        for (int i=0;i<prog->program.count;i++) {
            ASTNode *d = prog->program.decls[i];
            if (!d || d->kind != AST_FUNC_DECL || !d->func.body) continue;
            int off = find_func_offset_a64(&a64as, d->func.name);
            if (off >= 0) {
                export_names[export_count]   = d->func.name;
                export_offsets[export_count] = off;
                export_count++;
            }
        }
    }

    /* Codegen is designed to keep recovering past a codegen-level error
     * (undefined identifier, break/continue outside a loop, ...) —
     * substituting 0, skipping the bad statement, etc. — so a single
     * build surfaces every real problem at once instead of stopping dead
     * at the first one. But the resulting binary is broken by definition,
     * so once codegen has fully finished (this is the one point every
     * path — arm64 and x86-64, object-file and linked-executable —
     * converges through before any output file gets written), bail out
     * here instead of silently producing a corrupt .exe/.sqo that LOOKS
     * like a successful build. */
    if (diag_error_count() > 0) {
        int ec = diag_error_count();
        diag_emit(DIAG_ERROR, -1, NULL, NULL,
                  "compilation failed: %d error%s - no %s written",
                  ec, ec==1?"":"s", compile_only ? "object file" : "executable");
        free(export_names); free(export_offsets);
        return 1;
    }

    if (compile_only) {
        int ok = objfile_write(out_path, is_64bit, is_linux, is_arm64,
            text, text_len, rdata_data, rdata_len, wdata_pool_size,
            relocs, reloc_count,
            str_labels, str_offsets, str_count,
            wdata_labels, wdata_offsets, wdata_count,
            export_names, export_offsets, export_count,
            sym.imports, sym.import_count);
        if (ok) printf("Object written: %s (%d exported function%s)\n",
                        out_path, export_count, export_count==1?"":"s");
        free(export_names); free(export_offsets);
        free(src);
        free(str_labels); free(str_offsets);
        free(wdata_labels); free(wdata_offsets);
        if (rdata_data && !is_arm64) free(rdata_data);
        ast_free(prog);
        /* `as` (x86-64 Assembler) is never initialized on the ARM64 path
         * (only `a64as`/`cg64` are — see the is_arm64 branch above), so
         * freeing it unconditionally here would free uninitialized stack
         * garbage. Pre-existing bug, just never reached before ARM64 ".sqo"
         * support (this Phase) made "-c -arm64" a real, exercised path —
         * mirrors the correct is_arm64 dispatch already used at this
         * function's normal (non "-c") exit below. */
        if (is_arm64) a64_asm_free(&a64as);
        else          asm_free(&as);
        if (linker) linker_free(linker);
        if (winlinker) winlinker_free(winlinker);
        return ok ? 0 : 1;
    }

    /* Link in any ".sqo" object files given on the command line — merge
     * their text/rdata/wdata into this compile's own output and resolve
     * cross-object function calls by name (see objfile_merge()'s own
     * comment for exactly what this does and doesn't handle). Supported on
     * Windows/PE and Linux/ELF, both x86-64 and ARM64 (see codegen_arm64.c's
     * a64_call_static/RELOC_A64_STATIC_BL and objfile_merge()'s matching
     * patch case) — 32-bit only is unsupported. */
    MergedOutput merged; int have_merged = 0;
    ObjFile *loaded_objs = NULL;
    if (n_obj > 0) {
        if (!is_64bit) {
            diag_emit(DIAG_ERROR, -1, NULL, NULL, "linking .sqo object files is only supported for -64 builds");
            return 1;
        }
        loaded_objs = calloc((size_t)n_obj, sizeof(ObjFile));
        for (int oi=0; oi<n_obj; oi++) {
            if (!objfile_read(obj_flags[oi], &loaded_objs[oi])) return 1;
            if (!objfile_target_matches(&loaded_objs[oi], is_64bit, is_linux, is_arm64)) {
                diag_emit(DIAG_ERROR, -1, NULL, NULL,
                          "'%s' was compiled for a different target (is_64bit=%d is_linux=%d is_arm64=%d) than this build (is_64bit=%d is_linux=%d is_arm64=%d)",
                          obj_flags[oi], loaded_objs[oi].is_64bit, loaded_objs[oi].is_linux, loaded_objs[oi].is_arm64,
                          is_64bit, is_linux, is_arm64);
                return 1;
            }
        }
        objfile_merge(text, text_len, rdata_data, rdata_len, wdata_pool_size,
            relocs, reloc_count,
            str_labels, str_offsets, str_count,
            wdata_labels, wdata_offsets, wdata_count,
            export_names, export_offsets, export_count,
            sym.imports, sym.import_count,
            loaded_objs, n_obj, &merged);
        have_merged = 1;
        printf("Linked %d object file%s (merged .text: %d bytes)\n",
               n_obj, n_obj==1?"":"s", merged.text_len);
        if (getenv("SQUASH_FUNCADDR_DEBUG")) {
            int base = text_len;
            for (int oi=0; oi<n_obj; oi++) {
                fprintf(stderr, "[funcaddr-objbase] obj=%s text_base_rva=0x%x\n",
                        obj_flags[oi], base + 0x1000);
                base += loaded_objs[oi].text_len;
            }
        }

        if (merged.static_init_count > 0) {
            /* The __entry__ reloc (injected by inject_entry_reloc() at
             * compile time, before merge) currently points straight at
             * main(). Splice in a tiny stub that calls every linked
             * object's "__sqx_static_init" function first — see
             * objfile.h's MergedOutput.static_init_offsets comment for why
             * those exist: a library object's global/static function-
             * pointer initializers (e.g. SDL_malloc.c's own "static struct
             * {...} s_mem = { real_malloc, ... };") have no main() of
             * their own to run inside, and without this they stay
             * permanently zeroed — calling through a null function pointer
             * the first time anything in that object uses them. */
            int entry_reloc_idx = -1;
            for (int ri=0; ri<merged.reloc_count; ri++) {
                if (merged.relocs[ri].symbol && strcmp(merged.relocs[ri].symbol,"__entry__")==0) {
                    entry_reloc_idx = ri;
                    break;
                }
            }
            if (entry_reloc_idx < 0) {
                diag_emit(DIAG_ERROR, -1, NULL, NULL, "internal: no __entry__ relocation found while splicing static initializers");
                return 1;
            }
            int main_offset = merged.relocs[entry_reloc_idx].addend;
            int stub_size = 5 * (merged.static_init_count + 1); /* N calls + 1 jmp */
            uint8_t *new_text = malloc((size_t)merged.text_len + (size_t)stub_size);
            memcpy(new_text, merged.text, (size_t)merged.text_len);
            free(merged.text);
            merged.text = new_text;
            int stub_offset = merged.text_len;
            int cursor = stub_offset;
            for (int si=0; si<merged.static_init_count; si++) {
                int32_t disp = (int32_t)(merged.static_init_offsets[si] - (cursor + 5));
                merged.text[cursor+0] = 0xE8;
                merged.text[cursor+1] = (uint8_t)(disp);
                merged.text[cursor+2] = (uint8_t)(disp>>8);
                merged.text[cursor+3] = (uint8_t)(disp>>16);
                merged.text[cursor+4] = (uint8_t)(disp>>24);
                cursor += 5;
            }
            {
                int32_t disp = (int32_t)(main_offset - (cursor + 5));
                merged.text[cursor+0] = 0xE9;
                merged.text[cursor+1] = (uint8_t)(disp);
                merged.text[cursor+2] = (uint8_t)(disp>>8);
                merged.text[cursor+3] = (uint8_t)(disp>>16);
                merged.text[cursor+4] = (uint8_t)(disp>>24);
                cursor += 5;
            }
            merged.text_len += stub_size;
            merged.relocs[entry_reloc_idx].addend = stub_offset;
            printf("Spliced %d static-initializer call%s before main (stub at text offset %d)\n",
                   merged.static_init_count, merged.static_init_count==1?"":"s", stub_offset);
        }
    }
    free(export_names); free(export_offsets);

    /* Diagnostic: list every ".sqo"/".so"/".a" input file being linked into
     * this ELF build and the functions/symbols it exports — analogous to
     * the "-c" path's own "Object written: ... (%d exported functions)"
     * message, but per input file at link time instead of at object-write
     * time, and covering all three kinds of linkable input, not just
     * ".sqo". Linux/ELF only (matches the user-facing ask); Windows's
     * winlinker.c has no equivalent listing today. */
    if (is_linux && (n_obj > 0 || linker)) {
        int n_libs = linker ? linker_lib_count(linker) : 0;
        printf("Linking %d input file%s:\n", n_obj + n_libs, (n_obj + n_libs)==1?"":"s");
        for (int oi=0; oi<n_obj; oi++) {
            printf("  %-30s (sqo)     exports:", obj_flags[oi]);
            if (loaded_objs[oi].export_count == 0) printf(" (none)");
            for (int k=0;k<loaded_objs[oi].export_count;k++)
                printf(" %s%s", loaded_objs[oi].export_names[k], k+1<loaded_objs[oi].export_count?",":"");
            printf("\n");
        }
        for (int li=0; li<n_libs; li++) {
            int is_static = linker_lib_is_static(linker, li);
            const char *path = linker_lib_path(linker, li);
            if (is_static) {
                printf("  %-30s (static)  exports:", path);
                int any = 0;
                int nas = linker_ar_sym_count(linker);
                for (int k=0;k<nas;k++) {
                    if (strcmp(linker_ar_sym_file(linker,k), path)==0) {
                        printf(" %s", linker_ar_sym_name(linker,k));
                        any = 1;
                    }
                }
                if (!any) printf(" (none)");
                printf("\n");
            } else {
                char hdr[400];
                snprintf(hdr, sizeof hdr, "%s (shared, soname=%s)", path, linker_lib_soname(linker, li));
                printf("  %-30s exports:", hdr);
                int any = 0;
                int nse = linker_so_export_count(linker);
                for (int k=0;k<nse;k++) {
                    if (linker_so_export_lib(linker,k) == li) {
                        printf(" %s", linker_so_export_name(linker,k));
                        any = 1;
                    }
                }
                if (!any) printf(" (none)");
                printf("\n");
            }
        }
    }

    int rc = 0;

    if (is_macos) {
        /* Stage 7c: Mach-O build + link (Intel macOS).
         * Field-for-field the same wiring as the ELF branch below — macOS
         * shares Linux's SysV/LP64 codegen entirely, so only the container
         * differs (see macho_builder.h). The two differences worth noting:
         * there is no linker context (linker.c is ELF-only, and -l was
         * already rejected above), and no entry-point stub is prepended —
         * LC_MAIN lets libSystem call main() directly. */
        MachOBuildInput mbi; memset(&mbi,0,sizeof mbi);
        mbi.text              = have_merged ? merged.text          : text;
        mbi.text_len          = have_merged ? merged.text_len      : text_len;
        mbi.rdata_strings     = have_merged ? merged.rdata         : rdata_data;
        mbi.rdata_strings_len = have_merged ? merged.rdata_len     : rdata_len;
        mbi.relocs            = have_merged ? merged.relocs        : relocs;
        mbi.reloc_count       = have_merged ? merged.reloc_count   : reloc_count;
        mbi.string_labels     = have_merged ? merged.str_labels    : str_labels;
        mbi.string_offsets    = have_merged ? merged.str_offsets   : str_offsets;
        mbi.string_count      = have_merged ? merged.str_count     : str_count;
        int mbi_wdata_size    = have_merged ? merged.wdata_pool_size : wdata_pool_size;
        mbi.wdata_bytes       = calloc((size_t)mbi_wdata_size + 1, 1);
        mbi.wdata_len         = mbi_wdata_size;
        mbi.wdata_labels      = have_merged ? merged.wdata_labels  : wdata_labels;
        mbi.wdata_offsets     = have_merged ? merged.wdata_offsets : wdata_offsets;
        mbi.wdata_count       = have_merged ? merged.wdata_count   : wdata_count;
        /* Real initial bytes for static-local const tables — see the
         * identical ELF-path comment below for the .sqo-merge limitation. */
        if (!have_merged) {
            for (int wi=0; wi<wdata_count; wi++) {
                if (cg.wdata[wi].content)
                    memcpy(mbi.wdata_bytes + wdata_offsets[wi], cg.wdata[wi].content, (size_t)cg.wdata[wi].size);
            }
        }
        mbi.import_specs      = have_merged ? merged.import_specs  : sym.imports;
        mbi.import_count      = have_merged ? merged.import_count  : sym.import_count;
        mbi.entry_func        = "main";
        mbi.output_path       = out_path;
        mbi.as_               = NULL;
        mbi.linker            = NULL;
        rc = macho_link_and_write(&mbi);
        if (mbi.wdata_bytes) free(mbi.wdata_bytes);
    } else if (is_linux) {
        /* Stage 7a: ELF build + link */
        ELFBuildInput ebi; memset(&ebi,0,sizeof ebi);
        ebi.is_64bit          = is_64bit;
        ebi.text              = have_merged ? merged.text          : text;
        ebi.text_len          = have_merged ? merged.text_len      : text_len;
        ebi.rdata_strings     = have_merged ? merged.rdata         : rdata_data;
        ebi.rdata_strings_len = have_merged ? merged.rdata_len     : rdata_len;
        ebi.relocs            = have_merged ? merged.relocs        : relocs;
        ebi.reloc_count       = have_merged ? merged.reloc_count   : reloc_count;
        ebi.is_arm64          = is_arm64;
        ebi.is_openbsd        = is_openbsd;
        ebi.strip_debug_sections = nodebug;
        ebi.string_labels     = have_merged ? merged.str_labels    : str_labels;
        ebi.string_offsets    = have_merged ? merged.str_offsets   : str_offsets;
        ebi.string_count      = have_merged ? merged.str_count     : str_count;
        int ebi_wdata_size    = have_merged ? merged.wdata_pool_size : wdata_pool_size;
        ebi.wdata_bytes       = calloc((size_t)ebi_wdata_size + 1, 1);
        ebi.wdata_len         = ebi_wdata_size;
        ebi.wdata_labels      = have_merged ? merged.wdata_labels  : wdata_labels;
        ebi.wdata_offsets     = have_merged ? merged.wdata_offsets : wdata_offsets;
        ebi.wdata_count       = have_merged ? merged.wdata_count   : wdata_count;
        /* Copy real initial bytes for any wdata entry that has them (e.g. a
         * "static const T x[] = {...};" local with a compile-time-constant
         * initializer — see codegen.c's WDataEntry.content) — everything
         * else stays zero-filled as before. Only meaningful for a direct,
         * non-object-file compile: the .sqo split-compile path
         * (objfile_write/objfile_merge) doesn't currently serialize this
         * content, so a static-local const table compiled into a separately
         * cached .sqo object still falls back to zero-fill — a known,
         * narrower follow-up, not addressed here. */
        if (!is_arm64 && !have_merged) {
            for (int wi=0; wi<wdata_count; wi++) {
                if (cg.wdata[wi].content)
                    memcpy(ebi.wdata_bytes + wdata_offsets[wi], cg.wdata[wi].content, (size_t)cg.wdata[wi].size);
            }
        }
        ebi.import_specs      = have_merged ? merged.import_specs  : sym.imports;
        ebi.import_count      = have_merged ? merged.import_count  : sym.import_count;
        ebi.entry_func        = "main";
        ebi.output_path       = out_path;
        /* Skipped when merging object files: this patches `as.code` in place
         * using `as.labels[]`, but the merged build uses a separate,
         * concatenated text buffer (`merged.text`) with different offsets
         * instead — see the identical PE-branch comment below. */
        ebi.as_               = (is_arm64 || have_merged) ? NULL : &as;
        ebi.linker            = linker;
        rc = elf_link_and_write(&ebi);
        if (ebi.wdata_bytes) free(ebi.wdata_bytes);
    } else {
        /* Stage 7b: PE build + link */
        /* Resolve RELOC_TEXT_ABS32 (function pointer addresses in 32-bit mode).
         * Skipped when merging object files: this patches `as.code` in
         * place using `as.labels[]`, but the merged build uses a separate,
         * concatenated text buffer (`merged.text`) instead — see
         * objfile.h's scope note (32-bit function-pointer-as-data patterns
         * aren't supported through the object-file path; not needed for
         * the 64-bit-only use case this was built for). */
        if (!is_arm64 && !have_merged) {
            uint32_t text_rva_val   = 0x1000;
            uint64_t image_base_val = is_64bit ? 0x140000000ULL : 0x00400000ULL;
            asm_resolve_text_relocs(&as, text_rva_val, image_base_val);
        }

        PEBuildInput pbi; memset(&pbi,0,sizeof pbi);
        pbi.is_64bit          = is_64bit;
        pbi.is_arm64          = is_arm64;
        pbi.text              = have_merged ? merged.text          : text;
        pbi.text_len          = have_merged ? merged.text_len      : text_len;
        pbi.rdata_strings     = have_merged ? merged.rdata         : rdata_data;
        pbi.rdata_strings_len = have_merged ? merged.rdata_len     : rdata_len;
        pbi.relocs            = have_merged ? merged.relocs        : relocs;
        pbi.reloc_count       = have_merged ? merged.reloc_count   : reloc_count;
        pbi.string_labels     = have_merged ? merged.str_labels    : str_labels;
        pbi.string_offsets    = have_merged ? merged.str_offsets   : str_offsets;
        pbi.string_count      = have_merged ? merged.str_count     : str_count;
        int pbi_wdata_size    = have_merged ? merged.wdata_pool_size : wdata_pool_size;
        pbi.wdata_bytes       = calloc((size_t)pbi_wdata_size + 1, 1);
        pbi.wdata_len         = pbi_wdata_size;
        pbi.wdata_labels      = have_merged ? merged.wdata_labels  : wdata_labels;
        pbi.wdata_offsets     = have_merged ? merged.wdata_offsets : wdata_offsets;
        pbi.wdata_count       = have_merged ? merged.wdata_count   : wdata_count;
        /* Copy real initial bytes for any wdata entry that has them — see
         * the matching ELF-path comment above. Same .sqo-merge limitation:
         * only the direct (non-object-file) compile path is covered. */
        if (!is_arm64 && !have_merged) {
            for (int wi=0; wi<wdata_count; wi++) {
                if (cg.wdata[wi].content)
                    memcpy(pbi.wdata_bytes + wdata_offsets[wi], cg.wdata[wi].content, (size_t)cg.wdata[wi].size);
            }
        }
        pbi.import_specs      = have_merged ? merged.import_specs  : sym.imports;
        pbi.import_count      = have_merged ? merged.import_count  : sym.import_count;
        pbi.entry_func        = "main";
        pbi.output_path       = out_path;
        rc = pe_link_and_write(&pbi);
        if (pbi.wdata_bytes) free(pbi.wdata_bytes);
    }

    if (have_merged) {
        merged_output_free(&merged);
        for (int oi=0; oi<n_obj; oi++) objfile_free(&loaded_objs[oi]);
        free(loaded_objs);
    }

    free(src);
    free(str_labels); free(str_offsets);
    free(wdata_labels); free(wdata_offsets);
    if (rdata_data && !is_arm64) free(rdata_data);
    ast_free(prog);
    if (is_arm64) a64_asm_free(&a64as);
    else          asm_free(&as);
    if (linker) linker_free(linker);
    if (winlinker) winlinker_free(winlinker);
    return rc;
}
