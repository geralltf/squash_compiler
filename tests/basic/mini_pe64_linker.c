// mini_pe64_linker.c

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FILE_ALIGN 0x200
#define SECT_ALIGN 0x1000
#define IMAGE_BASE 0x140000000ULL

#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

#pragma pack(push,1)

typedef struct{
uint16_t e_magic;
uint8_t pad[58];
uint32_t e_lfanew;
} DOS;

typedef struct{
uint32_t VirtualAddress;
uint32_t Size;
} DIR;

typedef struct{
uint16_t Machine;
uint16_t Sections;
uint32_t TimeDateStamp;
uint32_t PtrSym;
uint32_t NumSym;
uint16_t OptSize;
uint16_t Chars;
} FH;

typedef struct{

uint16_t Magic;
uint8_t LinkMaj,LinkMin;

uint32_t SizeCode;
uint32_t SizeInit;
uint32_t SizeUninit;

uint32_t Entry;
uint32_t BaseCode;

uint64_t ImageBase;

uint32_t SecAlign;
uint32_t FileAlign;

uint16_t OS1,OS2;
uint16_t IV1,IV2;
uint16_t SV1,SV2;

uint32_t Win32Ver;

uint32_t SizeImage;
uint32_t SizeHeaders;

uint32_t CheckSum;

uint16_t Subsystem;
uint16_t DllChars;

uint64_t StackReserve;
uint64_t StackCommit;

uint64_t HeapReserve;
uint64_t HeapCommit;

uint32_t LoaderFlags;
uint32_t DirCount;

DIR Dir[16];

} OPT64;

typedef struct{
uint32_t Sig;
FH File;
OPT64 Opt;
} NT;

typedef struct{

char Name[8];
uint32_t VirtualSize;
uint32_t VirtualAddress;
uint32_t RawSize;
uint32_t RawPtr;
uint8_t pad[12];
uint32_t Chars;

} SH;

typedef struct{

uint32_t OriginalFirstThunk;
uint32_t Time;
uint32_t Forward;
uint32_t Name;
uint32_t FirstThunk;

} IMPORT;

#pragma pack(pop)

static const uint8_t dos_stub[]={
0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
0xB8,0x01,0x4C,0xCD,0x21,
'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
'i','n',' ','D','O','S',' ','m','o','d','e','.',
'\r','\n','$'
};

int main(){

uint8_t img[65536];
memset(img,0,sizeof(img));

uint32_t textRVA=0x1000;
uint32_t idataRVA=0x2000;

uint32_t textRaw=0x200;
uint32_t idataRaw=0x400;

/* DOS */

DOS*dos=(DOS*)img;
dos->e_magic=0x5A4D;
dos->e_lfanew=0x80;

memcpy(img+sizeof(DOS),dos_stub,sizeof(dos_stub));

/* NT */

NT*nt=(NT*)(img+0x80);

nt->Sig=0x4550;

nt->File.Machine=0x8664;
nt->File.Sections=2;
nt->File.OptSize=sizeof(OPT64);
nt->File.Chars=0x22;

OPT64*o=&nt->Opt;

o->Magic=0x20B;

o->Entry=textRVA;
o->BaseCode=textRVA;

o->ImageBase=IMAGE_BASE;

o->SecAlign=SECT_ALIGN;
o->FileAlign=FILE_ALIGN;

o->OS1=4;
o->SV1=4;

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

SH*sh=(SH*)((uint8_t*)nt+24+sizeof(OPT64));

memcpy(sh[0].Name,".text",5);
sh[0].VirtualAddress=textRVA;
sh[0].VirtualSize=0x200;
sh[0].RawSize=FILE_ALIGN;
sh[0].RawPtr=textRaw;
sh[0].Chars=0x60000020;

memcpy(sh[1].Name,".idata",6);
sh[1].VirtualAddress=idataRVA;
sh[1].VirtualSize=0x200;
sh[1].RawSize=FILE_ALIGN;
sh[1].RawPtr=idataRaw;
sh[1].Chars=0xC0000040;

/* MACHINE CODE */

uint8_t code[]={

0x48,0x83,0xEC,0x28,

0xB9,0xF5,0xFF,0xFF,0xFF,
0xFF,0x15,0,0,0,0,

0x48,0x89,0xC1,
0x48,0x8D,0x15,0,0,0,0,
0x41,0xB8,25,0,0,0,
0x4C,0x8D,0x0D,0,0,0,0,
0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
0xFF,0x15,0,0,0,0,

0x31,0xC9,
0xFF,0x15,0,0,0,0

};

memcpy(img+textRaw,code,sizeof(code));

strcpy((char*)(img+textRaw+0x80),
"Hello from mini linker!\r\n");

/* IMPORTS */

uint8_t*id=img+idataRaw;

IMPORT*imp=(IMPORT*)id;

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

/* WRITE */

FILE*f=fopen("hello_linked.exe","wb");
fwrite(img,1,0x600,f);
fclose(f);

printf("hello_linked.exe created\n");
}