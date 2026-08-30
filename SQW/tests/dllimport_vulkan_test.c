/* Phase 6c end-to-end test: compiles a real C# script that declares AND
 * CALLS a [DllImport("vulkan")] native function (vkEnumerateInstanceVersion
 * -- a real Vulkan entry point, real libvulkan.so.1) via "out uint", real
 * P/Invoke-style by-reference out-parameter marshaling, then loads and
 * runs it in-process through the same sqo_loader.c mechanism
 * sqo_loader_test.c already proved for CSR/csharp_rt.h calls -- just
 * against sqo_host_syms_vulkan()'s table instead. SQW itself can't run in
 * this sandbox (no display/Vulkan device), but vkEnumerateInstanceVersion
 * needs neither -- it only talks to the Vulkan LOADER (libvulkan.so.1's
 * own dispatch, no ICD/GPU involved for this one call), so this proves the
 * whole DllImport pipeline for real, standalone.
 *
 * This test also caught a real squash codegen.c bug (squash bug #12 this
 * session, fixed, not worked around): a "-c" precompile of a call to a
 * bodyless "extern <ret> name(...);" declaration (exactly what a
 * [DllImport] method lowers to) emitted a RELOC_IAT_REL32 relocation
 * (dynamic-import addressing) instead of RELOC_STATIC_REL32 (deferred
 * named call, resolved later against a sibling ".sqo"/host symbol table)
 * -- codegen.c's SYM_IMPORT/dll=="extern" call-emission branch checked
 * only "cg->prefer_static_calls && codegen_is_sqo_export(...)" (which is
 * always false during "-c", since there ARE no linked ".sqo" files yet to
 * search), missing the "|| cg->sqo_precompile" fallback its SYM_FUNC
 * sibling branch already had. This broke EVERY [DllImport] call under
 * "-c" (confirmed with a trivial "extern int abs(int x);" too, not just
 * Vulkan-specific) -- sqo_loader.c correctly refused to load the result
 * ("unsupported relocation kind 5 ... only DATA/WDATA/STATIC_REL32 are
 * supported") rather than silently mishandling it. Fixed in codegen.c by
 * adding the same "|| cg->sqo_precompile" fallback to both occurrences of
 * this branch. */
#include "../sqo_loader.c"
#include "../sqo_host_syms.c"
#include "../../CSR/csharp_rt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int system(const char *command);

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } } while (0)

int main(void) {
    const char *cs_src =
        "class Program {\n"
        "  [DllImport(\"vulkan\")]\n"
        "  static extern int vkEnumerateInstanceVersion(out uint apiVersion);\n"
        "  static void Main() {\n"
        "    uint version;\n"
        "    int result = vkEnumerateInstanceVersion(out version);\n"
        "    Console.WriteLine(\"vkEnumerateInstanceVersion-called-from-csharp\");\n"
        "    Console.WriteLine(result);\n"
        "  }\n"
        "}\n";
    FILE *f;
    SqoLoaded m;
    SqoHostSymbol syms[SQO_HOST_SYMS_COUNT + SQO_HOST_SYMS_VULKAN_COUNT];
    int n_syms, n_rt, n_vk;
    void (*main_fn)(void);
    int (*vk_enum_version)(unsigned int *);
    unsigned int api_version = 0;

    f = fopen("/tmp/dllimport_vulkan_test.cs", "w");
    fputs(cs_src, f);
    fclose(f);

    CHECK(system("./squash -c -linux -64 /tmp/dllimport_vulkan_test.cs -o /tmp/dllimport_vulkan_test.sqo >/tmp/dllimport_vulkan_test_compile.log 2>&1") == 0,
          "real 'squash -c' compile of a [DllImport] C# script to a standalone .sqo succeeds");

    n_rt = sqo_host_syms_csharp_rt(syms);
    n_vk = sqo_host_syms_vulkan(syms + n_rt);
    n_syms = n_rt + n_vk;
    CHECK(n_vk == SQO_HOST_SYMS_VULKAN_COUNT, "vulkan host symbol table populated");

    CHECK(sqo_loader_load("/tmp/dllimport_vulkan_test.sqo", syms, n_syms, &m) == 1,
          "sqo_loader_load() succeeds: mmap + relocation patching against csharp_rt + vulkan host symbols");

    main_fn = (void (*)(void))sqo_loader_get_symbol(&m, "Program__Main");
    CHECK(main_fn != 0, "sqo_loader_get_symbol() finds the 'Program__Main' export");
    if (main_fn) main_fn();

    /* The C# script's OWN "vkEnumerateInstanceVersion" export IS the
     * extern declaration itself (a bodyless function with no C definition
     * in the .sqo -- it's a pure external reference, resolved entirely by
     * the relocation patch against sqo_host_syms_vulkan()'s trampoline).
     * There's nothing to look up by that name in the LOADED script's own
     * export table (it never defines it) -- instead, call the SAME real
     * function directly through the host table's own address, proving
     * the exact function sqo_loader just resolved the script's call
     * against is a real, working Vulkan entry point. */
    {
        int i;
        vk_enum_version = 0;
        for (i = 0; i < n_syms; i++) {
            if (strcmp(syms[i].name, "vkEnumerateInstanceVersion") == 0) { vk_enum_version = (int (*)(unsigned int *))syms[i].addr; break; }
        }
    }
    CHECK(vk_enum_version != 0, "vkEnumerateInstanceVersion resolved in the host symbol table");
    if (vk_enum_version) {
        int rc = vk_enum_version(&api_version);
        printf("vkEnumerateInstanceVersion() -> VkResult=%d, apiVersion=0x%08x (variant=%u major=%u minor=%u patch=%u)\n",
               rc, api_version, api_version >> 29, (api_version >> 22) & 0x7f, (api_version >> 12) & 0x3ff, api_version & 0xfff);
        CHECK(rc == 0, "real vkEnumerateInstanceVersion() call succeeds (VK_SUCCESS)");
        CHECK(api_version != 0, "real vkEnumerateInstanceVersion() reports a non-zero API version");
    }

    sqo_loader_free(&m);
    remove("/tmp/dllimport_vulkan_test.cs");
    remove("/tmp/dllimport_vulkan_test.sqo");

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
