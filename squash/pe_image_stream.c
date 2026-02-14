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

#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000
#define IMAGE_BASE 0x140000000

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
const char* GetRichObjectType(uint16_t prodId)
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
const char* GetRichProductName(uint16_t buildNum)
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

//const char* GetMachineAsString(parsed_pe* pe)
//{
//    if (pe == NULL)
//    {
//        return NULL;
//    }
//
//    switch (pe->peHeader->nt->FileHeader->Machine)
//    {
//    case IMAGE_FILE_MACHINE_I386:
//        return create_heap_string("x86");
//    case IMAGE_FILE_MACHINE_ARMNT:
//        return create_heap_string("ARM Thumb-2 Little-Endian");
//    case IMAGE_FILE_MACHINE_IA64:
//        return create_heap_string("Intel IA64");
//    case IMAGE_FILE_MACHINE_AMD64:
//        return create_heap_string("x64");
//    case IMAGE_FILE_MACHINE_ARM64:
//        return create_heap_string("ARM64");
//    case IMAGE_FILE_MACHINE_CEE:
//        return create_heap_string("CLR Pure MSIL");
//    default:
//        return NULL;
//    }
//}
//
//const char* GetSubsystemAsString(parsed_pe* pe)
//{
//    if (pe == NULL)
//    {
//        return NULL;
//    }
//
//    uint16_t subsystem;
//    if (pe->peHeader->nt->OptionalMagic == NT_OPTIONAL_32_MAGIC)
//    {
//        subsystem = pe->peHeader->nt->OptionalHeader->Subsystem;
//    }
//       
//    else if (pe->peHeader->nt->OptionalMagic == NT_OPTIONAL_64_MAGIC)
//    {
//        subsystem = pe->peHeader->nt->OptionalHeader64->Subsystem;
//    } 
//    else
//    {
//        return NULL;
//    }
//
//    switch (subsystem)
//    {
//    case IMAGE_SUBSYSTEM_UNKNOWN:
//        return create_heap_string("UNKNOWN");
//    case IMAGE_SUBSYSTEM_NATIVE:
//        return create_heap_string("NATIVE");
//    case IMAGE_SUBSYSTEM_WINDOWS_GUI:
//        return create_heap_string("WINDOWS_GUI");
//    case IMAGE_SUBSYSTEM_WINDOWS_CUI:
//        return create_heap_string("WINDOWS_CUI");
//    case IMAGE_SUBSYSTEM_OS2_CUI:
//        return create_heap_string("OS2_CUI");
//    case IMAGE_SUBSYSTEM_POSIX_CUI:
//        return create_heap_string("POSIX_CUI");
//    case IMAGE_SUBSYSTEM_NATIVE_WINDOWS:
//        return create_heap_string("NATIVE_WINDOWS");
//    case IMAGE_SUBSYSTEM_WINDOWS_CE_GUI:
//        return create_heap_string("WINDOWS_CE_GUI");
//    case IMAGE_SUBSYSTEM_EFI_APPLICATION:
//        return create_heap_string("EFI_APPLICATION");
//    case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER:
//        return create_heap_string("EFI_BOOT_SERVICE_DRIVER");
//    case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER:
//        return create_heap_string("EFI_RUNTIME_DRIVER");
//    case IMAGE_SUBSYSTEM_EFI_ROM:
//        return create_heap_string("EFI_ROM");
//    case IMAGE_SUBSYSTEM_XBOX:
//        return create_heap_string("XBOX");
//    case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION:
//        return create_heap_string("WINDOWS_BOOT_APPLICATION");
//    case IMAGE_SUBSYSTEM_XBOX_CODE_CATALOG:
//        return create_heap_string("XBOX_CODE_CATALOG");
//    default:
//        return NULL;
//    }
//}
//
//void bb_init(binbuf* b, size_t cap)
//{
//    b->data = (uint8_t*)malloc(cap);
//    b->size = 0;
//    b->capacity = cap;
//}
//
//void bb_reserve(binbuf* b, size_t add)
//{
//    if (b->size + add > b->capacity) {
//        b->capacity *= 2;
//        b->data = (uint8_t*)realloc(b->data, b->capacity);
//    }
//}
//
//size_t bb_write(binbuf* b, const void* src, size_t sz)
//{
//    bb_reserve(b, sz);
//    memcpy(b->data + b->size, src, sz);
//    size_t off = b->size;
//    b->size += sz;
//    return off;
//}
//
//void bb_align(binbuf* b, size_t align)
//{
//    size_t pad = (align - (b->size % align)) % align;
//    uint8_t zero[16] = { 0 };
//    while (pad)
//    {
//        size_t chunk = pad > sizeof(zero) ? sizeof(zero) : pad;
//        bb_write(b, zero, chunk);
//        pad -= chunk;
//    }
//}

//dir build_imports(uint8_t* buf, uint32_t rvaBase)
//{
//    uint32_t off = 0;
//
//    IMAGE_IMPORT_DESCRIPTOR* imp = (void*)(buf + off);
//    off += sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2; // one + null
//
//    uint32_t iltRVA = rvaBase + off;
//    uint64_t* ilt = (uint64_t*)(buf + off);
//    off += 8 * 4;
//
//    uint32_t iatRVA = rvaBase + off;
//    uint64_t* iat = (uint64_t*)(buf + off);
//    off += 8 * 4;
//
//    uint32_t nameRVA = rvaBase + off;
//    char* dllName = (char*)(buf + off);
//    strcpy(dllName, "KERNEL32.dll");
//    off += strlen(dllName) + 1;
//
//    const char* funcs[] = { "GetStdHandle","WriteFile","ExitProcess" };
//
//    for (int i = 0; i < 3; i++)
//    {
//        uint32_t hintNameRVA = rvaBase + off;
//        ilt[i] = hintNameRVA;
//        iat[i] = hintNameRVA;
//
//        *(uint16_t*)(buf + off) = 0;
//        off += 2;
//        strcpy((char*)(buf + off), funcs[i]);
//        off += strlen(funcs[i]) + 1;
//    }
//
//    imp[0].OriginalFirstThunk = iltRVA;
//    imp[0].FirstThunk = iatRVA;
//    imp[0].Name = nameRVA;
//
//    dir d = { rvaBase, off };
//    return d;
//}

static const uint8_t dos_stub[] = {
  0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
  0xB8,0x01,0x4C,0xCD,0x21,
  'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
  'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
  'i','n',' ','D','O','S',' ','m','o','d','e','.','\r','\n','$'
};

//uint32_t build_imports(uint8_t* buf, uint32_t rvaBase)
//{
//    memset(buf, 0, 512);
//
//    IMAGE_IMPORT_DESCRIPTOR* imp = (void*)buf;
//
//    uint32_t off = sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2;
//
//    uint32_t iltRVA = rvaBase + off;
//    uint64_t* ilt = (uint64_t*)(buf + off);
//    off += 32;
//
//    uint32_t iatRVA = rvaBase + off;
//    uint64_t* iat = (uint64_t*)(buf + off);
//    off += 32;
//
//    uint32_t nameRVA = rvaBase + off;
//    strcpy((char*)(buf + off), "KERNEL32.dll");
//    off += 13;
//
//    const char* names[] = { "GetStdHandle","WriteFile","ExitProcess" };
//
//    for (int i = 0; i < 3; i++) {
//        uint32_t hintNameRVA = rvaBase + off;
//
//        ilt[i] = hintNameRVA;
//        iat[i] = hintNameRVA;
//
//        *(uint16_t*)(buf + off) = 0;
//        off += 2;
//        strcpy((char*)(buf + off), names[i]);
//        off += strlen(names[i]) + 1;
//    }
//
//    imp->OriginalFirstThunk = iltRVA;
//    imp->FirstThunk = iatRVA;
//    imp->Name = nameRVA;
//    imp->ForwarderChain = 0xFFFFFFFF;
//
//    return ALIGN(off, FILE_ALIGN);
//}

uint8_t code[] = {
    0x48,0x83,0xEC,0x28,

    // GetStdHandle(-11)
    0x48,0xC7,0xC1,0xF5,0xFF,0xFF,0xFF,
    0xFF,0x15,0,0,0,0,

    // WriteFile(...)
    0x48,0x89,0xC1,
    0x48,0x8D,0x15,0,0,0,0,
    0x41,0xB8,16,0,0,0,
    0x4C,0x8D,0x0D,0,0,0,0,
    0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
    0xFF,0x15,0,0,0,0,

    // ExitProcess(0)
    0x31,0xC9,
    0xFF,0x15,0,0,0,0
};

#define FA 0x200
#define SA 0x1000

void build_pe(const char* path)
{
    FILE* f = fopen(path, "wb");

    // ---------------- TEXT ----------------

    uint8_t text[512] = { 0 };
    memcpy(text, code, sizeof(code));
    uint32_t t = sizeof(code);

    uint32_t msgOff = t;
    memcpy(text + t, "Hello from PE!\r\n", 16);
    t += 16;

    uint32_t wrOff = t;
    *(uint32_t*)(text + t) = 0;
    t += 4;

    // Patch RIP-rel to msg/written later...

    uint32_t textSize = ALIGN(t, FILE_ALIGN);

    // ---------------- IDATA ----------------

    uint8_t id[512] = { 0 };
    IMPDESC* d = (void*)id;

    uint32_t off = sizeof(IMPDESC) * 2;

    uint64_t* ilt = (uint64_t*)(id + off);
    uint32_t iltRVA = 0x2000 + off;
    off += 32;

    uint64_t* iat = (uint64_t*)(id + off);
    uint32_t iatRVA = 0x2000 + off;
    off += 32;

    uint32_t nameRVA = 0x2000 + off;
    strcpy((char*)(id + off), "KERNEL32.dll");
    off += 13;

    const char* nms[] = { "GetStdHandle","WriteFile","ExitProcess" };

    for (int i = 0; i < 3; i++) {
        uint32_t rva = 0x2000 + off;
        ilt[i] = rva;
        iat[i] = rva;
        *(uint16_t*)(id + off) = 0;
        strcpy((char*)(id + off + 2), nms[i]);
        off += 2 + strlen(nms[i]) + 1;
    }

    d->ILT = iltRVA;
    d->IAT = iatRVA;
    d->Name = nameRVA;
    d->Fwd = 0;

    uint32_t idSize = ALIGN(off, FILE_ALIGN);

    // ---------------- HEADERS ----------------

    DOS dos = { 0 };
    dos.e_magic = 0x5A4D;
    dos.e_lfanew = 0x80;

    fwrite(&dos, sizeof(dos), 1, f);
    uint8_t z[256] = { 0 };
    fwrite(z, 0x80 - sizeof(dos), 1, f);

    NT64 nt = { 0 };
    nt.Sig = 0x4550;

    nt.F.Machine = 0x8664;
    nt.F.Sections = 2;
    nt.F.OptSize = sizeof(OPT64);
    nt.F.Chars = 0x22;

    nt.O.Magic = 0x20B;
    nt.O.Entry = 0x1000;
    nt.O.BaseCode = 0x1000;
    nt.O.ImageBase = IMAGE_BASE;
    nt.O.SecAlign = SEC_ALIGN;
    nt.O.FileAlign = FILE_ALIGN;
    nt.O.ImgSz = 0x3000;
    nt.O.HdrSz = 0x200;
    nt.O.Subsys = 3;
    nt.O.StkRes = 0x100000;
    nt.O.StkCom = 0x1000;
    nt.O.HeapRes = 0x100000;
    nt.O.HeapCom = 0x1000;
    nt.O.Dirs = 16;

    nt.O.Dir[1].VA = 0x2000;
    nt.O.Dir[1].Size = idSize;

    fwrite(&nt, sizeof(nt), 1, f);

    SECT s[2] = { 0 };

    memcpy(s[0].Name, ".text", 5);
    s[0].VA = 0x1000;
    s[0].VSize = t;
    s[0].RawSize = textSize;
    s[0].RawPtr = 0x200;
    s[0].Chars = 0x60000020;

    memcpy(s[1].Name, ".idata", 6);
    s[1].VA = 0x2000;
    s[1].VSize = idSize;
    s[1].RawSize = idSize;
    s[1].RawPtr = 0x200 + textSize;
    s[1].Chars = 0xC0000040;

    fwrite(s, sizeof(s), 1, f);

    fwrite(z, 0x200 - ftell(f), 1, f);
    fwrite(text, textSize, 1, f);
    fwrite(id, idSize, 1, f);

    fclose(f);
}

bool WritePEProgramSQImage(const char* fileName, unsigned char* sq_program_image, int sq_program_image_length)
{
    build_pe(fileName);

    //FILE* f = fopen(fileName, "wb");

    //uint8_t buf[4096] = { 0 };

    //// ---------------- DOS ----------------
    //*(uint16_t*)(buf) = 0x5A4D;
    //*(uint32_t*)(buf + 0x3C) = 0x80;

    //// ---------------- NT ----------------
    //uint8_t* nt = buf + 0x80;
    //*(uint32_t*)nt = 0x4550; // PE

    //// FILE HEADER
    //*(uint16_t*)(nt + 4) = 0x8664;
    //*(uint16_t*)(nt + 6) = 1;
    //*(uint16_t*)(nt + 20) = 0xF0;
    //*(uint16_t*)(nt + 22) = 0x22;

    //// OPTIONAL HEADER
    //uint8_t* op = nt + 24;
    //*(uint16_t*)op = 0x20B;
    //*(uint32_t*)(op + 16) = 0x1000; // entry
    //*(uint32_t*)(op + 20) = 0x1000;
    //*(uint64_t*)(op + 24) = 0x400000;
    //*(uint32_t*)(op + 32) = SA;
    //*(uint32_t*)(op + 36) = FA;
    //*(uint32_t*)(op + 56) = 0x2000;
    //*(uint32_t*)(op + 60) = 0x200;
    //*(uint16_t*)(op + 68) = 3;
    //*(uint64_t*)(op + 72) = 0x100000;
    //*(uint64_t*)(op + 80) = 0x1000;
    //*(uint64_t*)(op + 88) = 0x100000;
    //*(uint64_t*)(op + 96) = 0x1000;
    //*(uint32_t*)(op + 108) = 16;

    //// SECTION
    //uint8_t* s = nt + 24 + 0xF0;
    //memcpy(s, ".text", 5);
    //*(uint32_t*)(s + 8) = 1;
    //*(uint32_t*)(s + 12) = 0x1000;
    //*(uint32_t*)(s + 16) = FA;
    //*(uint32_t*)(s + 20) = 0x200;
    //*(uint32_t*)(s + 36) = 0x60000020;

    //// ---------------- CODE ----------------
    //uint8_t code[] = { 0xC3 }; // ret
    //memcpy(buf + 0x200, code, sizeof(code));

    //fwrite(buf, 1, 0x400, f);
    //fclose(f);

    return true;
}