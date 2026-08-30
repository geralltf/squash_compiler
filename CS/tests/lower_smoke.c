/* Phase 3 end-to-end smoke test: parses a small C# program, lowers it to
 * C source text, writes it to a file next to this test binary, and
 * prints the generated source + lowering error count so the harness
 * script (see tools/tests/cs_lower_e2e_test.sh) can feed it through a
 * REAL compiler (gcc first, then squash) and check the compiled
 * program's actual runtime output -- proving the whole pipeline, not
 * just that cs_lower.c itself runs without crashing. */
#include "../cs_parser.h"
#include "../cs_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lower_one(const char *src, const char *out_path) {
    CsParser p;
    CsNode *unit;
    CsLowerResult r;
    FILE *f;

    cs_parser_init(&p, src);
    unit = cs_parse_unit(&p);
    if (p.error_count > 0) {
        fprintf(stderr, "PARSE FAILED: %d errors\n", p.error_count);
        exit(1);
    }
    r = cs_lower_unit(unit, "csharp_rt.h");
    if (!r.ok) {
        fprintf(stderr, "LOWER FAILED: %d errors\n", r.error_count);
        exit(1);
    }
    f = fopen(out_path, "w");
    if (!f) { fprintf(stderr, "cannot open %s for writing\n", out_path); exit(1); }
    fputs(r.text, f);
    fclose(f);
    printf("wrote %s (%d bytes)\n", out_path, (int)strlen(r.text));
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "1";
    const char *out_path = argc > 2 ? argv[2] : "/tmp/cs_lowered.c";

    if (strcmp(which, "1") == 0) {
        const char *src =
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
            "    if (n == 2) {\n"
            "      Console.WriteLine(\"OK\");\n"
            "    } else {\n"
            "      Console.WriteLine(\"FAIL\");\n"
            "    }\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else if (strcmp(which, "2") == 0) {
        /* control flow + arithmetic + string interpolation */
        const char *src =
            "class Program {\n"
            "  static void Main() {\n"
            "    int total = 0;\n"
            "    for (int i = 1; i <= 5; i = i + 1) {\n"
            "      total = total + i;\n"
            "    }\n"
            "    string msg = $\"total={total}\";\n"
            "    Console.WriteLine(msg);\n"
            "    int j = 0;\n"
            "    while (j < 3) { j = j + 1; }\n"
            "    switch (j) {\n"
            "      case 3:\n"
            "        Console.WriteLine(\"three\");\n"
            "        break;\n"
            "      default:\n"
            "        Console.WriteLine(\"other\");\n"
            "        break;\n"
            "    }\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else if (strcmp(which, "3") == 0) {
        /* two classes calling each other, static method, 'is' operator */
        const char *src =
            "class Animal {\n"
            "  public int Legs;\n"
            "  public Animal(int legs) { Legs = legs; }\n"
            "  public static Animal MakeDog() { return new Animal(4); }\n"
            "}\n"
            "class Program {\n"
            "  static void Main() {\n"
            "    Animal a = Animal.MakeDog();\n"
            "    if (a is Animal) {\n"
            "      Console.WriteLine(\"is-animal-ok\");\n"
            "    }\n"
            "    int legs = a.Legs;\n"
            "    if (legs == 4) { Console.WriteLine(\"legs-ok\"); }\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else if (strcmp(which, "4") == 0) {
        /* List<T> -- construction, Add, Count, indexing (get+set), foreach, RemoveAt/Clear */
        const char *src =
            "class Program {\n"
            "  static void Main() {\n"
            "    List<int> nums = new List<int>();\n"
            "    nums.Add(10);\n"
            "    nums.Add(20);\n"
            "    nums.Add(30);\n"
            "    if (nums.Count == 3) { Console.WriteLine(\"count-ok\"); }\n"
            "    int second = nums[1];\n"
            "    if (second == 20) { Console.WriteLine(\"index-get-ok\"); }\n"
            "    nums[1] = 99;\n"
            "    if (nums[1] == 99) { Console.WriteLine(\"index-set-ok\"); }\n"
            "    int total = 0;\n"
            "    foreach (int n in nums) { total = total + n; }\n"
            "    if (total == 139) { Console.WriteLine(\"foreach-ok\"); }\n"
            "    nums.RemoveAt(0);\n"
            "    if (nums.Count == 2) { Console.WriteLine(\"removeat-ok\"); }\n"
            "    nums.Clear();\n"
            "    if (nums.Count == 0) { Console.WriteLine(\"clear-ok\"); }\n"
            "    List<string> names = new List<string>();\n"
            "    names.Add(\"alice\");\n"
            "    names.Add(\"bob\");\n"
            "    string joined = \"\";\n"
            "    foreach (string s in names) { joined = joined + s + \",\"; }\n"
            "    Console.WriteLine(joined);\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else if (strcmp(which, "5") == 0) {
        /* Phase 6b: real value-type struct -- ctor, instance method,
         * value-copy independence (mutating a copy must not affect the
         * original). */
        const char *src =
            "struct Point {\n"
            "  public int X;\n"
            "  public int Y;\n"
            "  public Point(int x, int y) { X = x; Y = y; }\n"
            "  public int Sum() { return X + Y; }\n"
            "}\n"
            "class Program {\n"
            "  static void Main() {\n"
            "    Point p = new Point(3, 4);\n"
            "    int s = p.Sum();\n"
            "    if (s == 7) { Console.WriteLine(\"sum-ok\"); }\n"
            "    Point q = p;\n"
            "    q.X = 999;\n"
            "    int px = p.X;\n"
            "    int qx = q.X;\n"
            "    if (px == 3 && qx == 999) { Console.WriteLine(\"copy-independence-ok\"); }\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else if (strcmp(which, "6") == 0) {
        /* Phase 6e: interfaces with real per-class vtables -- two
         * unrelated classes implementing the same interface, dispatched
         * through a single interface-typed local each, proving genuine
         * runtime dispatch (not resolved at compile time to one fixed
         * class's function). Also exercises the single-interface
         * "class X : IFoo" shape, where cs_parser.c's own base-class/
         * interface ambiguity lands the name in base_class_name instead
         * of interface_names[] (see class_implements()'s own comment). */
        const char *src =
            "interface IShape {\n"
            "  int Area();\n"
            "}\n"
            "class Circle : IShape {\n"
            "  public int Radius;\n"
            "  public Circle(int r) { Radius = r; }\n"
            "  public int Area() { return Radius * Radius * 3; }\n"
            "}\n"
            "class Square : IShape {\n"
            "  public int Side;\n"
            "  public Square(int s) { Side = s; }\n"
            "  public int Area() { return Side * Side; }\n"
            "}\n"
            "class Program {\n"
            "  static void Main() {\n"
            "    IShape a = new Circle(2);\n"
            "    IShape b = new Square(4);\n"
            "    int aArea = a.Area();\n"
            "    int bArea = b.Area();\n"
            "    if (aArea == 12 && bArea == 16) { Console.WriteLine(\"interface-dispatch-ok\"); }\n"
            "  }\n"
            "}\n";
        lower_one(src, out_path);
    } else {
        fprintf(stderr, "unknown fixture '%s'\n", which);
        return 1;
    }
    return 0;
}
