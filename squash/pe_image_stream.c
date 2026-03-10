
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

//static const uint8_t dos_stub[] = {
//    0x0E,                   // push cs
//    0x1F,                   // pop ds
//    0xBA,0x0E,0x00,         // mov dx,0x000E
//    0xB4,0x09,              // mov ah,9
//    0xCD,0x21,              // int 21h
//    0xB8,0x01,0x4C,         // mov ax,4C01h
//    0xCD,0x21,              // int 21h
//
//    // message:
//    'T','h','i','s',' ',
//    'p','r','o','g','r','a','m',' ',
//    'c','a','n','n','o','t',' ',
//    'b','e',' ','r','u','n',' ',
//    'i','n',' ','D','O','S',' ',
//    'm','o','d','e','.',
//    '\r','\n','$'
//};

//uint32_t build_imports(uint8_t* buf, uint32_t rvaBase)
//{
//    memset(buf, 0, 512);
//
//    const char* dll = "kernel32.dll";
//    const char* fn[] = {
//        "GetStdHandle",
//        "WriteFile",
//        "ExitProcess"
//    };
//
//    uint32_t off = 0;
//
//    IMPDESC* d = (IMPDESC*)buf;
//    off += sizeof(IMPDESC) * 2;
//
//    uint32_t iltRVA = rvaBase + off;
//    uint64_t* ilt = (uint64_t*)(buf + off);
//    off += 8 * (3 + 1);
//
//    uint32_t iatRVA = rvaBase + off;
//    uint64_t* iat = (uint64_t*)(buf + off);
//    off += 8 * (3 + 1);
//
//    uint32_t nameRVA = rvaBase + off;
//    strcpy((char*)(buf + off), dll);
//    off += strlen(dll) + 1;
//
//    uint32_t hnRVA[3];
//
//    for (int i = 0; i < 3; i++) {
//        off = ALIGN(off, 2);
//        hnRVA[i] = rvaBase + off;
//
//        *(uint16_t*)(buf + off) = 0;
//        strcpy((char*)(buf + off + 2), fn[i]);
//
//        off += 2 + strlen(fn[i]) + 1;
//    }
//
//    for (int i = 0; i < 3; i++) {
//        ilt[i] = hnRVA[i];
//        iat[i] = hnRVA[i];
//    }
//
//    d->OFT = iltRVA;
//    d->Name = nameRVA;
//    d->FT = iatRVA;
//
//    return ALIGN(off, 16);
//}

/* ===========================================================================
 * SHARED UTILITY — little-endian write helpers
 * ========================================================================= */

static void write_u16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
}

static void write_u32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void write_u64(uint8_t* p, uint64_t v) {
	int i;
	for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* ===========================================================================
 * SHARED SECTION BUILDERS
 * These fill structures whose layout is identical in PE32 and PE32+.
 * ========================================================================= */

 /* ---------------------------------------------------------------------------
  * build_dos_header()
  *
  * Fills IMAGE_DOS_HEADER (64 bytes) with canonical MSVC link.exe defaults.
  * e_lfanew = 0x80 — the PE signature begins at file offset 0x80 for both
  * the 32-bit and 64-bit layouts (DOS header 64B + DOS stub 64B = 128 = 0x80).
  *
  * Shared by: build_pe32() and build_pe64()
  * ------------------------------------------------------------------------- */
static void build_dos_header(uint8_t* buf) {
	DOSHeader* h;
	memset(buf, 0, sizeof(DOSHeader));
	h = (DOSHeader*)buf;

	h->e_magic = IMAGE_DOS_SIGNATURE; /* 'MZ' = 0x5A4D                    */
	h->e_cblp = 0x0090;             /* bytes on last 512-byte page       */
	h->e_cp = 0x0003;             /* total 512-byte pages              */
	h->e_crlc = 0x0000;             /* no relocations                    */
	h->e_cparhdr = 0x0004;             /* header = 4 paragraphs = 64 bytes  */
	h->e_minalloc = 0x0000;
	h->e_maxalloc = 0xFFFF;
	h->e_ss = 0x0000;
	h->e_sp = 0x00B8;
	h->e_csum = 0x0000;
	h->e_ip = 0x0000;
	h->e_cs = 0x0000;
	h->e_lfarlc = 0x0040;             /* reloc table at byte 64            */
	h->e_lfanew = 0x00000080;         /* PE sig at file offset 0x80        */
}

/* ---------------------------------------------------------------------------
 * build_dos_stub()
 *
 * Writes the 64-byte real-mode stub at file offset 0x40.
 * Prints "This program cannot be run in DOS mode.\r\r\n" and exits 1.
 * Under Windows NT+ this code is never executed.
 *
 * Shared by: build_pe32() and build_pe64()
 * ------------------------------------------------------------------------- */
static void build_dos_stub(uint8_t* buf) {
	static const uint8_t stub[64] = {
		0x0E,                       /* push cs                               */
		0x1F,                       /* pop  ds                               */
		0xBA, 0x0E, 0x00,           /* mov  dx, 0x000E  (offset of message)  */
		0xB4, 0x09,                 /* mov  ah, 9       (DOS print string)   */
		0xCD, 0x21,                 /* int  0x21                             */
		0xB8, 0x01, 0x4C,           /* mov  ax, 0x4C01  (exit code 1)       */
		0xCD, 0x21,                 /* int  0x21                             */
		/* message starts at stub+0x0E */
		'T','h','i','s',' ','p','r','o','g','r','a','m',' ','c','a','n','n',
		'o','t',' ','b','e',' ','r','u','n',' ','i','n',' ','D','O','S',' ',
		'm','o','d','e','.','\r','\r','\n','$',
		/* padding to reach exactly 64 bytes */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	memcpy(buf, stub, 64);
}

/* ---------------------------------------------------------------------------
 * build_pe_signature()
 *
 * Writes the 4-byte "PE\0\0" COFF signature at e_lfanew (0x80).
 *
 * Shared by: build_pe32() and build_pe64()
 * ------------------------------------------------------------------------- */
static void build_pe_signature(uint8_t* buf) {
	buf[0] = 'P'; buf[1] = 'E'; buf[2] = 0x00; buf[3] = 0x00;
}

/* ---------------------------------------------------------------------------
 * build_file_header()
 *
 * Fills IMAGE_FILE_HEADER (20 bytes).
 * Arguments vary between 32-bit and 64-bit builds:
 *   machine           0x014C (I386) for 32-bit, 0x8664 (AMD64) for 64-bit
 *   num_sections      3 for both
 *   opt_hdr_size      sizeof(OptionalHeader32)=224  or  sizeof(OptionalHeader64)=240
 *   characteristics   includes IMAGE_FILE_32BIT_MACHINE for 32-bit only
 *
 * Shared by: build_pe32() and build_pe64() (with different arguments)
 * ------------------------------------------------------------------------- */
static void build_file_header(uint8_t* buf,
	uint16_t  machine,
	uint16_t  num_sections,
	uint16_t  opt_hdr_size,
	uint16_t  characteristics) {
	FileHeader* fh;
	memset(buf, 0, sizeof(FileHeader));
	fh = (FileHeader*)buf;

	fh->Machine = machine;
	fh->NumberOfSections = num_sections;
	fh->TimeDateStamp = 0x00000000;  /* zeroed for reproducibility    */
	fh->PointerToSymbolTable = 0;
	fh->NumberOfSymbols = 0;
	fh->SizeOfOptionalHeader = opt_hdr_size;
	fh->Characteristics = characteristics;
}

/* ---------------------------------------------------------------------------
 * build_section_header()
 *
 * Fills one IMAGE_SECTION_HEADER (40 bytes).
 * The struct layout is identical in PE32 and PE32+; only the values differ.
 *
 * Shared by: build_pe32() and build_pe64() (called once per section each)
 * ------------------------------------------------------------------------- */
static void build_section_header(uint8_t* buf,
	const char* name,
	uint32_t    virt_size,
	uint32_t    rva,
	uint32_t    raw_size,
	uint32_t    raw_ptr,
	uint32_t    characteristics) {
	SectionHeader* sh;
	memset(buf, 0, sizeof(SectionHeader));
	sh = (SectionHeader*)buf;

	strncpy(sh->Name, name, 8);
	sh->VirtualSize = virt_size;
	sh->VirtualAddress = rva;
	sh->SizeOfRawData = raw_size;
	sh->PointerToRawData = raw_ptr;
	sh->PointerToRelocations = 0;
	sh->PointerToLinenumbers = 0;
	sh->NumberOfRelocations = 0;
	sh->NumberOfLinenumbers = 0;
	sh->Characteristics = characteristics;
}

/* ===========================================================================
 * 32-BIT (PE32 / x86) DEDICATED SECTION BUILDERS
 *
 * File layout:
 *   0x000  DOS header            (64 B)
 *   0x040  DOS stub              (64 B)
 *   0x080  PE signature          ( 4 B)
 *   0x084  FILE HEADER           (20 B)
 *   0x098  OPTIONAL HEADER 32   (224 B)   <- PE32
 *   0x178  SECTION TABLE        (120 B)   3 × 40 B
 *   0x200  .text  raw data      (512 B)   x86 machine code
 *   0x400  .rdata raw data      (512 B)   imports (4-byte thunks)
 *   0x600  .data  raw data      (512 B)   "Hello, World!\r\n"
 *   0x800  EOF
 *
 * RVAs:   .text=0x1000   .rdata=0x2000   .data=0x3000
 * ImageBase: 0x00400000
 *
 * .rdata internal layout (32-bit):
 *   [0x000]  ImportDescriptor[0]  kernel32  (20 B)
 *   [0x014]  ImportDescriptor[1]  null      (20 B)
 *   [0x028]  IAT  4 × uint32_t = 16 B       <- 4-byte thunks for PE32
 *   [0x038]  INT  4 × uint32_t = 16 B
 *   [0x048]  DLL name "KERNEL32.dll\0"
 *   [0x056]  Hint/Name: GetStdHandle
 *   [0x064]  Hint/Name: WriteFile
 *   [0x070]  Hint/Name: ExitProcess
 * ========================================================================= */

#define IMAGEBASE_32         0x00400000u
#define TEXT_FILE_OFF_32     0x200
#define RDATA_FILE_OFF_32    0x400
#define DATA_FILE_OFF_32     0x600
#define TOTAL_SIZE_32        0x800
#define TEXT_RVA_32          0x1000u
#define RDATA_RVA_32         0x2000u
#define DATA_RVA_32          0x3000u

 /* Offsets within .rdata (32-bit) */
#define R32_OFF_IDIR         0x000
#define R32_OFF_IAT          0x028
#define R32_OFF_INT          0x038
#define R32_OFF_DLLNAME      0x048
#define R32_OFF_HN_GSH       0x056
#define R32_OFF_HN_WF        0x064
#define R32_OFF_HN_EP        0x070

/* Corresponding RVAs (32-bit) */
#define R32_RVA_IDIR         (RDATA_RVA_32 + R32_OFF_IDIR)
#define R32_RVA_IAT          (RDATA_RVA_32 + R32_OFF_IAT)
#define R32_RVA_INT          (RDATA_RVA_32 + R32_OFF_INT)
#define R32_RVA_DLLNAME      (RDATA_RVA_32 + R32_OFF_DLLNAME)
#define R32_RVA_HN_GSH       (RDATA_RVA_32 + R32_OFF_HN_GSH)
#define R32_RVA_HN_WF        (RDATA_RVA_32 + R32_OFF_HN_WF)
#define R32_RVA_HN_EP        (RDATA_RVA_32 + R32_OFF_HN_EP)

/* Absolute VAs used in x86 indirect CALL instructions */
#define VA32_IAT_GSH         (IMAGEBASE_32 + R32_RVA_IAT + 0)  /* GetStdHandle */
#define VA32_IAT_WF          (IMAGEBASE_32 + R32_RVA_IAT + 4)  /* WriteFile    */
#define VA32_IAT_EP          (IMAGEBASE_32 + R32_RVA_IAT + 8)  /* ExitProcess  */
#define VA32_MSG             (IMAGEBASE_32 + DATA_RVA_32  + 0)  /* message VA   */

/* ---------------------------------------------------------------------------
 * build_optional_header_32()
 *
 * Fills IMAGE_OPTIONAL_HEADER (PE32 variant, 224 bytes).
 *
 * Key PE32-specific details:
 *   Magic       = 0x010B  (not 0x020B)
 *   BaseOfData  = RDATA_RVA (this field does not exist in PE32+)
 *   ImageBase   = uint32_t  (0x00400000 is the standard Win32 base)
 *   Stack/heap  = uint32_t  (half the width of PE32+)
 *
 * Dedicated to: build_pe32()
 * ------------------------------------------------------------------------- */
static void build_optional_header_32(uint8_t* buf,
	uint32_t  size_of_image,
	uint32_t  size_of_code,
	uint32_t  size_of_idata) {
	OptionalHeader32* oh;
	memset(buf, 0, sizeof(OptionalHeader32));
	oh = (OptionalHeader32*)buf;

	oh->Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	oh->MajorLinkerVersion = 14;
	oh->MinorLinkerVersion = 0;
	oh->SizeOfCode = size_of_code;
	oh->SizeOfInitializedData = size_of_idata;
	oh->SizeOfUninitializedData = 0;
	oh->AddressOfEntryPoint = TEXT_RVA_32;
	oh->BaseOfCode = TEXT_RVA_32;
	oh->BaseOfData = RDATA_RVA_32;   /* PE32 exclusive field */
	oh->ImageBase = IMAGEBASE_32;
	oh->SectionAlignment = SECTION_ALIGNMENT;
	oh->FileAlignment = FILE_ALIGNMENT;
	oh->MajorOperatingSystemVersion = 4;
	oh->MinorOperatingSystemVersion = 0;
	oh->MajorImageVersion = 0;
	oh->MinorImageVersion = 0;
	oh->MajorSubsystemVersion = 4;
	oh->MinorSubsystemVersion = 0;
	oh->Win32VersionValue = 0;
	oh->SizeOfImage = size_of_image;
	oh->SizeOfHeaders = 0x200;
	oh->CheckSum = 0;
	oh->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
	oh->DllCharacteristics = 0x8100;// keeps NX-compat, removes DYNAMIC_BASE. //oh->DllCharacteristics = 0x8540;
	oh->SizeOfStackReserve = 0x100000;
	oh->SizeOfStackCommit = 0x1000;
	oh->SizeOfHeapReserve = 0x100000;
	oh->SizeOfHeapCommit = 0x1000;
	oh->LoaderFlags = 0;
	oh->NumberOfRvaAndSizes = 16;

	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = R32_RVA_IDIR;
	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
		(uint32_t)(sizeof(ImportDescriptor) * 2);

	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = R32_RVA_IAT;
	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 4 * 4;
}

/* ---------------------------------------------------------------------------
 * build_text_section_32()
 *
 * Emits x86 (32-bit) machine code using the Windows stdcall convention:
 *   - Arguments pushed right-to-left before CALL
 *   - Callee cleans the stack (RETN n)
 *   - No shadow space required
 *   - Indirect calls use absolute 32-bit VA:  FF 15 <va32>
 *
 * Call sequence:
 *   1. push STD_OUTPUT_HANDLE (-11)
 *      call [IAT:GetStdHandle]          -> EAX = hStdOut
 *      mov  ebx, eax                    (save; EBX is callee-saved)
 *
 *   2. push 0                           (bytesWritten local, ESP = &bw)
 *      mov  ecx, esp                    (ECX = &bytesWritten)
 *      push 0                           arg5: lpOverlapped = NULL
 *      push ecx                         arg4: lpNumberOfBytesWritten
 *      push MSG_LEN                     arg3
 *      push VA_message                  arg2
 *      push ebx                         arg1: hFile
 *      call [IAT:WriteFile]             WriteFile cleans its 5 args (stdcall)
 *      add  esp, 4                      clean up our local bytesWritten slot
 *
 *   3. push 0                           uExitCode = 0
 *      call [IAT:ExitProcess]           does not return
 *
 * Dedicated to: build_pe32()
 * ------------------------------------------------------------------------- */
static uint8_t* build_text_section_32(uint32_t* out_size) {
	uint8_t* buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	uint8_t* p;
	if (!buf) { perror("calloc"); exit(1); }
	p = buf;

	/* --- GetStdHandle(STD_OUTPUT_HANDLE) --- */
	*p++ = 0x68;                         /* push imm32                       */
	write_u32(p, 0xFFFFFFF5); p += 4;    /*   STD_OUTPUT_HANDLE = -11        */
	*p++ = 0xFF; *p++ = 0x15;            /* call dword ptr [abs_va]          */
	write_u32(p, VA32_IAT_GSH); p += 4;
	*p++ = 0x89; *p++ = 0xC3;            /* mov ebx, eax  (save handle)      */

	/* --- Allocate bytesWritten on stack, get its address into ECX --- */
	*p++ = 0x6A; *p++ = 0x00;            /* push 0  (bytesWritten slot)      */
	*p++ = 0x89; *p++ = 0xE1;            /* mov ecx, esp                     */

	/* --- WriteFile(hFile, lpBuf, nBytes, &bw, NULL) --- */
	*p++ = 0x6A; *p++ = 0x00;            /* push 0     arg5: lpOverlapped    */
	*p++ = 0x51;                         /* push ecx   arg4: &bytesWritten   */
	*p++ = 0x68;                         /* push imm32 arg3: nBytes          */
	write_u32(p, MSG_LEN); p += 4;
	*p++ = 0x68;                         /* push imm32 arg2: lpBuffer        */
	write_u32(p, VA32_MSG); p += 4;
	*p++ = 0x53;                         /* push ebx   arg1: hFile           */
	*p++ = 0xFF; *p++ = 0x15;            /* call dword ptr [abs_va]          */
	write_u32(p, VA32_IAT_WF); p += 4;
	*p++ = 0x83; *p++ = 0xC4; *p++ = 0x04; /* add esp, 4  clean up bw slot  */

	/* --- ExitProcess(0) --- */
	*p++ = 0x6A; *p++ = 0x00;            /* push 0                           */
	*p++ = 0xFF; *p++ = 0x15;            /* call dword ptr [abs_va]          */
	write_u32(p, VA32_IAT_EP); p += 4;

	*p++ = 0xCC;                         /* INT3 — unreachable safety guard  */

	*out_size = (uint32_t)(p - buf);
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_rdata_section_32()
 *
 * Builds the .rdata section for a 32-bit PE.
 * IAT and INT thunks are 4 bytes (uint32_t) — one RVA per import.
 * The loader patches each IAT entry with the resolved 32-bit function VA.
 *
 * Dedicated to: build_pe32()
 * ------------------------------------------------------------------------- */
static uint8_t* build_rdata_section_32(uint32_t* out_vsize) {
	uint8_t* buf;
	ImportDescriptor* idir;
	uint32_t* iat, * intt;

	buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	if (!buf) { perror("calloc"); exit(1); }

	/* Import Descriptor Table: entry 0 = kernel32, entry 1 = null */
	idir = (ImportDescriptor*)(buf + R32_OFF_IDIR);
	idir[0].OriginalFirstThunk = R32_RVA_INT;
	idir[0].TimeDateStamp = 0;
	idir[0].ForwarderChain = 0xFFFFFFFF;
	idir[0].Name = R32_RVA_DLLNAME;
	idir[0].FirstThunk = R32_RVA_IAT;

	/* IAT — 4-byte thunks (PE32) */
	iat = (uint32_t*)(buf + R32_OFF_IAT);
	iat[0] = R32_RVA_HN_GSH;
	iat[1] = R32_RVA_HN_WF;
	iat[2] = R32_RVA_HN_EP;
	iat[3] = 0;

	/* INT — identical initial values */
	intt = (uint32_t*)(buf + R32_OFF_INT);
	intt[0] = R32_RVA_HN_GSH;
	intt[1] = R32_RVA_HN_WF;
	intt[2] = R32_RVA_HN_EP;
	intt[3] = 0;

	/* DLL name */
	memcpy(buf + R32_OFF_DLLNAME, "KERNEL32.dll", 12 + 1);

	/* Hint/Name entries: 2-byte hint (0) + function name + null */
	buf[R32_OFF_HN_GSH + 0] = 0x00; buf[R32_OFF_HN_GSH + 1] = 0x00;
	memcpy(buf + R32_OFF_HN_GSH + 2, "GetStdHandle", 12 + 1);

	buf[R32_OFF_HN_WF + 0] = 0x00; buf[R32_OFF_HN_WF + 1] = 0x00;
	memcpy(buf + R32_OFF_HN_WF + 2, "WriteFile", 9 + 1);

	buf[R32_OFF_HN_EP + 0] = 0x00; buf[R32_OFF_HN_EP + 1] = 0x00;
	memcpy(buf + R32_OFF_HN_EP + 2, "ExitProcess", 11 + 1);

	*out_vsize = R32_OFF_HN_EP + 2 + 11 + 1;
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_data_section_32()
 *
 * .data section for the 32-bit PE.
 * Message VA = 0x00400000 + 0x3000 = 0x00403000.
 *
 * Dedicated to: build_pe32()
 * ------------------------------------------------------------------------- */
static uint8_t* build_data_section_32(uint32_t* out_vsize) {
	uint8_t* buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	if (!buf) { perror("calloc"); exit(1); }
	memcpy(buf, HELLO_MSG, MSG_LEN);
	*out_vsize = MSG_LEN;
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_pe32()
 *
 * Orchestrates all 32-bit section builders into a complete PE32 file.
 *
 * Header offsets (shared with all PE files beginning at e_lfanew = 0x80):
 *   0x080  PE sig
 *   0x084  FileHeader         (20 B)
 *   0x098  OptionalHeader32  (224 B)    <- 0x098 + 0xE0 = 0x178
 *   0x178  SectionTable       (3×40 B)  <- 0x178 + 0x78 = 0x1F0, padded to 0x200
 *
 * Dedicated to: 32-bit PE generation
 * ------------------------------------------------------------------------- */
int build_pe32(const char* outfile) {
	uint8_t* image, * shtab;
	uint8_t* text_buf, * rdata_buf, * data_buf;
	uint32_t      text_vsize, rdata_vsize, data_vsize;
	uint32_t      size_of_image;
	SectionHeader* sh;
	FILE* fp;
	size_t written;

	printf("Building 32-bit PE (PE32 / x86): %s\n", outfile);

	image = (uint8_t*)calloc(TOTAL_SIZE_32, 1);
	if (!image) { perror("calloc"); return 1; }

	build_dos_header(image + 0x000);
	build_dos_stub(image + 0x040);
	build_pe_signature(image + 0x080);

	/* FileHeader: x86, 3 sections, optHdrSize=224, 32-bit exe */
	build_file_header(image + 0x084,
		IMAGE_FILE_MACHINE_I386,
		3,
		(uint16_t)sizeof(OptionalHeader32),   /* 224 */
		IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE);

	/* OptionalHeader32 at 0x98 */
	size_of_image = ALIGN_UP(DATA_RVA_32 + SECTION_ALIGNMENT, SECTION_ALIGNMENT);
	build_optional_header_32(image + 0x098,
		size_of_image,
		RAW_SECTION_SIZE,
		RAW_SECTION_SIZE * 2);

	/* Section table at 0x178  (0x98 + 0xE0 = 0x178) */
	shtab = image + 0x178;
	build_section_header(shtab + 0, ".text",
		RAW_SECTION_SIZE, TEXT_RVA_32, RAW_SECTION_SIZE, TEXT_FILE_OFF_32,
		IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
	build_section_header(shtab + 40, ".rdata",
		RAW_SECTION_SIZE, RDATA_RVA_32, RAW_SECTION_SIZE, RDATA_FILE_OFF_32,
		IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);
	build_section_header(shtab + 80, ".data",
		RAW_SECTION_SIZE, DATA_RVA_32, RAW_SECTION_SIZE, DATA_FILE_OFF_32,
		IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);

	/* Build and copy each section, then patch its VirtualSize */
	text_buf = build_text_section_32(&text_vsize);
	rdata_buf = build_rdata_section_32(&rdata_vsize);
	data_buf = build_data_section_32(&data_vsize);

	memcpy(image + TEXT_FILE_OFF_32, text_buf, RAW_SECTION_SIZE);
	memcpy(image + RDATA_FILE_OFF_32, rdata_buf, RAW_SECTION_SIZE);
	memcpy(image + DATA_FILE_OFF_32, data_buf, RAW_SECTION_SIZE);

	free(text_buf); free(rdata_buf); free(data_buf);

	((SectionHeader*)(shtab + 0))->VirtualSize = text_vsize;
	((SectionHeader*)(shtab + 40))->VirtualSize = rdata_vsize;
	((SectionHeader*)(shtab + 80))->VirtualSize = data_vsize;

	/* Write to file */
	fp = fopen(outfile, "wb");
	if (!fp) { perror("fopen"); free(image); return 1; }
	written = fwrite(image, 1, TOTAL_SIZE_32, fp);
	fclose(fp);
	free(image);

	if (written != TOTAL_SIZE_32) {
		fprintf(stderr, "  Error: wrote %zu / %d bytes\n", written, TOTAL_SIZE_32);
		return 1;
	}

	printf("  Success!  %d bytes written.\n", TOTAL_SIZE_32);
	printf("  Layout:   0x000 hdr | 0x%03X .text | 0x%03X .rdata | 0x%03X .data\n",
		TEXT_FILE_OFF_32, RDATA_FILE_OFF_32, DATA_FILE_OFF_32);
	printf("  ImageBase:  0x%08X\n", IMAGEBASE_32);
	printf("  EntryPoint: RVA 0x%04X  ->  VA 0x%08X\n",
		TEXT_RVA_32, IMAGEBASE_32 + TEXT_RVA_32);
	printf("  OptHdr:     %zu bytes  (PE32,  magic=0x%04X)\n",
		sizeof(OptionalHeader32), IMAGE_NT_OPTIONAL_HDR32_MAGIC);
	printf("  Thunk width: 4 bytes (uint32_t IAT entries)\n");
	return 0;
}

/* ===========================================================================
 * 64-BIT (PE32+ / x86-64) DEDICATED SECTION BUILDERS
 *
 * File layout:
 *   0x000  DOS header            (64 B)
 *   0x040  DOS stub              (64 B)
 *   0x080  PE signature          ( 4 B)
 *   0x084  FILE HEADER           (20 B)
 *   0x098  OPTIONAL HEADER 64   (240 B)   <- PE32+
 *   0x188  SECTION TABLE        (120 B)   3 × 40 B
 *   0x200  .text  raw data      (512 B)   x64 machine code
 *   0x400  .rdata raw data      (512 B)   imports (8-byte thunks)
 *   0x600  .data  raw data      (512 B)   "Hello, World!\r\n"
 *   0x800  EOF
 *
 * RVAs:   .text=0x1000   .rdata=0x2000   .data=0x3000
 * ImageBase: 0x140000000
 *
 * .rdata internal layout (64-bit):
 *   [0x000]  ImportDescriptor[0]  kernel32  (20 B)
 *   [0x014]  ImportDescriptor[1]  null      (20 B)
 *   [0x028]  IAT  4 × uint64_t = 32 B       <- 8-byte thunks for PE32+
 *   [0x048]  INT  4 × uint64_t = 32 B
 *   [0x068]  DLL name "KERNEL32.dll\0"
 *   [0x076]  Hint/Name: GetStdHandle
 *   [0x08A]  Hint/Name: WriteFile
 *   [0x097]  Hint/Name: ExitProcess
 * ========================================================================= */

#define IMAGEBASE_64         UINT64_C(0x140000000)
#define TEXT_FILE_OFF_64     0x200
#define RDATA_FILE_OFF_64    0x400
#define DATA_FILE_OFF_64     0x600
#define TOTAL_SIZE_64        0x800
#define TEXT_RVA_64          0x1000u
#define RDATA_RVA_64         0x2000u
#define DATA_RVA_64          0x3000u

 /* Offsets within .rdata (64-bit) */
#define R64_OFF_IDIR         0x000
#define R64_OFF_IAT          0x028
#define R64_OFF_INT          0x048
#define R64_OFF_DLLNAME      0x068
#define R64_OFF_HN_GSH       0x076
#define R64_OFF_HN_WF        0x08A
#define R64_OFF_HN_EP        0x097

/* Corresponding RVAs (64-bit) */
#define R64_RVA_IDIR         (RDATA_RVA_64 + R64_OFF_IDIR)
#define R64_RVA_IAT          (RDATA_RVA_64 + R64_OFF_IAT)
#define R64_RVA_INT          (RDATA_RVA_64 + R64_OFF_INT)
#define R64_RVA_DLLNAME      (RDATA_RVA_64 + R64_OFF_DLLNAME)
#define R64_RVA_HN_GSH       (RDATA_RVA_64 + R64_OFF_HN_GSH)
#define R64_RVA_HN_WF        (RDATA_RVA_64 + R64_OFF_HN_WF)
#define R64_RVA_HN_EP        (RDATA_RVA_64 + R64_OFF_HN_EP)

/* ---------------------------------------------------------------------------
 * build_optional_header_64()
 *
 * Fills IMAGE_OPTIONAL_HEADER64 (PE32+ variant, 240 bytes).
 *
 * Key PE32+-specific details:
 *   Magic       = 0x020B  (not 0x010B)
 *   No BaseOfData field
 *   ImageBase   = uint64_t  (0x140000000 is the standard x64 base)
 *   Stack/heap  = uint64_t  (double the width of PE32)
 *
 * Dedicated to: build_pe64()
 * ------------------------------------------------------------------------- */
static void build_optional_header_64(uint8_t* buf,
	uint32_t  size_of_image,
	uint32_t  size_of_code,
	uint32_t  size_of_idata) {
	OptionalHeader64* oh;
	memset(buf, 0, sizeof(OptionalHeader64));
	oh = (OptionalHeader64*)buf;

	oh->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	oh->MajorLinkerVersion = 14;
	oh->MinorLinkerVersion = 0;
	oh->SizeOfCode = size_of_code;
	oh->SizeOfInitializedData = size_of_idata;
	oh->SizeOfUninitializedData = 0;
	oh->AddressOfEntryPoint = TEXT_RVA_64;
	oh->BaseOfCode = TEXT_RVA_64;
	oh->ImageBase = IMAGEBASE_64;
	oh->SectionAlignment = SECTION_ALIGNMENT;
	oh->FileAlignment = FILE_ALIGNMENT;
	oh->MajorOperatingSystemVersion = 4;
	oh->MinorOperatingSystemVersion = 0;
	oh->MajorImageVersion = 0;
	oh->MinorImageVersion = 0;
	oh->MajorSubsystemVersion = 4;
	oh->MinorSubsystemVersion = 0;
	oh->Win32VersionValue = 0;
	oh->SizeOfImage = size_of_image;
	oh->SizeOfHeaders = 0x200;
	oh->CheckSum = 0;
	oh->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
	oh->DllCharacteristics = 0x8160;
	oh->SizeOfStackReserve = 0x100000;
	oh->SizeOfStackCommit = 0x1000;
	oh->SizeOfHeapReserve = 0x100000;
	oh->SizeOfHeapCommit = 0x1000;
	oh->LoaderFlags = 0;
	oh->NumberOfRvaAndSizes = 16;

	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = R64_RVA_IDIR;
	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
		(uint32_t)(sizeof(ImportDescriptor) * 2);

	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = R64_RVA_IAT;
	oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 4 * 8;
}

/* ---------------------------------------------------------------------------
 * build_text_section_64()
 *
 * Emits x64 machine code using the Microsoft x64 calling convention:
 *   - Integer args:  RCX, RDX, R8, R9; 5th+ at [RSP+0x20], [RSP+0x28]...
 *   - Caller allocates 32-byte (0x20) shadow space before each CALL
 *   - Stack must be 16-byte aligned at the CALL instruction
 *   - Indirect calls use RIP-relative:  FF 15 <disp32>
 *     where disp32 = IAT_VA - (RIP_of_next_instruction)
 *
 * Stack frame after prologue "sub rsp, 0x28":
 *   [RSP+0x00..0x1F]  shadow space (32 bytes = 4 register args)
 *   [RSP+0x20]        8-byte slot (arg5 for WriteFile = NULL,
 *                      also used to zero bytesWritten separately at +0x10)
 *
 * bytesWritten lives at [RSP+0x10] (inside shadow space, ours to use).
 * lpOverlapped (5th WriteFile arg) goes at [RSP+0x20].
 *
 * Stack alignment:
 *   The EXE entry point is reached via an internal CALL from ntdll, so RSP
 *   is already 8-misaligned from the caller's perspective.
 *   "sub rsp, 0x28" (dec by 40) brings RSP to 16-byte alignment for CALL.
 *
 * All RIP-relative displacements are signed 32-bit offsets from the VA
 * of the byte immediately after the 6-byte CALL/JMP instruction.
 *
 * Dedicated to: build_pe64()
 * ------------------------------------------------------------------------- */
static uint8_t* build_text_section_64(uint32_t* out_size) {
	uint8_t* buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	uint8_t* p;
	uint64_t  next_rip;
	int64_t   disp;
	if (!buf) { perror("calloc"); exit(1); }
	p = buf;

	/* sub rsp, 0x28  (48 83 EC 28) */
	*p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;

	/* --- GetStdHandle(STD_OUTPUT_HANDLE) ---
	 *   mov ecx, -11            ; STD_OUTPUT_HANDLE
	 *   call [RIP+disp]         ; IAT slot 0 = GetStdHandle
	 *   mov rbx, rax            ; save handle; RBX is non-volatile
	 */
	*p++ = 0xB9; write_u32(p, 0xFFFFFFF5); p += 4; /* mov ecx, imm32 */

	next_rip = IMAGEBASE_64 + TEXT_RVA_64 + (uint32_t)(p - buf) + 6;
	disp = (int64_t)(IMAGEBASE_64 + R64_RVA_IAT + 0) - (int64_t)next_rip;
	*p++ = 0xFF; *p++ = 0x15; write_u32(p, (uint32_t)(int32_t)disp); p += 4;

	*p++ = 0x48; *p++ = 0x89; *p++ = 0xC3;         /* mov rbx, rax */

	/* Zero the bytesWritten slot at [rsp+0x10]
	 *   mov dword ptr [rsp+0x10], 0   (C7 44 24 10 00 00 00 00)
	 */
	*p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x10;
	write_u32(p, 0); p += 4;

	/* Zero the lpOverlapped slot at [rsp+0x20] (5th arg for WriteFile)
	 *   mov qword ptr [rsp+0x20], 0   (48 C7 44 24 20 00 00 00 00)
	 */
	*p++ = 0x48; *p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x20;
	write_u32(p, 0); p += 4;

	/* --- WriteFile(hFile, lpBuffer, nBytes, &bytesWritten, NULL) ---
	 *   mov  rcx, rbx                  arg1: hFile
	 *   lea  rdx, [RIP+disp]           arg2: lpBuffer -> .data message
	 *   mov  r8d, MSG_LEN              arg3: nNumberOfBytesToWrite
	 *   lea  r9,  [rsp+0x10]           arg4: &bytesWritten
	 *   ; [rsp+0x20] already = 0       arg5: lpOverlapped = NULL
	 *   call [RIP+disp]                IAT slot 1 = WriteFile
	 */
	*p++ = 0x48; *p++ = 0x89; *p++ = 0xD9;         /* mov rcx, rbx */

	next_rip = IMAGEBASE_64 + TEXT_RVA_64 + (uint32_t)(p - buf) + 7;
	disp = (int64_t)(IMAGEBASE_64 + DATA_RVA_64) - (int64_t)next_rip;
	*p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;          /* lea rdx, [RIP+disp32] */
	write_u32(p, (uint32_t)(int32_t)disp); p += 4;

	*p++ = 0x41; *p++ = 0xB8; write_u32(p, MSG_LEN); p += 4; /* mov r8d, imm32 */
	*p++ = 0x4C; *p++ = 0x8D; *p++ = 0x4C; *p++ = 0x24; *p++ = 0x10; /* lea r9,[rsp+0x10] */

	next_rip = IMAGEBASE_64 + TEXT_RVA_64 + (uint32_t)(p - buf) + 6;
	disp = (int64_t)(IMAGEBASE_64 + R64_RVA_IAT + 8) - (int64_t)next_rip;
	*p++ = 0xFF; *p++ = 0x15; write_u32(p, (uint32_t)(int32_t)disp); p += 4;

	/* --- ExitProcess(0) ---
	 *   xor ecx, ecx
	 *   call [RIP+disp]         ; IAT slot 2 = ExitProcess
	 */
	*p++ = 0x31; *p++ = 0xC9;                        /* xor ecx, ecx */

	next_rip = IMAGEBASE_64 + TEXT_RVA_64 + (uint32_t)(p - buf) + 6;
	disp = (int64_t)(IMAGEBASE_64 + R64_RVA_IAT + 16) - (int64_t)next_rip;
	*p++ = 0xFF; *p++ = 0x15; write_u32(p, (uint32_t)(int32_t)disp); p += 4;

	*p++ = 0xCC;  /* INT3 — unreachable safety guard */

	*out_size = (uint32_t)(p - buf);
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_rdata_section_64()
 *
 * Builds the .rdata section for a 64-bit PE.
 * IAT and INT thunks are 8 bytes (uint64_t) — one RVA per import.
 * The loader patches each IAT entry with the resolved 64-bit function VA.
 *
 * Dedicated to: build_pe64()
 * ------------------------------------------------------------------------- */
static uint8_t* build_rdata_section_64(uint32_t* out_vsize) {
	uint8_t* buf;
	ImportDescriptor* idir;
	uint64_t* iat, * intt;

	buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	if (!buf) { perror("calloc"); exit(1); }

	/* Import Descriptor Table: entry 0 = kernel32, entry 1 = null */
	idir = (ImportDescriptor*)(buf + R64_OFF_IDIR);
	idir[0].OriginalFirstThunk = R64_RVA_INT;
	idir[0].TimeDateStamp = 0;
	idir[0].ForwarderChain = 0xFFFFFFFF;
	idir[0].Name = R64_RVA_DLLNAME;
	idir[0].FirstThunk = R64_RVA_IAT;

	/* IAT — 8-byte thunks (PE32+) */
	iat = (uint64_t*)(buf + R64_OFF_IAT);
	iat[0] = R64_RVA_HN_GSH;
	iat[1] = R64_RVA_HN_WF;
	iat[2] = R64_RVA_HN_EP;
	iat[3] = 0;

	/* INT — identical initial values */
	intt = (uint64_t*)(buf + R64_OFF_INT);
	intt[0] = R64_RVA_HN_GSH;
	intt[1] = R64_RVA_HN_WF;
	intt[2] = R64_RVA_HN_EP;
	intt[3] = 0;

	/* DLL name */
	memcpy(buf + R64_OFF_DLLNAME, "KERNEL32.dll", 12 + 1);

	/* Hint/Name entries */
	buf[R64_OFF_HN_GSH + 0] = 0x00; buf[R64_OFF_HN_GSH + 1] = 0x00;
	memcpy(buf + R64_OFF_HN_GSH + 2, "GetStdHandle", 12 + 1);

	buf[R64_OFF_HN_WF + 0] = 0x00; buf[R64_OFF_HN_WF + 1] = 0x00;
	memcpy(buf + R64_OFF_HN_WF + 2, "WriteFile", 9 + 1);

	buf[R64_OFF_HN_EP + 0] = 0x00; buf[R64_OFF_HN_EP + 1] = 0x00;
	memcpy(buf + R64_OFF_HN_EP + 2, "ExitProcess", 11 + 1);

	*out_vsize = R64_OFF_HN_EP + 2 + 11 + 1;
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_data_section_64()
 *
 * .data section for the 64-bit PE.
 * Message VA = 0x140000000 + 0x3000 = 0x140003000.
 *
 * Dedicated to: build_pe64()
 * ------------------------------------------------------------------------- */
static uint8_t* build_data_section_64(uint32_t* out_vsize) {
	uint8_t* buf = (uint8_t*)calloc(RAW_SECTION_SIZE, 1);
	if (!buf) { perror("calloc"); exit(1); }
	memcpy(buf, HELLO_MSG, MSG_LEN);
	*out_vsize = MSG_LEN;
	return buf;
}

/* ---------------------------------------------------------------------------
 * build_pe64()
 *
 * Orchestrates all 64-bit section builders into a complete PE32+ file.
 *
 * Header offsets:
 *   0x080  PE sig
 *   0x084  FileHeader         (20 B)
 *   0x098  OptionalHeader64  (240 B)    <- 0x098 + 0xF0 = 0x188
 *   0x188  SectionTable      (3×40 B)  <- 0x188 + 0x78 = 0x200 (file-aligned)
 *
 * Dedicated to: 64-bit PE generation
 * ------------------------------------------------------------------------- */
int build_pe64(const char* outfile) {
	uint8_t* image, * shtab;
	uint8_t* text_buf, * rdata_buf, * data_buf;
	uint32_t      text_vsize, rdata_vsize, data_vsize;
	uint32_t      size_of_image;
	FILE* fp;
	size_t written;

	printf("Building 64-bit PE (PE32+ / x86-64): %s\n", outfile);

	image = (uint8_t*)calloc(TOTAL_SIZE_64, 1);
	if (!image) { perror("calloc"); return 1; }

	build_dos_header(image + 0x000);
	build_dos_stub(image + 0x040);
	build_pe_signature(image + 0x080);

	/* FileHeader: AMD64, 3 sections, optHdrSize=240, large-address-aware */
	build_file_header(image + 0x084,
		IMAGE_FILE_MACHINE_AMD64,
		3,
		(uint16_t)sizeof(OptionalHeader64),   /* 240 */
		IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE);

	/* OptionalHeader64 at 0x98 */
	size_of_image = ALIGN_UP(DATA_RVA_64 + SECTION_ALIGNMENT, SECTION_ALIGNMENT);
	build_optional_header_64(image + 0x098,
		size_of_image,
		RAW_SECTION_SIZE,
		RAW_SECTION_SIZE * 2);

	/* Section table at 0x188  (0x98 + 0xF0 = 0x188) */
	shtab = image + 0x188;
	build_section_header(shtab + 0, ".text",
		RAW_SECTION_SIZE, TEXT_RVA_64, RAW_SECTION_SIZE, TEXT_FILE_OFF_64,
		IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
	build_section_header(shtab + 40, ".rdata",
		RAW_SECTION_SIZE, RDATA_RVA_64, RAW_SECTION_SIZE, RDATA_FILE_OFF_64,
		IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);
	build_section_header(shtab + 80, ".data",
		RAW_SECTION_SIZE, DATA_RVA_64, RAW_SECTION_SIZE, DATA_FILE_OFF_64,
		IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);

	/* Build and copy each section, then patch its VirtualSize */
	text_buf = build_text_section_64(&text_vsize);
	rdata_buf = build_rdata_section_64(&rdata_vsize);
	data_buf = build_data_section_64(&data_vsize);

	memcpy(image + TEXT_FILE_OFF_64, text_buf, RAW_SECTION_SIZE);
	memcpy(image + RDATA_FILE_OFF_64, rdata_buf, RAW_SECTION_SIZE);
	memcpy(image + DATA_FILE_OFF_64, data_buf, RAW_SECTION_SIZE);

	free(text_buf); free(rdata_buf); free(data_buf);

	((SectionHeader*)(shtab + 0))->VirtualSize = text_vsize;
	((SectionHeader*)(shtab + 40))->VirtualSize = rdata_vsize;
	((SectionHeader*)(shtab + 80))->VirtualSize = data_vsize;

	/* Write to file */
	fp = fopen(outfile, "wb");
	if (!fp) { perror("fopen"); free(image); return 1; }
	written = fwrite(image, 1, TOTAL_SIZE_64, fp);
	fclose(fp);
	free(image);

	if (written != TOTAL_SIZE_64) {
		fprintf(stderr, "  Error: wrote %zu / %d bytes\n", written, TOTAL_SIZE_64);
		return 1;
	}

	printf("  Success!  %d bytes written.\n", TOTAL_SIZE_64);
	printf("  Layout:   0x000 hdr | 0x%03X .text | 0x%03X .rdata | 0x%03X .data\n",
		TEXT_FILE_OFF_64, RDATA_FILE_OFF_64, DATA_FILE_OFF_64);
	printf("  ImageBase:  0x%016llX\n", (unsigned long long)IMAGEBASE_64);
	printf("  EntryPoint: RVA 0x%04X  ->  VA 0x%016llX\n",
		TEXT_RVA_64, (unsigned long long)(IMAGEBASE_64 + TEXT_RVA_64));
	printf("  OptHdr:     %zu bytes  (PE32+, magic=0x%04X)\n",
		sizeof(OptionalHeader64), IMAGE_NT_OPTIONAL_HDR64_MAGIC);
	printf("  Thunk width: 8 bytes (uint64_t IAT entries)\n");
	return 0;
}

void build_pe_console(const char* outfile, bool is64bit)
{
	int rc = 0;

	/* Suppress unused-function warnings for write_u16/write_u64 */
	(void)write_u16; (void)write_u64;

	printf("=========================================================\n");
	printf("  PE Builder  --  32-bit (PE32) and 64-bit (PE32+)\n");
	printf("=========================================================\n");
	printf("Struct sizes:\n");
	printf("  DOSHeader        = %3zu B\n", sizeof(DOSHeader));
	printf("  FileHeader       = %3zu B\n", sizeof(FileHeader));
	printf("  OptionalHeader32 = %3zu B  (PE32,  magic=0x010B, 32-bit base)\n",
		sizeof(OptionalHeader32));
	printf("  OptionalHeader64 = %3zu B  (PE32+, magic=0x020B, 64-bit base)\n",
		sizeof(OptionalHeader64));
	printf("  SectionHeader    = %3zu B\n", sizeof(SectionHeader));
	printf("  ImportDescriptor = %3zu B\n", sizeof(ImportDescriptor));
	printf("\n");

	if (is64bit == false)
	{
		rc = build_pe32(outfile);
	}
	else
	{
		rc = build_pe64(outfile);
	}

	if (rc == 0)
	{
		printf("\nDone. Run the generated .exe files on Windows.\n");
	}
	else
	{
		printf("\nErrors building PE.\n");
	}
	return rc;
}

bool WritePEProgramSQImage(const char* fileName, unsigned char* sq_program_image, int sq_program_image_length)
{
	bool is64bit = true;

	build_pe_console(fileName, is64bit);

	return true;
}