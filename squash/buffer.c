/*
The MIT License (MIT)

Copyright (c) 2013 Andrew Ruef

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//#include <cstring>
//#include <fstream>

// keep this header above "windows.h" because it contains many types
//#include "parse.h"

#include "buffer.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN

#include <intrin.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


inline uint16_t byteSwapUint16(uint16_t val)
{
#if defined(_MSC_VER) || defined(_MSC_FULL_VER)
    return _byteswap_ushort(val);
#else
    return __builtin_bswap16(val);
#endif
}

inline uint32_t byteSwapUint32(uint32_t val)
{
#if defined(_MSC_VER) || defined(_MSC_FULL_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
}

inline uint64_t byteSwapUint64(uint64_t val)
{
#if defined(_MSC_VER) || defined(_MSC_FULL_VER)
    return _byteswap_uint64(val);
#else
    return __builtin_bswap64(val);
#endif
}

//extern std::uint32_t err;
//extern std::string err_loc;

struct buffer_detail
{
#ifdef _WIN32
    HANDLE file;
    HANDLE sec;
#else
    int fd;
#endif
};

bool readByte(bounded_buffer* b, uint32_t offset, uint8_t* out)
{
    if (b == NULL)
    {
        // PE_ERR(PEERR_BUFFER);
        return false;
    }

    if (offset >= b->bufLen)
    {
        // PE_ERR(PEERR_ADDRESS);
        return false;
    }

    uint8_t* tmp = (b->buf + offset);
    out = *tmp;

    return true;
}

bool readWord(bounded_buffer* b, uint32_t offset, uint16_t* out)
{
    if (b == NULL)
    {
        // PE_ERR(PEERR_BUFFER);
        return false;
    }

    if ((uint64_t)(offset) + 1 >= b->bufLen)
    {
        // PE_ERR(PEERR_ADDRESS);
        return false;
    }

    uint16_t tmp;
    memcpy(&tmp, (b->buf + offset), sizeof(uint16_t));
    
    if (b->swapBytes)
    {
        out = byteSwapUint16(tmp);
    }
    else
    {
        out = tmp;
    }

    return true;
}

bool readDword(bounded_buffer* b, uint32_t offset, uint32_t* out)
{
    if (b == NULL)
    {
        // PE_ERR(PEERR_BUFFER);
        return false;
    }

    if ((uint64_t)(offset) + 3 >= b->bufLen)
    {
        // PE_ERR(PEERR_ADDRESS);
        return false;
    }

    uint32_t tmp;
    memcpy(&tmp, (b->buf + offset), sizeof(uint32_t));
    if (b->swapBytes)
    {
        out = byteSwapUint32(tmp);
    }
    else
    {
        out = tmp;
    }

    return true;
}

bool readQword(bounded_buffer* b, uint32_t offset, uint64_t* out)
{
    if (b == NULL)
    {
        // PE_ERR(PEERR_BUFFER);
        return false;
    }

    if ((uint64_t)(offset) + 7 >= b->bufLen)
    {
        // PE_ERR(PEERR_ADDRESS);
        return false;
    }

    uint64_t tmp;
    memcpy(&tmp, (b->buf + offset), sizeof(uint64_t));
    if (b->swapBytes)
    {
        out = byteSwapUint64(tmp);
    }
    else
    {
        out = tmp;
    }

    return true;
}

bool readChar16(bounded_buffer* b, uint32_t offset, char16_t* out)
{
    if (b == NULL)
    {
        // PE_ERR(PEERR_BUFFER);
        return false;
    }

    if ((uint64_t)(offset) + 1 >= b->bufLen)
    {
        // PE_ERR(PEERR_ADDRESS);
        return false;
    }

    char16_t tmp;
    if (b->swapBytes)
    {
        uint8_t tmpBuf[2];
        tmpBuf[0] = *(b->buf + offset + 1);
        tmpBuf[1] = *(b->buf + offset);
        memcpy(&tmp, tmpBuf, sizeof(uint16_t));
    }
    else
    {
        memcpy(&tmp, (b->buf + offset), sizeof(uint16_t));
    }
    out = tmp;

    return true;
}

bounded_buffer* readFileToFileBuffer(const char* filePath)
{
#ifdef _WIN32
    HANDLE h = CreateFileA(filePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (h == INVALID_HANDLE_VALUE) {
        // PE_ERR(PEERR_OPEN);
        return NULL;
    }

    DWORD fileSize = GetFileSize(h, NULL);

    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(h);
        // PE_ERR(PEERR_OPEN);
        return NULL;
    }

#else
    // only where we have mmap / open / etc
    int fd = open(filePath, O_RDONLY);

    if (fd == -1) {
        // PE_ERR(PEERR_OPEN);
        return NULL;
    }
#endif

    // make a buffer object
    bounded_buffer* p = (bounded_buffer*)malloc(sizeof(bounded_buffer));
    if (p == NULL)
    {
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    memset(p, 0, sizeof(bounded_buffer));
    struct buffer_detail* d = (struct buffer_detail*)malloc(sizeof(struct buffer_detail));

    if (d == NULL)
    {
        free(p);
        // PE_ERR(PEERR_MEM);
        return NULL;
    }
    memset(d, 0, sizeof(struct buffer_detail));
    p->detail = d;

    // only where we have mmap / open / etc
#ifdef _WIN32
    p->detail->file = h;

    HANDLE hMap = CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);

    if (hMap == NULL)
    {
        CloseHandle(h);
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    p->detail->sec = hMap;

    LPVOID ptr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);

    if (ptr == NULL)
    {
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    p->buf = (uint8_t*)(ptr);
    p->bufLen = fileSize;
#else
    p->detail->fd = fd;

    struct stat s;
    memset(&s, 0, sizeof(struct stat));

    if (fstat(fd, &s) != 0)
    {
        close(fd);
        free(d);
        free(p);
        // PE_ERR(PEERR_STAT);
        return NULL;
    }

    void* maddr = mmap(nullptr,
        (size_t)(s.st_size),
        PROT_READ,
        MAP_SHARED,
        fd,
        0);

    if (maddr == MAP_FAILED)
    {
        close(fd);
        free(d);
        free(p);
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    p->buf = (uint8_t*)(maddr);
    p->bufLen = (uint32_t)(s.st_size);
#endif
    p->copy = false;
    p->swapBytes = false;

    return p;
}

bounded_buffer* makeBufferFromPointer(uint8_t* data, uint32_t sz)
{
    if (data == NULL)
    {
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    bounded_buffer* p = (bounded_buffer*)malloc(sizeof(bounded_buffer));

    if (p == NULL)
    {
        // PE_ERR(PEERR_MEM);
        return NULL;
    }

    p->copy = true;
    p->detail = NULL;
    p->buf = data;
    p->bufLen = sz;
    p->swapBytes = false;

    return p;
}

// split buffer inclusively from from to to by offset
bounded_buffer* splitBuffer(bounded_buffer* b, uint32_t from, uint32_t to)
{
    if (b == NULL)
    {
        return NULL;
    }

    // safety checks
    if (to < from || to > b->bufLen)
    {
        return NULL;
    }

    // make a new buffer
    bounded_buffer* newBuff = (bounded_buffer*)malloc(sizeof(bounded_buffer));
    if (newBuff == NULL)
    {
        return NULL;
    }

    newBuff->copy = true;
    newBuff->buf = b->buf + from;
    newBuff->bufLen = (to - from);

    return newBuff;
}

void deleteBuffer(bounded_buffer* b)
{
    if (b == NULL)
    {
        return;
    }

    if (!b->copy)
    {
#ifdef _WIN32
        UnmapViewOfFile(b->buf);
        CloseHandle(b->detail->sec);
        CloseHandle(b->detail->file);
#else
        munmap(b->buf, b->bufLen);
        close(b->detail->fd);
#endif
    }

    free(b->detail);
    free(b);
}

uint64_t bufLen(bounded_buffer* b)
{
    return b->bufLen;
}