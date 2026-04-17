---
name: debugging approach feedback
description: What worked and what to avoid when debugging squash bootstrap issues
type: feedback
---

# Binary debug tracing works well for bootstrap bugs

Use temporary `printf`/`fflush(0)` prints in hot paths (preprocess loop, pp_expand) with per-line counters to binary-search the crashing location. Pattern: print every N lines, then narrow N until the exact crashing line is found.

**Why:** The squash-compiled binary is a Windows EXE that can't be run under gdb from bash. Printf tracing is the only practical debugging tool.

**How to apply:** Rebuild squash_msvc.exe after each round of debug prints to get a new squash2_new.exe with the tracing. Then capture stdout to a file (`./squash2_new.exe ... > /tmp/out.txt 2>&1`).

# Temp pointer workaround for nested struct array access

When squash-compiled code accesses `struct.array_field[i]` (array of pointers inside a struct), extract to a temp pointer first: `char **_pp = st->macros[m].params; then _pp[pi]`.

**Why:** squash's codegen had a bug treating inline array-of-pointer fields as pointer-valued (loads the pointer value) rather than address-based (uses field address). The codegen.c fix addresses this, but the workaround is belt-and-suspenders.

**How to apply:** Any time code accesses `struct->member[i]` where member is `T *member[N]` (inline array of pointers), use the temp pointer pattern.

# cmd.exe /c from bash doesn't capture stdout well

Windows EXE stdout captured via `cmd.exe /c "program.exe" > file.txt` from bash is unreliable. Run EXEs directly: `./program.exe > file.txt 2>&1`.

**Why:** The cmd.exe shell interacts oddly with bash's stdout redirection.

# MSVC rebuild requires vcvars64.bat

`cl.exe` is not on PATH by default. To rebuild squash_msvc.exe with MSVC, must first call `vcvars64.bat` in the same cmd.exe session. The `rebuild_all.bat` does this correctly. Running squash_msvc.exe directly from bash works once it's built.
