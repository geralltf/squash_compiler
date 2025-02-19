using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AST.CS
{
    public class BinOpAtom
    {
        public string operandValueLeft;
        public string operandValueRight;

        public BinOpAtom operandLeft; 
        public BinOpAtom operandRight;

        public OperationType operationType;

        public enum OperationType
        {
            Add,
            Subtract,
            Multiply,
            Divide,
            XOR,
            Undefined
        }
    }
}
