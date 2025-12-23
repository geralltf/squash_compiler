#include <string.h>
#include <limits.h>

#include "Symbols.h"

FunctionSymbol_t* FunctionSymbol_init(FunctionSymbol_t** symbol, char* name, list_t* parameters)
{
    FunctionSymbol_t* f = (FunctionSymbol_t*)malloc(sizeof(FunctionSymbol_t));
    f->Name = name;
    f->Parameters = parameters;

    *symbol = f;

    return f;
}

VariableSymbol_t* VariableSymbol_initI(VariableSymbol_t** symbol, enum VarType type, char* name, int value)
{
    int* val = &value;
    void* valObj = (void*)val;

    VariableSymbol_t* v = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    v->VariableType = type;
    v->Name = name;
    v->Value = valObj;
    v->ValueType = VAR_Int;
    *symbol = v;

    return v;
}

VariableSymbol_t* VariableSymbol_initL(VariableSymbol_t** symbol, enum VarType type, char* name, long value)
{
    long* val = &value;
    void* valObj = (void*)val;

    VariableSymbol_t* v = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    v->VariableType = type;
    v->Name = name;
    v->Value = valObj;
    v->ValueType = VAR_Long;
    *symbol = v;

    return v;
}

VariableSymbol_t* VariableSymbol_initD(VariableSymbol_t** symbol, enum VarType type, char* name, double value)
{
    double* val = &value;
    void* valObj = (void*)val;

    VariableSymbol_t* v = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    v->VariableType = type;
    v->Name = name;
    v->Value = valObj;
    v->ValueType = VAR_Double;

    *symbol = v;

    return v;
}

VariableSymbol_t* VariableSymbol_initC(VariableSymbol_t** symbol, enum VarType type, char* name, char* value)
{
    void* valObj = (void*)value;

    VariableSymbol_t* v = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    v->VariableType = type;
    v->Name = name;
    v->Value = valObj;
    v->ValueType = VAR_Char;

    *symbol = v;

    return v;
}

int VariableSymbol_ParseInt(VariableSymbol_t* symbol)
{
    if (symbol->Value != NULL && (symbol->VariableType == AST_Int || symbol->VariableType == AST_Int32))
    {
        if (symbol->VariableType == VAR_Int)
        {
            int val_a = *(int*)symbol->Value;

            return val_a;
        }
        else if (symbol->VariableType == VAR_Double)
        {
            double val_d = *(double*)symbol->Value;
            int val_a = (int)val_d;

            return val_a;
        }
        else if (symbol->VariableType == VAR_Float)
        {
            float val_f = *(float*)symbol->Value;
            int val_a = (int)val_f;

            return val_a;
        }
        else if (symbol->VariableType == VAR_Char)
        {
            return 0;
        }
        else if (symbol->VariableType == VAR_Undefined)
        {
            return 0;
        }
    }
    return 0;
}

char* VariableSymbol_ParseString(VariableSymbol_t* symbol)
{
    if (symbol->Value != NULL && (symbol->VariableType == AST_String))
    {
        return VariableSymbol_to_string(symbol);
    }
    return '\0';
}
char* VariableSymbol_to_string(VariableSymbol_t* symbol)
{
    char* result = (char*)malloc(sizeof(char) * 256);

    if (symbol->ValueType == VAR_Int)
    {
        snprintf(result, 256, "%d", (char*)symbol->Value);
    }
    else if (symbol->ValueType == VAR_Double)
    {
        sprintf(result, "%f", (char*)symbol->Value);
    }
    else if (symbol->ValueType == VAR_Float)
    { 
        sprintf(result, "%.6f", (char*)symbol->Value);

        //float* fptr = (float*)symbol->Value;
        //float f = *fptr;

        //_gcvt(f, 6, result);
    }
    else if (symbol->ValueType == VAR_Undefined)
    {
        result[0] = '\0';
        return result;
    }
    else
    {
        result[0] = '\0';
        return result;
    }
    return result;
}

//int SymbolTable_RevervedKeyword_ComparatorFunc(DictionaryPair* left, DictionaryPair* right)
//{
//    // DictionaryPair* left, DictionaryPair* right
//    int compareResult = 0;
//
//    return compareResult;
//}
//
//int SymbolTable_Variable_ComparatorFunc(DictionaryPair* left, DictionaryPair* right)
//{
//    int compareResult = 0;
//
//    //VariableSymbol_t* left_var = (VariableSymbol_t*)left->value;
//    //VariableSymbol_t* right_var = (VariableSymbol_t*)right->value;
//
//    //if (left_var->Name == NULL && right_var->Name == NULL)
//    //{
//    //    return 0;
//    //}
//    //if (left_var->Name == NULL && right_var->Name != NULL)
//    //{
//    //    return -1;
//    //}
//    //if (left_var->Name != NULL && right_var->Name == NULL)
//    //{
//    //    return 1;
//    //}
//
//    //compareResult = strcmp(left_var->Name, right_var->Name);
//
//    compareResult = strcmp((char*)left->key, (char*)right->key);
//
//    return compareResult;
//}
//
//int SymbolTable_Function_ComparatorFunc(DictionaryPair* left, DictionaryPair* right)
//{
//    int compareResult = 0;
//    
//    //FunctionSymbol_t* left_var = (FunctionSymbol_t*)left->value;
//    //FunctionSymbol_t* right_var = (FunctionSymbol_t*)right->value;
//
//    //if (left_var->Name == NULL || right_var->Name == NULL)
//    //{
//    //    return -1;
//    //}
//
//    //compareResult = strcmp(left_var->Name, right_var->Name);
//
//    compareResult = strcmp((char*)left->key, (char*)right->key);
//
//    return compareResult;
//}

static int rb_test_node_cmp(const struct rb_node* a, const struct rb_node* b)
{
    return strcmp((char*)b->key, (char*)a->key);
    //return tb->key - ta->key;
}

static int rb_node_search_cmp(const struct rb_node* a, const void* key)
{
    if (a->key == NULL)
    {
        return -1;
    }
    return strcmp((char*)key, (char*)a->key);
    //return tb->key - ta->key;
}

SymbolTable_t* SymbolTable_new()
{
    SymbolTable_t* symbols = (SymbolTable_t*)malloc(sizeof(SymbolTable_t));
    return symbols;
}

void SymbolTable_init(SymbolTable_t** symbols)
{
    SymbolTable_t* symbolTable = *symbols; // SymbolTable_new();

    //symbolTable->keywordDict = DictionaryNew((DictionaryPairCompare)&SymbolTable_RevervedKeyword_ComparatorFunc);

    //DictionaryPair* pairOut_sin = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    //DictionaryPair* pairOut_cos = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    //DictionaryPair* pairOut_tan = (DictionaryPair*)malloc(sizeof(DictionaryPair));

    //DictionaryPair* pair_sin = DictionaryPairNew();
    //char* key_sin = (char*)malloc(sizeof(char) * 4);
    //key_sin[0] = 's';
    //key_sin[1] = 'i';
    //key_sin[2] = 'n';
    //key_sin[3] = 0x00;
    //pair_sin->key = (void*)key_sin;
    ////DictionaryInsertPair(symbolTable->keywordDict, pair_sin, &pairOut_sin);

    //DictionaryPair* pair_cos = DictionaryPairNew();
    //char* key_cos = (char*)malloc(sizeof(char) * 4);
    //key_cos[0] = 'c';
    //key_cos[1] = 'o';
    //key_cos[2] = 's';
    //key_cos[3] = 0x00;
    //pair_cos->key = (void*)key_cos;
    ////DictionaryInsertPair(symbolTable->keywordDict, pair_cos, &pairOut_cos);

    //DictionaryPair* pair_tan = DictionaryPairNew();
    //char* key_tan = (char*)malloc(sizeof(char) * 4);
    //key_tan[0] = 'c';
    //key_tan[1] = 'o';
    //key_tan[2] = 's';
    //key_tan[3] = 0x00;
    //pair_tan->key = (void*)key_tan;
    //DictionaryInsertPair(symbolTable->keywordDict, pair_tan, &pairOut_tan);

    //{ "sin", TokenType.Function },
    //{ "cos", TokenType.Function },
    //{ "tan", TokenType.Function }

    //symbolTable->variables = DictionaryNew((DictionaryPairCompare)&SymbolTable_Variable_ComparatorFunc);
    //symbolTable->functions = DictionaryNew((DictionaryPairCompare)&SymbolTable_Function_ComparatorFunc);

    //variables = new Dictionary<string, VariableSymbol>();
    //functions = new Dictionary<string, FunctionSymbol>();

    symbolTable->variables = (struct rb_tree*)malloc(sizeof(struct rb_tree));
    symbolTable->functions = (struct rb_tree*)malloc(sizeof(struct rb_tree));

    symbolTable->variables->root = NULL;
    symbolTable->functions->root = NULL;

    rb_tree_init(symbolTable->variables);
    rb_tree_init(symbolTable->functions);
}

VariableSymbol_t* SymbolTable_DefineVariableI(SymbolTable_t* symbolTable, enum VarType type, char* name, int value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_initI(&varSymbol, type, name, value);

    struct rb_node* variable_node = (struct rb_node*)malloc(sizeof(struct rb_node));
    variable_node->key = name;
    variable_node->right = NULL;
    variable_node->left = NULL;
    variable_node->parent = 0;
    variable_node->variable_symbol = variable;

    rb_tree_insert(symbolTable->variables, variable_node, rb_test_node_cmp);

    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);
    variable = (VariableSymbol_t*)found->variable_symbol;

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableD(SymbolTable_t* symbolTable, enum VarType type, char* name, double value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_initD(&varSymbol, type, name, value);

    struct rb_node* variable_node = (struct rb_node*)malloc(sizeof(struct rb_node));
    variable_node->key = name;
    variable_node->right = NULL;
    variable_node->left = NULL;
    variable_node->parent = 0;
    variable_node->variable_symbol = variable;

    rb_tree_insert(symbolTable->variables, variable_node, rb_test_node_cmp);

    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);
    variable = (VariableSymbol_t*)found->variable_symbol;

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableL(SymbolTable_t* symbolTable, enum VarType type, char* name, long value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_initL(&varSymbol, type, name, value);

    struct rb_node* variable_node = (struct rb_node*)malloc(sizeof(struct rb_node));
    variable_node->key = name;
    variable_node->right = NULL;
    variable_node->left = NULL;
    variable_node->parent = 0;
    variable_node->variable_symbol = variable;

    rb_tree_insert(symbolTable->variables, variable_node, rb_test_node_cmp);

    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);
    variable = (VariableSymbol_t*)found->variable_symbol;

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableC(SymbolTable_t* symbolTable, enum VarType type, char* name, char* value)
{
    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_initC(&varSymbol, type, name, value);

    struct rb_node* variable_node = (struct rb_node*)malloc(sizeof(struct rb_node));
    variable_node->key = name;
    variable_node->right = NULL;
    variable_node->left = NULL;
    variable_node->parent = 0;
    variable_node->variable_symbol = (void*)variable;

    rb_tree_insert(symbolTable->variables, variable_node, rb_test_node_cmp);

    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);
    variable = (VariableSymbol_t*)found->variable_symbol;

    return variable;
}
FunctionSymbol_t* SymbolTable_DefineFunction(SymbolTable_t* symbolTable, char* name, list_t* parameters) // List<string> parameters
{
    //Logger.Log.LogInformation("SymbolTable.DefineFunction(): " + name);

    FunctionSymbol_t* functSymbol = (FunctionSymbol_t*)malloc(sizeof(FunctionSymbol_t));
    FunctionSymbol_t* function = FunctionSymbol_init(&functSymbol, name, parameters);

    struct rb_node* function_node = (struct rb_node*)malloc(sizeof(struct rb_node));
    function_node->key = name;
    function_node->right = NULL;
    function_node->left = NULL;
    function_node->parent = 0;
    function_node->variable_symbol = (void*)function;

    rb_tree_insert(symbolTable->functions, function_node, rb_test_node_cmp);

    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->functions, (void*)name, rb_node_search_cmp);
    FunctionSymbol_t* function_result = (FunctionSymbol_t*)found->variable_symbol;

    return function_result;
}

bool SymbolTable_FunctionHasKey(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->functions, (void*)name, rb_node_search_cmp);

    if (found == NULL)
    {
        return false;
    }

    FunctionSymbol_t* function = (FunctionSymbol_t*)found->variable_symbol;

    return function != NULL;
}

bool SymbolTable_VariableHasKey(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    if (found == NULL)
    {
        return false;
    }

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    return variable != NULL;
}

VariableSymbol_t* SymbolTable_LookupVariable(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    return variable;
}

FunctionSymbol_t* SymbolTable_LookupFunction(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->functions, (void*)name, rb_node_search_cmp);

    FunctionSymbol_t* function = (FunctionSymbol_t*)found->variable_symbol;

    if (found != NULL)
    {
        FunctionSymbol_t* function_symbol = (FunctionSymbol_t*)found->variable_symbol;
        return function_symbol;
    }

    return NULL;
}

int SymbolTable_GetInt(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = variable->Value;
        if (variable->ValueType != VAR_Undefined && variable->ValueType == VAR_Int)
        {
            int* iPtr = (int*)objValue;
            int result = *iPtr;
            return result;
        }
        else 
        {
            //Logger.Log.LogError("int can not be parsed from '" + objValue + "' value.");
        }
    }

    return 0;
}

long SymbolTable_GetLong(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = variable->Value;
        if (variable->ValueType != VAR_Undefined && variable->ValueType == VAR_Long)
        {
            long* iPtr = (long*)objValue;
            long result = *iPtr;
            return result;
        }
        else
        {
            //Logger.Log.LogError("long can not be parsed from '" + objValue + "' value.");
        }
    }
   
    return 0;
}

char* SymbolTable_GetString(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = variable->Value;
        if (variable->ValueType != VAR_Undefined && variable->ValueType == VAR_Char)
        {
            char* cPtr = (char*)objValue;
                    
            return cPtr;
        }
        else
        {
            //Logger.Log.LogError("string can not be parsed from '" + objValue + "' value.");
        }
    }
}

float SymbolTable_GetFloat(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = variable->Value;
        if (variable->ValueType != VAR_Undefined && variable->ValueType == VAR_Char)
        {
            float* fPtr = (float*)objValue;
            float fVal = *fPtr;
            return fVal;
        }
        else
        {
            //Logger.Log.LogError("float can not be parsed from '" + objValue + "' value.");
        }
    }

    return 0.0f;
}

double SymbolTable_GetDouble(SymbolTable_t* symbolTable, char* name)
{
    // Do a search in the tree for the current key ('name')
    struct rb_node* found = rb_tree_search(symbolTable->variables, (void*)name, rb_node_search_cmp);

    VariableSymbol_t* variable = (VariableSymbol_t*)found->variable_symbol;

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = variable->Value;
        if (variable->ValueType != VAR_Undefined && variable->ValueType == VAR_Char)
        {
            double* dfPtr = (double*)objValue;
            double dVal = *dfPtr;
            return dVal;
        }
        else
        {
            //Logger.Log.LogError("double can not be parsed from '" + objValue + "' value.");
        }
    }

    return 0.0f;
}