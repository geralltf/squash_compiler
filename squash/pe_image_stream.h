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

#ifndef PE_IMAGE_STREAM_H
#define PE_IMAGE_STREAM_H

#include <cstdint>
#include <map>
#include <string>
#include <stdbool.h>
#include <uchar.h>

#include "nt-headers.h"

#include <sstream>
#include <string>

//#if defined(_MSC_VER)
//typedef std::basic_string<wchar_t> UCharString;
//#else
//typedef std::u16string UCharString;
//#endif

//template <class T>
//static std::string to_string(T t, std::ios_base& (*f)(std::ios_base&)) {
//    std::ostringstream oss;
//    oss << f << t;
//    return oss.str();
//}
//
//std::string from_utf16(const std::u16string& utf16_str);
////std::string from_utf16(const UCharString& u);

#ifdef _MSC_VER
#define __typeof__(x) std::remove_reference<decltype(x)>::type
#endif

#define PE_ERR(x)               \
  err = static_cast<pe_err>(x); \
  err_loc.assign(__func__);     \
  err_loc += ":" + to_string<std::uint32_t>(__LINE__, std::dec);

#define READ_WORD(b, o, inst, member)                                          \
  if (!readWord(b,                                                             \
                o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
                inst.member)) {                                                \
    PE_ERR(PEERR_READ);                                                        \
    return false;                                                              \
  }

#define READ_DWORD(b, o, inst, member)                                   \
  if (!readDword(                                                        \
          b,                                                             \
          o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
          inst.member)) {                                                \
    PE_ERR(PEERR_READ);                                                  \
    return false;                                                        \
  }

#define READ_QWORD(b, o, inst, member)                                   \
  if (!readQword(                                                        \
          b,                                                             \
          o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
          inst.member)) {                                                \
    PE_ERR(PEERR_READ);                                                  \
    return false;                                                        \
  }

#define READ_BYTE(b, o, inst, member)                                          \
  if (!readByte(b,                                                             \
                o + static_cast<uint32_t>(offsetof(__typeof__(inst), member)), \
                inst.member)) {                                                \
    PE_ERR(PEERR_READ);                                                        \
    return false;                                                              \
  }

#define TEST_MACHINE_CHARACTERISTICS(h, m, ch) \
  ((h.FileHeader.Machine == m) && (h.FileHeader.Characteristics & ch))

    typedef uint32_t RVA;
    typedef uint64_t VA;

    struct buffer_detail;

    typedef struct _bounded_buffer {
        uint8_t* buf;
        uint32_t bufLen;
        bool copy;
        bool swapBytes;
        struct buffer_detail* detail;
    } bounded_buffer;

    struct resource {
        //resource()
        //    : type(0), name(0), lang(0), codepage(0), RVA(0), size(0), buf(nullptr) {
        //}

        char* type_str;
        char* name_str;
        char* lang_str;
        uint32_t type;
        uint32_t name;
        uint32_t lang;
        uint32_t codepage;
        uint32_t RVA;
        uint32_t size;
        bounded_buffer* buf;
    };

#ifndef _PEPARSE_WINDOWS_CONFLICTS
    // http://msdn.microsoft.com/en-us/library/ms648009(v=vs.85).aspx
    enum resource_type {
        RT_CURSOR = 1,
        RT_BITMAP = 2,
        RT_ICON = 3,
        RT_MENU = 4,
        RT_DIALOG = 5,
        RT_STRING = 6,
        RT_FONTDIR = 7,
        RT_FONT = 8,
        RT_ACCELERATOR = 9,
        RT_RCDATA = 10,
        RT_MESSAGETABLE = 11,
        RT_GROUP_CURSOR = 12, // MAKEINTRESOURCE((ULONG_PTR)(RT_CURSOR) + 11)
        RT_GROUP_ICON = 14,   // MAKEINTRESOURCE((ULONG_PTR)(RT_ICON) + 11)
        RT_VERSION = 16,
        RT_DLGINCLUDE = 17,
        RT_PLUGPLAY = 19,
        RT_VXD = 20,
        RT_ANICURSOR = 21,
        RT_ANIICON = 22,
        RT_HTML = 23,
        RT_MANIFEST = 24
    };
#endif

    enum pe_err {
        PEERR_NONE = 0,
        PEERR_MEM = 1,
        PEERR_HDR = 2,
        PEERR_SECT = 3,
        PEERR_RESC = 4,
        PEERR_SECTVA = 5,
        PEERR_READ = 6,
        PEERR_OPEN = 7,
        PEERR_STAT = 8,
        PEERR_MAGIC = 9,
        PEERR_BUFFER = 10,
        PEERR_ADDRESS = 11,
        PEERR_SIZE = 12,
    };

    bool readByte(bounded_buffer* b, uint32_t offset, uint8_t& out);
    bool readWord(bounded_buffer* b, uint32_t offset, uint16_t& out);
    bool readDword(bounded_buffer* b, uint32_t offset, uint32_t& out);
    bool readQword(bounded_buffer* b, uint32_t offset, uint64_t& out);
    bool readChar16(bounded_buffer* b, uint32_t offset, char16_t& out);

    bounded_buffer* readFileToFileBuffer(const char* filePath);
    bounded_buffer* makeBufferFromPointer(uint8_t* data, uint32_t sz);
    bounded_buffer* splitBuffer(bounded_buffer* b, uint32_t from, uint32_t to);
    void deleteBuffer(bounded_buffer* b);
    uint64_t bufLen(bounded_buffer* b);

    struct parsed_pe_internal;

    typedef struct _pe_header {
        struct dos_header dos;
        struct rich_header rich;
        struct nt_header_32 nt;
    } pe_header;

    typedef struct _parsed_pe {
        bounded_buffer* fileBuffer;
        struct parsed_pe_internal* internal;
        pe_header peHeader;
    } parsed_pe;

    // Resolve a Rich header product id / build number pair to a known
    // product name
    //typedef std::pair<std::uint16_t, std::uint16_t> ProductKey;
    const char* GetRichObjectType(uint16_t prodId);
    const char* GetRichProductName(uint16_t buildNum);

    // get parser error status as integer
    uint32_t GetPEErr();

    // get parser error status as string
    char* GetPEErrString();

    // get parser error location as string
    char* GetPEErrLoc();

    // get a PE parse context from a file
    parsed_pe* ParsePEFromFile(const char* filePath);

    parsed_pe* ParsePEFromPointer(std::uint8_t* buffer, std::uint32_t sz);
    parsed_pe* ParsePEFromBuffer(bounded_buffer* buffer);

    // destruct a PE context
    void DestructParsedPE(parsed_pe* p);

    // iterate over Rich header entries
    typedef int (*iterRich)(void*, const rich_entry&);
    void IterRich(parsed_pe* pe, iterRich cb, void* cbd);

    // iterate over the resources
    typedef int (*iterRsrc)(void*, const resource&);
    void IterRsrc(parsed_pe* pe, iterRsrc cb, void* cbd);

    // iterate over the imports by RVA and string
    typedef int (*iterVAStr)(void*,
        const VA&,
        const std::string&,
        const std::string&);
    void IterImpVAString(parsed_pe* pe, iterVAStr cb, void* cbd);

    // iterate over relocations in the PE file
    typedef int (*iterReloc)(void*, const VA&, const reloc_type&);
    void IterRelocs(parsed_pe* pe, iterReloc cb, void* cbd);

    // iterate over debug directories in the PE file
    typedef int (*iterDebug)(void*, const std::uint32_t&, const bounded_buffer*);
    void IterDebugs(parsed_pe* pe, iterDebug cb, void* cbd);

    // Iterate over symbols (symbol table) in the PE file
    typedef int (*iterSymbol)(void*,
        const std::string&,
        const std::uint32_t&,
        const std::int16_t&,
        const std::uint16_t&,
        const std::uint8_t&,
        const std::uint8_t&);
    void IterSymbols(parsed_pe* pe, iterSymbol cb, void* cbd);

    // iterate over the exports, except forwarded exports
    typedef int (*iterExp)(void*,
        const VA&,
        const std::string&,
        const std::string&);
    void IterExpVA(parsed_pe* pe, iterExp cb, void* cbd);

    // iterate over the exports, including forwarded exports
    // export ordinal is also provided as the third argument.
    // VA will be zero if the current export is forwarded,
    // in this case, the last argument (forward string) will be non-empty
    typedef int (*iterExpFull)(void*,
        const VA&,
        std::uint16_t,
        const std::string&,
        const std::string&,
        const std::string&);
    void IterExpFull(parsed_pe* pe, iterExpFull cb, void* cbd);

    // iterate over sections
    typedef int (*iterSec)(parsed_pe*,
        int,
        void*,
        const VA&,
        const std::string&,
        const image_section_header&,
        const bounded_buffer*);
    void IterSec(parsed_pe* pe, int dt, iterSec cb, void* cbd);

    // get byte at VA in PE
    bool ReadByteAtVA(parsed_pe* pe, VA v, std::uint8_t& b);

    // get section size at VA in PE
    uint32_t ReadSectionSize(parsed_pe* pe, VA v);

    // get bytes at VA in PE
    bool ReadBytesAtVA(parsed_pe* pe, VA v, std::uint8_t* buffer, uint32_t size_buffer);

    // get entry point into PE
    bool GetEntryPoint(parsed_pe* pe, VA& v);

    // get machine as human readable string
    const char* GetMachineAsString(parsed_pe* pe);

    // get subsystem as human readable string
    const char* GetSubsystemAsString(parsed_pe* pe);

    // get a table or string by its data directory entry
    //bool GetDataDirectoryEntry(parsed_pe* pe,
    //    data_directory_kind dirnum,
    //    std::vector<std::uint8_t>& raw_entry);
#endif