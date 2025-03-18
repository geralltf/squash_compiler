CC=gcc
CFLAGS=-I.
DEPS = 
OBJ = EntryPoint.o Assembly.o AST.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)