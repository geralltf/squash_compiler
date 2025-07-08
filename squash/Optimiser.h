#ifndef OPTIMISER_H
#define OPTIMISER_H

#include "AST.h"
#include "Token.h"

#include <stdlib.h>

void OptimiseNode(astnode_t** node, bool optimiseConstVariables);
void OptimiseFunctionBody(astnode_t** nde, bool optimiseConstVariables);
void OptimiseBinaryOperator(astnode_t** nde, bool optimiseConstVariables);

void CollapseNode(astnode_t** nde, char* result);

char* Optimiser_ApplyOperator(astnode_t* node, astnode_t* left, enum ASTNodeValueType operandLeftType, astnode_t* right, enum ASTNodeValueType operandRightType);

void Optimiser_ParseNumber(astnode_t* node);

int ApplyOperatorII(astnode_t* node, int operandLeft, int operandRight);
double ApplyOperatorID(astnode_t* node, int operandLeft, double operandRight);
float ApplyOperatorIF(astnode_t* node, int operandLeft, float operandRight);
double ApplyOperatorDI(astnode_t* node, double operandLeft, int operandRight);
double ApplyOperatorDD(astnode_t* node, double operandLeft, double operandRight);
double ApplyOperatorDF(astnode_t* node, double operandLeft, float operandRight);
float ApplyOperatorFI(astnode_t* node, float operandLeft, int operandRight);
float ApplyOperatorFD(astnode_t* node, float operandLeft, double operandRight);
float ApplyOperatorFF(astnode_t* node, float operandLeft, float operandRight);

#endif