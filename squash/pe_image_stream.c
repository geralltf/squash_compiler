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

//#include <algorithm>
//#include <array>
//#include <cassert>
//#include <cstring>
//#include <iostream>
//#include <stdexcept>
//#include <vector>

#include <inttypes.h> // Recommended for PRIu16 format specifier

#include "pe_image_stream.h"
//#include "nt-headers.h"
//#include "parse.h"
//#include "to_string.h"




// String representation of Rich header object types
char* kProdId_C = NULL;
char* kProdId_CPP = NULL;
char* kProdId_RES = NULL;
char* kProdId_IMP = NULL;
char* kProdId_EXP = NULL;
char* kProdId_ASM = NULL;
char* kProdId_LNK = NULL;
char* kProdId_UNK = NULL;

bool kProdId_loaded = false;

uint32_t err = 0;
char* err_loc = NULL;

char* create_heap_string(const char* source)
{
    size_t size = strlen(source) + 1;
    char* newString = (char*)malloc(size * sizeof(char));
    if (newString == NULL)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    strcpy(newString, source);
    return newString;
}

void ProductIdMap_init()
{
    if (kProdId_loaded == false)
    {
        kProdId_C = create_heap_string("[ C ]");
        kProdId_CPP = create_heap_string("[ C++ ]");
        kProdId_RES = create_heap_string("[ RES ]");
        kProdId_IMP = create_heap_string("[ IMP ]");
        kProdId_EXP = create_heap_string("[ EXP ]");
        kProdId_ASM = create_heap_string("[ ASM ]");
        kProdId_LNK = create_heap_string("[ LNK ]");
        kProdId_UNK = create_heap_string("[ ? ]");

        kProdId_loaded = true;
    }
}

// Mapping of Rich header Product ID to object type string
// Source: https://github.com/dishather/richprint/blob/master/comp_id.txt
char* GetProductIdMap(uint16_t val)
{
    if (kProdId_loaded == false)
    {
        ProductIdMap_init();
    }

    switch (val)
    {
    case 0x0000:
        return kProdId_UNK;
    case 0x0002:
        return kProdId_IMP;
    case 0x0004:
        return kProdId_LNK;
    case 0x0006:
        return kProdId_RES;
    case 0x000A:
        return kProdId_C;
    case 0x000B:
        return kProdId_CPP;
    case 0x000F:
        return kProdId_ASM;
    case 0x0015:
        return kProdId_C;
    case 0x0016:
        return kProdId_CPP;
    case 0x0019:
        return kProdId_IMP;
    case 0x001C:
        return kProdId_C;
    case 0x001D:
        return kProdId_CPP;
    case 0x003D:
        return kProdId_LNK;
    case 0x003F:
        return kProdId_EXP;
    case 0x0040:
        return kProdId_ASM;
    case 0x0045:
        return kProdId_RES;
    case 0x005A:
        return kProdId_LNK;
    case 0x005C:
        return kProdId_EXP;
    case 0x005D:
        return kProdId_IMP;
    case 0x005E:
        return kProdId_RES;
    case 0x005F:
        return kProdId_C;
    case 0x0060:
        return kProdId_CPP;
    case 0x006D:
        return kProdId_C;
    case 0x006E:
        return kProdId_CPP;
    case 0x0078:
        return kProdId_LNK;
    case 0x007A:
        return kProdId_EXP;
    case 0x007B:
        return kProdId_IMP;
    case 0x007C:
        return kProdId_RES;
    case 0x007D:
        return kProdId_ASM;
    case 0x0083:
        return kProdId_C;
    case 0x0084:
        return kProdId_CPP;
    case 0x0091:
        return kProdId_LNK;
    case 0x0092:
        return kProdId_EXP;
    case 0x0093:
        return kProdId_IMP;
    case 0x0094:
        return kProdId_RES;
    case 0x0095:
        return kProdId_ASM;
    case 0x009A:
        return kProdId_RES;
    case 0x009B:
        return kProdId_EXP;
    case 0x009C:
        return kProdId_IMP;
    case 0x009D:
        return kProdId_LNK;
    case 0x009E:
        return kProdId_ASM;
    case 0x00AA:
        return kProdId_C;
    case 0x00AB:
        return kProdId_CPP;
    case 0x00C9:
        return kProdId_RES;
    case 0x00CA:
        return kProdId_EXP;
    case 0x00CB:
        return kProdId_IMP;
    case 0x00CC:
        return kProdId_LNK;
    case 0x00CD:
        return kProdId_ASM;
    case 0x00CE:
        return kProdId_C;
    case 0x00CF:
        return kProdId_CPP;
    case 0x00DB:
        return kProdId_RES;
    case 0x00DC:
        return kProdId_EXP;
    case 0x00DD:
        return kProdId_IMP;
    case 0x00DE:
        return kProdId_LNK;
    case 0x00DF:
        return kProdId_ASM;
    case 0x00E0:
        return kProdId_C;
    case 0x00E1:
        return kProdId_CPP;
    case 0x00FF:
        return kProdId_RES;
    case 0x0100:
        return kProdId_EXP;
    case 0x0101:
        return kProdId_IMP;
    case 0x0102:
        return kProdId_LNK;
    case 0x0103:
        return kProdId_ASM;
    case 0x0104:
        return kProdId_C;
    case 0x0105:
        return kProdId_CPP;
    default:
        return NULL;
    }
}

char* GetProductMap(uint16_t id)
{
    switch (id)
    {
    case 0x0000:
        return create_heap_string("Imported Functions");
    case 0x0684:
        return create_heap_string("VS97 v5.0 SP3 cvtres 5.00.1668");
    case 0x06B8:
        return create_heap_string("VS98 v6.0 cvtres build 1720");
    case 0x06C8:
        return create_heap_string("VS98 v6.0 SP6 cvtres build 1736");
    case 0x1C87:
        return create_heap_string("VS97 v5.0 SP3 link 5.10.7303");
    case 0x5E92:
        return create_heap_string("VS2015 v14.0 UPD3 build 24210");
    case 0x5E95:
        return create_heap_string("VS2015 UPD3 build 24213");
    case 0x0BEC:
        return create_heap_string("VS2003 v7.1 Free Toolkit .NET build 3052");
    case 0x0C05:
        return create_heap_string("VS2003 v7.1 .NET build 3077");
    case 0x0FC3:
        return create_heap_string("VS2003 v7.1 | Windows Server 2003 SP1 DDK build 4035");
    case 0x1C83:
        return create_heap_string("MASM 6.13.7299");
    case 0x178E:
        return create_heap_string("VS2003 v7.1 SP1 .NET build 6030");
    case 0x1FE8:
        return create_heap_string("VS98 v6.0 RTM/SP1/SP2 build 8168");
    case 0x1FE9:
        return create_heap_string("VB 6.0/SP1/SP2 build 8169");
    case 0x20FC:
        return create_heap_string("MASM 6.14.8444");
    case 0x20FF:
        return create_heap_string("VC++ 6.0 SP3 build 8447");
    case 0x212F:
        return create_heap_string("VB 6.0 SP3 build 8495");
    case 0x225F:
        return create_heap_string("VS 6.0 SP4 build 8799");
    case 0x2263:
        return create_heap_string("MASM 6.15.8803");
    case 0x22AD:
        return create_heap_string("VB 6.0 SP4 build 8877");
    case 0x2304:
        return create_heap_string("VB 6.0 SP5 build 8964");
    case 0x2306:
        return create_heap_string("VS 6.0 SP5 build 8966");
    case 0x2346:
        return create_heap_string("VS 7.0 2000 Beta 1 build 9030");
    case 0x2354:
        return create_heap_string("VS 6.0 SP5 Processor Pack build 9044");
    case 0x2426:
        return create_heap_string("VS2001 v7.0 Beta 2 build 9254");
    case 0x24FA:
        return create_heap_string("VS2002 v7.0 .NET build 9466");
    case 0x2636:
        return create_heap_string("VB 6.0 SP6 / VC++ build 9782");
    case 0x26E3:
        return create_heap_string("VS2002 v7.0 SP1 build 9955");
    case 0x520D:
        return create_heap_string("VS2013 v12.[0,1] build 21005");
    case 0x521E:
        return create_heap_string("VS2008 v9.0 build 21022");
    case 0x56C7:
        return create_heap_string("VS2015 v14.0 build 22215");
    case 0x59F2:
        return create_heap_string("VS2015 v14.0 build 23026");
    case 0x5BD2:
        return create_heap_string("VS2015 v14.0 UPD1 build 23506");
    case 0x5D10:
        return create_heap_string("VS2015 v14.0 UPD2 build 23824");
    case 0x5E97:
        return create_heap_string("VS2015 v14.0 UPD3.1 build 24215");
    case 0x7725:
        return create_heap_string("VS2013 v12.0 UPD2 build 30501");
    case 0x766F:
        return create_heap_string("VS2010 v10.0 build 30319");
    case 0x7809:
        return create_heap_string("VS2008 v9.0 SP1 build 30729");
    case 0x797D:
        return create_heap_string("VS2013 v12.0 UPD4 build 31101");
    case 0x9D1B:
        return create_heap_string("VS2010 v10.0 SP1 build 40219");
    case 0x9EB5:
        return create_heap_string("VS2013 v12.0 UPD5 build 40629");
    case 0xC497:
        return create_heap_string("VS2005 v8.0 (Beta) build 50327");
    case 0xC627:
        return create_heap_string("VS2005 v8.0 | VS2012 v11.0 build 50727");
    case 0xC751:
        return create_heap_string("VS2012 v11.0 Nov CTP build 51025");
    case 0xC7A2:
        return create_heap_string("VS2012 v11.0 UPD1 build 51106");
    case 0xEB9B:
        return create_heap_string("VS2012 v11.0 UPD2 build 60315");
    case 0xECC2:
        return create_heap_string("VS2012 v11.0 UPD3 build 60610");
    case 0xEE66:
        return create_heap_string("VS2012 v11.0 UPD4 build 61030");
    case 0x5E9A:
        return create_heap_string("VS2015 v14.0 build 24218");
    case 0x61BB:
        return create_heap_string("VS2017 v14.1 build 25019");
    case 0x2264:
        return create_heap_string("VS 6 [SP5,SP6] build 8804");
    case 0x23D8:
        return create_heap_string("Windows XP SP1 DDK");
    case 0x0883:
        return create_heap_string("Windows Server 2003 DDK");
    case 0x08F4:
        return create_heap_string("VS2003 v7.1 .NET Beta build 2292");
    case 0x9D76:
        return create_heap_string("Windows Server 2003 SP1 DDK (for AMD64)");
    case 0x9E9F:
        return create_heap_string("VS2005 v8.0 Beta 1 build 40607");
    case 0xC427:
        return create_heap_string("VS2005 v8.0 Beta 2 build 50215");
    case 0xC490:
        return create_heap_string("VS2005 v8.0 build 50320");
    case 0x50E2:
        return create_heap_string("VS2008 v9.0 Beta 2 build 20706");
    case 0x501A:
        return create_heap_string("VS2010 v10.0 Beta 1 build 20506");
    case 0x520B:
        return create_heap_string("VS2010 v10.0 Beta 2 build 21003");
    case 0x5089:
        return create_heap_string("VS2013 v12.0 Preview build 20617");
    case 0x515B:
        return create_heap_string("VS2013 v12.0 RC build 20827");
    case 0x527A:
        return create_heap_string("VS2013 v12.0 Nov CTP build 21114");
    case 0x63A3:
        return create_heap_string("VS2017 v15.3.3 build 25507");
    case 0x63C6:
        return create_heap_string("VS2017 v15.4.4 build 25542");
    case 0x63CB:
        return create_heap_string("VS2017 v15.4.5 build 25547");
    case 0x7674:
        return create_heap_string("VS2013 v12.0 UPD2 RC build 30324");
    case 0x5D6E:
        return create_heap_string("VS2015 v14.0 UPD2 build 23918");
    case 0x61B9:
        return create_heap_string("VS2017 v15.[0,1] build 25017");
    case 0x63A2:
        return create_heap_string("VS2017 v15.2 build 25019");
    case 0x64E6:
        return create_heap_string("VS2017 v15 build 25830");
    case 0x64E7:
        return create_heap_string("VS2017 v15.5.2 build 25831");
    case 0x64EA:
        return create_heap_string("VS2017 v15.5.[3,4] build 25834");
    case 0x64EB:
        return create_heap_string("VS2017 v15.5.[5,6,7] build 25835");
    case 0x6610:
        return create_heap_string("VS2017 v15.6.[0,1,2] build 26128");
    case 0x6611:
        return create_heap_string("VS2017 v15.6.[3,4] build 26129");
    case 0x6613:
        return create_heap_string("VS2017 v15.6.6 build 26131");
    case 0x6614:
        return create_heap_string("VS2017 v15.6.7 build 26132");
    case 0x6723:
        return create_heap_string("VS2017 v15.1 build 26403");
    case 0x673C:
        return create_heap_string("VS2017 v15.7.[0,1] build 26428");
    case 0x673D:
        return create_heap_string("VS2017 v15.7.2 build 26429");
    case 0x673E:
        return create_heap_string("VS2017 v15.7.3 build 26430");
    case 0x673F:
        return create_heap_string("VS2017 v15.7.4 build 26431");
    case 0x6741:
        return create_heap_string("VS2017 v15.7.5 build 26433");
    case 0x6B74:
        return create_heap_string("VS2019 v16.0.0 build 27508");
    case 0x6866:
        return create_heap_string("VS2017 v15.8.0 build 26726");
    case 0x6869:
        return create_heap_string("VS2017 v15.8.4 build 26729");
    case 0x686A:
        return create_heap_string("VS2017 v15.8.9 build 26730");
    case 0x686C:
        return create_heap_string("VS2017 v15.8.5 build 26732");
    case 0x698F:
        return create_heap_string("VS2017 v15.9.[0,1] build 27023");
    case 0x6990:
        return create_heap_string("VS2017 v15.9.2 build 27024");
    case 0x6991:
        return create_heap_string("VS2017 v15.9.4 build 27025");
    case 0x6992:
        return create_heap_string("VS2017 v15.9.5 build 27026");
    case 0x6993:
        return create_heap_string("VS2017 v15.9.7 build 27027");
    case 0x6996:
        return create_heap_string("VS2017 v15.9.11 build 27030");
    case 0x6997:
        return create_heap_string("VS2017 v15.9.12 build 27031");
    case 0x6998:
        return create_heap_string("VS2017 v15.9.14 build 27032");
    case 0x699A:
        return create_heap_string("VS2017 v15.9.16 build 27034");
    case 0x6C36:
        return create_heap_string("VS2019 v16.1.2 UPD1 build 27702");
    case 0x6D01:
        return create_heap_string("VS2019 v16.2.3 UPD2 build 27905");
    case 0x6DC9:
        return create_heap_string("VS2019 v16.3.2 UPD3 build 28105");
    case 0x7803:
        return create_heap_string("VS2013 v12.0 UPD3 build 30723");
    case 0x685B:
        return create_heap_string("VS2017 v15.8.? build 26715");
    default:
        return create_heap_string("<unknown>");
    }
}

// Returns a stringified Rich header object type given a product id
char* GetRichObjectType(uint16_t prodId)
{
    char* outId = GetProductIdMap(prodId);
    if (outId != NULL)
    {
        return outId;
    }
    else 
    {
        return kProdId_UNK;
    }
}

// Returns a stringified Rich header product name given a build number
char* GetRichProductName(uint16_t buildNum)
{
    char* product = GetProductMap(buildNum);
    return product;
}

uint32_t GetPEErr()
{
    return err;
}

char* GetPEErrString()
{
    switch (err)
    {
    case 0:
        return create_heap_string("None");
    case 1:
        return create_heap_string("Out of memory");
    case 2:
        return create_heap_string("Invalid header");
    case 3:
        return create_heap_string("Invalid section");
    case 4:
        return create_heap_string("Invalid resource");
    case 5:
        return create_heap_string("Unable to get section for VA");
    case 6:
        return create_heap_string("Unable to read data");
    case 7:
        return create_heap_string("Unable to open");
    case 8:
        return create_heap_string("Unable to stat");
    case 9:
        return create_heap_string("Bad magic");
    case 10:
        return create_heap_string("Invalid buffer");
    case 11:
        return create_heap_string("Invalid address");
    case 12:
        return create_heap_string("Invalid size");
    default:
        return NULL;
    }
    return NULL;
}

char* GetPEErrLoc()
{
    return err_loc;
}

char* GetSymbolTableStorageClassName(uint8_t id)
{
    switch (id)
    {
    //case IMAGE_SYM_CLASS_END_OF_FUNCTION:
    //    return create_heap_string("CLASS_END_OF_FUNCTION");
    case IMAGE_SYM_CLASS_NULL:
        return create_heap_string("CLASS_NULL");
    case IMAGE_SYM_CLASS_AUTOMATIC:
        return create_heap_string("CLASS_AUTOMATIC");
    case IMAGE_SYM_CLASS_EXTERNAL:
        return create_heap_string("CLASS_EXTERNAL");
    case IMAGE_SYM_CLASS_STATIC:
        return create_heap_string("CLASS_STATIC");
    case IMAGE_SYM_CLASS_REGISTER:
        return create_heap_string("CLASS_REGISTER");
    case IMAGE_SYM_CLASS_EXTERNAL_DEF:
        return create_heap_string("CLASS_EXTERNAL_DEF");
    case IMAGE_SYM_CLASS_LABEL:
        return create_heap_string("CLASS_LABEL");
    case IMAGE_SYM_CLASS_UNDEFINED_LABEL:
        return create_heap_string("CLASS_UNDEFINED_LABEL");
    case IMAGE_SYM_CLASS_MEMBER_OF_STRUCT:
        return create_heap_string("CLASS_MEMBER_OF_STRUCT");
    case IMAGE_SYM_CLASS_ARGUMENT:
        return create_heap_string("CLASS_ARGUMENT");
    case IMAGE_SYM_CLASS_STRUCT_TAG:
        return create_heap_string("CLASS_STRUCT_TAG");
    case IMAGE_SYM_CLASS_MEMBER_OF_UNION:
        return create_heap_string("CLASS_MEMBER_OF_UNION");
    case IMAGE_SYM_CLASS_UNION_TAG:
        return create_heap_string("CLASS_UNION_TAG");
    case IMAGE_SYM_CLASS_TYPE_DEFINITION:
        return create_heap_string("CLASS_TYPE_DEFINITION");
    case IMAGE_SYM_CLASS_UNDEFINED_STATIC:
        return create_heap_string("CLASS_UNDEFINED_STATIC");
    case IMAGE_SYM_CLASS_ENUM_TAG:
        return create_heap_string("CLASS_ENUM_TAG");
    case IMAGE_SYM_CLASS_MEMBER_OF_ENUM:
        return create_heap_string("CLASS_MEMBER_OF_ENUM");
    case IMAGE_SYM_CLASS_REGISTER_PARAM:
        return create_heap_string("CLASS_REGISTER_PARAM");
    case IMAGE_SYM_CLASS_BIT_FIELD:
        return create_heap_string("CLASS_BIT_FIELD");
    case IMAGE_SYM_CLASS_BLOCK:
        return create_heap_string("CLASS_BLOCK");
    case IMAGE_SYM_CLASS_FUNCTION:
        return create_heap_string("CLASS_FUNCTION");
    case IMAGE_SYM_CLASS_END_OF_STRUCT:
        return create_heap_string("CLASS_END_OF_STRUCT");
    case IMAGE_SYM_CLASS_FILE:
        return create_heap_string("CLASS_FILE");
    case IMAGE_SYM_CLASS_SECTION:
        return create_heap_string("CLASS_SECTION");
    case IMAGE_SYM_CLASS_WEAK_EXTERNAL:
        return create_heap_string("CLASS_WEAK_EXTERNAL");
    case IMAGE_SYM_CLASS_CLR_TOKEN:
        return create_heap_string("CLASS_CLR_TOKEN");
    default:
        return NULL;
    }
}

bool readCString(const bounded_buffer* buffer, uint32_t off, char** result)
{
    if (off < buffer->bufLen)
    {
        uint8_t* p = buffer->buf;
        uint32_t n = buffer->bufLen;
        uint8_t* b = p + off;

        // std::find(b, p + n, 0) searches for the null terminator
        // memchr returns a pointer to the first occurrence of 0
        uint8_t* x = (uint8_t*)memchr(b, 0, n - off);

        if (x == NULL)
        {
            return false;
        }

        // Calculate length of the string found (excluding null terminator)
        size_t len = (size_t)(x - b);

        // Copy the data into result and null-terminate it
        memcpy(*result, b, len);

        (*result)[len] = '\0';

        return true;
    }
    return false;
}

bool getSecForVA(list_t* secs, VA v, struct section** sec) // const std::vector<section>& secs, VA v, section* sec)
{
    list_t* n = secs;
    struct section* s;

    while (n != NULL)
    {
        s = (struct section*)n->data;

        uint64_t low = s->sectionBase;
        uint64_t high = low + s->sec->Misc.VirtualSize;

        if (v >= low && v < high)
        {
            (*sec) = s;
            return true;
        }

        n = n->next;
    }
    (*sec) = NULL;
    return false;
}

void IterRich(parsed_pe* pe, iterRich cb, void* cbd)
{
    list_t* entries = pe->peHeader->rich->Entries;
    list_t* n = entries;
    struct rich_entry* r;

    while (n != NULL)
    {
        r = (struct rich_entry*)n->data;

        if (cb(cbd, r) != 0)
        {
            break;
        }

        n = n->next;
    }
}

void IterRsrc(parsed_pe* pe, iterRsrc cb, void* cbd)
{
    struct parsed_pe_internal* pint = pe->internal;

    list_t* n = pint->rsrcs;
    struct resource* r;

    while (n != NULL)
    {
        r = (struct resource*)n->data;

        if (cb(cbd, r) != 0)
        {
            break;
        }

        n = n->next;
    }
}

/// <summary>
/// A simple utility function to convert a null-terminated char16_t string to char*
/// </summary>
/// <param name="u16_string"></param>
/// <returns> C String in char* format.</returns>
char* convert_u16_to_c_str(const char16_t* u16_string)
{
    // We will build the char* string dynamically
    char* temp_mb_buf = (char*)malloc(sizeof(char) * MB_CUR_MAX);
    size_t current_len = 0;
    size_t buffer_size = 128; // Initial buffer size
    char* c_string = (char*)malloc(sizeof(char) * buffer_size);
    int i = 0;

    if (c_string == NULL)
    {
        perror("malloc failed");
        return NULL;
    }

    mbstate_t state = { 0 }; // Conversion state, must be initialized to zero
    size_t bytes_written;

    for (i = 0; u16_string[i] != u'\0'; ++i)
    {
        //char temp_mb_buf[MB_CUR_MAX]; // Temporary buffer for a single multibyte character

        // Convert the char16_t to a multibyte sequence
        bytes_written = c16rtomb(temp_mb_buf, u16_string[i], &state);

        if (bytes_written == (size_t)-1)
        {
            // Handle conversion error
            perror("c16rtomb failed, invalid character sequence");
            free(c_string);
            return NULL;
        }

        // Check if the current buffer is large enough
        if (current_len + bytes_written >= buffer_size)
        {
            buffer_size = buffer_size * 2;
            char* new_c_string = (char*)realloc(c_string, buffer_size);
            if (new_c_string == NULL)
            {
                free(c_string);
                perror("realloc failed");
                return NULL;
            }
            c_string = new_c_string;
        }

        // Append the converted bytes to the output string
        memcpy(c_string + current_len, temp_mb_buf, bytes_written);
        current_len += bytes_written;
    }

    // Null-terminate the resulting string
    c_string[current_len] = '\0';

    return c_string;
}

bool parse_resource_id(bounded_buffer* data, uint32_t id, char** result)
{
    uint16_t len;
    if (!readWord(data, id, len))
    {
        return false;
    }
    id += 2;

    uint32_t rawSize = len * 2U;
    
    list_t* rawstring_lst = NULL;
    list_t* lst_item = NULL;
    list_t* lst_curr = NULL;
    list_t* lst_end = NULL;

    //u16string rawString;
    //UCharString rawString;
    for (uint32_t i = 0; i < rawSize; i += 2)
    {
        char16_t* c = (char16_t*)malloc(sizeof(char16_t));

        if (!readChar16(data, id + i, c))
        {
            return false;
        }

        if (rawstring_lst == NULL)
        {
            rawstring_lst = list_new();
            rawstring_lst->data = (void*)c;
            rawstring_lst->next = NULL;
            rawstring_lst->prev = NULL;
        }
        else
        {
            lst_curr = rawstring_lst;

            while (lst_curr != NULL)
            {
                lst_end = lst_curr;

                lst_curr = lst_curr->next;
            }

            if (lst_end != NULL)
            {
                list_t* lst_item = list_new();

                lst_item->data = (void*)c;
                lst_item->next = NULL;
                lst_item->prev = lst_end;
                lst_end->next = lst_item;
            }
        }
    }

    size_t rawstring_count = 0;
    size_t rawstring_index;

    lst_curr = rawstring_lst;
    while (lst_curr != NULL)
    {
        rawstring_count++;

        lst_curr = lst_curr->next;
    }

    char16_t* utf16_buffer = (char16_t*)malloc(sizeof(char16_t) * rawstring_count);
    rawstring_index = 0;

    lst_curr = rawstring_lst;
    while (lst_curr != NULL)
    {
        utf16_buffer[rawstring_index] = (char16_t*)lst_curr->data;

        rawstring_index++;
        lst_curr = lst_curr->next;
    }

    (*result) = convert_u16_to_c_str(utf16_buffer);

    return true;
}

bool parse_resource_table(bounded_buffer* sectionData, uint32_t o, uint32_t virtaddr, uint32_t depth, 
    struct resource_dir_entry* dirent, list_t* rsrcs) // std::vector<resource>& rsrcs
{
    struct resource_dir_table rdt;

    if (sectionData == NULL) 
    {
        return false;
    }

    if (!readDword(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, Characteristics)), rdt.Characteristics)) 
    {
        //PE_ERR(PEERR_READ);                                                  
        return false;
    }

    if (!readDword(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, TimeDateStamp)), rdt.TimeDateStamp))
    {
        //PE_ERR(PEERR_READ);                                                  
        return false;
    }

    if (!readWord(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, MajorVersion)), rdt.MajorVersion))
    {
        //PE_ERR(PEERR_READ);                                                        
        return false;
    }

    if (!readWord(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, MinorVersion)), rdt.MinorVersion))
    {
        //PE_ERR(PEERR_READ);                                                        
        return false;
    }

    if (!readWord(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, NameEntries)), rdt.NameEntries))
    {
        //PE_ERR(PEERR_READ);                                                        
        return false;
    }

    if (!readWord(sectionData, o + (uint32_t)(offsetof(struct resource_dir_table, IDEntries)), rdt.IDEntries))
    {
        //PE_ERR(PEERR_READ);                                                        
        return false;
    }

    //READ_DWORD(sectionData, o, rdt, Characteristics);
    //READ_DWORD(sectionData, o, rdt, TimeDateStamp);
    //READ_WORD(sectionData, o, rdt, MajorVersion);
    //READ_WORD(sectionData, o, rdt, MinorVersion);
    //READ_WORD(sectionData, o, rdt, NameEntries);
    //READ_WORD(sectionData, o, rdt, IDEntries);

    o += sizeof(struct resource_dir_table);

    if (rdt.NameEntries == 0u && rdt.IDEntries == 0u)
    {
        return true; // This is not a hard error. It does happen.
    }

    for (uint32_t i = 0;
        i < (uint32_t)(rdt.NameEntries + rdt.IDEntries);
        i++) {
        struct resource_dir_entry* rde = dirent;
        if (dirent == NULL)
        {
            rde = (struct resource_dir_entry*)malloc(sizeof(struct resource_dir_entry));
        }

        if (!readDword(sectionData, o + offsetof(struct resource_dir_entry, ID), rde->ID))
        {
            //PE_ERR(PEERR_READ);
            if (dirent == NULL)
            {
                free(rde);
            }
            return false;
        }

        if (!readDword(sectionData, o + offsetof(struct resource_dir_entry, RVA), rde->RVA))
        {
            //PE_ERR(PEERR_READ);
            if (dirent == NULL)
            {
                free(rde);
            }
            return false;
        }

        o += sizeof(struct resource_dir_entry_sz);

        if (depth == 0) {
            rde->type = rde->ID;
            if (i < rdt.NameEntries) {
                if (!parse_resource_id(
                    sectionData, rde->ID & 0x0FFFFFFF, rde->type_str)) {
                    if (dirent == NULL)
                    {
                        free(rde);
                    }
                    return false;
                }
            }
        }
        else if (depth == 1) {
            rde->name = rde->ID;
            if (i < rdt.NameEntries) {
                if (!parse_resource_id(
                    sectionData, rde->ID & 0x0FFFFFFF, rde->name_str)) {
                    if (dirent == NULL)
                    {
                        free(rde);
                    }
                    return false;
                }
            }
        }
        else if (depth == 2) {
            rde->lang = rde->ID;
            if (i < rdt.NameEntries) {
                if (!parse_resource_id(
                    sectionData, rde->ID & 0x0FFFFFFF, rde->lang_str)) {
                    if (dirent == NULL)
                    {
                        free(rde);
                    }
                    return false;
                }
            }
        }
        else {
            /* .rsrc can accommodate up to 2**31 levels, but Windows only uses 3 by
                * convention. As such, any depth above 3 indicates potentially unchecked
                * recursion. See:
                * https://docs.microsoft.com/en-us/windows/desktop/debug/pe-format#the-rsrc-section
                */

            PE_ERR(PEERR_RESC);
            return false;
        }

        // High bit 0 = RVA to RDT.
        // High bit 1 = RVA to RDE.
        if (rde->RVA & 0x80000000) {
            if (!parse_resource_table(sectionData,
                rde->RVA & 0x0FFFFFFF,
                virtaddr,
                depth + 1,
                rde,
                rsrcs)) {
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }
        }
        else {
            struct resource_dat_entry rdat;

            /*
                * This one is using rde->RVA as an offset.
                *
                * This is because we don't want to set o because we have to keep the
                * original value when we are done parsing this resource data entry.
                * We could store the original o value and reset it when we are done,
                * but meh.
                */

            if (!readDword(sectionData,
                rde->RVA + offsetof(struct resource_dat_entry, RVA),
                rdat.RVA))
            {
                //PE_ERR(PEERR_READ);
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }

            if (!readDword(sectionData,
                rde->RVA + offsetof(struct resource_dat_entry, size),
                rdat.size))
            {
                //PE_ERR(PEERR_READ);
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }

            if (!readDword(sectionData,
                rde->RVA + offsetof(struct resource_dat_entry, codepage),
                rdat.codepage))
            {
                //PE_ERR(PEERR_READ);
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }

            if (!readDword(sectionData,
                rde->RVA + offsetof(struct resource_dat_entry, reserved),
                rdat.reserved))
            {
                //PE_ERR(PEERR_READ);
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }

            struct resource rsrc;
            rsrc.type_str = rde->type_str;
            rsrc.name_str = rde->name_str;
            rsrc.lang_str = rde->lang_str;
            rsrc.type = rde->type;
            rsrc.name = rde->name;
            rsrc.lang = rde->lang;
            rsrc.codepage = rdat.codepage;
            rsrc.RVA = rdat.RVA;
            rsrc.size = rdat.size;

            // The start address is (RVA - section virtual address).
            uint32_t start = rdat.RVA - virtaddr;
            /*
                * Some binaries (particularly packed) will have invalid addresses here.
                * If those happen, return a zero length buffer.
                * If the start is valid, try to get the data and if that fails return
                * a zero length buffer.
                */
            if (start > rdat.RVA)
            {
                rsrc.buf = splitBuffer(sectionData, 0, 0);
            }
            else {
                rsrc.buf = splitBuffer(sectionData, start, start + rdat.size);
                if (rsrc.buf == NULL)
                {
                    rsrc.buf = splitBuffer(sectionData, 0, 0);
                }
            }

            /* If we can't get even a zero length buffer, something is very wrong. */
            if (rsrc.buf == NULL)
            {
                if (dirent == NULL)
                {
                    free(rde);
                }
                return false;
            }

            list_enqueue(rsrcs, (void*)&rsrc);
        }

        if (depth == 0)
        {
            if (rde->type_str != NULL)
            {
                free(rde->type_str);
            }
            rde->type_str = NULL; //rde->type_str.clear();
        }
        else if (depth == 1)
        {
            if (rde->name_str != NULL)
            {
                free(rde->name_str);
            }
            rde->name_str = NULL; //rde->name_str.clear();
        }
        else if (depth == 2)
        {
            if (rde->lang_str != NULL)
            {
                free(rde->lang_str);
            }
            rde->lang_str = NULL; //rde->lang_str.clear();
        }

        if (dirent == NULL)
        {
            free(rde);
        }
    }

    return true;
}

bool getResources(bounded_buffer* b, bounded_buffer* fileBegin, list_t* secs, list_t* rsrcs)
{ //const std::vector<section> secs, std::vector<resource>& rsrcs) {
    //static_cast<void>(fileBegin);

    if (b == NULL)
    {
        return false;
    }
        
    struct section* s;
    list_t* n = secs;

    while (n != NULL)
    {
        s = (struct section*)n->data;

        if (strcmp(s->sectionName, ".rsrc") != 0)
        {
            n = n->next;

            continue;
        }

        if (!parse_resource_table(s->sectionData, 0, s->sec->VirtualAddress, 0, NULL, rsrcs))
        {
            return false;
        }

        break; // Because there should only be one .rsrc
    }

    return true;
}

// Assuming 'section' is defined as a struct
int compare_sections(const void* a, const void* b) {
    const struct section* lhs = (const struct section*)a;
    const struct section* rhs = (const struct section*)b;

    if (lhs->sec->PointerToRawData < rhs->sec->PointerToRawData) return -1;
    if (lhs->sec->PointerToRawData > rhs->sec->PointerToRawData) return 1;
    return 0;
}

bool getSections(bounded_buffer* b, bounded_buffer* fileBegin, struct nt_header_32* nthdr, list_t* secs) // std::vector<section>& secs
{
    if (b == NULL)
    {
        return false;
    }

    // get each of the sections...
    for (uint32_t i = 0; i < nthdr->FileHeader->NumberOfSections; i++)
    {
        struct image_section_header* curSec = (struct image_section_header*)malloc(sizeof(struct image_section_header));

        uint32_t o = i * sizeof(struct image_section_header);
        for (uint32_t k = 0; k < NT_SHORT_NAME_LEN; k++)
        {
            if (!readByte(b, o + k, curSec->Name[k]))
            {
                return false;
            }
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, Misc.VirtualSize)), curSec->Misc.VirtualSize))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, VirtualAddress)), curSec->VirtualAddress))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, SizeOfRawData)), curSec->SizeOfRawData))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, PointerToRawData)), curSec->PointerToRawData))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, PointerToRelocations)), curSec->PointerToRelocations))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, PointerToLinenumbers)), curSec->PointerToLinenumbers))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readWord(b, o + (uint32_t)(offsetof(struct image_section_header, NumberOfRelocations)), curSec->NumberOfRelocations))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readWord(b, o + (uint32_t)(offsetof(struct image_section_header, NumberOfLinenumbers)), curSec->NumberOfLinenumbers))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        if (!readDword(b, o + (uint32_t)(offsetof(struct image_section_header, Characteristics)), curSec->Characteristics))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        // now we have the section header information, so fill in a section object appropriately
        int index = 0;
        struct section* thisSec = (struct section*)malloc(sizeof(struct section));
        thisSec->sectionName = (char*)malloc(sizeof(char) * NT_SHORT_NAME_LEN);

        for (uint32_t charIndex = 0; charIndex < NT_SHORT_NAME_LEN; charIndex++)
        {
            uint8_t c = curSec->Name[charIndex];
            if (c == 0) {
                break;
            }

            uint8_t* ccc = &c;
            char* cc = (char*)&ccc;
            strcat(thisSec->sectionName, cc);
            //thisSec.sectionName.push_back(static_cast<char>(c));
            index++;
        }
        thisSec->sectionName[index + 1] = '\0';

        if (nthdr->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            thisSec->sectionBase = nthdr->OptionalHeader->ImageBase + curSec->VirtualAddress;
        }
        else if (nthdr->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            thisSec->sectionBase = nthdr->OptionalHeader64->ImageBase + curSec->VirtualAddress;
        }
        else
        {
            //PE_ERR(PEERR_MAGIC);
        }

        thisSec->sec = curSec;
        uint32_t lowOff = curSec->PointerToRawData;
        uint32_t highOff = lowOff + curSec->SizeOfRawData;
        thisSec->sectionData = splitBuffer(fileBegin, lowOff, highOff);

        // GH#109: we trusted [lowOff, highOff) to be a range that yields
        // a valid bounded_buffer, despite these being user-controllable.
        // splitBuffer correctly handles this, but we failed to check for
        // the nullptr it returns as a sentinel.
        if (thisSec->sectionData == NULL)
        {
            return false;
        }

        list_enqueue(secs, (void*)thisSec); // secs.push_back(thisSec);
    }

    size_t num_secs = list_count(secs);

    // Assuming 'secs' is a standard C array and 'num_secs' is its length
    qsort(secs, num_secs, sizeof(struct section), &compare_sections);

    return true;
}

bool readOptionalHeader(bounded_buffer* b, struct optional_header_32* header)
{
    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, Magic)), header->Magic))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readByte(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MajorLinkerVersion)), header->MajorLinkerVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readByte(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MinorLinkerVersion)), header->MinorLinkerVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfCode)), header->SizeOfCode))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfInitializedData)), header->SizeOfInitializedData))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfUninitializedData)), header->SizeOfUninitializedData))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, AddressOfEntryPoint)), header->AddressOfEntryPoint))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, BaseOfCode)), header->BaseOfCode))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, BaseOfData)), header->BaseOfData))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, ImageBase)), header->ImageBase))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SectionAlignment)), header->SectionAlignment))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, FileAlignment)), header->FileAlignment))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MajorOperatingSystemVersion)), header->MajorOperatingSystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MinorOperatingSystemVersion)), header->MinorOperatingSystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MajorImageVersion)), header->MajorImageVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MinorImageVersion)), header->MinorImageVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MajorSubsystemVersion)), header->MajorSubsystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, MinorSubsystemVersion)), header->MinorSubsystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, Win32VersionValue)), header->Win32VersionValue))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfImage)), header->SizeOfImage))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfHeaders)), header->SizeOfHeaders))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, CheckSum)), header->CheckSum))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, Subsystem)), header->Subsystem))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_32, DllCharacteristics)), header->DllCharacteristics))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfStackReserve)), header->SizeOfStackReserve))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfStackCommit)), header->SizeOfStackCommit))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfHeapReserve)), header->SizeOfHeapReserve))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, SizeOfHeapCommit)), header->SizeOfHeapCommit))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, LoaderFlags)), header->LoaderFlags))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_32, NumberOfRvaAndSizes)), header->NumberOfRvaAndSizes))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (header->NumberOfRvaAndSizes > NUM_DIR_ENTRIES)
    {
        header->NumberOfRvaAndSizes = NUM_DIR_ENTRIES;
    }

    for (uint32_t i = 0; i < header->NumberOfRvaAndSizes; i++)
    {
        uint32_t c = (i * sizeof(struct data_directory));
        c += offsetof(struct optional_header_32, DataDirectory[0]);
        uint32_t o;

        o = c + offsetof(struct data_directory, VirtualAddress);
        if (!readDword(b, o, header->DataDirectory[i].VirtualAddress))
        {
            return false;
        }

        o = c + offsetof(struct data_directory, Size);
        if (!readDword(b, o, header->DataDirectory[i].Size))
        {
            return false;
        }
    }

    return true;
}

bool readOptionalHeader64(bounded_buffer* b, struct optional_header_64* header)
{
    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, Magic)), header->Magic))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readByte(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MajorLinkerVersion)), header->MajorLinkerVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readByte(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MinorLinkerVersion)), header->MinorLinkerVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfCode)), header->SizeOfCode))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfInitializedData)), header->SizeOfInitializedData))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfUninitializedData)), header->SizeOfUninitializedData))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, AddressOfEntryPoint)), header->AddressOfEntryPoint))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, BaseOfCode)), header->BaseOfCode))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, ImageBase)), header->ImageBase))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SectionAlignment)), header->SectionAlignment))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, FileAlignment)), header->FileAlignment))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MajorOperatingSystemVersion)), header->MajorOperatingSystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MinorOperatingSystemVersion)), header->MinorOperatingSystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MajorImageVersion)), header->MajorImageVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MinorImageVersion)), header->MinorImageVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MajorSubsystemVersion)), header->MajorSubsystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, MinorSubsystemVersion)), header->MinorSubsystemVersion))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, Win32VersionValue)), header->Win32VersionValue))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfImage)), header->SizeOfImage))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfHeaders)), header->SizeOfHeaders))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, CheckSum)), header->CheckSum))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, Subsystem)), header->Subsystem))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct optional_header_64, DllCharacteristics)), header->DllCharacteristics))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfStackReserve)), header->SizeOfStackReserve))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfStackCommit)), header->SizeOfStackCommit))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfHeapReserve)), header->SizeOfHeapReserve))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, SizeOfHeapCommit)), header->SizeOfHeapCommit))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, LoaderFlags)), header->LoaderFlags))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }
    
    if (!readQword(b, 0 + (uint32_t)(offsetof(struct optional_header_64, NumberOfRvaAndSizes)), header->NumberOfRvaAndSizes))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (header->NumberOfRvaAndSizes > NUM_DIR_ENTRIES)
    {
        header->NumberOfRvaAndSizes = NUM_DIR_ENTRIES;
    }

    for (uint32_t i = 0; i < header->NumberOfRvaAndSizes; i++) {
        uint32_t c = (i * sizeof(struct data_directory));
        //c += offsetof(struct optional_header_64, DataDirectory[0]);
        c += offsetof(struct optional_header_64, DataDirectory);
        uint32_t o;

        o = c + offsetof(struct data_directory, VirtualAddress);
        if (!readDword(b, o, header->DataDirectory[i].VirtualAddress))
        {
            return false;
        }

        o = c + offsetof(struct data_directory, Size);
        if (!readDword(b, o, header->DataDirectory[i].Size))
        {
            return false;
        }
    }

    return true;
}

bool readFileHeader(bounded_buffer* b, struct file_header* header)
{
    if (!readWord(b, 0 + (uint32_t)(offsetof(struct file_header, Machine)), header->Machine))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct file_header, NumberOfSections)), header->NumberOfSections))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct file_header, TimeDateStamp)), header->TimeDateStamp))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct file_header, PointerToSymbolTable)), header->PointerToSymbolTable))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(b, 0 + (uint32_t)(offsetof(struct file_header, NumberOfSymbols)), header->NumberOfSymbols))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct file_header, SizeOfOptionalHeader)), header->SizeOfOptionalHeader))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(b, 0 + (uint32_t)(offsetof(struct file_header, Characteristics)), header->Characteristics))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    return true;
}

bool readNtHeader(bounded_buffer* b, struct nt_header_32* header)
{
    if (b == NULL)
    {
        return false;
    }

    uint32_t pe_magic;
    uint32_t curOffset = 0;
    if (!readDword(b, curOffset, pe_magic) || pe_magic != NT_MAGIC)
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    header->Signature = pe_magic;

    bounded_buffer* fhb = splitBuffer(b, offsetof(struct nt_header_32, FileHeader), b->bufLen);

    if (fhb == NULL)
    {
        //PE_ERR(PEERR_MEM);
        return false;
    }

    if (!readFileHeader(fhb, header->FileHeader))
    {
        deleteBuffer(fhb);
        return false;
    }

    if (TEST_MACHINE_CHARACTERISTICS(
        header, IMAGE_FILE_MACHINE_AMD64, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_ARM, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_ARM64, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_ARMNT, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_I386, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_M32R, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_POWERPC, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_R4000, IMAGE_FILE_BYTES_REVERSED_HI) ||
        TEST_MACHINE_CHARACTERISTICS(
            header, IMAGE_FILE_MACHINE_WCEMIPSV2, IMAGE_FILE_BYTES_REVERSED_HI)) {
        b->swapBytes = true;
    }

    /*
        * The buffer is split using the OptionalHeader offset, even if it turns
        * out to be a PE32+. The start of the buffer is at the same spot in the
        * buffer regardless.
        */
    bounded_buffer* ohb = splitBuffer(b, offsetof(struct nt_header_32, OptionalHeader), b->bufLen);

    if (ohb == NULL)
    {
        deleteBuffer(fhb);
        //PE_ERR(PEERR_MEM);
        return false;
    }

    /*
        * Read the Magic to determine if it is 32 or 64.
        */
    if (!readWord(ohb, 0, header->OptionalMagic))
    {
        //PE_ERR(PEERR_READ);
        if (ohb != NULL)
        {
            deleteBuffer(ohb);
        }
        deleteBuffer(fhb);
        return false;
    }
    if (header->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        if (!readOptionalHeader(ohb, header->OptionalHeader))
        {
            deleteBuffer(ohb);
            deleteBuffer(fhb);
            return false;
        }
    }
    else if (header->OptionalMagic == NT_OPTIONAL_64_MAGIC)
    {
        if (!readOptionalHeader64(ohb, header->OptionalHeader64))
        {
            deleteBuffer(ohb);
            deleteBuffer(fhb);
            return false;
        }
    }
    else
    {
        //PE_ERR(PEERR_MAGIC);
        deleteBuffer(ohb);
        deleteBuffer(fhb);
        return false;
    }

    deleteBuffer(ohb);
    deleteBuffer(fhb);

    return true;
}

// zero extends its first argument to 32 bits and then performs a rotate left
// operation equal to the second arguments value of the first argument’s bits
static inline uint32_t rol(uint32_t val, uint32_t num)
{
    //assert(num < 32);
    // Disable MSVC warning for unary minus operator applied to unsigned type
#if defined(_MSC_VER) || defined(_MSC_FULL_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
// https://blog.regehr.org/archives/1063
    return (val << num) | (val >> (-num & 31));
#if defined(_MSC_VER) || defined(_MSC_FULL_VER)
#pragma warning(pop)
#endif
}

uint32_t calculateRichChecksum(const bounded_buffer* b, pe_header* p)
{
    // First, calculate the sum of the DOS header bytes each rotated left the
    // number of times their position relative to the start of the DOS header e.g.
    // second byte is rotated left 2x using rol operation
    uint32_t checksum = 0;

    for (uint8_t i = 0; i < RICH_OFFSET; i++) {

        // skip over dos e_lfanew field at offset 0x3C
        if (i >= 0x3C && i <= 0x3F) {
            continue;
        }
        checksum += rol(b->buf[i], i & 0x1F);
    }

    // Next, take summation of each Rich header entry by combining its ProductId
    // and BuildNumber into a single 32 bit number and rotating by its count.

    list_t* entries = p->rich->Entries;
    list_t* n = entries;
    struct rich_entry* r;

    while (n != NULL)
    {
        r = (struct rich_entry*)n->data;

        uint32_t num = (uint32_t)((r->ProductId << 16) | r->BuildNumber);

        checksum += rol(num, r->Count & 0x1F);
        n = n->next;
    }

    checksum += RICH_OFFSET;

    return checksum;
}

bool readRichHeader(bounded_buffer* rich_buf, uint32_t key, struct rich_header* rich_hdr)
{
    if (rich_buf == NULL)
    {
        return false;
    }

    uint32_t encrypted_dword;
    uint32_t decrypted_dword;

    // Confirm DanS signature exists first.
    // The first decrypted DWORD value of the rich header
    // at offset 0 should be 0x536e6144 aka the "DanS" signature
    if (!readDword(rich_buf, 0, &encrypted_dword))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    decrypted_dword = encrypted_dword ^ key;

    if (decrypted_dword == RICH_MAGIC_START)
    {
        // DanS magic found
        rich_hdr->isPresent = true;
        rich_hdr->StartSignature = decrypted_dword;
    }
    else
    {
        // DanS magic not found
        rich_hdr->isPresent = false;
        return false;
    }

    // Iterate over the remaining entries.
    // Start from buffer offset 16 because after "DanS" there
    // are three DWORDs of zero padding that can be skipped over.
    // a DWORD is 4 bytes. Loop is incrementing 8 bytes, however
    // we are reading two DWORDS at a time, which is the size
    // of one rich header entry.
    for (uint32_t i = 16; i < rich_buf->bufLen - 8; i += 8)
    {
        struct rich_entry* entry = (struct rich_entry*)malloc(sizeof(struct rich_entry));

        // Read first DWORD of entry and decrypt it
        if (!readDword(rich_buf, i, encrypted_dword))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }
        decrypted_dword = encrypted_dword ^ key;
        // The high WORD of the first DWORD is the Product ID
        entry->ProductId = (decrypted_dword & 0xFFFF0000) >> 16;
        // The low WORD of the first DWORD is the Build Number
        entry->BuildNumber = (decrypted_dword & 0xFFFF);

        // The second DWORD represents the use count
        if (!readDword(rich_buf, i + 4, encrypted_dword))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }
        decrypted_dword = encrypted_dword ^ key;
        // The full 32-bit DWORD is the count
        entry->Count = decrypted_dword;

        // Preserve the individual entry
        list_enqueue(rich_hdr->Entries, (void*)entry);
    }

    // Preserve the end signature aka "Rich" magic
    if (!readDword(rich_buf, rich_buf->bufLen - 4, rich_hdr->EndSignature))
    {
        //PE_ERR(PEERR_READ);
        return false;
    };
    if (rich_hdr->EndSignature != RICH_MAGIC_END)
    {
        //PE_ERR(PEERR_MAGIC);
        return false;
    }

    // Preserve the decryption key
    rich_hdr->DecryptionKey = key;

    return true;
}

bool readDosHeader(bounded_buffer* file, struct dos_header* dos_hdr)
{
    if (file == NULL)
    {
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_magic)), dos_hdr->e_magic))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_cblp)), dos_hdr->e_cblp))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_cp)), dos_hdr->e_cp))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_crlc)), dos_hdr->e_crlc))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_cparhdr)), dos_hdr->e_cparhdr))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_minalloc)), dos_hdr->e_minalloc))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_maxalloc)), dos_hdr->e_maxalloc))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_ss)), dos_hdr->e_ss))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_sp)), dos_hdr->e_sp))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_csum)), dos_hdr->e_csum))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_ip)), dos_hdr->e_ip))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_cs)), dos_hdr->e_cs))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_lfarlc)), dos_hdr->e_lfarlc))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_ovno)), dos_hdr->e_ovno))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res[0])), dos_hdr->e_res[0]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res[1])), dos_hdr->e_res[1]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res[2])), dos_hdr->e_res[2]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res[3])), dos_hdr->e_res[3]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_oemid)), dos_hdr->e_oemid))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_oeminfo)), dos_hdr->e_oeminfo))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[0])), dos_hdr->e_res2[0]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[1])), dos_hdr->e_res2[1]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[2])), dos_hdr->e_res2[2]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[3])), dos_hdr->e_res2[3]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[4])), dos_hdr->e_res2[4]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[5])), dos_hdr->e_res2[5]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[6])), dos_hdr->e_res2[6]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[7])), dos_hdr->e_res2[7]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[8])), dos_hdr->e_res2[8]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_res2[9])), dos_hdr->e_res2[9]))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readWord(file, 0 + (uint32_t)(offsetof(struct dos_header, e_magic)), dos_hdr->e_magic))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    if (!readDword(file, 0 + (uint32_t)(offsetof(struct dos_header, e_lfanew)), dos_hdr->e_lfanew))
    {
        //PE_ERR(PEERR_READ);
        return false;
    }

    return true;
}

bool getHeader(bounded_buffer* file, pe_header* p, bounded_buffer* rem) 
{
    if (file == NULL)
    {
        return false;
    }

    // read the DOS header
    readDosHeader(file, p->dos);

    if (p->dos->e_magic != MZ_MAGIC)
    {
        //PE_ERR(PEERR_MAGIC);
        return false;
    }

    // get the offset to the NT headers
    uint32_t offset = p->dos->e_lfanew;
    uint32_t curOffset = offset;

    // read rich header
    uint32_t dword;
    uint32_t rich_end_signature_offset = 0;
    uint32_t xor_key;
    bool found_rich = false;

    // Start reading from RICH_OFFSET (0x80), a known Rich header offset.
    // Note: 0x80 is based on anecdotal evidence.
    //
    // Iterate over the DWORDs, hence why i increments 4 bytes at a time.
    for (uint32_t i = RICH_OFFSET; i < offset; i += 4)
    {
        if (!readDword(file, i, dword))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        // Found the trailing Rich signature
        if (dword == RICH_MAGIC_END)
        {
            found_rich = true;
            rich_end_signature_offset = i;
            break;
        }
    }

    if (found_rich)
    {
        // Get the XOR decryption key.  It is the DWORD immediately
        // after the Rich signature.
        if (!readDword(file, rich_end_signature_offset + 4, xor_key))
        {
            //PE_ERR(PEERR_READ);
            return false;
        }

        // Split the Rich header out into its own buffer
        bounded_buffer* richBuf = splitBuffer(file, 0x80, rich_end_signature_offset + 4);
        if (richBuf == NULL)
        {
            return false;
        }

        readRichHeader(richBuf, xor_key, p->rich);
        if (richBuf != NULL)
        {
            deleteBuffer(richBuf);
        }

        // Split the DOS header into a separate buffer which
        // starts at offset 0 and has length 0x80
        bounded_buffer* dosBuf = splitBuffer(file, 0, RICH_OFFSET);
        if (dosBuf == NULL)
        {
            return false;
        }
        // Calculate checksum
        p->rich->Checksum = calculateRichChecksum(dosBuf, p);
        if (p->rich->Checksum == p->rich->DecryptionKey)
        {
            p->rich->isValid = true;
        }
        else
        {
            p->rich->isValid = false;
        }
        if (dosBuf != NULL)
        {
            deleteBuffer(dosBuf);
        }

        // Rich header not present
    }
    else
    {
        p->rich->isPresent = false;
    }

    // now, we can read out the fields of the NT headers
    bounded_buffer* ntBuf = splitBuffer(file, curOffset, file->bufLen);

    if (!readNtHeader(ntBuf, p->nt))
    {
        // err is set by readNtHeader
        if (ntBuf != NULL)
        {
            deleteBuffer(ntBuf);
        }
        return false;
    }

    /*
        * Need to determine if this is a PE32 or PE32+ binary and use the
        # correct size.
        */
    uint32_t rem_size;
    if (p->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        // signature + file_header + optional_header_32
        rem_size = sizeof(uint32_t) + sizeof(struct file_header) + sizeof(struct optional_header_32);
    }
    else if (p->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
    {
        // signature + file_header + optional_header_64
        rem_size = sizeof(uint32_t) + sizeof(struct file_header) + sizeof(struct optional_header_64);
    }
    else
    {
        //PE_ERR(PEERR_MAGIC);
        deleteBuffer(ntBuf);
        return false;
    }

    // update 'rem' to point to the space after the header
    rem = splitBuffer(ntBuf, rem_size, ntBuf->bufLen);
    deleteBuffer(ntBuf);

    return true;
}

bool getExports(parsed_pe* p)
{
    struct data_directory exportDir;
    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        exportDir = p->peHeader->nt->OptionalHeader->DataDirectory[DIR_EXPORT];
    }
    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC) {
        exportDir = p->peHeader->nt->OptionalHeader64->DataDirectory[DIR_EXPORT];
    }
    else
    {
        return false;
    }

    if (exportDir.Size != 0)
    {
        struct section* s = (struct section*)malloc(sizeof(struct section));
        VA addr;
        if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            addr = exportDir.VirtualAddress + p->peHeader->nt->OptionalHeader->ImageBase;
        }
        else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            addr = exportDir.VirtualAddress + p->peHeader->nt->OptionalHeader64->ImageBase;
        }
        else {
            return false;
        }

        if (!getSecForVA(p->internal->secs, addr, &s))
        {
            return false;
        }

        uint32_t rvaofft = (uint32_t)(addr - s->sectionBase);

        // get the name of this module
        uint32_t nameRva;
        if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, NameRVA), nameRva))
        {
            return false;
        }

        VA nameVA;
        if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            nameVA = nameRva + p->peHeader->nt->OptionalHeader->ImageBase;
        }
        else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            nameVA = nameRva + p->peHeader->nt->OptionalHeader64->ImageBase;
        }
        else {
            return false;
        }

        struct section* nameSec = (struct section*)malloc(sizeof(struct section));
        if (!getSecForVA(p->internal->secs, nameVA, nameSec))
        {
            return false;
        }

        uint32_t nameOff = (uint32_t)(nameVA - nameSec->sectionBase);
        char* modName;
        if (!readCString(nameSec->sectionData, nameOff, modName))
        {
            return false;
        }

        // now, get all the named export symbols
        uint32_t numNames;
        if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, NumberOfNamePointers), &numNames))
        {
            return false;
        }

        if (numNames > 0)
        {
            // get the names section
            uint32_t namesRVA;
            if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, NamePointerRVA), &namesRVA))
            {
                return false;
            }

            VA namesVA;
            if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
            {
                namesVA = namesRVA + p->peHeader->nt->OptionalHeader->ImageBase;
            }
            else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
            {
                namesVA = namesRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
            }
            else
            {
                return false;
            }

            struct section* namesSec = (struct section*)malloc(sizeof(struct section));
            if (!getSecForVA(p->internal->secs, namesVA, namesSec))
            {
                return false;
            }

            uint32_t namesOff = (uint32_t)(namesVA - namesSec->sectionBase);

            // get the EAT section
            uint32_t eatRVA;
            if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, ExportAddressTableRVA), &eatRVA))
            {
                return false;
            }

            VA eatVA;
            if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
            {
                eatVA = eatRVA + p->peHeader->nt->OptionalHeader->ImageBase;
            }
            else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
            {
                eatVA = eatRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
            }
            else
            {
                return false;
            }

            struct section* eatSec = (struct section*)malloc(sizeof(struct section));
            if (!getSecForVA(p->internal->secs, eatVA, eatSec))
            {
                return false;
            }

            uint32_t eatOff = (uint32_t)(eatVA - eatSec->sectionBase);

            // get the ordinal base
            uint32_t ordinalBase;
            if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, OrdinalBase), &ordinalBase))
            {
                return false;
            }

            // get the ordinal table
            uint32_t ordinalTableRVA;
            if (!readDword(s->sectionData, rvaofft + offsetof(struct export_dir_table, OrdinalTableRVA), ordinalTableRVA))
            {
                return false;
            }

            VA ordinalTableVA;
            if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
            {
                ordinalTableVA = ordinalTableRVA + p->peHeader->nt->OptionalHeader->ImageBase;
            }
            else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
            {
                ordinalTableVA = ordinalTableRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
            }
            else
            {
                return false;
            }

            struct section* ordinalTableSec = (struct section*)malloc(sizeof(struct section));
            if (!getSecForVA(p->internal->secs, ordinalTableVA, ordinalTableSec))
            {
                return false;
            }

            uint32_t ordinalOff = (uint32_t)(ordinalTableVA - ordinalTableSec->sectionBase);

            for (uint32_t i = 0; i < numNames; i++)
            {
                uint32_t curNameRVA;
                if (!readDword(namesSec->sectionData, namesOff + (i * sizeof(uint32_t)), &curNameRVA))
                {
                    return false;
                }

                VA curNameVA;
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    curNameVA = curNameRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    curNameVA = curNameRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }

                struct section* curNameSec = (struct section*)malloc(sizeof(struct section));

                if (!getSecForVA(p->internal->secs, curNameVA, curNameSec))
                {
                    return false;
                }

                uint32_t curNameOff = (uint32_t)(curNameVA - curNameSec->sectionBase);
                char* symName = (char*)malloc(sizeof(char) * 200);
                uint8_t d;
                int index2 = 0;

                do {
                    if (!readByte(curNameSec->sectionData, curNameOff, &d))
                    {
                        return false;
                    }

                    if (d == 0)
                    {
                        break;
                    }

                    uint8_t* ccc = &d;
                    char* cc = (char*)&ccc;
                    strcat(symName, cc);

                    //symName.push_back(static_cast<char>(d));
                    curNameOff++;
                    index2++;
                } while (true);
                symName[index2 + 1] = '\0';

                // now, for this i, look it up in the ExportOrdinalTable
                uint16_t ordinal;
                if (!readWord(ordinalTableSec->sectionData, ordinalOff + (i * sizeof(uint16_t)), ordinal))
                {
                    return false;
                }

                //::uint32_t  eatIdx = ordinal - ordinalBase;
                uint32_t eatIdx = (ordinal * sizeof(uint32_t));

                uint32_t symRVA;
                if (!readDword(eatSec->sectionData, eatOff + eatIdx, symRVA))
                {
                    return false;
                }

                bool isForwarded = ((symRVA >= exportDir.VirtualAddress) && (symRVA < exportDir.VirtualAddress + exportDir.Size));

                VA symVA;
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    symVA = symRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    symVA = symRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }

                struct exportent* a = (struct exportent*)malloc(sizeof(struct exportent));
                a->ordinal = ordinal;
                a->symbolName = symName;
                a->moduleName = modName;

                if (!isForwarded)
                {
                    a->addr = symVA;

                    if (a->forwardName != NULL)
                    {
                        free(a->forwardName);
                    }
                    a->forwardName = NULL; // a->forwardName.clear();
                }
                else
                {
                    struct section* fwdSec = (struct section*)malloc(sizeof(struct section));
                    if (!getSecForVA(p->internal->secs, symVA, &fwdSec))
                    {
                        return false;
                    }
                    auto fwdOff = (uint32_t)(symVA - fwdSec->sectionBase);

                    a->addr = 0;
                    if (!readCString(fwdSec->sectionData, fwdOff, &a->forwardName))
                    {
                        return false;
                    }
                }

                list_enqueue(p->internal->exports, (void*)a);
            }
        }
    }

    return true;
}

bool getRelocations(parsed_pe* p)
{
    struct data_directory relocDir;
    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        relocDir = p->peHeader->nt->OptionalHeader->DataDirectory[DIR_BASERELOC];
    }
    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
    {
        relocDir = p->peHeader->nt->OptionalHeader64->DataDirectory[DIR_BASERELOC];
    }
    else
    {
        return false;
    }

    if (relocDir.Size != 0)
    {
        struct section* d = (struct section*)malloc(sizeof(struct section));
        VA vaAddr;
        if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            vaAddr = relocDir.VirtualAddress + p->peHeader->nt->OptionalHeader->ImageBase;
        }
        else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            vaAddr = relocDir.VirtualAddress + p->peHeader->nt->OptionalHeader64->ImageBase;
        }
        else
        {
            return false;
        }

        if (!getSecForVA(p->internal->secs, vaAddr, &d))
        {
            return false;
        }

        uint32_t rvaofft = (uint32_t)(vaAddr - d->sectionBase);

        while (rvaofft < relocDir.Size)
        {
            uint32_t pageRva;
            uint32_t blockSize;

            if (!readDword(d->sectionData, rvaofft + offsetof(struct reloc_block, PageRVA), pageRva))
            {
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + offsetof(struct reloc_block, BlockSize), blockSize))
            {
                return false;
            }

            // BlockSize - The total number of bytes in the base relocation block,
            // including the Page RVA and Block Size fields and the Type/Offset fields
            // that follow. Therefore we should subtract 8 bytes from BlockSize to
            // exclude the Page RVA and Block Size fields.
            uint32_t entryCount = (blockSize - 8) / sizeof(uint16_t);

            // Skip the Page RVA and Block Size fields
            rvaofft += sizeof(struct reloc_block);

            // Iterate over all of the block Type/Offset entries
            while (entryCount != 0)
            {
                uint16_t entry;
                uint8_t type;
                uint16_t offset;

                if (!readWord(d->sectionData, rvaofft, &entry))
                {
                    return false;
                }

                // Mask out the type and assign
                type = entry >> 12;
                // Mask out the offset and assign
                offset = entry & (uint16_t)(~0xf000);

                // Produce the VA of the relocation
                VA relocVA;
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    relocVA = pageRva + offset + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    relocVA = pageRva + offset + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }

                // Store in our list
                struct reloc* r = (struct reloc*)malloc(sizeof(struct reloc));
                r->shiftedAddr = relocVA;
                r->type = (enum reloc_type)(type);
                
                list_enqueue(p->internal->relocs, (void*)r); // p->internal->relocs.push_back(r);

                entryCount--;
                rvaofft += sizeof(uint16_t);
            }
        }
    }

    return true;
}

bool getDebugDir(parsed_pe* p)
{
    struct data_directory debugDir;
    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        debugDir = p->peHeader->nt->OptionalHeader->DataDirectory[DIR_DEBUG];
    }
    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
    {
        debugDir = p->peHeader->nt->OptionalHeader64->DataDirectory[DIR_DEBUG];
    }
    else
    {
        return false;
    }

    if (debugDir.Size != 0) {
        struct section* d = (struct section*)malloc(sizeof(struct section));
        VA vaAddr;
        if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            vaAddr = debugDir.VirtualAddress + p->peHeader->nt->OptionalHeader->ImageBase;
        }
        else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            vaAddr = debugDir.VirtualAddress + p->peHeader->nt->OptionalHeader64->ImageBase;
        }
        else
        {
            return false;
        }

        uint32_t numOfDebugEnts = debugDir.Size / sizeof(struct debug_dir_entry);

        //
        // this will return the rdata section, where the debug directories are
        //
        if (!getSecForVA(p->internal->secs, vaAddr, &d))
        {
            return false;
        }

        //
        // get debug directory from this section
        //
        uint32_t rvaofft = (uint32_t)(vaAddr - d->sectionBase);

        //struct debug_dir_entry emptyEnt;
        //memset(&emptyEnt, 0, sizeof(struct debug_dir_entry));

        for (uint32_t i = 0; i < numOfDebugEnts; i++)
        {
            struct debug_dir_entry* curEnt = (struct debug_dir_entry*)malloc(sizeof(struct debug_dir_entry));
            memset(curEnt, 0, sizeof(struct debug_dir_entry));

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, Characteristics)), curEnt->Characteristics))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, TimeStamp)), curEnt->TimeStamp))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readWord(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, MajorVersion)), curEnt->MajorVersion))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }
            
            if (!readWord(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, MinorVersion)), curEnt->MinorVersion))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, Type)), curEnt->Type))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, SizeOfData)), curEnt->SizeOfData))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, AddressOfRawData)), curEnt->AddressOfRawData))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(d->sectionData, rvaofft + (uint32_t)(offsetof(struct debug_dir_entry, PointerToRawData)), curEnt->PointerToRawData))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            // are all the fields in curEnt null? then we break
            if (curEnt->SizeOfData == 0 && curEnt->AddressOfRawData == 0 && curEnt->PointerToRawData == 0)
            {
                break;
            }

            //
            // Get the address of the data
            //
            VA rawData;
            if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
            {
                rawData = curEnt->AddressOfRawData + p->peHeader->nt->OptionalHeader->ImageBase;
            }
            else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
            {
                rawData = curEnt->AddressOfRawData + p->peHeader->nt->OptionalHeader64->ImageBase;
            }
            else
            {
                // Unrecognized optional header type. We can't process debug entries.
                // Debug entries themselves are optional, so skip them.
                break;
            }

            //
            // Get the section for the data
            //
            struct section* dataSec = (struct section*)malloc(sizeof(struct section));
            if (!getSecForVA(p->internal->secs, rawData, &dataSec))
            {
                // The debug entry points to non-existing data. This means it is
                // malformed. Skip it and the rest. They are not necessary for parsing
                // the binary, and binaries do have malformed debug entries sometimes.
                break;
            }

            struct debugent* ent = (struct debugent*)malloc(sizeof(struct debugent));

            auto dataofft = (uint32_t)(rawData - dataSec->sectionBase);
            if (dataofft + curEnt->SizeOfData > dataSec->sectionData->bufLen)
            {
                // The debug entry data stretches outside the containing section. It is
                // malformed. Skip it and the rest, similar to the above.
                break;
            }
            ent->type = curEnt->Type;
            ent->data = makeBufferFromPointer((uint8_t*)(dataSec->sectionData->buf + dataofft), curEnt->SizeOfData);

            list_enqueue(p->internal->debugdirs, (void*)ent); // p->internal->debugdirs.push_back(ent);

            rvaofft += sizeof(struct debug_dir_entry);
        }
    }

    return true;
}

bool getImports(parsed_pe* p)
{
    struct data_directory importDir;
    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
    {
        importDir = p->peHeader->nt->OptionalHeader->DataDirectory[DIR_IMPORT];
    }
    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
    {
        importDir = p->peHeader->nt->OptionalHeader64->DataDirectory[DIR_IMPORT];
    }
    else
    {
        return false;
    }

    if (importDir.Size != 0)
    {
        // get section for the RVA in importDir
        struct section* c = (struct section*)malloc(sizeof(struct section));
        VA addr;
        if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
        {
            addr = importDir.VirtualAddress + p->peHeader->nt->OptionalHeader->ImageBase;
        }
        else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
        {
            addr = importDir.VirtualAddress + p->peHeader->nt->OptionalHeader64->ImageBase;
        }
        else
        {
            return false;
        }

        if (!getSecForVA(p->internal->secs, addr, &c))
        {
            return false;
        }

        // get import directory from this section
        uint32_t offt = (uint32_t)(addr - c->sectionBase);

        do {
            // read each directory entry out
            struct import_dir_entry* curEnt = (struct import_dir_entry*)malloc(sizeof(struct import_dir_entry));
            memset(curEnt, 0, sizeof(struct import_dir_entry));

            if (!readDword(c->sectionData, offt + (uint32_t)(offsetof(struct import_dir_entry, LookupTableRVA)), curEnt->LookupTableRVA))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(c->sectionData, offt + (uint32_t)(offsetof(struct import_dir_entry, TimeStamp)), curEnt->TimeStamp))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(c->sectionData, offt + (uint32_t)(offsetof(struct import_dir_entry, ForwarderChain)), curEnt->ForwarderChain))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(c->sectionData, offt + (uint32_t)(offsetof(struct import_dir_entry, NameRVA)), curEnt->NameRVA))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            if (!readDword(c->sectionData, offt + (uint32_t)(offsetof(struct import_dir_entry, AddressRVA)), curEnt->AddressRVA))
            {
                //PE_ERR(PEERR_READ);
                return false;
            }

            // are all the fields in curEnt null? then we break
            if (curEnt->LookupTableRVA == 0 && curEnt->NameRVA == 0 && curEnt->AddressRVA == 0)
            {
                break;
            }

            // then, try and get the name of this particular module...
            VA name;
            if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
            {
                name = curEnt->NameRVA + p->peHeader->nt->OptionalHeader->ImageBase;
            }
            else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
            {
                name = curEnt->NameRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
            }
            else
            {
                return false;
            }

            struct section* nameSec = (struct section*)malloc(sizeof(struct section));
            if (!getSecForVA(p->internal->secs, name, &nameSec))
            {
                return false;
            }

            uint32_t nameOff = (uint32_t)(name - nameSec->sectionBase);

            char* modName = (char*)malloc(sizeof(char) * 200);
            if (!readCString(nameSec->sectionData, nameOff, modName))
            {
                return false;
            }

            // convert modName string to uppercase:
            for (int i = 0; i < strlen(modName); i++)
            {
                modName[i] = (char)toupper((unsigned char)modName[i]);
            }

            // then, try and get all of the sub-symbols
            VA lookupVA = 0;
            if (curEnt->LookupTableRVA != 0)
            {
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    lookupVA = curEnt->LookupTableRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    lookupVA = curEnt->LookupTableRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }
            }
            else if (curEnt->AddressRVA != 0)
            {
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    lookupVA = curEnt->AddressRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    lookupVA = curEnt->AddressRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }
            }

            struct section* lookupSec = (struct section*)malloc(sizeof(struct section));
            if (lookupVA == 0 || !getSecForVA(p->internal->secs, lookupVA, &lookupSec))
            {
                return false;
            }

            uint32_t lookupOff = (uint32_t)(lookupVA - lookupSec->sectionBase);
            uint32_t offInTable = 0;
            do {
                VA valVA = 0;
                uint8_t ord = 0;
                uint16_t oval = 0;
                uint32_t val32 = 0;
                uint64_t val64 = 0;
                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    if (!readDword(lookupSec->sectionData, lookupOff, &val32))
                    {
                        return false;
                    }
                    if (val32 == 0)
                    {
                        break;
                    }
                    ord = (val32 >> 31);
                    oval = (val32 & ~0xFFFF0000);
                    valVA = val32 + p->peHeader->nt->OptionalHeader->ImageBase;
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    if (!readQword(lookupSec->sectionData, lookupOff, val64))
                    {
                        return false;
                    }
                    if (val64 == 0)
                    {
                        break;
                    }
                    ord = (val64 >> 63);
                    oval = (val64 & ~0xFFFF0000);
                    valVA = val64 + p->peHeader->nt->OptionalHeader64->ImageBase;
                }
                else
                {
                    return false;
                }

                if (ord == 0)
                {
                    // import by name
                    char* symName = (char*)malloc(sizeof(char) * 200);
                    struct section* symNameSec = (struct section*)malloc(sizeof(struct section));

                    if (!getSecForVA(p->internal->secs, valVA, &symNameSec))
                    {
                        return false;
                    }

                    int index = 0;
                    uint32_t nameOffset = (uint32_t)(valVA - symNameSec->sectionBase) + sizeof(uint16_t);
                    do {
                        uint8_t chr;
                        if (!readByte(symNameSec->sectionData, nameOffset, &chr))
                        {
                            return false;
                        }

                        if (chr == 0)
                        {
                            break;
                        }

                        uint8_t* ccc = &c;
                        char* cc = (char*)&ccc;
                        strcat(symName, cc);
                        //symName.push_back(static_cast<char>(chr));

                        nameOffset++;
                        index++;
                    } while (true);

                    symName[index + 1] = '\0';

                    // okay now we know the pair... add it
                    struct importent* ent = (struct importent*)malloc(sizeof(struct importent));

                    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                    {
                        ent->addr = offInTable + curEnt->AddressRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                    }
                    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                    {
                        ent->addr = offInTable + curEnt->AddressRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                    }
                    else
                    {
                        return false;
                    }

                    ent->symbolName = symName;
                    ent->moduleName = modName;

                    list_enqueue(p->internal->imports, (void*)ent);
                }
                else
                {
                    //char* symName = "ORDINAL_" + modName + "_" + to_string<std::uint32_t>(oval, std::dec);

                    char* ordinal = create_heap_string("ORDINAL_");
                    char* separator = create_heap_string("_");

                    char oval_str_buffer[6];

                    // Use PRIu16 for the correct portable format specifier for uint16_t
                    snprintf(oval_str_buffer, sizeof(oval_str_buffer), "%" PRIu16, oval);

                    char* symName = (char*)malloc(sizeof(char) * 200);

                    strcat(symName, ordinal);
                    strcat(symName, modName);
                    strcat(symName, separator);
                    strcat(symName, oval_str_buffer);

                    struct importent* ent = (struct importent*)malloc(sizeof(struct importent));

                    if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                    {
                        ent->addr = offInTable + curEnt->AddressRVA + p->peHeader->nt->OptionalHeader->ImageBase;
                    }
                    else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                    {
                        ent->addr = offInTable + curEnt->AddressRVA + p->peHeader->nt->OptionalHeader64->ImageBase;
                    }
                    else
                    {
                        return false;
                    }

                    ent->symbolName = symName;
                    ent->moduleName = modName;

                    list_enqueue(p->internal->imports, (void*)ent);
                }

                if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
                {
                    lookupOff += sizeof(uint32_t);
                    offInTable += sizeof(uint32_t);
                }
                else if (p->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
                {
                    lookupOff += sizeof(uint64_t);
                    offInTable += sizeof(uint64_t);
                }
                else
                {
                    return false;
                }
            } while (true);

            offt += sizeof(struct import_dir_entry);
        } while (true);
    }

    return true;
}

bool getSymbolTable(parsed_pe* p)
{
    if (p->peHeader->nt->FileHeader->PointerToSymbolTable == 0)
    {
        return true;
    }

    uint32_t strTableOffset = p->peHeader->nt->FileHeader->PointerToSymbolTable 
        + (p->peHeader->nt->FileHeader->NumberOfSymbols * SYMTAB_RECORD_LEN);

    uint32_t offset = p->peHeader->nt->FileHeader->PointerToSymbolTable;

    for (uint32_t i = 0; i < p->peHeader->nt->FileHeader->NumberOfSymbols; i++)
    {
        struct symbol* sym;
        int index = 0;
        uint8_t ch;

        sym = (struct symbol*)malloc(sizeof(struct symbol));

        // Read name
        if (!readQword(p->fileBuffer, offset, &sym->name->data))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }

        sym->strName = (char*)malloc(sizeof(char) * 200);

        if (sym->name->zeroes == 0) {
            // The symbol name is greater than 8 bytes so it is stored in the string
            // table. In this case instead of name, an offset of the string in the
            // string table is provided.

            index = 0;

            uint32_t strOffset = strTableOffset + SYMBOL_NAME_OFFSET(sym->name);
            
            while(true)
            {
                if (!readByte(p->fileBuffer, strOffset, &ch))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }
                if (ch == 0u)
                {
                    break;
                }

                char cc = (char)ch;
                char* cp = &cc;
                strcat(sym->strName, cp);

                //sym.strName.push_back(static_cast<char>(ch));

                index++;
                strOffset += sizeof(uint8_t);
            }

            sym->strName[index + 1] = '\0';
        }
        else
        {
            index = 0;

            for (uint8_t n = 0; n < NT_SHORT_NAME_LEN && sym->name->shortName[n] != 0; n++)
            {
                ch = sym->name->shortName[n];

                char cc = (char)ch;
                char* cp = &cc;
                strcat(sym->strName, cp);
                
                //sym.strName.push_back(static_cast<char>(sym.name.shortName[n]));

                index++;
            }

            sym->strName[index + 1] = '\0';
        }

        offset += sizeof(uint64_t);

        // Read value
        if (!readDword(p->fileBuffer, offset, &sym->value))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }

        offset += sizeof(uint32_t);

        // Read section number
        uint16_t secNum;
        if (!readWord(p->fileBuffer, offset, &secNum))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }
        sym->sectionNumber = (int16_t)(secNum);

        offset += sizeof(uint16_t);

        // Read type
        if (!readWord(p->fileBuffer, offset, &sym->type))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }

        offset += sizeof(uint16_t);

        // Read storage class
        if (!readByte(p->fileBuffer, offset, &sym->storageClass))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }

        offset += sizeof(uint8_t);

        // Read number of auxiliary symbols
        if (!readByte(p->fileBuffer, offset, &sym->numberOfAuxSymbols))
        {
            //PE_ERR(PEERR_MAGIC);
            return false;
        }

        // Set offset to next symbol
        offset += sizeof(uint8_t);

        // Save the symbol
        list_enqueue(p->internal->symbols, (void*)sym);

        if (sym->numberOfAuxSymbols == 0)
        {
            continue;
        }

        // Read auxiliary symbol records
        auto nextSymbolOffset = offset + ((uint32_t)(sym->numberOfAuxSymbols) * (uint32_t)(SYMTAB_RECORD_LEN));

        i += sym->numberOfAuxSymbols;

        if (sym->storageClass == IMAGE_SYM_CLASS_EXTERNAL && SYMBOL_TYPE_HI(sym) == 0x20 && sym->sectionNumber > 0)
        {
            // Auxiliary Format 1: Function Definitions

            for (uint8_t n = 0; n < sym->numberOfAuxSymbols; n++)
            {
                struct aux_symbol_f1* asym = (struct aux_symbol_f1*)malloc(sizeof(struct aux_symbol_f1));

                // Read tag index
                if (!readDword(p->fileBuffer, offset, &asym->tagIndex))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint32_t);

                // Read total size
                if (!readDword(p->fileBuffer, offset, &asym->totalSize))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint32_t);

                // Read pointer to line number
                if (!readDword(p->fileBuffer, offset, &asym->pointerToLineNumber))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint32_t);

                // Read pointer to next function
                if (!readDword(p->fileBuffer, offset, &asym->pointerToNextFunction))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                // Skip the processed 4 bytes + unused 2 bytes
                offset += sizeof(uint8_t) * 6;

                // Save the record
                list_enqueue(sym->aux_symbols_f1, (void*)asym);
            }

        }
        else if (sym->storageClass == IMAGE_SYM_CLASS_FUNCTION) {
            // Auxiliary Format 2: .bf and .ef Symbols

            for (uint8_t n = 0; n < sym->numberOfAuxSymbols; n++)
            {
                struct aux_symbol_f2* asym = (struct aux_symbol_f2*)malloc(sizeof(struct aux_symbol_f2));

                // Skip unused 4 bytes
                offset += sizeof(uint32_t);

                // Read line number
                if (!readWord(p->fileBuffer, offset, &asym->lineNumber)) {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint16_t);

                // Skip unused 6 bytes
                offset += sizeof(uint8_t) * 6;

                // Read pointer to next function
                if (!readDword(p->fileBuffer, offset, &asym->pointerToNextFunction))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                // Skip the processed 4 bytes + unused 2 bytes
                offset += sizeof(uint8_t) * 6;

                // Save the record
                list_enqueue(sym->aux_symbols_f2, asym);
            }

        }
        else if (sym->storageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->sectionNumber == IMAGE_SYM_UNDEFINED && sym->value == 0)
        {
            // Auxiliary Format 3: Weak Externals

            for (uint8_t n = 0; n < sym->numberOfAuxSymbols; n++)
            {
                struct aux_symbol_f3* asym = (struct aux_symbol_f3*)malloc(sizeof(struct aux_symbol_f3));

                // Read line number
                if (!readDword(p->fileBuffer, offset, &asym->tagIndex))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                // Read characteristics
                if (!readDword(p->fileBuffer, offset, &asym->characteristics))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                // Skip unused 10 bytes
                offset += sizeof(uint8_t) * 10;

                // Save the record
                list_enqueue(sym->aux_symbols_f3, (void*)asym);
            }

        }
        else if (sym->storageClass == IMAGE_SYM_CLASS_FILE) {
            // Auxiliary Format 4: Files

            for (uint8_t n = 0; n < sym->numberOfAuxSymbols; n++)
            {
                struct aux_symbol_f4* asym = (struct aux_symbol_f4*)malloc(sizeof(struct aux_symbol_f4));

                // Read filename
                bool terminatorFound = false;
                asym->strFilename = (char*)malloc(sizeof(char) * 200);

                for (uint16_t j = 0; j < SYMTAB_RECORD_LEN; j++)
                {
                    // Save the raw field
                    if (!readByte(p->fileBuffer, offset, &asym->filename[j]))
                    {
                        //PE_ERR(PEERR_MAGIC);
                        return false;
                    }

                    offset += sizeof(uint8_t);

                    if (asym->filename[j] == 0)
                    {
                        terminatorFound = true;
                    }

                    if (!terminatorFound)
                    {
                        //asym->strFilename.push_back(static_cast<char>(asym->filename[j]));
                        uint8_t* ccc = asym->filename[j];
                        char* cc = (char*)&ccc;
                        strcat(asym->strFilename, cc);
                    }
                }

                // Save the record
                list_enqueue(sym->aux_symbols_f4, (void*)asym);
            }
        }
        else if (sym->storageClass == IMAGE_SYM_CLASS_STATIC)
        {
            // Auxiliary Format 5: Section Definitions

            for (uint8_t n = 0; n < sym->numberOfAuxSymbols; n++)
            {
                struct aux_symbol_f5* asym = (struct aux_symbol_f5*)malloc(sizeof(struct aux_symbol_f5));

                // Read length
                if (!readDword(p->fileBuffer, offset, &asym->length))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint32_t);

                // Read number of relocations
                if (!readWord(p->fileBuffer, offset, &asym->numberOfRelocations))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint16_t);

                // Read number of line numbers
                if (!readWord(p->fileBuffer, offset, &asym->numberOfLineNumbers))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint16_t);

                // Read checksum
                if (!readDword(p->fileBuffer, offset, &asym->checkSum))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint32_t);

                // Read number
                if (!readWord(p->fileBuffer, offset, &asym->number))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint16_t);

                // Read selection
                if (!readByte(p->fileBuffer, offset, &asym->selection))
                {
                    //PE_ERR(PEERR_MAGIC);
                    return false;
                }

                offset += sizeof(uint8_t);

                // Skip unused 3 bytes
                offset += sizeof(uint8_t) * 3;

                // Save the record
                list_enqueue(sym->aux_symbols_f5, (void*)asym);
            }
        }
        else
        {
//#ifdef PEPARSE_LIBRARY_WARNINGS
//            std::ios::fmtflags originalStreamFlags(std::cerr.flags());
//
//            auto storageClassName = GetSymbolTableStorageClassName(sym.storageClass);
//            if (storageClassName == nullptr) {
//                std::cerr << "Warning: Skipping auxiliary symbol of type 0x" << std::hex
//                    << static_cast<std::uint32_t>(sym.storageClass)
//                    << " at offset 0x" << std::hex << offset << "\n";
//            }
//            else {
//
//                std::cerr << "Warning: Skipping auxiliary symbol of type "
//                    << storageClassName << " at offset 0x" << std::hex << offset
//                    << "\n";
//            }
//
//            std::cerr.flags(originalStreamFlags);
//#endif
            offset = nextSymbolOffset;
        }

        if (offset != nextSymbolOffset)
        {
//#ifdef PEPARSE_LIBRARY_WARNINGS
//            std::ios::fmtflags originalStreamFlags(std::cerr.flags());
//
//            std::cerr << "Warning: Invalid internal offset (current: 0x" << std::hex
//                << offset << ", expected: 0x" << std::hex << nextSymbolOffset
//                << ")\n";
//
//            std::cerr.flags(originalStreamFlags);
//#endif
            offset = nextSymbolOffset;
        }
    }

    return true;
}

parsed_pe* ParsePEFromBuffer(bounded_buffer* buffer) {
    // First, create a new parsed_pe structure
    // We pass std::nothrow parameter to new so in case of failure it returns
    // nullptr instead of throwing exception std::bad_alloc.
    //parsed_pe* p = new (std::nothrow) parsed_pe();
    parsed_pe* p = (parsed_pe*)malloc(sizeof(parsed_pe));

    if (p == NULL)
    {
        //PE_ERR(PEERR_MEM);
        return NULL;
    }

    // Make a new buffer object to hold just our file data
    p->fileBuffer = buffer;

    p->internal = (struct parsed_pe_internal*)malloc(sizeof(struct parsed_pe_internal));

    if (p->internal == NULL)
    {
        deleteBuffer(p->fileBuffer);
        free(p);
        //PE_ERR(PEERR_MEM);
        return NULL;
    }

    // get header information
    bounded_buffer* remaining = NULL;
    if (!getHeader(p->fileBuffer, p->peHeader, remaining))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        // err is set by getHeader
        return NULL;
    }

    bounded_buffer* file = p->fileBuffer;
    if (!getSections(remaining, file, p->peHeader->nt, p->internal->secs))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        //PE_ERR(PEERR_SECT);
        return NULL;
    }

    if (!getResources(remaining, file, p->internal->secs, p->internal->rsrcs))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        //PE_ERR(PEERR_RESC);
        return NULL;
    }

    // Get exports
    if (!getExports(p))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        //PE_ERR(PEERR_MAGIC);
        return NULL;
    }

    // Get relocations, if exist
    if (!getRelocations(p))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        //PE_ERR(PEERR_MAGIC);
        return NULL;
    }

    if (!getDebugDir(p))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        //PE_ERR(PEERR_MAGIC);
        return NULL;
    }

    // Get imports
    if (!getImports(p))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        return NULL;
    }

    // Get symbol table
    if (!getSymbolTable(p))
    {
        deleteBuffer(remaining);
        DestructParsedPE(p);
        return NULL;
    }

    deleteBuffer(remaining);

    return p;
}

parsed_pe* ParsePEFromFile(const char* filePath)
{
    auto buffer = readFileToFileBuffer(filePath);

    if (buffer == NULL)
    {
        // err is set by readFileToFileBuffer
        return NULL;
    }

    return ParsePEFromBuffer(buffer);
}

parsed_pe* ParsePEFromPointer(uint8_t* ptr, uint32_t sz)
{
    auto buffer = makeBufferFromPointer(ptr, sz);

    if (buffer == NULL)
    {
        // err is set by makeBufferFromPointer
        return NULL;
    }

    return ParsePEFromBuffer(buffer);
}

void DestructParsedPE(parsed_pe* p)
{
    if (p == NULL)
    {
        return;
    }

    deleteBuffer(p->fileBuffer);

    list_t* n = p->internal->secs;
    struct section* s;
    struct resource* r;
    struct debugent* d;

    while (n != NULL)
    {
        s = (struct section*)n->data;
        if (s != NULL && s->sectionData != NULL)
        {
            deleteBuffer(s->sectionData);
        }
        n = n->next;
    }

    n = p->internal->rsrcs;
    while (n != NULL)
    {
        r = (struct resource*)n->data;
        if (r != NULL && r->buf != NULL)
        {
            deleteBuffer(r->buf);
        }
        n = n->next;
    }

    n = p->internal->debugdirs;
    while (n != NULL)
    {
        d = (struct debugent*)n->data;
        if (d != NULL && d->data != NULL)
        {
            deleteBuffer(d->data);
        }
        n = n->next;
    }

    free(p->internal);
    free(p);
}

// iterate over the imports by VA and string
void IterImpVAString(parsed_pe* pe, iterVAStr cb, void* cbd)
{
    list_t* l = pe->internal->imports;
    list_t* n = pe->internal->imports;

    while(n != NULL)
    {
        struct importent* i = (struct importent*)n->data;

        if (cb(cbd, i->addr, i->moduleName, i->symbolName) != 0)
        {
            break;
        }

        n = n->next;
    }
}

// iterate over relocations in the PE file
void IterRelocs(parsed_pe* pe, iterReloc cb, void* cbd)
{
    list_t* l = pe->internal->relocs;
    list_t* n = pe->internal->relocs;

    while (n != NULL)
    {
        struct reloc* r = (struct reloc*)n->data;

        if (cb(cbd, r->shiftedAddr, r->type) != 0)
        {
            break;
        }

        n = n->next;
    }
}

void IterDebugs(parsed_pe* pe, iterDebug cb, void* cbd)
{
    list_t* l = pe->internal->debugdirs;
    list_t* n = pe->internal->debugdirs;

    while (n != NULL)
    {
        struct debugent* d = (struct debugent*)n->data;

        if (cb(cbd, d->type, d->data) != 0)
        {
            break;
        }

        n = n->next;
    }
}

// Iterate over symbols (symbol table) in the PE file
void IterSymbols(parsed_pe* pe, iterSymbol cb, void* cbd)
{
    list_t* l = pe->internal->symbols;
    list_t* n = pe->internal->symbols;

    while (n != NULL)
    {
        struct symbol* s = (struct symbol*)n->data;

        if (cb(cbd,
            s->strName,
            s->value,
            s->sectionNumber,
            s->type,
            s->storageClass,
            s->numberOfAuxSymbols) != 0)
        {
            break;
        }

        n = n->next;
    }
}

// iterate over the exports by VA
void IterExpVA(parsed_pe* pe, iterExp cb, void* cbd)
{
    list_t* l = pe->internal->exports;
    list_t* n = pe->internal->exports;

    while (n != NULL)
    {
        struct exportent* i = (struct exportent*)n->data;

        if (i->addr == 0)
        {
            n = n->next;

            continue;
        }
        if (cb(cbd, i->addr, i->moduleName, i->symbolName) != 0)
        {
            break;
        }

        n = n->next;
    }

    return;
}

// iterate over the exports with full information
void IterExpFull(parsed_pe* pe, iterExpFull cb, void* cbd)
{
    list_t* l = pe->internal->exports;
    list_t* n = pe->internal->exports;

    while (n != NULL)
    {
        struct exportent* i = (struct exportent*)n->data;

        if (cb(cbd, i->addr, i->ordinal, i->moduleName, i->symbolName, i->forwardName) != 0)
        {
            break;
        }

        n = n->next;
    }

    return;
}

// iterate over sections
void IterSec(parsed_pe* pe, int dt, iterSec cb, void* cbd)
{
    struct parsed_pe_internal* pint = pe->internal;
    list_t* n = pint->secs;

    while (n != NULL)
    {
        struct section* s = (struct section*)n->data;

        if (cb(pe, dt, cbd, s->sectionBase, s->sectionName, s->sec, s->sectionData) != 0)
        {
            break;
        }

        n = n->next;
    }

    return;
}

bool ReadByteAtVA(parsed_pe* pe, VA v, std::uint8_t& b) {
    // find this VA in a section
    section s;

    if (!getSecForVA(pe->internal->secs, v, s)) {
        PE_ERR(PEERR_SECTVA);
        return false;
    }

    auto off = static_cast<std::uint32_t>(v - s.sectionBase);
    return readByte(s.sectionData, off, b);
}

uint32_t ReadSectionSize(parsed_pe* pe, VA v)
{
    section s;

    if (getSecForVA(pe->internal->secs, v, s))
    {
        return s.sec.SizeOfRawData;
    }
    return 0;
}

bool ReadBytesAtVA(parsed_pe* pe, VA v, std::uint8_t* buffer, uint32_t size_buffer) {
    bool result = true;
    // find this VA in a section
    section s;
    std::uint8_t b;
    uint32_t i;

    if (!getSecForVA(pe->internal->secs, v, s)) {
        PE_ERR(PEERR_SECTVA);
        return false;
    }

    auto offset = static_cast<std::uint32_t>(v - s.sectionBase);

    for (i = 0; i < size_buffer; i++)
    {
        if (readByte(s.sectionData, offset + i, b))
        {
            buffer[i] = b;

            //std::cout << " " << b;
        }
        else {
            //result = false;
        }
    }

    //for (i = 0; i < size_buffer; i++)
    //{
    //    std::cout << " " << std::hex << b;
    //}

    //std::cout << std::endl;
    //std::cout << "result: " << result << std::endl;
    return result;
}

bool GetEntryPoint(parsed_pe* pe, VA& v) {

    if (pe != nullptr) {
        nt_header_32* nthdr = &pe->peHeader.nt;

        if (nthdr->OptionalMagic == NT_OPTIONAL_32_MAGIC) {
            v = nthdr->OptionalHeader.AddressOfEntryPoint +
                nthdr->OptionalHeader.ImageBase;
        }
        else if (nthdr->OptionalMagic == NT_OPTIONAL_64_MAGIC) {
            v = nthdr->OptionalHeader64.AddressOfEntryPoint +
                nthdr->OptionalHeader64.ImageBase;
        }
        else {
            PE_ERR(PEERR_MAGIC);
            return false;
        }

        return true;
    }

    return false;
}

const char* GetMachineAsString(parsed_pe* pe)
{
    if (pe == NULL)
    {
        return NULL;
    }

    switch (pe->peHeader->nt->FileHeader->Machine)
    {
    case IMAGE_FILE_MACHINE_I386:
        return create_heap_string("x86");
    case IMAGE_FILE_MACHINE_ARMNT:
        return create_heap_string("ARM Thumb-2 Little-Endian");
    case IMAGE_FILE_MACHINE_IA64:
        return create_heap_string("Intel IA64");
    case IMAGE_FILE_MACHINE_AMD64:
        return create_heap_string("x64");
    case IMAGE_FILE_MACHINE_ARM64:
        return create_heap_string("ARM64");
    case IMAGE_FILE_MACHINE_CEE:
        return create_heap_string("CLR Pure MSIL");
    default:
        return NULL;
    }
}

const char* GetSubsystemAsString(parsed_pe* pe) {
    if (pe == nullptr)
        return nullptr;

    std::uint16_t subsystem;
    if (pe->peHeader.nt.OptionalMagic == NT_OPTIONAL_32_MAGIC)
        subsystem = pe->peHeader.nt.OptionalHeader.Subsystem;
    else if (pe->peHeader.nt.OptionalMagic == NT_OPTIONAL_64_MAGIC)
        subsystem = pe->peHeader.nt.OptionalHeader64.Subsystem;
    else
        return nullptr;

    switch (subsystem) {
    case IMAGE_SUBSYSTEM_UNKNOWN:
        return "UNKNOWN";
    case IMAGE_SUBSYSTEM_NATIVE:
        return "NATIVE";
    case IMAGE_SUBSYSTEM_WINDOWS_GUI:
        return "WINDOWS_GUI";
    case IMAGE_SUBSYSTEM_WINDOWS_CUI:
        return "WINDOWS_CUI";
    case IMAGE_SUBSYSTEM_OS2_CUI:
        return "OS2_CUI";
    case IMAGE_SUBSYSTEM_POSIX_CUI:
        return "POSIX_CUI";
    case IMAGE_SUBSYSTEM_NATIVE_WINDOWS:
        return "NATIVE_WINDOWS";
    case IMAGE_SUBSYSTEM_WINDOWS_CE_GUI:
        return "WINDOWS_CE_GUI";
    case IMAGE_SUBSYSTEM_EFI_APPLICATION:
        return "EFI_APPLICATION";
    case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER:
        return "EFI_BOOT_SERVICE_DRIVER";
    case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER:
        return "EFI_RUNTIME_DRIVER";
    case IMAGE_SUBSYSTEM_EFI_ROM:
        return "EFI_ROM";
    case IMAGE_SUBSYSTEM_XBOX:
        return "XBOX";
    case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION:
        return "WINDOWS_BOOT_APPLICATION";
    case IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG:
        return "XBOX_CODE_CATALOG";
    default:
        return nullptr;
    }
}

bool GetDataDirectoryEntry(parsed_pe* pe,
    data_directory_kind dirnum,
    std::vector<std::uint8_t>& raw_entry) {
    raw_entry.clear();

    if (pe == nullptr) {
        PE_ERR(PEERR_NONE);
        return false;
    }

    data_directory dir;
    VA addr;
    if (pe->peHeader.nt.OptionalMagic == NT_OPTIONAL_32_MAGIC) {
        dir = pe->peHeader.nt.OptionalHeader.DataDirectory[dirnum];
        addr = dir.VirtualAddress + pe->peHeader.nt.OptionalHeader.ImageBase;
    }
    else if (pe->peHeader.nt.OptionalMagic == NT_OPTIONAL_64_MAGIC) {
        dir = pe->peHeader.nt.OptionalHeader64.DataDirectory[dirnum];
        addr = dir.VirtualAddress + pe->peHeader.nt.OptionalHeader64.ImageBase;
    }
    else {
        PE_ERR(PEERR_MAGIC);
        return false;
    }

    if (dir.Size <= 0) {
        PE_ERR(PEERR_SIZE);
        return false;
    }

    /* NOTE(ww): DIR_SECURITY is an annoying special case: its contents
        * are never mapped into memory, so its "RVA" is actually a direct
        * file offset.
        * See:
        * https://docs.microsoft.com/en-us/windows/win32/debug/pe-format#the-attribute-certificate-table-image-only
        */
    if (dirnum == DIR_SECURITY) {
        auto* buf = splitBuffer(
            pe->fileBuffer, dir.VirtualAddress, dir.VirtualAddress + dir.Size);
        if (buf == nullptr) {
            PE_ERR(PEERR_SIZE);
            return false;
        }

        raw_entry.assign(buf->buf, buf->buf + buf->bufLen);
        deleteBuffer(buf);
    }
    else {
        section sec;
        if (!getSecForVA(pe->internal->secs, addr, sec)) {
            PE_ERR(PEERR_SECTVA);
            return false;
        }

        auto off = static_cast<std::uint32_t>(addr - sec.sectionBase);
        if (off + dir.Size >= sec.sectionData->bufLen) {
            PE_ERR(PEERR_SIZE);
            return false;
        }

        raw_entry.assign(sec.sectionData->buf + off,
            sec.sectionData->buf + off + dir.Size);
    }

    return true;
}