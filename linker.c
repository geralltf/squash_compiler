#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#include "linker.h"
#include "diag.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */
/* Forward-declare the shared my_strdup (defined in ast.c, included before us) */
char *my_strdup(const char *src);

static int str_ends_with(const char *s, const char *suf) {
    int sl = 0; while (s[sl]) sl++;
    int fl = 0; while (suf[fl]) fl++;
    if (fl > sl) return 0;
    return strcmp(s + sl - fl, suf) == 0;
}

/* fopen() alone isn't enough: on Linux it happily opens a directory as a
 * stream (reads from it just fail later), so a library search path that
 * happens to contain a same-named directory — e.g. "-lX11" matching
 * /usr/lib/x86_64-linux-gnu/X11, a real driver-modules directory on some
 * systems — got treated as a "found" library. read_file_buf then called
 * ftell() on that directory stream (undefined, glibc returns LONG_MAX),
 * malloc(LONG_MAX+1) failed, and writing to the NULL result segfaulted.
 * Require a regular file, matching what "found a library" should mean. */
static int file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

/* find_lib()'s unversioned "lib<name>.so" candidate (below) can match a
 * real installed FILE that isn't actually a shared object at all: modern
 * glibc packaging ships some libs' dev-time "lib<name>.so" as a plain-text
 * GNU ld linker script (e.g. Ubuntu's own /usr/lib/x86_64-linux-gnu/
 * libm.so is literally "GROUP ( libm.so.6 ... )", not an ELF), meant to be
 * understood by a real linker's script parser -- which this one doesn't
 * have. Accepting it as-is produces a DT_NEEDED entry pointing at that
 * non-ELF text file, which the dynamic loader then fails to mmap at
 * runtime ("invalid ELF header") -- confirmed as the actual cause of a
 * real "-lm" link (SQW's JPEG decoder's cos() calls) producing a binary
 * that built without error but crashed on first run. Checking the real
 * ELF magic here, rather than just file_exists(), makes find_lib() reject
 * the linker-script stub and fall through to the later "lib<name>.so.N"
 * versioned-candidate loop, which finds the real libm.so.6 ELF instead. */
static int file_is_elf(const char *path) {
    if (!file_exists(path)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char magic[4];
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    return n == 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static uint8_t *read_file_buf(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    /* Defensive: ftell() on a non-regular-file stream (e.g. a directory
     * opened via fopen, which glibc permits) returns -1 or LONG_MAX rather
     * than failing outright, which used to flow straight into
     * malloc(sz+1)/fread(...,sz,...) and crash on the NULL result. file_exists()
     * now filters directories out before a path ever reaches here, but keep
     * this as a last-resort guard against any other odd file type. */
    if (sz < 0 || sz > 0x7fffffffL) { fclose(f); return NULL; }
    uint8_t *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = 0;
    *out_len = (int)sz;
    return buf;
}

/* Little-endian readers */
static uint16_t rd16(const uint8_t *b, int off) {
    return (uint16_t)((uint32_t)b[off] | ((uint32_t)b[off+1]<<8));
}
static uint32_t rd32(const uint8_t *b, int off) {
    return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) |
           ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
}
static int32_t rd32s(const uint8_t *b, int off) { return (int32_t)rd32(b,off); }
static uint32_t rd32_be(const uint8_t *b, int off) {
    return ((uint32_t)b[off]<<24)|((uint32_t)b[off+1]<<16)|
           ((uint32_t)b[off+2]<<8)|(uint32_t)b[off+3];
}

/* Read 64-bit value as two 32-bit halves (squash 32-bit compatible) */
static uint32_t rd64lo(const uint8_t *b, int off) { return rd32(b, off); }
static uint32_t rd64hi(const uint8_t *b, int off) { return rd32(b, off+4); }

static void wr32(uint8_t *b, int off, uint32_t v) {
    b[off]=(uint8_t)v; b[off+1]=(uint8_t)(v>>8);
    b[off+2]=(uint8_t)(v>>16); b[off+3]=(uint8_t)(v>>24);
}

/* Copy a string into a flat char array slot (stride bytes per slot) */
static void lstr_copy(char *flat, int idx, int stride, const char *src, int max) {
    int base = idx * stride;
    int k = 0;
    while (src[k] && k < max) { flat[base+k] = src[k]; k++; }
    flat[base+k] = '\0';
}

/* =========================================================================
 * LinkerContext lifecycle
 * ========================================================================= */
LinkerContext *linker_new(int is_64bit, int is_arm64) {
    LinkerContext *ctx = (LinkerContext *)calloc(1, sizeof(LinkerContext));
    ctx->is_64bit = is_64bit;
    ctx->is_arm64 = is_arm64;

    ctx->static_text_cap = 65536;
    ctx->static_text = (uint8_t *)calloc(ctx->static_text_cap, 1);

    ctx->sreloc_cap = 256;
    ctx->srelocs = (StaticReloc *)calloc(ctx->sreloc_cap, sizeof(StaticReloc));

    ctx->static_rdata_cap = 65536;
    ctx->static_rdata = (uint8_t *)calloc(ctx->static_rdata_cap, 1);
    return ctx;
}

void linker_free(LinkerContext *ctx) {
    if (!ctx) return;
    if (ctx->static_text) free(ctx->static_text);
    if (ctx->srelocs)     free(ctx->srelocs);
    if (ctx->static_rdata) free(ctx->static_rdata);
    free(ctx);
}

/* =========================================================================
 * Default search paths
 * ========================================================================= */
static void add_default_paths(LinkerContext *ctx) {
    /* Debian/Ubuntu multiarch triplet directories differ per target CPU —
     * a squash `-arm64` build previously always searched the x86_64 ones
     * (baked in regardless of target), so -lGL/-lX11 etc. silently found
     * nothing on an arm64 target even when the aarch64 .so files were
     * actually installed at /usr/lib/aarch64-linux-gnu. */
    static const char *p_arm64[] = {
        "/usr/lib/aarch64-linux-gnu",
        "/usr/lib",
        "/usr/local/lib",
        "/lib/aarch64-linux-gnu",
        "/lib",
        NULL
    };
    static const char *p64[] = {
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib",
        "/usr/local/lib",
        "/lib/x86_64-linux-gnu",
        "/lib",
        NULL
    };
    static const char *p32[] = {
        "/usr/lib/i386-linux-gnu",
        "/usr/lib32",
        "/lib/i386-linux-gnu",
        "/lib32",
        NULL
    };
    const char **paths = ctx->is_arm64 ? p_arm64 : (ctx->is_64bit ? p64 : p32);
    int i = 0;
    while (paths[i] && ctx->search_count < LINKER_MAX_SEARCH) {
        lstr_copy(ctx->search, ctx->search_count, LSTR256, paths[i], 255);
        ctx->search_count++;
        i++;
    }
}

void linker_add_search_path(LinkerContext *ctx, const char *path) {
    if (ctx->search_count >= LINKER_MAX_SEARCH) return;
    lstr_copy(ctx->search, ctx->search_count, LSTR256, path, 255);
    ctx->search_count++;
}

/* =========================================================================
 * Find a library file by name in search paths.
 * Tries: libNAME.so*, libNAME.a
 * Returns 1 on success, sets path[256] and is_static.
 * ========================================================================= */
static int find_lib(LinkerContext *ctx, const char *name, char *out_path, int *out_static) {
    /* Accept full path directly */
    if (name[0] == '/') {
        if (file_exists(name)) {
            int i = 0; while (name[i] && i < 255) { out_path[i]=name[i]; i++; }
            out_path[i] = '\0';
            *out_static = str_ends_with(name, ".a");
            return 1;
        }
        return 0;
    }

    /* Make sure default paths are loaded on first call */
    if (ctx->search_count == 0) add_default_paths(ctx);

    char try_path[512];
    int si;

    /* Try .so variants first, then .a */
    for (si = 0; si < ctx->search_count; si++) {
        int sbase = si * LSTR256;
        char *sp = &ctx->search[sbase];
        /* Exact name (e.g. "libGL.so.1") -- a ".a" static archive is a
         * valid exact-name match too (not an ELF), so only require the
         * ELF check for a ".so"-named exact match. */
        snprintf(try_path, sizeof try_path, "%s/%s", sp, name);
        if (str_ends_with(try_path, ".a") ? file_exists(try_path) : file_is_elf(try_path)) {
            int i=0; while(try_path[i]&&i<255){out_path[i]=try_path[i];i++;}out_path[i]='\0';
            *out_static = str_ends_with(try_path, ".a");
            return 1;
        }
        /* lib<name>.so -- see file_is_elf()'s own comment on why this must
         * be a real ELF check, not just file_exists(). */
        snprintf(try_path, sizeof try_path, "%s/lib%s.so", sp, name);
        if (file_is_elf(try_path)) {
            int i=0; while(try_path[i]&&i<255){out_path[i]=try_path[i];i++;}out_path[i]='\0';
            *out_static = 0;
            return 1;
        }
        /* lib<name>.so.0 / .1 / .2 / .6 */
        { int v; for (v=0; v<=9; v++) {
            snprintf(try_path, sizeof try_path, "%s/lib%s.so.%d", sp, name, v);
            if (file_is_elf(try_path)) {
                int i=0; while(try_path[i]&&i<255){out_path[i]=try_path[i];i++;}out_path[i]='\0';
                *out_static = 0;
                return 1;
            }
        }}
    }

    /* Now try .a */
    for (si = 0; si < ctx->search_count; si++) {
        int sbase = si * LSTR256;
        snprintf(try_path, sizeof try_path, "%s/lib%s.a", &ctx->search[sbase], name);
        if (file_exists(try_path)) {
            int i=0; while(try_path[i]&&i<255){out_path[i]=try_path[i];i++;}out_path[i]='\0';
            *out_static = 1;
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 * Read SONAME from a .so ELF's .dynamic section
 * ========================================================================= */
static void read_soname(const uint8_t *data, int data_len, char *out_soname, int soname_cap) {
    out_soname[0] = '\0';
    if (data_len < 64) return;
    if (data[0]!=0x7f||data[1]!='E'||data[2]!='L'||data[3]!='F') return;
    int is64 = (data[4] == 2);

    /* Find .dynamic and .dynstr using section headers */
    int shoff, shentsize, shnum;
    if (is64) {
        shoff     = (int)rd64lo(data, 40);
        shentsize = rd16(data, 58);
        shnum     = rd16(data, 60);
    } else {
        shoff     = (int)rd32(data, 32);
        shentsize = rd16(data, 46);
        shnum     = rd16(data, 48);
    }
    if (shoff <= 0 || shentsize <= 0 || shnum <= 0) return;

    /* Find the .dynamic section */
    int dyn_off = -1, dyn_size = -1, dyn_link = -1;
    int i;
    for (i = 0; i < shnum; i++) {
        int shbase = shoff + i * shentsize;
        if (shbase + shentsize > data_len) break;
        uint32_t sh_type;
        int      sh_offset, sh_size, sh_link;
        if (is64) {
            sh_type   = rd32(data, shbase + 4);
            sh_offset = (int)rd64lo(data, shbase + 24);
            sh_size   = (int)rd64lo(data, shbase + 32);
            sh_link   = (int)rd32(data, shbase + 40);
        } else {
            sh_type   = rd32(data, shbase + 4);
            sh_offset = (int)rd32(data, shbase + 16);
            sh_size   = (int)rd32(data, shbase + 20);
            sh_link   = (int)rd32(data, shbase + 24);
        }
        if (sh_type == 6) { /* SHT_DYNAMIC */
            dyn_off = sh_offset; dyn_size = sh_size; dyn_link = sh_link;
            break;
        }
    }
    if (dyn_off < 0) return;

    /* Find the linked strtab */
    int strtab_off = -1;
    if (dyn_link > 0 && dyn_link < shnum) {
        int shbase = shoff + dyn_link * shentsize;
        if (is64) strtab_off = (int)rd64lo(data, shbase + 24);
        else      strtab_off = (int)rd32(data, shbase + 16);
    }
    if (strtab_off < 0) return;

    /* Scan .dynamic for DT_SONAME (tag=14) */
    int entry_size = is64 ? 16 : 8;
    int e;
    for (e = 0; e + entry_size <= dyn_size; e += entry_size) {
        int eoff = dyn_off + e;
        if (eoff + entry_size > data_len) break;
        int tag, val;
        if (is64) {
            tag = (int)rd64lo(data, eoff);
            val = (int)rd64lo(data, eoff + 8);
        } else {
            tag = (int)rd32(data, eoff);
            val = (int)rd32(data, eoff + 4);
        }
        if (tag == 0) break; /* DT_NULL */
        if (tag == 14) { /* DT_SONAME */
            int soff = strtab_off + val;
            int k = 0;
            while (soff + k < data_len && data[soff+k] && k < soname_cap-1) {
                out_soname[k] = (char)data[soff+k]; k++;
            }
            out_soname[k] = '\0';
            return;
        }
    }
}

/* =========================================================================
 * Search the .dynsym of a loaded .so for a function name.
 * Returns 1 if found and defined (st_shndx != SHN_UNDEF).
 * ========================================================================= */
static int so_has_func(const uint8_t *data, int data_len, const char *funcname) {
    if (data_len < 64) return 0;
    if (data[0]!=0x7f||data[1]!='E'||data[2]!='L'||data[3]!='F') return 0;
    int is64 = (data[4] == 2);

    int shoff, shentsize, shnum;
    if (is64) {
        shoff     = (int)rd64lo(data, 40);
        shentsize = rd16(data, 58);
        shnum     = rd16(data, 60);
    } else {
        shoff     = (int)rd32(data, 32);
        shentsize = rd16(data, 46);
        shnum     = rd16(data, 48);
    }
    if (shoff<=0 || shentsize<=0 || shnum<=0) return 0;

    /* Find .dynsym (SHT_DYNSYM = 11) and its linked .dynstr */
    int dynsym_off=-1, dynsym_sz=-1, dynsym_ent=-1, strtab_off=-1;
    int i;
    for (i = 0; i < shnum; i++) {
        int shbase = shoff + i * shentsize;
        if (shbase + shentsize > data_len) break;
        uint32_t sh_type;
        int sh_offset, sh_size, sh_link, sh_entsize;
        if (is64) {
            sh_type    = rd32(data, shbase + 4);
            sh_offset  = (int)rd64lo(data, shbase + 24);
            sh_size    = (int)rd64lo(data, shbase + 32);
            sh_link    = (int)rd32(data, shbase + 40);
            sh_entsize = (int)rd64lo(data, shbase + 56);
        } else {
            sh_type    = rd32(data, shbase + 4);
            sh_offset  = (int)rd32(data, shbase + 16);
            sh_size    = (int)rd32(data, shbase + 20);
            sh_link    = (int)rd32(data, shbase + 24);
            sh_entsize = (int)rd32(data, shbase + 36);
        }
        if (sh_type == 11 && dynsym_off < 0) { /* SHT_DYNSYM */
            dynsym_off = sh_offset;
            dynsym_sz  = sh_size;
            dynsym_ent = sh_entsize > 0 ? sh_entsize : (is64 ? 24 : 16);
            /* Find strtab */
            if (sh_link > 0 && sh_link < shnum) {
                int sbase = shoff + sh_link * shentsize;
                if (is64) strtab_off = (int)rd64lo(data, sbase + 24);
                else      strtab_off = (int)rd32(data, sbase + 16);
            }
        }
    }
    if (dynsym_off < 0 || strtab_off < 0) return 0;

    int nent = dynsym_sz / dynsym_ent;
    int namelen = 0; while (funcname[namelen]) namelen++;
    for (i = 0; i < nent; i++) {
        int eoff = dynsym_off + i * dynsym_ent;
        if (eoff + dynsym_ent > data_len) break;
        uint32_t st_name;
        uint16_t st_shndx;
        uint8_t  st_info;
        if (is64) {
            st_name  = rd32(data, eoff);
            st_info  = data[eoff + 4];
            st_shndx = rd16(data, eoff + 6);
        } else {
            st_name  = rd32(data, eoff);
            st_shndx = rd16(data, eoff + 14);
            st_info  = data[eoff + 12];
        }
        if (st_shndx == 0) continue; /* SHN_UNDEF — symbol is not defined here */
        /* Check name */
        int noff = strtab_off + (int)st_name;
        if (noff < 0 || noff >= data_len) continue;
        int k = 0;
        while (k < namelen && noff+k < data_len && data[noff+k] == (uint8_t)funcname[k]) k++;
        if (k == namelen && (noff+k >= data_len || data[noff+k] == 0))
            return 1;
        (void)st_info;
    }
    return 0;
}

/* =========================================================================
 * Enumerate every defined (non-SHN_UNDEF) FUNC/OBJECT symbol in a ".so"'s
 * .dynsym, tagging each with `lib_idx` — mirrors so_has_func()'s section/
 * symbol-table walk but collects every name instead of matching one.
 * Diagnostic-only (see linker.h's so_exp_name/so_exp_lib comment).
 * ========================================================================= */
static void so_list_exports(LinkerContext *ctx, int lib_idx, const uint8_t *data, int data_len) {
    if (data_len < 64) return;
    if (data[0]!=0x7f||data[1]!='E'||data[2]!='L'||data[3]!='F') return;
    int is64 = (data[4] == 2);

    int shoff, shentsize, shnum;
    if (is64) {
        shoff     = (int)rd64lo(data, 40);
        shentsize = rd16(data, 58);
        shnum     = rd16(data, 60);
    } else {
        shoff     = (int)rd32(data, 32);
        shentsize = rd16(data, 46);
        shnum     = rd16(data, 48);
    }
    if (shoff<=0 || shentsize<=0 || shnum<=0) return;

    int dynsym_off=-1, dynsym_sz=-1, dynsym_ent=-1, strtab_off=-1;
    int i;
    for (i = 0; i < shnum; i++) {
        int shbase = shoff + i * shentsize;
        if (shbase + shentsize > data_len) break;
        uint32_t sh_type;
        int sh_offset, sh_size, sh_link, sh_entsize;
        if (is64) {
            sh_type    = rd32(data, shbase + 4);
            sh_offset  = (int)rd64lo(data, shbase + 24);
            sh_size    = (int)rd64lo(data, shbase + 32);
            sh_link    = (int)rd32(data, shbase + 40);
            sh_entsize = (int)rd64lo(data, shbase + 56);
        } else {
            sh_type    = rd32(data, shbase + 4);
            sh_offset  = (int)rd32(data, shbase + 16);
            sh_size    = (int)rd32(data, shbase + 20);
            sh_link    = (int)rd32(data, shbase + 24);
            sh_entsize = (int)rd32(data, shbase + 36);
        }
        if (sh_type == 11 && dynsym_off < 0) { /* SHT_DYNSYM */
            dynsym_off = sh_offset;
            dynsym_sz  = sh_size;
            dynsym_ent = sh_entsize > 0 ? sh_entsize : (is64 ? 24 : 16);
            if (sh_link > 0 && sh_link < shnum) {
                int sbase = shoff + sh_link * shentsize;
                if (is64) strtab_off = (int)rd64lo(data, sbase + 24);
                else      strtab_off = (int)rd32(data, sbase + 16);
            }
        }
    }
    if (dynsym_off < 0 || strtab_off < 0) return;

    int nent = dynsym_sz / dynsym_ent;
    for (i = 0; i < nent; i++) {
        if (ctx->so_exp_count >= LINKER_MAX_CACHE) break;
        int eoff = dynsym_off + i * dynsym_ent;
        if (eoff + dynsym_ent > data_len) break;
        uint32_t st_name;
        uint16_t st_shndx;
        uint8_t  st_info;
        if (is64) { st_name=rd32(data,eoff); st_info=data[eoff+4];  st_shndx=rd16(data,eoff+6); }
        else      { st_name=rd32(data,eoff); st_shndx=rd16(data,eoff+14); st_info=data[eoff+12]; }
        if (st_shndx == 0) continue; /* SHN_UNDEF — not defined here */
        int st_type = st_info & 0xf;
        if (st_type != 1 && st_type != 2) continue; /* STT_OBJECT=1, STT_FUNC=2 */
        int noff = strtab_off + (int)st_name;
        if (noff < 0 || noff >= data_len) continue;
        const char *nm = (const char *)(data + noff);
        int nlen = 0; while (nm[nlen] && noff+nlen < data_len && nlen < 127) nlen++;
        if (nlen == 0) continue;
        int si = ctx->so_exp_count++;
        lstr_copy(ctx->so_exp_name, si, LSTR128, nm, nlen);
        ctx->so_exp_lib[si] = lib_idx;
    }
}

/* =========================================================================
 * Load a library into LinkerContext
 * ========================================================================= */
static void load_so_into_context(LinkerContext *ctx, const char *path, const char *soname,
                                  const uint8_t *fdata, int flen) {
    if (ctx->lib_count >= LINKER_MAX_LIBS) return;
    int idx = ctx->lib_count++;
    lstr_copy(ctx->lib_path,   idx, LSTR256, path,   255);
    lstr_copy(ctx->lib_soname, idx, LSTR128, soname, 127);
    ctx->lib_static[idx] = 0;
    if (fdata) so_list_exports(ctx, idx, fdata, flen);
}

/* =========================================================================
 * AR archive: parse symbol index and load symbol→member-offset map.
 * ========================================================================= */
static void load_ar_symbols(LinkerContext *ctx, const char *ar_path,
                             const uint8_t *data, int data_len) {
    /* Verify magic */
    if (data_len < 8) return;
    if (strncmp((const char*)data, "!<arch>\n", 8) != 0) return;

    int pos = 8;
    /* First member is the symbol index with name "/" or "//" or a ranlib table */
    while (pos + 60 <= data_len) {
        /* AR member header: 60 bytes */
        char ar_name[17]; ar_name[16] = '\0';
        int k; for (k=0;k<16;k++) ar_name[k]=(char)data[pos+k];
        char ar_size_str[11]; ar_size_str[10]='\0';
        for (k=0;k<10;k++) ar_size_str[k]=(char)data[pos+48+k];
        char ar_fmag[2]; ar_fmag[0]=data[pos+58]; ar_fmag[1]=data[pos+59];
        if (ar_fmag[0]!='`' || ar_fmag[1]!='\n') { pos += 2; continue; }
        int msize = 0;
        { const char *p=ar_size_str; while(*p==' ')p++; while(*p>='0'&&*p<='9'){msize=msize*10+(*p-'0');p++;} }
        int mdata_start = pos + 60;
        pos = mdata_start + msize + (msize & 1); /* pad to even */

        /* Symbol index member: name is "/"  (GNU format) */
        if (ar_name[0]=='/' && (ar_name[1]==' '||ar_name[1]=='\0')) {
            if (msize < 4) break;
            /* GNU format: uint32_t big-endian count, then count offsets, then strings */
            uint32_t nsyms = rd32_be(data, mdata_start);
            if (nsyms == 0) break;
            int offsets_start = mdata_start + 4;
            int strings_start = offsets_start + (int)nsyms * 4;
            if (strings_start > mdata_start + msize) break;
            uint32_t s;
            const char *sp = (const char *)(data + strings_start);
            for (s = 0; s < nsyms; s++) {
                if (ctx->ar_sym_count >= LINKER_MAX_CACHE) break;
                int member_off = (int)rd32_be(data, offsets_start + (int)s * 4);
                /* symbol name */
                int nlen = 0; while (sp[nlen] && nlen < 127) nlen++;
                int si = ctx->ar_sym_count++;
                lstr_copy(ctx->ar_sym,  si, LSTR128, sp,       nlen);
                lstr_copy(ctx->ar_file, si, LSTR256, ar_path,  255);
                ctx->ar_off[si] = member_off;
                sp += nlen + 1;
            }
            break; /* symbol index is always first */
        }
        break; /* unexpected first member format */
    }
}

void linker_add_library(LinkerContext *ctx, const char *name) {
    char path[256]; int is_static = 0;
    if (!find_lib(ctx, name, path, &is_static)) {
        diag_emit(DIAG_WARNING, -1, NULL, NULL,
                  "library not found: %s -- any symbol expected from it will show up as an unresolved reference instead",
                  name);
        return;
    }
    linker_add_lib_path(ctx, path);
}

void linker_add_lib_path(LinkerContext *ctx, const char *path) {
    if (ctx->lib_count >= LINKER_MAX_LIBS) return;
    int is_static = str_ends_with(path, ".a");

    if (!is_static) {
        /* Dynamic: read the file to get SONAME */
        int flen = 0;
        uint8_t *fdata = read_file_buf(path, &flen);
        char soname[128]; soname[0] = '\0';
        if (fdata) {
            read_soname(fdata, flen, soname, 128);
        }
        if (!soname[0]) {
            /* Derive soname from basename */
            const char *bn = path + strlen(path);
            while (bn > path && bn[-1] != '/') bn--;
            int k = 0; while (bn[k] && k < 127) { soname[k]=bn[k]; k++; } soname[k]='\0';
        }
        load_so_into_context(ctx, path, soname, fdata, flen);
        if (fdata) free(fdata);
    } else {
        /* Static: load AR symbol index */
        if (ctx->lib_count >= LINKER_MAX_LIBS) return;
        int idx = ctx->lib_count++;
        lstr_copy(ctx->lib_path,   idx, LSTR256, path, 255);
        ctx->lib_soname[idx*LSTR128] = '\0';
        ctx->lib_static[idx] = 1;

        int flen = 0;
        uint8_t *fdata = read_file_buf(path, &flen);
        if (fdata) {
            /* Check for GNU ld script redirect */
            if (flen > 2 && fdata[0]=='/' && fdata[1]=='*') {
                /* GNU ld script: look for GROUP ( ... ) */
                char *gp = strstr((char *)fdata, "GROUP");
                if (gp) {
                    char *lp = strchr(gp, '(');
                    char *rp = strchr(gp, ')');
                    if (lp && rp) {
                        char *p = lp + 1;
                        while (p < rp) {
                            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                            if (p >= rp) break;
                            char tok[256]; int tl = 0;
                            while (*p && *p!=' '&&*p!='\t'&&*p!='\n'&&*p!=')'&&tl<255)
                                tok[tl++] = *p++;
                            tok[tl] = '\0';
                            if (tl > 0) linker_add_lib_path(ctx, tok);
                        }
                    }
                }
                free(fdata);
                ctx->lib_count--; /* Remove the script placeholder */
                return;
            }
            load_ar_symbols(ctx, path, fdata, flen);
            free(fdata);
        }
    }
}

/* =========================================================================
 * Dynamic symbol lookup: returns soname or NULL
 * ========================================================================= */
const char *linker_lookup_dynamic(LinkerContext *ctx, const char *funcname) {
    int i;
    /* Check cache first */
    for (i = 0; i < ctx->cache_count; i++) {
        int csbase = i * LSTR128;
        if (strcmp(&ctx->cache_sym[csbase], funcname) == 0) {
            int cobase = i * LSTR128;
            return ctx->cache_so[cobase] ? &ctx->cache_so[cobase] : NULL;
        }
    }

    /* Search loaded .so files */
    for (i = 0; i < ctx->lib_count; i++) {
        if (ctx->lib_static[i]) continue;
        int flen = 0;
        int lpbase = i * LSTR256;
        uint8_t *fdata = read_file_buf(&ctx->lib_path[lpbase], &flen);
        if (!fdata) continue;
        int found = so_has_func(fdata, flen, funcname);
        free(fdata);
        if (found) {
            /* Add to cache */
            if (ctx->cache_count < LINKER_MAX_CACHE) {
                int ci = ctx->cache_count++;
                lstr_copy(ctx->cache_sym, ci, LSTR128, funcname, 127);
                int snbase = i * LSTR128;
                lstr_copy(ctx->cache_so, ci, LSTR128, &ctx->lib_soname[snbase], 127);
            }
            int snbase = i * LSTR128;
            return &ctx->lib_soname[snbase];
        }
    }

    /* Cache negative result */
    if (ctx->cache_count < LINKER_MAX_CACHE) {
        int ci = ctx->cache_count++;
        lstr_copy(ctx->cache_sym, ci, LSTR128, funcname, 127);
        ctx->cache_so[ci*LSTR128] = '\0';
    }
    return NULL;
}

/* =========================================================================
 * Static text append helpers
 * ========================================================================= */
static void ensure_static_text(LinkerContext *ctx, int need) {
    while (ctx->static_text_len + need > ctx->static_text_cap) {
        ctx->static_text_cap *= 2;
        ctx->static_text = (uint8_t *)realloc(ctx->static_text, ctx->static_text_cap);
    }
}
static void ensure_static_rdata(LinkerContext *ctx, int need) {
    while (ctx->static_rdata_len + need > ctx->static_rdata_cap) {
        ctx->static_rdata_cap *= 2;
        ctx->static_rdata = (uint8_t *)realloc(ctx->static_rdata, ctx->static_rdata_cap);
    }
}
static void add_sreloc(LinkerContext *ctx, int offset, int type, const char *sym, int addend) {
    if (ctx->sreloc_count >= ctx->sreloc_cap) {
        ctx->sreloc_cap *= 2;
        ctx->srelocs = (StaticReloc *)realloc(ctx->srelocs, ctx->sreloc_cap * sizeof(StaticReloc));
    }
    StaticReloc *sr = &ctx->srelocs[ctx->sreloc_count++];
    sr->offset = offset;
    sr->type   = type;
    sr->addend = addend;
    int k = 0; while(sym[k]&&k<127){sr->sym[k]=sym[k];k++;}
    sr->sym[k] = '\0';
}

/* =========================================================================
 * Parse an ELF .o member and link it into static_text.
 * Handles: 64-bit ELF only (32-bit static .a support is minimal).
 * ========================================================================= */
static void link_elf_object(LinkerContext *ctx, const uint8_t *obj, int obj_len,
                             const char *primary_sym) {
    if (obj_len < 64) return;
    if (obj[0]!=0x7f||obj[1]!='E'||obj[2]!='L'||obj[3]!='F') return;
    int is64 = (obj[4] == 2);

    int shoff, shentsize, shnum, shstrndx;
    if (is64) {
        shoff     = (int)rd64lo(obj, 40);
        shentsize = rd16(obj, 58);
        shnum     = rd16(obj, 60);
        shstrndx  = rd16(obj, 62);
    } else {
        shoff     = (int)rd32(obj, 32);
        shentsize = rd16(obj, 46);
        shnum     = rd16(obj, 48);
        shstrndx  = rd16(obj, 50);
    }
    if (shoff<=0||shentsize<=0||shnum<=0) return;

    /* Collect section info */
    int text_off=-1,text_sz=0;
    int symtab_off=-1,symtab_sz=0,symtab_link=-1,symtab_ent=-1;
    int rela_text_off=-1,rela_text_sz=0,rela_text_ent=-1;
    int rel_text_off=-1,rel_text_sz=0,rel_text_ent=-1;
    int rodata_off=-1,rodata_sz=0;
    int rela_rodata_off=-1,rela_rodata_sz=0,rela_rodata_ent=-1;
    int shstroff = -1;

    /* First pass: collect offsets */
    int i;
    for (i = 0; i < shnum; i++) {
        int shbase = shoff + i * shentsize;
        if (shbase + shentsize > obj_len) break;
        uint32_t sh_name_idx, sh_type;
        int sh_offset, sh_size, sh_link, sh_entsize, sh_info;
        if (is64) {
            sh_name_idx = rd32(obj, shbase);
            sh_type     = rd32(obj, shbase + 4);
            sh_offset   = (int)rd64lo(obj, shbase + 24);
            sh_size     = (int)rd64lo(obj, shbase + 32);
            sh_link     = (int)rd32(obj, shbase + 40);
            sh_info     = (int)rd32(obj, shbase + 44);
            sh_entsize  = (int)rd64lo(obj, shbase + 56);
        } else {
            sh_name_idx = rd32(obj, shbase);
            sh_type     = rd32(obj, shbase + 4);
            sh_offset   = (int)rd32(obj, shbase + 16);
            sh_size     = (int)rd32(obj, shbase + 20);
            sh_link     = (int)rd32(obj, shbase + 24);
            sh_info     = (int)rd32(obj, shbase + 28);
            sh_entsize  = (int)rd32(obj, shbase + 36);
        }
        if (i == shstrndx) shstroff = sh_offset;
        /* SHT_PROGBITS=1, SHT_SYMTAB=2, SHT_RELA=4, SHT_REL=9 */
        if (sh_type == 2) {
            symtab_off  = sh_offset;
            symtab_sz   = sh_size;
            symtab_link = sh_link;
            symtab_ent  = sh_entsize > 0 ? sh_entsize : (is64 ? 24 : 16);
        }
        (void)sh_name_idx; (void)sh_info; (void)sh_link; (void)sh_offset; (void)sh_size;
    }

    /* Second pass: identify .text and .rodata by name */
    if (shstroff <= 0) return;
    for (i = 0; i < shnum; i++) {
        int shbase = shoff + i * shentsize;
        if (shbase + shentsize > obj_len) break;
        uint32_t sh_name_idx, sh_type;
        int sh_offset, sh_size, sh_link2, sh_info2, sh_entsize2;
        if (is64) {
            sh_name_idx = rd32(obj, shbase);
            sh_type     = rd32(obj, shbase + 4);
            sh_offset   = (int)rd64lo(obj, shbase + 24);
            sh_size     = (int)rd64lo(obj, shbase + 32);
            sh_link2    = (int)rd32(obj, shbase + 40);
            sh_info2    = (int)rd32(obj, shbase + 44);
            sh_entsize2 = (int)rd64lo(obj, shbase + 56);
        } else {
            sh_name_idx = rd32(obj, shbase);
            sh_type     = rd32(obj, shbase + 4);
            sh_offset   = (int)rd32(obj, shbase + 16);
            sh_size     = (int)rd32(obj, shbase + 20);
            sh_link2    = (int)rd32(obj, shbase + 24);
            sh_info2    = (int)rd32(obj, shbase + 28);
            sh_entsize2 = (int)rd32(obj, shbase + 36);
        }
        const char *sname = (const char *)(obj + shstroff + sh_name_idx);
        if (sh_type == 1 && strcmp(sname, ".text") == 0) {
            text_off = sh_offset; text_sz = sh_size;
        } else if (sh_type == 1 && strncmp(sname, ".rodata", 7) == 0) {
            rodata_off = sh_offset; rodata_sz = sh_size;
        } else if (sh_type == 4) { /* SHT_RELA */
            if (strcmp(sname, ".rela.text") == 0) {
                rela_text_off = sh_offset; rela_text_sz = sh_size;
                rela_text_ent = sh_entsize2 > 0 ? sh_entsize2 : 24;
            } else if (strncmp(sname, ".rela.rodata", 12) == 0) {
                rela_rodata_off = sh_offset; rela_rodata_sz = sh_size;
                rela_rodata_ent = sh_entsize2 > 0 ? sh_entsize2 : 24;
            }
        } else if (sh_type == 9) { /* SHT_REL */
            if (strcmp(sname, ".rel.text") == 0) {
                rel_text_off = sh_offset; rel_text_sz = sh_size;
                rel_text_ent = sh_entsize2 > 0 ? sh_entsize2 : 8;
            }
        }
        (void)sh_link2; (void)sh_info2;
    }

    if (text_off < 0 || text_sz <= 0) return;

    /* Find strtab for symtab */
    int strtab_off = -1;
    if (symtab_link > 0 && symtab_link < shnum) {
        int sbase = shoff + symtab_link * shentsize;
        if (is64) strtab_off = (int)rd64lo(obj, sbase + 24);
        else      strtab_off = (int)rd32(obj, sbase + 16);
    }

    /* Append .text to static pool */
    int text_base = ctx->static_text_len;
    ensure_static_text(ctx, text_sz);
    int bi;
    for (bi = 0; bi < text_sz; bi++)
        ctx->static_text[text_base + bi] = obj[text_off + bi];
    ctx->static_text_len += text_sz;

    /* Append .rodata to static rodata pool */
    int rodata_base = ctx->static_rdata_len;
    if (rodata_off >= 0 && rodata_sz > 0) {
        ensure_static_rdata(ctx, rodata_sz);
        for (bi = 0; bi < rodata_sz; bi++)
            ctx->static_rdata[rodata_base + bi] = obj[rodata_off + bi];
        ctx->static_rdata_len += rodata_sz;
    }

    /* Build local symbol table (symname → offset-in-text) */
    /* Flat: local_sym_names[256*128], stride 128 per entry */
    char local_sym_names[32768]; /* 256 * 128 */
    int  local_sym_text_off[256];
    int  local_sym_rodata_off[256];
    int  local_sym_count = 0;

    if (symtab_off >= 0 && symtab_ent > 0 && strtab_off >= 0) {
        int nent = symtab_sz / symtab_ent;
        for (i = 0; i < nent && local_sym_count < 255; i++) {
            int eoff = symtab_off + i * symtab_ent;
            if (eoff + symtab_ent > obj_len) break;
            uint32_t st_name;
            int      st_value, st_shndx;
            uint8_t  st_info;
            if (is64) {
                st_name  = rd32(obj, eoff);
                st_info  = obj[eoff + 4];
                st_shndx = rd16(obj, eoff + 6);
                st_value = (int)rd64lo(obj, eoff + 8);
            } else {
                st_name  = rd32(obj, eoff);
                st_value = (int)rd32(obj, eoff + 4);
                st_shndx = rd16(obj, eoff + 14);
                st_info  = obj[eoff + 12];
            }
            int st_bind = st_info >> 4;
            int st_type = st_info & 0xf;
            if (st_shndx == 0 || st_shndx == 0xfff1) continue; /* undef or abs */
            if (st_name == 0) continue;
            const char *sname = (const char *)(obj + strtab_off + st_name);
            int nlen = 0; while (sname[nlen] && nlen < 127) nlen++;
            if (nlen == 0) continue;

            int ls = local_sym_count++;
            int lsbase = ls * LSTR128;
            int k; for(k=0;k<nlen;k++) local_sym_names[lsbase+k]=sname[k];
            local_sym_names[lsbase+nlen] = '\0';
            local_sym_text_off[ls]   = -1;
            local_sym_rodata_off[ls] = -1;

            if (st_type == 2 || (st_type == 0 && st_bind != 0)) {
                /* Function or notype global → likely .text */
                local_sym_text_off[ls] = st_value;

                /* Register as a static definition */
                if (st_bind != 0 && ctx->sdef_count < LINKER_MAX_STATIC) {
                    int di = ctx->sdef_count++;
                    int dbase = di * LSTR128;
                    for(k=0;k<nlen;k++) ctx->sdef_name[dbase+k]=sname[k];
                    ctx->sdef_name[dbase+nlen] = '\0';
                    ctx->sdef_off[di] = text_base + st_value;
                }
            } else if (st_type == 1 && rodata_off >= 0) {
                /* Object → .rodata */
                local_sym_rodata_off[ls] = st_value;
                /* Register as static rodata */
                if (st_bind != 0 && ctx->srd_count < LINKER_MAX_STATIC) {
                    int ri = ctx->srd_count++;
                    int rbase = ri * LSTR128;
                    for(k=0;k<nlen;k++) ctx->srd_name[rbase+k]=sname[k];
                    ctx->srd_name[rbase+nlen] = '\0';
                    ctx->srd_offset[ri] = rodata_base + st_value;
                }
            }
        }
        /* Also register STT_NOTYPE global syms as possible text symbols */
        for (i = 0; i < nent; i++) {
            int eoff = symtab_off + i * symtab_ent;
            if (eoff + symtab_ent > obj_len) break;
            uint32_t st_name;
            int      st_value;
            uint8_t  st_info;
            if (is64) { st_name=rd32(obj,eoff); st_info=obj[eoff+4]; st_value=(int)rd64lo(obj,eoff+8); }
            else      { st_name=rd32(obj,eoff); st_info=obj[eoff+12]; st_value=(int)rd32(obj,eoff+4); }
            int st_bind = st_info >> 4; int st_type = st_info & 0xf;
            if (st_type != 0 || st_bind == 0) continue;
            if (st_name == 0) continue;
            const char *sname = (const char *)(obj + strtab_off + st_name);
            int nlen = 0; while (sname[nlen] && nlen < 127) nlen++;
            if (nlen == 0) continue;
            if (ctx->sdef_count < LINKER_MAX_STATIC) {
                int k2; int already = 0;
                for(k2=0;k2<ctx->sdef_count;k2++) {
                    int db = k2 * LSTR128;
                    if(strcmp(&ctx->sdef_name[db],sname)==0){already=1;break;}
                }
                if (!already) {
                    int di = ctx->sdef_count++;
                    int dbase = di * LSTR128;
                    int k3; for(k3=0;k3<nlen;k3++) ctx->sdef_name[dbase+k3]=sname[k3];
                    ctx->sdef_name[dbase+nlen]='\0';
                    ctx->sdef_off[di] = text_base + st_value;
                }
            }
        }
    }

    /* Process .rela.text relocations */
    if (rela_text_off >= 0 && rela_text_sz > 0 && symtab_off >= 0 && strtab_off >= 0) {
        int nrel = rela_text_sz / rela_text_ent;
        for (i = 0; i < nrel; i++) {
            int roff = rela_text_off + i * rela_text_ent;
            if (roff + rela_text_ent > obj_len) break;
            int r_offset, addend;
            uint32_t r_info_lo, r_info_hi;
            if (is64) {
                r_offset  = (int)rd64lo(obj, roff);
                r_info_lo = rd64lo(obj, roff + 8);
                r_info_hi = rd64hi(obj, roff + 8);
                addend    = rd32s(obj, roff + 16);
            } else {
                r_offset = (int)rd32(obj, roff);
                r_info_lo = rd32(obj, roff + 4);
                r_info_hi = 0;
                addend    = 0;
            }
            int sym_idx = is64 ? (int)r_info_hi : (int)(r_info_lo >> 8);
            int r_type  = is64 ? (int)r_info_lo : (int)(r_info_lo & 0xff);

            /* Get target symbol name */
            int eoff = symtab_off + sym_idx * symtab_ent;
            if (eoff + symtab_ent > obj_len) continue;
            uint32_t st_name;
            int st_shndx;
            if (is64) { st_name=rd32(obj,eoff); st_shndx=rd16(obj,eoff+6); }
            else      { st_name=rd32(obj,eoff); st_shndx=rd16(obj,eoff+14); }
            if (st_name == 0 && st_shndx == 0) continue;

            char sym_name[128]; sym_name[0]='\0';
            if (st_name > 0) {
                const char *sn = (const char *)(obj + strtab_off + st_name);
                int k=0; while(sn[k]&&k<127){sym_name[k]=sn[k];k++;}sym_name[k]='\0';
            }

            int global_reloc_off = text_base + r_offset;

            if (ctx->is_arm64) {
                /* AArch64 ELF relocation types (AArch64 ELF ABI spec):
                 * R_AARCH64_CALL26=283 / R_AARCH64_JUMP26=282 (direct BL/B,
                 * 26-bit word-offset immediate — the only kind resolvable
                 * INTRA-.o here, same as x86-64's PLT32/PC32 case below);
                 * R_AARCH64_ADR_PREL_PG_HI21=275 (ADRP page immediate);
                 * R_AARCH64_ADD_ABS_LO12_NC=277 (ADD low 12 bits, unscaled);
                 * R_AARCH64_LDST64_ABS_LO12_NC=286 (LDR/STR low 12 bits,
                 * scaled by 8 for a 64-bit access); R_AARCH64_ABS64=257
                 * (64-bit absolute, raw 8-byte field — rare directly in
                 * .text, kept for completeness). These map onto
                 * StaticReloc.type 3..7 (see linker.h); elf_builder.c's
                 * static-reloc patch step does the actual instruction-bit
                 * packing via the shared e_patch_a64_* helpers. */
                if (r_type == 283 || r_type == 282) { /* CALL26 / JUMP26 */
                    int found_local = 0;
                    int li;
                    for (li = 0; li < local_sym_count; li++) {
                        int lsb = li * LSTR128;
                        if (strcmp(&local_sym_names[lsb], sym_name) == 0 &&
                            local_sym_text_off[li] >= 0) {
                            int target_off = text_base + local_sym_text_off[li];
                            int patch_off  = global_reloc_off;
                            int32_t word_disp = (int32_t)((target_off + addend - patch_off) / 4);
                            uint32_t cur = rd32(ctx->static_text, patch_off);
                            cur = (cur & 0xFC000000u) | ((uint32_t)word_disp & 0x03FFFFFFu);
                            wr32(ctx->static_text, patch_off, cur);
                            found_local = 1;
                            break;
                        }
                    }
                    if (!found_local && sym_name[0])
                        add_sreloc(ctx, global_reloc_off, 3, sym_name, addend);
                } else if (r_type == 275) { /* ADRP */
                    if (sym_name[0]) add_sreloc(ctx, global_reloc_off, 4, sym_name, addend);
                } else if (r_type == 277) { /* ADD_ABS_LO12_NC */
                    if (sym_name[0]) add_sreloc(ctx, global_reloc_off, 5, sym_name, addend);
                } else if (r_type == 286) { /* LDST64_ABS_LO12_NC */
                    if (sym_name[0]) add_sreloc(ctx, global_reloc_off, 6, sym_name, addend);
                } else if (r_type == 257) { /* ABS64 */
                    if (sym_name[0]) add_sreloc(ctx, global_reloc_off, 7, sym_name, addend);
                }
                continue;
            }

            /* R_X86_64_PLT32=4, R_X86_64_PC32=2 → PC-relative call/jmp */
            /* R_X86_64_32S=11, R_X86_64_32=10 → absolute 32-bit */
            /* R_386_PLT32=4, R_386_PC32=2, R_386_32=1 */
            if (r_type == 4 || r_type == 2) { /* PLT32 or PC32 */
                /* Check if target is a local symbol (defined in this .o's .text) */
                int found_local = 0;
                int li;
                for (li = 0; li < local_sym_count; li++) {
                    int lsb = li * LSTR128;
                    if (strcmp(&local_sym_names[lsb], sym_name) == 0 &&
                        local_sym_text_off[li] >= 0) {
                        /* Intra-.o PC32: patch directly in the static text */
                        int target_off = text_base + local_sym_text_off[li];
                        int patch_off  = global_reloc_off;
                        int disp = target_off + addend - (patch_off + 4);
                        wr32(ctx->static_text, patch_off, (uint32_t)disp);
                        found_local = 1;
                        break;
                    }
                }
                if (!found_local && sym_name[0]) {
                    /* External: add as static reloc (resolved later in ELF builder) */
                    add_sreloc(ctx, global_reloc_off, 0, sym_name, addend);
                }
            } else if (r_type == 11 || r_type == 10 || r_type == 1) { /* ABS32/32S */
                /* Check local rodata */
                int found_rd = 0;
                int li;
                for (li = 0; li < local_sym_count; li++) {
                    int lsb = li * LSTR128;
                    if (strcmp(&local_sym_names[lsb], sym_name) == 0 &&
                        local_sym_rodata_off[li] >= 0) {
                        /* Reference to local .rodata: record as static rdata reloc */
                        int target_off = rodata_base + local_sym_rodata_off[li];
                        add_sreloc(ctx, global_reloc_off, 1, sym_name, addend);
                        /* Also add to srd table if not already there */
                        if (ctx->srd_count < LINKER_MAX_STATIC) {
                            int already = 0; int k2;
                            for(k2=0;k2<ctx->srd_count;k2++) {
                                int rb = k2 * LSTR128;
                                if(strcmp(&ctx->srd_name[rb],sym_name)==0){already=1;break;}
                            }
                            if (!already) {
                                int ri = ctx->srd_count++;
                                int rbase = ri * LSTR128;
                                int nlen2=0; while(sym_name[nlen2]&&nlen2<127)nlen2++;
                                int k3; for(k3=0;k3<nlen2;k3++) ctx->srd_name[rbase+k3]=sym_name[k3];
                                ctx->srd_name[rbase+nlen2]='\0';
                                ctx->srd_offset[ri] = target_off;
                            }
                        }
                        found_rd = 1;
                        break;
                    }
                }
                if (!found_rd && sym_name[0])
                    add_sreloc(ctx, global_reloc_off, 1, sym_name, addend);
            }
            /* R_X86_64_64=1 (64-bit abs): add as type 2 */
            else if (is64 && r_type == 1) {
                if (sym_name[0])
                    add_sreloc(ctx, global_reloc_off, 2, sym_name, addend);
            }
        }
    }

    /* Process .rel.text (32-bit archives) */
    if (rel_text_off >= 0 && rel_text_sz > 0 && symtab_off >= 0 && strtab_off >= 0) {
        int nrel = rel_text_sz / rel_text_ent;
        for (i = 0; i < nrel; i++) {
            int roff = rel_text_off + i * rel_text_ent;
            if (roff + rel_text_ent > obj_len) break;
            int r_offset = (int)rd32(obj, roff);
            uint32_t r_info = rd32(obj, roff + 4);
            int sym_idx = (int)(r_info >> 8);
            int r_type  = (int)(r_info & 0xff);

            int eoff = symtab_off + sym_idx * symtab_ent;
            if (eoff + symtab_ent > obj_len) continue;
            uint32_t st_name = rd32(obj, eoff);
            if (st_name == 0) continue;
            const char *sn = (const char *)(obj + strtab_off + st_name);
            char sym_name[128]; int k=0;
            while(sn[k]&&k<127){sym_name[k]=sn[k];k++;} sym_name[k]='\0';

            int global_reloc_off = text_base + r_offset;
            if (r_type == 2 || r_type == 4) /* R_386_PC32, R_386_PLT32 */
                add_sreloc(ctx, global_reloc_off, 0, sym_name, -4);
            else if (r_type == 1) /* R_386_32 */
                add_sreloc(ctx, global_reloc_off, 1, sym_name, 0);
        }
    }

    (void)primary_sym;
    (void)rela_rodata_off; (void)rela_rodata_sz; (void)rela_rodata_ent;
}

/* =========================================================================
 * Extract and link a member from a .a archive at a given byte offset.
 * ========================================================================= */
static int extract_and_link(LinkerContext *ctx, const char *ar_path,
                             int member_off, const char *sym) {
    int flen = 0;
    uint8_t *ar = read_file_buf(ar_path, &flen);
    if (!ar) return 0;

    if (member_off + 60 > flen) { free(ar); return 0; }
    /* Read member header */
    char size_str[11]; size_str[10]='\0';
    int k; for(k=0;k<10;k++) size_str[k]=(char)ar[member_off+48+k];
    int msize = 0;
    { const char *p=size_str; while(*p==' ')p++; while(*p>='0'&&*p<='9'){msize=msize*10+(*p-'0');p++;} }

    int mdata_off = member_off + 60;
    if (mdata_off + msize > flen) { free(ar); return 0; }

    link_elf_object(ctx, ar + mdata_off, msize, sym);
    free(ar);
    return 1;
}

/* =========================================================================
 * Static linking: find and link the .o that defines funcname.
 * May recursively link dependencies.
 * ========================================================================= */
int linker_link_static(LinkerContext *ctx, const char *funcname) {
    /* Already linked? */
    int i;
    for (i = 0; i < ctx->sdef_count; i++) {
        int db = i * LSTR128;
        if (strcmp(&ctx->sdef_name[db], funcname) == 0) return 1;
    }

    /* Already queued? */
    for (i = 0; i < ctx->queued_count; i++) {
        int qb = i * LSTR128;
        if (strcmp(&ctx->queued[qb], funcname) == 0) return 0;
    }
    if (ctx->queued_count < LINKER_MAX_STATIC) {
        int qi = ctx->queued_count++;
        lstr_copy(ctx->queued, qi, LSTR128, funcname, 127);
    }

    /* Search AR symbol index */
    for (i = 0; i < ctx->ar_sym_count; i++) {
        int asb = i * LSTR128;
        if (strcmp(&ctx->ar_sym[asb], funcname) == 0) {
            int afb = i * LSTR256;
            return extract_and_link(ctx, &ctx->ar_file[afb], ctx->ar_off[i], funcname);
        }
    }
    return 0;
}

int linker_has_static_def(LinkerContext *ctx, const char *funcname) {
    int i;
    for (i = 0; i < ctx->sdef_count; i++) {
        int db = i * LSTR128;
        if (strcmp(&ctx->sdef_name[db], funcname) == 0) return 1;
    }
    return 0;
}

/* =========================================================================
 * ELF builder query interface
 * ========================================================================= */
int linker_soname_count(LinkerContext *ctx) {
    int count = 0, i;
    for (i = 0; i < ctx->lib_count; i++) {
        int snb = i * LSTR128;
        if (!ctx->lib_static[i] && ctx->lib_soname[snb]) count++;
    }
    return count;
}

const char *linker_soname_get(LinkerContext *ctx, int idx) {
    int count = 0, i;
    for (i = 0; i < ctx->lib_count; i++) {
        int snb = i * LSTR128;
        if (!ctx->lib_static[i] && ctx->lib_soname[snb]) {
            if (count == idx) return &ctx->lib_soname[snb];
            count++;
        }
    }
    return NULL;
}

uint8_t *linker_static_text(LinkerContext *ctx, int *len) {
    *len = ctx->static_text_len;
    return ctx->static_text;
}

uint8_t *linker_static_rdata(LinkerContext *ctx, int *len) {
    *len = ctx->static_rdata_len;
    return ctx->static_rdata;
}

StaticReloc *linker_static_relocs(LinkerContext *ctx, int *count) {
    *count = ctx->sreloc_count;
    return ctx->srelocs;
}

int linker_static_def_count(LinkerContext *ctx)               { return ctx->sdef_count; }
const char *linker_static_def_name(LinkerContext *ctx, int i) { int db=i*LSTR128; return &ctx->sdef_name[db]; }
int linker_static_def_off(LinkerContext *ctx, int i)          { return ctx->sdef_off[i]; }

int linker_static_rdata_count(LinkerContext *ctx)               { return ctx->srd_count; }
const char *linker_static_rdata_name(LinkerContext *ctx, int i) { int rb=i*LSTR128; return &ctx->srd_name[rb]; }
int linker_static_rdata_off(LinkerContext *ctx, int i)          { return ctx->srd_offset[i]; }

/* Export-listing diagnostic accessors (compiler.c) */
int linker_lib_count(LinkerContext *ctx) { return ctx->lib_count; }
const char *linker_lib_path(LinkerContext *ctx, int i)   { int b=i*LSTR256; return &ctx->lib_path[b]; }
const char *linker_lib_soname(LinkerContext *ctx, int i) { int b=i*LSTR128; return &ctx->lib_soname[b]; }
int linker_lib_is_static(LinkerContext *ctx, int i)      { return ctx->lib_static[i]; }

int linker_so_export_count(LinkerContext *ctx)               { return ctx->so_exp_count; }
const char *linker_so_export_name(LinkerContext *ctx, int i) { int b=i*LSTR128; return &ctx->so_exp_name[b]; }
int linker_so_export_lib(LinkerContext *ctx, int i)          { return ctx->so_exp_lib[i]; }

int linker_ar_sym_count(LinkerContext *ctx)               { return ctx->ar_sym_count; }
const char *linker_ar_sym_name(LinkerContext *ctx, int i) { int b=i*LSTR128; return &ctx->ar_sym[b]; }
const char *linker_ar_sym_file(LinkerContext *ctx, int i) { int b=i*LSTR256; return &ctx->ar_file[b]; }
