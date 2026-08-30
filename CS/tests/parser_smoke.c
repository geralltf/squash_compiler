#include "../cs_parser.h"
#include "../cs_ast.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("[OK]   %s\n", msg); else { printf("[FAIL] %s\n", msg); g_fail = 1; } } while (0)

static CsNode *parse_ok(const char *src, const char *label) {
    CsParser p;
    CsNode *unit;
    cs_parser_init(&p, src);
    unit = cs_parse_unit(&p);
    CHECK(p.error_count == 0, label);
    if (p.error_count != 0) { printf("  (source: %s)\n", src); }
    cs_parser_free(&p);
    return unit;
}

int main(void) {
    /* 1. classes, generics, fields, auto-properties, methods, ctor */
    {
        const char *src =
            "using System;\n"
            "using System.Collections.Generic;\n"
            "namespace Demo {\n"
            "  public class Box<T> where T : class {\n"
            "    private T value;\n"
            "    public int Count { get; set; }\n"
            "    public Box(T v) { value = v; Count = 0; }\n"
            "    public T Get() { return value; }\n"
            "    public static Box<T> Wrap(T v) { return new Box<T>(v); }\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: generic class with ctor/property/static method");
        CHECK(unit && unit->kind == CS_UNIT && unit->unit.n_usings == 2, "2 using directives collected");
        if (unit) {
            CsNode *ns = unit->unit.decls[0];
            CHECK(ns->kind == CS_NAMESPACE && strcmp(ns->namespace_decl.name, "Demo") == 0, "namespace 'Demo' parsed");
            {
                CsNode *cls = ns->namespace_decl.decls[0];
                CHECK(cls->kind == CS_CLASS_DECL && strcmp(cls->class_decl.name, "Box") == 0, "class 'Box' parsed");
                CHECK(cls->class_decl.n_type_params == 1 &&
                      strcmp(cls->class_decl.type_params[0]->type_param.name, "T") == 0, "type param T parsed");
                CHECK(cls->class_decl.type_params[0]->type_param.n_constraints == 1 &&
                      cls->class_decl.type_params[0]->type_param.constraints[0].kind == CS_CONSTRAINT_CLASS,
                      "where T : class constraint parsed");
                CHECK(cls->class_decl.n_members == 5, "5 members parsed (field, property, ctor, 2 methods)");
            }
            csast_free(unit);
        }
    }

    /* 2. LINQ method-syntax chain + lambda */
    {
        const char *src =
            "class Program {\n"
            "  static void Main() {\n"
            "    var nums = new List<int>();\n"
            "    var evens = nums.Where(x => x % 2 == 0).Select(x => x * 2).ToList();\n"
            "    Console.WriteLine(evens.Count);\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: LINQ method-syntax chain with lambdas");
        csast_free(unit);
    }

    /* 3. try/catch/finally, exceptions, generic exception base */
    {
        const char *src =
            "class Program {\n"
            "  static void Run() {\n"
            "    try {\n"
            "      throw new ArgumentException(\"bad\");\n"
            "    } catch (ArgumentException e) {\n"
            "      Console.WriteLine(e.Message);\n"
            "    } catch (Exception e) {\n"
            "      throw;\n"
            "    } finally {\n"
            "      Console.WriteLine(\"cleanup\");\n"
            "    }\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: try/catch(multiple)/finally/throw/rethrow");
        csast_free(unit);
    }

    /* 4. control flow: for/foreach/while/do-while/switch */
    {
        const char *src =
            "class Program {\n"
            "  static int Compute(int[] xs) {\n"
            "    int total = 0;\n"
            "    for (int i = 0; i < xs.Length; i++) total += xs[i];\n"
            "    foreach (int x in xs) { if (x < 0) continue; total += x; }\n"
            "    int j = 0;\n"
            "    while (j < 10) { j++; }\n"
            "    do { j--; } while (j > 0);\n"
            "    switch (total) {\n"
            "      case 0:\n"
            "      case 1:\n"
            "        return -1;\n"
            "      default:\n"
            "        return total;\n"
            "    }\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: for/foreach/while/do-while/switch control flow");
        csast_free(unit);
    }

    /* 5. string interpolation, generics with nested angle brackets */
    {
        const char *src =
            "class Program {\n"
            "  static void Main() {\n"
            "    Dictionary<string, List<int>> map = new Dictionary<string, List<int>>();\n"
            "    string name = \"world\";\n"
            "    int count = 3;\n"
            "    string msg = $\"Hello {name}, count={count + 1}!\";\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: nested generic angle brackets (>>) + string interpolation");
        if (unit) {
            CsNode *cls = unit->unit.decls[0];
            CsNode *method = cls->class_decl.members[0];
            CsNode *body = method->method_decl.body;
            CHECK(body->block.n_stmts == 4, "4 statements in Main body");
            {
                CsNode *msgdecl = body->block.stmts[3];
                CsNode *init = msgdecl->local_var_decl.init;
                CHECK(init->kind == CS_LIT_INTERP_STRING && init->lit_interp.n_parts == 5,
                      "interpolated string split into 5 parts (text,expr,text,expr,text)");
            }
        }
        csast_free(unit);
    }

    /* 6. cast vs parenthesized expr disambiguation */
    {
        const char *src =
            "class Program {\n"
            "  static void Main() {\n"
            "    object o = 5;\n"
            "    int x = (int)o;\n"
            "    int y = (x + 1);\n"
            "    int z = (x) * 2;\n"
            "  }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: cast vs parenthesized-expression disambiguation");
        if (unit) {
            CsNode *cls = unit->unit.decls[0];
            CsNode *method = cls->class_decl.members[0];
            CsNode *body = method->method_decl.body;
            CsNode *xdecl = body->block.stmts[1];
            CHECK(xdecl->local_var_decl.init->kind == CS_CAST, "'(int)o' parsed as a cast");
            {
                CsNode *ydecl = body->block.stmts[2];
                CHECK(ydecl->local_var_decl.init->kind != CS_CAST, "'(x + 1)' parsed as grouping, not a cast");
            }
        }
        csast_free(unit);
    }

    /* 7. interface + inheritance */
    {
        const char *src =
            "interface IShape { double Area(); }\n"
            "class Circle : IShape {\n"
            "  public double Radius;\n"
            "  public double Area() { return 3.14159 * Radius * Radius; }\n"
            "}\n";
        CsNode *unit = parse_ok(src, "parse: interface + class implementing it");
        if (unit) {
            CsNode *circle = unit->unit.decls[1];
            /* No semantic info at parse time to know "IShape" is an interface,
             * not a base class -- the parser's documented heuristic treats the
             * first base-list name as a base-class candidate; cs_lower.c
             * (Phase 3) resolves the real distinction once every type in the
             * program has been collected. */
            CHECK(circle->class_decl.base_class_name && strcmp(circle->class_decl.base_class_name, "IShape") == 0,
                  "Circle : IShape recorded (base-vs-interface distinction deferred to Phase 3, per heuristic)");
        }
        csast_free(unit);
    }

    /* 8. a deliberately broken program: parser should report an error, not crash */
    {
        CsParser p;
        CsNode *unit;
        cs_parser_init(&p, "class Program { static void Main() { int x = ; } }");
        unit = cs_parse_unit(&p);
        CHECK(p.error_count > 0, "malformed source correctly reports a parse error");
        CHECK(unit != 0, "parser still returns a usable (partial) AST after an error, doesn't crash");
        csast_free(unit);
        cs_parser_free(&p);
    }

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
