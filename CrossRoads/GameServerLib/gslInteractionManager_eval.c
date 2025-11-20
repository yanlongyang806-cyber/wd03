#include "Expression.h"
#include "gslInteractionManager.h"

// Expressions for Destructible Objects
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(IsAlive) ACMD_NOTESTCLIENT;
U32 im_func_FindAllEntsWithName(ACMD_EXPR_PARTITION iPartitionIdx, const char* pchObjectName)
{
	return im_FindAllEntsWithName(iPartitionIdx, pchObjectName);
}

