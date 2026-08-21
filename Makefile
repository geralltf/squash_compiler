CC=gcc
CFLAGS=-I. -Wall -pedantic
# squash's own preprocessor/parser recurse once per nested #include (and
# per nested expression/statement) -- the default 1MB Windows thread stack
# is exhausted by deeply-nested real-world header trees (confirmed via a
# real Windows SDK <windows.h> import attempt: STATUS_STACK_OVERFLOW inside
# ___chkstk_ms, well before running out of memory). 16MB gives real headroom
# without meaningfully affecting startup cost.
LDFLAGS=
DEPS =
HEADERS =
OBJ = compiler.o assembler.o ast.o codegen.o arm64_asm.o codegen_arm64.o lexer.o parser_new4.o pe_builder.o elf_builder.o macho_builder.o symtable.o linker.o winlinker.o objfile.o implib.o diag.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o squash $^ $(CFLAGS) $(LDFLAGS)

# tools/self_verify.sh's own multi-generation self-hosting check doesn't
# yet support actually RUNNING it against Windows-targeted output -- see
# that script's own platform-mismatch messages for why (every generation
# past the first has to execute the previous one to keep going, and this
# project's own verification tooling was only ever built/tested on
# Linux). Wired up here anyway so the target exists and reports that
# honestly, rather than leaving `make -f Makefile verify` undefined.
verify: all
	SQUASH_VERIFY_PLATFORM=windows bash tools/self_verify.sh

clean:
	rm -rf *.o
	rm -rf squash.exe
	rm -rf squash
