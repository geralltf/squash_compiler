using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace SquashC.Compiler
{
    public class FunctionSymbol
    {
        public string Name { get; private set; }
        public List<string> Parameters { get; private set; }
        // Additional properties or methods as needed

        public FunctionSymbol(string name, List<string> parameters)
        {
            Name = name;
            Parameters = parameters;
        }
    }

    public class VariableSymbol
    {
        public VarType VariableType { get; set; }
        public string Name { get; private set; }
        public object Value { get; private set; }
        // Additional properties or methods as needed

        public VariableSymbol(VarType type, string name, int value)
        {
            VariableType = type;
            Name = name;
            Value = value;
        }
        public VariableSymbol(VarType type, string name, double value)
        {
            VariableType = type;
            Name = name;
            Value = value;
        }
        public VariableSymbol(VarType type, string name, string value)
        {
            VariableType = type;
            Name = name;
            Value = value;
        }
        public int ParseInt()
        {
            if (Value != null && (VariableType == VarType.Int || VariableType == VarType.Int32))
            {
                string val = Value.ToString();
                if (!string.IsNullOrEmpty(val))
                {
                    return int.Parse(val);
                }
            }
            return 0;
        }

        public string ParseString()
        {
            if (Value != null && (VariableType == VarType.String))
            {
                return Value.ToString();
            }
            return string.Empty;
        }
    }

    public class SymbolTable
    {
        private Dictionary<string, TokenType> keywordDict;
        private Dictionary<string, VariableSymbol> variables;
        private Dictionary<string, FunctionSymbol> functions;

        public SymbolTable()
        {
            this.keywordDict = new Dictionary<string, TokenType>
        {
            { "sin", TokenType.Function },
            { "cos", TokenType.Function },
            { "tan", TokenType.Function }
        };
            variables = new Dictionary<string, VariableSymbol>();
            functions = new Dictionary<string, FunctionSymbol>();
        }

        public VariableSymbol DefineVariable(VarType type, string name, int value)
        {
            Logger.Log.LogInformation("SymbolTable.DefineVariable(): "+ name + ",type: " + type.ToString()+",value: " + value.ToString());
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, double value)
        {
            Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, string value)
        {
            Logger.Log.LogInformation("SymbolTable.DefineVariable(): " + name + ",type: " + type.ToString() + ",value: " + value.ToString());
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public void DefineFunction(string name, List<string> parameters)
        {
            Logger.Log.LogInformation("SymbolTable.DefineFunction(): " + name);
            FunctionSymbol function = new FunctionSymbol(name, parameters);
            functions[name] = function;
        }

        public bool FunctionHasKey(string name)
        {
            if (functions.ContainsKey(name))
            {
                return true;
            }
            return false;
        }
        public bool VariableHasKey(string name)
        {
            if (variables.ContainsKey(name))
            {
                return true;
            }
            return false;
        }
        public VariableSymbol LookupVariable(string name)
        {
            if (variables.ContainsKey(name))
            {
                return variables[name];
            }
            Logger.Log.LogError($"Variable '{name}' not found in symbol table.");
            //throw new Exception($"Variable '{name}' not found in symbol table.");
            return null;
        }

        public FunctionSymbol LookupFunction(string name)
        {
            if (functions.ContainsKey(name))
            {
                return functions[name];
            }
            Logger.Log.LogError($"Function '{name}' not found in symbol table.");
            //throw new Exception($"Function '{name}' not found in symbol table.");
            return null;
        }

        public int GetInt(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                int intValue;
                if (int.TryParse(strValue, out intValue))
                {
                    return intValue;
                }
                else
                {
                    Logger.Log.LogError("int can not be parsed from '" + objValue + "' value.");
                    return 0;
                }
            }
            return 0;
        }

        public long GetLong(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                long longValue;
                if (long.TryParse(strValue, out longValue))
                {
                    return longValue;
                }
                else
                {
                    Logger.Log.LogError("long can not be parsed from '" + strValue + "' value.");
                    return 0;
                }
            }
            return 0;
        }

        public string GetString(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                return strValue;
            }
            return string.Empty;
        }

        public float GetFloat(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                float floatValue;
                if (float.TryParse(strValue, out floatValue))
                {
                    return floatValue;
                }
                else
                {
                    Logger.Log.LogError("float can not be parsed from '" + strValue + "' value.");
                    return 0.0f;
                }
            }
            return 0.0f;
        }

        public double GetDouble(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                double doubleValue;
                if (double.TryParse(strValue, out doubleValue))
                {
                    return doubleValue;
                }
                else
                {
                    Logger.Log.LogError("double can not be parsed from '" + strValue + "' value.");
                    return 0.0;
                }
            }
            return 0.0f;
        }
    }
}
