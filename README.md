### Squash Minimalist C Compiler written in C.

I have designed and implemented a C-based compiler toolchain that can parse, analyze, and compile C code into a working Windows executable.

**Core Compiler Components**
Build a lexer to tokenize input (numbers, identifiers, expressions, etc.).
Implement a recursive descent parser that:
Constructs an AST (Abstract Syntax Tree).
Correctly handles operator precedence, associativity, and parentheses.
Supports expressions, variables, functions, if statements, and while loops.
Provide parsing functions such as:
ParseNumber, ParseIdentifier, ParseFunction, ParseVariable.
Maintain a symbol table for variables and functions.

**Code Generation**
Traverse the AST to generate code using structured, readable functions.
Produce assembly via a custom assembler API.
Convert assembly into machine code.

**PE Executable Builder**
Implement a Portable Executable (PE) builder in C that:
Generates valid 32-bit and 64-bit Windows executables.
Uses structured headers (DOS header, PE headers, sections).
Supports linking and importing Windows APIs detected during parsing.
Accepts .text section input from the assembler.
Ensures executables run correctly (no crashes or invalid instructions).
Applies specific header constraints (e.g., OS version = 4, subsystem version = 4).

**Handles security flags:**
32-bit: DllCharacteristics = 0x8100 (NX enabled, ASLR disabled).
64-bit: ASLR enabled.

**Testing**
Create a test program that validates:

**Console output**
Function compilation
Expressions and statements
if and while constructs
Windows API imports
The test program must be compiled into a working PE executable.

**Extended Language Features (Enhancements)**
Enhance the compiler to support a much broader subset of C, including:
Control flow: for, do, break, continue, goto, switch, else if
Types and qualifiers: int, char, double, long, short, signed, unsigned, const, volatile, etc.

**Advanced features:**
Pointers, arrays, structs, unions, typedefs
Function pointers (still a work in progress)
Type casting and sizeof
Memory management (malloc, calloc, realloc, free)
Arithmetic operators (including %)
Escape sequences
Preprocessor support:
#define, #include, #ifndef, #endif
Header guards and forward declarations
Error handling:
Syntax errors and compile-time diagnostics

**Additional Testing**
A second test program (test_program2.c) validates all new features, including:
Preprocessor directives
Header inclusion and guards
Extended language constructs

**In short:**
Build a fully functional, self-contained C compiler (subset → extended C) with a focus on minimalism in C, 
complete with lexer, parser, AST, code generation, assembler, and PE executable builder, 
plus comprehensive feature support and test programs.
