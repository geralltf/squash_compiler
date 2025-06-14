#ifndef SYMBOLS_H
#define SYMBOLS_H

#include "List.h"
#include "Dictionary.h"
#include "Token.h"
#include "AST.h"
#include <stdio.h>

typedef struct FunctionSymbol
{
    char* Name;
    list_t* Parameters;
} FunctionSymbol_t;

typedef struct VariableSymbol
{
    enum VarType VariableType;
    char* Name;
    void* Value;
    enum VarSymbolType ValueType;

} VariableSymbol_t;

FunctionSymbol_t* FunctionSymbol_init(FunctionSymbol_t** symbol, char* name, list_t* parameters);
VariableSymbol_t* VariableSymbol_initI(VariableSymbol_t** symbol, enum VarType type, char* name, int value);
VariableSymbol_t* VariableSymbol_initD(VariableSymbol_t** symbol, enum VarType type, char* name, double value);
VariableSymbol_t* VariableSymbol_initC(VariableSymbol_t** symbol, enum VarType type, char* name, char* value);
int VariableSymbol_ParseInt(VariableSymbol_t* symbol);
char* VariableSymbol_ParseString(VariableSymbol_t* symbol);
char* VariableSymbol_to_string(VariableSymbol_t* symbol);

typedef struct SymbolTable
{
    Dictionary* keywordDict; // Dictionary<string, TokenType>
    Dictionary* variables; // Dictionary<string, VariableSymbol>
    Dictionary* functions; // Dictionary<string, FunctionSymbol>
} SymbolTable_t;

int SymbolTable_RevervedKeyword_CompararatorFunc(DictionaryPair* left, DictionaryPair* right);
int SymbolTable_Variable_CompararatorFunc(DictionaryPair* left, DictionaryPair* right);
int SymbolTable_Function_CompararatorFunc(DictionaryPair* left, DictionaryPair* right);

SymbolTable_t* SymbolTable_new();
void SymbolTable_init(SymbolTable_t** symbols);

VariableSymbol_t* SymbolTable_DefineVariableI(SymbolTable_t* symbolTable, enum VarType type, char* name, int value);
VariableSymbol_t* SymbolTable_DefineVariableD(SymbolTable_t* symbolTable, enum VarType type, char* name, double value);
VariableSymbol_t* SymbolTable_DefineVariableL(SymbolTable_t* symbolTable, enum VarType type, char* name, long value);
VariableSymbol_t* SymbolTable_DefineVariableC(SymbolTable_t* symbolTable, enum VarType type, char* name, char* value);
FunctionSymbol_t* SymbolTable_DefineFunction(SymbolTable_t* symbolTable, char* name, list_t* parameters); // List<string>

bool SymbolTable_FunctionHasKey(SymbolTable_t* symbolTable, char* name);
bool SymbolTable_VariableHasKey(SymbolTable_t* symbolTable, char* name);

VariableSymbol_t* SymbolTable_LookupVariable(SymbolTable_t* symbolTable, char* name);
FunctionSymbol_t* SymbolTable_LookupFunction(SymbolTable_t* symbolTable, char* name);

int SymbolTable_GetInt(SymbolTable_t* symbolTable, char* name);
long SymbolTable_GetLong(SymbolTable_t* symbolTable, char* name);
char* SymbolTable_GetString(SymbolTable_t* symbolTable, char* name);
float SymbolTable_GetFloat(SymbolTable_t* symbolTable, char* name);
double SymbolTable_GetDouble(SymbolTable_t* symbolTable, char* name);

#endif