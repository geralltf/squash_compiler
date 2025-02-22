using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AST.CS
{
    public class Assembly
    {
        public static bool Preprocessor(string inSourceFile, string includesDir, string srcDir, string objDir)
        {
            return true;
        }
        public static List<AST> Expr(string inProgramExpression)
        {
            return AST.Expr(inProgramExpression);
        }
        public static bool Compile(List<AST> programAST, out List<ASMTree> programASM)
        {
            List <ASMTree> result = new List <ASMTree>();

            foreach (AST node in programAST)
            {

            }

            programASM = result;

            return true;
        }
        public static bool Link(List<ASMTree> program, string outDir, string[] objFiles, out Assembly outAssembly, string outProgramBuildFilenameASM)
        {
            Assembly asm = new Assembly();

            outAssembly = asm;

            return true;
        }
        public static bool CodeGeneratorAssembler(ref Assembly assembly, string outProgramBuildFilenameASM, string objDir)
        {
            return true;
        }
        public static bool AssembleBinaryExecutable(Assembly inAssembly, string objDir, string inProgramBuildFilenameASM, string outProgramBuildFilenameBIN)
        {
            return true;
        }
        public static bool AssembleBinarySharedLibrary(Assembly inAssembly, string objDir, string inProgramBuildFilenameASM, string outProgramBuildFilenameBIN)
        {
            return true;
        }

        public class ASMTree // Derived from AST
        {

        }
        public class ASMConstant
        {

        }
        public class ASMReturn
        {

        }
        public class ASMFunction
        {

        }
        public class ASMFunctionPointer
        {

        }
        public class ASMStruct
        {

        }
        public class ASMIfCondition
        {

        }
        public class ASMForLoop
        {

        }
        public class ASMDoLoop
        {

        }
        public class ASMWhileLoop
        {

        }
        public class ASMProgram
        {

        }
    }
}