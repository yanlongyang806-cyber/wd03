#include "ExpressionFunc.h"
#include "ExpressionPrivate.h"

#include "EString.h"
#include "mathutil.h"
#include "objPath.h"
#include "ResourceManager.h"
#include "ScratchStack.h"
#include "sysutil.h"
#include "timing.h"

#define STATIC_CHECKING_HYBRID 0
#define STATIC_CHECKING_ON 1

extern ExprFuncReturnVal exprCodeEvaluate_Autogen(MultiVal** args, MultiVal* retval, ExprContext* context, char** errEString, ExprFuncDesc *pFuncDesc, void *pFuncPtr);

#include "ExpressionEvaluateBody.c"



int exprEvaluateStaticCheck_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS)
{
	// all of the checking is done through #defines, so when we care more about performance this might actually
	// be a #included version of the function with the checking code
	int remainingExprLen;
	context->staticCheck = true;
	context->staticCheckError = false;
	context->scInfoStack = ScratchStackAlloc(&context->scratch, EXPR_STACK_SIZE * sizeof(ExprSCInfo));
	remainingExprLen = exprInitializeAndUpdateContext(expr, context);
	exprEvaluateInternalSC(expr->postfixEArray, context, NULL, answer, remainingExprLen, context->stack, context->stackidx, &context->stackidx, &context->instrPtr, false MEM_DBG_PARMS_CALL);
	devassertmsg(answer->type != MULTIOP_CONTINUATION, "Cannot return continuation from static check run");
	context->staticCheck = false;

	exprPostEvalCleanup(context);

	return !context->staticCheckError;
}

void exprEvaluateSubExprStaticCheck_dbg(ACMD_EXPR_SUBEXPR_IN subExpr, ExprContext* origContext, ExprContext* subExprContext, MultiVal* answer, int deepCopyAnswer MEM_DBG_PARMS)
{
	MultiVal localStack[EXPR_STACK_SIZE];

	PERFINFO_AUTO_START_FUNC();

	subExprContext->curExpr = origContext->curExpr;

	exprEvaluateInternalSC(subExpr->exprPtr, subExprContext, NULL, answer, subExpr->exprSize, localStack, -1, NULL, NULL, deepCopyAnswer MEM_DBG_PARMS_CALL);
	PERFINFO_AUTO_STOP();
}
