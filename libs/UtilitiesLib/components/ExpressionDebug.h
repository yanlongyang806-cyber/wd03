#pragma once
GCC_SYSTEM

typedef struct Expression	Expression;
typedef struct ExprContext	ExprContext;
typedef struct ExprVarEntry	ExprVarEntry;
typedef struct MultiVal		MultiVal;

char* exprDebugPrint(Expression* expr, ExprContext* context, int printToConsole);
char* exprDebugPrintEArray(const MultiVal* exprEArray, int len, ExprContext* context, int printToConsole);
char* exprDebug(ACMD_SENTENCE cmdBuf, Expression* expr, ExprContext* context);

int exprDebugRegressionTest(int assertOnError);

void exprContextGetVarsAsEArray(ExprContext* context, ExprVarEntry*** entries);
