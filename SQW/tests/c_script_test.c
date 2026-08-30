/* Phase 6f end-to-end test: compiles a raw ".c" script (exactly what a
 * "<script type=\"text/c\">" block's inline text is -- see sqw_run_c_
 * script()'s own comment: no lowering step at all, unlike the C# path)
 * via a real "squash -c" subprocess, then loads and runs it in-process
 * through sqo_loader.c, matching sqo_loader_test.c's/dllimport_vulkan_
 * test.c's own established pattern. Proves a raw C script can call
 * straight into CSR/csharp_rt.h (Console.WriteLine's own real C
 * function) with no C# involved at all. */
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
    const char *c_src =
        "#include \"csharp_rt.h\"\n"
        "int main(void) {\n"
        "    csr_console_write_line(cs_string_new(\"hello-from-raw-c-script\"));\n"
        "    return 0;\n"
        "}\n";
    FILE *f;
    SqoLoaded m;
    SqoHostSymbol syms[SQO_HOST_SYMS_COUNT + SQO_HOST_SYMS_VULKAN_COUNT];
    int n_rt, n_vk, n_syms;
    int (*main_fn)(void);

    f = fopen("/tmp/c_script_test.c", "w");
    fputs(c_src, f);
    fclose(f);

    CHECK(system("./squash -c -linux -64 -ICSR /tmp/c_script_test.c -o /tmp/c_script_test.sqo >/tmp/c_script_test_compile.log 2>&1") == 0,
          "real 'squash -c' compile of a raw text/c script succeeds");

    n_rt = sqo_host_syms_csharp_rt(syms);
    n_vk = sqo_host_syms_vulkan(syms + n_rt);
    n_syms = n_rt + n_vk;

    CHECK(sqo_loader_load("/tmp/c_script_test.sqo", syms, n_syms, &m) == 1,
          "sqo_loader_load() succeeds: mmap + relocation patching against csharp_rt + vulkan host symbols");

    main_fn = (int (*)(void))sqo_loader_get_symbol(&m, "main");
    CHECK(main_fn != 0, "sqo_loader_get_symbol() finds the raw script's own 'main' export");
    if (main_fn) {
        printf("--- calling the raw C script's main() in-process ---\n");
        fflush(stdout);
        main_fn();
        fflush(stdout);
    }

    sqo_loader_free(&m);
    remove("/tmp/c_script_test.c");
    remove("/tmp/c_script_test.sqo");

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
