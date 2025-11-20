#include "ExpressionDebug.h"

#include "ExpressionPrivate.h"

#include "BlockEarray.h"
#include "earray.h"
#include "EString.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

char* exprDebugPrint(Expression* expr, ExprContext* context, int printToConsole)
{
	int i, n = beaSize(&expr->postfixEArray);
	static char* estr = NULL;

	if(!n)
	{
		estrPrintf(&estr, "Expression is empty\n");
		return estr;
	}

	estrPrintf(&estr, "Original Expression: %s\n", exprGetCompleteString(expr));

	exprPrintMultiVals(expr, &estr);

	estrConcatf(&estr, "\nRemaining Expr: ");

	for(i = context->instrPtr; i < n; i++)
		estrConcatf(&estr, "%s ", MultiValPrint(&expr->postfixEArray[i]));

	estrConcatf(&estr, "\nStack: ");

	for(i = 0; i <= context->stackidx; i++)
		estrConcatf(&estr, "%s ", MultiValPrint(&context->stack[i]));

	if(printToConsole)
		printf("%s\n", estr);

	return estr;
}

char* exprDebugPrintEArray(const MultiVal* exprEArray, int len, ExprContext* context, int printToConsole)
{
	int i, j;
	static char* estr = NULL;
	int* jumps = NULL;

	if(!len)
	{
		estrPrintf(&estr, "Expression is empty\n");
		return estr;
	}

	estrClear(&estr);

	for(i = 0; i < len; i++)
	{
		if(exprEArray[i].type == MULTIOP_JUMPIFZERO ||
			exprEArray[i].type == MULTIOP_JUMP)
		{
			eaiPush(&jumps, exprEArray[i].intval);
		}
	}

	estrPrintf(&estr, "\nExpression: ");

	for(i = 0; i < len; i++)
	{
		estrConcatf(&estr, "%s", i == context->instrPtr ? ">>" : "  ");
		for(j = eaiSize(&jumps)-1; j >= 0; j--)
		{
			if(jumps[j] == i)
			{
				estrConcatf(&estr, "[%d:]", i);
				break;
			}
		}
		estrConcatf(&estr, "%s\n", MultiValPrint(&exprEArray[i]));
	}

	estrConcatf(&estr, "\nRemaining Expr: ");

	for(i = context->instrPtr; i < len; i++)
	{
		estrConcatf(&estr, "%s", i == context->instrPtr ? ">>" : "  ");
		for(j = eaiSize(&jumps)-1; j >= 0; j--)
		{
			if(jumps[j] == i)
			{
				estrConcatf(&estr, "[%d:]", i);
				break;
			}
		}
		estrConcatf(&estr, "%s\n", MultiValPrint(&exprEArray[i]));
	}

	estrConcatf(&estr, "\nStack: ");

	for(i = 0; i <= context->stackidx; i++)
		estrConcatf(&estr, "%s\n", MultiValPrint(&context->stack[i]));

	if(printToConsole)
		printf("%s\n", estr);

	eaiDestroy(&jumps);

	return estr;
}

AUTO_ENUM;
typedef enum exprDebugTestType
{
	EXPR_DEBUG_TEST_TYPE_A,
	EXPR_DEBUG_TEST_TYPE_B,
}exprDebugTestType;

AUTO_STRUCT;
typedef struct ExprDebugPolyTestBase
{
	exprDebugTestType type; AST(POLYPARENTTYPE)
	U32 key;
}ExprDebugPolyTestBase;

AUTO_STRUCT;
typedef struct ExprDebugPolyTestInt
{
	ExprDebugPolyTestBase baseData; AST(POLYCHILDTYPE(EXPR_DEBUG_TEST_TYPE_A))
	U32 duplicateField;
}ExprDebugPolyTestInt;

AUTO_STRUCT;
typedef struct ExprDebugPolyTestFloat
{
	ExprDebugPolyTestBase baseData; AST(POLYCHILDTYPE(EXPR_DEBUG_TEST_TYPE_B))
	F32 duplicateField;
}ExprDebugPolyTestFloat;

AUTO_STRUCT;
typedef struct ExprDebugPolyTestContainer
{
	ExprDebugPolyTestBase* polyType;
}ExprDebugPolyTestContainer;

static ExprDebugPolyTestInt* polyInt;
static ExprDebugPolyTestFloat* polyFloat;
static ExprDebugPolyTestContainer* polyContainerInt;
static ExprDebugPolyTestContainer* polyContainerFloat;

typedef struct ExprDebugState
{
	ExprContext* debugContext;
	ExprContext* debugGenerateContext;

	U32 verbose : 1;
} ExprDebugState;

static ExprDebugState gExprDebugState;

#include "ExpressionDebug_c_ast.h"

void exprDebugInitDebugContexts()
{
	ExprFuncTable* funcTable = exprContextCreateFunctionTable("ExprDebugInit");
	exprContextAddFuncsToTableByTag(funcTable, "test");

	gExprDebugState.debugContext = exprContextCreate();
	// TODO: make something to override the errorf handler so that we can properly test for errors
	// getting reported without having errorfs pop up (instead of setting silenterrors)
	exprContextSetSilentErrors(gExprDebugState.debugContext, true);
	exprContextSetFuncTable(gExprDebugState.debugContext, funcTable);

	gExprDebugState.debugGenerateContext = exprContextCreate();
	exprContextSetFuncTable(gExprDebugState.debugGenerateContext, funcTable);

	polyInt = StructCreate(parse_ExprDebugPolyTestInt);
	polyFloat = StructCreate(parse_ExprDebugPolyTestFloat);
	polyContainerInt = StructCreate(parse_ExprDebugPolyTestContainer);
	polyContainerInt->polyType = (ExprDebugPolyTestBase*)polyInt;
	polyContainerFloat = StructCreate(parse_ExprDebugPolyTestContainer);
	polyContainerFloat->polyType = (ExprDebugPolyTestBase*)polyFloat;
	exprContextSetPointerVar(gExprDebugState.debugContext, "polyInt", polyInt, parse_ExprDebugPolyTestBase, true, true);
	exprContextSetPointerVar(gExprDebugState.debugContext, "polyFloat", polyFloat, parse_ExprDebugPolyTestBase, true, true);
	exprContextSetPointerVar(gExprDebugState.debugContext, "polyContainerInt", polyContainerInt, parse_ExprDebugPolyTestContainer, true, true);
	exprContextSetPointerVar(gExprDebugState.debugContext, "polyContainerFloat", polyContainerFloat, parse_ExprDebugPolyTestContainer, true, true);
	exprContextSetPointerVar(gExprDebugState.debugGenerateContext, "polyInt", NULL, parse_ExprDebugPolyTestInt, true, true);
	exprContextSetPointerVar(gExprDebugState.debugGenerateContext, "polyFloat", NULL, parse_ExprDebugPolyTestFloat, true, true);
	exprContextSetPointerVar(gExprDebugState.debugGenerateContext, "polyContainerInt", NULL, parse_ExprDebugPolyTestContainer, true, true);
	exprContextSetPointerVar(gExprDebugState.debugGenerateContext, "polyContainerFloat", NULL, parse_ExprDebugPolyTestContainer, true, true);

	exprContextSetAllowRuntimePartition(gExprDebugState.debugContext);
	exprContextSetAllowRuntimePartition(gExprDebugState.debugGenerateContext);
}

// Make an expression, copy it, and destroy it, to show a memory leak.
#include "textparser.h"
AUTO_COMMAND ACMD_CATEGORY(Debug);
void exprDebugDestroyLeak(void)
{
	Expression* expr = exprCreateFromString("1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1", __FILE__);
	if (!gExprDebugState.debugContext)
		exprDebugInitDebugContexts();
	exprGenerate(expr, gExprDebugState.debugContext);
	StructDestroy(parse_Expression, expr);
}

AUTO_EXPR_FUNC(test);
S32 exprDebugIncrement(S32 i)
{
	return i + 1;
}

AUTO_EXPR_FUNC(test);
int exprDebugTestPartition(ExprContext* context, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ERRSTRING errStr)
{
	return partition;
}

int exprDebugGenerateAndVerifyEx(const char* exprString, ExprContext* debugGenerateContext, ExprContext* debugContext, MultiVal* val, int assertOnError, int isConstant)
{
	Expression* expr = exprCreateFromString(exprString, NULL);
	MultiVal answer = {0};
	int match = false;
	int generateSuccess = exprGenerate(expr, debugGenerateContext);
	int constantVal = exprContextLastGenerateWasConstant(debugGenerateContext, expr);

	match = constantVal == isConstant;
	if(!match)
	{
		devassertmsgf(!assertOnError || match, "%s is required to return a static result but didn't", exprString);
		goto cleanup;
	}

	if(generateSuccess)
	{
		if(gExprDebugState.verbose)
			exprDebugPrint(expr, debugContext, true);
		exprEvaluate(expr, debugContext, &answer);
		match = val->type == answer.type && (val->type == MULTI_INVALID || val->intval == answer.intval);
		devassertmsgf(!assertOnError || match, "%s does not match required result", exprString);
		goto cleanup;
	}

	devassertmsgf(generateSuccess || !assertOnError, "Generation failed for %s", exprString);

cleanup:
	exprDestroy(expr);
	return match;
}

#define exprDebugGenerateAndVerify(passed, exprString, multitype, multival, isConstant)\
{\
	MultiVal val;\
	val.type = multitype;\
	switch(val.type)\
	{\
	xcase MULTI_INT:\
		val.intval = multival;\
	xcase MULTI_FLOAT:\
		val.floatval = multival;\
	xcase MULTI_INVALID:\
		break;\
	xdefault:\
		passed = false;\
	}\
	passed &= exprDebugGenerateAndVerifyEx(exprString, gExprDebugState.debugGenerateContext, gExprDebugState.debugContext, &val, assertOnError, isConstant);\
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
int exprDebugRegressionTest(int assertOnError)
{
	int passed = true;

	if(!gExprDebugState.debugContext)
		exprDebugInitDebugContexts();

	exprContextSetPartition(gExprDebugState.debugContext, 1);
	exprContextSetPartition(gExprDebugState.debugGenerateContext, 1);

	// TODO: make version of exprDebugGenerateAndVerify that only does static check and
	// verifies it fails
	exprDebugGenerateAndVerify(passed, "-1", MULTI_INT, -1, true);
	exprDebugGenerateAndVerify(passed, "1=1", MULTI_INT, 1, true);
	exprDebugGenerateAndVerify(passed, "1=0", MULTI_INT, 0, true);
	exprDebugGenerateAndVerify(passed, "1+1", MULTI_INT, 2, true);
	exprDebugGenerateAndVerify(passed, "2*3", MULTI_INT, 6, true);
	exprDebugGenerateAndVerify(passed, "6/3", MULTI_INT, 2, true);
	exprDebugGenerateAndVerify(passed, "5/2", MULTI_FLOAT, 2.5, true);
	exprDebugGenerateAndVerify(passed, "2&1", MULTI_INT, 0, true);
	exprDebugGenerateAndVerify(passed, "2|1", MULTI_INT, 3, true);
	exprDebugGenerateAndVerify(passed, "not 0 and 0", MULTI_INT, 0, true);
	exprDebugGenerateAndVerify(passed, "16mb", MULTI_INT, 16*1024*1024, true);
	polyFloat->duplicateField = 1.0f;
	polyInt->duplicateField = 1;
	exprDebugGenerateAndVerify(passed, "ExprDebugIncrement(PolyContainerInt.PolyType.DuplicateField)", MULTI_INT, 2, false);
	exprDebugGenerateAndVerify(passed, "ExprDebugIncrement(PolyContainerFloat.PolyType.DuplicateField)", MULTI_INVALID, 0, false);

	// TODO: Add static checking and validation to make sure that not setting a partition
	// makes expressions error
	// TODO add exprContextStaticCheckAllowPartition instead of having to set on SC context
	// (and entity ver)
	exprDebugGenerateAndVerify(passed, "ExprDebugTestPartition() = 1", MULTI_INT, 1, false);
	exprContextClearPartition(gExprDebugState.debugContext);
	exprContextClearPartition(gExprDebugState.debugGenerateContext);

	return passed;
}

Expression* debugExpr = NULL;
ExprContext* debugExprContext = NULL;

typedef void (*ExprDebugRuntimeCommandFunc)(Expression* expr, ExprContext* context, char** outStr);

typedef struct ExprDebugRuntimeCommand {
	char cmdChar;
	ExprDebugRuntimeCommandFunc func;
	char* funcName;
	char* helpText;
} ExprDebugRuntimeCommand;

#define DEBUG_PRINT estrPrintf(outStr, "%s\n", exprDebugPrint(expr, context, false))





// s
void exprDebugRTDebugStep(Expression* expr, ExprContext* context, char** outStr)
{
	MultiVal answer;
	exprEvaluateDebugStep(expr, context, &answer);
	DEBUG_PRINT;
}

// p
void exprDebugRTPrint(Expression* expr, ExprContext* context, char** outStr)
{
	DEBUG_PRINT;
}

// R
void exprDebugRTRegressionTest(Expression* expr, ExprContext* context, char** outStr)
{
	exprDebugRegressionTest(false);
}

// r
void exprDebugRTRun(Expression* expr, ExprContext* context, char** outStr)
{
	MultiVal answer;
	exprEvaluate(expr, context, &answer);
	estrPrintf(outStr, "\nResult: %s\n", MultiValPrint(&answer));
}

// v
void exprDebugRTToggleVerbose(Expression* expr, ExprContext* context, char** outStr)
{
	gExprDebugState.verbose = !gExprDebugState.verbose;
	estrPrintf(outStr, "ExprDebug verbose state: %s\n", gExprDebugState.verbose ? "on" : "off");
}

// ?
// body after table because it needs ARRAY_SIZE of the table itself
void exprDebugRTPrintCommandTable(Expression* expr, ExprContext* context, char** outStr);

#define EXPRDEBUG_RUNTIME_COMMAND_DEF(cmdChar, func, helpText) \
	{ cmdChar, func, #func, helpText },

ExprDebugRuntimeCommand exprDebugCmdDispatch[] =
{
	EXPRDEBUG_RUNTIME_COMMAND_DEF('s', exprDebugRTDebugStep, "Debug Step")
	EXPRDEBUG_RUNTIME_COMMAND_DEF('p', exprDebugRTPrint, "Print Debug State")
	EXPRDEBUG_RUNTIME_COMMAND_DEF('R', exprDebugRTRegressionTest, "Run Regression Test")
	EXPRDEBUG_RUNTIME_COMMAND_DEF('r', exprDebugRTRun, "Run Debug Expression")
	EXPRDEBUG_RUNTIME_COMMAND_DEF('v', exprDebugRTToggleVerbose, "Toggle Verbose Mode")
	EXPRDEBUG_RUNTIME_COMMAND_DEF('?', exprDebugRTPrintCommandTable, "Print a List of Commands")
};

void exprDebugRTPrintCommandTable(Expression* expr, ExprContext* context, char** outStr)
{
	int i;

	estrConcatf(outStr, "Command list:\n");

	for(i = 0; i < ARRAY_SIZE(exprDebugCmdDispatch); i++)
	{
		ExprDebugRuntimeCommand* cmd = &exprDebugCmdDispatch[i];
		estrConcatf(outStr, "'%c'\t", cmd->cmdChar);
		if(gExprDebugState.verbose)
			estrConcatf(outStr, "%-35s", cmd->funcName);
		estrConcatf(outStr, "%s\n", cmd->helpText);
	}
}

char* exprDebug(ACMD_SENTENCE cmdBuf, Expression* expr, ExprContext* context)
{
	static char* outStr = NULL;
	char cmdChar = 0;
	Expression* curExpr;
	ExprContext* curContext;

	if(!debugExpr)
	{
		ExprFuncTable* funcTable = exprContextCreateFunctionTable("ExprDebug");

		debugExpr = exprCreate();
		debugExprContext = exprContextCreate();

		exprContextAddFuncsToTableByTag(funcTable, "test");
		exprContextSetFuncTable(debugExprContext, funcTable);
	}

	curExpr = expr ? expr : debugExpr;
	curContext = context ? context : debugExprContext;

	estrClear(&outStr);

	if(cmdBuf[0] == '!')
		cmdChar = cmdBuf[1];
	else if(cmdBuf[1] == '\0')
		cmdChar = cmdBuf[0];

	if(cmdChar)
	{
		int i;

		for(i = ARRAY_SIZE(exprDebugCmdDispatch)-1; i >= 0; i--)
		{
			ExprDebugRuntimeCommand* cmd = &exprDebugCmdDispatch[i];
			if(cmd->cmdChar == cmdChar)
			{
				cmd->func(curExpr, curContext, &outStr);
				break;
			}
		}

		if(i < 0)
			estrPrintf(&outStr, "Invalid command %c, press ? for a list of commands\n", cmdChar);
	}
	else
	{
		exprGenerateFromString(curExpr, curContext, cmdBuf, NULL);
		estrPrintf(&outStr, "Generated\n");
	}

	return outStr;
}

#include "ExpressionDebug_c_ast.c"