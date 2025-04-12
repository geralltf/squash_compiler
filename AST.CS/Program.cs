using Squash.Compiler;

namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Wello, Horld!");

            //string code = "var x=sin(1)+3*9+(2*8)+(5*6)";
            //string code = "a=sin(92,910,99)+3*9+(4*8)+(5*6)";
            //string code = "double x = 4.3;";
            //string code = "double a;\na=6.3;a=4*2";
            string code = "double a;\na=6.3*5;double b=1;b=a*3";
            //string code = "(2+1)-(3+2)*(5+8)*((6*9)+14)+7";
            //string code = "(2*1)";
            //string code = "2*1";
            //string code = "1+(13*2)";
            //string code = "a = 10 + (b * 5)";
            //string code = "10 + (2 * 5)";
            ExpressionCompiler compiler = new ExpressionCompiler(code);
            compiler.CompileExpression();
        }
    }
}

