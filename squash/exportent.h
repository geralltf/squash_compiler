#ifndef EXPORTENT_H
#define EXPORTENT_H

#include <stdbool.h>
#include <uchar.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t RVA;
typedef uint64_t VA;

struct exportent {
    VA addr;
    uint16_t ordinal;
    char* symbolName;
    char* moduleName;
    char* forwardName;
};

//typedef struct exportent exportent_t;

#endif