#ifndef _WINDOWS_H
#define _WINDOWS_H
/* Minimal Win32 type/macro stub — NOT a real windows.h. Provides just
 * enough of the common Win32 ABI surface (basic types, calling
 * conventions, HRESULT, GUID, RECT, WAVEFORMATEX, ...) for portable C
 * source that merely *declares* functions using these types to parse.
 * squash has no COM/GDI/kernel implementation behind any of this — code
 * that actually CALLS a real Windows API through here will fail to link
 * (an unresolved import), same as any other library function squash
 * doesn't implement. */

/* uintptr_t/intptr_t are extremely common alongside Win32 HANDLE-style code
 * (e.g. _beginthreadex's return type, SDL3's thread/windows/SDL_systhread.c)
 * but this file never defined or pulled them in on its own. When
 * ParseTypeSpecifier() (parser_new4.c) can't recognize ANY token as a type,
 * it silently falls back to "int" WITHOUT ADVANCING THE PARSER PAST THE
 * UNRECOGNIZED TOKEN — a real, general parser desync bug in its own right —
 * so "typedef uintptr_t (*Name)(...);" with uintptr_t undefined didn't just
 * mis-type, it corrupted parsing of everything after it in the file.
 * Pulling in stdint.h here (which already has its own include guard, safe
 * to include from files that also include it directly) fixes the practical
 * case; the parser-desync issue itself is deeper and not fixed here. */
#include "include/stdint.h"

typedef int             BOOL;
typedef unsigned char    BYTE;
typedef unsigned short   WORD;
typedef unsigned int     DWORD;
typedef unsigned long long DWORD64;
typedef unsigned int     UINT;
typedef int              INT;
typedef long             LONG;
typedef unsigned long    ULONG;
typedef long long        LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned int     UINT32;
typedef unsigned long long UINT64;
typedef long long        INT64;
typedef float            FLOAT;
typedef void             VOID;
typedef void            *PVOID;
typedef void            *LPVOID;
typedef const void       *LPCVOID;
typedef unsigned long long ULONG_PTR;
typedef long long        LONG_PTR;
typedef ULONG_PTR        DWORD_PTR;
typedef unsigned short   WCHAR;
typedef char             CHAR;
typedef WCHAR            OLECHAR;
typedef CHAR             *LPSTR;
typedef const CHAR       *LPCSTR;
typedef WCHAR            *LPWSTR;
typedef const WCHAR      *LPCWSTR;
typedef const CHAR       *LPCCH;
typedef const WCHAR      *LPCWCH;
typedef BOOL             *LPBOOL;
typedef WCHAR            TCHAR;
typedef LPWSTR           LPTSTR;
typedef LPCWSTR          LPCTSTR;
typedef WCHAR            *LPWCH;
typedef CHAR             *LPCH;
typedef unsigned long long SIZE_T;

#ifdef UNICODE
#define TEXT(q) L##q
#else
#define TEXT(q) q
#endif
#define OutputDebugString OutputDebugStringA
#define WriteConsole WriteConsoleW
#define GetEnvironmentVariable GetEnvironmentVariableA
#define SetEnvironmentVariable SetEnvironmentVariableA
#define CreateFile CreateFileA

#ifndef WINAPI
#define WINAPI __stdcall
#endif
#ifndef CALLBACK
#define CALLBACK __stdcall
#endif
#ifndef NTAPI
#define NTAPI __stdcall
#endif
#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

/* Opaque handle types */
#define DECLARE_HANDLE(name) typedef struct name##__ { int unused; } *name
DECLARE_HANDLE(HANDLE);
DECLARE_HANDLE(HMODULE);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HCURSOR);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HBRUSH);
typedef void *(*FARPROC)(void);

/* HRESULT */
typedef long HRESULT;
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define S_OK          ((HRESULT)0L)
#define S_FALSE       ((HRESULT)1L)
#define E_FAIL        ((HRESULT)0x80004005L)
#define E_INVALIDARG  ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_NOTIMPL     ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)

/* GUID / IID / CLSID */
typedef struct _GUID {
    unsigned int   Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef const GUID *REFGUID;
typedef const IID  *REFIID;
typedef const CLSID *REFCLSID;

/* IUnknown — the base of every real COM interface (DXGI, D3D11, shell COM
 * objects, etc). Real Microsoft headers generate this same "lpVtbl points
 * at a struct of function pointers, first arg is always the interface
 * pointer itself" shape via the DECLARE_INTERFACE/BEGIN_INTERFACE macros;
 * spelled out directly here since squash has no macro-generated-interface
 * machinery of its own. Already validated end-to-end this session (real
 * CoCreateInstance against shell32.dll's IShellLinkW, QueryInterface,
 * Release — see SDL3_Build/scratch/test_com_real_object.c), just using the
 * real SDK's own windows.h there instead of this project's — this is the
 * same pattern, now provided by squash's own header for DXGI/D3D11 to
 * build on. */
typedef struct IUnknown IUnknown;
typedef struct IUnknownVtbl {
    HRESULT (WINAPI *QueryInterface)(IUnknown *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IUnknown *self);
    ULONG   (WINAPI *Release)(IUnknown *self);
} IUnknownVtbl;
struct IUnknown { IUnknownVtbl *lpVtbl; };

/* Geometry structs */
typedef struct tagPOINT { LONG x; LONG y; } POINT;
typedef struct tagSIZE  { LONG cx; LONG cy; } SIZE;
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

/* Minimal waveformat struct (normally from mmreg.h) */
typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;

#define TRUE  1
#define FALSE 0
#define MAX_PATH 260

/* DllMain reason codes */
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH  2
#define DLL_THREAD_DETACH  3
#define DLL_PROCESS_DETACH 0

/* File I/O constants/types (CreateFile family) */
#define GENERIC_READ         0x80000000
#define GENERIC_WRITE        0x40000000
#define FILE_SHARE_READ      0x00000001
#define FILE_SHARE_WRITE     0x00000002
#define OPEN_EXISTING        3
#define OPEN_ALWAYS          4
#define CREATE_ALWAYS        2
#define CREATE_NEW           1
#define TRUNCATE_EXISTING    5
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

/* Heap corruption detection — enabling this makes the heap manager
 * fail-fast the instant it detects corruption (e.g. a buffer overrun into
 * heap metadata), instead of silently continuing until some unrelated,
 * much later heap operation crashes — turns "crashes somewhere after a
 * huge call graph ran" into "crashes at the actual overflow." */
typedef enum {
    HeapCompatibilityInformation = 0,
    HeapEnableTerminationOnCorruption = 1,
    HeapOptimizeResources = 3
} HEAP_INFORMATION_CLASS;
BOOL HeapSetInformation(HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass, PVOID HeapInformation, SIZE_T HeapInformationLength);

/* Kernel semaphore API — used to give this build a real, non-circular
 * semaphore implementation (see feedback-squash-sdl3-thread-generic-cycle):
 * SDL's own thread/generic/SDL_sysmutex.c ("mutexes using semaphores") and
 * thread/generic/SDL_syssem.c ("semaphores using mutexes") are mutually
 * recursive by design and were never meant to be paired together — real
 * platforms always supply a native implementation of at least one of the
 * two. */
#define INFINITE 0xFFFFFFFF
HANDLE CreateSemaphoreW(void *lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName);
BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LONG *lpPreviousCount);
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable);
BOOL CloseHandle(HANDLE hObject);

/* Real thread-creation surface — needed by SDL3's actual
 * thread/windows/SDL_systhread.c (SDL_SYS_CreateThread), which falls back to
 * a direct CreateThread() call whenever the CRT's _beginthreadex (passed in
 * via SDL_BeginThreadFunction) isn't supplied. Without these declared here,
 * squash still resolves the calls correctly via its hardcoded WinAPI/DLL
 * routing table (see symtable.c's SI("CreateThread","KERNEL32.dll") etc.),
 * but declaring them properly lets normal type-checking apply at call sites. */
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpParameter);
HANDLE CreateThread(void *lpThreadAttributes, SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags,
    DWORD *lpThreadId);
DWORD GetCurrentThreadId(void);

/* Process heap — used throughout this build's own thread/mutex helper code
 * (HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ...) is the standard Win32
 * idiom for zero-filled allocation). squash already resolves calls to these
 * correctly even without a prototype (see symtable.c's hardcoded KERNEL32.dll
 * routing table), but HEAP_ZERO_MEMORY is a plain constant, not a function --
 * without a real #define here it silently evaluates to an undefined
 * identifier, which is a genuine bug (not just a missing type-check). */
#define HEAP_ZERO_MEMORY 0x00000008
HANDLE GetProcessHeap(void);
LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
LPVOID HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);

/* High-resolution timer — SDL's own timer/windows/SDL_systimer.c wasn't
 * pulled into this build (it also needs GetProcAddress-based dynamic
 * loading of waitable-timer APIs, well beyond what SDL_InitTicks() itself
 * needs), so SDL_GetPerformanceCounter/Frequency are reimplemented
 * directly in squash_build/sdl_unity.c using these two declarations. */
typedef union {
    LONGLONG QuadPart;
} LARGE_INTEGER;
BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

/* SDL_cpuinfo.c's SDL_GetNumLogicalCPUCores()/SDL_GetSystemPageSize() call
 * the real GetSystemInfo() — field layout must match the real Win32 ABI
 * exactly (this struct is filled in by the actual OS API, not squash). */
typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        } s;
    } u;
    DWORD     dwPageSize;
    void     *lpMinimumApplicationAddress;
    void     *lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD     dwNumberOfProcessors;
    DWORD     dwProcessorType;
    DWORD     dwAllocationGranularity;
    WORD      wProcessorLevel;
    WORD      wProcessorRevision;
} SYSTEM_INFO;
void GetSystemInfo(SYSTEM_INFO *lpSystemInfo);

/* SDL_GetSystemRAM() calls the real GlobalMemoryStatusEx() — likewise must
 * match the real ABI layout exactly. */
typedef struct _MEMORYSTATUSEX {
    DWORD    dwLength;
    DWORD    dwMemoryLoad;
    ULONGLONG ullTotalPhys;
    ULONGLONG ullAvailPhys;
    ULONGLONG ullTotalPageFile;
    ULONGLONG ullAvailPageFile;
    ULONGLONG ullTotalVirtual;
    ULONGLONG ullAvailVirtual;
    ULONGLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX;
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *lpBuffer);

/* WIN_SetError()/WIN_SetErrorFromHRESULT() (reimplemented directly in
 * squash_build/sdl_unity.c, since the real ones live in core/windows/
 * SDL_windows.c which pulls in video-subsystem and COM/WinRT headers well
 * outside this build's scope) need FormatMessageW to turn a GetLastError()/
 * HRESULT code into a human-readable string. The last parameter is really
 * "va_list *", but since every caller here always passes NULL for it, a
 * plain pointer type is ABI-equivalent and avoids a <stdarg.h> dependency. */
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
DWORD FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
                     DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize,
                     void *Arguments);

/* io/SDL_iostream.c's Windows-native (HANDLE-based) file backend. */
#define SEM_FAILCRITICALERRORS  0x0001
#define SEM_NOOPENFILEERRORBOX  0x8000
UINT SetErrorMode(UINT uMode);

#define FILE_BEGIN   0
#define FILE_CURRENT 1
#define FILE_END     2
BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, LARGE_INTEGER *lpNewFilePointer, DWORD dwMoveMethod);
DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod);
BOOL GetFileSizeEx(HANDLE hFile, LARGE_INTEGER *lpFileSize);
BOOL FlushFileBuffers(HANDLE hFile);
BOOL DeleteFileW(LPCWSTR lpFileName);

/* Real winerror.h/winbase.h values — used both in ordinary comparisons
 * (SDL_log.c's console-attach handling, SDL_getenv.c's env-var probing)
 * and, in io/SDL_iostream.c's windows_file_read(), as switch/case labels,
 * where an undefined identifier is a hard "case value must be constant"
 * parse error rather than a tolerated "codegen: undefined" warning. */
#define ERROR_SUCCESS           0
#define ERROR_ACCESS_DENIED     5
#define ERROR_INVALID_HANDLE    6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_GEN_FAILURE       31
#define ERROR_BROKEN_PIPE       109
#define ERROR_NO_DATA           232
#define STD_INPUT_HANDLE        ((DWORD)-10)
#define STD_OUTPUT_HANDLE       ((DWORD)-11)
#define STD_ERROR_HANDLE        ((DWORD)-12)
#define ATTACH_PARENT_PROCESS   ((DWORD)-1)

#define FILE_TYPE_UNKNOWN 0x0000
#define FILE_TYPE_DISK    0x0001
#define FILE_TYPE_CHAR    0x0002
#define FILE_TYPE_PIPE    0x0003
DWORD GetFileType(HANDLE hFile);

/* filesystem/windows/SDL_sysfsops.c's real (non-dummy) directory/file
 * management backend. */
#define ERROR_ALREADY_EXISTS  183
#define ERROR_FILE_NOT_FOUND  2
#define ERROR_PATH_NOT_FOUND  3

#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_OFFLINE   0x00001000
#define FILE_ATTRIBUTE_DEVICE    0x00000040

#define MOVEFILE_REPLACE_EXISTING 0x00000001

#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000008

typedef enum {
    GetFileExInfoStandard = 0
} GET_FILEEX_INFO_LEVELS;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
BOOL GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, void *lpFileInformation);

BOOL CreateDirectoryW(LPCWSTR lpPathName, void *lpSecurityAttributes);
BOOL RemoveDirectoryW(LPCWSTR lpPathName);
BOOL MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
BOOL CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, void *lpProgressRoutine,
                  void *lpData, BOOL *pbCancel, DWORD dwCopyFlags);
DWORD GetLogicalDrives(void);

typedef enum {
    FindExInfoStandard = 0
} FINDEX_INFO_LEVELS;
typedef enum {
    FindExSearchNameMatch = 0
} FINDEX_SEARCH_OPS;
#define MAX_PATH_WIN32_FIND_DATA 260
typedef struct _WIN32_FIND_DATAW {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    WCHAR    cFileName[MAX_PATH_WIN32_FIND_DATA];
    WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATAW;
HANDLE FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, void *lpFindFileData,
                        FINDEX_SEARCH_OPS fSearchOp, void *lpSearchFilter, DWORD dwAdditionalFlags);
BOOL FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW *lpFindFileData);
BOOL FindClose(HANDLE hFindFile);

/* A real (hand-written, ASCII-only) SDL_SYS_GetBasePath()/GetCurrentDirectory()
 * for sdl_unity.c — see WIN_GetModulePath there. */
DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);

/* filesystem/windows/SDL_sysfilesystem.c's real SDL_SYS_GetPrefPath()/
 * SDL_SYS_GetUserFolder() — the modern SHGetKnownFolderPath is loaded
 * dynamically via GetProcAddress (see include/shlobj.h), so only these
 * three plain KERNEL32 exports need declaring here. */
HMODULE LoadLibraryW(LPCWSTR lpLibFileName);
FARPROC GetProcAddress(HMODULE hModule, const char *lpProcName);
BOOL FreeLibrary(HMODULE hLibModule);

/* process/windows/SDL_windowsprocess.c's real (non-dummy) CreateProcessW-
 * based backend — real subprocess spawning + pipe I/O. */
typedef ULONG_PTR WPARAM;
typedef LONG_PTR  LPARAM;
typedef LONG_PTR  LRESULT;
typedef BOOL (CALLBACK *WNDENUMPROC)(HWND, LPARAM);

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} PROCESS_INFORMATION;

typedef struct _STARTUPINFOW {
    DWORD  cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD  dwX;
    DWORD  dwY;
    DWORD  dwXSize;
    DWORD  dwYSize;
    DWORD  dwXCountChars;
    DWORD  dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    WORD   wShowWindow;
    WORD   cbReserved2;
    void  *lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOW;

#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define CREATE_NO_WINDOW           0x08000000
#define STARTF_USESTDHANDLES       0x00000100
#define HANDLE_FLAG_INHERIT        0x00000001
#define DUPLICATE_SAME_ACCESS      0x00000002
#define PIPE_WAIT                  0x00000000
#define PIPE_NOWAIT                0x00000001
#define WM_CLOSE                   0x0010
#define CTRL_BREAK_EVENT           1
#define WAIT_OBJECT_0              0x00000000
#define WAIT_FAILED                ((DWORD)0xFFFFFFFF)

BOOL CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                     SECURITY_ATTRIBUTES *lpProcessAttributes, SECURITY_ATTRIBUTES *lpThreadAttributes,
                     BOOL bInheritHandles, DWORD dwCreationFlags, void *lpEnvironment,
                     LPCWSTR lpCurrentDirectory, STARTUPINFOW *lpStartupInfo,
                     PROCESS_INFORMATION *lpProcessInformation);
BOOL CreatePipe(HANDLE *hReadPipe, HANDLE *hWritePipe, SECURITY_ATTRIBUTES *lpPipeAttributes, DWORD nSize);
BOOL DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle,
                      HANDLE hTargetProcessHandle, HANDLE *lpTargetHandle,
                      DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);
BOOL SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags);
BOOL SetNamedPipeHandleState(HANDLE hNamedPipe, DWORD *lpMode, DWORD *lpMaxCollectionCount, DWORD *lpCollectDataTimeout);
BOOL GetExitCodeProcess(HANDLE hProcess, DWORD *lpExitCode);
BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode);
BOOL EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam);
/* PostMessage/PostThreadMessage are generic TCHAR-style macro names in
 * real Win32 (winuser.h #defines them to ...A/...W based on UNICODE) —
 * there's no DLL export literally named "PostMessage"/"PostThreadMessage",
 * only "PostMessageA"/"PostMessageW" etc. Calling the bare generic name
 * (as process/windows/SDL_windowsprocess.c's source does, matching real
 * Win32 usage) without this macro produced a hard STATUS_ENTRYPOINT_NOT_FOUND
 * at process launch — squash's import resolution looks up the DLL export
 * table by the literal identifier used at the call site, and "PostMessage"
 * genuinely isn't one, unlike "CreateFile" which this shim already
 * special-cases the same way just above. */
#define PostMessage PostMessageA
#define PostThreadMessage PostThreadMessageA
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL PostThreadMessageA(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL GenerateConsoleCtrlEvent(DWORD dwCtrlEvent, DWORD dwProcessGroupId);
DWORD GetWindowThreadProcessId(HWND hWnd, DWORD *lpdwProcessId);
HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                    SECURITY_ATTRIBUTES *lpSecurityAttributes, DWORD dwCreationDisposition,
                    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

/* ---- Minimal GUI/windowing surface (video/windows custom backend,
 * squash_build/sdl_unity.c's hand-written SDL_VIDEO_DRIVER_PRIVATE) ----
 * Real top-level window creation + a message pump + a GDI DIB-section
 * framebuffer for SDL's software renderer. Same ANSI-only ("A"-suffixed)
 * convention as the rest of this shim (WIN_GetModulePath etc.) — matches
 * the real signatures/ABI closely enough to link against the genuine
 * user32.dll/gdi32.dll exports, just without a wide-char code path. */
typedef ULONG_PTR WNDPROC_placeholder; /* unused; keeps grep-diffs quiet */
typedef unsigned short ATOM;
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSEXA {
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXA;

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG;

#define CS_VREDRAW          0x0001
#define CS_HREDRAW          0x0002
#define CS_OWNDC            0x0020
#define CS_DBLCLKS          0x0008

#define WS_OVERLAPPED       0x00000000
#define WS_CAPTION          0x00C00000
#define WS_SYSMENU          0x00080000
#define WS_THICKFRAME       0x00040000
#define WS_MINIMIZEBOX      0x00020000
#define WS_MAXIMIZEBOX      0x00010000
#define WS_POPUP            0x80000000
#define WS_CHILD            0x40000000
#define WS_VISIBLE          0x10000000
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX)
#define WS_EX_APPWINDOW     0x00040000

#define CW_USEDEFAULT       ((int)0x80000000)

#define SW_HIDE             0
#define SW_SHOWNORMAL       1
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_RESTORE          9
#define SW_MAXIMIZE         3

#define PM_REMOVE           0x0001
#define PM_NOREMOVE         0x0000

#define WM_DESTROY          0x0002
#define WM_SIZE             0x0005
#define WM_MOVE             0x0003
#define WM_PAINT            0x000F
#define WM_QUIT             0x0012
#define WM_ERASEBKGND       0x0014
#define WM_SETCURSOR        0x0020
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define WM_CHAR             0x0102
#define WM_SYSKEYDOWN       0x0104
#define WM_SYSKEYUP         0x0105
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MOUSEWHEEL       0x020A
#define WM_ACTIVATE         0x0006
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008

#define SM_CXSCREEN         0
#define SM_CYSCREEN         1

#define IDC_ARROW           ((LPCSTR)32512)
#define IDI_APPLICATION     ((LPCSTR)32512)
#define COLOR_WINDOW        5

#define GWLP_USERDATA       (-21)

ATOM      RegisterClassExA(const WNDCLASSEXA *wc);
HWND      CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName,
                          DWORD style, int x, int y, int w, int h,
                          HWND parent, HMENU menu, HINSTANCE hInst, void *param);
LRESULT   DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
BOOL      DestroyWindow(HWND hwnd);
BOOL      ShowWindow(HWND hwnd, int cmdShow);
BOOL      UpdateWindow(HWND hwnd);
BOOL      GetMessageA(MSG *msg, HWND hwnd, UINT msgMin, UINT msgMax);
BOOL      PeekMessageA(MSG *msg, HWND hwnd, UINT msgMin, UINT msgMax, UINT remove);
BOOL      TranslateMessage(const MSG *msg);
LRESULT   DispatchMessageA(const MSG *msg);
void      PostQuitMessage(int code);
HINSTANCE GetModuleHandleA(LPCSTR moduleName);
/* Real windows.h makes "GetModuleHandle" a UNICODE-dependent macro, not a
 * real symbol — kernel32.dll exports GetModuleHandleA/W but nothing named
 * exactly "GetModuleHandle". Source calling the bare name (e.g. SDL3's
 * thread/windows/SDL_systhread.c) needs this macro or the literal
 * identifier resolves to a nonexistent import at link time. */
#ifndef GetModuleHandle
#define GetModuleHandle GetModuleHandleA
#endif
HDC       GetDC(HWND hwnd);
int       ReleaseDC(HWND hwnd, HDC hdc);
HCURSOR   LoadCursorA(HINSTANCE hInstance, LPCSTR cursorName);
HICON     LoadIconA(HINSTANCE hInstance, LPCSTR iconName);
HWND      GetDesktopWindow(void);
int       MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type);
BOOL      GetClientRect(HWND hwnd, RECT *rect);
BOOL      GetWindowRect(HWND hwnd, RECT *rect);
BOOL      AdjustWindowRect(RECT *rect, DWORD style, BOOL hasMenu);
int       GetSystemMetrics(int index);
BOOL      InvalidateRect(HWND hwnd, const RECT *rect, BOOL erase);
BOOL      SetWindowTextA(HWND hwnd, LPCSTR text);
BOOL      SetWindowPos(HWND hwnd, HWND hwndInsertAfter, int x, int y, int cx, int cy, UINT flags);
BOOL      MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint);
LONG_PTR  SetWindowLongPtrA(HWND hwnd, int index, LONG_PTR newLong);
LONG_PTR  GetWindowLongPtrA(HWND hwnd, int index);
BOOL      ClientToScreen(HWND hwnd, POINT *point);
HWND      WindowFromPoint(POINT point);
BOOL      SetForegroundWindow(HWND hwnd);
#define HWND_TOPMOST ((HWND)-1)
#define SWP_NOMOVE 0x0002
#define SWP_NOSIZE 0x0001

/* GDI framebuffer (DIB section blit) — used by the custom PRIVATE
 * CreateWindowFramebuffer/UpdateWindowFramebuffer backend, matching real
 * SDL3's own video/windows/SDL_windowsframebuffer.c technique. */
typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO;

#define BI_RGB       0
#define BI_RLE8      1
#define BI_RLE4      2
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020

HDC     CreateCompatibleDC(HDC hdc);
HANDLE  CreateCompatibleBitmap(HDC hdc, int w, int h);
HANDLE  CreateDIBSection(HDC hdc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE hSection, DWORD offset);
HANDLE  SelectObject(HDC hdc, HANDLE obj);
BOOL    DeleteDC(HDC hdc);
BOOL    DeleteObject(HANDLE obj);
BOOL    BitBlt(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int xSrc, int ySrc, DWORD rop);
int     GetDIBits(HDC hdc, HANDLE hbm, UINT startScan, UINT scanLines, void *bits, BITMAPINFO *info, UINT usage);
DWORD   GetPixel(HDC hdc, int x, int y);

/* --- WinMM (winmm.dll) waveOut, minimal PCM playback surface --- */
DECLARE_HANDLE(HWAVEOUT);
typedef UINT MMRESULT;
#define MMSYSERR_NOERROR 0
#define WAVE_MAPPER ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define CALLBACK_NULL 0x00000000
#define WHDR_DONE      0x00000001
#define WHDR_PREPARED  0x00000002

typedef struct tWAVEHDR {
    char        *lpData;
    DWORD       dwBufferLength;
    DWORD       dwBytesRecorded;
    DWORD_PTR   dwUser;
    DWORD       dwFlags;
    DWORD       dwLoops;
    struct tWAVEHDR *lpNext;
    DWORD_PTR   reserved;
} WAVEHDR;

MMRESULT waveOutOpen(HWAVEOUT *phwo, UINT uDeviceID, const WAVEFORMATEX *pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen);
MMRESULT waveOutClose(HWAVEOUT hwo);
MMRESULT waveOutPrepareHeader(HWAVEOUT hwo, WAVEHDR *pwh, UINT cbwh);
MMRESULT waveOutUnprepareHeader(HWAVEOUT hwo, WAVEHDR *pwh, UINT cbwh);
MMRESULT waveOutWrite(HWAVEOUT hwo, WAVEHDR *pwh, UINT cbwh);
MMRESULT waveOutReset(HWAVEOUT hwo);
MMRESULT waveOutPause(HWAVEOUT hwo);
MMRESULT waveOutRestart(HWAVEOUT hwo);

/* Structured exception handling — needed by SDL3's real
 * thread/windows/SDL_systhread.c (SDL_SYS_SetupThread's debugger-thread-
 * naming path). Only the shapes actually touched there are declared: a
 * pointer chain (info->ExceptionRecord->ExceptionCode) and the two
 * CONTINUE constants — not a faithful full EXCEPTION_RECORD/CONTEXT. */
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;
typedef EXCEPTION_RECORD *PEXCEPTION_RECORD;
typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PVOID ContextRecord;
} EXCEPTION_POINTERS;
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH    (0)
typedef long (NTAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *ExceptionInfo);
PVOID AddVectoredExceptionHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler);
ULONG RemoveVectoredExceptionHandler(PVOID Handle);
VOID RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments);
HINSTANCE GetModuleHandleW(LPCWSTR lpModuleName);
HRESULT SetThreadDescription(HANDLE hThread, LPCWSTR lpThreadDescription);

#define THREAD_PRIORITY_LOWEST         (-2)
#define THREAD_PRIORITY_NORMAL         (0)
#define THREAD_PRIORITY_HIGHEST        (2)
#define THREAD_PRIORITY_TIME_CRITICAL  (15)
HANDLE GetCurrentThread(void);
BOOL SetThreadPriority(HANDLE hThread, int nPriority);

/* CRITICAL_SECTION + Interlocked* — needed so SDL3's own SDL_malloc.c (a
 * dlmalloc port; see its own header comment: "Thread-safety: NOT thread-safe
 * unless USE_LOCKS defined non-zero") can actually be made thread-safe.
 * Without this, the allocator's shared internal free-list state has zero
 * synchronization, and the FIRST real concurrent SDL_malloc/SDL_calloc call
 * from a genuine second OS thread (e.g. an audio worker thread) corrupts it
 * — confirmed via a crash reading Windows' own "BAADF00D" uninitialized-heap
 * debug marker where a freshly-calloc'd struct's field should have been.
 * Real struct layout (fields are directly read/written by the actual OS
 * synchronization code these functions call into, unlike most of this
 * shim's simplified stand-ins — this one must be byte-for-byte accurate). */
typedef struct _RTL_CRITICAL_SECTION_DEBUG *PRTL_CRITICAL_SECTION_DEBUG;
typedef struct _RTL_CRITICAL_SECTION {
    PRTL_CRITICAL_SECTION_DEBUG DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION;
VOID InitializeCriticalSection(CRITICAL_SECTION *lpCriticalSection);
BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *lpCriticalSection, DWORD dwSpinCount);
VOID EnterCriticalSection(CRITICAL_SECTION *lpCriticalSection);
VOID LeaveCriticalSection(CRITICAL_SECTION *lpCriticalSection);
BOOL TryEnterCriticalSection(CRITICAL_SECTION *lpCriticalSection);
VOID DeleteCriticalSection(CRITICAL_SECTION *lpCriticalSection);
LONG InterlockedCompareExchange(LONG volatile *Destination, LONG Exchange, LONG Comparand);
LONG InterlockedExchange(LONG volatile *Target, LONG Value);
VOID SleepEx(DWORD dwMilliseconds, BOOL bAlertable);
/* SDL_malloc.c's dlmalloc port calls these two through lowercase macro
 * names (its own MSVC-intrinsic / GCC-__sync_builtin selection branches
 * don't fire for squash, since it defines neither _MSC_VER nor __GNUC__) —
 * point them at the real kernel32.dll exports above instead. Argument order
 * matches Interlocked*'s own signature directly (unlike the GCC
 * __sync_val_compare_and_swap path, which swaps the last two arguments). */
#define interlockedcompareexchange(a,b,c) InterlockedCompareExchange((a),(b),(c))
#define interlockedexchange(a,b) InterlockedExchange((a),(b))

#endif /* _WINDOWS_H */
