#include "symtable.h"
#include "implib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* FNV-1a over the symbol name — used only to index the global scope's
 * hbuckets (see Scope.hbuckets' comment in symtable.h). Any decent
 * string hash works here; FNV-1a is simple, has no external dependency,
 * and spreads real-world identifier names (SDL3's own naming conventions
 * included) evenly enough that chain lengths stay short. */
static unsigned long sym_hash(const char *s) {
    unsigned long h = 2166136261UL;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619UL; }
    return h;
}

/* Bucket count for the global scope's hash index. Fixed rather than
 * grow-on-demand: real single-TU builds top out at a few thousand global
 * symbols (SQW's build, which #includes all of SDL3 plus its own files
 * into one TU, is ~6500+), so 32768 buckets keeps the average chain under
 * 1 even for TUs several times larger, without needing rehash logic. */
#define SYM_GLOBAL_HASH_BUCKETS 32768

/* Insert a just-allocated global-scope Symbol into its scope's hash
 * index, lazily allocating the bucket array on first use. */
static void global_hash_insert(Scope *g, Symbol *s) {
    if (!g->hbuckets) {
        g->hbuckets = calloc(SYM_GLOBAL_HASH_BUCKETS, sizeof(Symbol*));
        g->hcap = SYM_GLOBAL_HASH_BUCKETS;
    }
    unsigned long h = sym_hash(s->name) % (unsigned long)g->hcap;
    s->hnext = g->hbuckets[h];
    g->hbuckets[h] = s;
}


/* Avoid global array initializers (squash codegen doesn't support pointer-field init).
 * Use explicit per-entry definitions in symtable_init and find_dll instead. */
#define SI(name,dll) symtable_define_import(st,(name),(dll))
#define SF(name) do { TypeInfo *_t=typeinfo_new("void");_t->pointer_depth=0; symtable_define_func(st,(name),_t,-1,NULL); } while(0)

static const char *find_dll(const char *name) {
    if (!name) return NULL;
    if (strcmp(name,"GetStdHandle")==0||strcmp(name,"WriteFile")==0||
        strcmp(name,"WriteConsoleA")==0||strcmp(name,"ExitProcess")==0||
        strcmp(name,"GetLastError")==0||strcmp(name,"CloseHandle")==0||
        strcmp(name,"CreateFileA")==0||strcmp(name,"ReadFile")==0||
        strcmp(name,"Sleep")==0||strcmp(name,"GetTickCount")==0||
        strcmp(name,"VirtualAlloc")==0||strcmp(name,"VirtualFree")==0||
        strcmp(name,"HeapAlloc")==0||strcmp(name,"HeapFree")==0||
        strcmp(name,"GetProcessHeap")==0||strcmp(name,"HeapReAlloc")==0||
        strcmp(name,"LocalAlloc")==0||strcmp(name,"LocalFree")==0||
        strcmp(name,"SetConsoleTextAttribute")==0||
        strcmp(name,"GetConsoleScreenBufferInfo")==0||
        strcmp(name,"SetConsoleCursorPosition")==0||
        strcmp(name,"GetCommandLineA")==0||
        strcmp(name,"GetSystemTimeAsFileTime")==0||
        strcmp(name,"GetCurrentProcessId")==0||
        strcmp(name,"GetCurrentThreadId")==0||
        strcmp(name,"InitializeSListHead")==0||
        strcmp(name,"GetSystemTime")==0||
        strcmp(name,"GetLocalTime")==0||
        strcmp(name,"GetCurrentProcess")==0||
        strcmp(name,"TerminateProcess")==0||
        strcmp(name,"AttachConsole")==0||
        strcmp(name,"OutputDebugStringA")==0||
        strcmp(name,"OutputDebugStringW")==0||
        strcmp(name,"GetConsoleMode")==0||
        strcmp(name,"WriteConsoleW")==0||
        strcmp(name,"GetEnvironmentStringsW")==0||
        strcmp(name,"FreeEnvironmentStringsW")==0||
        strcmp(name,"GetEnvironmentVariableA")==0||
        strcmp(name,"GetEnvironmentVariableW")==0||
        strcmp(name,"SetEnvironmentVariableA")==0||
        strcmp(name,"SetEnvironmentVariableW")==0||
        strcmp(name,"SetLastError")==0||
        strcmp(name,"CreateFileW")==0||
        strcmp(name,"GetFileTime")==0||
        strcmp(name,"GetFileAttributesW")==0||
        strcmp(name,"HeapSetInformation")==0||
        strcmp(name,"MultiByteToWideChar")==0||
        strcmp(name,"WideCharToMultiByte")==0||
        strcmp(name,"CreateSemaphoreW")==0||
        strcmp(name,"ReleaseSemaphore")==0||
        strcmp(name,"WaitForSingleObject")==0||
        strcmp(name,"QueryPerformanceCounter")==0||
        strcmp(name,"QueryPerformanceFrequency")==0||
        strcmp(name,"GetSystemInfo")==0||
        strcmp(name,"GlobalMemoryStatusEx")==0||
        strcmp(name,"FormatMessageW")==0||
        strcmp(name,"SetErrorMode")==0||
        strcmp(name,"SetFilePointerEx")==0||
        strcmp(name,"SetFilePointer")==0||
        strcmp(name,"GetFileSizeEx")==0||
        strcmp(name,"FlushFileBuffers")==0||
        strcmp(name,"DeleteFileW")==0||
        strcmp(name,"GetFileType")==0||
        strcmp(name,"CreateDirectoryW")==0||
        strcmp(name,"RemoveDirectoryW")==0||
        strcmp(name,"MoveFileExW")==0||
        strcmp(name,"CopyFileExW")==0||
        strcmp(name,"GetFileAttributesExW")==0||
        strcmp(name,"FindFirstFileExW")==0||
        strcmp(name,"FindNextFileW")==0||
        strcmp(name,"FindClose")==0||
        strcmp(name,"GetLogicalDrives")==0||
        strcmp(name,"CreateToolhelp32Snapshot")==0||
        strcmp(name,"Process32First")==0||
        strcmp(name,"Process32Next")==0||
        strcmp(name,"Module32First")==0||
        strcmp(name,"Module32Next")==0||
        strcmp(name,"GetModuleFileNameW")==0||
        strcmp(name,"GetCurrentDirectoryW")==0||
        strcmp(name,"CreateProcessW")==0||
        strcmp(name,"CreatePipe")==0||
        strcmp(name,"DuplicateHandle")==0||
        strcmp(name,"SetHandleInformation")==0||
        strcmp(name,"SetNamedPipeHandleState")==0||
        strcmp(name,"GetExitCodeProcess")==0||
        strcmp(name,"GenerateConsoleCtrlEvent")==0||
        strcmp(name,"LoadLibraryW")==0||
        strcmp(name,"GetProcAddress")==0||
        strcmp(name,"FreeLibrary")==0||
        strcmp(name,"SetUnhandledExceptionFilter")==0||
        strcmp(name,"AddVectoredExceptionHandler")==0||
        strcmp(name,"RemoveVectoredExceptionHandler")==0||
        strcmp(name,"RaiseException")==0||
        strcmp(name,"GetModuleHandleW")==0||
        strcmp(name,"SetThreadDescription")==0||
        strcmp(name,"GetCurrentThread")==0||
        strcmp(name,"SetThreadPriority")==0||
        strcmp(name,"CreateThread")==0||
        strcmp(name,"GetModuleHandle")==0||
        strcmp(name,"WaitForSingleObjectEx")==0||
        strcmp(name,"InitializeCriticalSection")==0||
        strcmp(name,"InitializeCriticalSectionAndSpinCount")==0||
        strcmp(name,"EnterCriticalSection")==0||
        strcmp(name,"LeaveCriticalSection")==0||
        strcmp(name,"TryEnterCriticalSection")==0||
        strcmp(name,"DeleteCriticalSection")==0||
        strcmp(name,"InterlockedCompareExchange")==0||
        strcmp(name,"InterlockedExchange")==0||
        strcmp(name,"SleepEx")==0) return "KERNEL32.dll";
    if (strcmp(name,"GetDoubleClickTime")==0||
        strcmp(name,"EnumWindows")==0||
        strcmp(name,"PostMessageA")==0||
        strcmp(name,"PostThreadMessageA")==0||
        strcmp(name,"GetWindowThreadProcessId")==0) return "USER32.dll";
    /* Shell32 (shell32.dll) — SDL_SYS_GetPrefPath()/GetUserFolder() */
    if (strcmp(name,"SHGetFolderPathW")==0) return "SHELL32.dll";
    /* MSVC CRT thread-creation entry points (msvcrt.dll) — referenced by
     * name via SDL3's SDL_BeginThreadFunction/SDL_EndThreadFunction macros
     * (SDL_thread.h) whenever SDL_CreateThread() is called on Windows. */
    if (strcmp(name,"_beginthreadex")==0||strcmp(name,"_endthreadex")==0) return "msvcrt.dll";
    /* WinMM (winmm.dll) — minimal waveOut PCM playback backend, this
     * project's own PRIVATEAUDIO_bootstrap (see sdl_core.inc). */
    if (strcmp(name,"waveOutOpen")==0||strcmp(name,"waveOutClose")==0||
        strcmp(name,"waveOutPrepareHeader")==0||strcmp(name,"waveOutUnprepareHeader")==0||
        strcmp(name,"waveOutWrite")==0||strcmp(name,"waveOutReset")==0||
        strcmp(name,"waveOutPause")==0||strcmp(name,"waveOutRestart")==0) return "winmm.dll";
    /* OpenGL/WGL (opengl32.dll) — declared via include/GL/gl.h and wgl.h's
     * plain extern prototypes, which (unlike the KERNEL32 shims above)
     * squash never generates a body for, so codegen needs to know their DLL
     * to emit an import call instead of treating them as locally-defined.
     * Every core GL 1.0/1.1 function ("glFoo") and every WGL function
     * ("wglFoo") is a real opengl32.dll export, so match by prefix instead
     * of hardcoding gl.h's ~40-function list (and whatever subset any other
     * GL example ends up calling directly rather than through a pfnGl*
     * extension pointer). */
    if (name[0]=='g'&&name[1]=='l'&&name[2]>='A'&&name[2]<='Z') return "opengl32.dll";
    if (name[0]=='w'&&name[1]=='g'&&name[2]=='l'&&name[3]>='A'&&name[3]<='Z') return "opengl32.dll";
    /* GDI (gdi32.dll) */
    if (strcmp(name,"ChoosePixelFormat")==0||strcmp(name,"SetPixelFormat")==0||
        strcmp(name,"SwapBuffers")==0||strcmp(name,"DescribePixelFormat")==0||
        strcmp(name,"CreateCompatibleDC")==0||strcmp(name,"CreateDIBSection")==0||
        strcmp(name,"SelectObject")==0||strcmp(name,"DeleteDC")==0||
        strcmp(name,"DeleteObject")==0||
        strcmp(name,"CreateCompatibleBitmap")==0||
        strcmp(name,"BitBlt")==0||strcmp(name,"GetDIBits")==0||
        strcmp(name,"GetPixel")==0) return "gdi32.dll";
    /* User32 (user32.dll) — GetDC/ReleaseDC operate on window handles and
     * are exported from user32.dll on real Windows, not gdi32.dll despite
     * the "DC" naming making that an easy guess to get wrong. */
    if (strcmp(name,"RegisterClassExA")==0||strcmp(name,"CreateWindowExA")==0||
        strcmp(name,"DefWindowProcA")==0||strcmp(name,"GetMessageA")==0||
        strcmp(name,"PeekMessageA")==0||strcmp(name,"GetDC")==0||
        strcmp(name,"ReleaseDC")==0||
        strcmp(name,"TranslateMessage")==0||strcmp(name,"DispatchMessageA")==0||
        strcmp(name,"PostQuitMessage")==0||
        strcmp(name,"ShowWindow")==0||strcmp(name,"UpdateWindow")==0||
        strcmp(name,"DestroyWindow")==0||strcmp(name,"LoadCursorA")==0||
        strcmp(name,"LoadIconA")==0||strcmp(name,"GetDesktopWindow")==0||
        strcmp(name,"MessageBoxA")==0||
        strcmp(name,"GetClientRect")==0||strcmp(name,"GetWindowRect")==0||
        strcmp(name,"AdjustWindowRect")==0||strcmp(name,"GetSystemMetrics")==0||
        strcmp(name,"InvalidateRect")==0||strcmp(name,"SetWindowTextA")==0||
        strcmp(name,"SetWindowPos")==0||strcmp(name,"MoveWindow")==0||
        strcmp(name,"SetWindowLongPtrA")==0||strcmp(name,"GetWindowLongPtrA")==0||
        strcmp(name,"ClientToScreen")==0) return "user32.dll";
    if (strcmp(name,"GetModuleHandleA")==0) return "KERNEL32.dll";
    /* rand()/srand() — see the identical SI() registration comment in
     * symtable_init() for why this duplicate list entry is needed too. */
    if (strcmp(name,"rand")==0||strcmp(name,"srand")==0) return "msvcrt.dll";
    /* Vulkan (vulkan-1.dll) — see the identical SI() registration list in
     * symtable_init() for why this duplicate list is needed too: a bodyless
     * "RetType Name(...);" prototype (exactly what vulkan_core.h/
     * vulkan_win32.h declare each of these as) gets parsed as a SYM_IMPORT
     * with the placeholder dll "extern", which shadows whatever SI()
     * registered at startup — codegen's fallback for an "extern"-tagged
     * import consults *this* function (find_dll(), via symtable_find_dll()),
     * not the symbol table's own dll field, so the name needs to resolve
     * here too or it silently falls through to the cross-object-call path
     * and never gets a real vulkan-1.dll import at all. */
    if (strncmp(name,"vk",2)==0 && (
        strcmp(name,"vkCreateInstance")==0||strcmp(name,"vkDestroyInstance")==0||
        strcmp(name,"vkEnumeratePhysicalDevices")==0||
        strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")==0||
        strcmp(name,"vkGetPhysicalDeviceMemoryProperties")==0||
        strcmp(name,"vkCreateDevice")==0||strcmp(name,"vkDestroyDevice")==0||
        strcmp(name,"vkGetDeviceQueue")==0||strcmp(name,"vkDestroySurfaceKHR")==0||
        strcmp(name,"vkGetPhysicalDeviceSurfaceSupportKHR")==0||
        strcmp(name,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")==0||
        strcmp(name,"vkGetPhysicalDeviceSurfaceFormatsKHR")==0||
        strcmp(name,"vkGetPhysicalDeviceSurfacePresentModesKHR")==0||
        strcmp(name,"vkCreateSwapchainKHR")==0||strcmp(name,"vkDestroySwapchainKHR")==0||
        strcmp(name,"vkGetSwapchainImagesKHR")==0||strcmp(name,"vkAcquireNextImageKHR")==0||
        strcmp(name,"vkQueuePresentKHR")==0||strcmp(name,"vkCreateImageView")==0||
        strcmp(name,"vkDestroyImageView")==0||strcmp(name,"vkCreateRenderPass")==0||
        strcmp(name,"vkDestroyRenderPass")==0||strcmp(name,"vkCreateFramebuffer")==0||
        strcmp(name,"vkDestroyFramebuffer")==0||strcmp(name,"vkCreateShaderModule")==0||
        strcmp(name,"vkDestroyShaderModule")==0||strcmp(name,"vkCreatePipelineLayout")==0||
        strcmp(name,"vkDestroyPipelineLayout")==0||strcmp(name,"vkCreateGraphicsPipelines")==0||
        strcmp(name,"vkDestroyPipeline")==0||strcmp(name,"vkCreateCommandPool")==0||
        strcmp(name,"vkDestroyCommandPool")==0||strcmp(name,"vkAllocateCommandBuffers")==0||
        strcmp(name,"vkResetCommandBuffer")==0||strcmp(name,"vkBeginCommandBuffer")==0||
        strcmp(name,"vkEndCommandBuffer")==0||strcmp(name,"vkCmdBeginRenderPass")==0||
        strcmp(name,"vkCmdEndRenderPass")==0||strcmp(name,"vkCmdBindPipeline")==0||
        strcmp(name,"vkCmdBindVertexBuffers")==0||strcmp(name,"vkCmdSetViewport")==0||
        strcmp(name,"vkCmdSetScissor")==0||strcmp(name,"vkCmdPushConstants")==0||
        strcmp(name,"vkCmdDraw")==0||strcmp(name,"vkCreateSemaphore")==0||
        strcmp(name,"vkDestroySemaphore")==0||strcmp(name,"vkCreateFence")==0||
        strcmp(name,"vkDestroyFence")==0||strcmp(name,"vkWaitForFences")==0||
        strcmp(name,"vkResetFences")==0||strcmp(name,"vkQueueSubmit")==0||
        strcmp(name,"vkDeviceWaitIdle")==0||strcmp(name,"vkCreateBuffer")==0||
        strcmp(name,"vkDestroyBuffer")==0||strcmp(name,"vkGetBufferMemoryRequirements")==0||
        strcmp(name,"vkAllocateMemory")==0||strcmp(name,"vkFreeMemory")==0||
        strcmp(name,"vkBindBufferMemory")==0||strcmp(name,"vkMapMemory")==0||
        strcmp(name,"vkUnmapMemory")==0||strcmp(name,"vkCreateWin32SurfaceKHR")==0||
        strcmp(name,"vkCmdPipelineBarrier")==0||strcmp(name,"vkCmdCopyImageToBuffer")==0||strcmp(name,"vkQueueWaitIdle")==0
    )) return "vulkan-1.dll";
    /* OpenCL (OpenCL.dll) — see the identical SI() registration list above
     * for why this duplicate list is needed too (same "extern"-tagged
     * bodyless-prototype shadowing find_dll() consults, not SI()'s table). */
    if (strncmp(name,"cl",2)==0 && (
        strcmp(name,"clGetPlatformIDs")==0||strcmp(name,"clGetDeviceIDs")==0||
        strcmp(name,"clGetPlatformInfo")==0||strcmp(name,"clGetDeviceInfo")==0||
        strcmp(name,"clCreateContext")==0||strcmp(name,"clReleaseContext")==0||
        strcmp(name,"clCreateCommandQueue")==0||strcmp(name,"clReleaseCommandQueue")==0||
        strcmp(name,"clFinish")==0||strcmp(name,"clCreateBuffer")==0||
        strcmp(name,"clReleaseMemObject")==0||strcmp(name,"clEnqueueReadBuffer")==0||
        strcmp(name,"clEnqueueWriteBuffer")==0||strcmp(name,"clCreateProgramWithSource")==0||
        strcmp(name,"clBuildProgram")==0||strcmp(name,"clGetProgramBuildInfo")==0||
        strcmp(name,"clReleaseProgram")==0||strcmp(name,"clCreateKernel")==0||
        strcmp(name,"clSetKernelArg")==0||strcmp(name,"clReleaseKernel")==0||
        strcmp(name,"clEnqueueNDRangeKernel")==0
    )) return "OpenCL.dll";
    /* Fall back to real Windows SDK .lib import libraries for every name
     * this hardcoded list doesn't know — real <windows.h>-based programs
     * routinely call API functions (e.g. lstrlenA, wsprintfA) far beyond
     * what's practical to hand-maintain here. See implib.c. */
    return implib_find_dll(name);
}

void symtable_init(SymTable *st, int is_64bit) {
    memset(st,0,sizeof *st);
    st->is_64bit = is_64bit;
    symtable_push_scope(st);
    /* Windows API imports */
    SI("GetStdHandle","KERNEL32.dll"); SI("WriteFile","KERNEL32.dll");
    SI("WriteConsoleA","KERNEL32.dll"); SI("ExitProcess","KERNEL32.dll");
    SI("GetLastError","KERNEL32.dll"); SI("CloseHandle","KERNEL32.dll");
    SI("CreateFileA","KERNEL32.dll"); SI("ReadFile","KERNEL32.dll");
    SI("Sleep","KERNEL32.dll"); SI("GetTickCount","KERNEL32.dll");
    SI("VirtualAlloc","KERNEL32.dll"); SI("VirtualFree","KERNEL32.dll");
    SI("HeapAlloc","KERNEL32.dll"); SI("HeapFree","KERNEL32.dll");
    SI("HeapReAlloc","KERNEL32.dll"); SI("GetProcessHeap","KERNEL32.dll");
    SI("LocalAlloc","KERNEL32.dll"); SI("LocalFree","KERNEL32.dll");
    SI("SetConsoleTextAttribute","KERNEL32.dll");
    SI("GetConsoleScreenBufferInfo","KERNEL32.dll");
    SI("SetConsoleCursorPosition","KERNEL32.dll");
    SI("GetCommandLineA","KERNEL32.dll");
    SI("GetSystemTimeAsFileTime","KERNEL32.dll");
    SI("GetCurrentProcessId","KERNEL32.dll");
    SI("GetCurrentThreadId","KERNEL32.dll");
    SI("InitializeSListHead","KERNEL32.dll");
    SI("GetSystemTime","KERNEL32.dll");
    SI("GetLocalTime","KERNEL32.dll");
    SI("GetCurrentProcess","KERNEL32.dll");
    SI("TerminateProcess","KERNEL32.dll");
    SI("AttachConsole","KERNEL32.dll");
    SI("OutputDebugStringA","KERNEL32.dll");
    SI("OutputDebugStringW","KERNEL32.dll");
    SI("GetConsoleMode","KERNEL32.dll");
    SI("WriteConsoleW","KERNEL32.dll");
    SI("GetEnvironmentStringsW","KERNEL32.dll");
    SI("FreeEnvironmentStringsW","KERNEL32.dll");
    SI("GetEnvironmentVariableA","KERNEL32.dll");
    SI("GetEnvironmentVariableW","KERNEL32.dll");
    SI("SetEnvironmentVariableA","KERNEL32.dll");
    SI("SetEnvironmentVariableW","KERNEL32.dll");
    SI("SetLastError","KERNEL32.dll");
    SI("CreateFileW","KERNEL32.dll");
    SI("GetFileTime","KERNEL32.dll");
    SI("GetFileAttributesW","KERNEL32.dll");
    SI("HeapSetInformation","KERNEL32.dll");
    SI("MultiByteToWideChar","KERNEL32.dll");
    SI("WideCharToMultiByte","KERNEL32.dll");
    SI("CreateSemaphoreW","KERNEL32.dll");
    SI("ReleaseSemaphore","KERNEL32.dll");
    SI("WaitForSingleObject","KERNEL32.dll");
    SI("QueryPerformanceCounter","KERNEL32.dll");
    SI("QueryPerformanceFrequency","KERNEL32.dll");
    SI("GetSystemInfo","KERNEL32.dll");
    SI("GlobalMemoryStatusEx","KERNEL32.dll");
    SI("FormatMessageW","KERNEL32.dll");
    SI("SetErrorMode","KERNEL32.dll");
    SI("SetFilePointerEx","KERNEL32.dll");
    SI("SetFilePointer","KERNEL32.dll");
    SI("GetFileSizeEx","KERNEL32.dll");
    SI("FlushFileBuffers","KERNEL32.dll");
    SI("DeleteFileW","KERNEL32.dll");
    SI("GetFileType","KERNEL32.dll");
    SI("CreateDirectoryW","KERNEL32.dll");
    SI("RemoveDirectoryW","KERNEL32.dll");
    SI("MoveFileExW","KERNEL32.dll");
    SI("CopyFileExW","KERNEL32.dll");
    SI("GetFileAttributesExW","KERNEL32.dll");
    SI("FindFirstFileExW","KERNEL32.dll");
    SI("FindNextFileW","KERNEL32.dll");
    SI("FindClose","KERNEL32.dll");
    SI("GetLogicalDrives","KERNEL32.dll");
    SI("CreateToolhelp32Snapshot","KERNEL32.dll");
    SI("Process32First","KERNEL32.dll");
    SI("Process32Next","KERNEL32.dll");
    SI("Module32First","KERNEL32.dll");
    SI("Module32Next","KERNEL32.dll");
    SI("GetModuleFileNameW","KERNEL32.dll");
    SI("GetCurrentDirectoryW","KERNEL32.dll");
    SI("CreateProcessW","KERNEL32.dll");
    SI("CreatePipe","KERNEL32.dll");
    SI("DuplicateHandle","KERNEL32.dll");
    SI("SetHandleInformation","KERNEL32.dll");
    SI("SetNamedPipeHandleState","KERNEL32.dll");
    SI("GetExitCodeProcess","KERNEL32.dll");
    SI("GenerateConsoleCtrlEvent","KERNEL32.dll");
    SI("LoadLibraryW","KERNEL32.dll");
    SI("GetProcAddress","KERNEL32.dll");
    SI("FreeLibrary","KERNEL32.dll");
    SI("SetUnhandledExceptionFilter","KERNEL32.dll");
    SI("AddVectoredExceptionHandler","KERNEL32.dll");
    SI("RemoveVectoredExceptionHandler","KERNEL32.dll");
    SI("RaiseException","KERNEL32.dll");
    SI("GetModuleHandleW","KERNEL32.dll");
    SI("SetThreadDescription","KERNEL32.dll");
    SI("GetCurrentThread","KERNEL32.dll");
    SI("SetThreadPriority","KERNEL32.dll");
    SI("CreateThread","KERNEL32.dll");
    SI("GetModuleHandle","KERNEL32.dll");
    SI("WaitForSingleObjectEx","KERNEL32.dll");
    SI("InitializeCriticalSection","KERNEL32.dll");
    SI("InitializeCriticalSectionAndSpinCount","KERNEL32.dll");
    SI("EnterCriticalSection","KERNEL32.dll");
    SI("LeaveCriticalSection","KERNEL32.dll");
    SI("TryEnterCriticalSection","KERNEL32.dll");
    SI("DeleteCriticalSection","KERNEL32.dll");
    SI("InterlockedCompareExchange","KERNEL32.dll");

    /* Vulkan (vulkan-1.dll) — flat, non-COM exports. Not covered by
     * implib.c's real-.lib scan since the Vulkan SDK (which ships
     * vulkan-1.lib) isn't part of a standard Windows SDK install and isn't
     * present on this system; explicit registration here is simpler than
     * sourcing/bundling a real vulkan-1.lib for a single-purpose lookup. */
    SI("vkCreateInstance","vulkan-1.dll");
    SI("vkDestroyInstance","vulkan-1.dll");
    SI("vkEnumeratePhysicalDevices","vulkan-1.dll");
    SI("vkGetPhysicalDeviceQueueFamilyProperties","vulkan-1.dll");
    SI("vkGetPhysicalDeviceMemoryProperties","vulkan-1.dll");
    SI("vkCreateDevice","vulkan-1.dll");
    SI("vkDestroyDevice","vulkan-1.dll");
    SI("vkGetDeviceQueue","vulkan-1.dll");
    SI("vkDestroySurfaceKHR","vulkan-1.dll");
    SI("vkGetPhysicalDeviceSurfaceSupportKHR","vulkan-1.dll");
    SI("vkGetPhysicalDeviceSurfaceCapabilitiesKHR","vulkan-1.dll");
    SI("vkGetPhysicalDeviceSurfaceFormatsKHR","vulkan-1.dll");
    SI("vkGetPhysicalDeviceSurfacePresentModesKHR","vulkan-1.dll");
    SI("vkCreateSwapchainKHR","vulkan-1.dll");
    SI("vkDestroySwapchainKHR","vulkan-1.dll");
    SI("vkGetSwapchainImagesKHR","vulkan-1.dll");
    SI("vkAcquireNextImageKHR","vulkan-1.dll");
    SI("vkQueuePresentKHR","vulkan-1.dll");
    SI("vkCreateImageView","vulkan-1.dll");
    SI("vkDestroyImageView","vulkan-1.dll");
    SI("vkCreateRenderPass","vulkan-1.dll");
    SI("vkDestroyRenderPass","vulkan-1.dll");
    SI("vkCreateFramebuffer","vulkan-1.dll");
    SI("vkDestroyFramebuffer","vulkan-1.dll");
    SI("vkCreateShaderModule","vulkan-1.dll");
    SI("vkDestroyShaderModule","vulkan-1.dll");
    SI("vkCreatePipelineLayout","vulkan-1.dll");
    SI("vkDestroyPipelineLayout","vulkan-1.dll");
    SI("vkCreateGraphicsPipelines","vulkan-1.dll");
    SI("vkDestroyPipeline","vulkan-1.dll");
    SI("vkCreateCommandPool","vulkan-1.dll");
    SI("vkDestroyCommandPool","vulkan-1.dll");
    SI("vkAllocateCommandBuffers","vulkan-1.dll");
    SI("vkResetCommandBuffer","vulkan-1.dll");
    SI("vkBeginCommandBuffer","vulkan-1.dll");
    SI("vkEndCommandBuffer","vulkan-1.dll");
    SI("vkCmdBeginRenderPass","vulkan-1.dll");
    SI("vkCmdEndRenderPass","vulkan-1.dll");
    SI("vkCmdBindPipeline","vulkan-1.dll");
    SI("vkCmdBindVertexBuffers","vulkan-1.dll");
    SI("vkCmdSetViewport","vulkan-1.dll");
    SI("vkCmdSetScissor","vulkan-1.dll");
    SI("vkCmdPushConstants","vulkan-1.dll");
    SI("vkCmdDraw","vulkan-1.dll");
    SI("vkCreateSemaphore","vulkan-1.dll");
    SI("vkDestroySemaphore","vulkan-1.dll");
    SI("vkCreateFence","vulkan-1.dll");
    SI("vkDestroyFence","vulkan-1.dll");
    SI("vkWaitForFences","vulkan-1.dll");
    SI("vkResetFences","vulkan-1.dll");
    SI("vkQueueSubmit","vulkan-1.dll");
    SI("vkDeviceWaitIdle","vulkan-1.dll");
    SI("vkCreateBuffer","vulkan-1.dll");
    SI("vkDestroyBuffer","vulkan-1.dll");
    SI("vkGetBufferMemoryRequirements","vulkan-1.dll");
    SI("vkAllocateMemory","vulkan-1.dll");
    SI("vkFreeMemory","vulkan-1.dll");
    SI("vkBindBufferMemory","vulkan-1.dll");
    SI("vkMapMemory","vulkan-1.dll");
    SI("vkUnmapMemory","vulkan-1.dll");
    SI("vkCreateWin32SurfaceKHR","vulkan-1.dll");
    SI("vkCmdPipelineBarrier","vulkan-1.dll");
    SI("vkCmdCopyImageToBuffer","vulkan-1.dll");
    SI("vkQueueWaitIdle","vulkan-1.dll");
    /* OpenCL (OpenCL.dll) — the real Khronos ICD loader, present via the
     * GPU driver. Flat, non-COM exports; see the identical find_dll()
     * entry below for why this duplicate registration is needed (a
     * bodyless prototype gets parsed as SYM_IMPORT with dll="extern",
     * which shadows this SI() registration; codegen's fallback for
     * "extern" imports consults find_dll(), not this table). */
    SI("clGetPlatformIDs","OpenCL.dll");
    SI("clGetDeviceIDs","OpenCL.dll");
    SI("clGetPlatformInfo","OpenCL.dll");
    SI("clGetDeviceInfo","OpenCL.dll");
    SI("clCreateContext","OpenCL.dll");
    SI("clReleaseContext","OpenCL.dll");
    SI("clCreateCommandQueue","OpenCL.dll");
    SI("clReleaseCommandQueue","OpenCL.dll");
    SI("clFinish","OpenCL.dll");
    SI("clCreateBuffer","OpenCL.dll");
    SI("clReleaseMemObject","OpenCL.dll");
    SI("clEnqueueReadBuffer","OpenCL.dll");
    SI("clEnqueueWriteBuffer","OpenCL.dll");
    SI("clCreateProgramWithSource","OpenCL.dll");
    SI("clBuildProgram","OpenCL.dll");
    SI("clGetProgramBuildInfo","OpenCL.dll");
    SI("clReleaseProgram","OpenCL.dll");
    SI("clCreateKernel","OpenCL.dll");
    SI("clSetKernelArg","OpenCL.dll");
    SI("clReleaseKernel","OpenCL.dll");
    SI("clEnqueueNDRangeKernel","OpenCL.dll");
    SI("InterlockedExchange","KERNEL32.dll");
    SI("SleepEx","KERNEL32.dll");
    SI("GetDoubleClickTime","USER32.dll");
    SI("EnumWindows","USER32.dll");
    SI("PostMessageA","USER32.dll");
    SI("PostThreadMessageA","USER32.dll");
    SI("GetWindowThreadProcessId","USER32.dll");
    SI("SHGetFolderPathW","SHELL32.dll");
    SI("_beginthreadex","msvcrt.dll");
    SI("_endthreadex","msvcrt.dll");
    /* WinMM waveOut (custom PRIVATEAUDIO backend, squash_build/sdl_core.inc) */
    SI("waveOutOpen","winmm.dll");
    SI("waveOutClose","winmm.dll");
    SI("waveOutPrepareHeader","winmm.dll");
    SI("waveOutUnprepareHeader","winmm.dll");
    SI("waveOutWrite","winmm.dll");
    SI("waveOutReset","winmm.dll");
    SI("waveOutPause","winmm.dll");
    SI("waveOutRestart","winmm.dll");
    /* GUI/windowing (custom PRIVATE video backend, squash_build/sdl_unity.c) */
    SI("RegisterClassExA","USER32.dll");
    SI("CreateWindowExA","USER32.dll");
    SI("DefWindowProcA","USER32.dll");
    SI("GetMessageA","USER32.dll");
    SI("PeekMessageA","USER32.dll");
    SI("GetDC","USER32.dll");
    SI("ReleaseDC","USER32.dll");
    SI("TranslateMessage","USER32.dll");
    SI("DispatchMessageA","USER32.dll");
    SI("PostQuitMessage","USER32.dll");
    SI("ShowWindow","USER32.dll");
    SI("UpdateWindow","USER32.dll");
    SI("DestroyWindow","USER32.dll");
    SI("LoadCursorA","USER32.dll");
    SI("LoadIconA","USER32.dll");
    SI("GetDesktopWindow","USER32.dll");
    SI("MessageBoxA","USER32.dll");
    SI("GetClientRect","USER32.dll");
    SI("GetWindowRect","USER32.dll");
    SI("AdjustWindowRect","USER32.dll");
    SI("GetSystemMetrics","USER32.dll");
    SI("InvalidateRect","USER32.dll");
    SI("SetWindowTextA","USER32.dll");
    SI("SetWindowPos","USER32.dll");
    SI("MoveWindow","USER32.dll");
    SI("SetWindowLongPtrA","USER32.dll");
    SI("GetWindowLongPtrA","USER32.dll");
    SI("ClientToScreen","USER32.dll");
    SI("GetModuleHandleA","KERNEL32.dll");
    SI("CreateCompatibleDC","gdi32.dll");
    SI("CreateCompatibleBitmap","gdi32.dll");
    SI("CreateDIBSection","gdi32.dll");
    SI("SelectObject","gdi32.dll");
    SI("DeleteDC","gdi32.dll");
    SI("DeleteObject","gdi32.dll");
    SI("BitBlt","gdi32.dll");
    SI("GetDIBits","gdi32.dll");
    SI("GetPixel","gdi32.dll");
    /* Internal shims (handled by codegen directly) */
    SF("malloc"); SF("calloc"); SF("realloc"); SF("free");
    SF("memcpy"); SF("memset"); SF("memcmp"); SF("memmove");
    SF("strlen"); SF("strcpy"); SF("strncpy"); SF("strcmp");
    SF("strncmp"); SF("strcat"); SF("strncat"); SF("strchr");
    SF("strstr"); SF("atoi"); SF("atol"); SF("itoa");
    SF("sprintf"); SF("printf"); SF("puts"); SF("putchar");
    SF("abort"); SF("exit");
    SF("fprintf"); SF("fflush"); SF("fwrite"); SF("fread");
    SF("fopen"); SF("fclose"); SF("fseek"); SF("ftell"); SF("rewind");
    SF("fgets"); SF("fputs"); SF("feof"); SF("fgetc"); SF("fputc");
    SF("ungetc"); SF("remove"); SF("rename"); SF("ferror"); SF("clearerr");
    SF("vfprintf"); SF("vprintf"); SF("snprintf"); SF("vsnprintf");
    SF("perror"); SF("atof"); SF("strrchr"); SF("_snprintf");
    SF("puts"); SF("putchar");
    /* rand()/srand() are real msvcrt.dll exports, not squash-internal
     * shims -- but weren't registered in EITHER this table or find_dll()'s
     * if-chain (unlike every other CRT function above), so a bodyless
     * "int rand(void);" prototype (exactly what stdlib.h declares) fell
     * through every resolution path with no matching DLL and no clear
     * compile error either -- codegen silently emitted a phantom
     * call-to-the-next-instruction (a de-facto no-op that just returns
     * whatever was already in RAX) instead. Confirmed via a real repro: a
     * particle-physics demo seeding positions with "rand() % 2000" got the
     * exact same "random" value on every iteration, because rand() never
     * actually ran or updated any PRNG state. */
    SI("rand","msvcrt.dll");
    SI("srand","msvcrt.dll");
}

void symtable_push_scope(SymTable *st) {
    Scope *s = calloc(1,sizeof(Scope));
    s->parent = st->current; st->current = s;
}

void symtable_pop_scope(SymTable *st) {
    if (!st->current) return;
    Scope *dead = st->current;
    st->current = dead->parent;
    Symbol *sym = dead->head;
    while (sym) {
        Symbol *nx=sym->next;
        free(sym->name); free(sym->dll);
        free(sym); sym=nx;
    }
    free(dead->hbuckets);
    free(dead);
}

static Symbol *alloc_sym(SymTable *st, const char *name, TypeInfo *type, SymKind kind) {
    Symbol *s = calloc(1,sizeof(Symbol));
    s->name = my_strdup(name);
    s->type = type;
    s->kind = kind;
    s->is_64bit = st->is_64bit;
    s->next = st->current->head;
    st->current->head = s;
    return s;
}

static Symbol *alloc_global_sym(SymTable *st, const char *name, TypeInfo *type, SymKind kind) {
    /* Insert into global (bottom) scope */
    Scope *g = st->current;
    while (g->parent) g=g->parent;
    Symbol *s = calloc(1,sizeof(Symbol));
    s->name=my_strdup(name);
    s->type=type;
    s->kind=kind;
    s->is_64bit=st->is_64bit;
    s->next=g->head; g->head=s;
    global_hash_insert(g, s);
    return s;
}


static int symtable_compute_struct_size(SymTable *st, ASTNode *struct_node);

/* Compute a struct/union type's actual ALIGNMENT requirement (the max
 * alignment of any member, recursing into nested struct-typed fields for
 * THEIR true alignment) — NOT the same thing as its total byte size.
 * symtable_compute_struct_size()/its trailing-padding pass both used to
 * conflate the two by capping a nested struct field's alignment at
 * "min(its own total size, 8)" — wrong whenever a struct's size happens to
 * be large (>=8) purely from having several small members, rather than
 * from containing any member that actually NEEDS 8-byte alignment. The
 * real Windows FILETIME struct ({DWORD low; DWORD high;}) is exactly this
 * shape: two 4-byte members, total size 8, but true alignment 4 (matching
 * its largest/only member width) — not 8. Treating it as needing 8-byte
 * alignment inserted a spurious 4-byte padding gap in front of any
 * FILETIME field (and after it, before the next field) inside
 * WIN32_FILE_ATTRIBUTE_DATA (dwFileAttributes + 3 FILETIMEs + 2 DWORDs),
 * shifting the real Windows API's nFileSizeHigh/nFileSizeLow bytes 4
 * bytes out of place from where squash's own field_byte_offset()
 * expected them — SDL_SYS_GetPathInfo() (filesystem/windows/
 * SDL_sysfsops.c) then read nFileSizeHigh as the REAL nFileSizeLow value
 * and nFileSizeLow as uninitialized stack memory past the real
 * (Windows-ABI-correct, unaffected by squash's own miscomputed size)
 * struct's true 36-byte extent — corrupting SDL_GetPathInfo()'s/
 * SDL_GetStorageFileSize()'s reported file size for any code path that
 * stats a file through this exact struct shape. */
int symtable_compute_struct_alignment(SymTable *st, ASTNode *struct_node) {
    if (!struct_node || struct_node->kind != AST_STRUCT_DECL) return 4;
    int max_align = 1;
    for (int i = 0; i < struct_node->struct_decl.nfields; i++) {
        ASTNode *f = struct_node->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD || !f->field.type) continue;
        TypeInfo *ft = f->field.type;
        int fa;
        if (ft->pointer_depth > 0) {
            fa = st->is_64bit ? 8 : 4;
        } else {
            const char *b = ft->base;
            const char *bare = b;
            if (b && strncmp(bare,"struct ",7)==0) bare+=7;
            else if (b && strncmp(bare,"union ",6)==0) bare+=6;
            if (b && bare != b) {
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss = symtable_lookup(st, key);
                fa = (ss && ss->struct_node) ? symtable_compute_struct_alignment(st, ss->struct_node) : 4;
            } else if (b) {
                Symbol *td = symtable_lookup(st, b);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth > 0) {
                    /* Pointer typedef (DECLARE_HANDLE-style "typedef struct
                     * X__ {...} *NAME;", or a plain "typedef void* NAME;").
                     * Must short-circuit to pointer alignment here: the
                     * guard below (pointer_depth==0) meant this case fell
                     * through every branch to the final generic "else fa=4"
                     * default, undercounting the struct's own trailing-
                     * padding alignment whenever its LAST field was a
                     * pointer typedef like this — e.g. DXGI_SWAP_CHAIN_DESC
                     * (ends in "UINT Flags", fine) but more generally any
                     * struct ending in an HWND/HICON/HMODULE/etc field, or
                     * any struct whose sizeof() must round up to 8 because
                     * of one. See field_byte_offset()'s identical fix (same
                     * root cause, different function) for the full story —
                     * found via D3D11CreateDeviceAndSwapChain returning
                     * DXGI_ERROR_INVALID_CALL. */
                    fa = st->is_64bit ? 8 : 4;
                } else if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0 && td->type->base) {
                    const char *tb = td->type->base; const char *tbare = tb;
                    if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                    else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                    if (tbare != tb) {
                        char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                        Symbol *tss = symtable_lookup(st, tkey);
                        fa = (tss && tss->struct_node) ? symtable_compute_struct_alignment(st, tss->struct_node) : 4;
                    } else {
                        fa = typeinfo_size(td->type, st->is_64bit);
                        if (fa < 1 || fa > 8) fa = 4;
                    }
                } else if (td && td->kind == SYM_STRUCT && td->struct_node) {
                    fa = symtable_compute_struct_alignment(st, td->struct_node);
                } else {
                    if (strcmp(b,"char")==0||strcmp(b,"int8_t")==0||strcmp(b,"uint8_t")==0||strcmp(b,"_Bool")==0||strcmp(b,"bool")==0) fa=1;
                    else if (strcmp(b,"short")==0||strcmp(b,"int16_t")==0||strcmp(b,"uint16_t")==0) fa=2;
                    else if (strcmp(b,"double")==0||strcmp(b,"long long")==0||strcmp(b,"int64_t")==0||strcmp(b,"uint64_t")==0) fa=8;
                    else if (strcmp(b,"long")==0) fa = (st->is_64bit && !g_squash_windows_target) ? 8 : 4;
                    else fa=4;
                }
            } else fa = 4;
        }
        if (fa > max_align) max_align = fa;
    }
    if (max_align > 8) max_align = 8;
    return max_align;
}

static int symtable_compute_struct_size(SymTable *st, ASTNode *struct_node);

/* Compute a single field's own size (and, via out_struct_align, its real
 * struct/union alignment if it is struct/union-typed) — shared by
 * symtable_compute_struct_size() and symtable_field_offset() so the two
 * can never independently drift on how a field's size is derived. */
static int symtable_field_size_and_align(SymTable *st, TypeInfo *ft, int *out_struct_align) {
    int fsz;
    /* Real alignment requirement for a struct/union-typed field —
     * see symtable_compute_struct_alignment()'s comment: a nested
     * struct's SIZE (fsz below) is not the same as its ALIGNMENT, so
     * this is tracked separately and only used when the field is
     * actually struct/union-typed (0 = "not a struct field, use the
     * normal size-based alignment further down instead"). */
    int struct_field_align = 0;
    if (ft->pointer_depth > 0) {
        fsz = st->is_64bit ? 8 : 4;
    } else {
            /* Check if it's a struct/union type - look up recursively */
            const char *b = ft->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) {
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss2 = symtable_lookup(st, key);
                if (ss2 && ss2->struct_node) {
                    fsz = symtable_compute_struct_size(st, ss2->struct_node);
                    struct_field_align = symtable_compute_struct_alignment(st, ss2->struct_node);
                }
                else if (ss2 && ss2->struct_size > 0)
                    fsz = ss2->struct_size;
                else
                    fsz = 4;
            } else {
                /* Check if it's a typedef for a struct */
                Symbol *td = symtable_lookup(st, b);
                if (td && td->kind == SYM_TYPEDEF && td->type) {
                    /* Recurse with the resolved typedef type */
                    TypeInfo *rt = td->type;
                    if (rt->pointer_depth > 0) {
                        fsz = st->is_64bit ? 8 : 4;
                    } else {
                        const char *rb = rt->base; const char *rbare = rb;
                        if (strncmp(rbare,"struct ",7)==0) rbare+=7;
                        else if (strncmp(rbare,"union ",6)==0) rbare+=6;
                        if (rbare != rb) {
                            char rkey[256]; snprintf(rkey,sizeof rkey,"struct %s",rbare);
                            Symbol *rss = symtable_lookup(st, rkey);
                            if (rss && rss->struct_node) {
                                fsz = symtable_compute_struct_size(st, rss->struct_node);
                                struct_field_align = symtable_compute_struct_alignment(st, rss->struct_node);
                            }
                            else if (rss && rss->struct_size > 0)
                                fsz = rss->struct_size;
                            else fsz = 4;
                        } else {
                            /* This only resolved ONE typedef hop (e.g. a
                             * struct field "SDL_ThreadID owner;" resolves
                             * "SDL_ThreadID" -> TypeInfo{base="Uint64"} here),
                             * then called the plain typeinfo_size() — which
                             * has NO SymTable access and only recognizes
                             * literal builtin names ("uint64_t", not the
                             * SDL3-style capitalized typedef alias "Uint64"
                             * one hop up from it). Any FURTHER typedef hop
                             * silently fell through to typeinfo_size()'s
                             * "return 4" struct/typedef default, undercounting
                             * the WHOLE struct's size by however many bytes
                             * the real field type actually needs beyond 4 —
                             * confirmed via a minimal repro: a struct field
                             * typed through a 2-hop typedef chain
                             * (SDL_ThreadID-shaped: alias -> alias -> real
                             * builtin) made the struct's total computed size
                             * 8 bytes too small, corrupting whatever memory
                             * follows once code allocates only that
                             * (too-small) size. Keep resolving through the
                             * symbol table (bounded, matching the pattern
                             * used elsewhere in this file) instead of giving
                             * up after one hop. */
                            TypeInfo cursor; cursor = *rt;
                            int hops = 0;
                            fsz = 4;
                            while (hops++ < 8) {
                                int cand = typeinfo_size(&cursor, st->is_64bit);
                                if (cand != 4) { fsz = cand; break; }
                                Symbol *ntd = symtable_lookup(st, cursor.base);
                                if (ntd && ntd->kind == SYM_TYPEDEF && ntd->type &&
                                    !(ntd->type->base && strcmp(ntd->type->base, cursor.base) == 0)) {
                                    if (ntd->type->pointer_depth > 0) { fsz = st->is_64bit ? 8 : 4; break; }
                                    cursor = *ntd->type;
                                } else {
                                    break; /* genuinely unresolvable further — keep the 4-byte default */
                                }
                            }
                            if (fsz < 1) fsz = 4;
                        }
                    }
                } else if (td && td->kind == SYM_STRUCT) {
                    if (td->struct_node) {
                        fsz = symtable_compute_struct_size(st, td->struct_node);
                        struct_field_align = symtable_compute_struct_alignment(st, td->struct_node);
                    }
                    else if (td->struct_size > 0)
                        fsz = td->struct_size;
                    else fsz = 4;
                } else {
                    /* Primitive type */
                    if (strcmp(b,"char")==0||strcmp(b,"int8_t")==0||strcmp(b,"uint8_t")==0||strcmp(b,"_Bool")==0||strcmp(b,"bool")==0) fsz=1;
                    else if (strcmp(b,"short")==0||strcmp(b,"int16_t")==0||strcmp(b,"uint16_t")==0) fsz=2;
                    else if (strcmp(b,"float")==0) fsz=4;
                    else if (strcmp(b,"long")==0) fsz = (st->is_64bit && !g_squash_windows_target) ? 8 : 4;
                    else if (strcmp(b,"double")==0||strcmp(b,"long long")==0||strcmp(b,"int64_t")==0||strcmp(b,"uint64_t")==0) fsz=8;
                    else fsz=4; /* int, unsigned int, etc */
                }
            }
    }
    if (out_struct_align) *out_struct_align = struct_field_align;
    return fsz;
}

/* Compute the actual size of a struct/union type, recursively resolving
 * nested struct fields. Returns size in bytes with proper field alignment. */
static int symtable_compute_struct_size(SymTable *st, ASTNode *struct_node) {
    if (!struct_node || struct_node->kind != AST_STRUCT_DECL) return 4;
    int is_union = struct_node->struct_decl.is_union;
    int total = 0;
    for (int i = 0; i < struct_node->struct_decl.nfields; i++) {
        ASTNode *f = struct_node->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD || !f->field.type) continue;
        TypeInfo *ft = f->field.type;
        int struct_field_align = 0;
        int fsz = symtable_field_size_and_align(st, ft, &struct_field_align);
        int elem_fsz = fsz; /* element size, before array-count multiplication */
        if (f->field.array_size > 0) {
            fsz *= f->field.array_size;
            if (f->field.array_size2 > 0) fsz *= f->field.array_size2; /* T x[N][M] */
        }
        if (fsz < 1) fsz = 1;
        if (is_union) {
            if (fsz > total) total = fsz;
        } else {
            /* Align field: struct/union-typed fields use their OWN real
             * alignment (struct_field_align, computed above); everything
             * else uses element size capped at 8 (correct for primitives,
             * where size always equals natural alignment). */
            int align = struct_field_align > 0 ? struct_field_align : (elem_fsz < 8 ? elem_fsz : 8);
            if (align > 1) total = (total + align - 1) & ~(align - 1);
            total += fsz;
        }
    }
    if (total < 1) total = 1;
    /* Add trailing padding to align the struct size to its own alignment
     * (max natural alignment of any field, capped at 8) — reuse
     * symtable_compute_struct_alignment()'s already-correct per-field
     * resolution (pointer/struct-via-typedef/primitive) instead of a
     * separate, independently-bugged copy of the same logic that (a)
     * treated any struct/union-typed field as needing 8-byte alignment
     * regardless of its real member alignment, and (b) didn't even
     * resolve typedef'd struct field types (e.g. "FILETIME foo;", where
     * "FILETIME" is a typedef alias, not a literal "struct "/"union "
     * prefix) to know they were struct-typed at all. */
    if (!is_union) {
        int max_align = symtable_compute_struct_alignment(st, struct_node);
        if (max_align > 1) total = (total + max_align - 1) & ~(max_align - 1);
    }
    return total;
}

/* Resolve a type name ("struct Foo", "union Foo", or a bare typedef alias
 * like "FILETIME") down to its underlying struct/union declaration node,
 * chasing typedef chains (bounded, matching the pattern used throughout
 * this file for the same reason: real Windows headers commonly typedef a
 * struct several hops deep, e.g. "typedef struct _FOO FOO;" then further
 * aliases of FOO itself). Returns NULL if type_name doesn't ultimately name
 * a struct/union (e.g. it's a primitive, or truly unresolvable). */
ASTNode *symtable_resolve_struct_node(SymTable *st, const char *type_name) {
    int hops = 8;
    while (type_name && hops-- > 0) {
        const char *bare = type_name;
        if (strncmp(bare,"struct ",7)==0) bare += 7;
        else if (strncmp(bare,"union ",6)==0) bare += 6;
        if (bare != type_name) {
            char key[256];
            snprintf(key,sizeof key,"struct %s",bare);
            Symbol *ss = symtable_lookup(st,key);
            if (ss && ss->struct_node) return ss->struct_node;
            snprintf(key,sizeof key,"union %s",bare);
            ss = symtable_lookup(st,key);
            if (ss && ss->struct_node) return ss->struct_node;
            return NULL;
        }
        Symbol *td = symtable_lookup(st, type_name);
        if (td && td->kind == SYM_STRUCT && td->struct_node) return td->struct_node;
        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0 &&
            td->type->base && strcmp(td->type->base, type_name) != 0) {
            type_name = td->type->base;
            continue;
        }
        return NULL;
    }
    return NULL;
}

/* Compute the byte offset of a named top-level field within struct_node,
 * mirroring symtable_compute_struct_size()'s own field layout exactly (via
 * the shared symtable_field_size_and_align() helper) so the two can never
 * disagree. On success returns the offset (>=0) and, if out_type is
 * non-NULL, stores the field's own TypeInfo* (for chaining through a
 * further member-access, e.g. offsetof-style "&(((T*)0)->a.b)"). If
 * out_elem_size is non-NULL, stores the field's PER-ELEMENT size (ignoring
 * any array count) for indexing into an array field. Returns -1 if
 * field_name isn't a direct member of struct_node. */
int symtable_field_offset(SymTable *st, ASTNode *struct_node, const char *field_name,
                           TypeInfo **out_type, int *out_elem_size) {
    if (!struct_node || struct_node->kind != AST_STRUCT_DECL || !field_name) return -1;
    int is_union = struct_node->struct_decl.is_union;
    int total = 0;
    for (int i = 0; i < struct_node->struct_decl.nfields; i++) {
        ASTNode *f = struct_node->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD || !f->field.type) continue;
        TypeInfo *ft = f->field.type;
        int struct_field_align = 0;
        int fsz = symtable_field_size_and_align(st, ft, &struct_field_align);
        int elem_fsz = fsz;
        int is_match = f->field.name && strcmp(f->field.name, field_name) == 0;
        if (f->field.array_size > 0) {
            fsz *= f->field.array_size;
            if (f->field.array_size2 > 0) fsz *= f->field.array_size2;
        }
        if (fsz < 1) fsz = 1;
        if (is_union) {
            if (is_match) {
                if (out_type) *out_type = ft;
                if (out_elem_size) *out_elem_size = elem_fsz;
                return 0; /* every union member starts at offset 0 */
            }
            if (fsz > total) total = fsz;
        } else {
            int align = struct_field_align > 0 ? struct_field_align : (elem_fsz < 8 ? elem_fsz : 8);
            if (align > 1) total = (total + align - 1) & ~(align - 1);
            if (is_match) {
                if (out_type) *out_type = ft;
                if (out_elem_size) *out_elem_size = elem_fsz;
                return total;
            }
            total += fsz;
        }
    }
    return -1;
}

Symbol *symtable_define_var(SymTable *st, const char *name, TypeInfo *type) {
    int ptr_size = st->is_64bit ? 8 : 4;
    int slot;
    if (type && type->array_size > 0) {
        /* Array: allocate elem_size * count bytes contiguously.
         * C arrays are stored element-by-element at their natural size.
         * Minimum slot is ptr_size so a zero-element array still has room. */
        int elem_size = typeinfo_size(type, st->is_64bit);
        if (elem_size < 1) elem_size = 4;
        /* For struct/union element types, look up actual size in symtable */
        if (type->base && type->pointer_depth == 0) {
            const char *b = type->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) {
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss = symtable_lookup(st, key);
                if (ss && ss->struct_size > 0) elem_size = ss->struct_size;
                else if (ss && ss->struct_node) elem_size = symtable_compute_struct_size(st, ss->struct_node);
            } else {
                /* Try as typedef */
                Symbol *td = symtable_lookup(st, b);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0) {
                    const char *tb = td->type->base;
                    const char *tbare = tb;
                    if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                    else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                    if (tbare != tb) {
                        char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                        Symbol *tss = symtable_lookup(st, tkey);
                        if (tss && tss->struct_size > 0) elem_size = tss->struct_size;
                        else if (tss && tss->struct_node) elem_size = symtable_compute_struct_size(st, tss->struct_node);
                    }
                }
            }
        }
        slot = type->array_size * elem_size;
        if (type->array_size2 > 0) slot *= type->array_size2; /* T x[N][M] */
        if (slot < ptr_size) slot = ptr_size;
    } else {
        /* For struct/union types, look up the actual stored size */
        slot = typeinfo_size(type, st->is_64bit);
        if (slot <= 0) slot = ptr_size;
        /* For struct/union, use the struct_size from symtable if available */
        if (type && type->base && type->pointer_depth == 0) {
            const char *b = type->base;
            const char *bare = b;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            if (bare != b) { /* it is a struct or union type */
                char key[256]; snprintf(key,sizeof key,"struct %s",bare);
                Symbol *ss=symtable_lookup(st,key);
                if (ss && ss->struct_size > 0) slot=ss->struct_size;
            }
        }
        if (slot < ptr_size) slot = ptr_size;
        /* Override with recursively-computed size for nested structs or typedefs */
        if (type->base && type->pointer_depth == 0) {
            const char *b2 = type->base;
            const char *bare2 = b2;
            if (strncmp(bare2,"struct ",7)==0) bare2+=7;
            else if (strncmp(bare2,"union ",6)==0) bare2+=6;
            if (bare2 != b2) {
                char key2[256]; snprintf(key2,sizeof key2,"struct %s",bare2);
                Symbol *ss2 = symtable_lookup(st, key2);
                if (ss2 && ss2->struct_node) {
                    int real_sz = symtable_compute_struct_size(st, ss2->struct_node);
                    if (real_sz > slot) slot = real_sz;
                }
            } else {
                /* Try as typedef — e.g. PPState *st = ..., where PPState is a typedef for a struct */
                Symbol *td = symtable_lookup(st, b2);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0) {
                    const char *tb = td->type->base;
                    const char *tbare = tb;
                    if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                    else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                    if (tbare != tb) {
                        char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                        Symbol *tss = symtable_lookup(st, tkey);
                        if (tss && tss->struct_node) {
                            int real_sz = symtable_compute_struct_size(st, tss->struct_node);
                            if (real_sz > slot) slot = real_sz;
                        } else if (tss && tss->struct_size > 0 && tss->struct_size > slot) {
                            slot = tss->struct_size;
                        }
                    }
                } else if (td && td->kind == SYM_STRUCT) {
                    if (td->struct_size > slot) slot = td->struct_size;
                }
            }
        }
    }
    int aligned_slot = (slot + 15) & ~15; /* align to 16 for safety */
    st->next_offset -= aligned_slot;
    Symbol *s = alloc_sym(st, name, type, SYM_VAR);
    s->offset = st->next_offset;
    s->array_size = type ? type->array_size : -1;
    s->array_size2 = type ? type->array_size2 : 0;
    s->slot_size = aligned_slot;
    return s;
}

Symbol *symtable_define_global(SymTable *st, const char *name, TypeInfo *type, int array_size) {
    Symbol *s = alloc_global_sym(st, name, type, SYM_GLOBAL);
    s->array_size = array_size;
    s->array_size2 = type ? type->array_size2 : 0;
    return s;
}

Symbol *symtable_define_param(SymTable *st, const char *name, TypeInfo *type, int idx, int byte_offset_32) {
    Symbol *s = alloc_sym(st, name, type, SYM_PARAM);
    s->param_index = idx;
    if (st->is_64bit && st->is_linux) {
        if (idx < 6) {
            /* Linux SysV: home reg params in local frame at fixed negative
             * offsets, BELOW the SQ_SYSV_CALLEE_SAVE_WORDS reserved for the
             * pushed callee-saved GPRs (see its own comment in symtable.h)
             * so param storage never overlaps that region. Avoids conflict
             * with stack-passed args (args 7+) which arrive at [rbp+16]+. */
            s->offset = -(SQ_SYSV_CALLEE_SAVE_WORDS+idx+1)*8;
            /* Reserve this slot so local variable allocation doesn't overlap. */
            int claimed = -(SQ_SYSV_CALLEE_SAVE_WORDS+idx+2)*8;
            if (st->next_offset > claimed) st->next_offset = claimed;
        } else {
            /* Stack-passed args (7th param onwards): arrive at [rbp+16+(idx-6)*8] per SysV ABI. */
            s->offset = 16 + (idx-6)*8;
        }
    } else {
        s->offset = st->is_64bit ? 16+idx*8 : 8+byte_offset_32;
    }
    return s;
}

Symbol *symtable_define_func(SymTable *st, const char *name, TypeInfo *ret, int paramc, ASTNode *node) {
    Symbol *s = alloc_global_sym(st, name, ret, SYM_FUNC);
    s->paramc = paramc; s->func_node = node;
    return s;
}

Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll) {
    Symbol *s = alloc_global_sym(st, name, typeinfo_new("int"), SYM_IMPORT);
    s->dll = my_strdup(dll); s->paramc = -1;
    return s;
}

/* Like symtable_define_import(), but for the "extern RET name(params);"
 * bodyless-prototype case (parser_new4.c's ParseFunction) where the REAL
 * declared return type is already known — pass it through instead of
 * hardcoding "int". Without this, codegen_is_float_expr()'s AST_CALL case
 * (which checks a call's symbol's ->type to decide whether the result
 * needs to be read from XMM0 as a float/double) always says "no" for any
 * such call, since every genuine Win32-API import registered via the
 * SI()/symtable_define_import() convention really does mean "int" (or a
 * pointer/BOOL, close enough) — but a function like SDL_sin/SDL_cos/
 * __kernel_sin, declared exactly the same way ("extern double name(...)")
 * because its real definition lives in a SEPARATELY-COMPILED .sqo object,
 * is NOT a real int-returning DLL import at all. The caller then read the
 * call's result out of RAX (an unrelated leftover value from whatever the
 * callee's own stack/frame bookkeeping happened to leave there) and
 * cvtsi2sd'd THAT into the destination double — this is why every SDL3
 * math function (sin/cos/sqrt/pow/atan2/...) called across the
 * sdl_common.sqo boundary returned a wrong, constant, input-independent
 * value regardless of the real math result, silently generating all-zero
 * (or otherwise wrong) audio sample data. */
Symbol *symtable_define_import_typed(SymTable *st, const char *name, const char *dll, TypeInfo *rettype) {
    Symbol *s = alloc_global_sym(st, name, rettype ? typeinfo_copy(rettype) : typeinfo_new("int"), SYM_IMPORT);
    s->dll = my_strdup(dll); s->paramc = -1;
    return s;
}

Symbol *symtable_define_enum_val(SymTable *st, const char *name, long long val) {
    TypeInfo *ti = typeinfo_new("int");
    Symbol *s = alloc_global_sym(st, name, ti, SYM_ENUM_VAL);
    s->enum_value = val;
    return s;
}

Symbol *symtable_define_typedef(SymTable *st, const char *name, TypeInfo *type) {
    Symbol *s = alloc_global_sym(st, name, type, SYM_TYPEDEF);
    return s;
}

Symbol *symtable_define_struct(SymTable *st, const char *name, ASTNode *node, int sz) {
    char key[256]; snprintf(key,sizeof key,"struct %s",name);
    Symbol *s = alloc_global_sym(st, key, typeinfo_new(key), SYM_STRUCT);
    s->struct_node = node;
    /* Compute accurate struct size using recursive field traversal */
    int real_sz = symtable_compute_struct_size(st, node);
    s->struct_size = (real_sz > sz) ? real_sz : sz;
    return s;
}

int symtable_sizeof_struct(SymTable *st, ASTNode *struct_node) {
    return symtable_compute_struct_size(st, struct_node);
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    Symbol *found = NULL;
    Scope *sc = st->current;
    while (sc && !found) {
        if (sc->hcap > 0) {
            /* Global scope (see Scope.hbuckets' comment): hash lookup
             * instead of scanning every symbol ever declared at file
             * scope. Chain order (newest-inserted first) matches the
             * plain-list scan below, so a same-name redefinition within
             * this scope still resolves to the same symbol either way. */
            unsigned long h = sym_hash(name) % (unsigned long)sc->hcap;
            for (Symbol *s = sc->hbuckets[h]; s; s = s->hnext) {
                if (strcmp(s->name, name) == 0) { found = s; break; }
            }
        } else {
            Symbol *s = sc->head;
            while (s) {
                if (strcmp(s->name, name) == 0) { found = s; break; }
                s = s->next;
            }
        }
        if (!found) sc = sc->parent;
    }
    return found;
}


void symtable_reset_locals(SymTable *st) { st->next_offset=0; }
int  symtable_local_size  (SymTable *st) { int sz=-st->next_offset; return (sz+15)&~15; }

void symtable_add_import(SymTable *st, const char *dll_func) {
    for (int i=0;i<st->import_count;i++)
        if (strcmp(st->imports[i],dll_func)==0) return;
    if (st->import_count==st->import_cap) {
        st->import_cap = st->import_cap ? st->import_cap*2 : 16;
        st->imports = realloc(st->imports, st->import_cap*sizeof(char*));
    }
    st->imports[st->import_count++] = my_strdup(dll_func);
}

const char *symtable_find_dll(SymTable *st, const char *name) {
    (void)st;
    return find_dll(name);
}

void symtable_print(const SymTable *st) {
    printf("=== Symbol Table ===\n");
    Scope *sc=st->current; int d=0;
    while(sc) {
        printf(" Scope[%d]:\n",d++);
        for (Symbol *s=sc->head;s;s=s->next) {
            const char *kn[] = {"var","param","func","import","enum","typedef","struct","global"};
            printf("  %s %s",kn[s->kind],s->name);
            if (s->kind==SYM_VAR||s->kind==SYM_PARAM) printf("[%+d]",s->offset);
            if (s->dll) printf("(%s)",s->dll);
            printf("\n");
        }
        sc=sc->parent;
    }
    printf("Imports: %d\n",st->import_count);
    for (int i=0;i<st->import_count;i++) printf("  %s\n",st->imports[i]);
}
