﻿using Squash.Compiler;

namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Wello, Horld!");

            //string code = "var x = sin(y) + 2 * z + (2 * 8) + (5*5)";
            //string code = "double x = 4.3;";
            //string code = "double a;\nx = 6.3;\ndouble y = x * 2";
            string code = "(2+1)-(3+2)";

            ExpressionCompiler compiler = new ExpressionCompiler(code);
            compiler.CompileExpression();

            //string code = "a = 10 + (b * 5)";
            //string code = "10 + (2 * 5)";
            //Compiler compiler = new Compiler(code);
            //ASTNode ast = compiler.Compile();
            //Console.WriteLine($"Generated Abstract Syntax Tree: {ast}");

        }
    }
}

