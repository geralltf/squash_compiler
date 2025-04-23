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
}
