// pe64_debug_builder.c

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FILE_ALIGN 0x200
#define SECT_ALIGN 0x1000
#define IMAGE_BASE 0x140000000ULL

#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

#pragma pack(push,1)

/* DOS HEADER */

typedef struct {
uint16_t e_magic;
uint8_t pad[58];
uint32_t e_lfanew;
} DOS_HEADER;

/* DATA DIRECTORY */

typedef struct {
uint32_t VirtualAddress;
uint32_t Size;
} DATA_DIR;

/* FILE HEADER */

typedef struct {
uint16_t Machine;
uint16_t Sections;
uint32_t TimeDateStamp;
uint32_t PtrSymbols;
uint32_t NumSymbols;
uint16_t OptSize;
uint16_t Characteristics;
} FILE_HEADER;

/* OPTIONAL HEADER */

typedef struct {

uint16_t Magic;
uint8_t MajorLinker;
uint8_t MinorLinker;

uint32_t SizeCode;
uint32_t SizeInitData;
uint32_t SizeUninit;

uint32_t EntryPoint;
uint32_t BaseCode;

uint64_t ImageBase;

uint32_t SectionAlign;
uint32_t FileAlign;

uint16_t MajorOS;
uint16_t MinorOS;

uint16_t MajorImage;
uint16_t MinorImage;

uint16_t MajorSubsystem;
uint16_t MinorSubsystem;

uint32_t Win32Ver;

uint32_t SizeImage;
uint32_t SizeHeaders;

uint32_t Checksum;

uint16_t Subsystem;
uint16_t DllChars;

uint64_t StackReserve;
uint64_t StackCommit;

uint64_t HeapReserve;
uint64_t HeapCommit;

uint32_t LoaderFlags;
uint32_t DirCount;

DATA_DIR Dir[16];

} OPTIONAL_HEADER;

/* NT HEADERS */

typedef struct {
uint32_t Signature;
FILE_HEADER File;
OPTIONAL_HEADER Opt;
} NT_HEADERS;

/* SECTION HEADER */

typedef struct {
char Name[8];
uint32_t VirtualSize;
uint32_t VirtualAddress;
uint32_t RawSize;
uint32_t RawPtr;
uint8_t pad[12];
uint32_t Characteristics;
} SECTION_HEADER;

/* IMPORT DESCRIPTOR */

typedef struct {
uint32_t OriginalFirstThunk;
uint32_t TimeDateStamp;
uint32_t ForwarderChain;
uint32_t Name;
uint32_t FirstThunk;
} IMPORT_DESC;

#pragma pack(pop)

/* DOS STUB */

static const uint8_t dos_stub[]={
0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
0xB8,0x01,0x4C,0xCD,0x21,
'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
'i','n',' ','D','O','S',' ','m','o','d','e','.',
'\r','\n','$'
};

int main(){

uint8_t img[8192];
memset(img,0,sizeof(img));

uint32_t textRVA=0x1000;
uint32_t idataRVA=0x2000;

uint32_t textRaw=0x200;
uint32_t idataRaw=0x400;

/* DOS */

DOS_HEADER*dos=(DOS_HEADER*)img;
dos->e_magic=0x5A4D;
dos->e_lfanew=0x80;

memcpy(img+sizeof(DOS_HEADER),dos_stub,sizeof(dos_stub));

/* NT */

NT_HEADERS*nt=(NT_HEADERS*)(img+0x80);

nt->Signature=0x4550;

nt->File.Machine=0x8664;
nt->File.Sections=2;
nt->File.OptSize=sizeof(OPTIONAL_HEADER);
nt->File.Characteristics=0x22;

OPTIONAL_HEADER*o=&nt->Opt;

o->Magic=0x20B;

o->EntryPoint=textRVA;
o->BaseCode=textRVA;

o->ImageBase=IMAGE_BASE;

o->SectionAlign=SECT_ALIGN;
o->FileAlign=FILE_ALIGN;

o->MajorOS=4;
o->MajorSubsystem=4;

o->Subsystem=3;

o->StackReserve=0x100000;
o->StackCommit=0x1000;

o->HeapReserve=0x100000;
o->HeapCommit=0x1000;

o->DirCount=16;

o->SizeImage=0x3000;
o->SizeHeaders=0x200;

o->Dir[1].VirtualAddress=idataRVA;
o->Dir[1].Size=0x200;

/* SECTIONS */

SECTION_HEADER*sh=
(SECTION_HEADER*)((uint8_t*)nt+
24+sizeof(OPTIONAL_HEADER));

memcpy(sh[0].Name,".text",5);
sh[0].VirtualAddress=textRVA;
sh[0].VirtualSize=0x200;
sh[0].RawSize=FILE_ALIGN;
sh[0].RawPtr=textRaw;
sh[0].Characteristics=0x60000020;

memcpy(sh[1].Name,".idata",6);
sh[1].VirtualAddress=idataRVA;
sh[1].VirtualSize=0x200;
sh[1].RawSize=FILE_ALIGN;
sh[1].RawPtr=idataRaw;
sh[1].Characteristics=0xC0000040;

/* MACHINE CODE */

uint8_t code[]={
0x48,0x83,0xEC,0x28,

0xB9,0xF5,0xFF,0xFF,0xFF,
0xFF,0x15,0,0,0,0,

0x48,0x89,0xC1,
0x48,0x8D,0x15,0,0,0,0,
0x41,0xB8,26,0,0,0,
0x4C,0x8D,0x0D,0,0,0,0,
0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
0xFF,0x15,0,0,0,0,

0x31,0xC9,
0xFF,0x15,0,0,0,0
};

memcpy(img+textRaw,code,sizeof(code));

strcpy((char*)(img+textRaw+0x80),
"Hello from PE64 builder!\r\n");

/* IMPORTS */

uint8_t*id=img+idataRaw;

IMPORT_DESC*imp=(IMPORT_DESC*)id;

uint32_t ilt=idataRVA+0x40;
uint32_t iat=idataRVA+0x60;
uint32_t name=idataRVA+0x20;

imp->OriginalFirstThunk=ilt;
imp->FirstThunk=iat;
imp->Name=name;

strcpy((char*)(id+0x20),"kernel32.dll");

*(uint64_t*)(id+0x40)=idataRVA+0x80;
*(uint64_t*)(id+0x60)=idataRVA+0x80;

*(uint16_t*)(id+0x80)=0;
strcpy((char*)(id+0x82),"GetStdHandle");

*(uint16_t*)(id+0xA0)=0;
strcpy((char*)(id+0xA2),"WriteFile");

*(uint16_t*)(id+0xC0)=0;
strcpy((char*)(id+0xC2),"ExitProcess");

/* WRITE FILE */

FILE*f=fopen("hello_debug64.exe","wb");
fwrite(img,1,0x600,f);
fclose(f);

printf("hello_debug64.exe created\n");
}