CC=gcc
CFLAGS=-I. -Wall -pedantic -std=c23
DEPS = 
HEADERS =
OBJ = EntryPoint.o FileIO.o SquashCompiler.o Instruction.o rb_tree.o List.o Lexer.o Assembler.o AST.o Symbols.o Token.o sb.o Minifier.o Logger.o Optimiser.o EncoderInstructionSet.o Instruction.Create.o InstructionMemorySizes.o InstructionOpCounts.o Mnemonic.o OpCodeInfo.o OpCodeInfoData.o Encoder.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o squash $^ $(CFLAGS)
	
clean:
	rm -rf *.o
	rm -rf squash.exe