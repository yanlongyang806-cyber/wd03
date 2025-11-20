#pragma once

typedef enum TestServerExpressionOp
{
	TSE_Op_NoOp,

	TSE_Op_Arithmetic_Min, // DO NOT USE cutoff placeholder
	TSE_Op_Add,
	TSE_Op_Sub,
	TSE_Op_Mul,
	TSE_Op_Div,
	TSE_Op_Arithmetic_Max, // DO NOT USE cutoff placeholder

	TSE_Op_Boolean_Min, // DO NOT USE cutoff placeholder
	TSE_Op_And,
	TSE_Op_Or,
	TSE_Op_Not,
	TSE_Op_Boolean_Max, // DO NOT USE cutoff placeholder
} TestServerExpressionOp;

typedef struct TestServerExpression TestServerExpression;

void TestServer_NewExpression(const char *pcScope, const char *pcName, TestServerExpressionOp eOp);
void TestServer_ClearExpression(const char *pcScope, const char *pcName);
void TestServer_DestroyExpression(TestServerExpression *pExp);

TestServerExpressionOp TestServer_GetExpressionType(const char *pcScope, const char *pcName);
bool TestServer_IsArithmeticExpression(const char *pcScope, const char *pcName);
bool TestServer_IsBooleanExpression(const char *pcScope, const char *pcName);

void TestServer_EvaluateExpressions(const char *pcScope, const char *pcName);
bool TestServer_EvalArithmeticExpression(const char *pcScope, const char *pcName, TestServerExpressionOp eOp, float *fResult);
bool TestServer_EvalBooleanExpression(const char *pcScope, const char *pcName, TestServerExpressionOp eOp, bool *bResult);