#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FA 0x200
#define SA 0x1000
#define IB 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

#pragma pack(push,1)

typedef struct{
    uint16_t e_magic;
    uint8_t pad[58];
    uint32_t e_lfanew;
} DOS;

typedef struct{uint32_t va,size;} DIR;

typedef struct{
    uint16_t m,sec;
    uint32_t t,ps,ns;
    uint16_t opt,ch;
} FH;

typedef struct{
    uint16_t magic;
    uint8_t lmj,lmn;
    uint32_t cs,ds,bs;
    uint32_t entry,base;
    uint64_t ib;
    uint32_t sa,fa;
    uint16_t osv1,osv2,iv1,iv2,sv1,sv2;
    uint32_t win32v;
    uint32_t imgsz,hdrsz,chk;
    uint16_t subsys,dllc;
    uint64_t stkres,stkcom,heapres,heapcom;
    uint32_t loader,dirs;
    DIR dir[16];
} OPT;

typedef struct{uint32_t sig;FH f;OPT o;} NT;

typedef struct{
    char name[8];
    uint32_t vs,va,rs,rp;
    uint8_t pad[12];
    uint32_t ch;
} SH;

typedef struct{
    uint32_t oft,t,f,name,ft;
} IMP;

#pragma pack(pop)

int main(){

    uint8_t img[4096]={0};

    // ---------------- DOS
    DOS*d=(DOS*)img;
    d->e_magic=0x5A4D;
    d->e_lfanew=0x80;

    // ---------------- NT
    NT*nt=(NT*)(img+0x80);
    nt->sig=0x4550;

    nt->f.m=0x8664;
    nt->f.sec=2;
    nt->f.opt=sizeof(OPT);
    nt->f.ch=0x22;

    OPT*o=&nt->o;
    o->magic=0x20B;
    o->entry=0x1000;
    o->base=0x1000;
    o->ib=IB;
    o->sa=SA;
    o->fa=FA;
    o->imgsz=0x3000;
    o->hdrsz=0x200;
    o->subsys=3;
    o->stkres=o->heapres=0x100000;
    o->stkcom=o->heapcom=0x1000;
    o->dirs=16;

    // import dir
    o->dir[1].va=0x2000;
    o->dir[1].size=0x100;

    // ---------------- SECTIONS
    SH*s=(SH*)((uint8_t*)nt+24+sizeof(OPT));

    // .text
    memcpy(s[0].name,".text",5);
    s[0].va=0x1000;
    s[0].vs=0x100;
    s[0].rs=FA;
    s[0].rp=0x200;
    s[0].ch=0x60000020;

    // .idata
    memcpy(s[1].name,".idata",6);
    s[1].va=0x2000;
    s[1].vs=0x100;
    s[1].rs=FA;
    s[1].rp=0x400;
    s[1].ch=0xC0000040;

    // ---------------- CODE
    uint8_t code[]={
        0x48,0x83,0xEC,0x28,
        0x31,0xC9,
        0xFF,0x15,0,0,0,0
    };

    memcpy(img+0x200,code,sizeof(code));

    // ---------------- IDATA
    uint8_t*id=img+0x400;

    IMP*imp=(IMP*)id;

    uint32_t iltRVA=0x2000+0x28;
    uint32_t iatRVA=0x2000+0x38;
    uint32_t nameRVA=0x2000+0x48;
    uint32_t hintRVA=0x2000+0x60;

    imp->oft=iltRVA;
    imp->name=nameRVA;
    imp->ft=iatRVA;

    // null descriptor auto zero

    // ILT
    *(uint64_t*)(id+0x28)=hintRVA;
    *(uint64_t*)(id+0x30)=0;

    // IAT
    *(uint64_t*)(id+0x38)=hintRVA;
    *(uint64_t*)(id+0x40)=0;

    // DLL name
    strcpy((char*)(id+0x48),"kernel32.dll");

    // Hint/name
    *(uint16_t*)(id+0x60)=0;
    strcpy((char*)(id+0x62),"ExitProcess");

    // ---------------- PATCH CALL
    uint64_t callVA=IB+0x1000+10;
    uint64_t iatVA=IB+iatRVA;
    *(int32_t*)(img+0x200+8)=(int32_t)(iatVA-callVA);

    // ---------------- WRITE
    FILE*f=fopen("working.exe","wb");
    fwrite(img,1,0x600,f);
    fclose(f);

    printf("working.exe created\n");
}
// 000001D8