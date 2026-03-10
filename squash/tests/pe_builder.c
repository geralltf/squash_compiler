/*
 * pe_builder.c - Windows PE (Portable Executable) Builder for 64-bit executables
 *
 * Generates valid PE32+ (64-bit) executables with:
 *   - DOS header with sane defaults
 *   - Valid DOS stub
 *   - PE file header
 *   - Optional header (PE32+)
 *   - Section table (.text, .rdata, .data)
 *   - Import directory (kernel32.dll: GetStdHandle, WriteFile, ExitProcess)
 *   - x64 machine code that prints to the console
 *
 * Build: gcc -o pe_builder pe_builder.c
 * Usage: ./pe_builder output.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ============================================================
 * PE structure definitions
 * ============================================================ */

/* DOS Header (IMAGE_DOS_HEADER) */
typedef struct {
    uint16_t e_magic;       /* Magic number: "MZ" = 0x5A4D                 */
    uint16_t e_cblp;        /* Bytes on last page of file                   */
    uint16_t e_cp;          /* Pages in file                                */
    uint16_t e_crlc;        /* Relocations                                  */
    uint16_t e_cparhdr;     /* Size of header in paragraphs                 */
    uint16_t e_minalloc;    /* Minimum extra paragraphs needed              */
    uint16_t e_maxalloc;    /* Maximum extra paragraphs needed              */
    uint16_t e_ss;          /* Initial (relative) SS value                  */
    uint16_t e_sp;          /* Initial SP value                             */
    uint16_t e_csum;        /* Checksum                                     */
    uint16_t e_ip;          /* Initial IP value                             */
    uint16_t e_cs;          /* Initial (relative) CS value                  */
    uint16_t e_lfarlc;      /* File address of relocation table             */
    uint16_t e_ovno;        /* Overlay number                               */
    uint16_t e_res[4];      /* Reserved words                               */
    uint16_t e_oemid;       /* OEM identifier (for e_oeminfo)               */
    uint16_t e_oeminfo;     /* OEM information; e_oemid specific            */
    uint16_t e_res2[10];    /* Reserved words                               */
    uint32_t e_lfanew;      /* File address of new exe header               */
} DOSHeader;

/* PE File Header (IMAGE_FILE_HEADER) */
typedef struct {
    uint16_t Machine;               /* Target machine type                  */
    uint16_t NumberOfSections;      /* Number of sections                   */
    uint32_t TimeDateStamp;         /* File creation timestamp              */
    uint32_t PointerToSymbolTable;  /* Offset to COFF symbol table          */
    uint32_t NumberOfSymbols;       /* Number of symbols                    */
    uint16_t SizeOfOptionalHeader;  /* Size of optional header              */
    uint16_t Characteristics;       /* File characteristics flags           */
} FileHeader;

/* Data directory entry */
typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} DataDirectory;

/* Optional Header (IMAGE_OPTIONAL_HEADER64 / PE32+) */
typedef struct {
    uint16_t Magic;                         /* PE32+ magic: 0x020B               */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;           /* RVA of entry point                */
    uint32_t BaseOfCode;
    uint64_t ImageBase;                     /* Preferred load address            */
    uint32_t SectionAlignment;             /* Alignment of sections in memory   */
    uint32_t FileAlignment;                /* Alignment of sections on disk     */
    uint16_t MajorOperatingSystemVersion;  /* Major OS version (set to 4)       */
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;        /* Major subsystem version (set to 4)*/
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;            /* Reserved, must be zero            */
    uint32_t SizeOfImage;                  /* Total size of image in memory     */
    uint32_t SizeOfHeaders;               /* Combined size of all headers      */
    uint32_t CheckSum;
    uint16_t Subsystem;                    /* Subsystem required (console=3)    */
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;                  /* Reserved, must be zero            */
    uint32_t NumberOfRvaAndSizes;          /* Number of data-directory entries  */
    DataDirectory DataDirectory[16];
} OptionalHeader64;

/* Section header (IMAGE_SECTION_HEADER) */
typedef struct {
    char     Name[8];                  /* Section name (8 bytes, null-padded) */
    uint32_t VirtualSize;             /* Size of section when loaded         */
    uint32_t VirtualAddress;          /* RVA of section start                */
    uint32_t SizeOfRawData;           /* Size of section data on disk        */
    uint32_t PointerToRawData;        /* File offset of section data         */
    uint32_t PointerToRelocations;    /* File offset of relocations          */
    uint32_t PointerToLinenumbers;    /* File offset of line numbers         */
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;         /* Section flags                       */
} SectionHeader;

/* Import Directory Table entry (IMAGE_IMPORT_DESCRIPTOR) */
typedef struct {
    uint32_t OriginalFirstThunk;  /* RVA to INT (import name table)         */
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;                /* RVA to DLL name                        */
    uint32_t FirstThunk;         /* RVA to IAT (import address table)      */
} ImportDescriptor;

/* Hint/Name entry */
typedef struct {
    uint16_t Hint;
    char     Name[1];  /* variable-length null-terminated name             */
} ImportByName;

/* ============================================================
 * PE layout constants
 * ============================================================ */

#define IMAGE_DOS_SIGNATURE          0x5A4D      /* "MZ"                  */
#define IMAGE_NT_SIGNATURE           0x00004550  /* "PE\0\0"              */
#define IMAGE_FILE_MACHINE_AMD64     0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE  0x0002
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x020B
#define IMAGE_SUBSYSTEM_WINDOWS_CUI  3           /* Console application   */

/* Section characteristics */
#define IMAGE_SCN_CNT_CODE               0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040
#define IMAGE_SCN_MEM_EXECUTE            0x20000000
#define IMAGE_SCN_MEM_READ               0x40000000
#define IMAGE_SCN_MEM_WRITE              0x80000000

/* Data directory indices */
#define IMAGE_DIRECTORY_ENTRY_IMPORT     1

/* Alignment helpers */
#define FILE_ALIGNMENT    0x200   /* 512 bytes                              */
#define SECTION_ALIGNMENT 0x1000  /* 4 KB                                   */

#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

/* ============================================================
 * Layout plan (all offsets are file offsets unless noted RVA)
 *
 *  0x000  DOS header (64 bytes)
 *  0x040  DOS stub  (64 bytes)  -> e_lfanew = 0x80
 *  0x080  PE signature (4 bytes)
 *  0x084  File header (20 bytes)
 *  0x098  Optional header (240 bytes)
 *  0x188  Section table: 3 × 40 bytes = 120 bytes  -> ends at 0x200
 *  0x200  .text  raw data  (1 sector = 0x200 bytes)
 *  0x400  .rdata raw data  (1 sector = 0x200 bytes) [imports live here]
 *  0x600  .data  raw data  (1 sector = 0x200 bytes)
 *
 *  RVAs (ImageBase = 0x140000000):
 *    .text  RVA = 0x1000
 *    .rdata RVA = 0x2000
 *    .data  RVA = 0x3000
 * ============================================================ */

#define IMAGE_BASE          UINT64_C(0x140000000)

#define HEADERS_SIZE        0x200   /* SizeOfHeaders (file-aligned)          */

#define TEXT_FILE_OFFSET    0x200
#define RDATA_FILE_OFFSET   0x400
#define DATA_FILE_OFFSET    0x600
#define TOTAL_FILE_SIZE     0x800

#define TEXT_RVA            0x1000
#define RDATA_RVA           0x2000
#define DATA_RVA            0x3000

#define RAW_SECTION_SIZE    0x200   /* one FILE_ALIGNMENT block each         */

/* ============================================================
 * .rdata layout (RVAs relative to RDATA_RVA = 0x2000)
 *
 * We pack in this order:
 *  [0x000] Import Descriptor table: 2 entries × 20 bytes = 40 bytes
 *          entry[0] = kernel32.dll
 *          entry[1] = zero terminator
 *  [0x028] IAT (Import Address Table) – 4 entries × 8 bytes = 32 bytes
 *          iat[0] = &GetStdHandle hint/name  (patched by loader)
 *          iat[1] = &WriteFile hint/name
 *          iat[2] = &ExitProcess hint/name
 *          iat[3] = 0  (null terminator)
 *  [0x048] INT (Import Name Table) – same layout as IAT, pre-patch values
 *          int[0] = &GetStdHandle hint/name
 *          int[1] = &WriteFile hint/name
 *          int[2] = &ExitProcess hint/name
 *          int[3] = 0
 *  [0x068] DLL name "kernel32.dll\0"
 *  [0x076] Hint/Name: GetStdHandle
 *  [0x08A] Hint/Name: WriteFile
 *  [0x097] Hint/Name: ExitProcess
 * ============================================================ */

/* Offsets within .rdata */
#define RDATA_OFF_IDIR      0x000   /* import descriptor table               */
#define RDATA_OFF_IAT       0x028   /* IAT                                   */
#define RDATA_OFF_INT       0x048   /* INT                                   */
#define RDATA_OFF_DLLNAME   0x068   /* "kernel32.dll"                        */
#define RDATA_OFF_HN_GSH    0x076   /* hint/name: GetStdHandle               */
#define RDATA_OFF_HN_WF     0x08A   /* hint/name: WriteFile                  */
#define RDATA_OFF_HN_EP     0x097   /* hint/name: ExitProcess                */

/* Corresponding RVAs */
#define RVA_IDIR        (RDATA_RVA + RDATA_OFF_IDIR)
#define RVA_IAT         (RDATA_RVA + RDATA_OFF_IAT)
#define RVA_INT         (RDATA_RVA + RDATA_OFF_INT)
#define RVA_DLLNAME     (RDATA_RVA + RDATA_OFF_DLLNAME)
#define RVA_HN_GSH      (RDATA_RVA + RDATA_OFF_HN_GSH)
#define RVA_HN_WF       (RDATA_RVA + RDATA_OFF_HN_WF)
#define RVA_HN_EP       (RDATA_RVA + RDATA_OFF_HN_EP)

/* IAT slot addresses (VA) used in the code stubs */
#define VA_IAT_GSH  (IMAGE_BASE + RVA_IAT + 0)   /* GetStdHandle slot      */
#define VA_IAT_WF   (IMAGE_BASE + RVA_IAT + 8)   /* WriteFile slot         */
#define VA_IAT_EP   (IMAGE_BASE + RVA_IAT + 16)  /* ExitProcess slot       */

/* .data layout */
#define DATA_OFF_MSG    0x000
#define DATA_OFF_MSGLEN 0x020   /* kept as constant, or just inline         */
static const char HELLO_MSG[]  = "Hello, World!\r\n";
static const uint32_t MSG_LEN  = 15;  /* sizeof(HELLO_MSG)-1                */

/* ============================================================
 * Helper: write a little-endian value into a byte buffer
 * ============================================================ */
static void write_u16(uint8_t *buf, uint16_t v) {
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
}
static void write_u32(uint8_t *buf, uint32_t v) {
    buf[0] =  v        & 0xFF;
    buf[1] = (v >>  8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
}
static void write_u64(uint8_t *buf, uint64_t v) {
    for (int i = 0; i < 8; i++) buf[i] = (v >> (8*i)) & 0xFF;
}

/* ============================================================
 * Section builders – each returns a heap-allocated zeroed
 * RAW_SECTION_SIZE buffer; caller must free().
 * ============================================================ */

/* --- build_dos_header ------------------------------------------
 * Fills the 64-byte IMAGE_DOS_HEADER.
 * e_lfanew = 0x80 (PE sig lives right after the 64-byte stub).
 * --------------------------------------------------------------- */
static void build_dos_header(uint8_t *buf) {
    memset(buf, 0, sizeof(DOSHeader));
    DOSHeader *h = (DOSHeader *)buf;

    h->e_magic    = IMAGE_DOS_SIGNATURE;   /* 'MZ'                          */
    h->e_cblp     = 0x0090;               /* bytes on last page             */
    h->e_cp       = 0x0003;               /* pages in file                  */
    h->e_crlc     = 0x0000;               /* no relocations                 */
    h->e_cparhdr  = 0x0004;               /* header size in paragraphs      */
    h->e_minalloc = 0x0000;
    h->e_maxalloc = 0xFFFF;               /* max paragraphs                 */
    h->e_ss       = 0x0000;
    h->e_sp       = 0x00B8;               /* initial stack pointer          */
    h->e_csum     = 0x0000;
    h->e_ip       = 0x0000;
    h->e_cs       = 0x0000;
    h->e_lfarlc   = 0x0040;               /* relocation table offset        */
    h->e_ovno     = 0x0000;
    h->e_oemid    = 0x0000;
    h->e_oeminfo  = 0x0000;
    h->e_lfanew   = 0x00000080;           /* offset to PE signature         */
}

/* --- build_dos_stub --------------------------------------------
 * Classic "This program cannot be run in DOS mode." stub.
 * 64 bytes, placed at file offset 0x40.
 * --------------------------------------------------------------- */
static void build_dos_stub(uint8_t *buf) {
    /*
     * Real-mode stub that prints the compatibility message and exits.
     * This is the canonical stub used by MSVC/link.exe.
     */
    static const uint8_t stub[64] = {
        0x0E,                   /* push cs                                  */
        0x1F,                   /* pop ds                                   */
        0xBA, 0x0E, 0x00,       /* mov dx, 0x000E  (offset of message)      */
        0xB4, 0x09,             /* mov ah, 9  (DOS print string)            */
        0xCD, 0x21,             /* int 0x21                                 */
        0xB8, 0x01, 0x4C,       /* mov ax, 0x4C01 (terminate with code 1)  */
        0xCD, 0x21,             /* int 0x21                                 */
        /* message at offset 0x0E within stub */
        'T','h','i','s',' ','p','r','o','g','r','a','m',' ','c','a','n','n',
        'o','t',' ','b','e',' ','r','u','n',' ','i','n',' ','D','O','S',' ',
        'm','o','d','e','.','\r','\r','\n','$',
        /* padding to 64 bytes */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    memcpy(buf, stub, 64);
}

/* --- build_pe_signature ----------------------------------------
 * 4-byte "PE\0\0" signature.
 * --------------------------------------------------------------- */
static void build_pe_signature(uint8_t *buf) {
    buf[0] = 'P'; buf[1] = 'E'; buf[2] = 0; buf[3] = 0;
}

/* --- build_file_header -----------------------------------------
 * Fills IMAGE_FILE_HEADER (20 bytes).
 * --------------------------------------------------------------- */
static void build_file_header(uint8_t *buf, uint16_t num_sections) {
    memset(buf, 0, 20);
    FileHeader *fh = (FileHeader *)buf;

    fh->Machine              = IMAGE_FILE_MACHINE_AMD64;
    fh->NumberOfSections     = num_sections;
    fh->TimeDateStamp        = 0x00000000;              /* reproducible     */
    fh->PointerToSymbolTable = 0;
    fh->NumberOfSymbols      = 0;
    fh->SizeOfOptionalHeader = sizeof(OptionalHeader64);
    fh->Characteristics      = IMAGE_FILE_EXECUTABLE_IMAGE |
                               IMAGE_FILE_LARGE_ADDRESS_AWARE;
}

/* --- build_optional_header -------------------------------------
 * Fills IMAGE_OPTIONAL_HEADER64 (240 bytes).
 * --------------------------------------------------------------- */
static void build_optional_header(uint8_t *buf,
                                  uint32_t size_of_image,
                                  uint32_t size_of_code,
                                  uint32_t size_of_idata)
{
    memset(buf, 0, sizeof(OptionalHeader64));
    OptionalHeader64 *oh = (OptionalHeader64 *)buf;

    oh->Magic                       = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    oh->MajorLinkerVersion          = 14;
    oh->MinorLinkerVersion          = 0;
    oh->SizeOfCode                  = size_of_code;
    oh->SizeOfInitializedData       = size_of_idata;
    oh->SizeOfUninitializedData     = 0;
    oh->AddressOfEntryPoint         = TEXT_RVA;         /* start of .text   */
    oh->BaseOfCode                  = TEXT_RVA;
    oh->ImageBase                   = IMAGE_BASE;
    oh->SectionAlignment            = SECTION_ALIGNMENT;
    oh->FileAlignment               = FILE_ALIGNMENT;
    oh->MajorOperatingSystemVersion = 4;                /* as requested     */
    oh->MinorOperatingSystemVersion = 0;
    oh->MajorImageVersion           = 0;
    oh->MinorImageVersion           = 0;
    oh->MajorSubsystemVersion       = 4;                /* as requested     */
    oh->MinorSubsystemVersion       = 0;
    oh->Win32VersionValue           = 0;
    oh->SizeOfImage                 = size_of_image;
    oh->SizeOfHeaders               = HEADERS_SIZE;
    oh->CheckSum                    = 0;
    oh->Subsystem                   = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    oh->DllCharacteristics          = 0x8160; /* NX-compat, no-SEH, dynamic-base, terminal-server-aware */
    oh->SizeOfStackReserve          = 0x100000;
    oh->SizeOfStackCommit           = 0x1000;
    oh->SizeOfHeapReserve           = 0x100000;
    oh->SizeOfHeapCommit            = 0x1000;
    oh->LoaderFlags                 = 0;
    oh->NumberOfRvaAndSizes         = 16;

    /* Import directory */
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = RVA_IDIR;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
            sizeof(ImportDescriptor) * 2;  /* 1 dll + null terminator       */

    /* IAT directory (index 12) */
    oh->DataDirectory[12].VirtualAddress = RVA_IAT;
    oh->DataDirectory[12].Size = 4 * 8;   /* 3 pointers + null             */
}

/* --- build_section_header --------------------------------------
 * Helper to fill one IMAGE_SECTION_HEADER (40 bytes).
 * --------------------------------------------------------------- */
static void build_section_header(uint8_t *buf,
                                 const char *name,
                                 uint32_t virt_size,
                                 uint32_t rva,
                                 uint32_t raw_size,
                                 uint32_t raw_ptr,
                                 uint32_t characteristics)
{
    memset(buf, 0, 40);
    SectionHeader *sh = (SectionHeader *)buf;

    strncpy(sh->Name, name, 8);
    sh->VirtualSize          = virt_size;
    sh->VirtualAddress       = rva;
    sh->SizeOfRawData        = raw_size;
    sh->PointerToRawData     = raw_ptr;
    sh->PointerToRelocations = 0;
    sh->PointerToLinenumbers = 0;
    sh->NumberOfRelocations  = 0;
    sh->NumberOfLinenumbers  = 0;
    sh->Characteristics      = characteristics;
}

/* --- build_text_section ----------------------------------------
 * x64 machine code.
 *
 * Calling convention: Microsoft x64
 *   - First 4 integer args: RCX, RDX, R8, R9
 *   - Caller allocates 32-byte shadow space before call
 *   - Stack must be 16-byte aligned at call site
 *   - RAX = return value
 *
 * Imports (called via IAT):
 *   GetStdHandle(DWORD nStdHandle) -> HANDLE
 *     STD_OUTPUT_HANDLE = -11 = 0xFFFFFFF5
 *
 *   WriteFile(HANDLE hFile,
 *             LPCVOID lpBuffer,
 *             DWORD nNumberOfBytesToWrite,
 *             LPDWORD lpNumberOfBytesWritten,
 *             LPOVERLAPPED lpOverlapped) -> BOOL
 *
 *   ExitProcess(UINT uExitCode)
 *
 * Stack frame layout (grows down):
 *   [RSP+0x00 .. RSP+0x27]  shadow space / local storage
 *   [RSP+0x28]              bytesWritten DWORD (4 bytes, zero-init)
 *
 * Total RSP adjustment: 0x38 (to keep 16-byte alignment after the
 * implicit CALL pushes the return address).
 *
 * Before entry: RSP is 16-byte aligned (Windows loader guarantee for
 * the process entry point in 64-bit mode).  A CALL would misalign it,
 * but we are the entry point itself so no CALL has occurred yet.
 * We sub 0x28 to allocate the shadow space (32 bytes) + 8 bytes for
 * bytesWritten, keeping RSP 16-byte aligned when we make our calls.
 * --------------------------------------------------------------- */
static uint8_t *build_text_section(uint32_t *out_size) {
    uint8_t *buf = (uint8_t *)calloc(RAW_SECTION_SIZE, 1);
    if (!buf) { perror("calloc"); exit(1); }

    uint8_t *p = buf;

    /*
     * Prologue: align stack and reserve shadow + locals.
     *   sub rsp, 0x28
     */
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;

    /*
     * --- GetStdHandle(STD_OUTPUT_HANDLE) ---
     *   mov ecx, 0xFFFFFFF5   ; STD_OUTPUT_HANDLE = -11
     *   call [IAT_GetStdHandle]
     *   mov rbx, rax          ; save handle in RBX (non-volatile)
     */
    /* mov ecx, imm32 */
    *p++ = 0xB9;
    write_u32(p, 0xFFFFFFF5); p += 4;

    /* call qword ptr [RIP + disp32]
     * RIP at the call instruction's *next* byte (after the 6-byte encoding).
     * We compute the disp32 below. */
    uint64_t call_gsh_next_rva; /* RVA of byte after this call              */
    uint64_t call_wf_next_rva;
    uint64_t call_ep_next_rva;

    /* GetStdHandle call: FF 15 <disp32> */
    uint8_t *call_gsh_disp = p + 2;
    call_gsh_next_rva = TEXT_RVA + (p - buf) + 6;
    {
        int64_t disp = (int64_t)(IMAGE_BASE + RVA_IAT + 0) -
                       (int64_t)(IMAGE_BASE + call_gsh_next_rva);
        *p++ = 0xFF; *p++ = 0x15;
        write_u32(p, (uint32_t)(int32_t)disp); p += 4;
    }

    /* mov rbx, rax */
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC3;

    /*
     * --- Zero out bytesWritten on stack ---
     *   mov dword ptr [rsp+0x20], 0
     */
    *p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x20;
    write_u32(p, 0); p += 4;

    /*
     * --- WriteFile(hStdOut, msg, len, &bytesWritten, NULL) ---
     *   mov rcx, rbx                 ; hFile = stdout handle
     *   lea rdx, [RIP + msg_disp]    ; lpBuffer = address of message
     *   mov r8d, MSG_LEN             ; nNumberOfBytesToWrite
     *   lea r9,  [rsp+0x20]         ; lpNumberOfBytesWritten
     *   mov qword ptr [rsp+0x20], 0  ; lpOverlapped = NULL (5th arg on stack)
     *     (we reuse the same slot since we write r9 before the call)
     *
     * Actually the 5th arg goes at [rsp+0x20] and bytesWritten needs its
     * own slot.  Let's use [rsp+0x20] for bytesWritten and place the NULL
     * for lpOverlapped at [rsp+0x28] — but we only allocated 0x28 bytes.
     *
     * Re-think: sub rsp, 0x30 instead to get:
     *   [rsp+0x00..0x1F] shadow space
     *   [rsp+0x20]       bytesWritten (DWORD, 4 bytes)
     *   [rsp+0x28]       lpOverlapped (8 bytes, NULL)
     *
     * But we already emitted sub rsp, 0x28.  Patch it to 0x38 so we keep
     * 16-byte alignment (0x38 % 16 == 8, and entry point RSP is 16-aligned,
     * so RSP-0x38 is also 8-mod-16 — wait, we need RSP%16==0 at call sites).
     *
     * Entry point: RSP is 16-aligned (Windows guarantee for EXE entry).
     * sub rsp, 0x38 -> RSP % 16 = (16 - 8) = 8 — misaligned!
     * sub rsp, 0x28 -> RSP % 16 = 16 - 8 - ... let's just be explicit:
     *   if RSP_entry % 16 == 0, then after sub 0x28: RSP % 16 == 8.
     *   But CALL pushes 8 bytes, so at the called function's entry,
     *   RSP % 16 == 0.  That's what Windows ABI requires.
     *
     * So sub 0x28 is correct.  For the 5th argument (lpOverlapped = NULL),
     * the ABI says it goes at [RSP+0x20] from the CALLER's RSP, i.e. the
     * RSP value when we execute the CALL instruction.  Our RSP at CALL is
     * (RSP_entry - 0x28).  So:
     *   shadow: [RSP+0x00 .. RSP+0x1F]
     *   arg5:   [RSP+0x20]  <- this is what we need for lpOverlapped
     *
     * But we also need storage for bytesWritten (the DWORD that WriteFile
     * writes to).  We can place it in unused shadow space, e.g. [RSP+0x10].
     * Shadow space is ours to use.
     *
     * Final plan (with sub rsp, 0x28):
     *   rcx = hFile
     *   rdx = &message
     *   r8d = length
     *   r9  = &[rsp+0x10]   (bytesWritten, in our shadow space)
     *   [rsp+0x20] = 0      (lpOverlapped = NULL, 5th arg)
     */

    /* mov rcx, rbx */
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xD9;

    /* lea rdx, [RIP + disp]  -- points to message in .data */
    {
        /* RIP after this instruction = TEXT_RVA + (p - buf) + 7 */
        uint64_t next_rip = TEXT_RVA + (p - buf) + 7;
        int64_t disp = (int64_t)(DATA_RVA + DATA_OFF_MSG) - (int64_t)next_rip;
        *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
        write_u32(p, (uint32_t)(int32_t)disp); p += 4;
    }

    /* mov r8d, MSG_LEN */
    *p++ = 0x41; *p++ = 0xB8;
    write_u32(p, MSG_LEN); p += 4;

    /* lea r9, [rsp+0x10]  (bytesWritten storage in shadow space) */
    *p++ = 0x4C; *p++ = 0x8D; *p++ = 0x4C; *p++ = 0x24; *p++ = 0x10;

    /* zero [rsp+0x10] first (bytesWritten = 0) */
    *p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x10;
    write_u32(p, 0); p += 4;

    /* mov qword ptr [rsp+0x20], 0  (lpOverlapped = NULL) */
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x20;
    write_u32(p, 0); p += 4;

    /* call qword ptr [RIP + disp32]  -- WriteFile */
    {
        call_wf_next_rva = TEXT_RVA + (p - buf) + 6;
        int64_t disp = (int64_t)(IMAGE_BASE + RVA_IAT + 8) -
                       (int64_t)(IMAGE_BASE + call_wf_next_rva);
        *p++ = 0xFF; *p++ = 0x15;
        write_u32(p, (uint32_t)(int32_t)disp); p += 4;
    }

    /*
     * --- ExitProcess(0) ---
     *   xor ecx, ecx
     *   call [IAT_ExitProcess]
     */
    *p++ = 0x31; *p++ = 0xC9;   /* xor ecx, ecx */

    {
        call_ep_next_rva = TEXT_RVA + (p - buf) + 6;
        int64_t disp = (int64_t)(IMAGE_BASE + RVA_IAT + 16) -
                       (int64_t)(IMAGE_BASE + call_ep_next_rva);
        *p++ = 0xFF; *p++ = 0x15;
        write_u32(p, (uint32_t)(int32_t)disp); p += 4;
    }

    /* Unreachable but add an INT3 for safety */
    *p++ = 0xCC;

    /* Suppress unused variable warnings */
    (void)call_gsh_disp;
    (void)call_gsh_next_rva;
    (void)call_wf_next_rva;
    (void)call_ep_next_rva;

    *out_size = (uint32_t)(p - buf);
    return buf;
}

/* --- build_rdata_section ---------------------------------------
 * Builds the .rdata section containing:
 *   - Import Descriptor Table
 *   - IAT (patched by loader to function VAs)
 *   - INT (original thunks)
 *   - DLL name string
 *   - Hint/Name entries
 * --------------------------------------------------------------- */
static uint8_t *build_rdata_section(uint32_t *out_virtual_size) {
    uint8_t *buf = (uint8_t *)calloc(RAW_SECTION_SIZE, 1);
    if (!buf) { perror("calloc"); exit(1); }

    /* ---- Import Descriptor Table ---- */
    /* Entry 0: kernel32.dll */
    ImportDescriptor *idir = (ImportDescriptor *)(buf + RDATA_OFF_IDIR);
    idir[0].OriginalFirstThunk = RVA_INT;       /* points to INT             */
    idir[0].TimeDateStamp      = 0;
    idir[0].ForwarderChain     = 0xFFFFFFFF;
    idir[0].Name               = RVA_DLLNAME;   /* "kernel32.dll"            */
    idir[0].FirstThunk         = RVA_IAT;       /* points to IAT             */
    /* Entry 1: null terminator (already zero from calloc) */

    /* ---- IAT ---- */
    uint64_t *iat = (uint64_t *)(buf + RDATA_OFF_IAT);
    iat[0] = RVA_HN_GSH;    /* -> hint/name GetStdHandle                    */
    iat[1] = RVA_HN_WF;     /* -> hint/name WriteFile                       */
    iat[2] = RVA_HN_EP;     /* -> hint/name ExitProcess                     */
    iat[3] = 0;              /* null terminator                              */

    /* ---- INT (same initial values as IAT) ---- */
    uint64_t *intt = (uint64_t *)(buf + RDATA_OFF_INT);
    intt[0] = RVA_HN_GSH;
    intt[1] = RVA_HN_WF;
    intt[2] = RVA_HN_EP;
    intt[3] = 0;

    /* ---- DLL name ---- */
    const char *dllname = "KERNEL32.dll";
    memcpy(buf + RDATA_OFF_DLLNAME, dllname, strlen(dllname) + 1);

    /* ---- Hint/Name: GetStdHandle ---- */
    buf[RDATA_OFF_HN_GSH + 0] = 0x00;  /* hint low  */
    buf[RDATA_OFF_HN_GSH + 1] = 0x00;  /* hint high */
    memcpy(buf + RDATA_OFF_HN_GSH + 2, "GetStdHandle", 12 + 1);

    /* ---- Hint/Name: WriteFile ---- */
    buf[RDATA_OFF_HN_WF + 0] = 0x00;
    buf[RDATA_OFF_HN_WF + 1] = 0x00;
    memcpy(buf + RDATA_OFF_HN_WF + 2, "WriteFile", 9 + 1);

    /* ---- Hint/Name: ExitProcess ---- */
    buf[RDATA_OFF_HN_EP + 0] = 0x00;
    buf[RDATA_OFF_HN_EP + 1] = 0x00;
    memcpy(buf + RDATA_OFF_HN_EP + 2, "ExitProcess", 11 + 1);

    *out_virtual_size = RDATA_OFF_HN_EP + 2 + 11 + 1;
    return buf;
}

/* --- build_data_section ----------------------------------------
 * .data: read/write initialised data.
 * Contains: "Hello, World!\r\n"
 * --------------------------------------------------------------- */
static uint8_t *build_data_section(uint32_t *out_virtual_size) {
    uint8_t *buf = (uint8_t *)calloc(RAW_SECTION_SIZE, 1);
    if (!buf) { perror("calloc"); exit(1); }

    memcpy(buf + DATA_OFF_MSG, HELLO_MSG, MSG_LEN);

    *out_virtual_size = DATA_OFF_MSG + MSG_LEN;
    return buf;
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char **argv) {
    const char *outfile = (argc > 1) ? argv[1] : "hello.exe";

    printf("PE Builder - generating 64-bit PE: %s\n", outfile);

    /* Allocate output buffer (entire file) */
    uint8_t *image = (uint8_t *)calloc(TOTAL_FILE_SIZE, 1);
    if (!image) { perror("calloc"); return 1; }

    /* ---- DOS Header (offset 0x00, 64 bytes) ---- */
    build_dos_header(image + 0x00);

    /* ---- DOS Stub (offset 0x40, 64 bytes) ---- */
    build_dos_stub(image + 0x40);

    /* ---- PE Signature (offset 0x80, 4 bytes) ---- */
    build_pe_signature(image + 0x80);

    /* ---- File Header (offset 0x84, 20 bytes) ---- */
    build_file_header(image + 0x84, 3 /* .text, .rdata, .data */);

    /* ---- Optional Header (offset 0x98, 240 bytes) ---- */
    /*
     * SizeOfImage = next section-aligned boundary after last section:
     *   .data ends at RVA 0x3000 + SECTION_ALIGNMENT = 0x4000
     */
    uint32_t size_of_image = ALIGN_UP(DATA_RVA + SECTION_ALIGNMENT,
                                      SECTION_ALIGNMENT);
    build_optional_header(image + 0x98,
                          size_of_image,
                          RAW_SECTION_SIZE,   /* SizeOfCode      */
                          RAW_SECTION_SIZE * 2 /* SizeOfInitData */);

    /* ---- Section Headers (offset 0x188, 3×40 = 120 bytes) ---- */
    uint8_t *shtab = image + 0x188;

    build_section_header(shtab + 0,
        ".text",
        RAW_SECTION_SIZE,   /* VirtualSize   */
        TEXT_RVA,
        RAW_SECTION_SIZE,
        TEXT_FILE_OFFSET,
        IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);

    build_section_header(shtab + 40,
        ".rdata",
        RAW_SECTION_SIZE,
        RDATA_RVA,
        RAW_SECTION_SIZE,
        RDATA_FILE_OFFSET,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);

    build_section_header(shtab + 80,
        ".data",
        RAW_SECTION_SIZE,
        DATA_RVA,
        RAW_SECTION_SIZE,
        DATA_FILE_OFFSET,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);

    /* ---- .text section ---- */
    uint32_t text_code_size = 0;
    uint8_t *text_buf = build_text_section(&text_code_size);
    memcpy(image + TEXT_FILE_OFFSET, text_buf, RAW_SECTION_SIZE);
    free(text_buf);

    /* ---- .rdata section ---- */
    uint32_t rdata_vsize = 0;
    uint8_t *rdata_buf = build_rdata_section(&rdata_vsize);
    memcpy(image + RDATA_FILE_OFFSET, rdata_buf, RAW_SECTION_SIZE);
    free(rdata_buf);

    /* Update .rdata VirtualSize in section header */
    SectionHeader *rdata_sh = (SectionHeader *)(shtab + 40);
    rdata_sh->VirtualSize = rdata_vsize;

    /* ---- .data section ---- */
    uint32_t data_vsize = 0;
    uint8_t *data_buf = build_data_section(&data_vsize);
    memcpy(image + DATA_FILE_OFFSET, data_buf, RAW_SECTION_SIZE);
    free(data_buf);

    /* Update .data VirtualSize in section header */
    SectionHeader *data_sh = (SectionHeader *)(shtab + 80);
    data_sh->VirtualSize = data_vsize;

    /* Update .text VirtualSize with actual code size */
    SectionHeader *text_sh = (SectionHeader *)(shtab + 0);
    text_sh->VirtualSize = text_code_size;

    /* ---- Write file ---- */
    FILE *fp = fopen(outfile, "wb");
    if (!fp) { perror("fopen"); free(image); return 1; }

    size_t written = fwrite(image, 1, TOTAL_FILE_SIZE, fp);
    fclose(fp);
    free(image);

    if (written != TOTAL_FILE_SIZE) {
        fprintf(stderr, "Error: only wrote %zu of %d bytes\n",
                written, TOTAL_FILE_SIZE);
        return 1;
    }

    printf("Success! Wrote %d bytes to %s\n", TOTAL_FILE_SIZE, outfile);
    printf("\nFile layout:\n");
    printf("  0x%04X  DOS header\n", 0);
    printf("  0x%04X  DOS stub\n", 0x40);
    printf("  0x%04X  PE signature + headers\n", 0x80);
    printf("  0x%04X  .text  (code, RVA=0x%04X)\n",
           TEXT_FILE_OFFSET, TEXT_RVA);
    printf("  0x%04X  .rdata (imports, RVA=0x%04X)\n",
           RDATA_FILE_OFFSET, RDATA_RVA);
    printf("  0x%04X  .data  (strings, RVA=0x%04X)\n",
           DATA_FILE_OFFSET, DATA_RVA);
    printf("  0x%04X  end of file\n", TOTAL_FILE_SIZE);
    printf("\nTarget: x86-64 Windows, console subsystem\n");
    printf("ImageBase:  0x%llX\n", (unsigned long long)IMAGE_BASE);
    printf("EntryPoint: RVA 0x%04X\n", TEXT_RVA);

    return 0;
}
