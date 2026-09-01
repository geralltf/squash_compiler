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
#define SQO_HOST_SYMS_COUNT 65

int sqo_host_syms_csharp_rt(SqoHostSymbol *out);

/* Phase 6c (instance/device enumeration) + Phase 7 (real graphics
 * pipeline creation/recording -- shader modules, pipeline layout/
 * pipeline, buffers, memory, and the vkCmd* calls needed to bind and
 * draw): a hand-picked subset of real Vulkan entry points, by real
 * in-process address (SQW already links libvulkan.so.1 directly for its
 * own renderer, SQW/vk_context.c -- these are the SAME real functions,
 * not a stub/shim), for a `[DllImport("vulkan")]`-declared C# method to
 * call -- NOT all ~200+ Vulkan entry points (a full auto-generated table
 * parsed from vulkan_core.h is a reasonable follow-up once real scripts
 * need more of the API; this is trivially extensible by adding more
 * VKSYM() lines to sqo_host_syms_vulkan()'s own definition, same pattern
 * as sqo_host_syms_csharp_rt() above). Vulkan OBJECTS a script needs
 * (device, physical device, render pass, command pool, queue) come from
 * a SEPARATE table, SQW/sqw_main.c's own sqo_host_syms_app() -- SQW's
 * real, already-initialized ones, not created fresh by the script. */
#define SQO_HOST_SYMS_VULKAN_COUNT 27

int sqo_host_syms_vulkan(SqoHostSymbol *out);

/* Phase 7c: libm/libc functions procedural geometry and buffer upload
 * need (sinf/cosf/sqrtf/tanf, memcpy) -- see sqo_host_syms_libc()'s own
 * comment in sqo_host_syms.c for why these go through trampolines like
 * sqo_host_syms_vulkan()'s own entries, not direct addresses. */
#define SQO_HOST_SYMS_LIBC_COUNT 5

int sqo_host_syms_libc(SqoHostSymbol *out);

#endif /* SQO_HOST_SYMS_H */
