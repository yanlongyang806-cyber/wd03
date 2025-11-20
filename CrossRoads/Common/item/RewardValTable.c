/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "stdtypes.h"
#include "error.h"
#include "earray.h"
#include "ResourceManager.h"
#include "RewardCommon.h"

#include "entCritter.h"

#include "RewardCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

XPdata g_XPdata;

RewardValTable* rewardvaltable_Lookup(const char *pcRank, const char *pcSubRank, RewardValueType Type)
{
	int size = eaSize(&g_XPdata.xpTables);
	int ii;
	assert(size > 0);
	for(ii = g_XPdata.typeStarts[Type]; ii < size; ii++)
	{
		if (g_XPdata.xpTables[ii]->pcRank == pcRank && 
			(g_RewardConfig.bIgnoreSubRank || g_XPdata.xpTables[ii]->pcSubRank == pcSubRank) && 
			g_XPdata.xpTables[ii]->ValType == Type && g_XPdata.xpTables[ii]->shouldIndex)
			return g_XPdata.xpTables[ii];
	}
	return NULL;
}


AUTO_STARTUP(RewardValTables) ASTRT_DEPS(CritterRanks RewardsCommon);
void rewardvaltable_Load(void)
{
	int size, ii;
	ResourceIterator iter;
	RewardValTable* theTable;

	resLoadResourcesFromDisk(g_hRewardValTableDict, "defs/rewards", "xptables.data", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	resInitIterator(g_hRewardValTableDict, &iter);

	g_XPdata.typeStarts[0] = -1;
	g_XPdata.typeStarts[1] = -1;
	g_XPdata.typeStarts[2] = -1;
	g_XPdata.typeStarts[3] = -1;
	size = resDictGetNumberOfObjects(g_hRewardValTableDict);
	eaSetSize(&g_XPdata.xpTables, size);
	for (ii = 0; ii < size; ii++)
	{
		resIteratorGetNext(&iter, NULL, &theTable);
		g_XPdata.xpTables[ii] = theTable;
		if (g_XPdata.typeStarts[0] == -1 && g_XPdata.xpTables[ii]->ValType == kRewardValueType_XP)
			g_XPdata.typeStarts[0] = ii;
		else if (g_XPdata.typeStarts[1] == -1 && g_XPdata.xpTables[ii]->ValType == kRewardValueType_RP)
			g_XPdata.typeStarts[1] = ii;
		else if (g_XPdata.typeStarts[2] == -1 && g_XPdata.xpTables[ii]->ValType == kRewardValueType_Res)
			g_XPdata.typeStarts[2] = ii;
		else if (g_XPdata.typeStarts[3] == -1 && g_XPdata.xpTables[ii]->ValType == kRewardValueType_Star)
			g_XPdata.typeStarts[3] = ii;
		if (theTable->pcRank && !critterRankExists(theTable->pcRank) && stricmp(theTable->pcRank, "Henchman") != 0) 
		{
			Errorf( "The XP table '%s' refers to non-existent critter rank '%s'", theTable->Name, theTable->pcRank);
		}
		if (!g_RewardConfig.bIgnoreSubRank)
		{		
			if (theTable->pcSubRank && !critterSubRankExists(theTable->pcSubRank)) 
			{
				Errorf( "The XP table '%s' refers to non-existent critter sub-rank '%s'", theTable->Name, theTable->pcSubRank);
			}
		}
	}
	resFreeIterator(&iter);
}


/* End of File */
