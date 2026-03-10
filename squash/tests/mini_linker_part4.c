#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

// ------------------------------------------------------------
// COFF ARCHIVE STRUCTURES
// ------------------------------------------------------------

typedef struct {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char end[2];
} ARHDR;

// ------------------------------------------------------------

typedef struct {
    char func[64];
    char dll[64];
} Import;

Import imports[256];
int importCount=0;

// ------------------------------------------------------------
// PARSE .LIB
// ------------------------------------------------------------

void parse_lib(const char*path){
    FILE*f=fopen(path,"rb");
    if(!f) return;

    char sig[8];
    fread(sig,1,8,f);

    if(strncmp(sig,"!<arch>\n",8)!=0){
        fclose(f);
        return;
    }

    while(!feof(f)){
        ARHDR h;
        if(fread(&h,1,60,f)!=60) break;

        int size=atoi(h.size);
        long pos=ftell(f);

        uint8_t*data=malloc(size);
        fread(data,1,size,f);

        // crude scan for import strings
        for(int i=0;i<size-16;i++){
            if(isalpha(data[i]) &&
               strstr((char*)&data[i],".dll")){

                char*dll=(char*)&data[i];

                char*fn=dll+strlen(dll)+1;

                if(strlen(fn)<64 &&
                   strlen(dll)<64){

                    strcpy(imports[importCount].dll,dll);
                    strcpy(imports[importCount].func,fn);
                    importCount++;
                }
            }
        }

        free(data);

        fseek(f,pos+ALIGN(size,2),SEEK_SET);
    }

    fclose(f);
}

// ------------------------------------------------------------
// BUILD IDATA
// ------------------------------------------------------------

uint32_t build_idata(uint8_t*b,uint32_t rva){

    uint32_t off=0;

    typedef struct{
        uint32_t oft,t,f,name,ft;
    } IMPDESC;

    IMPDESC*d=(IMPDESC*)b;

    off+=sizeof(IMPDESC)*(importCount+1);

    uint32_t nameRVA=rva+off;

    strcpy((char*)(b+off),
           imports[0].dll);

    off+=strlen(imports[0].dll)+1;

    uint32_t iltRVA=rva+off;
    uint64_t*ilt=(uint64_t*)(b+off);
    off+=(importCount+1)*8;

    uint32_t iatRVA=rva+off;
    uint64_t*iat=(uint64_t*)(b+off);
    off+=(importCount+1)*8;

    for(int i=0;i<importCount;i++){
        uint32_t hnRVA=rva+off;

        *(uint16_t*)(b+off)=0;
        strcpy((char*)(b+off+2),
               imports[i].func);

        ilt[i]=hnRVA;
        iat[i]=hnRVA;

        off+=2+strlen(imports[i].func)+1;
    }

    d[0].oft=iltRVA;
    d[0].name=nameRVA;
    d[0].ft=iatRVA;

    return ALIGN(off,16);
}

// ------------------------------------------------------------
// DEMO MAIN
// ------------------------------------------------------------

int main(int c,char**v){

    if(c<2){
        printf("usage: linker lib\n");
        return 0;
    }

    parse_lib(v[1]);

    uint8_t idata[4096]={0};

    uint32_t sz=
        build_idata(idata,0x3000);

    FILE*f=fopen("idata.bin","wb");
    fwrite(idata,1,sz,f);
    fclose(f);

    printf("Extracted %d imports\n",
           importCount);
}
