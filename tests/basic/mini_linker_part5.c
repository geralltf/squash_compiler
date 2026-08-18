#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SECTS 256
#define MAX_SYMS  1024

// ------------------------------------------------------------
// DATA STRUCTURES
// ------------------------------------------------------------

typedef struct{
    char name[64];
    uint8_t *data;
    uint32_t size;
    int isCOMDAT;
    int live;
} Section;

typedef struct{
    char name[64];
    int sectionIndex;
    uint32_t offset;
} Symbol;

typedef struct{
    int fromSection;
    uint32_t offset;
    char target[64];
} Reloc;

// ------------------------------------------------------------

Section sections[MAX_SECTS];
Symbol  symbols[MAX_SYMS];
Reloc   relocs[MAX_SYMS];

int scount=0, symcount=0, rcount=0;

// ------------------------------------------------------------
// HELPERS
// ------------------------------------------------------------

int find_symbol(const char*n){
    for(int i=0;i<symcount;i++)
        if(strcmp(symbols[i].name,n)==0)
            return i;
    return -1;
}

int add_section(const char*n,int comdat){
    strcpy(sections[scount].name,n);
    sections[scount].data=malloc(1024);
    sections[scount].size=0;
    sections[scount].isCOMDAT=comdat;
    sections[scount].live=0;
    return scount++;
}

// ------------------------------------------------------------
// COMDAT SELECTION
// ------------------------------------------------------------

void comdat_select(){

    for(int i=0;i<scount;i++){
        if(!sections[i].isCOMDAT) continue;

        for(int j=i+1;j<scount;j++){
            if(sections[j].isCOMDAT &&
               strcmp(sections[i].name,
                      sections[j].name)==0){

                // discard duplicate
                sections[j].size=0;
            }
        }
    }
}

// ------------------------------------------------------------
// DEAD CODE ELIMINATION
// ------------------------------------------------------------

void mark_live(const char*entry){

    int s=find_symbol(entry);
    if(s<0) return;

    int sec=symbols[s].sectionIndex;
    sections[sec].live=1;

    // recursively mark via relocations
    for(int i=0;i<rcount;i++){
        if(relocs[i].fromSection==sec){

            int ts=find_symbol(
                relocs[i].target);

            if(ts>=0){
                int tsec=
                    symbols[ts].sectionIndex;

                if(!sections[tsec].live){
                    sections[tsec].live=1;
                    mark_live(
                      symbols[ts].name);
                }
            }
        }
    }
}

// ------------------------------------------------------------
// DEMO OBJECT INPUT
// ------------------------------------------------------------

void mock_input(){

    // funcA (entry)
    int a=add_section("funcA",0);
    sections[a].size=10;

    strcpy(symbols[symcount].name,"funcA");
    symbols[symcount++] =
        (Symbol){"funcA",a,0};

    // funcB (used)
    int b=add_section("funcB",1);
    sections[b].size=10;

    strcpy(symbols[symcount].name,"funcB");
    symbols[symcount++] =
        (Symbol){"funcB",b,0};

    // funcC (dead)
    int c=add_section("funcC",1);
    sections[c].size=10;

    strcpy(symbols[symcount].name,"funcC");
    symbols[symcount++] =
        (Symbol){"funcC",c,0};

    // relocation A → B
    relocs[rcount++] =
        (Reloc){a,0,"funcB"};
}

// ------------------------------------------------------------
// LINK
// ------------------------------------------------------------

int main(){

    mock_input();

    printf("Before COMDAT/DCE:\n");
    for(int i=0;i<scount;i++)
        printf("%s size=%u\n",
               sections[i].name,
               sections[i].size);

    comdat_select();

    mark_live("funcA");

    printf("\nAfter DCE:\n");
    for(int i=0;i<scount;i++){
        if(sections[i].live &&
           sections[i].size)
            printf("KEEP %s\n",
                   sections[i].name);
        else
            printf("DROP %s\n",
                   sections[i].name);
    }
}
