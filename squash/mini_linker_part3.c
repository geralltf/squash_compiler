#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SA 0x1000
#define FA 0x200
#define IB 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

// ------------------------------------------------------------
// COFF STRUCTURES
// ------------------------------------------------------------

typedef struct{
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PtrToSym;
    uint32_t NumSyms;
    uint16_t OptHdrSize;
    uint16_t Chars;
} COFFHDR;

typedef struct{
    char Name[8];
    uint32_t VSize;
    uint32_t VAddr;
    uint32_t Size;
    uint32_t RawPtr;
    uint32_t RelocPtr;
    uint32_t LinePtr;
    uint16_t NumReloc;
    uint16_t NumLines;
    uint32_t Chars;
} COFFSECT;

typedef struct{
    uint32_t VA;
    uint32_t SymIndex;
    uint16_t Type;
} COFFRELOC;

typedef struct{
    union{
        char Name[8];
        struct{uint32_t Zeros,Offset;} Long;
    };
    uint32_t Value;
    int16_t Sect;
    uint16_t Type;
    uint8_t Storage;
    uint8_t Aux;
} COFFSYM;

// ------------------------------------------------------------

typedef struct{
    uint8_t* text;
    uint32_t textSize;
} Obj;

// ------------------------------------------------------------
// GLOBALS
// ------------------------------------------------------------

uint8_t finalText[65536];
uint32_t finalTextSize=0;

typedef struct{
    char name[64];
    uint32_t rva;
} GSym;

GSym gsyms[1024];
int gcount=0;

void add_sym(const char*n,uint32_t rva){
    strcpy(gsyms[gcount].name,n);
    gsyms[gcount].rva=rva;
    gcount++;
}

uint32_t find_sym(const char*n){
    for(int i=0;i<gcount;i++)
        if(strcmp(n,gsyms[i].name)==0)
            return gsyms[i].rva;
    return 0;
}

// ------------------------------------------------------------
// PARSE OBJECT
// ------------------------------------------------------------

void parse_obj(const char*path){

    FILE*f=fopen(path,"rb");
    if(!f){printf("no %s\n",path);exit(1);}

    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    rewind(f);

    uint8_t*buf=malloc(sz);
    fread(buf,1,sz,f);
    fclose(f);

    COFFHDR*h=(COFFHDR*)buf;
    COFFSECT*s=(COFFSECT*)(buf+sizeof(COFFHDR));

    COFFSYM*syms=(COFFSYM*)(buf+h->PtrToSym);
    char*strtab=(char*)(buf+h->PtrToSym+h->NumSyms*18);

    // find .text
    COFFSECT*textSec=NULL;
    for(int i=0;i<h->NumberOfSections;i++){
        if(strncmp(s[i].Name,".text",5)==0)
            textSec=&s[i];
    }

    if(!textSec){ free(buf); return; }

    uint8_t*code=buf+textSec->RawPtr;
    uint32_t baseRVA=0x1000+finalTextSize;

    memcpy(finalText+finalTextSize,
           code,
           textSec->Size);

    // symbols
    for(uint32_t i=0;i<h->NumSyms;i++){
        COFFSYM*sym=&syms[i];

        if(sym->Sect>0){
            const char*name;

            if(sym->Name[0]==0)
                name=strtab+sym->Long.Offset;
            else
                name=sym->Name;

            add_sym(name,
                baseRVA+sym->Value);
        }

        i+=sym->Aux;
    }

    // relocations
    COFFRELOC*r=(COFFRELOC*)(buf+textSec->RelocPtr);

    for(int i=0;i<textSec->NumReloc;i++){
        COFFRELOC*rr=&r[i];

        COFFSYM*rs=&syms[rr->SymIndex];

        const char*name=
            (rs->Name[0]==0)?
            strtab+rs->Long.Offset:
            rs->Name;

        uint32_t target=find_sym(name);

        uint32_t patchRVA=
            baseRVA+rr->VA;

        uint32_t instrVA=
            IB+patchRVA+4;

        int32_t disp=
            (IB+target)-instrVA;

        *(int32_t*)(finalText+
            finalTextSize+rr->VA)=disp;
    }

    finalTextSize+=textSec->Size;

    free(buf);
}

// ------------------------------------------------------------
// PE WRITER
// ------------------------------------------------------------

void write_pe(){
    uint8_t img[65536]={0};

    *(uint16_t*)img=0x5A4D;
    *(uint32_t*)(img+0x3C)=0x80;

    uint8_t*nt=img+0x80;

    *(uint32_t*)nt=0x4550;

    *(uint16_t*)(nt+4)=0x8664;
    *(uint16_t*)(nt+6)=1;
    *(uint16_t*)(nt+20)=0xF0;
    *(uint16_t*)(nt+22)=0x22;

    uint8_t*op=nt+24;

    *(uint16_t*)op=0x20B;
    *(uint32_t*)(op+16)=0x1000;
    *(uint32_t*)(op+20)=0x1000;
    *(uint64_t*)(op+24)=IB;

    *(uint32_t*)(op+32)=SA;
    *(uint32_t*)(op+36)=FA;

    *(uint32_t*)(op+56)=0x2000;
    *(uint32_t*)(op+60)=0x200;

    *(uint16_t*)(op+68)=3;
    *(uint64_t*)(op+72)=0x100000;
    *(uint64_t*)(op+80)=0x1000;
    *(uint64_t*)(op+88)=0x100000;
    *(uint64_t*)(op+96)=0x1000;
    *(uint32_t*)(op+108)=16;

    uint8_t*sh=nt+24+0xF0;

    memcpy(sh,".text",5);
    *(uint32_t*)(sh+8)=finalTextSize;
    *(uint32_t*)(sh+12)=0x1000;
    *(uint32_t*)(sh+16)=ALIGN(finalTextSize,FA);
    *(uint32_t*)(sh+20)=0x200;
    *(uint32_t*)(sh+36)=0x60000020;

    memcpy(img+0x200,
           finalText,
           finalTextSize);

    FILE*f=fopen("linked.exe","wb");
    fwrite(img,1,0x200+
           ALIGN(finalTextSize,FA),f);
    fclose(f);
}

// ------------------------------------------------------------

int main(int c,char**v){

    if(c<2){
        printf("usage: linker obj...\n");
        return 0;
    }

    for(int i=1;i<c;i++)
        parse_obj(v[i]);

    write_pe();

    printf("linked.exe created\n");
}
