#include "sqo_host_syms.h"
#include "../CSR/csharp_rt.h"
#include "../include/vulkan_core.h"
#include "../include/math.h"
#include "../include/string.h"

/* vkEnumerateInstanceVersion isn't declared in this repo's own
 * include/vulkan_core.h shim (it only carries the subset SQW/vk_context.c
 * itself uses) -- but SQW already links the real libvulkan.so.1 directly
 * (see SQW/vk_context.c), so the real symbol resolves at link time same
 * as any other Vulkan call; this is just its real, spec-accurate
 * prototype, needed here only to take its address. */
VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion);

/* NOTE: `(void *)fn`, NOT `(void *)&fn` -- a real squash codegen bug
 * (found this session, the ninth): applying the unary `&` operator
 * directly to a FUNCTION name (as opposed to a bare function name, which
 * already decays to a pointer per ordinary C rules, or a function-
 * pointer-TYPED variable holding one) reads back NULL under squash,
 * confirmed via a minimal repro (`void (*p)(void) = &my_func;` -> NULL;
 * `void (*p)(void) = my_func;` -> the real address; both forms are
 * required to be equivalent by the C standard, and gcc treats them
 * identically). Every entry in this table came back `(nil)` under squash
 * before this was found. `(void *)fn` sidesteps it entirely and works
 * identically to `(void *)&fn` under gcc. ISO C technically forbids a
 * function-pointer -> void* conversion at all (the same "issue" POSIX's
 * own dlsym() has, which is why POSIX explicitly carves out an exception
 * for it) -- harmless under both compilers actually used to build this
 * file (SQW builds through squash, not gcc -Wall -pedantic). */
#define SYM(fn) do { out[n].name = #fn; out[n].addr = (void *)fn; n++; } while (0)

int sqo_host_syms_csharp_rt(SqoHostSymbol *out) {
    int n = 0;
    SYM(csr_hdr_of);
    SYM(csr_payload_of);
    SYM(csr_type_id_of);
    SYM(csr_gc_alloc);
    SYM(csr_gc_push_root);
    SYM(csr_gc_pop_root);
    SYM(csr_gc_collect);
    SYM(csr_gc_register_type_layout);
    SYM(csr_gc_live_bytes);
    SYM(csr_gc_alloc_count);
    SYM(csr_gc_collect_count);
    SYM(csr_try_push);
    SYM(csr_try_pop);
    SYM(csr_throw);
    SYM(csr_exception_matches);
    SYM(csr_current_exception);
    SYM(csr_current_exception_type);
    SYM(csr_register_exception_base);
    SYM(csr_rethrow);
    SYM(cs_string_new);
    SYM(cs_string_new_len);
    SYM(cs_string_concat);
    SYM(cs_string_eq);
    SYM(cs_string_cmp);
    SYM(cs_string_from_int);
    SYM(cs_string_from_double);
    SYM(cs_string_substring);
    SYM(csr_list_new);
    SYM(csr_list_add);
    SYM(csr_list_get);
    SYM(csr_list_set);
    SYM(csr_list_remove_at);
    SYM(csr_list_clear);
    SYM(csr_list_count);
    SYM(csr_list_copy);
    SYM(csr_list_data);
    SYM(csr_string_data);
    SYM(csr_dict_new);
    SYM(csr_dict_set);
    SYM(csr_dict_try_get);
    SYM(csr_dict_contains_key);
    SYM(csr_dict_count);
    SYM(csr_console_write_line);
    SYM(csr_console_write);
    SYM(csr_linq_where);
    SYM(csr_linq_select);
    SYM(csr_linq_order_by);
    SYM(csr_linq_count);
    SYM(csr_linq_sum_int);
    /* A real squash codegen bug found this session (#13, documented, not
     * fixed -- doesn't block any current work): taking the ADDRESS of a
     * float/double-RETURNING function via the ordinary bare-name decay
     * this whole SYM() macro relies on (confirmed: an int-returning
     * function's address is fine, a float/double-returning one's is not)
     * reads back NULL, unlike every other entry in this table -- a
     * DIFFERENT bug from the earlier-documented "&func" (explicit
     * address-of) one (#9): this is the bare, auto-decaying form, the
     * form #9's own fix said was safe. Confirmed via a minimal repro
     * (a bare "double foo(double x)"'s "(void*)foo" is NULL under
     * squash, the real address under gcc). Left in the table rather
     * than removed -- a C# script calling this specific function would
     * fail to load with "unresolved function symbol", a clear, honest
     * failure mode, not a silent wrong call through a NULL pointer. */
    SYM(csr_linq_sum_double);
    SYM(csr_linq_first);
    SYM(csr_linq_first_where);
    SYM(csr_linq_any);
    SYM(csr_linq_all);
    SYM(csr_linq_to_list);
    return n;
}

/* Vulkan functions are DYNAMIC imports (SQW links the real libvulkan.so.1
 * shared library, not a statically-defined function this same
 * translation unit has a body for -- unlike every csr_* entry above).
 * Taking their address directly with the bare-name form (`(void*)fn`,
 * SYM()'s usual trick) still fails under squash: confirmed via a direct
 * repro this session -- squash's linker only knows how to resolve
 * RELOC_STATIC_REL32 (a PC-relative CALL instruction) against a
 * statically-linked function; there's no equivalent relocation kind here
 * for "load this dynamic import's address into a data value" the way an
 * ordinary CALL to the same function already works fine (that's how
 * SQW/vk_context.c calls real Vulkan functions today) -- so every entry
 * in this table came back a squash link error ("unresolved static
 * symbol: vkCreateInstance", etc.) before this was found. Sidestepped
 * with a one-line CALL-through trampoline per function, defined right
 * here (a real, statically-defined function THIS translation unit has a
 * body for, so its address is taken exactly like any csr_* entry above)
 * -- the host symbol table still exposes each entry under its real
 * Vulkan NAME (sqo_loader/cs_lower.c only care about the name, never how
 * the host side obtained the address), so a `[DllImport("vulkan")]`
 * C# declaration for e.g. "vkCreateInstance" resolves to this trampoline
 * transparently. */
static VkResult sqo_tramp_vkEnumerateInstanceVersion(uint32_t *pApiVersion) { return vkEnumerateInstanceVersion(pApiVersion); }
static VkResult sqo_tramp_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const void *pAllocator, VkInstance *pInstance) { return vkCreateInstance(pCreateInfo, pAllocator, pInstance); }
static void     sqo_tramp_vkDestroyInstance(VkInstance instance, const void *pAllocator) { vkDestroyInstance(instance, pAllocator); }
static VkResult sqo_tramp_vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices) { return vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices); }
static void     sqo_tramp_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties) { vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties); }
static void     sqo_tramp_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties) { vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties); }
static VkResult sqo_tramp_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const void *pAllocator, VkDevice *pDevice) { return vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice); }
static void     sqo_tramp_vkDestroyDevice(VkDevice device, const void *pAllocator) { vkDestroyDevice(device, pAllocator); }

/* Phase 7: pipeline/shader/buffer/command trampolines, needed for a C#
 * script to build and record its own graphics pipeline against SQW's
 * real, already-created device (see SQW/sqw_main.c's own SqwGetDevice()/
 * etc host symbols) -- same dynamic-import-address workaround as every
 * other entry above, not a new mechanism. */
static VkResult sqo_tramp_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const void *pAllocator, VkShaderModule *pShaderModule) { return vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule); }
static void     sqo_tramp_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const void *pAllocator) { vkDestroyShaderModule(device, shaderModule, pAllocator); }
static VkResult sqo_tramp_vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const void *pAllocator, VkPipelineLayout *pPipelineLayout) { return vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout); }
static void     sqo_tramp_vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const void *pAllocator) { vkDestroyPipelineLayout(device, pipelineLayout, pAllocator); }
static VkResult sqo_tramp_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const void *pAllocator, VkPipeline *pPipelines) { return vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines); }
static void     sqo_tramp_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const void *pAllocator) { vkDestroyPipeline(device, pipeline, pAllocator); }
static VkResult sqo_tramp_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const void *pAllocator, VkBuffer *pBuffer) { return vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer); }
static void     sqo_tramp_vkDestroyBuffer(VkDevice device, VkBuffer buffer, const void *pAllocator) { vkDestroyBuffer(device, buffer, pAllocator); }
static void     sqo_tramp_vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements) { vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements); }
static VkResult sqo_tramp_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const void *pAllocator, VkDeviceMemory *pMemory) { return vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory); }
static void     sqo_tramp_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const void *pAllocator) { vkFreeMemory(device, memory, pAllocator); }
static VkResult sqo_tramp_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) { return vkBindBufferMemory(device, buffer, memory, memoryOffset); }
static VkResult sqo_tramp_vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData) { return vkMapMemory(device, memory, offset, size, flags, ppData); }
static void     sqo_tramp_vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { vkUnmapMemory(device, memory); }
static void     sqo_tramp_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) { vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline); }
static void     sqo_tramp_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) { vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets); }
static void     sqo_tramp_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t indexType) { vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType); }
static void     sqo_tramp_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) { vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance); }
static void     sqo_tramp_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues) { vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues); }

#define VKSYM(realname, tramp) do { out[n].name = #realname; out[n].addr = (void *)tramp; n++; } while (0)

int sqo_host_syms_vulkan(SqoHostSymbol *out) {
    int n = 0;
    VKSYM(vkEnumerateInstanceVersion, sqo_tramp_vkEnumerateInstanceVersion);
    VKSYM(vkCreateInstance, sqo_tramp_vkCreateInstance);
    VKSYM(vkDestroyInstance, sqo_tramp_vkDestroyInstance);
    VKSYM(vkEnumeratePhysicalDevices, sqo_tramp_vkEnumeratePhysicalDevices);
    VKSYM(vkGetPhysicalDeviceQueueFamilyProperties, sqo_tramp_vkGetPhysicalDeviceQueueFamilyProperties);
    VKSYM(vkGetPhysicalDeviceMemoryProperties, sqo_tramp_vkGetPhysicalDeviceMemoryProperties);
    VKSYM(vkCreateDevice, sqo_tramp_vkCreateDevice);
    VKSYM(vkDestroyDevice, sqo_tramp_vkDestroyDevice);
    VKSYM(vkCreateShaderModule, sqo_tramp_vkCreateShaderModule);
    VKSYM(vkDestroyShaderModule, sqo_tramp_vkDestroyShaderModule);
    VKSYM(vkCreatePipelineLayout, sqo_tramp_vkCreatePipelineLayout);
    VKSYM(vkDestroyPipelineLayout, sqo_tramp_vkDestroyPipelineLayout);
    VKSYM(vkCreateGraphicsPipelines, sqo_tramp_vkCreateGraphicsPipelines);
    VKSYM(vkDestroyPipeline, sqo_tramp_vkDestroyPipeline);
    VKSYM(vkCreateBuffer, sqo_tramp_vkCreateBuffer);
    VKSYM(vkDestroyBuffer, sqo_tramp_vkDestroyBuffer);
    VKSYM(vkGetBufferMemoryRequirements, sqo_tramp_vkGetBufferMemoryRequirements);
    VKSYM(vkAllocateMemory, sqo_tramp_vkAllocateMemory);
    VKSYM(vkFreeMemory, sqo_tramp_vkFreeMemory);
    VKSYM(vkBindBufferMemory, sqo_tramp_vkBindBufferMemory);
    VKSYM(vkMapMemory, sqo_tramp_vkMapMemory);
    VKSYM(vkUnmapMemory, sqo_tramp_vkUnmapMemory);
    VKSYM(vkCmdBindPipeline, sqo_tramp_vkCmdBindPipeline);
    VKSYM(vkCmdBindVertexBuffers, sqo_tramp_vkCmdBindVertexBuffers);
    VKSYM(vkCmdBindIndexBuffer, sqo_tramp_vkCmdBindIndexBuffer);
    VKSYM(vkCmdDrawIndexed, sqo_tramp_vkCmdDrawIndexed);
    VKSYM(vkCmdPushConstants, sqo_tramp_vkCmdPushConstants);
    return n;
}

/* Phase 7c: procedural geometry (a lathed teapot-profile revolve, a UV
 * sphere) needs libm's sinf/cosf/sqrtf/tanf; buffer upload needs libc's
 * memcpy -- SQW already links -lm/-lc (every C program does), so these
 * are the real functions, resolved through the same call-through-
 * trampoline workaround as sqo_host_syms_vulkan() above (taking the
 * address of a DYNAMIC-import symbol directly fails at squash's own
 * link stage, see that table's own comment for the full explanation;
 * a real, statically-defined trampoline in THIS translation unit sidesteps
 * it, same as every VKSYM entry does). */
#define LIBSYM(realname, tramp) do { out[n].name = #realname; out[n].addr = (void *)tramp; n++; } while (0)

static float sqo_tramp_sinf(float x) { return sinf(x); }
static float sqo_tramp_cosf(float x) { return cosf(x); }
/* sqrtf/tanf implemented WITHOUT calling any external libm entry point
 * at all -- a real, confirmed squash bug (independent of anything else
 * in this file, found while chasing why a Phase 7 C# page's 3D geometry
 * rendered as degenerate/invisible garbage): calling the real libm
 * "sqrtf"/"tanf" through a squash-compiled call site returns the input
 * unchanged (sqrtf) or 0.0 (tanf), while "sinf"/"cosf" -- compiled and
 * linked exactly the same way -- return correct results. Routing
 * through the DOUBLE-precision "sqrt"/"tan" entry points instead (float
 * -> double -> call -> float) fixed a DIRECT call to the trampoline, but
 * broke again once the trampoline's own address is taken and called
 * INDIRECTLY (this table's whole reason to exist -- sqo_loader.c always
 * calls host symbols through a resolved address, never by name):
 * confirmed via a minimal repro that an indirect call to a function
 * combining a float<->double narrowing/widening conversion AROUND a
 * real external double-returning call reads back 0, while the identical
 * function called DIRECTLY (by name, no address involved) is correct,
 * and while a PURE double-in/double-out wrapper with no float
 * conversion at all (or a pure-float wrapper making only float<->float
 * external calls, like sinf/cosf's own trampolines) is correct via
 * BOTH direct and indirect calls. Root cause not fully isolated (glibc
 * exports sqrtf/tanf as *symbol-versioned* entries -- e.g. both
 * "sqrtf@GLIBC_2.2.5" and "sqrtf@@GLIBC_2.43" exist side by side, per
 * `nm -D libm.so.6` -- while sinf/cosf are GNU-indirect-function
 * symbols; the float<->double conversion combined with an indirect call
 * to code containing an external double-returning call is the specific
 * shape that corrupts XMM0's return value). Rather than keep chasing an
 * ABI-level bug two layers deep, sidestepped entirely: sqrtf is a
 * self-contained Newton's-method iteration (no external call of any
 * kind), tanf is sinf(x)/cosf(x) (both proven safe under indirect calls
 * already, pure float<->float, no double intermediate) -- confirmed via
 * the same minimal-repro discipline that both forms are correct under
 * direct AND indirect calls before adopting them here. */
static float sqo_tramp_sqrtf(float x) {
    float guess;
    int i;
    if (x <= 0.0f) return 0.0f;
    guess = x;
    for (i = 0; i < 12; i++) guess = 0.5f * (guess + x / guess);
    return guess;
}
static float sqo_tramp_tanf(float x) { return sinf(x) / cosf(x); }
static void  sqo_tramp_memcpy(void *dest, const void *src, unsigned long n) { memcpy(dest, src, n); }

int sqo_host_syms_libc(SqoHostSymbol *out) {
    int n = 0;
    /* squash bug #13 (already documented above at csr_linq_sum_double)
     * strikes again here, confirmed via a minimal repro: "(void *)fn" for
     * a float/double-RETURNING fn reads back NULL via the ordinary bare-
     * name-decay LIBSYM/SYM/VKSYM all rely on -- but assigning the SAME
     * bare name to a properly float-return-typed FUNCTION POINTER
     * VARIABLE first, then casting THAT variable, reads back the correct
     * address (confirmed: this is a different codegen path than the
     * direct case-of-a-bare-name-in-an-expression one). Applied only to
     * the four float-returning entries below -- sqo_tramp_memcpy returns
     * void, unaffected, still goes through the ordinary LIBSYM macro. */
    {
        float (*p_sinf)(float) = sqo_tramp_sinf;
        float (*p_cosf)(float) = sqo_tramp_cosf;
        float (*p_sqrtf)(float) = sqo_tramp_sqrtf;
        float (*p_tanf)(float) = sqo_tramp_tanf;
        out[n].name = "sinf";  out[n].addr = (void *)p_sinf;  n++;
        out[n].name = "cosf";  out[n].addr = (void *)p_cosf;  n++;
        out[n].name = "sqrtf"; out[n].addr = (void *)p_sqrtf; n++;
        out[n].name = "tanf";  out[n].addr = (void *)p_tanf;  n++;
    }
    LIBSYM(memcpy, sqo_tramp_memcpy);
    return n;
}

#undef LIBSYM
#undef VKSYM
#undef SYM
