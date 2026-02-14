#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000
#define ALIGN(x,a) (((x)+(a-1))&~(a-1))

int main() {
    FILE *f = fopen("minimal32.exe","wb");
    if(!f) return 1;

    uint8_t buf[1024];
    memset(buf,0,sizeof(buf));

    // ---------------- DOS HEADER ----------------
    *(uint16_t*)(buf+0x00) = 0x5A4D;      // e_magic = MZ
    *(uint32_t*)(buf+0x3C) = 0x80;        // e_lfanew -> NT headers

    // ---------------- NT HEADERS ----------------
    uint8_t *nt = buf + 0x80;

    // Signature "PE\0\0"
    *(uint32_t*)(nt+0x00) = 0x00004550;

    // ---------------- FILE HEADER ----------------
    *(uint16_t*)(nt+0x04) = 0x014C;       // Machine = I386
    *(uint16_t*)(nt+0x06) = 1;            // NumberOfSections
    *(uint32_t*)(nt+0x08) = 0;            // TimeDateStamp
    *(uint32_t*)(nt+0x0C) = 0;
    *(uint32_t*)(nt+0x10) = 0;
    *(uint16_t*)(nt+0x14) = 0xE0;         // SizeOfOptionalHeader
    *(uint16_t*)(nt+0x16) = 0x0102;       // Executable | 32-bit

    // ---------------- OPTIONAL HEADER (PE32) ----------------
    uint8_t *op = nt + 0x18;

    *(uint16_t*)(op+0x00) = 0x10B;        // Magic PE32
    *(uint32_t*)(op+0x10) = 0x1000;       // AddressOfEntryPoint
    *(uint32_t*)(op+0x14) = 0x1000;       // BaseOfCode
    *(uint32_t*)(op+0x18) = 0x2000;       // BaseOfData
    *(uint32_t*)(op+0x1C) = 0x400000;     // ImageBase

    *(uint32_t*)(op+0x20) = SEC_ALIGN;    // SectionAlignment
    *(uint32_t*)(op+0x24) = FILE_ALIGN;   // FileAlignment

    *(uint16_t*)(op+0x28) = 4;            // OS version
    *(uint16_t*)(op+0x2C) = 0;
    *(uint16_t*)(op+0x30) = 4;            // Subsystem version

    *(uint32_t*)(op+0x38) = 0x2000;       // SizeOfImage
    *(uint32_t*)(op+0x3C) = 0x200;        // SizeOfHeaders

    *(uint16_t*)(op+0x44) = 3;            // Subsystem (CUI)

    *(uint32_t*)(op+0x48) = 0x100000;     // StackReserve
    *(uint32_t*)(op+0x4C) = 0x1000;       // StackCommit
    *(uint32_t*)(op+0x50) = 0x100000;     // HeapReserve
    *(uint32_t*)(op+0x54) = 0x1000;       // HeapCommit

    *(uint32_t*)(op+0x5C) = 16;           // NumberOfRvaAndSizes

    // ---------------- SECTION HEADER ----------------
    uint8_t *sec = nt + 0x18 + 0xE0;

    memcpy(sec+0x00,".text",5);

    *(uint32_t*)(sec+0x08) = 1;           // VirtualSize
    *(uint32_t*)(sec+0x0C) = 0x1000;      // VirtualAddress
    *(uint32_t*)(sec+0x10) = FILE_ALIGN;  // SizeOfRawData
    *(uint32_t*)(sec+0x14) = 0x200;       // PointerToRawData
    *(uint32_t*)(sec+0x24) = 0x60000020;  // Code | Execute | Read

    // ---------------- CODE ----------------
    // Entry point just returns
    buf[0x200] = 0xC3; // RET

    // ---------------- WRITE FILE ----------------
    fwrite(buf,1,0x400,f);
    fclose(f);

    printf("minimal32.exe created\n");
    return 0;
}
