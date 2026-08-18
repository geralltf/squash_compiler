#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define FA 0x200
#define SA 0x1000
#define IB 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

// ------------------------------------------------------------
// PE STRUCTS
// ------------------------------------------------------------

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
	
    //uint16_t e_magic;
    //uint8_t  pad[58];
    //uint32_t e_lfanew;
} IMAGE_DOS_HEADER;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} IMAGE_DATA_DIRECTORY;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PtrToSymbolTable;
    uint32_t NumSymbols;
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
    uint16_t MajorOSVersion,MinorOSVersion;
    uint16_t MajorImageVersion,MinorImageVersion;
    uint16_t MajorSubsystemVersion,MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve,SizeOfStackCommit;
    uint64_t SizeOfHeapReserve,SizeOfHeapCommit;
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
    uint8_t  Name[8];
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

// ------------------------------------------------------------
// LINKER-STYLE IMPORT BUILDER
// ------------------------------------------------------------

uint32_t build_imports(
    uint8_t *buf,
    uint32_t rvaBase,
    const char *dll,
    const char **funcs,
    int count)
{
    memset(buf,0,4096);

    uint32_t off = 0;

    IMAGE_IMPORT_DESCRIPTOR *desc =
        (IMAGE_IMPORT_DESCRIPTOR*)(buf+off);

    off += sizeof(IMAGE_IMPORT_DESCRIPTOR)*2;

    uint32_t iltRVA = rvaBase + off;
    uint64_t *ilt = (uint64_t*)(buf+off);
    off += (count+1)*8;

    uint32_t iatRVA = rvaBase + off;
    uint64_t *iat = (uint64_t*)(buf+off);
    off += (count+1)*8;

    uint32_t nameRVA = rvaBase + off;
    strcpy((char*)(buf+off), dll);
    off += strlen(dll)+1;

    uint32_t hintRVAs[32];

    for(int i=0;i<count;i++){
        off = ALIGN(off,2);
        hintRVAs[i] = rvaBase + off;

        *(uint16_t*)(buf+off) = 0;
        strcpy((char*)(buf+off+2), funcs[i]);
        off += 2 + strlen(funcs[i]) + 1;
    }

    for(int i=0;i<count;i++){
        ilt[i] = hintRVAs[i];
        iat[i] = hintRVAs[i];
    }

    desc->OriginalFirstThunk = iltRVA;
    desc->Name = nameRVA;
    desc->FirstThunk = iatRVA;

    return ALIGN(off,16);
}

// ------------------------------------------------------------
// MAIN BUILDER
// ------------------------------------------------------------

int main(){
    FILE *f = fopen("minimal64.exe","wb");

    uint8_t buf[8192];
    memset(buf,0,sizeof(buf));

    uint32_t textRaw=0x200;
    uint32_t idataRaw=0x400;

    uint32_t textRVA=0x1000;
    uint32_t idataRVA=0x2000;

    // ------------------------------------------------ DOS
    IMAGE_DOS_HEADER dos={0};
    dos.e_magic = 0x5A4D; // MZ_MAGIC; // "MZ" 
    dos.e_cblp = 0x0090;   // Bytes on last page
    dos.e_cp = 0x0003;   // Pages in file
    dos.e_crlc = 0x0000;   // No relocations
    dos.e_cparhdr = 0x0004;   // Header size = 4 paragraphs (64 bytes)
    dos.e_minalloc = 0x0000;   // No extra memory required
    dos.e_maxalloc = 0xFFFF;   // Max memory
    dos.e_ss = 0x0000;
    dos.e_sp = 0x00B8;   // Typical DOS stub stack
    dos.e_csum = 0x0000;
    dos.e_ip = 0x0000;
    dos.e_cs = 0x0000;
    dos.e_lfarlc = 0x0040;   // Relocation table offset
    dos.e_ovno = 0x0000;
    dos.e_res[0] = 0; //  { 0, 0, 0, 0 };
    dos.e_res[1] = 0;
    dos.e_res[2] = 0;
    dos.e_res[3] = 0;
    dos.e_oemid = 0x0000;
    dos.e_oeminfo = 0x0000;
    dos.e_res2[0] = 0;
    dos.e_lfanew = 0x80;  //RICH_OFFSET // Offset to PE header
	
    memcpy(buf,&dos,sizeof(dos));

   static const uint8_t dos_stub[] =
    {
        0x0E,                         // push cs
        0x1F,                         // pop ds
        0xBA, 0x0E, 0x00,             // mov dx, 000Eh (offset of string)
        0xB4, 0x09,                   // mov ah, 09h (print string)
        0xCD, 0x21,                   // int 21h
        0xB8, 0x01, 0x4C,             // mov ax, 4C01h (exit)
        0xCD, 0x21,                   // int 21h
        // message:
        'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
        'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
        'i','n',' ','D','O','S',' ','m','o','d','e','.','\r','\n','$'
    };

    // Write the DOS stub program
	memcpy(buf + sizeof(dos), &dos_stub, sizeof(dos_stub));

    // ------------------------------------------------ NT
    IMAGE_NT_HEADERS64 nt={0};
    nt.Signature=0x4550;

    nt.FileHeader.Machine=0x8664;
    nt.FileHeader.NumberOfSections=2;
    nt.FileHeader.SizeOfOptionalHeader=
        sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.FileHeader.Characteristics=0x22;

    IMAGE_OPTIONAL_HEADER64 *op=&nt.OptionalHeader;

    op->Magic=0x20B;
    op->AddressOfEntryPoint=textRVA;
    op->BaseOfCode=textRVA;
    op->ImageBase=IB;
    op->SectionAlignment=SA;
    op->FileAlignment=FA;
    op->SizeOfImage=0x3000;
    op->SizeOfHeaders=0x200;
    op->Subsystem=3;
    op->SizeOfStackReserve=0x100000;
    op->SizeOfStackCommit=0x1000;
    op->SizeOfHeapReserve=0x100000;
    op->SizeOfHeapCommit=0x1000;
    op->NumberOfRvaAndSizes=16;

    const char* funcs[]={"ExitProcess"};
    uint8_t idata[4096];
    uint32_t idataSize=
        build_imports(idata,idataRVA,
                      "kernel32.dll",
                      funcs,1);

    op->DataDirectory[1].VirtualAddress=idataRVA;
    op->DataDirectory[1].Size=idataSize;

    memcpy(buf+0x80,&nt,sizeof(nt));

    // ------------------------------------------------ SECTIONS
    IMAGE_SECTION_HEADER sh[2]={0};

    memcpy(sh[0].Name,".text",5);
    sh[0].VirtualAddress=textRVA;
    sh[0].VirtualSize=0x100;
    sh[0].SizeOfRawData=FA;
    sh[0].PointerToRawData=textRaw;
    sh[0].Characteristics=0x60000020;

    memcpy(sh[1].Name,".idata",6);
    sh[1].VirtualAddress=idataRVA;
    sh[1].VirtualSize=idataSize;
    sh[1].SizeOfRawData=FA;
    sh[1].PointerToRawData=idataRaw;
    sh[1].Characteristics=0xC0000040;

    memcpy(buf+0x80+sizeof(nt),sh,sizeof(sh));

    // ------------------------------------------------ CODE
    uint8_t code[]={
        0x48,0x83,0xEC,0x28,
        0x31,0xC9,
        0xFF,0x15,0,0,0,0
    };

    memcpy(buf+textRaw,code,sizeof(code));

    // RIP-relative call to IAT
    uint64_t callVA = IB+textRVA+10;
    uint64_t iatVA  = IB+idataRVA+sizeof(IMAGE_IMPORT_DESCRIPTOR)*2+8;
    int32_t disp = (int32_t)(iatVA-callVA);

    *(int32_t*)(buf+textRaw+8)=disp;

    memcpy(buf+idataRaw,idata,idataSize);

    fwrite(buf,1,0x600,f);
    fclose(f);

    printf("minimal64.exe created\n");
}
