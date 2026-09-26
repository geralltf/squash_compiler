# Squash — a minimalist, from-scratch compiler written in C

Squash compiles **C**, **C#** and **C++** straight to native machine code. It
does not depend on an external assembler, linker or SDK. It ships its own
lexer, preprocessor, parser, AST, code generators (x86-32, x86-64 and AArch64),
assembler, object-file format and linker. It writes finished executables
directly for:

| Target            | Output format                 | Architectures             |
|-------------------|-------------------------------|---------------------------|
| Windows           | PE / PE32+ (`.exe`)           | x86 (32-bit), x86-64      |
| Linux             | ELF                           | x86-64, AArch64 (ARM64)   |
| macOS             | Mach-O                        | x86-64 (Intel)            |
| OpenBSD           | ELF                           | x86-64                    |
| Android           | Signed, installable `.apk`    | AArch64                   |

Every target works from every host. For example, you can build a Windows
`.exe`, a macOS binary and an Android `.apk` on the same Linux machine.

---

## Building squash

**Linux / macOS / OpenBSD** (bootstraps with the system `gcc`/`clang`):

```sh
make -f Makefile.linux        # Linux
make -f Makefile.macos        # macOS
make -f BSDMakefile           # OpenBSD
./install.sh                  # optional: build + install to ~/.local/bin
```

**Windows:** use `build_msvc.bat` or `squash.sln` (MSVC), `make` (MinGW), or
`install.ps1`.

**Self-hosting:** squash can compile itself. `Makefile.squash` rebuilds the
compiler with a previously built `squash`. `tools/self_verify.sh` runs several
self-compile generations, checks that the output converges, and writes a
signed provenance manifest. It is a "Trusting Trust" countermeasure; see
`tools/setup_signing_keys.sh` for setting up the signing keys.

---

## Usage

```
squash [options] <source.c|source.cs|source.cpp> [object.sqo ...] [-o output]
```

Run `squash --help` for the full colorized reference. Set `NO_COLOR=1` to
disable color.

### Options

**Target platform** (defaults to the platform the squash binary itself was
built for)

| Option                     | Meaning |
|----------------------------|---------|
| `-linux`                   | Native ELF output for Linux |
| `-windows`                 | Native PE output for Windows (squash's original target) |
| `-macos`                   | Native Mach-O output for Intel (x86-64) macOS |
| `-openbsd`                 | Native ELF output for OpenBSD |
| `-openbsd-libc <so>`       | Override the OpenBSD `libc.so` soname to link against |
| `-android`                 | Signed, installable `.apk` (implies `-linux -arm64 -64`) |
| `-android-package <name>`  | Android package name (default: derived from `-o`'s basename) |
| `-android-activity`        | Use the DEX-based `SquashActivity` shim instead of raw `NativeActivity` (see [Android](#android)) |

**Word size / architecture**

| Option   | Meaning |
|----------|---------|
| `-32`    | 32-bit output (Windows only; every other target is 64-bit only) |
| `-64`    | 64-bit output (default) |
| `-arm64` | AArch64 instead of x86-64 (implies `-64`; Linux only, and `-android` is always AArch64) |

**Compile mode, search paths and linking**

| Option        | Meaning |
|---------------|---------|
| `-c`          | Emit a linkable `.sqo` object instead of an executable |
| `-o <path>`   | Output path (default: the source name with its extension replaced) |
| `-I <dir>`    | Add an `#include` search directory (repeatable) |
| `-L <dir>`    | Add a native library search directory (repeatable) |
| `-l <name>`   | Link a native library, e.g. `-lvulkan` → `libvulkan.so` (repeatable) |

You can also pass a `.sqo`, `.so` or `.a` file directly as an argument.

**Diagnostics**

| Option          | Meaning |
|-----------------|---------|
| `-dump`         | Print the fully preprocessed source before compiling it |
| `-nodebug`      | Strip ELF `.symtab` / `.strtab` / `.shstrtab` from the output (Linux only) |
| `-h`, `--help`  | Show help and exit |

---

## Examples

### Everyday use

```sh
squash hello.c -o hello                      # native executable for this host
squash game.cs -o game                       # C# straight to a native executable
squash app.cpp -o app                        # C++ straight to a native executable
squash -windows -64 app.c -lvulkan -o app.exe  # Windows binary linked against Vulkan
squash -dump app.c -o app                    # show preprocessed source, then compile
squash -linux -nodebug app.c -o app          # smaller ELF without symbol tables
```

### Separate compilation with `.sqo` objects

```sh
# Precompile a large shared translation unit once...
squash -c -linux -64 big_shared.c -o common.sqo
# ...then recompile only main.c and relink it against that object.
squash main.c common.sqo -o app
```

### Cross-compiling for another platform

```sh
squash -linux   -64    app.c -o app          # Linux x86-64 ELF
squash -linux   -arm64 app.c -o app          # Linux AArch64 ELF
squash -windows -32    app.c -o app.exe      # 32-bit Windows PE
squash -windows -64    app.c -o app.exe      # 64-bit Windows PE
squash -macos          app.c -o app          # Intel macOS Mach-O
squash -openbsd        app.c -o app          # OpenBSD ELF
```

When squash runs on OpenBSD, it detects the target's `libc.so` soname
automatically. When you cross-compile for OpenBSD from another OS, name the
exact libc version installed on the target machine:

```sh
squash -openbsd -openbsd-libc libc.so.99.1 app.c -o app
```

### Android

```sh
squash -android app.c -o app.apk
squash -android -android-package com.example.demo app.c -o demo.apk
squash -android -android-activity android/examples/triangle_android.c -o triangle.apk
```

Building an `.apk` needs no Android SDK, NDK, AAPT, apksigner, Gradle or Java.
squash generates all of it:

- the AArch64 bionic `.so`;
- the binary `AndroidManifest.xml` (AXML);
- the ZIP container;
- the APK signatures, both **v1 (JAR)** and **v2**, with self-implemented
  SHA-1/SHA-256, RSA, DER/X.509 and bignum code. A debug signing key is
  generated once and cached under `~/.squash/`.

There are two ways to start the app:

- **Default:** `main()` is exported as `ANativeActivity_onCreate` and runs
  once through Android's built-in `NativeActivity`. No Java or DEX is
  involved.
- **`-android-activity`:** squash also emits a tiny `classes.dex` containing
  `com.squash.runtime.SquashActivity`. It creates a `SurfaceView` and forwards
  the surface lifecycle to JNI entry points in your code:
  `Java_com_squash_runtime_SquashActivity_nativeSurfaceCreated`,
  `…nativeSurfaceChanged` and `…nativeSurfaceDestroyed`. Use this mode on
  newer hardware. On a real Android 17 device, raw `NativeActivity` never
  delivered its window callbacks. `android/examples/triangle_android.c` is a
  complete Vulkan triangle that uses this mode and has been tested on real
  hardware.

---

## Language support

### C

Squash supports a broad, practical subset of C (including many C11 features):

- **Control flow:** `if`/`else if`/`else`, `while`, `do`, `for`, `switch`,
  `break`, `continue`, `goto`
- **Types and qualifiers:** `char`, `short`, `int`, `long`, `float`, `double`,
  `signed`, `unsigned`, `const`, `static`, `volatile`, `extern`
- **Aggregates:** pointers, arrays, `struct`, `union`, `enum`, `typedef`,
  function pointers
- **Expressions:** full operator precedence and associativity, casts,
  `sizeof`, `%`, escape sequences, floating-point arithmetic
- **Preprocessor:** `#define` (including function-like macros), `#include`,
  `#if`/`#ifdef`/`#ifndef`/`#else`/`#endif`, header guards
- **Diagnostics:** syntax errors, warnings, "did you mean …?" fuzzy matches
  for typos, missing-include hints and linker diagnostics (see
  `tests/test_diagnostics_*.c`)
- **Platform APIs:** Win32 imports detected while parsing, POSIX and
  pthreads, OpenGL, Vulkan, SDL3, OpenSSL

### C# (`.cs`)

C# source is lowered to C and then compiled by the same, unmodified C
pipeline. The lowered code is backed by the small runtime in `CSR/`.
Supported features include:

- classes and structs
- interfaces with real per-class vtables
- `List<T>` (generics are erased)
- LINQ method chains
- string interpolation
- `[DllImport]`-declared native calls, for example calling Vulkan directly
  from C#

See `CS/cs_lower.h` for the exact scope.

### C++ (`.cpp`, `.cxx`, `.cc`)

C++ source is also lowered to C, backed by the runtime in `CPPR/`. Supported
features include:

- classes and structs with single inheritance and virtual dispatch
- constructors and destructors (RAII)
- function, method and operator overloading
- references, `new`/`delete`, namespaces
- function and class templates
- a small `std::string`, `std::vector<T>`, `std::cout` and `std::cin`
  (these are not a full STL)

See `CPP/cpp_lower.h` for the exact scope and `CPP/tests/` for examples with
expected output.

```cpp
#include <iostream>
class Point {
public:
    int x, y;
    Point(int a, int b) { x = a; y = b; }
    int sum() { return x + y; }
};
int main() {
    Point p(3, 4);
    std::cout << "sum=" << p.sum() << std::endl;
    return 0;
}
```

```sh
squash point.cpp -o point && ./point
```

---

## How it works

```
 .c   ──────────────────────────────────┐
 .cs  ─► C# lexer/parser  ─► lower to C ┤
 .cpp ─► C++ lexer/parser ─► lower to C ┘
                  │
      preprocessor ─► lexer ─► recursive-descent parser ─► AST + symbol table
                  │
      codegen (x86 / x86-64 / AArch64) ─► built-in assembler ─► machine code
                  │
      linker (+ .sqo objects, import libraries, .so/.a)
                  │
      PE builder │ ELF builder │ Mach-O builder │ APK packager + signer
```

| Component | Files |
|-----------|-------|
| Driver and CLI | `compiler.c` |
| Lexer, parser, AST, symbols | `lexer.c`, `parser_new4.c`, `ast.c`, `symtable.c` |
| Code generation | `codegen.c` (x86/x86-64), `codegen_arm64.c` |
| Assemblers | `assembler.c`, `arm64_asm.c` |
| Objects and linking | `objfile.c` (`.sqo`), `linker.c`, `winlinker.c`, `implib.c` |
| Executable writers | `pe_builder.c`, `elf_builder.c`, `macho_builder.c` |
| Android packaging and signing | `android/` |
| C# and C++ frontends | `CS/`, `CPP/` (runtimes in `CSR/`, `CPPR/`) |
| Diagnostics | `diag.c` |
| Bundled headers | `include/` |

### PE details

- Valid 32-bit and 64-bit Windows executables with DOS header, PE headers and
  sections. Windows API imports are found during parsing and linked
  automatically.
- 32-bit: `DllCharacteristics = 0x8100` (NX on, ASLR off). 64-bit: ASLR on.

---

## Companion projects built with squash

- **SQW** (`SQW/`): a minimal HTML5 web browser rendered with Vulkan through
  SDL3. It has a DOM, CSS (selectors, grid, box shadows), layout, text
  rendering, PNG/GIF/JPEG/SVG images, HTML forms, a small JavaScript engine,
  and in-page scripting in C and C#, including Vulkan 3D scenes. Build it
  with `Makefile.SQW` or `Makefile.SQW.linux`.
- **SQS** (`SQS/`): a small HTTP/HTTPS test server built on OpenSSL, with a
  mini PHP interpreter, a database engine and DNS. It serves
  `SQW/testpages/` and is being developed toward running WordPress. Build it
  with `Makefile.SQS` or `Makefile.SQS.linux`.
- **SDL3** builds: `Makefile.SDL3*` compile SDL3 itself with squash.

---

## Tests

- `tests/`: C feature programs (`test_program0-2.c`, `test_c11_features.c`),
  diagnostics, linker errors, pthreads, OpenGL
- `CPP/tests/`: C++ programs with `.expected` output
- `CS/tests/`, `CSR/tests/`: C# lexer, parser and lowering smoke tests, plus
  runtime tests
- `SQW/tests/`, `SQS/tests/`: DOM, JS, layout, image decoders, fuzzing and the
  database engine
- `tools/verify_sdl3_builds.sh`, `tools/self_verify.sh`: whole-project and
  self-hosting checks

---

## Future directions

- An open standard library, either as shims or as a drop-in replacement,
  built from source with no prebuilt object binaries
- Optimisations such as dead-code elimination and constant folding
- Wider C# and C++ coverage, and macOS on Apple Silicon (AArch64 Mach-O)

---

**In short:** Squash is a self-contained, self-hosting compiler toolchain
with a focus on minimalism. It turns C, C# and C++ into native executables
for Windows, Linux, macOS, OpenBSD and Android, and needs no outside
toolchain.

— Gerallt
