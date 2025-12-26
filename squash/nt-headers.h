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

#ifndef NT_HEADERS_H
#define NT_HEADERS_H

#include <stdint.h>

#include "List.h"

const uint32_t RICH_MAGIC_END = 0x68636952;
const uint32_t RICH_MAGIC_START = 0x536e6144;
const uint32_t RICH_OFFSET = 0x80;
const uint16_t MZ_MAGIC = 0x5A4D;
const uint32_t NT_MAGIC = 0x00004550;
const uint16_t NUM_DIR_ENTRIES = 16;
const uint16_t NT_OPTIONAL_32_MAGIC = 0x10B;
const uint16_t NT_OPTIONAL_64_MAGIC = 0x20B;
const uint16_t NT_SHORT_NAME_LEN = 8;
const uint16_t SYMTAB_RECORD_LEN = 18;

#ifndef _PEPARSE_WINDOWS_CONFLICTS
// Machine Types
const uint16_t IMAGE_FILE_MACHINE_UNKNOWN = 0x0;
const uint16_t IMAGE_FILE_MACHINE_ALPHA = 0x184;     // Alpha_AXP
const uint16_t IMAGE_FILE_MACHINE_ALPHA64 = 0x284;   // ALPHA64
const uint16_t IMAGE_FILE_MACHINE_AM33 = 0x1d3;      // Matsushita AM33
const uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;    // x64
const uint16_t IMAGE_FILE_MACHINE_ARM = 0x1c0;       // ARM little endian
const uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xaa64;    // ARM64 little endian
const uint16_t IMAGE_FILE_MACHINE_ARMNT = 0x1c4;     // ARM Thumb-2 little endian
const uint16_t IMAGE_FILE_MACHINE_AXP64 = 0x284;     // ALPHA64
const uint16_t IMAGE_FILE_MACHINE_CEE = 0xc0ee;
const uint16_t IMAGE_FILE_MACHINE_CEF = 0xcef;
const uint16_t IMAGE_FILE_MACHINE_EBC = 0xebc;       // EFI byte code
const uint16_t IMAGE_FILE_MACHINE_I386 = 0x14c;      // Intel 386 or later processors and compatible processors
const uint16_t IMAGE_FILE_MACHINE_IA64 = 0x200;      // Intel Itanium processor family
const uint16_t IMAGE_FILE_MACHINE_LOONGARCH32 = 0x6232; // LoongArch 32-bit address space
const uint16_t IMAGE_FILE_MACHINE_LOONGARCH64 = 0x6264; // LoongArch 64-bit address space
const uint16_t IMAGE_FILE_MACHINE_M32R = 0x9041;     // Mitsubishi M32R little endian
const uint16_t IMAGE_FILE_MACHINE_MIPS16 = 0x266;    // MIPS16
const uint16_t IMAGE_FILE_MACHINE_MIPSFPU = 0x366;   // MIPS with FPU
const uint16_t IMAGE_FILE_MACHINE_MIPSFPU16 = 0x466; // MIPS16 with FPU
const uint16_t IMAGE_FILE_MACHINE_POWERPC = 0x1f0;   // Power PC little endian
const uint16_t IMAGE_FILE_MACHINE_POWERPCFP = 0x1f1; // Power PC with floating point support
const uint16_t IMAGE_FILE_MACHINE_POWERPCBE = 0x1f2; // Power PC big endian
const uint16_t IMAGE_FILE_MACHINE_R3000 = 0x162;     // MIPS little endian, 0x160 big-endian
const uint16_t IMAGE_FILE_MACHINE_R4000 = 0x166;     // MIPS little endian
const uint16_t IMAGE_FILE_MACHINE_R10000 = 0x168;    // MIPS little endian
const uint16_t IMAGE_FILE_MACHINE_RISCV32 = 0x5032;  // RISC-V 32-bit address space
const uint16_t IMAGE_FILE_MACHINE_RISCV64 = 0x5064;  // RISC-V 64-bit address space
const uint16_t IMAGE_FILE_MACHINE_RISCV128 = 0x5128; // RISC-V 128-bit address space
const uint16_t IMAGE_FILE_MACHINE_SH3 = 0x1a2;       // Hitachi SH3
const uint16_t IMAGE_FILE_MACHINE_SH3DSP = 0x1a3;    // Hitachi SH3 DSP
const uint16_t IMAGE_FILE_MACHINE_SH3E = 0x1a4;      // Hitachi SH3E
const uint16_t IMAGE_FILE_MACHINE_SH4 = 0x1a6;       // Hitachi SH4
const uint16_t IMAGE_FILE_MACHINE_SH5 = 0x1a8;       // Hitachi SH5
const uint16_t IMAGE_FILE_MACHINE_THUMB = 0x1c2;     // Thumb
const uint16_t IMAGE_FILE_MACHINE_TRICORE = 0x520;   // Infineon
const uint16_t IMAGE_FILE_MACHINE_WCEMIPSV2 = 0x169; // MIPS little-endian WCE v2

const uint16_t IMAGE_FILE_RELOCS_STRIPPED = 0x0001;
const uint16_t IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002;
const uint16_t IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004;
const uint16_t IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008;
const uint16_t IMAGE_FILE_AGGRESSIVE_WS_TRIM = 0x0010;
const uint16_t IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020;
const uint16_t IMAGE_FILE_BYTES_REVERSED_LO = 0x0080;
const uint16_t IMAGE_FILE_32BIT_MACHINE = 0x0100;
const uint16_t IMAGE_FILE_DEBUG_STRIPPED = 0x0200;
const uint16_t IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = 0x0400;
const uint16_t IMAGE_FILE_NET_RUN_FROM_SWAP = 0x0800;
const uint16_t IMAGE_FILE_SYSTEM = 0x1000;
const uint16_t IMAGE_FILE_DLL = 0x2000;
const uint16_t IMAGE_FILE_UP_SYSTEM_ONLY = 0x4000;
const uint16_t IMAGE_FILE_BYTES_REVERSED_HI = 0x8000;

const uint32_t IMAGE_SCN_TYPE_NO_PAD = 0x00000008;
const uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
const uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
const uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
const uint32_t IMAGE_SCN_LNK_OTHER = 0x00000100;
const uint32_t IMAGE_SCN_LNK_INFO = 0x00000200;
const uint32_t IMAGE_SCN_LNK_REMOVE = 0x00000800;
const uint32_t IMAGE_SCN_LNK_COMDAT = 0x00001000;
const uint32_t IMAGE_SCN_NO_DEFER_SPEC_EXC = 0x00004000;
const uint32_t IMAGE_SCN_GPREL = 0x00008000;
const uint32_t IMAGE_SCN_MEM_FARDATA = 0x00008000;
const uint32_t IMAGE_SCN_MEM_PURGEABLE = 0x00020000;
const uint32_t IMAGE_SCN_MEM_16BIT = 0x00020000;
const uint32_t IMAGE_SCN_MEM_LOCKED = 0x00040000;
const uint32_t IMAGE_SCN_MEM_PRELOAD = 0x00080000;
const uint32_t IMAGE_SCN_ALIGN_1BYTES = 0x00100000;
const uint32_t IMAGE_SCN_ALIGN_2BYTES = 0x00200000;
const uint32_t IMAGE_SCN_ALIGN_4BYTES = 0x00300000;
const uint32_t IMAGE_SCN_ALIGN_8BYTES = 0x00400000;
const uint32_t IMAGE_SCN_ALIGN_16BYTES = 0x00500000;
const uint32_t IMAGE_SCN_ALIGN_32BYTES = 0x00600000;
const uint32_t IMAGE_SCN_ALIGN_64BYTES = 0x00700000;
const uint32_t IMAGE_SCN_ALIGN_128BYTES = 0x00800000;
const uint32_t IMAGE_SCN_ALIGN_256BYTES = 0x00900000;
const uint32_t IMAGE_SCN_ALIGN_512BYTES = 0x00A00000;
const uint32_t IMAGE_SCN_ALIGN_1024BYTES = 0x00B00000;
const uint32_t IMAGE_SCN_ALIGN_2048BYTES = 0x00C00000;
const uint32_t IMAGE_SCN_ALIGN_4096BYTES = 0x00D00000;
const uint32_t IMAGE_SCN_ALIGN_8192BYTES = 0x00E00000;
const uint32_t IMAGE_SCN_ALIGN_MASK = 0x00F00000;
const uint32_t IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000;
const uint32_t IMAGE_SCN_MEM_DISCARDABLE = 0x02000000;
const uint32_t IMAGE_SCN_MEM_NOT_CACHED = 0x04000000;
const uint32_t IMAGE_SCN_MEM_NOT_PAGED = 0x08000000;
const uint32_t IMAGE_SCN_MEM_SHARED = 0x10000000;
const uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
const uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
const uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;

const uint16_t IMAGE_SUBSYSTEM_UNKNOWN = 0;
const uint16_t IMAGE_SUBSYSTEM_NATIVE = 1;
const uint16_t IMAGE_SUBSYSTEM_WINDOWS_GUI = 2;
const uint16_t IMAGE_SUBSYSTEM_WINDOWS_CUI = 3;
const uint16_t IMAGE_SUBSYSTEM_OS2_CUI = 5;
const uint16_t IMAGE_SUBSYSTEM_POSIX_CUI = 7;
const uint16_t IMAGE_SUBSYSTEM_NATIVE_WINDOWS = 8;
const uint16_t IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9;
const uint16_t IMAGE_SUBSYSTEM_EFI_APPLICATION = 10;
const uint16_t IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11;
const uint16_t IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12;
const uint16_t IMAGE_SUBSYSTEM_EFI_ROM = 13;
const uint16_t IMAGE_SUBSYSTEM_XBOX = 14;
const uint16_t IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16;
const uint16_t IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG = 17;

// Symbol section number values
const int16_t IMAGE_SYM_UNDEFINED = 0;
const int16_t IMAGE_SYM_ABSOLUTE = -1;
const int16_t IMAGE_SYM_DEBUG = -2;

// Symbol table types
const uint16_t IMAGE_SYM_TYPE_NULL = 0;
const uint16_t IMAGE_SYM_TYPE_VOID = 1;
const uint16_t IMAGE_SYM_TYPE_CHAR = 2;
const uint16_t IMAGE_SYM_TYPE_SHORT = 3;
const uint16_t IMAGE_SYM_TYPE_INT = 4;
const uint16_t IMAGE_SYM_TYPE_LONG = 5;
const uint16_t IMAGE_SYM_TYPE_FLOAT = 6;
const uint16_t IMAGE_SYM_TYPE_DOUBLE = 7;
const uint16_t IMAGE_SYM_TYPE_STRUCT = 8;
const uint16_t IMAGE_SYM_TYPE_UNION = 9;
const uint16_t IMAGE_SYM_TYPE_ENUM = 10;
const uint16_t IMAGE_SYM_TYPE_MOE = 11;
const uint16_t IMAGE_SYM_TYPE_BYTE = 12;
const uint16_t IMAGE_SYM_TYPE_WORD = 13;
const uint16_t IMAGE_SYM_TYPE_UINT = 14;
const uint16_t IMAGE_SYM_TYPE_DWORD = 15;
const uint16_t IMAGE_SYM_DTYPE_NULL = 0;
const uint16_t IMAGE_SYM_DTYPE_POINTER = 1;
const uint16_t IMAGE_SYM_DTYPE_FUNCTION = 2;
const uint16_t IMAGE_SYM_DTYPE_ARRAY = 3;

// Symbol table storage classes
const uint8_t IMAGE_SYM_CLASS_END_OF_FUNCTION = 0;// static_cast<uint8_t>(-1);
const uint8_t IMAGE_SYM_CLASS_NULL = 0;
const uint8_t IMAGE_SYM_CLASS_AUTOMATIC = 1;
const uint8_t IMAGE_SYM_CLASS_EXTERNAL = 2;
const uint8_t IMAGE_SYM_CLASS_STATIC = 3;
const uint8_t IMAGE_SYM_CLASS_REGISTER = 4;
const uint8_t IMAGE_SYM_CLASS_EXTERNAL_DEF = 5;
const uint8_t IMAGE_SYM_CLASS_LABEL = 6;
const uint8_t IMAGE_SYM_CLASS_UNDEFINED_LABEL = 7;
const uint8_t IMAGE_SYM_CLASS_MEMBER_OF_STRUCT = 8;
const uint8_t IMAGE_SYM_CLASS_ARGUMENT = 9;
const uint8_t IMAGE_SYM_CLASS_STRUCT_TAG = 10;
const uint8_t IMAGE_SYM_CLASS_MEMBER_OF_UNION = 11;
const uint8_t IMAGE_SYM_CLASS_UNION_TAG = 12;
const uint8_t IMAGE_SYM_CLASS_TYPE_DEFINITION = 13;
const uint8_t IMAGE_SYM_CLASS_UNDEFINED_STATIC = 14;
const uint8_t IMAGE_SYM_CLASS_ENUM_TAG = 15;
const uint8_t IMAGE_SYM_CLASS_MEMBER_OF_ENUM = 16;
const uint8_t IMAGE_SYM_CLASS_REGISTER_PARAM = 17;
const uint8_t IMAGE_SYM_CLASS_BIT_FIELD = 18;
const uint8_t IMAGE_SYM_CLASS_BLOCK = 100;
const uint8_t IMAGE_SYM_CLASS_FUNCTION = 101;
const uint8_t IMAGE_SYM_CLASS_END_OF_STRUCT = 102;
const uint8_t IMAGE_SYM_CLASS_FILE = 103;
const uint8_t IMAGE_SYM_CLASS_SECTION = 104;
const uint8_t IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105;
const uint8_t IMAGE_SYM_CLASS_CLR_TOKEN = 107;

// Optional header DLL characteristics
const uint16_t IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020;
const uint16_t IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040;
const uint16_t IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY = 0x0080;
const uint16_t IMAGE_DLLCHARACTERISTICS_NX_COMPAT = 0x0100;
const uint16_t IMAGE_DLLCHARACTERISTICS_NO_ISOLATION = 0x0200;
const uint16_t IMAGE_DLLCHARACTERISTICS_NO_SEH = 0x0400;
const uint16_t IMAGE_DLLCHARACTERISTICS_NO_BIND = 0x0800;
const uint16_t IMAGE_DLLCHARACTERISTICS_APPCONTAINER = 0x1000;
const uint16_t IMAGE_DLLCHARACTERISTICS_WDM_DRIVER = 0x2000;
const uint16_t IMAGE_DLLCHARACTERISTICS_GUARD_CF = 0x4000;
const uint16_t IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000;

// Extended DLL characteristics
const uint16_t IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT = 0x0001;

#endif

struct dos_header 
{
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
};

struct file_header 
{
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct data_directory 
{
    uint32_t VirtualAddress;
    uint32_t Size;
};

enum data_directory_kind 
{
    DIR_EXPORT = 0,
    DIR_IMPORT = 1,
    DIR_RESOURCE = 2,
    DIR_EXCEPTION = 3,
    DIR_SECURITY = 4,
    DIR_BASERELOC = 5,
    DIR_DEBUG = 6,
    DIR_ARCHITECTURE = 7,
    DIR_GLOBALPTR = 8,
    DIR_TLS = 9,
    DIR_LOAD_CONFIG = 10,
    DIR_BOUND_IMPORT = 11,
    DIR_IAT = 12,
    DIR_DELAY_IMPORT = 13,
    DIR_COM_DESCRIPTOR = 14,
    DIR_RESERVED = 15,
};

struct optional_header_32 
{
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
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
    struct data_directory* DataDirectory;// [NUM_DIR_ENTRIES] ;
};

/*
* This is used for PE32+ binaries. It is similar to optional_header_32
* except some fields don't exist here (BaseOfData), and others are bigger.
*/
struct optional_header_64 
{
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
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
    struct data_directory DataDirectory;// [NUM_DIR_ENTRIES] ;
};

struct nt_header_32 
{
    uint32_t Signature;
    struct file_header* FileHeader;
    struct optional_header_32* OptionalHeader;
    struct optional_header_64* OptionalHeader64;
    uint16_t OptionalMagic;
};

struct rich_entry 
{
    uint16_t ProductId;
    uint16_t BuildNumber;
    uint32_t Count;
};

struct rich_header 
{
    uint32_t StartSignature;
    list_t* Entries; //std::vector<rich_entry> Entries;
    uint32_t EndSignature;
    uint32_t DecryptionKey;
    uint32_t Checksum;
    bool isPresent;
    bool isValid;
};

/*
* This structure is only used to know how far to move the offset
* when parsing resources. The data is stored in a resource_dir_entry
* struct but that also has extra information used in the parsing which
* causes the size to be inaccurate.
*/
struct resource_dir_entry_sz 
{
    uint32_t ID;
    uint32_t RVA;
};

struct resource_dir_entry 
{
    //inline resource_dir_entry(void) : ID(0), RVA(0), type(0), name(0), lang(0)
    //{
    //}

    uint32_t ID;
    uint32_t RVA;
    uint32_t type;
    uint32_t name;
    uint32_t lang;
    char* type_str;
    char* name_str;
    char* lang_str;
};

struct resource_dir_table 
{
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint16_t NameEntries;
    uint16_t IDEntries;
};

struct resource_dat_entry 
{
    uint32_t RVA;
    uint32_t size;
    uint32_t codepage;
    uint32_t reserved;
};

struct image_section_header 
{
    uint8_t* Name;// [NT_SHORT_NAME_LEN] ;
    union {
        uint32_t PhysicalAddress;
        uint32_t VirtualSize;
    } Misc;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct import_dir_entry 
{
    uint32_t LookupTableRVA;
    uint32_t TimeStamp;
    uint32_t ForwarderChain;
    uint32_t NameRVA;
    uint32_t AddressRVA;
};

struct export_dir_table 
{
    uint32_t ExportFlags;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t NameRVA;
    uint32_t OrdinalBase;
    uint32_t AddressTableEntries;
    uint32_t NumberOfNamePointers;
    uint32_t ExportAddressTableRVA;
    uint32_t NamePointerRVA;
    uint32_t OrdinalTableRVA;
};

struct debug_dir_entry 
{
    uint32_t Characteristics;
    uint32_t TimeStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Type;
    uint32_t SizeOfData;
    uint32_t AddressOfRawData;
    uint32_t PointerToRawData;
};

enum reloc_type 
{
    RELOC_ABSOLUTE = 0,
    RELOC_HIGH = 1,
    RELOC_LOW = 2,
    RELOC_HIGHLOW = 3,
    RELOC_HIGHADJ = 4,
    RELOC_MIPS_JMPADDR = 5, // only valid on MIPS
    RELOC_ARM_MOV32 = 5,    // only valid on ARM/Thumb
    RELOC_RISCV_HIGH20 = 5, // only valid on RISC-V
    RELOC_RESERVED = 6,
    RELOC_THUMB_MOV32 = 7,         // only valid on Thumb
    RELOC_RISCV_LOW32I = 7,        // only valid on RISC-V
    RELOC_RISCV_LOW12S = 8,        // only valid on RISC-V
    RELOC_LOONGARCH32_MARK_LA = 8, // only valid on LoongArch 32
    RELOC_LOONGARCH64_MARK_LA = 8, // only valid on LoongArch 64
    RELOC_MIPS_JMPADDR16 = 9,      // only valid on MIPS
    RELOC_IA64_IMM64 = 9,
    RELOC_DIR64 = 10
};

struct reloc_block 
{
    uint32_t PageRVA;
    uint32_t BlockSize;
};

struct image_load_config_code_integrity 
{
    uint16_t Flags;
    uint16_t Catalog;
    uint32_t CatalogOffset;
    uint32_t Reserved;
};

struct image_load_config_32 
{
    uint32_t Size;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t GlobalFlagsClear;
    uint32_t GlobalFlagsSet;
    uint32_t CriticalSectionDefaultTimeout;
    uint32_t DeCommitFreeBlockThreshold;
    uint32_t DeCommitTotalFreeThreshold;
    uint32_t LockPrefixTable;
    uint32_t MaximumAllocationSize;
    uint32_t VirtualMemoryThreshold;
    uint32_t ProcessHeapFlags;
    uint32_t ProcessAffinityMask;
    uint16_t CSDVersion;
    uint16_t DependentLoadFlags;
    uint32_t EditList;
    uint32_t SecurityCookie;
    uint32_t SEHandlerTable;
    uint32_t SEHandlerCount;
    uint32_t GuardCFCheckFunctionPointer;
    uint32_t GuardCFDispatchFunctionPointer;
    uint32_t GuardCFFunctionTable;
    uint32_t GuardCFFunctionCount;
    uint32_t GuardFlags;
    struct image_load_config_code_integrity* CodeIntegrity;
    uint32_t GuardAddressTakenIatEntryTable;
    uint32_t GuardAddressTakenIatEntryCount;
    uint32_t GuardLongJumpTargetTable;
    uint32_t GuardLongJumpTargetCount;
    uint32_t DynamicValueRelocTable;
    uint32_t CHPEMetadataPointer;
    uint32_t GuardRFFailureRoutine;
    uint32_t GuardRFFailureRoutineFunctionPointer;
    uint32_t DynamicValueRelocTableOffset;
    uint16_t DynamicValueRelocTableSection;
    uint16_t Reserved2;
    uint32_t GuardRFVerifyStackPointerFunctionPointer;
    uint32_t HotPatchTableOffset;
    uint32_t Reserved3;
    uint32_t EnclaveConfigurationPointer;
    uint32_t VolatileMetadataPointer;
};

struct image_load_config_64 
{
    uint32_t Size;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t GlobalFlagsClear;
    uint32_t GlobalFlagsSet;
    uint32_t CriticalSectionDefaultTimeout;
    uint64_t DeCommitFreeBlockThreshold;
    uint64_t DeCommitTotalFreeThreshold;
    uint64_t LockPrefixTable;
    uint64_t MaximumAllocationSize;
    uint64_t VirtualMemoryThreshold;
    uint64_t ProcessAffinityMask;
    uint32_t ProcessHeapFlags;
    uint16_t CSDVersion;
    uint16_t DependentLoadFlags;
    uint64_t EditList;
    uint64_t SecurityCookie;
    uint64_t SEHandlerTable;
    uint64_t SEHandlerCount;
    uint64_t GuardCFCheckFunctionPointer;
    uint64_t GuardCFDispatchFunctionPointer;
    uint64_t GuardCFFunctionTable;
    uint64_t GuardCFFunctionCount;
    uint32_t GuardFlags;
    struct image_load_config_code_integrity* CodeIntegrity;
    uint64_t GuardAddressTakenIatEntryTable;
    uint64_t GuardAddressTakenIatEntryCount;
    uint64_t GuardLongJumpTargetTable;
    uint64_t GuardLongJumpTargetCount;
    uint64_t DynamicValueRelocTable;
    uint64_t CHPEMetadataPointer;
    uint64_t GuardRFFailureRoutine;
    uint64_t GuardRFFailureRoutineFunctionPointer;
    uint32_t DynamicValueRelocTableOffset;
    uint16_t DynamicValueRelocTableSection;
    uint16_t Reserved2;
    uint64_t GuardRFVerifyStackPointerFunctionPointer;
    uint32_t HotPatchTableOffset;
    uint32_t Reserved3;
    uint64_t EnclaveConfigurationPointer;
    uint64_t VolatileMetadataPointer;
};
#endif