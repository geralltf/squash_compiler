// pe64_complete_builder.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FILE_ALIGN 0x200
#define SECT_ALIGN 0x1000
#define IMAGE_BASE 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

#pragma pack(push,1)

/* ---------------- DOS HEADER ---------------- */

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
} IMAGE_DOS_HEADER;

/* ---------------- NT HEADERS ---------------- */

typedef struct { uint32_t VirtualAddress, Size; } IMAGE_DATA_DIRECTORY;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} IMAGE_FILE_HEADER;

typedef struct {

    uint16_t Magic;
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

    IMAGE_DATA_DIRECTORY DataDirectory[16];

} IMAGE_OPTIONAL_HEADER64;

typedef struct {
    uint32_t Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64;

typedef struct {

    char Name[8];

    uint32_t VirtualSize;
    uint32_t VirtualAddress;

    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;

    uint8_t pad[12];

    uint32_t Characteristics;

} IMAGE_SECTION_HEADER;

/* ---------------- IMPORT DESCRIPTOR ---------------- */

typedef struct {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR;

#pragma pack(pop)

/* ---------------- DOS STUB ---------------- */

static const uint8_t dos_stub[] = {
    0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
    0xB8,0x01,0x4C,0xCD,0x21,
    'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
    'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
    'i','n',' ','D','O','S',' ','m','o','d','e','.',
    '\r','\n','$'
};

/* ----------------------------------------------------------- */

int main() {

    uint8_t image[65536];
    memset(image,0,sizeof(image));

    /* -------- SECTION RVAs -------- */

    uint32_t textRVA  = 0x1000;
    uint32_t rdataRVA = 0x2000;
    uint32_t dataRVA  = 0x3000;
    uint32_t idataRVA = 0x4000;
    uint32_t pdataRVA = 0x5000;
    uint32_t tlsRVA   = 0x6000;
    uint32_t rsrcRVA  = 0x7000;

    /* -------- FILE OFFSETS -------- */

    uint32_t textRaw  = 0x200;
    uint32_t rdataRaw = 0x400;
    uint32_t dataRaw  = 0x600;
    uint32_t idataRaw = 0x800;
    uint32_t pdataRaw = 0xA00;
    uint32_t tlsRaw   = 0xC00;
    uint32_t rsrcRaw  = 0xE00;

    /* -------- DOS HEADER -------- */

    IMAGE_DOS_HEADER dos = {0};

    dos.e_magic  = 0x5A4D;
    dos.e_cblp   = 0x0090;
    dos.e_cp     = 3;
    dos.e_cparhdr= 4;
    dos.e_maxalloc = 0xFFFF;
    dos.e_sp     = 0x00B8;
    dos.e_lfarlc = 0x0040;
    dos.e_lfanew = 0x80;

    memcpy(image,&dos,sizeof(dos));
    memcpy(image+sizeof(dos),dos_stub,sizeof(dos_stub));

    /* -------- NT HEADERS -------- */

    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(image+0x80);

    nt->Signature = 0x4550;

    nt->FileHeader.Machine = 0x8664;
    nt->FileHeader.NumberOfSections = 7;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = 0x0022;

    IMAGE_OPTIONAL_HEADER64* opt = &nt->OptionalHeader;

    opt->Magic = 0x20B;
    opt->AddressOfEntryPoint = textRVA;
    opt->BaseOfCode = textRVA;
    opt->ImageBase = IMAGE_BASE;

    opt->SectionAlignment = SECT_ALIGN;
    opt->FileAlignment = FILE_ALIGN;

    opt->MajorOperatingSystemVersion = 4;
    opt->MajorSubsystemVersion = 4;

    opt->Subsystem = 3;

    opt->SizeOfImage = 0x8000;
    opt->SizeOfHeaders = 0x200;

    opt->SizeOfStackReserve = 0x100000;
    opt->SizeOfStackCommit  = 0x1000;
    opt->SizeOfHeapReserve  = 0x100000;
    opt->SizeOfHeapCommit   = 0x1000;

    opt->NumberOfRvaAndSizes = 16;

    /* -------- DIRECTORIES -------- */

    opt->DataDirectory[1].VirtualAddress = idataRVA;
    opt->DataDirectory[1].Size = 0x200;

    opt->DataDirectory[3].VirtualAddress = rsrcRVA;
    opt->DataDirectory[3].Size = 0x100;

    opt->DataDirectory[4].VirtualAddress = pdataRVA;
    opt->DataDirectory[4].Size = 0x100;

    opt->DataDirectory[9].VirtualAddress = tlsRVA;
    opt->DataDirectory[9].Size = 0x40;

    /* -------- SECTION HEADERS -------- */

    IMAGE_SECTION_HEADER* sh =
        (IMAGE_SECTION_HEADER*)((uint8_t*)nt +
        24 + sizeof(IMAGE_OPTIONAL_HEADER64));

#define SETSEC(i,name,va,raw,ch) \
memcpy(sh[i].Name,name,strlen(name));\
sh[i].VirtualAddress=va;\
sh[i].VirtualSize=0x200;\
sh[i].SizeOfRawData=FILE_ALIGN;\
sh[i].PointerToRawData=raw;\
sh[i].Characteristics=ch;

    SETSEC(0,".text", textRVA,textRaw,0x60000020)
    SETSEC(1,".rdata",rdataRVA,rdataRaw,0x40000040)
    SETSEC(2,".data", dataRVA,dataRaw,0xC0000040)
    SETSEC(3,".idata",idataRVA,idataRaw,0xC0000040)
    SETSEC(4,".pdata",pdataRVA,pdataRaw,0x40000040)
    SETSEC(5,".tls",  tlsRVA,tlsRaw,0xC0000040)
    SETSEC(6,".rsrc", rsrcRVA,rsrcRaw,0x40000040)

    /* -------- CODE (prints message) -------- */

    uint8_t code[] = {
        0x48,0x83,0xEC,0x28,

        0xB9,0xF5,0xFF,0xFF,0xFF,
        0xFF,0x15,0,0,0,0,

        0x48,0x89,0xC1,
        0x48,0x8D,0x15,0,0,0,0,
        0x41,0xB8,22,0,0,0,
        0x4C,0x8D,0x0D,0,0,0,0,
        0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
        0xFF,0x15,0,0,0,0,

        0x31,0xC9,
        0xFF,0x15,0,0,0,0
    };

    memcpy(image+textRaw,code,sizeof(code));

    strcpy((char*)(image+textRaw+0x80),
           "Hello from full PE builder!\r\n");

    /* -------- IMPORTS -------- */

    uint8_t* id = image + idataRaw;

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)id;

    uint32_t iltRVA = idataRVA + 0x40;
    uint32_t iatRVA = idataRVA + 0x60;
    uint32_t nameRVA= idataRVA + 0x20;

    imp[0].OriginalFirstThunk = iltRVA;
    imp[0].FirstThunk = iatRVA;
    imp[0].Name = nameRVA;

    strcpy((char*)(id+0x20),"kernel32.dll");

    *(uint64_t*)(id+0x40) = idataRVA+0x80;
    *(uint64_t*)(id+0x60) = idataRVA+0x80;

    *(uint16_t*)(id+0x80)=0;
    strcpy((char*)(id+0x82),"ExitProcess");

    /* -------- WRITE FILE -------- */

    FILE* f = fopen("hello_full64.exe","wb");
    fwrite(image,1,rsrcRVA + rsrcRaw,f);
    fclose(f);

    printf("hello_full64.exe created\n");
}