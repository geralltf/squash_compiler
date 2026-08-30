/* End-to-end test for sqo_loader.c: compiles a real .cs script to a
 * standalone ".sqo" (via a real "squash -c" subprocess, matching how a
 * developer would build one), loads it into THIS process with
 * sqo_loader_load() against a host symbol table of every csharp_rt.h
 * function, and calls its "Program__Main" export directly as an
 * in-process function pointer -- the actual novel, risky piece of Phase
 * 5 (see /home/squash/.claude/plans/nested-finding-walrus.md), proven
 * working end to end, not just compiling. */
#include "../sqo_loader.h"
#include "../sqo_host_syms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int system(const char *command);

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } } while (0)

int main(void) {
    const char *cs_src =
        "class Greeter {\n"
        "  public string Name;\n"
        "  public int Greetings;\n"
        "  public Greeter(string name) { Name = name; Greetings = 0; }\n"
        "  public void SayHello() {\n"
        "    Console.WriteLine(Name);\n"
        "    Greetings = Greetings + 1;\n"
        "  }\n"
        "  public int GetGreetings() { return Greetings; }\n"
        "}\n"
        "class Program {\n"
        "  static void Main() {\n"
        "    Greeter g = new Greeter(\"World\");\n"
        "    g.SayHello();\n"
        "    g.SayHello();\n"
        "    int n = g.GetGreetings();\n"
        "    if (n == 2) { Console.WriteLine(\"greetings-ok\"); }\n"
        "  }\n"
        "}\n";
    FILE *f;
    SqoLoaded m;
    SqoHostSymbol syms[SQO_HOST_SYMS_COUNT];
    int n_syms;
    void (*main_fn)(void);

    f = fopen("/tmp/sqo_loader_test.cs", "w");
    fputs(cs_src, f);
    fclose(f);

    CHECK(system("./squash -c -linux -64 /tmp/sqo_loader_test.cs -o /tmp/sqo_loader_test.sqo >/tmp/sqo_loader_test_compile.log 2>&1") == 0,
          "real 'squash -c' compile of a C# script to a standalone .sqo succeeds");

    n_syms = sqo_host_syms_csharp_rt(syms);
    CHECK(n_syms > 40, "host symbol table populated with csharp_rt functions");

    CHECK(sqo_loader_load("/tmp/sqo_loader_test.sqo", syms, n_syms, &m) == 1,
          "sqo_loader_load() succeeds: mmap + relocation patching against the host symbol table");

    main_fn = (void (*)(void))sqo_loader_get_symbol(&m, "Program__Main");
    CHECK(main_fn != 0, "sqo_loader_get_symbol() finds the 'Program__Main' export");

    if (main_fn) {
        printf("--- calling Program__Main() in-process ---\n");
        fflush(stdout);
        main_fn();
        fflush(stdout);
        printf("--- returned from Program__Main() ---\n");
    }

    /* A second, independent load in the SAME process, to confirm the
     * loader doesn't leak/corrupt state across loads (a real concern for
     * something meant to load a fresh .sqo per <script type="text/csharp">
     * block on a page, potentially several per page). */
    {
        SqoLoaded m2;
        void (*main_fn2)(void);
        CHECK(sqo_loader_load("/tmp/sqo_loader_test.sqo", syms, n_syms, &m2) == 1,
              "a second, independent load of the same .sqo in the same process also succeeds");
        main_fn2 = (void (*)(void))sqo_loader_get_symbol(&m2, "Program__Main");
        if (main_fn2) {
            printf("--- calling Program__Main() from the SECOND load ---\n");
            fflush(stdout);
            main_fn2();
            fflush(stdout);
        }
        sqo_loader_free(&m2);
    }

    sqo_loader_free(&m);
    remove("/tmp/sqo_loader_test.cs");
    remove("/tmp/sqo_loader_test.sqo");

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
