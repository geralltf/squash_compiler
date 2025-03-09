namespace AST.CS
{
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Wello, Horld!");

            //List<AST> computeResult = AST.Expr("int a = 2 / 8 * (9 + 2) + 3+1;");
            // 1 + (2*3)
            // (2*3) + 1

            if(Assembly.Preprocessor("test.c", "includes", "src", "obj"))
            {
                List<AST> computeResult = Assembly.Expr("int a = 2 / 8 * (90 + 20) + 3+1;");
                List<Assembly.ASMTree> outASMComputeResult;

                if(Assembly.Compile(computeResult, out outASMComputeResult))
                {
                    Assembly asm;

                    if(Assembly.Link(outASMComputeResult, "bin", new string[] { "test.obj"}, out asm, "test.asm"))
                    {
                        if(Assembly.CodeGeneratorAssembler(ref asm, "test.asm64", "obj"))
                        {

                        }
                        else
                        {

                        }
                    }
                    else
                    {

                    }
                }
            }
            else
            {

            }

        }
    }
}

