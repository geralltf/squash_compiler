#ifndef DEBUGENT_H
#define DEBUGENT_H

#include <stdbool.h>
#include <uchar.h>
#include <stdint.h>
#include <string.h>

struct buffer_detail;

typedef struct _bounded_buffer {
    uint8_t* buf;
    uint32_t bufLen;
    bool copy;
    bool swapBytes;
    struct buffer_detail* detail;
} bounded_buffer;

struct debugent {
    uint32_t type;
    bounded_buffer* data;
};

#endif