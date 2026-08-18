#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FA 0x200
#define SA 0x1000
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

int main(){
    FILE*f=fopen("print32.exe","wb");
    if(!f) return 1;

    uint8_t buf[4096];
    memset(buf,0,sizeof(buf));

    // --------------------------------------------------
    // OFFSETS / RVAs
    // --------------------------------------------------

    uint32_t textRaw  = 0x200;
    uint32_t idataRaw = 0x400;

    uint32_t textRVA  = 0x1000;
    uint32_t idataRVA = 0x2000;

    // --------------------------------------------------
    // DOS HEADER
    // --------------------------------------------------

    *(uint16_t*)(buf+0x00)=0x5A4D;
    *(uint32_t*)(buf+0x3C)=0x80;

    // --------------------------------------------------
    // NT HEADERS
    // --------------------------------------------------

    uint8_t*nt=buf+0x80;
    *(uint32_t*)(nt+0x00)=0x4550;

    // FILE HEADER
    *(uint16_t*)(nt+4)=0x14C;
    *(uint16_t*)(nt+6)=2;
    *(uint16_t*)(nt+20)=0xE0;
    *(uint16_t*)(nt+22)=0x0102;

    // OPTIONAL HEADER (PE32)
    uint8_t*op=nt+24;

    *(uint16_t*)(op+0x00)=0x10B;
    *(uint32_t*)(op+0x10)=textRVA;
    *(uint32_t*)(op+0x14)=textRVA;
    *(uint32_t*)(op+0x18)=0x3000;
    *(uint32_t*)(op+0x1C)=0x400000;

    *(uint32_t*)(op+0x20)=SA;
    *(uint32_t*)(op+0x24)=FA;

    *(uint32_t*)(op+0x38)=0x3000;
    *(uint32_t*)(op+0x3C)=0x200;

    *(uint16_t*)(op+0x44)=3;

    *(uint32_t*)(op+0x48)=0x100000;
    *(uint32_t*)(op+0x4C)=0x1000;
    *(uint32_t*)(op+0x50)=0x100000;
    *(uint32_t*)(op+0x54)=0x1000;

    *(uint32_t*)(op+0x5C)=16;

    // Import directory
    *(uint32_t*)(op+0x68)=idataRVA;
    *(uint32_t*)(op+0x6C)=0x100;

    // --------------------------------------------------
    // SECTION HEADERS
    // --------------------------------------------------

    uint8_t*sec=nt+24+0xE0;

    // .text
    memcpy(sec,".text",5);
    *(uint32_t*)(sec+8)=0x200;
    *(uint32_t*)(sec+12)=textRVA;
    *(uint32_t*)(sec+16)=FA;
    *(uint32_t*)(sec+20)=textRaw;
    *(uint32_t*)(sec+36)=0x60000020;

    // .idata
    sec+=40;
    memcpy(sec,".idata",6);
    *(uint32_t*)(sec+8)=0x200;
    *(uint32_t*)(sec+12)=idataRVA;
    *(uint32_t*)(sec+16)=FA;
    *(uint32_t*)(sec+20)=idataRaw;
    *(uint32_t*)(sec+36)=0xC0000040;

    // --------------------------------------------------
    // IMPORT TABLE
    // --------------------------------------------------

    uint8_t*id=buf+idataRaw;

    uint32_t oftRVA = idataRVA+0x28;
    uint32_t ftRVA  = idataRVA+0x38;
    uint32_t nameRVA= idataRVA+0x48;

    // Descriptor
    *(uint32_t*)(id+0x00)=oftRVA;
    *(uint32_t*)(id+0x0C)=nameRVA;
    *(uint32_t*)(id+0x10)=ftRVA;

    // DLL name
    strcpy((char*)(id+0x48),"kernel32.dll");

    // ILT / IAT
    uint32_t hn= idataRVA+0x60;

    uint32_t*ilt=(uint32_t*)(id+0x28);
    uint32_t*iat=(uint32_t*)(id+0x38);

    const char*fn[]={"GetStdHandle","WriteFile","ExitProcess"};

    uint32_t off=0x60;
    for(int i=0;i<3;i++){
        ilt[i]=iat[i]=idataRVA+off;
        *(uint16_t*)(id+off)=0;
        strcpy((char*)(id+off+2),fn[i]);
        off+=2+strlen(fn[i])+1;
    }

    // --------------------------------------------------
    // CODE
    // --------------------------------------------------

    uint8_t*code=buf+textRaw;

    uint32_t msgRVA=textRVA+0x80;
    uint32_t wrRVA =textRVA+0xA0;

    uint8_t c[]={
        0x6A,0xF5,                         // push -11
        0xFF,0x15,0,0,0,0,                 // call [GetStdHandle]
        0x50,                              // push eax
        0x6A,0x00,
        0x68,0,0,0,0,                      // &written
        0x6A,16,
        0x68,0,0,0,0,                      // msg
        0x50,
        0xFF,0x15,0,0,0,0,                 // call [WriteFile]
        0x6A,0,
        0xFF,0x15,0,0,0,0                  // call [ExitProcess]
    };

    memcpy(code,c,sizeof(c));

    *(uint32_t*)(code+4) = 0x400000+ftRVA;
    *(uint32_t*)(code+13)= 0x400000+wrRVA;
    *(uint32_t*)(code+19)= 0x400000+msgRVA;
    *(uint32_t*)(code+25)= 0x400000+ftRVA+4;
    *(uint32_t*)(code+31)= 0x400000+ftRVA+8;

    // message
    strcpy((char*)(buf+textRaw+0x80),"Hello from PE32!\r\n");

    // --------------------------------------------------
    fwrite(buf,1,0x600,f);
    fclose(f);

    printf("print32.exe created\n");
}
