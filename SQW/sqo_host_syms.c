#include "sqo_host_syms.h"
#include "../CSR/csharp_rt.h"
#include "../include/vulkan_core.h"

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
    return n;
}

#undef VKSYM
#undef SYM
