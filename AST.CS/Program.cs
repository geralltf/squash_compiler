namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Wello, Horld!");

            //string input = "x = sin(y) + 2 * z";

            //ExpressionCompiler compiler = new ExpressionCompiler(input);
            //compiler.CompileExpression();

            //string code = "a = 10 + (b * 5)";
            string code = "10 + (2 * 5)";
            Compiler compiler = new Compiler(code);
            ASTNode ast = compiler.Compile();
            Console.WriteLine($"Generated Abstract Syntax Tree: {ast}");

        }
    }
}

