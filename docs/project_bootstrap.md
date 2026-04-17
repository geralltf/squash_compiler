---
name: squash compiler bootstrap chain
description: How to build the squash compiler bootstrap chain, key files, and codegen bugs fixed
type: project
---

# squash compiler bootstrap chain

squash is a self-hosting C compiler targeting Windows x64 PE executables.

## Build chain
1. `cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe` — MSVC builds stage-1 binary (requires vcvars64.bat)
2. `./squash_msvc.exe squash.c -o squash2_new.exe` — stage-1 compiles squash itself (stage-2)
3. `./squash2_new.exe test_program2.c -o test_program2_new.exe` — stage-2 compiles test suite
4. `./test_program2_new.exe` — must print "ALL TESTS PASSED"

`rebuild_all.bat` automates steps 1-4 but requires PATH fix (use `.\squash_msvc.exe` not bare name).

## Test suite
`test_program2.c` has 43 tests covering arithmetic, structs, macros, pointers, malloc, etc.
`test_features.c` has additional feature tests.

## Key bugs fixed (completed 2026-04-17)
1. **Stale params[] in pp_macro_undef** (lexer.c): After swap-with-last during undef, vacated slot wasn't zeroed → stale function-like macro params used when slot reused as object-like macro. Fix: zero the vacated slot after copy.

2. **Double-free of params on nparams=-1** (lexer.c): Cleanup loop freed params even for object-like macros (nparams=-1). Fix: check `nparams >= 0` before freeing.

3. **NULL wdata label** (codegen.c): squash-compiled binaries pass NULL to intern_wdata for 4th global variable. Fix: generate fallback name `__wdata_anon_N` when label is NULL.

4. **NULL r->symbol crash in pe_link_and_write** (pe_builder.c): strcmp(NULL, "__entry__") crash. Fix: `if (!r->symbol) continue` guard.

5. **Inline array struct field codegen bug** (codegen.c, the BIG one): `char *params[16]` inside PPMacro struct was treated as a pointer (value-based) instead of inline array (address-based) when used as array base in subscript expressions like `st->macros[m].params[pi]`. 
   - Root cause: in `codegen_lvalue` for `AST_INDEX` when `n->index.array->kind == AST_MEMBER`, the check `pointer_depth > 0 → is_pointer_base = 1` didn't distinguish between pointer fields (e.g. `char *ptr`) and inline array-of-pointer fields (e.g. `char *arr[16]`).
   - Fix in codegen.c: `if (ff->field.type->pointer_depth > 0 && ff->field.array_size <= 0) is_pointer_base = 1;`
   - Workaround in lexer.c: Use temp `char **_pp = st->macros[m].params` before indexing to avoid nested struct array access patterns.

6. **Large stack arrays in pp_expand** (lexer.c): `char args_buf[8192]` and `char body[4096]` on stack crashed in squash-compiled binaries. Changed to heap allocations (`malloc`/`free`) to avoid squash codegen stack frame issues.

## Key structs
- **PPMacro**: `{char *name; char *value; char *params[16]; int nparams;}` — sizeof=152 bytes
- **PPState**: has `PPMacro macros[1024]` at offset 0
- nparams=-1 = object-like macro, nparams>=0 = function-like macro

## Why: bootstrap is critical
The bootstrap test verifies that squash can compile itself. A squash-compiled squash must produce correct binaries for the 43-test suite.
