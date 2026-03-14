#ifndef PE_BUILDER_H
#define PE_BUILDER_H

#include <stdint.h>
#include <stddef.h>
#include "assembler.h"

/* =========================================================================
 * PE structure definitions (shared PE32 and PE32+)
 * ========================================================================= */

typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} PE_DOSHeader;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} PE_FileHeader;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} PE_DataDirectory;

/* PE32 optional header (32-bit) */
typedef struct {
    uint16_t Magic;                      /* 0x010B */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    PE_DataDirectory DataDirectory[16];
} PE_OptionalHeader32;

/* PE32+ optional header (64-bit) */
typedef struct {
    uint16_t Magic;                      /* 0x020B */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    PE_DataDirectory DataDirectory[16];
} PE_OptionalHeader64;

typedef struct {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} PE_SectionHeader;

typedef struct {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} PE_ImportDescriptor;

/* =========================================================================
 * Import entry: one function from one DLL
 * ========================================================================= */
typedef struct {
    char *dll;      /* e.g. "KERNEL32.dll"   */
    char *func;     /* e.g. "WriteFile"      */
} ImportEntry;

/* =========================================================================
 * PE build input descriptor
 * ========================================================================= */
typedef struct {
    int is_64bit;

    /* .text section: raw machine code from assembler */
    uint8_t *text;
    int      text_len;

    /* .rdata string pool (from codegen) */
    uint8_t *rdata_strings;
    int      rdata_strings_len;

    /* Relocations to patch: IAT calls and data references */
    Relocation *relocs;
    int         reloc_count;

    /* String labels: name -> offset within rdata_strings */
    /* Passed as parallel arrays */
    char **string_labels;
    int   *string_offsets;
    int    string_count;

    /* Imports: "DLL:func" pairs derived from symtable */
    char **import_specs;   /* "KERNEL32.dll:WriteFile" */
    int    import_count;

    /* Entry point: name of the function to use as entry */
    char *entry_func;      /* e.g. "main" */

    /* Output file path */
    const char *output_path;
} PEBuildInput;

/* =========================================================================
 * API — each section has its own builder function
 * ========================================================================= */
void pe_build_dos_header    (uint8_t *buf, uint32_t e_lfanew);
void pe_build_dos_stub      (uint8_t *buf);
void pe_build_pe_signature  (uint8_t *buf);
void pe_build_file_header   (uint8_t *buf, uint16_t machine, uint16_t nsec,
                             uint16_t opt_size, uint16_t chars);
void pe_build_opt_header_32 (uint8_t *buf, uint32_t ep_rva, uint32_t img_size,
                             uint32_t code_size, uint32_t idata_size,
                             uint32_t headers_size);
void pe_build_opt_header_64 (uint8_t *buf, uint32_t ep_rva, uint32_t img_size,
                             uint32_t code_size, uint32_t idata_size,
                             uint32_t headers_size,
                             uint32_t import_rva,  uint32_t import_size,
                             uint32_t iat_rva,     uint32_t iat_size);
void pe_build_section_header(uint8_t *buf, const char *name,
                             uint32_t vsize, uint32_t rva,
                             uint32_t rawsz, uint32_t rawptr,
                             uint32_t chars);

/* Linking: patch relocations, build import table */
int  pe_link_and_write(PEBuildInput *in);

#endif /* PE_BUILDER_H */
