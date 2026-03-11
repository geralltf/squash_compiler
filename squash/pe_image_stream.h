/*
The MIT License (MIT)

Copyright (c) 2013 Andrew Ruef

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef PE_IMAGE_STREAM_H
#define PE_IMAGE_STREAM_H

//#include <cstdint>
//#include <map>
//#include <string>
#include <stdbool.h>
#include <uchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "debugent.h"
#include "exportent.h"
#include "buffer.h"
#include "nt-headers.h"
#include "List.h"

/* ===========================================================================
 * Shared PE structure definitions
 * ========================================================================= */

 /* IMAGE_DOS_HEADER  (identical layout in PE32 and PE32+) */
typedef struct {
    uint16_t e_magic;       /* "MZ" = 0x5A4D                                 */
    uint16_t e_cblp;        /* Bytes on last page of file                     */
    uint16_t e_cp;          /* Pages in file                                  */
    uint16_t e_crlc;        /* Relocations                                    */
    uint16_t e_cparhdr;     /* Size of header in paragraphs                   */
    uint16_t e_minalloc;    /* Minimum extra paragraphs                       */
    uint16_t e_maxalloc;    /* Maximum extra paragraphs                       */
    uint16_t e_ss;          /* Initial (relative) SS value                    */
    uint16_t e_sp;          /* Initial SP value                               */
    uint16_t e_csum;        /* Checksum                                       */
    uint16_t e_ip;          /* Initial IP value                               */
    uint16_t e_cs;          /* Initial (relative) CS value                    */
    uint16_t e_lfarlc;      /* File offset of relocation table                */
    uint16_t e_ovno;        /* Overlay number                                 */
    uint16_t e_res[4];      /* Reserved                                       */
    uint16_t e_oemid;       /* OEM identifier                                 */
    uint16_t e_oeminfo;     /* OEM information                                */
    uint16_t e_res2[10];    /* Reserved                                       */
    uint32_t e_lfanew;      /* File offset of PE signature ("PE\0\0")         */
} DOSHeader;

/* IMAGE_FILE_HEADER  (identical layout; machine type differs by target) */
typedef struct {
    uint16_t Machine;               /* 0x014C=x86  0x8664=x86-64             */
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;  /* 0 for images                           */
    uint32_t NumberOfSymbols;       /* 0 for images                           */
    uint16_t SizeOfOptionalHeader;  /* 224 for PE32 / 240 for PE32+           */
    uint16_t Characteristics;
} FileHeader;

/* Data directory entry  (same in PE32 and PE32+) */
typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} DataDirectory;

/*
 * IMAGE_OPTIONAL_HEADER  (PE32 / 32-bit)
 * Notable differences from PE32+:
 *   - Magic         = 0x010B
 *   - BaseOfData    field present (removed in PE32+)
 *   - ImageBase     is 32-bit
 *   - Stack/heap    fields are 32-bit
 *   - sizeof        = 224 bytes
 */
typedef struct {
    uint16_t Magic;                         /* 0x010B                        */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;                    /* PE32 only                     */
    uint32_t ImageBase;                     /* 32-bit preferred base         */
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;             /* reserved, 0                   */
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;            /* 32-bit in PE32                */
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;                   /* reserved, 0                   */
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectory[16];
} OptionalHeader32;

/*
 * IMAGE_OPTIONAL_HEADER64  (PE32+ / 64-bit)
 * Notable differences from PE32:
 *   - Magic         = 0x020B
 *   - No BaseOfData field
 *   - ImageBase     is 64-bit
 *   - Stack/heap    fields are 64-bit
 *   - sizeof        = 240 bytes
 */
typedef struct {
    uint16_t Magic;                         /* 0x020B                        */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;                     /* 64-bit preferred base         */
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;             /* reserved, 0                   */
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;            /* 64-bit in PE32+               */
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;                   /* reserved, 0                   */
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectory[16];
} OptionalHeader64;

/* IMAGE_SECTION_HEADER  (identical layout in PE32 and PE32+) */
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
} SectionHeader;

/* IMAGE_IMPORT_DESCRIPTOR  (identical; IAT/INT thunk *width* differs) */
typedef struct {
    uint32_t OriginalFirstThunk;  /* RVA of INT                               */
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;                /* RVA of DLL name string                   */
    uint32_t FirstThunk;          /* RVA of IAT                               */
} ImportDescriptor;

/* ===========================================================================
 * Shared constants
 * ========================================================================= */

#define IMAGE_DOS_SIGNATURE              0x5A4D
#define IMAGE_FILE_MACHINE_I386          0x014C
#define IMAGE_FILE_MACHINE_AMD64         0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE      0x0002
#define IMAGE_FILE_32BIT_MACHINE         0x0100
#define IMAGE_FILE_LARGE_ADDRESS_AWARE   0x0020
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC    0x010B
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC    0x020B
#define IMAGE_SUBSYSTEM_WINDOWS_CUI      3

#define IMAGE_SCN_CNT_CODE               0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040
#define IMAGE_SCN_MEM_EXECUTE            0x20000000
#define IMAGE_SCN_MEM_READ               0x40000000
#define IMAGE_SCN_MEM_WRITE              0x80000000

#define IMAGE_DIRECTORY_ENTRY_IMPORT     1
#define IMAGE_DIRECTORY_ENTRY_IAT        12

#define FILE_ALIGNMENT                   0x200
#define SECTION_ALIGNMENT                0x1000
#define RAW_SECTION_SIZE                 0x200

#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

 /* Message shared by both build targets */
static const char    HELLO_MSG[] = "Hello, World!\r\n";
static const uint32_t MSG_LEN = 15;

///////////////////////////////////////////////////

int build_pe32(const char* outfile, unsigned char* sq_program_image, int sq_program_image_length);
int build_pe64(const char* outfile, unsigned char* sq_program_image, int sq_program_image_length);
int build_pe_console(const char* outfile, bool is64bit, unsigned char* sq_program_image, int sq_program_image_length);

bool WritePEProgramSQImage(const char* fileName, unsigned char* sq_program_image, int sq_program_image_length);

#endif