/*
 * pe_builder.c  —  Windows PE32 / PE32+ builder with linker
 *
 * File layout produced (4 sections):
 *
 *   [headers]  DOS hdr(64) + stub(64) + PE sig(4) + FileHdr(20) +
 *              OptHdr32(224) or OptHdr64(240) + SectionTable(4×40)
 *              → padded to FILE_ALIGN boundary
 *   [.text]    machine code from assembler
 *   [.rdata]   import tables + string pool
 *   [.data]    zero-initialised BSS placeholder (minimal)
 *
 * Import table layout inside .rdata:
 *   ImportDescriptor table   (N+1) × 20 bytes  (N DLLs + null)
 *   IAT                      per-DLL thunks, 4 or 8 bytes each + null
 *   INT                      same initial values as IAT
 *   DLL name strings
 *   Hint/Name entries
 *   String pool              (from codegen string literals)
 *
 * Linker steps (pe_link_and_write):
 *   1. Parse import specs → group by DLL
 *   2. Lay out .rdata: import dir, IAT, INT, names, strings
 *   3. Lay out .text: raw code bytes
 *   4. Patch RELOC_IAT_REL32  → RIP-relative offset to IAT slot (64-bit)
 *      Patch RELOC_ABS32      → absolute VA of IAT slot (32-bit)
 *      Patch RELOC_DATA_REL32 → RIP-relative offset to string in .rdata (64-bit)
 *      Patch RELOC_DATA ABS32 → absolute VA of string in .rdata (32-bit)
 *   5. Compute entry point RVA (find label offset in .text)
 *   6. Write all sections + headers to output file
 */

#include "pe_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#define strdup _strdup
#define strcasecmp _stricmp
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */
#define FILE_ALIGN    0x200
#define SEC_ALIGN     0x1000
#define IMAGE_BASE_32 0x00400000u
#define IMAGE_BASE_64 UINT64_C(0x140000000)

#define ALIGN_UP(v,a) (((v)+(a)-1) & ~((a)-1))

#define IMAGE_FILE_MACHINE_I386       0x014C
#define IMAGE_FILE_MACHINE_AMD64      0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE   0x0002
#define IMAGE_FILE_32BIT_MACHINE      0x0100
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x010B
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x020B
#define IMAGE_SUBSYSTEM_WINDOWS_CUI   3
#define IMAGE_DIRECTORY_ENTRY_IMPORT  1
#define IMAGE_DIRECTORY_ENTRY_IAT     12

#define SCN_CODE   (0x00000020|0x20000000|0x40000000) /* CNT_CODE|MEM_EXEC|MEM_READ */
#define SCN_RDATA  (0x00000040|0x40000000)             /* CNT_INIT|MEM_READ */
#define SCN_DATA   (0x00000040|0x40000000|0x80000000)  /* CNT_INIT|MEM_R|MEM_W */

/* =========================================================================
 * Little-endian write helpers
 * ========================================================================= */
static void pu16(uint8_t *p, uint16_t v){ p[0]=v&0xFF; p[1]=(v>>8)&0xFF; }
static void pu32(uint8_t *p, uint32_t v){
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static void pu64(uint8_t *p, uint64_t v){
    for(int i=0;i<8;i++) p[i]=(v>>(8*i))&0xFF;
}

/* =========================================================================
 * Section builder functions (shared, called by both 32 and 64-bit paths)
 * ========================================================================= */

/* pe_build_dos_header — fills IMAGE_DOS_HEADER (64 bytes) */
void pe_build_dos_header(uint8_t *buf, uint32_t e_lfanew) {
    memset(buf, 0, sizeof(PE_DOSHeader));
    PE_DOSHeader *h = (PE_DOSHeader *)buf;
    h->e_magic    = 0x5A4D;
    h->e_cblp     = 0x0090;
    h->e_cp       = 0x0003;
    h->e_cparhdr  = 0x0004;
    h->e_maxalloc = 0xFFFF;
    h->e_sp       = 0x00B8;
    h->e_lfarlc   = 0x0040;
    h->e_lfanew   = e_lfanew;
}

/* pe_build_dos_stub — 64-byte real-mode "cannot run in DOS mode" stub */
void pe_build_dos_stub(uint8_t *buf) {
    static const uint8_t stub[64] = {
        0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21,
        0xB8, 0x01, 0x4C, 0xCD, 0x21,
        'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
        'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
        'i','n',' ','D','O','S',' ','m','o','d','e','.',
        '\r','\r','\n','$',
        0,0,0,0,0,0,0
    };
    memcpy(buf, stub, 64);
}

/* pe_build_pe_signature — 4-byte "PE\0\0" */
void pe_build_pe_signature(uint8_t *buf) {
    buf[0]='P'; buf[1]='E'; buf[2]=0; buf[3]=0;
}

/* pe_build_file_header — IMAGE_FILE_HEADER (20 bytes) */
void pe_build_file_header(uint8_t *buf, uint16_t machine, uint16_t nsec,
                           uint16_t opt_size, uint16_t chars) {
    memset(buf, 0, sizeof(PE_FileHeader));
    PE_FileHeader *fh = (PE_FileHeader *)buf;
    fh->Machine              = machine;
    fh->NumberOfSections     = nsec;
    fh->TimeDateStamp        = (uint32_t)time(NULL);  /* real timestamp */
    fh->SizeOfOptionalHeader = opt_size;
    fh->Characteristics      = chars;
}

/* pe_build_opt_header_32 — IMAGE_OPTIONAL_HEADER (PE32, 224 bytes) */
void pe_build_opt_header_32(uint8_t *buf, uint32_t ep_rva, uint32_t img_size,
                             uint32_t code_size, uint32_t idata_size,
                             uint32_t headers_size) {
    memset(buf, 0, sizeof(PE_OptionalHeader32));
    PE_OptionalHeader32 *oh = (PE_OptionalHeader32 *)buf;
    oh->Magic                       = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    oh->MajorLinkerVersion          = 14;
    oh->SizeOfCode                  = ALIGN_UP(code_size, FILE_ALIGN);
    oh->SizeOfInitializedData       = ALIGN_UP(idata_size, FILE_ALIGN);
    oh->AddressOfEntryPoint         = ep_rva;
    oh->BaseOfCode                  = SEC_ALIGN;          /* .text RVA */
    oh->BaseOfData                  = 2 * SEC_ALIGN;      /* .rdata RVA */
    oh->ImageBase                   = IMAGE_BASE_32;
    oh->SectionAlignment            = SEC_ALIGN;
    oh->FileAlignment               = FILE_ALIGN;
    oh->MajorOperatingSystemVersion = 4;
    oh->MajorSubsystemVersion       = 4;
    oh->SizeOfImage                 = img_size;
    oh->SizeOfHeaders               = headers_size;
    oh->Subsystem                   = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    /* NX-compat only; NO DYNAMIC_BASE (would break abs-VA calls without reloc)
     * NO TERMINAL_SERVER_AWARE (0x8000 removed — reduces AV heuristic triggers) */
    oh->DllCharacteristics          = 0x0100;  /* NX_COMPAT only */
    oh->SizeOfStackReserve          = 0x100000;
    oh->SizeOfStackCommit           = 0x1000;
    oh->SizeOfHeapReserve           = 0x100000;
    oh->SizeOfHeapCommit            = 0x1000;
    oh->NumberOfRvaAndSizes         = 16;
}

/* pe_build_opt_header_64 — IMAGE_OPTIONAL_HEADER64 (PE32+, 240 bytes) */
void pe_build_opt_header_64(uint8_t *buf, uint32_t ep_rva, uint32_t img_size,
                             uint32_t code_size, uint32_t idata_size,
                             uint32_t headers_size,
                             uint32_t import_rva,  uint32_t import_size,
                             uint32_t iat_rva,     uint32_t iat_size) {
    memset(buf, 0, sizeof(PE_OptionalHeader64));
    PE_OptionalHeader64 *oh = (PE_OptionalHeader64 *)buf;
    oh->Magic                       = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    oh->MajorLinkerVersion          = 14;
    oh->SizeOfCode                  = ALIGN_UP(code_size, FILE_ALIGN);
    oh->SizeOfInitializedData       = ALIGN_UP(idata_size, FILE_ALIGN);
    oh->AddressOfEntryPoint         = ep_rva;
    oh->BaseOfCode                  = SEC_ALIGN;
    oh->ImageBase                   = IMAGE_BASE_64;
    oh->SectionAlignment            = SEC_ALIGN;
    oh->FileAlignment               = FILE_ALIGN;
    oh->MajorOperatingSystemVersion = 4;
    oh->MajorSubsystemVersion       = 4;
    oh->SizeOfImage                 = img_size;
    oh->SizeOfHeaders               = headers_size;
    oh->Subsystem                   = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    /* NX-compat + DYNAMIC_BASE + terminal-server-aware (ASLR safe: uses RIP-rel) */
    oh->DllCharacteristics          = 0x0140;  /* DYNAMIC_BASE + NX_COMPAT */
    oh->SizeOfStackReserve          = 0x100000;
    oh->SizeOfStackCommit           = 0x1000;
    oh->SizeOfHeapReserve           = 0x100000;
    oh->SizeOfHeapCommit            = 0x1000;
    oh->NumberOfRvaAndSizes         = 16;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = import_rva;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size           = import_size;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress    = iat_rva;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size              = iat_size;
}

/* pe_build_section_header — IMAGE_SECTION_HEADER (40 bytes) */
void pe_build_section_header(uint8_t *buf, const char *name,
                              uint32_t vsize, uint32_t rva,
                              uint32_t rawsz, uint32_t rawptr,
                              uint32_t chars) {
    memset(buf, 0, sizeof(PE_SectionHeader));
    PE_SectionHeader *sh = (PE_SectionHeader *)buf;
    strncpy(sh->Name, name, 8);
    sh->VirtualSize      = vsize;
    sh->VirtualAddress   = rva;
    sh->SizeOfRawData    = rawsz;
    sh->PointerToRawData = rawptr;
    sh->Characteristics  = chars;
}

/* =========================================================================
 * Import grouping helpers
 * ========================================================================= */
typedef struct {
    char  *dll;
    char **funcs;
    int    func_count;
    int    func_cap;
} DLLGroup;

static DLLGroup *find_or_add_dll(DLLGroup *groups, int *gc, const char *dll) {
    for (int i=0;i<*gc;i++)
        if (strcasecmp(groups[i].dll, dll)==0) return &groups[i];
    DLLGroup *g = &groups[(*gc)++];
    g->dll       = strdup(dll);
    g->func_cap  = 8;
    g->funcs     = malloc(g->func_cap * sizeof(char*));
    g->func_count= 0;
    return g;
}
static void dll_group_add_func(DLLGroup *g, const char *func) {
    for (int i=0;i<g->func_count;i++)
        if (strcmp(g->funcs[i],func)==0) return; /* already present */
    if (g->func_count==g->func_cap) {
        g->func_cap*=2;
        g->funcs=realloc(g->funcs,g->func_cap*sizeof(char*));
    }
    g->funcs[g->func_count++] = strdup(func);
}

/* =========================================================================
 * pe_link_and_write  —  the linker: patch relocations, build import table,
 *                        emit PE file.
 * ========================================================================= */
int pe_link_and_write(PEBuildInput *in) {
    int is64 = in->is_64bit;
    int thunk_size = is64 ? 8 : 4;

    /* -----------------------------------------------------------------------
     * Step 1: Parse "DLL:func" import specs, group by DLL
     * -------------------------------------------------------------------- */
    DLLGroup groups[64]; int gc = 0;
    memset(groups, 0, sizeof groups);

    /* Always include ExitProcess and GetStdHandle / WriteFile for the shim */
    /* These are added by the code generator anyway, but ensure them here */
    for (int i=0;i<in->import_count;i++) {
        char spec[512];
        strncpy(spec, in->import_specs[i], sizeof spec-1);
        char *colon = strchr(spec, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *dll  = spec;
        const char *func = colon+1;
        DLLGroup *g = find_or_add_dll(groups, &gc, dll);
        dll_group_add_func(g, func);
    }
    /* If no imports at all, still need ExitProcess */
    if (gc == 0) {
        DLLGroup *g = find_or_add_dll(groups, &gc, "KERNEL32.dll");
        dll_group_add_func(g, "ExitProcess");
        dll_group_add_func(g, "GetStdHandle");
        dll_group_add_func(g, "WriteFile");
    }

    /* -----------------------------------------------------------------------
     * Step 2: Compute .rdata layout
     *
     * Layout (byte offsets from start of .rdata):
     *   [0]                   Import Descriptor table: (gc+1)*20 bytes
     *   [idir_size]           IAT blocks, thunk_size*(func_count+1) per DLL
     *   [idir+iat_total]      INT blocks (same)
     *   [idir+2*iat_total]    DLL name strings
     *   [...]                 Hint/Name entries
     *   [...]                 String pool (from codegen)
     * -------------------------------------------------------------------- */
    uint32_t idir_size = (uint32_t)((gc+1) * sizeof(PE_ImportDescriptor));

    /* Count total IAT slots across all DLLs */
    int total_funcs = 0;
    for (int i=0;i<gc;i++) total_funcs += groups[i].func_count;
    /* IAT: per DLL = (func_count + 1 null) thunks; total bytes: */
    uint32_t iat_total = 0;
    for (int i=0;i<gc;i++) iat_total += (uint32_t)(groups[i].func_count+1)*thunk_size;
    uint32_t int_total = iat_total;  /* INT mirrors IAT initially */

    /* Per-DLL IAT offsets within the IAT block */
    uint32_t *dll_iat_off = malloc(gc * sizeof(uint32_t));
    uint32_t  cur_iat = 0;
    for (int i=0;i<gc;i++) {
        dll_iat_off[i] = cur_iat;
        cur_iat += (uint32_t)(groups[i].func_count+1)*thunk_size;
    }

    /* DLL names */
    uint32_t dllnames_off = idir_size + iat_total + int_total;
    uint32_t *dll_name_off = malloc(gc * sizeof(uint32_t));
    uint32_t cur_name = dllnames_off;
    for (int i=0;i<gc;i++) {
        dll_name_off[i] = cur_name;
        cur_name += (uint32_t)strlen(groups[i].dll) + 1;
        if (cur_name & 1) cur_name++; /* word-align */
    }

    /* Hint/Name entries per function */
    /* Per function: 2-byte hint + name + null + optional pad byte */
    uint32_t hn_base = cur_name;
    uint32_t **hn_off = malloc(gc * sizeof(uint32_t*));
    uint32_t cur_hn = hn_base;
    for (int i=0;i<gc;i++) {
        hn_off[i] = malloc(groups[i].func_count * sizeof(uint32_t));
        for (int j=0;j<groups[i].func_count;j++) {
            hn_off[i][j] = cur_hn;
            cur_hn += 2 + (uint32_t)strlen(groups[i].funcs[j]) + 1;
            if (cur_hn & 1) cur_hn++; /* word-align */
        }
    }

    /* String pool follows */
    uint32_t strpool_off = cur_hn;
    uint32_t rdata_size  = strpool_off + (uint32_t)in->rdata_strings_len;
    rdata_size = ALIGN_UP(rdata_size, FILE_ALIGN);

    /* -----------------------------------------------------------------------
     * Step 3: Assign RVAs
     * Sections: .text=RVA1, .rdata=RVA2, .data=RVA3
     * Header size: figure out from structure sizes below.
     * -------------------------------------------------------------------- */
    /* Header block:
     *   DOS header 64 + stub 64 = 128 = 0x80
     *   PE sig 4, FileHdr 20, OptHdr 224 or 240, SectionHdr 4*40=160
     */
    uint32_t oh_size  = is64 ? (uint32_t)sizeof(PE_OptionalHeader64)
                              : (uint32_t)sizeof(PE_OptionalHeader32);
    uint32_t hdr_raw  = 0x80 + 4 + 20 + oh_size + 4*40; /* 4 sections */
    uint32_t hdr_size = ALIGN_UP(hdr_raw, FILE_ALIGN);

    uint32_t text_rva   = SEC_ALIGN;                           /* 0x1000 */
    uint32_t text_raw   = ALIGN_UP(in->text_len, FILE_ALIGN);
    uint32_t rdata_rva  = text_rva  + ALIGN_UP(text_raw,  SEC_ALIGN);
    uint32_t data_rva   = rdata_rva + ALIGN_UP(rdata_size, SEC_ALIGN);
    /* .data section holds the writable data pool (handle cache, static vars) */
    uint32_t wdata_size = (in->wdata_len > 0) ? (uint32_t)in->wdata_len : 0;
    uint32_t data_raw   = ALIGN_UP(wdata_size > 0 ? wdata_size : 1, FILE_ALIGN);
    uint32_t bss_rva    = data_rva  + SEC_ALIGN;  /* .bss after full .data page */
    uint32_t img_size   = bss_rva   + SEC_ALIGN;  /* SizeOfImage covers all sections */

    /* File offsets */
    uint32_t text_foff  = hdr_size;
    uint32_t rdata_foff = text_foff  + text_raw;
    uint32_t data_foff  = rdata_foff + rdata_size;

    /* IAT RVA within .rdata */
    uint32_t iat_rva_base = rdata_rva + idir_size;
    /* INT RVA within .rdata */
    uint32_t int_rva_base = iat_rva_base + iat_total;

    /* -----------------------------------------------------------------------
     * Step 4: Find entry point offset in .text
     * We look for the "main" function label in the assembler's label table.
     * -------------------------------------------------------------------- */
    uint32_t ep_offset = 0;  /* default: start of .text */
    if (in->entry_func) {
        /* The assembler emitted code; the entry function is at label offset.
         * We stored function offsets during codegen. For now we search
         * the first byte of .text as the first function. The codegen emits
         * functions in order; "main" is typically last or as specified.
         * We rely on the caller setting text so that entry_func's code
         * begins at ep_offset.
         * Since our codegen emits all user functions in source order and
         * we know main's offset from the label table (passed via relocs),
         * use reloc mechanism: a special RELOC_ABS32 with symbol "__entry__"
         * encodes the entry offset as its addend field. */
        for (int i=0;i<in->reloc_count;i++) {
            if (strcmp(in->relocs[i].symbol,"__entry__")==0) {
                ep_offset = (uint32_t)in->relocs[i].addend;
                break;
            }
        }
    }
    uint32_t ep_rva = text_rva + ep_offset;

    /* -----------------------------------------------------------------------
     * Step 5: Build .rdata blob
     * -------------------------------------------------------------------- */
    uint8_t *rdata = calloc(rdata_size, 1);

    /* --- Import Descriptor Table --- */
    for (int i=0;i<gc;i++) {
        uint32_t off = (uint32_t)(i * sizeof(PE_ImportDescriptor));
        PE_ImportDescriptor *id = (PE_ImportDescriptor *)(rdata + off);
        id->OriginalFirstThunk = int_rva_base + dll_iat_off[i]; /* INT */
        id->TimeDateStamp      = 0;
        id->ForwarderChain     = 0;  /* no forwarders */
        id->Name               = rdata_rva + dll_name_off[i];
        id->FirstThunk         = iat_rva_base + dll_iat_off[i]; /* IAT */
    }
    /* null terminator already zero from calloc */

    /* --- IAT --- */
    for (int i=0;i<gc;i++) {
        uint32_t base = idir_size + dll_iat_off[i];
        for (int j=0;j<groups[i].func_count;j++) {
            uint64_t hn_rva = rdata_rva + hn_off[i][j];
            if (thunk_size==8) pu64(rdata+base+j*8, hn_rva);
            else               pu32(rdata+base+j*4, (uint32_t)hn_rva);
        }
        /* null terminator already zero */
    }

    /* --- INT (mirrors IAT) --- */
    uint32_t int_block_start = idir_size + iat_total;
    for (int i=0;i<gc;i++) {
        uint32_t iat_off_abs = idir_size + dll_iat_off[i];
        uint32_t int_off_abs = int_block_start + dll_iat_off[i];
        memcpy(rdata + int_off_abs, rdata + iat_off_abs,
               (uint32_t)groups[i].func_count * thunk_size);
    }

    /* --- DLL names --- */
    for (int i=0;i<gc;i++) {
        strcpy((char*)rdata + dll_name_off[i], groups[i].dll);
    }

    /* --- Hint/Name entries --- */
    for (int i=0;i<gc;i++) {
        for (int j=0;j<groups[i].func_count;j++) {
            uint8_t *hn = rdata + hn_off[i][j];
            pu16(hn, 0);  /* hint = 0; loader will search by name */
            strcpy((char*)hn+2, groups[i].funcs[j]);
        }
    }

    /* --- String pool --- */
    if (in->rdata_strings && in->rdata_strings_len > 0)
        memcpy(rdata + strpool_off, in->rdata_strings, in->rdata_strings_len);

    /* -----------------------------------------------------------------------
     * Step 6: Patch relocations in .text
     *
     * For each relocation:
     *   RELOC_IAT_REL32:  patch offset with RIP-relative disp to IAT slot VA
     *   RELOC_ABS32:      patch with absolute VA of IAT slot or data label
     *   RELOC_DATA_REL32: patch with RIP-relative disp to string in .rdata
     * -------------------------------------------------------------------- */
    uint8_t *text = malloc(text_raw);
    memset(text, 0, text_raw);
    memcpy(text, in->text, in->text_len);

    uint64_t image_base = is64 ? IMAGE_BASE_64 : (uint64_t)IMAGE_BASE_32;

    /* Build a fast lookup: func name → IAT VA */
    typedef struct { char *name; uint64_t iat_va; } IATEntry;
    int n_iat_entries = total_funcs;
    IATEntry *iat_entries = malloc(n_iat_entries * sizeof(IATEntry));
    int iat_ei = 0;
    for (int i=0;i<gc;i++) {
        for (int j=0;j<groups[i].func_count;j++) {
            uint64_t slot_rva = iat_rva_base + dll_iat_off[i] + j*thunk_size;
            iat_entries[iat_ei].name   = groups[i].funcs[j];
            iat_entries[iat_ei].iat_va = image_base + slot_rva;
            iat_ei++;
        }
    }

    /* Build string label → VA lookup */
    /* (string labels are stored in the string pool at strpool_off) */

    for (int ri=0;ri<in->reloc_count;ri++) {
        Relocation *r = &in->relocs[ri];
        if (strcmp(r->symbol,"__entry__")==0) continue;

        int patch = r->offset;  /* offset in text[] */

        if (r->kind == RELOC_IAT_REL32 || r->kind == RELOC_ABS32) {
            /* Find the IAT slot for this symbol */
            uint64_t target_va = 0;
            for (int e=0;e<iat_ei;e++) {
                if (strcmp(iat_entries[e].name, r->symbol)==0) {
                    target_va = iat_entries[e].iat_va;
                    break;
                }
            }
            if (target_va == 0) {
                fprintf(stderr, "pe_link: no IAT entry for '%s'\n", r->symbol);
                /* Don't abort — just leave zero; will crash at runtime */
            }
            if (r->kind == RELOC_IAT_REL32) {
                /* RIP at next instruction = text_va + patch + 4 */
                uint64_t next_rip = image_base + text_rva + patch + 4;
                int32_t  disp     = (int32_t)(target_va - next_rip);
                text[patch+0] = (uint8_t)(disp);
                text[patch+1] = (uint8_t)(disp>>8);
                text[patch+2] = (uint8_t)(disp>>16);
                text[patch+3] = (uint8_t)(disp>>24);
            } else {
                /* RELOC_ABS32: write 32-bit absolute VA */
                pu32(text+patch, (uint32_t)target_va);
            }

        } else if (r->kind == RELOC_DATA_REL32 || r->kind == RELOC_DATA_ABS32) {
            /* Find string label in string pool */
            uint32_t str_rva = 0;
            for (int si=0;si<in->string_count;si++) {
                if (strcmp(in->string_labels[si], r->symbol)==0) {
                    str_rva = rdata_rva + strpool_off + in->string_offsets[si];
                    break;
                }
            }
            if (str_rva == 0) {
                fprintf(stderr,"pe_link: no string '%s' in pool\n", r->symbol);
            }
            if (r->kind == RELOC_DATA_REL32) {
                uint64_t target_va = image_base + str_rva;
                uint64_t next_rip  = image_base + text_rva + patch + 4;
                int32_t  disp      = (int32_t)(target_va - next_rip);
                text[patch+0] = (uint8_t)(disp);
                text[patch+1] = (uint8_t)(disp>>8);
                text[patch+2] = (uint8_t)(disp>>16);
                text[patch+3] = (uint8_t)(disp>>24);
            } else {
                /* RELOC_DATA_ABS32: 32-bit absolute VA */
                pu32(text+patch, (uint32_t)(image_base + str_rva));
            }

        } else if (r->kind == RELOC_WDATA_REL32 || r->kind == RELOC_WDATA_ABS32) {
            /* Find label in writable data pool (.data section) */
            uint32_t wdata_rva_base = data_rva;
            uint32_t wdata_off_in_section = 0;
            int found_w = 0;
            for (int wi=0; wi < in->wdata_count; wi++) {
                if (strcmp(in->wdata_labels[wi], r->symbol)==0) {
                    wdata_off_in_section = (uint32_t)in->wdata_offsets[wi];
                    found_w = 1;
                    break;
                }
            }
            if (!found_w) {
                fprintf(stderr,"pe_link: no wdata label '%s'\n", r->symbol);
            } else {
                uint32_t target_rva = wdata_rva_base + wdata_off_in_section;
                if (r->kind == RELOC_WDATA_REL32) {
                    uint64_t target_va = image_base + target_rva;
                    uint64_t next_rip  = image_base + text_rva + patch + 4;
                    int32_t  disp      = (int32_t)(target_va - next_rip);
                    text[patch+0] = (uint8_t)(disp);
                    text[patch+1] = (uint8_t)(disp>>8);
                    text[patch+2] = (uint8_t)(disp>>16);
                    text[patch+3] = (uint8_t)(disp>>24);
                } else {
                    pu32(text+patch, (uint32_t)(image_base + target_rva));
                }
            }
        }
    }
    free(iat_entries);

    /* -----------------------------------------------------------------------
     * Step 7: Build header block
     * -------------------------------------------------------------------- */
    uint8_t *hdr = calloc(hdr_size, 1);

    pe_build_dos_header(hdr + 0x00, 0x80);
    pe_build_dos_stub  (hdr + 0x40);
    pe_build_pe_signature(hdr + 0x80);

    uint32_t iat_data_size = iat_total;
    if (is64) {
        pe_build_file_header(hdr+0x84,
            IMAGE_FILE_MACHINE_AMD64, 4,
            (uint16_t)sizeof(PE_OptionalHeader64),
            IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE);
        pe_build_opt_header_64(hdr+0x98, ep_rva, img_size,
            in->text_len, rdata_size, hdr_size,
            rdata_rva + 0,           /* import dir RVA = start of .rdata */
            (uint32_t)(gc+1)*20,     /* import dir size */
            iat_rva_base, iat_data_size);
    } else {
        pe_build_file_header(hdr+0x84,
            IMAGE_FILE_MACHINE_I386, 4,
            (uint16_t)sizeof(PE_OptionalHeader32),
            IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE);
        pe_build_opt_header_32(hdr+0x98, ep_rva, img_size,
            in->text_len, rdata_size, hdr_size);
        /* Patch import DataDirectory[1] into PE32 optional header */
        /* DataDir starts at oh_off + 96 bytes into opt header */
        uint32_t dd_off = 0x98 + 96 + 8; /* +8 = [1] */
        pu32(hdr + dd_off,     rdata_rva);          /* import RVA */
        pu32(hdr + dd_off + 4, (uint32_t)(gc+1)*20);/* import size */
        /* IAT dir [12] */
        uint32_t iat_dd_off = 0x98 + 96 + 12*8;
        pu32(hdr + iat_dd_off,     iat_rva_base);
        pu32(hdr + iat_dd_off + 4, iat_data_size);
    }

    /* Section table: placed right after optional header */
    uint32_t sh_base = 0x98 + oh_size;
    pe_build_section_header(hdr+sh_base+0*40, ".text",
        (uint32_t)in->text_len, text_rva,  text_raw,  text_foff,  SCN_CODE);
    pe_build_section_header(hdr+sh_base+1*40, ".rdata",
        rdata_size, rdata_rva, rdata_size, rdata_foff, SCN_RDATA);
    pe_build_section_header(hdr+sh_base+2*40, ".data",
        data_raw, data_rva, data_raw, data_foff, SCN_DATA);
    pe_build_section_header(hdr+sh_base+3*40, ".bss",
        SEC_ALIGN, bss_rva, 0, 0,
        0x00000080|0x40000000|0x80000000); /* CNT_UNINIT|R|W */

    /* -----------------------------------------------------------------------
     * Step 8: Write the output PE file
     * -------------------------------------------------------------------- */
    FILE *fp = fopen(in->output_path, "wb");
    if (!fp) { perror("fopen output"); return 1; }

    uint8_t *data_section = calloc(data_raw, 1);
    /* Copy wdata pool into .data section */
    if (in->wdata_bytes && in->wdata_len > 0)
        memcpy(data_section, in->wdata_bytes, in->wdata_len);

    fwrite(hdr,          1, hdr_size,  fp);
    fwrite(text,         1, text_raw,  fp);
    fwrite(rdata,        1, rdata_size,fp);
    fwrite(data_section, 1, data_raw,  fp);

    fclose(fp);

    /* Compute and patch the PE checksum.
     * The checksum is at: e_lfanew + 4 + 20 + 64 = e_lfanew + 88 bytes.
     * Algorithm: sum all 16-bit words, fold carry, add file size.
     * We re-read the whole file, compute, then patch the 4-byte field. */
    {
        FILE *ckf = fopen(in->output_path, "r+b");
        if (ckf) {
            fseek(ckf, 0, SEEK_END);
            long fsize = ftell(ckf);
            rewind(ckf);
            uint8_t *fbuf = calloc((fsize + 1) & ~1, 1);
            fread(fbuf, 1, fsize, ckf);

            /* Find checksum field offset: [e_lfanew]+88 */
            uint32_t e_lfanew_v;
            memcpy(&e_lfanew_v, fbuf + 0x3C, 4);
            uint32_t ckoff = e_lfanew_v + 4 + 20 + 64;  /* CheckSum field */
            /* Zero out checksum field before computing */
            fbuf[ckoff]=fbuf[ckoff+1]=fbuf[ckoff+2]=fbuf[ckoff+3]=0;

            /* 16-bit word sum with carry folding */
            uint32_t ck = 0;
            long words = (fsize + 1) / 2;
            for (long wi = 0; wi < words; wi++) {
                uint16_t w = (uint16_t)fbuf[wi*2] | ((uint16_t)fbuf[wi*2+1]<<8);
                ck += w;
                if (ck > 0xFFFF) ck = (ck & 0xFFFF) + (ck >> 16);
            }
            ck += (uint32_t)fsize;

            /* Write checksum back */
            fbuf[ckoff+0]=(uint8_t)(ck);
            fbuf[ckoff+1]=(uint8_t)(ck>>8);
            fbuf[ckoff+2]=(uint8_t)(ck>>16);
            fbuf[ckoff+3]=(uint8_t)(ck>>24);
            rewind(ckf);
            fwrite(fbuf, 1, fsize, ckf);
            fclose(ckf);
            free(fbuf);
        }
    }

    printf("PE written: %s\n", in->output_path);
    printf("  %-10s file=0x%04X  RVA=0x%04X  size=%u\n",
           ".text",  text_foff,  text_rva,  (unsigned)in->text_len);
    printf("  %-10s file=0x%04X  RVA=0x%04X  size=%u\n",
           ".rdata", rdata_foff, rdata_rva, rdata_size);
    printf("  %-10s file=0x%04X  RVA=0x%04X  size=%u\n",
           ".data",  data_foff,  data_rva,  data_raw);
    printf("  EntryPoint: RVA=0x%04X\n", ep_rva);
    printf("  ImageBase:  %s\n", is64 ? "0x140000000" : "0x00400000");
    printf("  Imports (%d DLLs):\n", gc);
    for (int i=0;i<gc;i++) {
        printf("    %s: ", groups[i].dll);
        for (int j=0;j<groups[i].func_count;j++)
            printf("%s%s", groups[i].funcs[j], j+1<groups[i].func_count?", ":"");
        printf("\n");
    }

    /* Cleanup */
    free(hdr); free(text); free(rdata); free(data_section);
    free(dll_iat_off); free(dll_name_off);
    for (int i=0;i<gc;i++) { free(hn_off[i]); free(groups[i].dll); free(groups[i].funcs); }
    free(hn_off);
    return 0;
}
