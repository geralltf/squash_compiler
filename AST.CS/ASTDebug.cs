using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AST.CS
{
    public class ASTDebug
    {
        static List<uint> debugIDs = new List<uint>();

        public static void PrintAST(AST ast, int currentDepth = 0)
        {
            if(debugIDs.Exists(ID => ID == ast.ID))
            {
                return;
            }

            debugIDs.Add(ast.ID);

            string indentation = string.Empty;
            for (int d = 0; d < currentDepth; d++)
            {
                indentation += "\t";
            }

            Console.WriteLine(indentation + "--AST--");
            Console.WriteLine(indentation + "op: " + ast.operatorType.ToString() 
                + ", next: " + ast.nextOprType.ToString() 
                + ", left: " + ast.operandLeft 
                + ", right: " + ast.operandRight 
                + ", precedence: " + ast.precedence);
            
            if(ast.leftChild != null)
            {
                if (!debugIDs.Exists(ID => ID == ast.leftChild.ID))
                {
                    Console.WriteLine(indentation + "child left:");

                    PrintAST(ast.leftChild, currentDepth + 1);
                }
            }
            if (ast.rightChild != null)
            {
                if (!debugIDs.Exists(ID => ID == ast.rightChild.ID))
                {
                    Console.WriteLine(indentation + "child right:");
                    PrintAST(ast.rightChild, currentDepth + 1);
                }
            }
        }
    }
}
