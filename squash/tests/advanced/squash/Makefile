CC=gcc
CFLAGS=-I. -Wall -pedantic -std=c23
DEPS = 
HEADERS =
OBJ = compiler.o assembler.o ast.o codegen.o lexer.o parser.o pe_builder.o symtable.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o squash $^ $(CFLAGS)
	
clean:
	rm -rf *.o
	rm -rf squash.exe
	rm -rf squash