using SquashC.Compiler;

namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Logger.InitLogging();

            Console.WriteLine("*-*-*-*-*-*-* SquashC Compiler *-*-*-*-*-*-*");

            string code = System.IO.File.ReadAllText("./../../../scripts/EntryPoint.c");

            SquashCompiler compiler = new SquashCompiler(code);
            compiler.CompileExpression();
        }
    }
}