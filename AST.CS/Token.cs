using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public enum TokenType
    {
        Number,
        Variable,
        Function,
        Operator,
        Parenthesis,
        Identifier,
        Assignment,
        Comma,
        Peroid,
        ReturnKeyword,
        VoidKeyword,
        VarKeyword,
        IntKeyword,
        DoubleKeyword,
        StringKeyword,
        StringLiteral,
        IntLiteral,
        CurleyBrace,
        SemiColon,
        Whitespace,
        EOF
    }

    public enum VarType //TODO: Array types and object types
    {
        VarAutomatic,
        Void,
        NullLiteral,
        Object,
        FunctionPointer,
        VoidPointer,
        Int,
        Short,
        Long,
        Int32,
        Int64,
        UInt32,
        UInt64,
        Double,
        Float,
        String,
        Char,
        Matrix,
        Matrix4,
        Matrix3,
        Vector,
        Vector2,
        Vector3,
        Vector4
    }

    public class Token
    {
        public TokenType Type { get; set; }
        public string Value { get; set; }
        public int Position { get; set; }
        public PreToken PreToken { get; set; }

        public Token(TokenType type, string value, int position, PreToken preToken)
        {
            Type = type;
            Value = value;
            Position = position + 1;
            PreToken = preToken;
        }
    }
}
