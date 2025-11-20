/***************************************************************************
 *     Copyright (c) 2011-2011, Aaron Brady
 *     All Rights Reserved
 *
 * Module Description:
 *
 *
 ***************************************************************************/
#include "gslPartition.h"

#include "GameServerLib.h"
#include "gslEncounter.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "GlobalTypes.h"
#include "StringCache.h"
#include "MapDescription_h_ast.h"
#include "cmdparse.h"

// Includes for hacky mapVariables debugging
#include "WorldVariable.h"
#include "WorldVariable_h_ast.h"


// Destroys an existing partition by index.  Returns the index destroyed, or -1 on failure.
AUTO_COMMAND ACMD_CATEGORY(partitions);
S32 PartitionDestroyByIdx(int iPartitionIdx, CmdContext *pContext)
{
	char *pReason = NULL;
	estrStackCreate(&pReason);
	estrPrintf(&pReason, "PartitionDestroyByIdx called by %s", StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled));
	if (partition_ExistsByIdx(iPartitionIdx)) {
		partition_DestroyByIdx(iPartitionIdx,pReason);
	} else {
		Errorf("PartitionDestroyByIdx: Couldn't find partition by index %d", iPartitionIdx);
		iPartitionIdx = -1;
	}
	estrDestroy(&pReason);
	return iPartitionIdx;
}

// Destroys the current partition for a player
AUTO_COMMAND ACMD_CATEGORY(partitions);
S32 PartitionDestroyCurrent(Entity* pEnt, CmdContext *pContext)
{
	return PartitionDestroyByIdx(entGetPartitionIdx(pEnt), pContext);
}

//dump out all partition debug logs to a file. specify the partition IDX, 0 means all
AUTO_COMMAND ACMD_CATEGORY(partitions);
void PartitionDumpLogs(int iPartitionIdx)
{
	partition_DumpPartitionLogs(iPartitionIdx);
}
	