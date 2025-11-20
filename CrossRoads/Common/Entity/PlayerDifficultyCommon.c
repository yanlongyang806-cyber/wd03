#include "error.h"
#include "Expression.h"
#include "mapstate_common.h"
#include "PlayerDifficultyCommon.h"
#include "textparser.h"
#include "WorldGrid.h"
#include "rewardCommon.h"
#include "entcritter.h"
#include "PlayerDifficultyCommon_h_ast.h"

#define PD_DEF_FILE "defs/config/PlayerDifficulties.def"
#define PD_BIN_FILE "PlayerDifficulties.bin"
#define PD_CLIENT_BIN_FILE "PlayerDifficultiesClient.bin"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("PlayerDifficulty", BUDGET_GameSystems););

// global player difficulty set
PlayerDifficultySet g_PlayerDifficultySet;

// global stash lookup by name into g_PlayerDifficultySet
StashTable g_PlayerDifficultyStash;

static int pd_Compare(const PlayerDifficulty **ppDifficulty1, const PlayerDifficulty **ppDifficulty2)
{
	if (!(*ppDifficulty1) || !(*ppDifficulty2))
		return -1;
	else
		return (*ppDifficulty1)->iIndex - (*ppDifficulty2)->iIndex;
}

// Compares two PlayerDifficultyMapData structs
// Compares first on region type, then on map name
static int pd_MapDataCompare(const PlayerDifficultyMapData **ppDiffMapData1, const PlayerDifficultyMapData **ppDiffMapData2)
{
	const char *pchMapName1 = (*ppDiffMapData1) ? (*ppDiffMapData1)->pchMapName : NULL;
	const char *pchMapName2 = (*ppDiffMapData2) ? (*ppDiffMapData2)->pchMapName : NULL;
	WorldRegionType eRegionType1 = (*ppDiffMapData1) ? (*ppDiffMapData1)->eRegionType : WRT_None;
	WorldRegionType eRegionType2 = (*ppDiffMapData2) ? (*ppDiffMapData2)->eRegionType : WRT_None;

	if (eRegionType1 > eRegionType2)
		return -1;
	else if(eRegionType1 < eRegionType2)
		return 1;
	else if (pchMapName1 && !pchMapName2)
		return -1;
	else if (pchMapName2 && !pchMapName1)
		return 1;
	else if (!pchMapName1 && !pchMapName2)
		return 0;
	else
		return strcmp(pchMapName1, pchMapName2);
}

// Compares two DropRateMultiplierStructs structs by quality
// Compares first on region type, then on map name
static int pd_DropRateMultiplierEq(const DropRateDifficultyMultiplier *ppDropRate1, const DropRateDifficultyMultiplier *ppDropRate2)
{
	if(!ppDropRate1  && !ppDropRate2)
	{
		return 1;
	}
	if(!ppDropRate1 || !ppDropRate2)
	{
		return 0;
	}
	return (ppDropRate1->eQuality == ppDropRate2->eQuality);
}

AUTO_FIXUPFUNC;
TextParserResult PlayerDifficultySetFixup(PlayerDifficultySet *pSet, enumTextParserFixupType eType, void *pExtraData)
{
	int i,j;

	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			if (IsClient()) {
				// Remove handles to data the client doesn't need at runtime
				for(i=eaSize(&pSet->peaPlayerDifficulties)-1; i>=0; --i) {
					PlayerDifficulty *pDiff = pSet->peaPlayerDifficulties[i];
					for(j=eaSize(&pDiff->peaMapSettings)-1; j>=0; --j) {
						PlayerDifficultyMapData *pData = pDiff->peaMapSettings[j];

						REMOVE_HANDLE(pData->hDeathPenaltyTable);
						REMOVE_HANDLE(pData->hPowerDef);
						REMOVE_HANDLE(pData->hRewardTable);
						REMOVE_HANDLE(pData->hSavedPetDeathPenaltyTable);
					}
				}
			}
		}
	}

	return 1;
}

/******
* This loads up all of the specified difficulty settings on disk.
******/
AUTO_STARTUP(PlayerDifficulty) ASTRT_DEPS(ItemGen);
void pd_Load(void)
{
	int i, j, k;
	int iLastIndex = 0;

	loadstart_printf("Loading PlayerDifficulties...");

	if (IsClient()) {
		ParserLoadFiles(NULL, PD_DEF_FILE, PD_CLIENT_BIN_FILE, PARSER_OPTIONALFLAG, parse_PlayerDifficultySet, &g_PlayerDifficultySet);
	} else {
		ParserLoadFiles(NULL, PD_DEF_FILE, PD_BIN_FILE, PARSER_OPTIONALFLAG, parse_PlayerDifficultySet, &g_PlayerDifficultySet);
	}

	// create the stash
	g_PlayerDifficultyStash = stashTableCreateWithStringKeys(16, StashDefault);

	// sort difficulties by ascending indexes
	eaQSort(g_PlayerDifficultySet.peaPlayerDifficulties, pd_Compare);

	// validate:
	// - no difficulty has a duplicate index
	// - all difficulties have a non-empty internal name
	// - no difficulty has a duplicate internal name
	// - all difficulties have a default map setting
	// - no map setting is specified twice
	// - no map setting has both a map name and region type specified
	// - reward table must validate as a killtable
	for (i = eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties) - 1; i >= 0; i--)
	{
		PlayerDifficulty *pDifficulty = g_PlayerDifficultySet.peaPlayerDifficulties[i];
		const char *pchLastMapName = NULL;
		bool bFoundDefault = false;
		WorldRegionType eLastRegion = WRT_None;

		if (!pDifficulty)
		{
			eaRemove(&g_PlayerDifficultySet.peaPlayerDifficulties, i);
			continue;
		}

		if (i < eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties) - 1 && pDifficulty->iIndex == iLastIndex)
		{
			Errorf("Duplicate difficulty data specified for difficulty %i.", pDifficulty->iIndex);
			StructDestroy(parse_PlayerDifficulty, pDifficulty);
			eaRemove(&g_PlayerDifficultySet.peaPlayerDifficulties, i);
			continue;
		}

		eaQSort(pDifficulty->peaMapSettings, pd_MapDataCompare);
		for (j = eaSize(&pDifficulty->peaMapSettings) - 1; j >= 0; j--)
		{
			PlayerDifficultyMapData *pDiffMapData = pDifficulty->peaMapSettings[j];

			if (!pDiffMapData)
			{
				eaRemove(&pDifficulty->peaMapSettings, j);
				continue;
			}

			if (!killreward_Validate(GET_REF(pDiffMapData->hRewardTable), PD_DEF_FILE))
			{
				continue;
			}
			if(j < eaSize(&pDifficulty->peaMapSettings) - 1)
			{
				if (pDiffMapData->pchMapName == pchLastMapName && pDiffMapData->eRegionType == WRT_None)
				{
					Errorf("Duplicate difficulty data specified for difficulty %i, map %s.", pDifficulty->iIndex, pDiffMapData->pchMapName);
					StructDestroy(parse_PlayerDifficultyMapData, pDiffMapData);
					eaRemove(&pDifficulty->peaMapSettings, j);
					continue;
				}

				if (pDiffMapData->pchMapName != pchLastMapName && pDiffMapData->eRegionType != WRT_None && pDiffMapData->eRegionType == eLastRegion)
				{
					Errorf("Duplicate difficulty data specified for difficulty %i, region %s.", pDifficulty->iIndex, StaticDefineIntRevLookup(WorldRegionTypeEnum, eLastRegion));
					StructDestroy(parse_PlayerDifficultyMapData, pDiffMapData);
					eaRemove(&pDifficulty->peaMapSettings, j);
					continue;
				}

				if (pDiffMapData->eRegionType != WRT_None && pDiffMapData->pchMapName)
				{
					Errorf("Difficulty data specifies both region and map: difficulty %i, region %s, map %s.", 
						pDifficulty->iIndex, StaticDefineIntRevLookup(WorldRegionTypeEnum, eLastRegion), pDiffMapData->pchMapName);
					StructDestroy(parse_PlayerDifficultyMapData, pDiffMapData);
					eaRemove(&pDifficulty->peaMapSettings, j);
					continue;
				}
			}

 			for(k = eaSize(&pDiffMapData->eaDropRateMultipliers)-1; k >= 0; k--)
 			{
				// Search for a duplicate.  This relies on the fact that the struct we're using to compare against is the LAST one of its quality
				// in the array, and eaFindCmp finds the FIRST struct of the quality we're searching for.
				int iFirstIdx = eaFindCmp(&pDiffMapData->eaDropRateMultipliers, pDiffMapData->eaDropRateMultipliers[k], pd_DropRateMultiplierEq);
				if(iFirstIdx != k)
				{				
					Errorf("Duplicate drop rate multiplier found: difficulty %i, region %s, map %s, drop rate quality %s.", 
					pDifficulty->iIndex, StaticDefineIntRevLookup(WorldRegionTypeEnum, eLastRegion), pDiffMapData->pchMapName,
					StaticDefineIntRevLookup(ItemGenRarityEnum, pDiffMapData->eaDropRateMultipliers[k]->eQuality));
					StructDestroy(parse_DropRateDifficultyMultiplier, pDiffMapData->eaDropRateMultipliers[k]);
					eaRemove(&pDiffMapData->eaDropRateMultipliers, k);
					continue;
				}
 			}

			if (!pDiffMapData->pchMapName && pDiffMapData->eRegionType == WRT_None)
				bFoundDefault = true;
			pchLastMapName = pDiffMapData->pchMapName;
			eLastRegion = pDiffMapData->eRegionType;
		}

		if (!bFoundDefault)
		{
			Errorf("No default map data found for difficulty %i.", pDifficulty->iIndex);
			StructDestroy(parse_PlayerDifficulty, pDifficulty);
			eaRemove(&g_PlayerDifficultySet.peaPlayerDifficulties, i);
			continue;
		}

		if (!pDifficulty->pchInternalName || !pDifficulty->pchInternalName[0])
		{
			Errorf("Internal name not specified for difficulty %i.", pDifficulty->iIndex);
			StructDestroy(parse_PlayerDifficulty, pDifficulty);
			eaRemove(&g_PlayerDifficultySet.peaPlayerDifficulties, i);
			continue;
		}

		if (!stashAddPointer(g_PlayerDifficultyStash, pDifficulty->pchInternalName, pDifficulty, false))
		{
			Errorf("Duplicate internal name of %s found for difficulty %i.", pDifficulty->pchInternalName, pDifficulty->iIndex);
			StructDestroy(parse_PlayerDifficulty, pDifficulty);
			eaRemove(&g_PlayerDifficultySet.peaPlayerDifficulties, i);
			continue;
		}


		iLastIndex = pDifficulty->iIndex;
	}

	// TODO (JDJ): support reloading?
	loadend_printf(" done (%i PlayerDifficulties).", eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties));
}

/******
* This is the condition that the server uses to check whether the player difficulty
* setting for players or teams should be allowed to change depending on the
* current map type.
******/
bool pd_MapDifficultyApplied(void)
{
	ZoneMapType eType = zmapInfoGetMapType(NULL);
	return (eType == ZMTYPE_MISSION || eType == ZMTYPE_OWNED);
}

/******
* This returns the player difficulty structure for the given difficulty value,
* NULL if not found; in the case of only one difficulty, it is always returned.
******/
PlayerDifficulty *pd_GetDifficulty(PlayerDifficultyIdx iDifficulty)
{
	int iCount, i;

	iCount = eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties);
	if (iCount == 1)
		return g_PlayerDifficultySet.peaPlayerDifficulties[0];

	for (i = 0; i < iCount; i++)
	{
		if (g_PlayerDifficultySet.peaPlayerDifficulties[i]->iIndex == iDifficulty)
			return g_PlayerDifficultySet.peaPlayerDifficulties[i];
	}

	return NULL;
}

/******
* This returns the player difficulty map data structure for the given difficulty value on
* the current map; if no difficulty setting is found, and a target region is specified, 
* it will return the default setting for that region; otherwise, returns the default setting;
* returns NULL if no difficulty data is found for the specified level index.
******/
PlayerDifficultyMapData *pd_GetDifficultyMapData(PlayerDifficultyIdx iDifficulty, const char* pchMapName, WorldRegionType eTargetRegion)
{
	PlayerDifficulty *pDifficulty = pd_GetDifficulty(iDifficulty);
	PlayerDifficultyMapData *pDefaultMapData = NULL;
	int i;

	if (!pDifficulty)
		return NULL;

	for (i = 0; i < eaSize(&pDifficulty->peaMapSettings); i++)
	{
		if (pDifficulty->peaMapSettings[i]->pchMapName == pchMapName) {
			// Map Data found
			return pDifficulty->peaMapSettings[i];
		} else if(!pDefaultMapData && !pDifficulty->peaMapSettings[i]->pchMapName && pDifficulty->peaMapSettings[i]->eRegionType == WRT_None) {
			// Default found
			pDefaultMapData = pDifficulty->peaMapSettings[i];
		} else if(eTargetRegion != WRT_None && pDifficulty->peaMapSettings[i]->eRegionType == eTargetRegion) {
			// Region Default found
			pDefaultMapData = pDifficulty->peaMapSettings[i];
		}
	}

	// Return the best default
	return pDefaultMapData;
}


/******
* This is the static checking function to ensure that a string argument matches a difficulty
* internal name.
******/
AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal pd_exprInstanceDifficultyIsCheck(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT bRet, const char *pchInternalName, ACMD_EXPR_ERRSTRING estrError)
{
	if (!pchInternalName || !pchInternalName[0])
	{
		estrPrintf(estrError, "Difficulty internal name not specified.");
		return ExprFuncReturnError;
	}

	if (!stashFindPointer(g_PlayerDifficultyStash, pchInternalName, NULL))
	{
		estrPrintf(estrError, "Could not find difficulty with internal name \"%s\".", pchInternalName);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

/******
* This expression function can be used to check the current map's difficulty.
******/
AUTO_EXPR_FUNC(ai, encounter_action, reward) ACMD_NAME(InstanceDifficultyIs) ACMD_EXPR_STATIC_CHECK(pd_exprInstanceDifficultyIsCheck);
ExprFuncReturnVal pd_exprInstanceDifficultyIs(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT bRet, const char *pchInternalName, ACMD_EXPR_ERRSTRING estrError)
{
	PlayerDifficulty *pMapDifficulty = NULL;
	PlayerDifficulty *pFoundDifficulty = NULL;

	if (!pd_MapDifficultyApplied())
	{
		*bRet = false;
		return ExprFuncReturnFinished;
	}

	pMapDifficulty = pd_GetDifficulty(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)));
	if (!pMapDifficulty)
	{
		*bRet = false;
		return ExprFuncReturnFinished;
	}

	*bRet = (stashFindPointer(g_PlayerDifficultyStash, pchInternalName, &pFoundDifficulty) && pFoundDifficulty == pMapDifficulty);
	return ExprFuncReturnFinished;
}

Message* pd_GetDifficultyDescMsg(PlayerDifficultyIdx eIndex, const char* pchMapName, WorldRegionType eTargetRegion)
{
	PlayerDifficulty *pDifficulty = NULL;
	PlayerDifficultyMapData *pMapData = NULL;
	Message* pMsg = NULL;

	pDifficulty = pd_GetDifficulty(eIndex);

	if(pDifficulty)
	{
		pMsg = GET_REF(pDifficulty->hDescription);
		if(pchMapName)
		{
			pMapData = pd_GetDifficultyMapData(eIndex, pchMapName, eTargetRegion);
			if(pMapData && GET_REF(pMapData->hDescription))
			{
				pMsg = GET_REF(pMapData->hDescription);
			}
		}
	}

	return pMsg;
}

Message* pd_GetDifficultyNameMsg(PlayerDifficultyIdx eIndex, const char* pchMapName, WorldRegionType eTargetRegion)
{
	PlayerDifficulty *pDifficulty = NULL;
	PlayerDifficultyMapData *pMapData = NULL;
	Message* pMsg = NULL;

	pDifficulty = pd_GetDifficulty(eIndex);

	if(pDifficulty)
	{
		pMsg = GET_REF(pDifficulty->hName);
		if(pchMapName)
		{
			pMapData = pd_GetDifficultyMapData(eIndex, pchMapName, eTargetRegion);
			if(pMapData && GET_REF(pMapData->hName))
			{
				pMsg = GET_REF(pMapData->hName);
			}
		}
	}

	return pMsg;
}

// return the player difficulty index from the internal name
PlayerDifficultyIdx pd_GetPlayerDifficultyIndexFromName(const char *pchInternalName)
{
	PlayerDifficulty *pFoundDifficulty = NULL;

	if(!stashFindPointer(g_PlayerDifficultyStash, pchInternalName, &pFoundDifficulty))
	{
		return 0;
	}
	
	return pFoundDifficulty->iIndex;
}

#include "PlayerDifficultyCommon_h_ast.c"
