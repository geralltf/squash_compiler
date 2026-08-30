#ifndef SQO_HOST_SYMS_H
#define SQO_HOST_SYMS_H
#include "sqo_loader.h"

/* Every CSR/csharp_rt.h function a loaded ".cs" script's own `-c` output
 * can call by name (see sqo_loader.h's own header comment: a script
 * compiled without merging csharp_rt.sqo in at compile time still
 * references these as ordinary unresolved RELOC_STATIC_REL32 calls,
 * exactly like any other external symbol) -- built once at SQW startup
 * from real `&csr_gc_alloc`-style addresses in SQW's OWN process (SQW
 * links CSR/csharp_rt.c directly, the same #include-unity pattern
 * sqw_main.c already uses for every one of its other dependencies), not
 * looked up via dlsym() (squash-produced binaries don't carry that kind
 * of runtime symbol table for a plain built-in .c file). Returns the
 * number of entries written into `out` (caller supplies a big-enough
 * array -- see SQO_HOST_SYMS_COUNT). */
#define SQO_HOST_SYMS_COUNT 64

int sqo_host_syms_csharp_rt(SqoHostSymbol *out);

/* Phase 6c: a small, hand-picked subset of real Vulkan entry points, by
 * real in-process address (SQW already links libvulkan.so.1 directly for
 * its own renderer, SQW/vk_context.c -- these are the SAME real
 * functions, not a stub/shim), for a `[DllImport("vulkan")]`-declared C#
 * method to call. Scoped to cover one real end-to-end smoke test
 * (vkEnumerateInstanceVersion -- just a pointer-to-uint32 out-param, no
 * struct marshaling needed) plus enough of the instance/device-
 * enumeration surface to be a genuinely useful starting point
 * (vkCreateInstance/vkDestroyInstance/vkEnumeratePhysicalDevices) --
 * NOT all ~200+ Vulkan entry points (a full auto-generated table parsed
 * from vulkan_core.h is a reasonable follow-up once real scripts need
 * more of the API; this is trivially extensible by adding more SYM()
 * lines to sqo_host_syms_vulkan()'s own definition, same pattern as
 * sqo_host_syms_csharp_rt() above). */
#define SQO_HOST_SYMS_VULKAN_COUNT 8

int sqo_host_syms_vulkan(SqoHostSymbol *out);

#endif /* SQO_HOST_SYMS_H */
