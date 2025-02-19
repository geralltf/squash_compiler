namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Wello, Horld!");

            List<AST> computeResult = AST.Expr("int a = 2 / 8 * (9 + 2) + 3+1;");


        }
    }
}

