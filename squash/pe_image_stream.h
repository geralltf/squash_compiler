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
#include <stdint.h>
#include <string.h>

#include "debugent.h"
#include "exportent.h"
#include "buffer.h"
#include "nt-headers.h"
#include "List.h"

//#include <sstream>
//#include <string>

//#if defined(_MSC_VER)
//typedef std::basic_string<wchar_t> UCharString;
//#else
//typedef std::u16string UCharString;
//#endif

//template <class T>
//static std::string to_string(T t, std::ios_base& (*f)(std::ios_base&)) {
//    std::ostringstream oss;
//    oss << f << t;
//    return oss.str();
//}
//
//std::string from_utf16(const std::u16string& utf16_str);
////std::string from_utf16(const UCharString& u);

//#ifdef _MSC_VER
//#define __typeof__(x) std::remove_reference<decltype(x)>::type
//#endif
//
//#define PE_ERR(x)               \
//  err = static_cast<pe_err>(x); \
//  err_loc.assign(__func__);     \
//  err_loc += ":" + to_string<std::uint32_t>(__LINE__, std::dec);
//
//#define READ_WORD(b, o, inst, member)                                          \
//  if (!readWord(b,                                                             \
//                o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
//                inst.member)) {                                                \
//    PE_ERR(PEERR_READ);                                                        \
//    return false;                                                              \
//  }
//
//#define READ_DWORD(b, o, inst, member)                                   \
//  if (!readDword(                                                        \
//          b,                                                             \
//          o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
//          inst.member)) {                                                \
//    PE_ERR(PEERR_READ);                                                  \
//    return false;                                                        \
//  }
//
//#define READ_QWORD(b, o, inst, member)                                   \
//  if (!readQword(                                                        \
//          b,                                                             \
//          o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
//          inst.member)) {                                                \
//    PE_ERR(PEERR_READ);                                                  \
//    return false;                                                        \
//  }
//
//#define READ_BYTE(b, o, inst, member)                                          \
//  if (!readByte(b,                                                             \
//                o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
//                inst.member)) {                                                \
//    PE_ERR(PEERR_READ);                                                        \
//    return false;                                                              \
//  }
//
#define TEST_MACHINE_CHARACTERISTICS(h, m, ch) \
  ((h->FileHeader->Machine == m) && (h->FileHeader->Characteristics & ch))

#define SYMBOL_NAME_OFFSET(sn) ((uint32_t)(sn->data >> 32))
#define SYMBOL_TYPE_HI(x) (x->type >> 8)


#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000
#define IMAGE_BASE 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

//struct section {
//    char* sectionName;
//    uint64_t sectionBase;
//    bounded_buffer* sectionData;
//    //uint32_t size; // sectionData->bufLen
//    struct image_section_header* sec;
//};
//
//struct importent {
//    VA addr;
//    char* symbolName;
//    char* moduleName;
//};
//
//struct reloc {
//    VA shiftedAddr;
//    enum reloc_type type;
//};
//
//union symbol_name {
//    uint8_t* shortName;// [NT_SHORT_NAME_LEN] ;
//    uint32_t zeroes;
//    uint64_t data;
//};
//
//struct aux_symbol_f1 {
//    uint32_t tagIndex;
//    uint32_t totalSize;
//    uint32_t pointerToLineNumber;
//    uint32_t pointerToNextFunction;
//};
//
//struct aux_symbol_f2 {
//    uint16_t lineNumber;
//    uint32_t pointerToNextFunction;
//};
//
//struct aux_symbol_f3 {
//    uint32_t tagIndex;
//    uint32_t characteristics;
//};
//
//struct aux_symbol_f4 {
//    uint8_t* filename;// [SYMTAB_RECORD_LEN] ;
//    char* strFilename;
//};
//
//struct aux_symbol_f5 {
//    uint32_t length;
//    uint16_t numberOfRelocations;
//    uint16_t numberOfLineNumbers;
//    uint32_t checkSum;
//    uint16_t number;
//    uint8_t selection;
//};
//
//struct symbol {
//    char* strName;
//    union symbol_name* name;
//    uint32_t value;
//    int16_t sectionNumber;
//    uint16_t type;
//    uint8_t storageClass;
//    uint8_t numberOfAuxSymbols;
//    //std::vector<aux_symbol_f1> aux_symbols_f1;
//    //std::vector<aux_symbol_f2> aux_symbols_f2;
//    //std::vector<aux_symbol_f3> aux_symbols_f3;
//    //std::vector<aux_symbol_f4> aux_symbols_f4;
//    //std::vector<aux_symbol_f5> aux_symbols_f5;
//
//    list_t* aux_symbols_f1;
//    list_t* aux_symbols_f2;
//    list_t* aux_symbols_f3;
//    list_t* aux_symbols_f4;
//    list_t* aux_symbols_f5;
//};
//
//struct parsed_pe_internal {
//    //std::vector<section> secs;
//    //std::vector<resource> rsrcs;
//    //std::vector<importent> imports;
//    //std::vector<reloc> relocs;
//    //std::vector<exportent> exports;
//    //std::vector<symbol> symbols;
//    //std::vector<debugent> debugdirs;
//    list_t* secs;
//    list_t* rsrcs;
//    list_t* imports;
//    list_t* relocs;
//    list_t* exports;
//    list_t* symbols;
//    list_t* debugdirs;
//};
//
//struct resource {
//    //resource()
//    //    : type(0), name(0), lang(0), codepage(0), RVA(0), size(0), buf(nullptr) {
//    //}
//
//    char* type_str;
//    char* name_str;
//    char* lang_str;
//    uint32_t type;
//    uint32_t name;
//    uint32_t lang;
//    uint32_t codepage;
//    uint32_t RVA;
//    uint32_t size;
//    bounded_buffer* buf;
//};
//
//#ifndef _PEPARSE_WINDOWS_CONFLICTS
//// http://msdn.microsoft.com/en-us/library/ms648009(v=vs.85).aspx
//enum resource_type {
//    RT_CURSOR = 1,
//    RT_BITMAP = 2,
//    RT_ICON = 3,
//    RT_MENU = 4,
//    RT_DIALOG = 5,
//    RT_STRING = 6,
//    RT_FONTDIR = 7,
//    RT_FONT = 8,
//    RT_ACCELERATOR = 9,
//    RT_RCDATA = 10,
//    RT_MESSAGETABLE = 11,
//    RT_GROUP_CURSOR = 12, // MAKEINTRESOURCE((ULONG_PTR)(RT_CURSOR) + 11)
//    RT_GROUP_ICON = 14,   // MAKEINTRESOURCE((ULONG_PTR)(RT_ICON) + 11)
//    RT_VERSION = 16,
//    RT_DLGINCLUDE = 17,
//    RT_PLUGPLAY = 19,
//    RT_VXD = 20,
//    RT_ANICURSOR = 21,
//    RT_ANIICON = 22,
//    RT_HTML = 23,
//    RT_MANIFEST = 24
//};
//#endif
//
//enum pe_err {
//    PEERR_NONE = 0,
//    PEERR_MEM = 1,
//    PEERR_HDR = 2,
//    PEERR_SECT = 3,
//    PEERR_RESC = 4,
//    PEERR_SECTVA = 5,
//    PEERR_READ = 6,
//    PEERR_OPEN = 7,
//    PEERR_STAT = 8,
//    PEERR_MAGIC = 9,
//    PEERR_BUFFER = 10,
//    PEERR_ADDRESS = 11,
//    PEERR_SIZE = 12,
//};
//
//struct parsed_pe_internal;
//
//typedef struct _pe_header {
//    struct dos_header* dos;
//    struct rich_header* rich;
//    struct nt_header_32* nt;
//} pe_header;
//
//typedef struct _parsed_pe {
//    bounded_buffer* fileBuffer;
//    struct parsed_pe_internal* internal;
//    pe_header* peHeader;
//} parsed_pe;

////////////////////////////////////////////////////

//#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

//typedef struct
//{
//    uint16_t e_magic;      // Magic number ("MZ")
//    uint16_t e_cblp;       // Bytes on last page of file
//    uint16_t e_cp;         // Pages in file
//    uint16_t e_crlc;       // Relocations
//    uint16_t e_cparhdr;    // Size of header in paragraphs
//    uint16_t e_minalloc;   // Minimum extra paragraphs needed
//    uint16_t e_maxalloc;   // Maximum extra paragraphs needed
//    uint16_t e_ss;         // Initial (relative) SS
//    uint16_t e_sp;         // Initial SP
//    uint16_t e_csum;       // Checksum
//    uint16_t e_ip;         // Initial IP
//    uint16_t e_cs;         // Initial (relative) CS
//    uint16_t e_lfarlc;     // File address of relocation table
//    uint16_t e_ovno;       // Overlay number
//    uint16_t e_res[4];     // Reserved words
//    uint16_t e_oemid;      // OEM identifier
//    uint16_t e_oeminfo;    // OEM information
//    uint16_t e_res2[10];   // Reserved words
//    int32_t  e_lfanew;     // File address of PE header
//} IMAGE_DOS_HEADER;
//
//typedef struct
//{
//    //uint32_t Signature;
//    uint16_t Machine;
//    uint16_t NumberOfSections;
//    uint32_t TimeDateStamp;
//    uint32_t PtrToSymbolTable;
//    uint32_t NumSymbols;
//    uint16_t SizeOfOptionalHeader;
//    uint16_t Characteristics;
//} IMAGE_FILE_HEADER;
//
//typedef struct
//{
//    uint32_t VirtualAddress;
//    uint32_t Size;
//} IMAGE_DATA_DIRECTORY;
//
//typedef struct
//{
//    uint16_t Magic;
//    uint8_t  MajorLinkerVersion;
//    uint8_t  MinorLinkerVersion;
//    uint32_t SizeOfCode;
//    uint32_t SizeOfInitializedData;
//    uint32_t SizeOfUninitializedData;
//    uint32_t AddressOfEntryPoint;
//    uint32_t BaseOfCode;
//
//    uint64_t ImageBase;
//    uint32_t SectionAlignment;
//    uint32_t FileAlignment;
//
//    uint16_t MajorOperatingSystemVersion;
//    uint16_t MinorOperatingSystemVersion;
//    uint16_t MajorImageVersion;
//    uint16_t MinorImageVersion;
//    uint16_t MajorSubsystemVersion;
//    uint16_t MinorSubsystemVersion;
//
//    uint32_t Win32VersionValue;
//    uint32_t SizeOfImage;
//    uint32_t SizeOfHeaders;
//    uint32_t CheckSum;
//
//    uint16_t Subsystem;
//    uint16_t DllCharacteristics;
//
//    uint64_t SizeOfStackReserve;
//    uint64_t SizeOfStackCommit;
//    uint64_t SizeOfHeapReserve;
//    uint64_t SizeOfHeapCommit;
//
//    uint32_t LoaderFlags;
//    uint32_t NumberOfRvaAndSizes;
//
//    IMAGE_DATA_DIRECTORY DataDirectory[NUM_DIR_ENTRIES];
//} IMAGE_OPTIONAL_HEADER64;
//
//typedef struct {
//    uint32_t Signature;
//    IMAGE_FILE_HEADER FileHeader;
//    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
//} IMAGE_NT_HEADERS64;
//
//typedef struct
//{
//    uint8_t  Name[8];
//    uint32_t VirtualSize;
//    uint32_t VirtualAddress;
//    uint32_t SizeOfRawData;
//    uint32_t PointerToRawData;
//    uint8_t  pad[16];
//    uint32_t Characteristics;
//} IMAGE_SECTION_HEADER;
//
//typedef struct
//{
//    const char* name;
//    uint8_t* data;
//    uint32_t size;
//    uint32_t characteristics;
//} section_def;
//
//typedef struct _IMAGE_IMPORT_DESCRIPTOR {
//    union {
//        uint32_t Characteristics;      // 0 for terminating null descriptor
//        uint32_t OriginalFirstThunk;   // RVA to Import Lookup Table (ILT)
//    };
//    uint32_t TimeDateStamp;            // 0 if not bound
//    uint32_t ForwarderChain;           // -1 if no forwarders
//    uint32_t Name;                     // RVA of DLL name string
//    uint32_t FirstThunk;               // RVA to Import Address Table (IAT)
//} IMAGE_IMPORT_DESCRIPTOR;
//
//typedef struct 
//{
//    uint32_t rva; 
//    uint32_t size; 
//} dir;
//
//typedef struct {
//    uint8_t* data;
//    size_t   size;
//    size_t   capacity;
//} binbuf;
/////////////////////////////////////////////////////////
//typedef struct {
//    uint16_t e_magic;
//    uint8_t  pad[58];
//    uint32_t e_lfanew;
//} IMAGE_DOS_HEADER;
//
//typedef struct {
//    uint32_t VirtualAddress;
//    uint32_t Size;
//} IMAGE_DATA_DIRECTORY;
//
//typedef struct {
//    uint16_t Machine;
//    uint16_t NumberOfSections;
//    uint32_t TimeDateStamp;
//    uint32_t PtrToSymbolTable;
//    uint32_t NumSymbols;
//    uint16_t SizeOfOptionalHeader;
//    uint16_t Characteristics;
//} IMAGE_FILE_HEADER;
//
//typedef struct {
//    uint16_t Magic;
//    uint8_t  MajorLinkerVersion;
//    uint8_t  MinorLinkerVersion;
//    uint32_t SizeOfCode;
//    uint32_t SizeOfInitializedData;
//    uint32_t SizeOfUninitializedData;
//    uint32_t AddressOfEntryPoint;
//    uint32_t BaseOfCode;
//    uint64_t ImageBase;
//    uint32_t SectionAlignment;
//    uint32_t FileAlignment;
//    uint16_t MajorOSVersion;
//    uint16_t MinorOSVersion;
//    uint16_t MajorImageVersion;
//    uint16_t MinorImageVersion;
//    uint16_t MajorSubsystemVersion;
//    uint16_t MinorSubsystemVersion;
//    uint32_t Win32VersionValue;
//    uint32_t SizeOfImage;
//    uint32_t SizeOfHeaders;
//    uint32_t CheckSum;
//    uint16_t Subsystem;
//    uint16_t DllCharacteristics;
//    uint64_t SizeOfStackReserve;
//    uint64_t SizeOfStackCommit;
//    uint64_t SizeOfHeapReserve;
//    uint64_t SizeOfHeapCommit;
//    uint32_t LoaderFlags;
//    uint32_t NumberOfRvaAndSizes;
//    IMAGE_DATA_DIRECTORY DataDirectory[16];
//} IMAGE_OPTIONAL_HEADER64;
//
//typedef struct {
//    uint32_t Signature;
//    IMAGE_FILE_HEADER FileHeader;
//    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
//} IMAGE_NT_HEADERS64;
//
//typedef struct {
//    uint8_t  Name[8];
//    uint32_t VirtualSize;
//    uint32_t VirtualAddress;
//    uint32_t SizeOfRawData;
//    uint32_t PointerToRawData;
//    uint8_t  pad[12];
//    uint32_t Characteristics;
//} IMAGE_SECTION_HEADER;
//
//typedef struct {
//    uint32_t OriginalFirstThunk;
//    uint32_t TimeDateStamp;
//    uint32_t ForwarderChain;
//    uint32_t Name;
//    uint32_t FirstThunk;
//} IMAGE_IMPORT_DESCRIPTOR;
//////////////////////////////////////////////////
// 
// 

#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000
#define IMAGE_BASE 0x140000000ULL

// ------------------------------------------------
// STRUCTS
// ------------------------------------------------

typedef struct {
    uint16_t e_magic;
    uint8_t  pad[58];
    uint32_t e_lfanew;
} DOS;

typedef struct { uint32_t VA, Size; } DIR;

typedef struct {
    uint16_t Machine, Sections;
    uint32_t Time;
    uint32_t SymPtr, NumSyms;
    uint16_t OptSize, Chars;
} FILEHDR;

typedef struct {
    uint16_t Magic;
    uint8_t  MajL, MinL;
    uint32_t CodeSz, InitSz, UninitSz;
    uint32_t Entry, BaseCode;
    uint64_t ImageBase;
    uint32_t SecAlign, FileAlign;
    uint16_t OSv1, OSv2, ImgV1, ImgV2, SubV1, SubV2;
    uint32_t Win32Ver;
    uint32_t ImgSz, HdrSz, Chk;
    uint16_t Subsys, DllChars;
    uint64_t StkRes, StkCom, HeapRes, HeapCom;
    uint32_t Loader, Dirs;
    DIR Dir[16];
} OPT64;

typedef struct { uint32_t Sig; FILEHDR F; OPT64 O; } NT64;

typedef struct {
    uint8_t Name[8];
    uint32_t VSize, VA, RawSize, RawPtr;
    uint8_t pad[12];
    uint32_t Chars;
} SECT;

typedef struct {
    uint32_t ILT;
    uint32_t Time;
    uint32_t Fwd;
    uint32_t Name;
    uint32_t IAT;
} IMPDESC;

///////////////////////////////////////////////////

// Resolve a Rich header product id / build number pair to a known
// product name
//typedef std::pair<std::uint16_t, std::uint16_t> ProductKey;
const char* GetRichObjectType(uint16_t prodId);
const char* GetRichProductName(uint16_t buildNum);

// get parser error status as integer
uint32_t GetPEErr();

// get parser error status as string
char* GetPEErrString();

// get parser error location as string
char* GetPEErrLoc();

//// get machine as human readable string
//const char* GetMachineAsString(parsed_pe* pe);
//
//// get subsystem as human readable string
//const char* GetSubsystemAsString(parsed_pe* pe);

void build_pe(const char* outPath);

bool WritePEProgramSQImage(const char* fileName, unsigned char* sq_program_image, int sq_program_image_length);

#endif