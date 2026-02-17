
#include <inttypes.h> // Recommended for PRIu16 format specifier

#include "pe_image_stream.h"

#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000
#define IMAGE_BASE 0x140000000

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

//static const uint8_t dos_stub[] = 
//{
//    0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,
//    0xB8,0x01,0x4C,0xCD,0x21,
//    'T','h','i','s',' ','p','r','o','g','r','a','m',' ',
//    'c','a','n','n','o','t',' ','b','e',' ','r','u','n',' ',
//    'i','n',' ','D','O','S',' ','m','o','d','e','.','\r','\n','$'
//};

static const uint8_t dos_stub[] = {
    0x0E,                   // push cs
    0x1F,                   // pop ds
    0xBA,0x0E,0x00,         // mov dx,0x000E
    0xB4,0x09,              // mov ah,9
    0xCD,0x21,              // int 21h
    0xB8,0x01,0x4C,         // mov ax,4C01h
    0xCD,0x21,              // int 21h

    // message:
    'T','h','i','s',' ',
    'p','r','o','g','r','a','m',' ',
    'c','a','n','n','o','t',' ',
    'b','e',' ','r','u','n',' ',
    'i','n',' ','D','O','S',' ',
    'm','o','d','e','.',
    '\r','\n','$'
};

uint32_t build_imports(uint8_t* buf, uint32_t rvaBase)
{
    memset(buf, 0, 512);

    const char* dll = "kernel32.dll";
    const char* fn[] = {
        "GetStdHandle",
        "WriteFile",
        "ExitProcess"
    };

    uint32_t off = 0;

    IMPDESC* d = (IMPDESC*)buf;
    off += sizeof(IMPDESC) * 2;

    uint32_t iltRVA = rvaBase + off;
    uint64_t* ilt = (uint64_t*)(buf + off);
    off += 8 * (3 + 1);

    uint32_t iatRVA = rvaBase + off;
    uint64_t* iat = (uint64_t*)(buf + off);
    off += 8 * (3 + 1);

    uint32_t nameRVA = rvaBase + off;
    strcpy((char*)(buf + off), dll);
    off += strlen(dll) + 1;

    uint32_t hnRVA[3];

    for (int i = 0; i < 3; i++) {
        off = ALIGN(off, 2);
        hnRVA[i] = rvaBase + off;

        *(uint16_t*)(buf + off) = 0;
        strcpy((char*)(buf + off + 2), fn[i]);

        off += 2 + strlen(fn[i]) + 1;
    }

    for (int i = 0; i < 3; i++) {
        ilt[i] = hnRVA[i];
        iat[i] = hnRVA[i];
    }

    d->OFT = iltRVA;
    d->Name = nameRVA;
    d->FT = iatRVA;

    return ALIGN(off, 16);
}

void build_pe32(const char* fileName)
{
    FILE* f = fopen(fileName, "wb");

    //uint8_t img[4096] = { 0 };

    uint8_t buf[8192] = { 0 };
    memset(buf, 0, sizeof(buf));

    //uint32_t textRaw = 0x200;
    //uint32_t idataRaw = 0x400;

    //uint32_t textRVA = 0x1000;
    //uint32_t idataRVA = 0x2000;

    uint32_t textRVA = 0x1000;
    uint32_t idataRVA = 0x2000;

    uint32_t textRaw = 0x200;
    uint32_t idataRaw = 0x400;

    IMAGE_DOS_HEADER dos = { 0 };
    dos.e_magic = 0x5A4D;
    dos.e_lfanew = RICH_OFFSET;
    memcpy(buf, &dos, sizeof(dos));

    // dos 16bit executable stub.
    memcpy(buf + sizeof(dos), &dos_stub, sizeof(dos_stub));

    // copy stub right after DOS header
    //memcpy(buf + sizeof(IMAGE_DOS_HEADER), dos_stub, sizeof(dos_stub));

    IMAGE_NT_HEADERS32 nt = { 0 };
    nt.Signature = 0x4550;

    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386; // IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(OPT32);
    nt.FileHeader.Characteristics = 0x22;
    //nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    //nt.FileHeader.Characteristics = IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_EXECUTABLE_IMAGE;

    OPT32* op = &nt.OptionalHeader;

    op->Magic = NT_OPTIONAL_32_MAGIC;
    op->AddressOfEntryPoint = textRVA;
    op->BaseOfCode = textRVA;
    op->ImageBase = IMAGE_BASE;
    op->SectionAlignment = SA;
    op->FileAlignment = FA;
    op->SizeOfImage = 0x3000;
    op->SizeOfHeaders = 0x200;
    op->Subsystem = 3;
    op->MajorOSVersion = 0004; // Needed otherwise loader wont load executable.
    op->MajorSubsystemVersion = 0004; // Needed otherwise loader wont load executable.
    op->SizeOfStackReserve = 0x100000;
    op->SizeOfStackCommit = 0x1000;
    op->SizeOfHeapReserve = 0x100000;
    op->SizeOfHeapCommit = 0x1000;
    //op->NumberOfRvaAndSizes = 16;

    uint8_t idata[512];
    uint32_t idSz = build_imports(idata, idataRVA);

    op->DataDirectory[1].VirtualAddress = idataRVA;
    op->DataDirectory[1].Size = idSz;

    memcpy(buf + RICH_OFFSET, &nt, sizeof(nt));

    IMAGE_SECTION_HEADER sh[2] = { 0 };

    memcpy(sh[0].Name, ".text", 5);
    sh[0].VirtualAddress = textRVA;
    sh[0].VirtualSize = 0x100;
    sh[0].SizeOfRawData = FA;
    sh[0].PointerToRawData = textRaw;
    sh[0].Characteristics = 0x60000020;

    memcpy(sh[1].Name, ".idata", 6);
    sh[1].VirtualAddress = idataRVA;
    sh[1].VirtualSize = idSz;
    sh[1].SizeOfRawData = FA;
    sh[1].PointerToRawData = idataRaw;
    sh[1].Characteristics = 0xC0000040;

    memcpy(buf + 0x80 + sizeof(nt), sh, sizeof(sh));

    uint8_t code[] = // Win64 ABI.
    {
        0x48,0x83,0xEC,0x28,          // shadow space

        // GetStdHandle(-11)
        0xB9,0xF5,0xFF,0xFF,0xFF,
        0xFF,0x15,0,0,0,0,

        // WriteFile
        0x48,0x89,0xC1,
        0x48,0x8D,0x15,0,0,0,0,
        0x41,0xB8,18,0,0,0,
        0x4C,0x8D,0x0D,0,0,0,0,
        0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
        0xFF,0x15,0,0,0,0,

        // ExitProcess
        0x31,0xC9,
        0xFF,0x15,0,0,0,0
    };

    memcpy(buf + textRaw, code, sizeof(code));

    // message + written
    uint32_t msgRVA = textRVA + 0x80;
    uint32_t wrRVA = textRVA + 0xA0;

    strcpy((char*)(buf + textRaw + 0x80), "Hello from handmade PE64!\r\n");

    // IAT base
    uint32_t iat = idataRVA + sizeof(IMPDESC) * 2 + 32;

    uint64_t base = IMAGE_BASE;

    // patch RIP displacements
#define PATCH(pos,target) \
        *(int32_t*)(buf+textRaw+pos) = \
        (int32_t)((base+target)-(base+textRVA+pos+4))

    PATCH(7, iat);        // GetStdHandle
    PATCH(16, msgRVA);    // msg
    PATCH(27, wrRVA);     // written
    PATCH(39, iat + 8);     // WriteFile
    PATCH(45, iat + 16);    // ExitProcess

    // copy idata
    memcpy(buf + idataRaw, idata, idSz);

    //FILE* f = fopen(fileName, "wb");
    fwrite(buf, 1, 0x600, f);
    fclose(f);

    //memcpy(buf + textRaw, code, sizeof(code));

    //// RIP-relative call to IAT
    //uint64_t callVA = IMAGE_BASE + textRVA + 10;
    //uint64_t iatVA = IMAGE_BASE + idataRVA + sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2 + 8;
    //int32_t disp = (int32_t)(iatVA - callVA);

    //*(int32_t*)(buf + textRaw + 8) = disp;

    //memcpy(buf + idataRaw, idata, idSz);

    //fwrite(buf, 1, 0x600, f);

    //fclose(f);
}

void build_pe(const char* fileName)
{
    FILE* f = fopen(fileName, "wb");

    //uint8_t img[4096] = { 0 };

    uint8_t buf[8192] = { 0 };
    memset(buf, 0, sizeof(buf));

    //uint32_t textRaw = 0x200;
    //uint32_t idataRaw = 0x400;

    //uint32_t textRVA = 0x1000;
    //uint32_t idataRVA = 0x2000;

    uint32_t textRVA = 0x1000;
    uint32_t idataRVA = 0x2000;

    uint32_t textRaw = 0x200;
    uint32_t idataRaw = 0x400;

    IMAGE_DOS_HEADER dos = { 0 };
    dos.e_magic = 0x5A4D;
    dos.e_lfanew = RICH_OFFSET;
    memcpy(buf, &dos, sizeof(dos));

    // dos 16bit executable stub.
    //memcpy(buf + sizeof(dos), &dos_stub, sizeof(dos_stub));

    // copy stub right after DOS header
    //memcpy(buf + sizeof(IMAGE_DOS_HEADER), dos_stub, sizeof(dos_stub));

    IMAGE_NT_HEADERS64 nt = { 0 };
    nt.Signature = 0x4550;

    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.FileHeader.Characteristics = 0x22;
    //nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    //nt.FileHeader.Characteristics = IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_EXECUTABLE_IMAGE;

    IMAGE_OPTIONAL_HEADER64* op = &nt.OptionalHeader;

    op->Magic = NT_OPTIONAL_64_MAGIC;
    op->AddressOfEntryPoint = textRVA;
    op->BaseOfCode = textRVA;
    op->ImageBase = IMAGE_BASE;
    op->SectionAlignment = SA;
    op->FileAlignment = FA;
    op->SizeOfImage = 0x3000;
    op->SizeOfHeaders = 0x200;
    op->Subsystem = 3;
    op->MajorOSVersion = 0004; // Needed otherwise loader wont load executable.
    op->MajorSubsystemVersion = 0004; // Needed otherwise loader wont load executable.
    op->SizeOfStackReserve = 0x100000;
    op->SizeOfStackCommit = 0x1000;
    op->SizeOfHeapReserve = 0x100000;
    op->SizeOfHeapCommit = 0x1000;
    op->NumberOfRvaAndSizes = 16;

    uint8_t idata[512];
    uint32_t idSz = build_imports(idata, idataRVA);

    op->DataDirectory[1].VirtualAddress = idataRVA;
    op->DataDirectory[1].Size = idSz;

    memcpy(buf + RICH_OFFSET, &nt, sizeof(nt));

    IMAGE_SECTION_HEADER sh[2] = { 0 };

    memcpy(sh[0].Name, ".text", 5);
    sh[0].VirtualAddress = textRVA;
    sh[0].VirtualSize = 0x100;
    sh[0].SizeOfRawData = FA;
    sh[0].PointerToRawData = textRaw;
    sh[0].Characteristics = 0x60000020;

    memcpy(sh[1].Name, ".idata", 6);
    sh[1].VirtualAddress = idataRVA;
    sh[1].VirtualSize = idSz;
    sh[1].SizeOfRawData = FA;
    sh[1].PointerToRawData = idataRaw;
    sh[1].Characteristics = 0xC0000040;

    memcpy(buf + 0x80 + sizeof(nt), sh, sizeof(sh));

    uint8_t code[] = // Win64 ABI.
    {
        0x48,0x83,0xEC,0x28,          // shadow space

        // GetStdHandle(-11)
        0xB9,0xF5,0xFF,0xFF,0xFF,
        0xFF,0x15,0,0,0,0,

        // WriteFile
        0x48,0x89,0xC1,
        0x48,0x8D,0x15,0,0,0,0,
        0x41,0xB8,18,0,0,0,
        0x4C,0x8D,0x0D,0,0,0,0,
        0x48,0xC7,0x44,0x24,0x20,0,0,0,0,
        0xFF,0x15,0,0,0,0,

        // ExitProcess
        0x31,0xC9,
        0xFF,0x15,0,0,0,0
    };

    memcpy(buf + textRaw, code, sizeof(code));

    // message + written
    uint32_t msgRVA = textRVA + 0x80;
    uint32_t wrRVA = textRVA + 0xA0;

    strcpy((char*)(buf + textRaw + 0x80), "Hello from handmade PE64!\r\n");

    // IAT base
    uint32_t iat = idataRVA + sizeof(IMPDESC) * 2 + 32;

    uint64_t base = IMAGE_BASE;

    // patch RIP displacements
#define PATCH(pos,target) \
        *(int32_t*)(buf+textRaw+pos) = \
        (int32_t)((base+target)-(base+textRVA+pos+4))

    PATCH(7, iat);        // GetStdHandle
    PATCH(16, msgRVA);    // msg
    PATCH(27, wrRVA);     // written
    PATCH(39, iat + 8);     // WriteFile
    PATCH(45, iat + 16);    // ExitProcess

    // copy idata
    memcpy(buf + idataRaw, idata, idSz);

    //FILE* f = fopen(fileName, "wb");
    fwrite(buf, 1, 0x600, f);
    fclose(f);

    //memcpy(buf + textRaw, code, sizeof(code));

    //// RIP-relative call to IAT
    //uint64_t callVA = IMAGE_BASE + textRVA + 10;
    //uint64_t iatVA = IMAGE_BASE + idataRVA + sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2 + 8;
    //int32_t disp = (int32_t)(iatVA - callVA);

    //*(int32_t*)(buf + textRaw + 8) = disp;

    //memcpy(buf + idataRaw, idata, idSz);

    //fwrite(buf, 1, 0x600, f);

    //fclose(f);
}

bool WritePEProgramSQImage(const char* fileName, unsigned char* sq_program_image, int sq_program_image_length)
{
    build_pe32(fileName);

    return true;
}