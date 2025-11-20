#include "ExpressionPrivate.h"
#include "estring.h"
#include "objPath.h"
#include "ResourceManager.h"
#include "ScratchStack.h"
#include "sysutil.h"
#include "timing.h"
#include "ExpressionDebug.h"


#define STATIC_CHECKING_HYBRID 0
#define STATIC_CHECKING_ON 0

#include "ExpressionEvaluateBody.c"

// This field is set during most expression evaluation.
// It is checked inside the transaction system to avoid letting
// expressions start transactions.
static Expression *s_ActiveExpression = NULL;

void setActiveExpression(Expression* expr)
{
	s_ActiveExpression = expr;
}

Expression *getActiveExpression(void)
{
	return s_ActiveExpression;
}

void clearActiveExpression(void)
{
	s_ActiveExpression = NULL;
}


void exprEvaluate_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS)
{
	int remainingExprLen;
	if (!expr)
		return;
	PERFINFO_AUTO_START_FUNC_L2();
	setActiveExpression(expr);
	remainingExprLen = exprInitializeAndUpdateContext(expr, context);
	exprEvaluateInternal(expr->postfixEArray, context, NULL, answer, remainingExprLen, context->stack, context->stackidx, &context->stackidx, &context->instrPtr, false MEM_DBG_PARMS_CALL);
	if(answer->type != MULTIOP_CONTINUATION)
		exprPostEvalCleanup(context);
	clearActiveExpression();
	PERFINFO_AUTO_STOP_L2();
}

void exprEvaluateDeepCopyAnswer_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS)
{
	int remainingExprLen;
	if (!expr)
		return;
	PERFINFO_AUTO_START_FUNC_L2();
	setActiveExpression(expr);
	remainingExprLen = exprInitializeAndUpdateContext(expr, context);
	exprEvaluateInternal(expr->postfixEArray, context, NULL, answer, remainingExprLen, context->stack, context->stackidx, &context->stackidx, &context->instrPtr, true MEM_DBG_PARMS_CALL);
	if(answer->type != MULTIOP_CONTINUATION)
		exprPostEvalCleanup(context);
	clearActiveExpression();
	PERFINFO_AUTO_STOP_L2();
}

// only use this function with FSM as it allows transactions within expressions
void exprEvaluateWithFuncTable_dbg(Expression* expr, ExprContext* context, ExprFuncTable* funcTable, MultiVal* answer MEM_DBG_PARMS)
{
	int remainingExprLen;
	if (!expr)
		return;
	PERFINFO_AUTO_START_FUNC_L2();
	remainingExprLen = exprInitializeAndUpdateContext(expr, context);
	exprEvaluateInternal(expr->postfixEArray, context, funcTable, answer, remainingExprLen, context->stack, context->stackidx, &context->stackidx, &context->instrPtr, false MEM_DBG_PARMS_CALL);
	if(answer->type != MULTIOP_CONTINUATION)
		exprPostEvalCleanup(context);
	PERFINFO_AUTO_STOP_L2();
}

void exprEvaluateDebugStep_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS)
{
	exprInitializeAndUpdateContext(expr, context);

	exprEvaluateInternal(expr->postfixEArray, context, NULL, answer, context->instrPtr + 1, context->stack, context->stackidx, &context->stackidx, &context->instrPtr, false MEM_DBG_PARMS_CALL);
}

void exprEvaluateSubExpr_dbg(ACMD_EXPR_SUBEXPR_IN subExpr, ExprContext* origContext, ExprContext* subExprContext, MultiVal* answer, int deepCopyAnswer MEM_DBG_PARMS)
{
	MultiVal localStack[EXPR_STACK_SIZE];

	PERFINFO_AUTO_START_FUNC();

	subExprContext->curExpr = origContext->curExpr;
	subExprContext->instrPtr = (int)(subExpr->exprPtr - subExprContext->curExpr->postfixEArray);

	exprEvaluateInternal(subExprContext->curExpr->postfixEArray, subExprContext, NULL, answer, subExprContext->instrPtr + subExpr->exprSize, localStack, -1, NULL, &subExprContext->instrPtr, deepCopyAnswer MEM_DBG_PARMS_CALL);
	PERFINFO_AUTO_STOP();
}

void exprEvaluateTolerateInvalidUsage_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS)
{
	char* oldTransStr = exprCurAutoTrans;
	exprCurAutoTrans = NULL;
	exprEvaluate_dbg(expr, context, answer MEM_DBG_PARMS_CALL);
	exprCurAutoTrans = oldTransStr;
}