#include "implib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* my_strdup(const char* src);

/* =========================================================================
 * Real Windows .lib import-library parser.
 *
 * symtable.c's find_dll() only recognizes a few dozen hardcoded API names
 * — fine for squash's own hand-written test programs, but hopeless for
 * real Windows SDK headers, which declare (and real code calls) thousands
 * of API functions no hardcoded list will ever fully cover (e.g. real
 * <windows.h>'s "lstrlenA"/"wsprintfA" aren't in the hardcoded table, so a
 * call to either compiles to an unresolved relocation that's silently
 * never patched — no link error, just a broken call at runtime). A real
 * Windows SDK install already ships the authoritative answer for every
 * name: kernel32.lib/user32.lib/etc are COFF archives where (for ordinary
 * function imports) each exported symbol gets its own tiny archive member
 * in the "short import" format — 20-byte IMPORT_OBJECT_HEADER followed by
 * two NUL-terminated strings (the symbol name, then its DLL name). Parsing
 * that format directly (skipping the rarer full-COFF-object import member
 * shape some older/data-only imports use) covers the overwhelming
 * majority of real API calls and needs no relocation/section handling at
 * all — just a sequential walk of the archive. */

typedef struct ImpEnt {
    char *name;
    char *dll;
    struct ImpEnt *next;
} ImpEnt;

#define IMPLIB_HASH_SIZE 8192
static ImpEnt *g_implib_hash[IMPLIB_HASH_SIZE];
static int g_implib_loaded = 0;

static unsigned implib_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h % IMPLIB_HASH_SIZE;
}

static void implib_add(const char *name, const char *dll) {
    if (!name[0] || !dll[0]) return;
    unsigned h = implib_hash(name);
    for (ImpEnt *e = g_implib_hash[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return; /* first DLL found for a name wins */
    ImpEnt *e = (ImpEnt*)malloc(sizeof(ImpEnt));
    e->name = my_strdup(name);
    e->dll = my_strdup(dll);
    e->next = g_implib_hash[h];
    g_implib_hash[h] = e;
}

/* Walk one archive's members sequentially, harvesting every short-import
 * record found. Real archive member headers are ASCII, fixed 60 bytes,
 * with the member's byte size in a 10-char decimal field at offset 48;
 * member data immediately follows and is padded to an even offset. */
static void implib_load_one(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    unsigned char magic[8];
    if (fread(magic,1,8,f)!=8 || memcmp(magic,"!<arch>\n",8)!=0) { fclose(f); return; }
    for (;;) {
        unsigned char hdr[60];
        if (fread(hdr,1,60,f) != 60) break;
        char sizebuf[11]; memcpy(sizebuf,hdr+48,10); sizebuf[10]='\0';
        long member_size = strtol(sizebuf, NULL, 10);
        if (member_size <= 0) break;
        long member_start = ftell(f);
        if (member_size >= 20) {
            unsigned char ih[20];
            if (fread(ih,1,20,f) == 20) {
                unsigned sig1 = ih[0] | ((unsigned)ih[1]<<8);
                unsigned sig2 = ih[2] | ((unsigned)ih[3]<<8);
                /* IMAGE_FILE_MACHINE_UNKNOWN(0) + 0xFFFF: the fixed
                 * signature real link.exe writes for every short-format
                 * import member (never a valid machine-type/section-count
                 * pair for a genuine COFF object, so it's an unambiguous
                 * marker). */
                if (sig1 == 0 && sig2 == 0xFFFF) {
                    unsigned sizeofdata = (unsigned)ih[12] | ((unsigned)ih[13]<<8) |
                                          ((unsigned)ih[14]<<16) | ((unsigned)ih[15]<<24);
                    if (sizeofdata > 0 && sizeofdata < 8192) {
                        char *buf = (char*)malloc(sizeofdata+1);
                        size_t rd = fread(buf,1,sizeofdata,f);
                        buf[rd]='\0';
                        size_t slen = strlen(buf);
                        if (slen+1 < rd) implib_add(buf, buf+slen+1);
                        free(buf);
                    }
                }
            }
        }
        long next = member_start + member_size;
        if (next & 1) next++; /* archive members are 2-byte aligned */
        if (fseek(f, next, SEEK_SET) != 0) break;
    }
    fclose(f);
}

/* Every ordinary function-import .lib worth having on hand for real
 * Windows SDK header coverage. Missing/absent files are silently skipped
 * (implib_load_one no-ops if fopen fails) — this list is deliberately
 * generous since the cost of probing a nonexistent path is one failed
 * fopen(), not a hard error. */
static const char *g_implib_names[] = {
    "kernel32.lib","user32.lib","gdi32.lib","shell32.lib","advapi32.lib",
    "ole32.lib","oleaut32.lib","comdlg32.lib","winmm.lib","ws2_32.lib",
    "shlwapi.lib","comctl32.lib","gdiplus.lib","version.lib","imm32.lib",
    "setupapi.lib","uuid.lib","dbghelp.lib","crypt32.lib","wininet.lib",
    "winspool.lib","msimg32.lib","dwmapi.lib","d3d11.lib","dxgi.lib",
    "d2d1.lib","dwrite.lib","opengl32.lib","glu32.lib","iphlpapi.lib",
    "netapi32.lib","userenv.lib","psapi.lib","powrprof.lib","bcrypt.lib",
    "ncrypt.lib","secur32.lib","rpcrt4.lib","propsys.lib","shcore.lib",
    "xinput.lib","hid.lib","dinput8.lib","mfplat.lib","mf.lib",
    "mfuuid.lib","wbemuuid.lib","oleacc.lib","urlmon.lib","normaliz.lib",
    "avrt.lib","dxguid.lib","d3dcompiler.lib","windowscodecs.lib",
    "cfgmgr32.lib","credui.lib","ntdll.lib",
    NULL
};

/* Real SDK version directories seen in practice — tried newest-first;
 * the first one whose kernel32.lib actually exists wins, matching how a
 * real toolchain picks "the" installed SDK version. Not a general
 * directory scan (would need FindFirstFile/dirent, dragging in a
 * dependency this single-purpose lookup doesn't need) — just a short,
 * easily-extended probe list. */
static const char *g_implib_dirs[] = {
    "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64\\",
    "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.22621.0\\um\\x64\\",
    "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.20348.0\\um\\x64\\",
    "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.19041.0\\um\\x64\\",
    NULL
};

static void implib_load_all(void) {
    if (g_implib_loaded) return;
    g_implib_loaded = 1;
    for (int d = 0; g_implib_dirs[d]; d++) {
        char probe[512];
        snprintf(probe, sizeof probe, "%skernel32.lib", g_implib_dirs[d]);
        FILE *t = fopen(probe, "rb");
        if (!t) continue;
        fclose(t);
        for (int i = 0; g_implib_names[i]; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s%s", g_implib_dirs[d], g_implib_names[i]);
            implib_load_one(path);
        }
        break; /* only the first SDK version directory that actually exists */
    }
}

const char *implib_find_dll(const char *symbol_name) {
    if (!symbol_name) return NULL;
    implib_load_all();
    unsigned h = implib_hash(symbol_name);
    for (ImpEnt *e = g_implib_hash[h]; e; e = e->next)
        if (strcmp(e->name, symbol_name) == 0) return e->dll;
    return NULL;
}
