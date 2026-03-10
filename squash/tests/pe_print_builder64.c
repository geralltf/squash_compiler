#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FA 0x200
#define SA 0x1000
#define IB 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

#pragma pack(push,1)

// ---------------- DOS ----------------
typedef struct {
    uint16_t e_magic;      // MZ
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
    uint32_t e_lfanew;     // -> NT headers
} IMAGE_DOS_HEADER;

// ---------------- NT ----------------
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
    uint16_t Magic;                // 0x20B
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
    uint8_t  pad[12];
    uint32_t Characteristics;
} IMAGE_SECTION_HEADER;

typedef struct {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR;

#pragma pack(pop)

// ---------------- DOS STUB ----------------
static const uint8_t dos_stub[] = {
    0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
    0xB8,0x01,0x4C,0xCD,0x21,
    'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
    'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
    'i','n',' ','D','O','S',' ','m','o','d','e','.',
    '\r','\n','$'
};

// ------------------------------------------------------------
// Import builder (kernel32: GetStdHandle, WriteFile, ExitProcess)
// ------------------------------------------------------------
uint32_t build_imports(uint8_t* b, uint32_t rva) {
    memset(b, 0, 512);

    const char* dll = "kernel32.dll";
    const char* fn[] = {"GetStdHandle","WriteFile","ExitProcess"};

    uint32_t off = 0;

    IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)b;
    off += sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2;

    uint32_t iltRVA = rva + off;
    uint64_t* ilt = (uint64_t*)(b + off);
    off += 8 * 4;

    uint32_t iatRVA = rva + off;
    uint64_t* iat = (uint64_t*)(b + off);
    off += 8 * 4;

    uint32_t nameRVA = rva + off;
    strcpy((char*)(b + off), dll);
    off += strlen(dll) + 1;

    uint32_t hn[3];

    for(int i=0;i<3;i++){
        off = ALIGN(off,2);
        hn[i] = rva + off;
        *(uint16_t*)(b + off) = 0;
        strcpy((char*)(b + off + 2), fn[i]);
        off += 2 + strlen(fn[i]) + 1;
    }

    for(int i=0;i<3;i++){
        ilt[i] = hn[i];
        iat[i] = hn[i];
    }

    d[0].OriginalFirstThunk = iltRVA;
    d[0].Name = nameRVA;
    d[0].FirstThunk = iatRVA;

    return ALIGN(off,16);
}

// ------------------------------------------------------------

int main() {

    uint8_t img[8192];
    memset(img,0,sizeof(img));

    uint32_t textRVA = 0x1000;
    uint32_t idataRVA = 0x2000;

    uint32_t textRaw = 0x200;
    uint32_t idataRaw = 0x400;

    // ---------------- DOS HEADER ----------------
    IMAGE_DOS_HEADER dos = {0};
    dos.e_magic = 0x5A4D;
    dos.e_cblp = 0x90;
    dos.e_cp = 3;
    dos.e_cparhdr = 4;
    dos.e_maxalloc = 0xFFFF;
    dos.e_sp = 0xB8;
    dos.e_lfarlc = 0x40;
    dos.e_lfanew = 0x80;

    memcpy(img, &dos, sizeof(dos));
    memcpy(img + sizeof(dos), dos_stub, sizeof(dos_stub));

    // ---------------- NT ----------------
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(img + 0x80);
    nt->Signature = 0x4550;

    nt->FileHeader.Machine = 0x8664;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = 0x0022;

    IMAGE_OPTIONAL_HEADER64* o = &nt->OptionalHeader;

    o->Magic = 0x20B;
    o->AddressOfEntryPoint = textRVA;
    o->BaseOfCode = textRVA;
    o->ImageBase = IB;
    o->SectionAlignment = SA;
    o->FileAlignment = FA;

    o->MajorOperatingSystemVersion = 4;
    o->MajorSubsystemVersion = 4;

    o->SizeOfImage = 0x3000;
    o->SizeOfHeaders = 0x200;
    o->Subsystem = 3;

    o->SizeOfStackReserve = 0x100000;
    o->SizeOfStackCommit  = 0x1000;
    o->SizeOfHeapReserve  = 0x100000;
    o->SizeOfHeapCommit   = 0x1000;

	o->SizeOfCode = FA;// ALIGN(textSize, FA);

    o->NumberOfRvaAndSizes = 16;

    // ---------------- IMPORTS ----------------
    uint8_t idata[512];
    uint32_t idSize = build_imports(idata, idataRVA);

    o->DataDirectory[1].VirtualAddress = idataRVA;
    o->DataDirectory[1].Size = idSize;

    // ---------------- SECTIONS ----------------
    IMAGE_SECTION_HEADER* sh =
        (IMAGE_SECTION_HEADER*)((uint8_t*)nt + 24 + sizeof(IMAGE_OPTIONAL_HEADER64));

    memcpy(sh[0].Name,".text",5);
    sh[0].VirtualAddress = textRVA;
    sh[0].VirtualSize = 0x200;
    sh[0].SizeOfRawData = FA;
	sh[0].PointerToRawData = textRaw;
    sh[0].Characteristics = 0x60000020;

    memcpy(sh[1].Name,".idata",6);
    sh[1].VirtualAddress = idataRVA;
    sh[1].VirtualSize = idSize;
    sh[1].SizeOfRawData = FA;
    sh[1].PointerToRawData = idataRaw;
    sh[1].Characteristics = 0xC0000040;

    // ---------------- CODE ----------------
    uint8_t code[] = {
        0x48,0x83,0xEC,0x28,          // shadow

        0xB9,0xF5,0xFF,0xFF,0xFF,     // GetStdHandle(-11)
        0xFF,0x15,0,0,0,0,

        0x48,0x89,0xC1,               // handle → rcx
        0x48,0x8D,0x15,0,0,0,0,       // msg
        0x41,0xB8,22,0,0,0,           // len
        0x4C,0x8D,0x0D,0,0,0,0,       // written
        0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
        0xFF,0x15,0,0,0,0,            // WriteFile

        0x31,0xC9,
        0xFF,0x15,0,0,0,0             // ExitProcess
    };

    memcpy(img + textRaw, code, sizeof(code));

    // data
    uint32_t msgRVA = textRVA + 0x80;
    uint32_t wrRVA  = textRVA + 0xA0;

    strcpy((char*)(img + textRaw + 0x80),
           "Hello from PE64 builder!\r\n");

    // IAT
    uint32_t iat = idataRVA + sizeof(IMAGE_IMPORT_DESCRIPTOR)*2 + 32;

    uint64_t base = IB;

    #define PATCH(p,t) *(int32_t*)(img+textRaw+p) = \
        (int32_t)((base+t)-(base+textRVA+p+4))

    PATCH(7,iat);
    PATCH(16,msgRVA);
    PATCH(27,wrRVA);
    PATCH(39,iat+8);
    PATCH(45,iat+16);

    memcpy(img + idataRaw, idata, idSize);

    FILE* f = fopen("hello64.exe","wb");
    fwrite(img,1,0x600,f);
    fclose(f);

    printf("hello64.exe created\n");
}