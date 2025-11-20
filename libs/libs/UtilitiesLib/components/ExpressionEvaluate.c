#include "ExpressionFunc.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "textparser.h"

#include "earray.h"
#include "error.h"
#include "estring.h"
#include "InfoTracker.h"
#include "math.h"
#include "mathutil.h"
#include "objPath.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"
#include "ScratchStack.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "sysutil.h"
#include "timing.h"

int DEFAULT_LATELINK_exprEvaluateRuntimeEntArrayFromLookupEntry(ParseTable* table, void* ptr, Entity** entOut)
{
	*entOut = NULL;
	return false;
}

int DEFAULT_LATELINK_exprEvaluateRuntimeEntFromEntArray(Entity** ents, ParseTable* table, Entity** entOut)
{
	*entOut = NULL;
	return false;
}
