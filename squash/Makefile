CC=gcc
CFLAGS=-I.
DEPS = 
HEADERS =
OBJ = EntryPoint.o FileIO.o SquashCompiler.o Dictionary.o List.o Lexer.o Assembler.o AST.o Symbols.o Token.o sb.o Minifier.o Optimiser.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o squash $^ $(CFLAGS)
	
clean:
	rm -rf *.o
	rm -rf squash.exe