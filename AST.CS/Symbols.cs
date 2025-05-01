using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

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
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, double value)
        {
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, string value)
        {
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public void DefineFunction(string name, List<string> parameters)
        {
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
            throw new Exception($"Variable '{name}' not found in symbol table.");
        }

        public FunctionSymbol LookupFunction(string name)
        {
            if (functions.ContainsKey(name))
            {
                return functions[name];
            }
            throw new Exception($"Function '{name}' not found in symbol table.");
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
                    throw new Exception("int can not be parsed from '" + objValue + "' value.");
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
                    throw new Exception("long can not be parsed from '" + strValue + "' value.");
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
                    throw new Exception("float can not be parsed from '" + strValue + "' value.");
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
                    throw new Exception("double can not be parsed from '" + strValue + "' value.");
                }
            }
            return 0.0f;
        }
    }
}
