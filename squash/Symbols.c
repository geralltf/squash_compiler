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
        int* iptr = (int*)symbol->Value;
        int i = *iptr;

        itoa(i, result, 10);
    }
    else if (symbol->ValueType == VAR_Double)
    {
        double* dptr = (double*)symbol->Value;
        double d = *dptr;

        sprintf(result, "%f", d);
    }
    else if (symbol->ValueType == VAR_Float)
    {
        float* fptr = (float*)symbol->Value;
        float f = *fptr;

        gcvt(f, 6, result);
    }
    else if (symbol->ValueType == VAR_Undefined)
    {
        return '\0';
    }
    else
    {
        return '\0';
    }
    return result;
}

int SymbolTable_RevervedKeyword_CompararatorFunc(DictionaryPair* left, DictionaryPair* right)
{
    int compareResult = 0;

    return compareResult;
}

int SymbolTable_Variable_CompararatorFunc(DictionaryPair* left, DictionaryPair* right)
{
    int compareResult = 0;

    return compareResult;
}

int SymbolTable_Function_CompararatorFunc(DictionaryPair* left, DictionaryPair* right)
{
    int compareResult = 0;

    return compareResult;
}

SymbolTable_t* SymbolTable_new()
{
    SymbolTable_t* symbols = (SymbolTable_t*)malloc(sizeof(SymbolTable_t));
    return symbols;
}

void SymbolTable_init(SymbolTable_t** symbols)
{
    SymbolTable_t* symbolTable = *symbols; // SymbolTable_new();

    symbolTable->keywordDict = DictionaryNew(SymbolTable_RevervedKeyword_CompararatorFunc);

    DictionaryPair* pairOut_sin = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* pairOut_cos = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* pairOut_tan = (DictionaryPair*)malloc(sizeof(DictionaryPair));

    DictionaryPair* pair_sin = DictionaryPairNew();
    char* key_sin = (char*)malloc(sizeof(char) * 3);
    key_sin[0] = 's';
    key_sin[1] = 'i';
    key_sin[2] = 'n';
    pair_sin->key = (void*)key_sin;
    //DictionaryInsertPair(symbolTable->keywordDict, pair_sin, &pairOut_sin);

    DictionaryPair* pair_cos = DictionaryPairNew();
    char* key_cos = (char*)malloc(sizeof(char) * 3);
    key_cos[0] = 'c';
    key_cos[1] = 'o';
    key_cos[2] = 's';
    pair_cos->key = (void*)key_cos;
    //DictionaryInsertPair(symbolTable->keywordDict, pair_cos, &pairOut_cos);

    DictionaryPair* pair_tan = DictionaryPairNew();
    char* key_tan = (char*)malloc(sizeof(char) * 3);
    key_tan[0] = 'c';
    key_tan[1] = 'o';
    key_tan[2] = 's';
    pair_tan->key = (void*)key_tan;
    //DictionaryInsertPair(symbolTable->keywordDict, pair_tan, &pairOut_tan);

    //{ "sin", TokenType.Function },
    //{ "cos", TokenType.Function },
    //{ "tan", TokenType.Function }

    symbolTable->variables = DictionaryNew(SymbolTable_Variable_CompararatorFunc);
    symbolTable->functions = DictionaryNew(SymbolTable_Function_CompararatorFunc);

    //variables = new Dictionary<string, VariableSymbol>();
    //functions = new Dictionary<string, FunctionSymbol>();
}

VariableSymbol_t* SymbolTable_DefineVariableI(SymbolTable_t* symbolTable, enum VarType type, char* name, int value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_init(&varSymbol, type, name, value);

    DictionaryPair* variable_pair_out = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* variable_pair = DictionaryPairNew();
    variable_pair->key = (void*)name;
    variable_pair->value = (void*)variable;
    variable_pair->type = VAR_Int;

    if (DictionaryInsertPair(symbolTable->variables, variable_pair, &variable_pair_out)) 
    {

    }

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableD(SymbolTable_t* symbolTable, enum VarType type, char* name, double value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_init(&varSymbol, type, name, value);

    DictionaryPair* variable_pair_out = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* variable_pair = DictionaryPairNew();
    variable_pair->key = (void*)name;
    variable_pair->value = (void*)variable;
    variable_pair->type = VAR_Double;
    if (DictionaryInsertPair(symbolTable->variables, variable_pair, &variable_pair_out))
    {

    }

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableL(SymbolTable_t* symbolTable, enum VarType type, char* name, long value)
{
    //Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_init(&varSymbol, type, name, value);

    DictionaryPair* variable_pair_out = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* variable_pair = DictionaryPairNew();
    variable_pair->key = (void*)name;
    variable_pair->value = (void*)variable;
    variable_pair->type = VAR_Long;
    if (DictionaryInsertPair(symbolTable->variables, variable_pair, &variable_pair_out))
    {

    }

    return variable;
}

VariableSymbol_t* SymbolTable_DefineVariableC(SymbolTable_t* symbolTable, enum VarType type, char* name, char* value)
{
    //if (value != NULL)
    //{
    //    Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());
    //}
    //else
    //{
    //    Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: null");
    //}
    //VariableSymbol_t* variable = VariableSymbol_init(symbolTable, type, name, value);
    //variables[name] = variable;
    //return variable;

    VariableSymbol_t* varSymbol = (VariableSymbol_t*)malloc(sizeof(VariableSymbol_t));
    VariableSymbol_t* variable = VariableSymbol_init(&varSymbol, type, name, value);

    DictionaryPair* variable_pair_out = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* variable_pair = DictionaryPairNew();
    variable_pair->key = (void*)name;
    variable_pair->value = (void*)variable;
    variable_pair->type = VAR_Char;
    if (DictionaryInsertPair(symbolTable->variables, variable_pair, &variable_pair_out))
    {

    }

    return variable;
}
FunctionSymbol_t* SymbolTable_DefineFunction(SymbolTable_t* symbolTable, char* name, list_t* parameters) // List<string> parameters
{
    //Logger.Log.LogInformation("SymbolTable.DefineFunction(): " + name);
    //FunctionSymbol function = new FunctionSymbol(name, parameters);
    //functions[name] = function;

    FunctionSymbol_t* functSymbol = (FunctionSymbol_t*)malloc(sizeof(FunctionSymbol_t));
    FunctionSymbol_t* function = FunctionSymbol_init(&functSymbol, name, parameters);

    DictionaryPair* function_pair_out = (DictionaryPair*)malloc(sizeof(DictionaryPair));
    DictionaryPair* function_pair = DictionaryPairNew();
    function_pair->key = (void*)name;
    function_pair->value = (void*)function;
    function_pair->type = VAR_Undefined;

    if (DictionaryInsertPair(symbolTable->functions, function_pair, &function_pair_out))
    {

    }

    return function;
}

bool SymbolTable_FunctionHasKey(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->functions, (void*)name);

    if (found != NULL) // ContainsKey(name)
    {
        return true;
    }
    return false;
}

bool SymbolTable_VariableHasKey(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL)//variables.ContainsKey(name))
    {
        return true;
    }
    return false;
}

VariableSymbol_t* SymbolTable_LookupVariable(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL) //variables.ContainsKey(name))
    {
        VariableSymbol_t* variable_symbol = (VariableSymbol_t*)found->value;
        return variable_symbol;

        //return variables[name];
    }
    //Logger.Log.LogError($"Variable '{name}' not found in symbol table.");
    //throw new Exception($"Variable '{name}' not found in symbol table.");
    return NULL;
}

FunctionSymbol_t* SymbolTable_LookupFunction(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->functions, (void*)name);

    if (found != NULL) //variables.ContainsKey(name))
    {
        FunctionSymbol_t* function_symbol = (FunctionSymbol_t*)found->value;
        return function_symbol;
    }

    //if (functions.ContainsKey(name))
    //{
    //    return functions[name];
    //}
    //Logger.Log.LogError($"Function '{name}' not found in symbol table.");
    //throw new Exception($"Function '{name}' not found in symbol table.");
    return NULL;
}

int SymbolTable_GetInt(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = found->value;
        if (found->type != VAR_Undefined && found->type == VAR_Int)
        {
            int* iPtr = (int*)objValue;
            int result = *iPtr;
            return result;
        }
        else 
        {
            //Logger.Log.LogError("int can not be parsed from '" + objValue + "' value.");
        }
        //string strValue = objValue.ToString();
        //int intValue;
        //if (int.TryParse(strValue, out intValue))
        //{
        //    return intValue;
        //}
        //else
        //{
        //    Logger.Log.LogError("int can not be parsed from '" + objValue + "' value.");
        //    return 0;
        //}
    }
    return 0;
}

long SymbolTable_GetLong(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL) //symbolTable->variables.ContainsKey(name))
    {
        //object objValue = variables[name].Value;
        void* objValue = found->value;
        if (found->type != VAR_Undefined && found->type == VAR_Long)
        {
            long* iPtr = (long*)objValue;
            long result = *iPtr;
            return result;
        }
        else
        {
            //Logger.Log.LogError("int can not be parsed from '" + objValue + "' value.");
        }
    }

    //if (variables.ContainsKey(name))
    //{
    //    object objValue = variables[name].Value;
    //    string strValue = objValue.ToString();
    //    long longValue;
    //    if (long.TryParse(strValue, out longValue))
    //    {
    //        return longValue;
    //    }
    //    else
    //    {
    //        Logger.Log.LogError("long can not be parsed from '" + strValue + "' value.");
    //        return 0;
    //    }
    //}
    return 0;
}

char* SymbolTable_GetString(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL)
    {
        void* objValue = found->value;
        if (found->type != VAR_Undefined && found->type == VAR_Char)
        {
            char* cPtr = (char*)objValue;
            
            return cPtr;
        }
        else
        {
            //Logger.Log.LogError("char* can not be parsed from '" + objValue + "' value.");
        }
    }
    return '\0';

    //if (variables.ContainsKey(name))
    //{
    //    object objValue = variables[name].Value;
    //    string strValue = objValue.ToString();
    //    return strValue;
    //}
    //return string.Empty;
}

float SymbolTable_GetFloat(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL)
    {
        void* objValue = found->value;
        if (found->type != VAR_Undefined && found->type == VAR_Float)
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

    //if (variables.ContainsKey(name))
    //{
    //    object objValue = variables[name].Value;
    //    string strValue = objValue.ToString();
    //    float floatValue;
    //    if (float.TryParse(strValue, out floatValue))
    //    {
    //        return floatValue;
    //    }
    //    else
    //    {
    //        Logger.Log.LogError("float can not be parsed from '" + strValue + "' value.");
    //        return 0.0f;
    //    }
    //}
    //return 0.0f;
}

double SymbolTable_GetDouble(SymbolTable_t* symbolTable, char* name)
{
    DictionaryPair* found;

    found = DictionaryGetPair(symbolTable->variables, (void*)name);

    if (found != NULL)
    {
        void* objValue = found->value;
        if (found->type != VAR_Undefined && found->type == VAR_Float)
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
    //if (variables.ContainsKey(name))
    //{
    //    object objValue = variables[name].Value;
    //    string strValue = objValue.ToString();
    //    double doubleValue;
    //    if (double.TryParse(strValue, out doubleValue))
    //    {
    //        return doubleValue;
    //    }
    //    else
    //    {
    //        Logger.Log.LogError("double can not be parsed from '" + strValue + "' value.");
    //        return 0.0;
    //    }
    //}
    return 0.0f;
}