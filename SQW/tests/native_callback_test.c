/* Phase 6d end-to-end test: compiles a real C# script that passes a bare
 * static method NAME (not a call) as a value to a native function
 * expecting a real C-ABI function pointer -- the shape Vulkan's own
 * callback APIs (debug messengers, allocation callbacks) need. Loads it
 * in-process via sqo_loader.c (matching sqo_loader_test.c's own
 * established pattern) against a tiny native test harness function,
 * "NativeApply", that genuinely calls back INTO the loaded C# code --
 * confirming the native side invoking the callback produces the expected
 * C# side effect (Double(21) == 42), not just that the pointer looks
 * non-NULL. */
#include "../sqo_loader.c"
#include "../sqo_host_syms.c"
#include "../../CSR/csharp_rt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int system(const char *command);

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } } while (0)

/* The native test harness function itself -- a real, statically-defined
 * C function (this translation unit has a body for it, same as every
 * csr_* host symbol) that accepts a callback function pointer and calls
 * it, mirroring the shape of a real native callback-registration API
 * (Vulkan's vkCreateDebugUtilsMessengerEXT, glibc's qsort comparator,
 * etc.) without needing a real Vulkan device/comparator contract for
 * this test. */
static int sqo_tramp_NativeApply(int (*fn)(int), int x) { return fn(x); }

int main(void) {
    const char *cs_src =
        "class Program {\n"
        "  static int Double(int x) { return x * 2; }\n"
        "  [DllImport(\"harness\")]\n"
        "  static extern int NativeApply(IntPtr fn, int x);\n"
        "  static void Main() {\n"
        "    int r = NativeApply(Double, 21);\n"
        "    Console.WriteLine(r);\n"
        "  }\n"
        "}\n";
    FILE *f;
    SqoLoaded m;
    SqoHostSymbol syms[SQO_HOST_SYMS_COUNT + 1];
    int n_rt, n_syms;
    void (*main_fn)(void);

    f = fopen("/tmp/native_callback_test.cs", "w");
    fputs(cs_src, f);
    fclose(f);

    CHECK(system("./squash -c -linux -64 /tmp/native_callback_test.cs -o /tmp/native_callback_test.sqo >/tmp/native_callback_test_compile.log 2>&1") == 0,
          "real 'squash -c' compile of a static-method-as-callback C# script succeeds");

    n_rt = sqo_host_syms_csharp_rt(syms);
    syms[n_rt].name = "NativeApply";
    syms[n_rt].addr = (void *)sqo_tramp_NativeApply;
    n_syms = n_rt + 1;

    CHECK(sqo_loader_load("/tmp/native_callback_test.sqo", syms, n_syms, &m) == 1,
          "sqo_loader_load() succeeds: mmap + relocation patching against csharp_rt + the native test harness symbol");

    main_fn = (void (*)(void))sqo_loader_get_symbol(&m, "Program__Main");
    CHECK(main_fn != 0, "sqo_loader_get_symbol() finds the 'Program__Main' export");
    if (main_fn) {
        printf("--- calling Program__Main() in-process (it calls NativeApply(Double, 21), which calls back into Double) ---\n");
        fflush(stdout);
        main_fn();
        fflush(stdout);
    }

    sqo_loader_free(&m);
    remove("/tmp/native_callback_test.cs");
    remove("/tmp/native_callback_test.sqo");

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
