/* Phase 7d/7e verification: confirms the full teapot/cube/sphere Vulkan
 * 3D-rendering C# program (SQW/testpages/vulkan_3d_models_test.html)
 * compiles cleanly via a real "squash -c" and LOADS via sqo_loader_load()
 * against the complete host-symbol table (csharp_rt + vulkan + libc/libm
 * + the 12 SQW app accessors) with zero unresolved symbols -- catching
 * the large majority of struct-layout/signature mistakes without needing
 * a live GPU/display (this sandbox has neither -- see the plan's own
 * documented verification-bar note for Phase 7). Program__Main() is
 * deliberately NOT invoked here: it calls real Vulkan object-creation
 * functions against a real SQW device/render-pass obtained through the
 * app accessors below, which only return non-NULL when a real
 * SqwAppState/SqwVkContext exists (i.e. inside a running, windowed SQW)
 * -- this harness stubs those accessors just enough to prove every name
 * the script references resolves to *some* real, callable function
 * pointer.
 *
 * This test also found a real squash codegen bug (independent of
 * anything Vulkan/C#-specific): "ptr + int_local_a + int_local_b" (two
 * chained additions inline in a function-call ARGUMENT expression)
 * corrupts the callee's writes once 3+ such calls share one buffer --
 * confirmed via a minimal repro (three trivial functions filling a
 * shared struct array). Worked around here (and in the real
 * SQW/sqw_main.c host-symbol-table construction, which has the exact
 * same shape) by hoisting each running offset into its own local before
 * adding it to the base pointer. */
#include "../sqo_loader.c"
#include "../sqo_host_syms.c"
#include "../../CSR/csharp_rt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int system(const char *command);

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } fflush(stdout); } while (0)

/* Stub SQW app accessors -- same names/signatures sqw_main.c's real
 * sqw_host_syms_app() registers, just enough for relocation resolution.
 * Parameter named "realname" (not "name") deliberately in the S() macro
 * below -- see its own comment. */
static void *stub_get_device(void) { return (void *)0x1; }
static void *stub_get_render_pass(void) { return (void *)0x2; }
static void stub_get_extent(unsigned int *w, unsigned int *h) { *w = 800; *h = 600; }
static int stub_find_memory_type(unsigned int typeBits, unsigned int properties) { return 0; }
static void stub_register_render_callback(void *fn) { (void)fn; }
static void stub_register_click_handler(CsString *id, void *fn) { (void)id; (void)fn; }
static void stub_set_element_text(CsString *id, CsString *text) { (void)id; (void)text; }
static void *stub_get_physical_device(void) { return (void *)0x3; }
static void *stub_get_command_pool(void) { return (void *)0x4; }
static void *stub_get_queue(void) { return (void *)0x5; }
static unsigned int stub_get_queue_family(void) { return 0; }
static void *stub_get_mem_props(void) { return (void *)0x6; }

static int sqw_host_syms_app_stub(SqoHostSymbol *out) {
    int n = 0;
    /* Parameter named "realname" (not "name") deliberately -- a macro
     * parameter literally named "name" textually collides with the
     * struct field access "out[n].name" during preprocessing (the C
     * preprocessor has no notion of "." as a substitution boundary), so
     * "out[n].name = ...;" silently expands to "out[n].<the argument
     * token>", producing a bogus member access. Confirmed via a minimal
     * repro this session (plain C macro hygiene, not squash-specific)
     * that this corrupts adjacent struct entries. Matches the existing
     * SYM/VKSYM macros' own parameter names in sqo_host_syms.c
     * (fn/realname), which were never susceptible to this. */
#define S(realname, tramp) do { out[n].name = #realname; out[n].addr = (void *)tramp; n++; } while (0)
    S(SqwRegisterRenderCallback, stub_register_render_callback);
    S(SqwRegisterClickHandler, stub_register_click_handler);
    S(SqwSetElementText, stub_set_element_text);
    S(SqwGetDevice, stub_get_device);
    S(SqwGetPhysicalDevice, stub_get_physical_device);
    S(SqwGetRenderPass, stub_get_render_pass);
    S(SqwGetCommandPool, stub_get_command_pool);
    S(SqwGetQueue, stub_get_queue);
    S(SqwGetQueueFamily, stub_get_queue_family);
    S(SqwGetExtent, stub_get_extent);
    S(SqwGetMemoryProperties, stub_get_mem_props);
    S(SqwFindMemoryType, stub_find_memory_type);
#undef S
    return n;
}

int main(void) {
    SqoLoaded m;
    SqoHostSymbol syms[SQO_HOST_SYMS_COUNT + SQO_HOST_SYMS_VULKAN_COUNT + SQO_HOST_SYMS_LIBC_COUNT + 12];
    int n_rt, n_vk, n_libc, n_app, n_syms;
    int off_libc, off_app;
    void (*main_fn)(void);
    int compile_rc;
    int load_rc;

    compile_rc = system("./squash -c -linux -64 "
                 "SQW/testpages/vulkan_3d_models_test.cs "
                 "-o /tmp/vulkan_3d_models_test.sqo "
                 ">/tmp/vulkan_3d_models_test_compile.log 2>&1");
    CHECK(compile_rc == 0, "real 'squash -c' compile of the full teapot/cube/sphere C# program succeeds");

    /* Each offset hoisted into its own local before use -- see file
     * header comment on the chained-pointer-arithmetic codegen bug. */
    n_rt = sqo_host_syms_csharp_rt(syms);
    n_vk = sqo_host_syms_vulkan(syms + n_rt);
    off_libc = n_rt + n_vk;
    n_libc = sqo_host_syms_libc(syms + off_libc);
    off_app = off_libc + n_libc;
    n_app = sqw_host_syms_app_stub(syms + off_app);
    n_syms = off_app + n_app;
    CHECK(n_libc == SQO_HOST_SYMS_LIBC_COUNT, "libc/libm host symbol table has all entries");
    CHECK(n_app == 12, "stub app host symbol table has all 12 entries");

    load_rc = sqo_loader_load("/tmp/vulkan_3d_models_test.sqo", syms, n_syms, &m);
    CHECK(load_rc == 1,
          "sqo_loader_load() succeeds: every relocation (csharp_rt + vulkan + libc/libm + SQW app accessors) resolves with zero unresolved symbols");

    main_fn = (void (*)(void))sqo_loader_get_symbol(&m, "Program__Main");
    CHECK(main_fn != 0, "sqo_loader_get_symbol() finds the 'Program__Main' export");

    /* Program__Main() is not called here -- see file header comment: it
     * needs a real Vulkan device/render-pass from a running, windowed SQW,
     * which this sandbox cannot provide (no display/GPU-backed window). */

    sqo_loader_free(&m);
    remove("/tmp/vulkan_3d_models_test.sqo");

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
