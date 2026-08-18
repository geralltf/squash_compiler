#ifndef _PROCESS_H
#define _PROCESS_H

/* Minimal shim for MSVC's <process.h> — squash has no real one. Only
 * declares what's actually needed: _beginthreadex/_endthreadex, the CRT
 * thread-creation entry points SDL3's SDL_thread.h (SDL_BeginThreadFunction/
 * SDL_EndThreadFunction macros) references directly by name whenever
 * SDL_PLATFORM_WINDOWS is defined. Real signatures, matching MSVC's own
 * process.h exactly, since these are genuine msvcrt.dll exports. */

#include "include/stdint.h"

uintptr_t __cdecl _beginthreadex(void *security, unsigned stack_size,
    unsigned (__stdcall *start_address)(void *), void *arglist,
    unsigned initflag, unsigned *thrdaddr);
void __cdecl _endthreadex(unsigned retval);

#endif /* _PROCESS_H */
