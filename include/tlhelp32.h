#ifndef _TLHELP32_H
#define _TLHELP32_H
/* Minimal Win32 Toolhelp32 (process/module snapshot) API stub — declarations
 * only, matching the include/windows.h shim's convention, so real C source
 * that includes <tlhelp32.h> (e.g. SDL3's SDL_joystick.c, checking for
 * input-remapping programs) parses correctly. */
#include "include/windows.h"

#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPPROCESS  0x00000002
#define TH32CS_SNAPTHREAD   0x00000004
#define TH32CS_SNAPMODULE   0x00000008
#define TH32CS_SNAPMODULE32 0x00000010
#define TH32CS_SNAPALL      (TH32CS_SNAPHEAPLIST|TH32CS_SNAPPROCESS|TH32CS_SNAPTHREAD|TH32CS_SNAPMODULE)
#define TH32CS_INHERIT      0x80000000

typedef struct tagPROCESSENTRY32 {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    unsigned long long th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    CHAR szExeFile[MAX_PATH];
} PROCESSENTRY32;
typedef PROCESSENTRY32 *LPPROCESSENTRY32;

typedef struct tagMODULEENTRY32 {
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    unsigned char *modBaseAddr;
    DWORD modBaseSize;
    HANDLE hModule;
    CHAR szModule[256];
    CHAR szExePath[MAX_PATH];
} MODULEENTRY32;
typedef MODULEENTRY32 *LPMODULEENTRY32;

HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL Process32First(HANDLE hSnapshot, LPPROCESSENTRY32 lppe);
BOOL Process32Next(HANDLE hSnapshot, LPPROCESSENTRY32 lppe);
BOOL Module32First(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
BOOL Module32Next(HANDLE hSnapshot, LPMODULEENTRY32 lpme);

#endif /* _TLHELP32_H */
