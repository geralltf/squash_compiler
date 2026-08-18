#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FA 0x200
#define SA 0x1000
#define IB 0x140000000ULL
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

// ------------------------------------------------------------

int main() {
    FILE *f = fopen("print64.exe","wb");
    if(!f) return 1;

    uint8_t buf[8192];
    memset(buf,0,sizeof(buf));

    uint32_t textRaw=0x200, idataRaw=0x400;
    uint32_t textRVA=0x1000, idataRVA=0x2000;

    // ------------------------------------------------ DOS
    *(uint16_t*)(buf)=0x5A4D;
    *(uint32_t*)(buf+0x3C)=0x80;

    // ------------------------------------------------ NT
    uint8_t*nt=buf+0x80;
    *(uint32_t*)nt=0x4550;

    // FILE HEADER
    *(uint16_t*)(nt+4)=0x8664;
    *(uint16_t*)(nt+6)=2;
    *(uint16_t*)(nt+20)=0xF0;
    *(uint16_t*)(nt+22)=0x22;

    // OPTIONAL HEADER (PE32+)
    uint8_t*op=nt+24;
    *(uint16_t*)(op)=0x20B;

    *(uint32_t*)(op+16)=textRVA;
    *(uint32_t*)(op+20)=textRVA;

    *(uint64_t*)(op+24)=IB;

    *(uint32_t*)(op+32)=SA;
    *(uint32_t*)(op+36)=FA;

    *(uint32_t*)(op+56)=0x3000;
    *(uint32_t*)(op+60)=0x200;

    *(uint16_t*)(op+68)=3;

    *(uint64_t*)(op+72)=0x100000;
    *(uint64_t*)(op+80)=0x1000;
    *(uint64_t*)(op+88)=0x100000;
    *(uint64_t*)(op+96)=0x1000;

    *(uint32_t*)(op+108)=16;

    // Import directory
    *(uint32_t*)(op+120)=idataRVA;
    *(uint32_t*)(op+124)=0x200;

    // ------------------------------------------------ SECTIONS
    uint8_t*sec=nt+24+0xF0;

    memcpy(sec,".text",5);
    *(uint32_t*)(sec+8)=0x200;
    *(uint32_t*)(sec+12)=textRVA;
    *(uint32_t*)(sec+16)=FA;
    *(uint32_t*)(sec+20)=textRaw;
    *(uint32_t*)(sec+36)=0x60000020;

    sec+=40;

    memcpy(sec,".idata",6);
    *(uint32_t*)(sec+8)=0x200;
    *(uint32_t*)(sec+12)=idataRVA;
    *(uint32_t*)(sec+16)=FA;
    *(uint32_t*)(sec+20)=idataRaw;
    *(uint32_t*)(sec+36)=0xC0000040;

    // ------------------------------------------------ IMPORTS
    uint8_t*id=buf+idataRaw;

    uint32_t iltRVA=idataRVA+0x28;
    uint32_t iatRVA=idataRVA+0x48;
    uint32_t nameRVA=idataRVA+0x68;

    *(uint32_t*)(id)=iltRVA;
    *(uint32_t*)(id+12)=nameRVA;
    *(uint32_t*)(id+16)=iatRVA;

    strcpy((char*)(id+0x68),"kernel32.dll");

    uint64_t*ilt=(uint64_t*)(id+0x28);
    uint64_t*iat=(uint64_t*)(id+0x48);

    const char*fn[]={"GetStdHandle","WriteFile","ExitProcess"};

    uint32_t off=0x80;
    for(int i=0;i<3;i++){
        ilt[i]=iat[i]=idataRVA+off;
        *(uint16_t*)(id+off)=0;
        strcpy((char*)(id+off+2),fn[i]);
        off+=2+strlen(fn[i])+1;
    }

    // ------------------------------------------------ CODE
    uint8_t*code=buf+textRaw;

    uint32_t msgRVA=textRVA+0x90;
    uint32_t wrRVA =textRVA+0xB0;

    uint8_t c[]={
        0x48,0x83,0xEC,0x28,                 // sub rsp,40

        0xB9,0xF5,0xFF,0xFF,0xFF,           // mov ecx,-11
        0xFF,0x15,0,0,0,0,                  // call [rip+disp]

        0x48,0x89,0xC1,                    // mov rcx,rax
        0x48,0x8D,0x15,0,0,0,0,            // lea rdx,msg
        0x41,0xB8,18,0,0,0,                // mov r8d,len
        0x4C,0x8D,0x0D,0,0,0,0,            // lea r9,written
        0x48,0xC7,0x44,0x24,0x20,0,0,0,0,  // overlap=0
        0xFF,0x15,0,0,0,0,                // call WriteFile

        0x31,0xC9,                        // xor ecx,ecx
        0xFF,0x15,0,0,0,0                 // call ExitProcess
    };

    memcpy(code,c,sizeof(c));

    // Patch RIP-relative displacements
    uint64_t next;
    int32_t disp;

    // GetStdHandle
    next=IB+textRVA+11;
    disp=(IB+iatRVA-next);
    *(int32_t*)(code+7)=disp;

    // msg lea
    next=IB+textRVA+20;
    disp=(IB+msgRVA-next);
    *(int32_t*)(code+16)=disp;

    // written lea
    next=IB+textRVA+31;
    disp=(IB+wrRVA-next);
    *(int32_t*)(code+27)=disp;

    // WriteFile
    next=IB+textRVA+43;
    disp=(IB+iatRVA+8-next);
    *(int32_t*)(code+39)=disp;

    // ExitProcess
    next=IB+textRVA+49;
    disp=(IB+iatRVA+16-next);
    *(int32_t*)(code+45)=disp;

    strcpy((char*)(buf+textRaw+0x90),"Hello from PE64!\r\n");

    // ------------------------------------------------ WRITE
    fwrite(buf,1,0x600,f);
    fclose(f);

    printf("print64.exe created\n");
    return 0;
}
